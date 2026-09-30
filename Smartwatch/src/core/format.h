/*
 * format.h - Small text formatting helpers shared by services and UI.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

void format_thousands(uint32_t value, char *buf, size_t len);  // 12345 -> "12,345"
void format_duration(uint32_t minutes, char *buf, size_t len);  // 95 -> "1 h 35 min", 40 -> "40 min"
