/*
 * Copyright (c) 2022 Brilliant Labs Limited
 * Licensed under the MIT License
 */

/**
 * IQS620 touch controller driver.
 * @file monocle_iqs620_c
 * @author Nathan Ashelman
 * @author Shreyas Hemachandra
 * @author Georgi Beloev (2021-07-20)
 */

#include "monocle_iqs620.h"
#include "monocle_i2c.h"
#include "monocle_config.h"

#include "nrfx_gpiote.h"
#include "nrfx_systick.h"
#include "nrfx_log.h"

#define LOG NRFX_LOG_ERROR
#define CHECK(err) check(__func__, err)

// registers

#define IQS620_ID                               0x00
#define IQS620_SYS_FLAGS                        0x10
#define IQS620_GLOBAL_EVENTS                    0x11
#define IQS620_PROX_FUSION_FLAGS                0x12

#define IQS620_CHANNEL_COUNT_0_LO               0x20
#define IQS620_CHANNEL_COUNT_0_HI               0x21
#define IQS620_CHANNEL_COUNT_1_LO               0x22
#define IQS620_CHANNEL_COUNT_1_HI               0x23

#define IQS620_PROX_FUSION_0_0                  0x40
#define IQS620_PROX_FUSION_0_1                  0x41
#define IQS620_PROX_FUSION_1_0                  0x43
#define IQS620_PROX_FUSION_1_1                  0x44
#define IQS620_PROX_FUSION_2_0                  0x46
#define IQS620_PROX_FUSION_2_1                  0x47
#define IQS620_PROX_FUSION_3_0                  0x49
#define IQS620_PROX_FUSION_3_1                  0x4A

#define IQS620_PROX_THRESHOLD_0                 0x60
#define IQS620_PROX_THRESHOLD_1                 0x62
#define IQS620_PROX_THRESHOLD_2                 0x64

#define IQS620_TOUCH_THRESHOLD_0                0x61
#define IQS620_TOUCH_THRESHOLD_1                0x63
#define IQS620_TOUCH_THRESHOLD_2                0x65

#define IQS620_SYS_SETTINGS                     0xD0
#define IQS620_ACTIVE_CHANNELS                  0xD1
#define IQS620_POWER_MODE                       0xD2
#define IQS620_NORMAL_POWER_REPORT_RATE         0xD3
#define IQS620_LOW_POWER_REPORT_RATE            0xD4
#define IQS620_ULTRA_LOW_POWER_REPORT_RATE      0xD5
#define IQS620_AUTO_SWITCH_TIMER_500MS          0xD6

// bit fields

#define IQS620_SYS_FLAGS_RESET_HAPPENED         (1 << 7)
#define IQS620_SYS_FLAGS_POWER_MODE_NP          (0 << 3)
#define IQS620_SYS_FLAGS_POWER_MODE_LP          (1 << 3)
#define IQS620_SYS_FLAGS_POWER_MODE_ULP         (2 << 3)
#define IQS620_SYS_FLAGS_POWER_MODE_HALT        (3 << 3)
#define IQS620_SYS_FLAGS_ATI_BUSY               (1 << 2)
#define IQS620_SYS_FLAGS_EVENT                  (1 << 1)
#define IQS620_SYS_FLAGS_NP_UPDATE              (1 << 0)

#define IQS620_GLOBAL_EVENTS_SAR_ACTIVE         (1 << 7)
#define IQS620_GLOBAL_EVENTS_PMU                (1 << 6)
#define IQS620_GLOBAL_EVENTS_SYS                (1 << 5)
#define IQS620_GLOBAL_EVENTS_TEMP               (1 << 4)
#define IQS620_GLOBAL_EVENTS_HYST               (1 << 3)
#define IQS620_GLOBAL_EVENTS_HALL               (1 << 2)
#define IQS620_GLOBAL_EVENTS_SAR                (1 << 1)
#define IQS620_GLOBAL_EVENTS_PROX               (1 << 0)

#define IQS620_PROX_FUSION_FLAGS_CH2_T          (1 << 6)
#define IQS620_PROX_FUSION_FLAGS_CH1_T          (1 << 5)
#define IQS620_PROX_FUSION_FLAGS_CH0_T          (1 << 4)
#define IQS620_PROX_FUSION_FLAGS_CH2_P          (1 << 2)
#define IQS620_PROX_FUSION_FLAGS_CH1_P          (1 << 1)
#define IQS620_PROX_FUSION_FLAGS_CH0_P          (1 << 0)

