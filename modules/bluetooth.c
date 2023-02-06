/*
 * This file is part of the MicroPython for Monocle project:
 *      https://github.com/brilliantlabsAR/monocle-micropython
 *
 * Authored by: Josuah Demangeon (me@josuah.net)
 *              Raj Nakarja / Brilliant Labs Inc (raj@itsbrilliant.co)
 *
 * ISC Licence
 *
 * Copyright © 2023 Brilliant Labs Inc.
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

#include "py/runtime.h"
#include "bluetooth.h"

static mp_obj_t bluetooth_x(void)
{
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(bluetooth_x_obj, bluetooth_x);

STATIC const mp_rom_map_elem_t bluetooth_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__),     MP_ROM_QSTR(MP_QSTR_bluetooth)},
    {MP_ROM_QSTR(MP_QSTR_on),           MP_ROM_PTR(&bluetooth_x_obj)},
};
STATIC MP_DEFINE_CONST_DICT(bluetooth_module_globals, bluetooth_module_globals_table);

const mp_obj_module_t bluetooth_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&bluetooth_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_bluetooth, bluetooth_module);
