#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define TIME_SERVICE_DEFAULT_NTP_SERVER "pool.ntp.org"
#define TIME_SERVICE_DEFAULT_TIMEZONE "CST-8"
#define TIME_SERVICE_DEFAULT_SYNC_INTERVAL_MIN 10

typedef struct {
    int8_t timezone_hours;
    int8_t timezone_minutes;
    char timezone_name[16];
} timezone_info_t;

typedef struct {
    time_t timestamp;
    uint32_t millis;
    bool valid;
} current_time_t;

void time_service_init(void);

bool time_service_sync(void);

bool time_service_is_synced(void);

current_time_t time_service_get_current_time(void);

void time_service_set_timezone(int8_t hours, int8_t minutes);

timezone_info_t time_service_get_timezone(void);

void time_service_set_ntp_server(const char *server);

const char* time_service_get_ntp_server(void);

void time_service_set_sync_interval(uint16_t minutes);

uint16_t time_service_get_sync_interval(void);

void time_service_set_auto_sync(bool enable);

bool time_service_get_auto_sync(void);

char* time_service_format_time(char *buffer, size_t len, const char *format);

char* time_service_format_date(char *buffer, size_t len, const char *format);