#define IQS620_PROX_FUSION_0_CS_MODE            (0 << 6)
#define IQS620_PROX_FUSION_0_CS_RX_NONE         (0 << 0)
#define IQS620_PROX_FUSION_0_CS_RX_0            (1 << 0)
#define IQS620_PROX_FUSION_0_CS_RX_1            (2 << 0)
#define IQS620_PROX_FUSION_0_CS_RX_01           (3 << 0)

#define IQS620_PROX_FUSION_1_CAP_15PF           (0 << 6)
#define IQS620_PROX_FUSION_1_CAP_60PF           (1 << 6)
#define IQS620_PROX_FUSION_1_CHG_FREQ_DIV_1_2   (0 << 4)
#define IQS620_PROX_FUSION_1_CHG_FREQ_DIV_1_4   (1 << 4)
#define IQS620_PROX_FUSION_1_CHG_FREQ_DIV_1_8   (2 << 4)
#define IQS620_PROX_FUSION_1_CHG_FREQ_DIV_1_16  (3 << 4)
#define IQS620_PROX_FUSION_1_ATI_DISABLED       (0 << 0)
#define IQS620_PROX_FUSION_1_ATI_PARTIAL        (1 << 0)
#define IQS620_PROX_FUSION_1_ATI_SEMI_PARTIAL   (2 << 0)
#define IQS620_PROX_FUSION_1_ATI_FULL           (3 << 0)

#define IQS620_PROX_FUSION_2_ATI_BASE_75        (0 << 6)
#define IQS620_PROX_FUSION_2_ATI_BASE_100       (1 << 6)
#define IQS620_PROX_FUSION_2_ATI_BASE_150       (2 << 6)
#define IQS620_PROX_FUSION_2_ATI_BASE_200       (3 << 6)

#define IQS620_SYS_SETTINGS_SOFT_RESET          (1 << 7)
#define IQS620_SYS_SETTINGS_ACK_RESET           (1 << 6)
#define IQS620_SYS_SETTINGS_EVENT_MODE          (1 << 5)
#define IQS620_SYS_SETTINGS_4MHZ                (1 << 4)
#define IQS620_SYS_SETTINGS_COMMS_ATI           (1 << 3)
#define IQS620_SYS_SETTINGS_ATI_BAND_1_16       (1 << 2)
#define IQS620_SYS_SETTINGS_REDO_ATI            (1 << 1)
#define IQS620_SYS_SETTINGS_RESEED              (1 << 0)

#define IQS620_POWER_MODE_PWM_OUT               (1 << 7)
#define IQS620_POWER_MODE_ULP_ENABLE            (1 << 6)
#define IQS620_POWER_MODE_AUTO                  (0 << 3)
#define IQS620_POWER_MODE_NP                    (4 << 3)
#define IQS620_POWER_MODE_LP                    (5 << 3)
#define IQS620_POWER_MODE_ULP                   (6 << 3)
#define IQS620_POWER_MODE_HALT                  (7 << 3)
#define IQS620_POWER_MODE_NP_RATE_1_2           (0 << 0)
#define IQS620_POWER_MODE_NP_RATE_1_4           (1 << 0)
#define IQS620_POWER_MODE_NP_RATE_1_8           (2 << 0)
#define IQS620_POWER_MODE_NP_RATE_1_16          (3 << 0)
#define IQS620_POWER_MODE_NP_RATE_1_32          (4 << 0)
#define IQS620_POWER_MODE_NP_RATE_1_64          (5 << 0)
#define IQS620_POWER_MODE_NP_RATE_1_128         (6 << 0)
#define IQS620_POWER_MODE_NP_RATE_1_256         (7 << 0)

// values

#define IQS620_ID_VALUE                         0x410D82

// default is 0x10 = target of 512.
// target = 0x1E * 32 = 960, gives good results on MK11 Flex through
// 1mm plastic (higher value slow to react)
#define IQS620_ATI_TARGET                       0x1E

// 0=default (22), 1=most sensitive, 255=least sensitive
#define IQS620_PROX_THRESHOLD                   10         

// 0=default (27), 1=most sensitive, 255=least sensitive
#define IQS620_TOUCH_THRESHOLD                  10         

// Internal data.
static uint8_t iqs620_prox_touch_state;       

// State of the button
static uint16_t iqs620_button_status;         


