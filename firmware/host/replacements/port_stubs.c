// What the ESP32 build gets from ESP-IDF or from ports/esp32, and the host does not
// build. The pregenerated genhdr/moduledefs.h still registers machine, network,
// socket, espnow, and tls, so their module objects have to exist to link. Each is
// a stub: `import machine` succeeds, and touching any name inside it raises
// NotImplementedError saying so, rather than an AttributeError that reads like an
// app bug. Config for these modules is off on the host (REPLAY_ENABLE_*).

#include <stdlib.h>
#include "py/obj.h"
#include "py/runtime.h"

// ESP-IDF's FATFS component defines these for oofatfs (FF_USE_LFN == 3).
void *ff_memalloc(unsigned int size) { return malloc(size); }
void ff_memfree(void *p) { free(p); }

// Module-level __getattr__ (MICROPY_MODULE_GETATTR) catches every name lookup.
#define HOST_STUB_MODULE(sym, qstr, text, ...)                                           \
    static mp_obj_t sym##_getattr(mp_obj_t attr) {                                    \
        mp_raise_msg_varg(&mp_type_NotImplementedError,                               \
                          MP_ERROR_TEXT(text " has no host implementation: %q"),      \
                          mp_obj_str_get_qstr(attr));                                 \
    }                                                                                 \
    static MP_DEFINE_CONST_FUN_OBJ_1(sym##_getattr_obj, sym##_getattr);               \
    static const mp_rom_map_elem_t sym##_globals_table[] = {                          \
        {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(qstr)},                           \
        {MP_ROM_QSTR(MP_QSTR___getattr__), MP_ROM_PTR(&sym##_getattr_obj)},           \
        __VA_ARGS__                                                                   \
    };                                                                                \
    static MP_DEFINE_CONST_DICT(sym##_globals, sym##_globals_table);                  \
    const mp_obj_module_t sym = {                                                     \
        .base = {&mp_type_module},                                                    \
        .globals = (mp_obj_dict_t *)&sym##_globals,                                   \
    }

HOST_STUB_MODULE(mp_module_machine, MP_QSTR_machine, "machine");
HOST_STUB_MODULE(mp_module_network, MP_QSTR_network, "network");
HOST_STUB_MODULE(mp_module_socket, MP_QSTR_socket, "socket");
HOST_STUB_MODULE(mp_module_espnow, MP_QSTR__espnow, "espnow");
// The frozen /lib/ssl.py reads these constants at import, so they exist; the
// first real use (tls.SSLContext) then raises like the other stubs.
HOST_STUB_MODULE(mp_module_tls, MP_QSTR_tls, "ssl",
    {MP_ROM_QSTR(MP_QSTR_CERT_NONE), MP_ROM_INT(0)},
    {MP_ROM_QSTR(MP_QSTR_CERT_OPTIONAL), MP_ROM_INT(1)},
    {MP_ROM_QSTR(MP_QSTR_CERT_REQUIRED), MP_ROM_INT(2)},
    {MP_ROM_QSTR(MP_QSTR_PROTOCOL_TLS_CLIENT), MP_ROM_INT(0)},
    {MP_ROM_QSTR(MP_QSTR_PROTOCOL_TLS_SERVER), MP_ROM_INT(1)});
