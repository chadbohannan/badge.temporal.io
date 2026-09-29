#ifndef HOST_DEFINES_H
#define HOST_DEFINES_H

// Pins and sizes for the host test harness (-DHARDWARE_HOST). Mirrors
// EchoDefines.h for everything the firmware reads through the virtual pin
// model, and leaves out what the host does not model.
//
// Left out on purpose:
//   BADGE_HAS_IMU / _HAPTICS / _BATTERY_GAUGE / _SLEEP_SERVICE
//       -> the firmware's existing no-hardware code paths run instead.
//   CHG_GOOD_PIN / CHG_STAT_PIN
//       -> Power.cpp guards the charger telemetry with #if defined(...),
//          so those paths compile away and need no chosen pin level.

#define JOY_Y 12
#define JOY_X 13

#define BUTTON_RIGHT 18
#define BUTTON_DOWN  17
#define BUTTON_LEFT  0
#define BUTTON_UP    7

#define SDA_PIN 4
#define SCL_PIN 5

#define IR_RX_PIN 1
#define IR_TX_PIN 2

#define MCU_SLEEP_PIN 10
// GPIO 13 is both JOY_X and INT_PWR_PIN on the real board. The pin model keeps
// digital and analog values apart, so joystick motion never reads as a power
// interrupt.
#define INT_PWR_PIN 13
#define INT_GP_PIN 3
#define ACCEL_INT_PIN 3
#define TILT_PIN 3
#define SYSOFF_PIN 10

#define BATT_VOLTAGE_PIN 8
#define CE_PIN 11

#define MOTOR_PIN 6

#define LED_MATRIX_INTB_PIN 38
#define LED_MATRIX_AUDIO_PIN 38
#define LED_MATRIX_ENABLE_PIN 9

#define LED_MATRIX_WIDTH 8
#define LED_MATRIX_HEIGHT 8
#define LED_MATRIX_I2C_ADDRESS 0x74

#define LCD_RES_PIN 42
#define LCD_INVERTED 0

#define OLED_I2C_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#define LIS2DH12_I2C_ADDRESS 0x19

#define NAPTIME_LIGHT_SLEEP_AFTER_NO_MOTION_MS 30000UL
#define NAPTIME_DEEP_SLEEP_AFTER_NO_MOTION_MS 300000UL
#define NAPTIME_LIGHT_SLEEP_POLL_MS 1000UL

// The host models the LED matrix; the real LEDmatrix.cpp double buffer runs
// over the host Adafruit_IS31FL3731.
#define BADGE_HAS_LED_MATRIX

// BADGE_ECHO is left out: it selects the ADC battery gauge and the screens that
// read its raw values, and the host has no gauge.

#endif
