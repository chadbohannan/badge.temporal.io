#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool setCpuFrequencyMhz(uint32_t cpu_freq_mhz);
uint32_t getCpuFrequencyMhz(void);
uint32_t getXtalFrequencyMhz(void);
uint32_t getApbFrequency(void);
#ifdef __cplusplus
}
#endif
