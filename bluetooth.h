/*
 * This file is part of the MicroPython for Monocle:
 *      https://github.com/Itsbrilliantlabs/monocle-micropython
 *
 * Authored by: Josuah Demangeon <me@josuah.net>
 *
 * ISC Licence
 *
 * Copyright © 2022 Brilliant Labs Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * Bluetooth Low Energy driver, providing the Nordic Uart Service console
 * and the custom media transfer protocol.
 */

#include "ble_gatts.h"

#define BLE_MAX_MTU_LENGTH 128

// Buffer sizes for REPL ring buffers; +45 allows a bytearray to be printed in one go.
#define RING_BUFFER_LENGTH (1024 + 45)

/**
 * @brief Ring buffers for the repl rx and tx data which goes over BLE.
 */
typedef struct
{
    uint8_t buffer[RING_BUFFER_LENGTH];
    uint16_t head;
    uint16_t tail;
} ring_buf_t;

extern uint16_t ble_negotiated_mtu;
extern ring_buf_t ble_nus_rx;
extern ring_buf_t ble_nus_tx;
extern uint16_t ble_conn_handle;
extern ble_gatts_char_handles_t ble_nus_tx_char;
extern ble_gatts_char_handles_t ble_raw_tx_char;

bool ring_full(ring_buf_t const *ring);
bool ring_empty(ring_buf_t const *ring);
void ring_push(ring_buf_t *ring, uint8_t byte);
uint8_t ring_pop(ring_buf_t *ring);