#pragma once
// REG_READ/REG_WRITE over a small in-memory register array keyed by address.
// On the badge these are absolute peripheral addresses; on the host they would
// segfault. A register the firmware touches for the first time reads as 0.
// A new raw register access needs its reg header added under soc/.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t host_reg_read(uint32_t addr);
void host_reg_write(uint32_t addr, uint32_t value);
#ifdef __cplusplus
}
#endif
#define REG_READ(addr) host_reg_read((uint32_t)(addr))
#define REG_WRITE(addr, val) host_reg_write((uint32_t)(addr), (uint32_t)(val))
#define REG_SET_BIT(addr, mask) REG_WRITE((addr), REG_READ(addr) | (mask))
#define REG_CLR_BIT(addr, mask) REG_WRITE((addr), REG_READ(addr) & ~(uint32_t)(mask))
