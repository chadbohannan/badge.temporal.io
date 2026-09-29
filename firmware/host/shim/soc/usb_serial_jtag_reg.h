#pragma once
#include "soc/host_reg.h"
// USB-host detection reads the SOF frame counter; it stays 0 on the host.
#define USB_SERIAL_JTAG_FRAM_NUM_REG 0x60038024u
