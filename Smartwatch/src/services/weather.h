/*
 * weather.h - Weather service: current conditions, the next hours, a 5-day forecast,
 * sunrise/sunset/UV and city search (Open-Meteo), or the phone's weather through
 * Gadgetbridge. Changes are announced through subj_weather.
 */
#pragma once

#include <stdint.h>
#include <time.h>

#define WEATHER_DAYS        5
#define WEATHER_HOURS       12
#define WEATHER_SEARCH_MAX  6

struct WeatherDay {
  int16_t code;         // WMO weather code, -1 = no data
  float t_max, t_min;
  uint8_t precip;       // % probability
};

struct WeatherHour {
  int8_t hour;          // local hour 0..23, -1 = no data
  int8_t temp;          // degrees C
  int16_t code;
  uint8_t precip;       // % probability
  uint8_t is_day;
};

struct WeatherInfo {
  bool valid;
  time_t updated;
  char city[32];
  float temp, feels, wind;
  uint8_t humidity;
  int16_t code;
  bool is_day;
  WeatherDay days[WEATHER_DAYS];
  WeatherHour hours[WEATHER_HOURS];  // from the current hour on
  int16_t sunrise, sunset;           // today, minutes after local midnight, -1 = unknown
  float uv;                          // today's highest UV index, < 0 = unknown
};

struct WeatherPlace {
  char name[40];
  char region[48];      // "Berlin, Germany"
  float lat, lon;
};

enum WeatherState : int32_t { WEATHER_IDLE, WEATHER_LOADING, WEATHER_OK, WEATHER_ERROR };
enum WeatherSearchState : int32_t { WSEARCH_IDLE, WSEARCH_BUSY, WSEARCH_DONE, WSEARCH_FAILED };

void weather_init();
void weather_refresh();
const WeatherInfo *weather_info();
int weather_state();
const char *weather_error();
void weather_apply_phone(const WeatherInfo *info);
void weather_set_place(const WeatherPlace *place);  // NULL: locate automatically
bool weather_place_is_auto();
const char *weather_place_name();
bool weather_search(const char *query);
int weather_search_state();
int weather_search_count();
const WeatherPlace *weather_search_result(int index);

// Presentation of WMO codes and temperatures (faces, tiles, the Weather app).
const char *weather_text(int code);
const char *weather_icon(int code, bool day);
uint32_t weather_color(int code, bool day);
int weather_temp(float celsius);  // in the unit the user chose
