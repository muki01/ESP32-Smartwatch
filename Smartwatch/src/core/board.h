/*
 * board.h - Waveshare ESP32-S3-Touch-AMOLED-1.8: pins and panel geometry.
 *
 *   SH8601 368x448 AMOLED (QSPI)   FT3168 touch (I2C)     AXP2101 PMU (I2C)
 *   ES8311 codec + mic (I2S/I2C)   PCF85063 RTC (I2C)     QMI8658 IMU (I2C)
 *   microSD (SDMMC, 1 bit)         BOOT button (GPIO0)    power key (through the PMU)
 */
#pragma once

// ---- Display (QSPI) ----------------------------------------------------------------
#define LCD_SDIO0        4
#define LCD_SDIO1        5
#define LCD_SDIO2        6
#define LCD_SDIO3        7
#define LCD_SCLK         11
#define LCD_CS           12
#define LCD_WIDTH        368
#define LCD_HEIGHT       448

// ---- Shared I2C bus: touch, PMU, RTC, motion sensor, codec -------------------------
#define I2C_SDA_PIN      15
#define I2C_SCL_PIN      14
#define I2C_FREQ_HZ      400000
#define TOUCH_INT_PIN    21

// ---- Audio: ES8311 codec over I2S, speaker amplifier -------------------------------
#define I2S_MCLK_PIN     16
#define I2S_BCLK_PIN     9
#define I2S_WS_PIN       45
#define I2S_DOUT_PIN     8   // ESP32 -> codec (speaker)
#define I2S_DIN_PIN      10  // codec -> ESP32 (microphone)
#define SPEAKER_AMP_PIN  46  // amplifier enable, active high

// ---- microSD (SDMMC, 1-bit) --------------------------------------------------------
#define SDMMC_CLK_PIN    2
#define SDMMC_CMD_PIN    1
#define SDMMC_D0_PIN     3

// ---- Buttons -----------------------------------------------------------------------
#define BOOT_BUTTON_PIN  0   // active low