/**
 * Workaround the fact taht nordic returns an ENUM instead of a simple integer.
 */
static inline void check(char const *func, nrfx_err_t err)
{
    if (err != NRFX_SUCCESS)
        LOG("%s: %s", func, NRFX_LOG_ERROR_STRING_GET(err));
}

/**
 * Configure a register with given value.
 * @param reg Address of the register.
 * @param data Value to write.
 */
static void iqs620_write_reg(uint8_t addr, uint8_t data)
{
    uint8_t buf[2] = { addr, data };
    bool ok;

    ok = i2c_write(IQS620_I2C, IQS620_ADDR, buf, sizeof(buf));
    assert(ok);
}

/**
 * Read multiple bytes from a register.
 * @param reg Address of the register.
 * @param buf Destination buffer.
 * @param len Size of this buffer, number of bytes to read.
 * @bug NRFX library returns ERR_ANACK, but still getting data in: bug in NRFX?
 */
static void iqs620_read_reg(uint8_t addr, uint8_t *buf, unsigned len)
{
    bool ok;

    // I2C write for the register address (without stop)
    ok = i2c_write_no_stop(IQS620_I2C, IQS620_ADDR, &addr, 1);
    assert(ok);

    // I2C read the data after the write.
    ok = i2c_read(IQS620_I2C, IQS620_ADDR, buf, len);
    assert(ok);
}

/**
 * Configure the IQS620 to get it ready to work.
 */
static void iqs620_configure(void)
{
    // acknowledge any pending resets, switch to event mode, comms enabled in ATI
    iqs620_write_reg(IQS620_SYS_SETTINGS, IQS620_SYS_SETTINGS_ACK_RESET |
        IQS620_SYS_SETTINGS_EVENT_MODE | IQS620_SYS_SETTINGS_COMMS_ATI);

    // enable channels 0 and 1 for capacitive prox/touch sensing
    iqs620_write_reg(IQS620_ACTIVE_CHANNELS, (1 << 1) | (1 << 0));

    // auto power mode, ULP disabled, 1/16 normal power update rate
    iqs620_write_reg(IQS620_POWER_MODE,
        IQS620_POWER_MODE_AUTO | IQS620_POWER_MODE_NP_RATE_1_16);

    // set up channel 0 to process RX 0
    iqs620_write_reg(IQS620_PROX_FUSION_0_0,
        IQS620_PROX_FUSION_0_CS_MODE | IQS620_PROX_FUSION_0_CS_RX_0);

    // set up channel 1 to process RX 1
    iqs620_write_reg(IQS620_PROX_FUSION_0_1,
        IQS620_PROX_FUSION_0_CS_MODE | IQS620_PROX_FUSION_0_CS_RX_1);

    // channel 0 cap size 15 pF, full-ATI mode
    iqs620_write_reg(IQS620_PROX_FUSION_1_0,
        IQS620_PROX_FUSION_1_CAP_15PF | IQS620_PROX_FUSION_1_CHG_FREQ_DIV_1_8 | IQS620_PROX_FUSION_1_ATI_FULL);

    // channel 1 cap size 15 pF, full-ATI mode
    iqs620_write_reg(IQS620_PROX_FUSION_1_1,
        IQS620_PROX_FUSION_1_CAP_15PF | IQS620_PROX_FUSION_1_CHG_FREQ_DIV_1_8 | IQS620_PROX_FUSION_1_ATI_FULL);

    // channel 0 cap sensing ATI base & target (default 0xD0: base=200, target=512 is not sensitive enough)
    iqs620_write_reg(IQS620_PROX_FUSION_2_0,
        // base=75, target as configured
        IQS620_PROX_FUSION_2_ATI_BASE_75 | IQS620_ATI_TARGET);

    // channel 1 cap sensing ATI base & target (default 0xD0: base=200, target=512 is not sensitive enough)
    iqs620_write_reg(IQS620_PROX_FUSION_2_1,
        // base=75, target as configured
        IQS620_PROX_FUSION_2_ATI_BASE_75 | IQS620_ATI_TARGET);

    if (IQS620_PROX_THRESHOLD != 0)
    {
        // set prox detection threshold for channels 0 and 1
        iqs620_write_reg(IQS620_PROX_THRESHOLD_0, IQS620_PROX_THRESHOLD);
        iqs620_write_reg(IQS620_PROX_THRESHOLD_1, IQS620_PROX_THRESHOLD);
    }

    if (IQS620_TOUCH_THRESHOLD != 0)
    {
        // set touch detection threshold for channels 0 and 1
        iqs620_write_reg(IQS620_TOUCH_THRESHOLD_0, IQS620_TOUCH_THRESHOLD);
        iqs620_write_reg(IQS620_TOUCH_THRESHOLD_1, IQS620_TOUCH_THRESHOLD);
    }

    // event mode, comms enabled in ATI, redo ATI
    iqs620_write_reg(IQS620_SYS_SETTINGS, IQS620_SYS_SETTINGS_EVENT_MODE |
        IQS620_SYS_SETTINGS_COMMS_ATI | IQS620_SYS_SETTINGS_REDO_ATI);
}

