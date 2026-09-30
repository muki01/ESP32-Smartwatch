/*
 * rtc.cpp - PCF85063 real-time clock on the shared I2C bus. Stores UTC in BCD registers.
 */
#include "rtc.h"

#include <Arduino.h>
#include <Wire.h>
#include "../core/system.h"

#define PCF85063_ADDR    0x51
#define PCF85063_REG_SC  0x04  // seconds; bit 7 = oscillator stopped

static bool rtc_ok;

static uint8_t bcd2dec(uint8_t v) {
  return (v >> 4) * 10 + (v & 0x0F);
}

static uint8_t dec2bcd(uint8_t v) {
  return ((v / 10) << 4) | (v % 10);
}

// Days since 1970-01-01 for a proleptic Gregorian date (no time zone involved).
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

bool rtc_init() {
  I2CGuard lock;
  Wire.beginTransmission(PCF85063_ADDR);
  rtc_ok = Wire.endTransmission() == 0;
  return rtc_ok;
}

bool rtc_read(time_t *utc) {
  if (!rtc_ok) return false;
  uint8_t r[7];
  {
    I2CGuard lock;
    Wire.beginTransmission(PCF85063_ADDR);
    Wire.write(PCF85063_REG_SC);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)PCF85063_ADDR, (uint8_t)7) != 7) return false;
    for (uint8_t &b : r) b = Wire.read();
  }
  if (r[0] & 0x80) return false;  // oscillator stopped: the RTC lost power, the time is garbage
  int year = 2000 + bcd2dec(r[6]);
  *utc = (time_t)(days_from_civil(year, bcd2dec(r[5] & 0x1F), bcd2dec(r[3] & 0x3F)) * 86400LL +
                  bcd2dec(r[2] & 0x3F) * 3600 + bcd2dec(r[1] & 0x7F) * 60 + bcd2dec(r[0] & 0x7F));
  return true;
}

void rtc_write(time_t utc) {
  if (!rtc_ok) return;
  struct tm t;
  gmtime_r(&utc, &t);
  I2CGuard lock;
  Wire.beginTransmission(PCF85063_ADDR);
  Wire.write(PCF85063_REG_SC);
  Wire.write(dec2bcd(t.tm_sec));  // also clears the oscillator-stop flag
  Wire.write(dec2bcd(t.tm_min));
  Wire.write(dec2bcd(t.tm_hour));
  Wire.write(dec2bcd(t.tm_mday));
  Wire.write(t.tm_wday);
  Wire.write(dec2bcd(t.tm_mon + 1));
  Wire.write(dec2bcd(t.tm_year % 100));
  Wire.endTransmission();
}
