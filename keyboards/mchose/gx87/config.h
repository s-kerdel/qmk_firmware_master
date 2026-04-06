// Copyright 2024 Wind (@yelishang)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef WIRELESS_ENABLE
#    define LPWR_TIMEOUT RGB_MATRIX_TIMEOUT
#    define WLS_KEYBOARD_REPORT_KEYS 5

#endif

#define USB_POWER_EN_PIN A14
#define LED_POWER_EN_PIN A15

#  define BT_CABLE_PIN B8 // 充电接入时为高
#  define BT_CHARGE_PIN B9 // 充电时为低，充满时为高

#  define BT_MODE_SW_PIN C0 // 低电平时
#  define RF_MODE_SW_PIN C15 // 低电平时

/* UART */
#define UART_TX_PIN A9
#define UART_RX_PIN A10

/*
 * EEPROM is now backed by the WB32FQ95 on-chip embedded flash via QMK's
 * wear_leveling/embedded_flash driver (selected in keyboard.json). The
 * ChibiOS WB32 EFL LLD (EFLD1) is already compiled in by default and
 * HAL_USE_EFL defaults to TRUE in the ChibiOS halconf.h template, so no
 * extra halconf.h / mcuconf.h overrides are required here.
 *
 * The external SPI NOR that was previously used by wear_leveling is not
 * touched by QMK anymore. Its pin/driver defines have been removed because
 * they are dead code in this configuration; if the chip turns out to be
 * useful for something else later it can be re-added via a dedicated
 * driver rather than hidden behind the wear-leveling backing store.
 *
 * WEAR_LEVELING_LOGICAL_SIZE is intentionally NOT defined here: the EFL
 * backing-store default is (WEAR_LEVELING_BACKING_SIZE / 2), which matches
 * what this board used to set explicitly. Letting the default stand keeps
 * the config honest and removes the risk of drifting out of sync with the
 * backing-size value that now lives in keyboard.json.
 */

/* I2C Config for LED Driver */
#define SNLED27351_I2C_ADDRESS_1 0b1110100
#define SNLED27351_I2C_ADDRESS_2 0b1110111
#define I2C1_OPMODE OPMODE_I2C
#define I2C1_CLOCK_SPEED 400000 /* 400000 */

#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_KEYPRESSES