// OLD  NEW
  //TP   TP
  //01   00     RELEASE             (A)
  //00   01     PROX-IN             (B)
  //0X   1X     TOUCH               (C)
  //1X   01     PROX-OUT            (D)
  //1X   00     RELEASE             (E)

typedef struct
{
    bool prox;
    bool touch;
} tp_t;

#define TP(x, p, t)         { .prox = (x) & (p), .touch = (x) & (t) }

/**
 * To override by an user-provided function.
 * @param button The button related to this event.
 * @param event The event triggered for that button.
 */
//__attribute__((__weak__)) // TODO: test overriding this function
void iqs620_callback(iqs620_button_t button, iqs620_event_t event)
{
    LOG("button=0x%02X event=%02X", button, event);
}

/**
 * Helper function for iqs620_prox_touch.
 */
static void iqs620_process_events(iqs620_button_t button, tp_t oldstate, tp_t newstate)
{
    if (!oldstate.touch && newstate.touch)
    {
        // event C (touch)
        // update button_status: set button bit
        iqs620_button_status = iqs620_button_status | (1 << button);
        LOG("button_status=0x%02X.", iqs620_button_status);
    
        iqs620_callback(button, IQS620_BUTTON_DOWN);
    }
    else if (oldstate.touch && !newstate.touch)
    {
        if (newstate.prox)
        {
            // event D (prox-out)
            iqs620_callback(button, IQS620_BUTTON_PROX);
        }
        else
        {
            // event E (release)
            // update button_status: clear button bit
            iqs620_button_status = iqs620_button_status & ~(1 << button);
            LOG("release: button_status = 0x%x.", iqs620_button_status);
    
            iqs620_callback(button, IQS620_BUTTON_UP);
        }
    }
    else if (!oldstate.touch && !newstate.touch)
    {
        if (!oldstate.prox && newstate.prox)
        {
            // event B (prox-in)
            iqs620_callback(button, IQS620_BUTTON_PROX);
        }
        if (oldstate.prox && !newstate.prox)
        {
            // event A (release)
            // update button_status: clear button bit
            iqs620_button_status = iqs620_button_status & ~(1 << button);
            LOG("release: button_status = 0x%x.", iqs620_button_status);
    
            iqs620_callback(button, IQS620_BUTTON_UP);
        }
    }
}

/**
 * Process the touch events received from the chip.
 * @param proxflags Binary flags describing the state of touch/proximity.
 */
static void iqs620_prox_touch(uint8_t proxflags)
{
    // extract B0 prox/touch flags
    tp_t b0old = TP(iqs620_prox_touch_state,
        IQS620_PROX_FUSION_FLAGS_CH0_P, IQS620_PROX_FUSION_FLAGS_CH0_T);
    tp_t b0new = TP(proxflags,
        IQS620_PROX_FUSION_FLAGS_CH0_P, IQS620_PROX_FUSION_FLAGS_CH0_T);

    // process B0 events
    iqs620_process_events(IQS620_BUTTON_B0, b0old, b0new);

    // extract B1 prox/touch flags
    tp_t b1old = TP(iqs620_prox_touch_state,
        IQS620_PROX_FUSION_FLAGS_CH1_P, IQS620_PROX_FUSION_FLAGS_CH1_T);
    tp_t b1new = TP(proxflags,
        IQS620_PROX_FUSION_FLAGS_CH1_P, IQS620_PROX_FUSION_FLAGS_CH1_T);

    // process B1 events
    iqs620_process_events(IQS620_BUTTON_B1, b1old, b1new);

    // udpate state
    iqs620_prox_touch_state = proxflags;
}

