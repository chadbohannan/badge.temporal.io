#pragma once
#include <stdint.h>

#define LOW 0x0
#define HIGH 0x1

#define INPUT 0x01
#define OUTPUT 0x03
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define INPUT_PULLDOWN 0x09
#define OPEN_DRAIN 0x10
#define OUTPUT_OPEN_DRAIN 0x13

#define DISABLED 0x00
#define RISING 0x01
#define FALLING 0x02
#define CHANGE 0x03
#define ONLOW 0x04
#define ONHIGH 0x05

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*voidFuncPtr)(void);
typedef void (*voidFuncPtrArg)(void*);

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
void attachInterrupt(uint8_t pin, voidFuncPtr handler, int mode);
void attachInterruptArg(uint8_t pin, voidFuncPtrArg handler, void* arg, int mode);
void detachInterrupt(uint8_t pin);
#define digitalPinToInterrupt(p) (p)
void noInterrupts(void);
void interrupts(void);

// ADC
#define ADC_0db 0
#define ADC_2_5db 1
#define ADC_6db 2
#define ADC_11db 3
typedef int adc_attenuation_t;
int analogRead(uint8_t pin);
uint32_t analogReadMilliVolts(uint8_t pin);
void analogReadResolution(uint8_t bits);
void analogSetAttenuation(adc_attenuation_t attenuation);
void analogSetPinAttenuation(uint8_t pin, adc_attenuation_t attenuation);
void analogWrite(uint8_t pin, int value);

#ifdef __cplusplus
}
#endif
