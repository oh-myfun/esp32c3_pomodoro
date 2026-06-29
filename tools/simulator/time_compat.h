/* Force-included via -include to provide localtime_r on Windows MinGW. */
#pragma once
#include <time.h>

#ifdef _WIN32
#ifndef SIM_LOCALTIME_R_DEFINED
#define SIM_LOCALTIME_R_DEFINED
static inline struct tm *sim_localtime_r(const time_t *timep, struct tm *result) {
    if (localtime_s(result, timep) != 0) return NULL;
    return result;
}
#define localtime_r sim_localtime_r
#endif
#endif
