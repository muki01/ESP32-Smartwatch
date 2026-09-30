// The preview's wall clock: Tuesday, 29 September 2026, 14:32:10 local time.
#pragma once
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
time_t pv_time(time_t *out);
#ifdef __cplusplus
}
#endif
#define time(x) pv_time(x)
