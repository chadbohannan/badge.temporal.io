#pragma once
#include "soc/host_reg.h"
// Brown-out control register, as used by Power.cpp's brown-out guard.
#define RTC_CNTL_BROWN_OUT_REG 0x600080D4u
#define RTC_CNTL_BROWN_OUT_INT_ENA (1u << 30)
#define RTC_CNTL_BROWN_OUT_RST_ENA (1u << 31)
#define RTC_CNTL_BROWN_OUT_CLOSE_FLASH_ENA (1u << 25)
#define RTC_CNTL_BROWN_OUT_ENA (1u << 30)
