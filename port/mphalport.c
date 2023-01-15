/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Glenn Ruben Bakke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "py/mpstate.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "nrfx_errors.h"
#include "nrfx_config.h"
#include "nrf_soc.h"
#include "driver/bluetooth_low_energy.h"

#if MICROPY_PY_TIME_TICKS
#include "nrfx_rtc.h"
#include "nrf_clock.h"
#endif

#if MICROPY_PY_TIME_TICKS

// Use RTC1 for time ticks generation (ms and us) with 32kHz tick resolution
// and overflow handling in RTC IRQ.

#define RTC_TICK_INCREASE_MSEC (33)

#define RTC_RESCHEDULE_CC(rtc, cc_nr, ticks) \
    do { \
        nrfx_rtc_cc_set(&rtc, cc_nr, nrfx_rtc_counter_get(&rtc) + ticks, true); \
    } while (0);

// RTC overflow irq handling notes:
// - If has_overflowed is set it could be before or after COUNTER is read.
//   If before then an adjustment must be made, if after then no adjustment is necessary.
// - The before case is when COUNTER is very small (because it just overflowed and was set to zero),
//   the after case is when COUNTER is very large (because it's just about to overflow
//   but we read it right before it overflows).
// - The extra check for counter is to distinguish these cases. 1<<23 because it's halfway
//   between min and max values of COUNTER.
#define RTC1_GET_TICKS_ATOMIC(rtc, overflows, counter) \
    do { \
        rtc.p_reg->INTENCLR = RTC_INTENCLR_OVRFLW_Msk; \
        overflows = rtc_overflows; \
        counter = rtc.p_reg->COUNTER; \
        uint32_t has_overflowed = rtc.p_reg->EVENTS_OVRFLW; \
        if (has_overflowed && counter < (1 << 23)) \
            overflows += 1; \
        rtc.p_reg->INTENSET = RTC_INTENSET_OVRFLW_Msk; \
    } while (0);

nrfx_rtc_t rtc1 = NRFX_RTC_INSTANCE(1);
volatile mp_uint_t rtc_overflows = 0;

const nrfx_rtc_config_t rtc_config_time_ticks = {
    .prescaler = 0,
    .reliable = 0,
    .tick_latency = 0,
    .interrupt_priority = 3,
};

STATIC void rtc_irq_time(nrfx_rtc_int_type_t event)
{
    // irq handler for overflow
    if (event == NRFX_RTC_INT_OVERFLOW)
        rtc_overflows += 1;
    // irq handler for wakeup from WFI (~1msec)
    if (event == NRFX_RTC_INT_COMPARE0)
        RTC_RESCHEDULE_CC(rtc1, 0, RTC_TICK_INCREASE_MSEC)
}

void rtc1_init_time_ticks(void)
{
    // Start the low-frequency clock (if it hasn't been started already)
    if (!nrf_clock_lf_is_running(NRF_CLOCK))
        nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_LFCLKSTART);
    // Uninitialize first, then set overflow IRQ and first CC event
    nrfx_rtc_uninit(&rtc1);
    nrfx_rtc_init(&rtc1, &rtc_config_time_ticks, rtc_irq_time);
    nrfx_rtc_overflow_enable(&rtc1, true);
    RTC_RESCHEDULE_CC(rtc1, 0, RTC_TICK_INCREASE_MSEC)
    nrfx_rtc_enable(&rtc1);
}

mp_uint_t mp_hal_ticks_ms(void)
{
    // Compute: (rtc_overflows << 24 + COUNTER) * 1000 / 32768
    //
    // Note that COUNTER * 1000 / 32768 would overflow during calculation, so use
    // the less obvious * 125 / 4096 calculation (overflow secure).
    //
    // Make sure not to call this function within an irq with higher prio than the
    // RTC's irq.  This would introduce the danger of preempting the RTC irq and
    // calling mp_hal_ticks_ms() at that time would return a false result.
    uint32_t overflows;
    uint32_t counter;
    // guard against overflow irq
    RTC1_GET_TICKS_ATOMIC(rtc1, overflows, counter)
    return (overflows << 9) * 1000 + (counter * 125 / 4096);
}

mp_uint_t mp_hal_ticks_us(void)
{
    // Compute: ticks_us = (overflows << 24 + counter) * 1000000 / 32768
    //    = (overflows << 15 * 15625) + (counter * 15625 / 512)
    // Since this function is likely to be called in a poll loop it must
    // be fast, using an optimized 64bit mult/divide.
    uint32_t overflows;
    uint32_t counter;
    // guard against overflow irq
    RTC1_GET_TICKS_ATOMIC(rtc1, overflows, counter)
    // first compute counter * 15625
    uint32_t counter_lo = (counter & 0xffff) * 15625;
    uint32_t counter_hi = (counter >> 16) * 15625;
    // actual value is counter_hi << 16 + counter_lo
    return ((overflows << 15) * 15625) + ((counter_hi << 7) + (counter_lo >> 9));
}

#else

mp_uint_t mp_hal_ticks_ms(void)
{
    return 0;
}

#endif

uint64_t mp_hal_time_ns(void)
{
    return 0;
}

// this table converts from HAL_StatusTypeDef to POSIX errno
const byte mp_hal_status_to_errno_table[4] = {
    [HAL_OK] = 0,
    [HAL_ERROR] = MP_EIO,
    [HAL_BUSY] = MP_EBUSY,
    [HAL_TIMEOUT] = MP_ETIMEDOUT,
};

NORETURN void mp_hal_raise(HAL_StatusTypeDef status)
{
    mp_raise_OSError(mp_hal_status_to_errno_table[status]);
}

/**
 * @brief Sends data to BLE central device.
 * @param str: String to send.
 * @param len: Length of string.
 */
void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len)
{
    ble_nus_tx(str, len);
}

void mp_hal_stdout_tx_strn_cooked(const char *str, mp_uint_t len)
{
    mp_hal_stdout_tx_strn(str, len);
}

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags)
{
    uintptr_t ret = 0;
    if ((poll_flags & MP_STREAM_POLL_RD) && ble_nus_is_rx_pending())
        ret |= MP_STREAM_POLL_RD;
    return ret;
}

/**
 * @brief Takes a single character from the received data buffer, and sends it
 *        to the micropython parser.
 */
int mp_hal_stdin_rx_chr(void)
{
    return ble_nus_rx();
}

void mp_hal_stdout_tx_str(const char *str)
{
    mp_hal_stdout_tx_strn(str, strlen(str));
}

void mp_hal_delay_us(mp_uint_t us)
{
    uint32_t now;
    if (us == 0)
        return;
    now = mp_hal_ticks_us();
    while (mp_hal_ticks_us() - now < us);
}

void mp_hal_delay_ms(mp_uint_t ms)
{
    uint32_t now;
    if (ms == 0)
        return;
    now = mp_hal_ticks_ms();
    while (mp_hal_ticks_ms() - now < ms)
        MICROPY_EVENT_POLL_HOOK
}

NORETURN void mp_hal_enter_bootloader(void)
{
    // Set the persistent memory flag telling the bootloaer to go into DFU mode.
    sd_power_gpregret_set(0, 0xB1);

    // Reset the CPU, giving controll to the bootloader.
    NVIC_SystemReset();
}
