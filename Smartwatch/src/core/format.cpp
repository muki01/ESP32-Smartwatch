/*
 * format.cpp - Small text formatting helpers.
 */
#include "format.h"

#include <Arduino.h>
#include <stdio.h>

void format_thousands(uint32_t value, char *buf, size_t len) {
  if (value >= 1000000) {
    snprintf(buf, len, "%u,%03u,%03u", (unsigned)(value / 1000000), (unsigned)(value / 1000 % 1000), (unsigned)(value % 1000));
  } else if (value >= 1000) {
    snprintf(buf, len, "%u,%03u", (unsigned)(value / 1000), (unsigned)(value % 1000));
  } else {
    snprintf(buf, len, "%u", (unsigned)value);
  }
}

void format_duration(uint32_t minutes, char *buf, size_t len) {
  if (minutes >= 60) snprintf(buf, len, "%u h %u min", (unsigned)(minutes / 60), (unsigned)(minutes % 60));
  else snprintf(buf, len, "%u min", (unsigned)minutes);
}