// TOUCH_RDY pin high-to-low state change handler

/**
 * Handler for an event on a GPIO pin, notifying that the IQS620 is ready.
 * @param pin The pin triggering the event.
 * @param action The event triggered.
 */
static void iqs620_touch_rdy_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
    assert(IQS620_TOUCH_RDY_PIN == pin);

    uint8_t events = 0;
    iqs620_read_reg(IQS620_GLOBAL_EVENTS, &events, 1);
    LOG("events=0x%02x", events);

    if (events & IQS620_GLOBAL_EVENTS_PROX)
    {
        // read prox/touch UI status
        uint8_t proxflags = 0;
        iqs620_read_reg(IQS620_PROX_FUSION_FLAGS, &proxflags, 1);
        LOG("proxflags=0x%02X", proxflags);

        // process prox/touch events
        iqs620_prox_touch(proxflags);
    }
    if (events & IQS620_GLOBAL_EVENTS_SYS)
    {
        uint8_t sysflags = 0;
        iqs620_read_reg(IQS620_SYS_FLAGS, &sysflags, 1);
        LOG("sysflags=0x%02x", sysflags);

        if (sysflags & IQS620_SYS_FLAGS_RESET_HAPPENED) {
            LOG("reset detected, reconfiguring");
            iqs620_configure();
        }
    }
}

/**
 * Enable (or disable) the event telling that IQS620 is ready.
 */
static void iqs620_set_touch_rdy_handler(bool on)
{
    if (on)
        // enable the GPIOTE event
        nrfx_gpiote_in_event_enable(IQS620_TOUCH_RDY_PIN, true);
    else
        // disable the GPIOTE event
        nrfx_gpiote_in_event_disable(IQS620_TOUCH_RDY_PIN);
}

/**
 * Get the ID of the product number.
 */
uint32_t iqs620_get_id(void)
{
    uint8_t data[3] = {0};

    iqs620_read_reg(IQS620_ID, data, sizeof(data));
    return (data[0] << 16) | (data[1] << 8) | (data[2] << 0);
}

/**
 * Initialise the chip as well as the iqs620 instance.
 */
void iqs620_init(void)
{
    // Setup the GPIO pin for touch state interrupts.
    nrf_gpio_cfg(
        IQS620_TOUCH_RDY_PIN,
        NRF_GPIO_PIN_DIR_INPUT,
        NRF_GPIO_PIN_INPUT_CONNECT,
        NRF_GPIO_PIN_PULLUP,
        NRF_GPIO_PIN_S0S1,
        NRF_GPIO_PIN_SENSE_LOW
    );

    // Initialize the internal state.
    iqs620_prox_touch_state = 0;
    iqs620_button_status = 0;

    // Configure the TOUCH_RDY pin for high-to-low edge GPIOTE event
    nrfx_gpiote_in_config_t config = NRFX_GPIOTE_CONFIG_IN_SENSE_HITOLO(true);
    config.pull = NRF_GPIO_PIN_PULLUP;
    CHECK(nrfx_gpiote_in_init(IQS620_TOUCH_RDY_PIN, &config, iqs620_touch_rdy_handler));

    // Reset the chip and reconfigure it.

    // Disable the TOUCH_RDY event during reset.
    iqs620_set_touch_rdy_handler(false);

    // Initiate soft reset.
    iqs620_write_reg(IQS620_SYS_SETTINGS, 1 << 7);

    // Wait for IQS620 system reset completion.
    nrfx_systick_delay_ms(10);

    // Check that the chip responds correctly.
    assert(iqs620_get_id() == IQS620_ID_VALUE);

    // Enable the TOUCH_RDY event after the reset.
    iqs620_set_touch_rdy_handler(true);
}

/**
 * Wrapper that mimics the CY8CMBR3 driver for code compatibility.
 */
uint16_t iqs620_get_button_status(void)
{
    return (iqs620_button_status);
}

/**
 * Get the raw counts for tuning thresholds.
 * @param channel Sensor channel number to read the data from
 */
uint16_t iqs620_get_count(uint8_t channel)
{
    uint16_t count;
    uint8_t data[2];

    // Read 2 bytes from the base address of the channel count register
    iqs620_read_reg(channel * 2 + IQS620_CHANNEL_COUNT_0_LO, data, sizeof(data));

    count = data[1] << 8 | data[0] << 0;
    LOG("channel=%d count=%d", channel, count);
    return (count);
}
