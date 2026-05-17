#pragma once

#include <stdint.h>
#include <stdbool.h>

#define TIME_SERVICE_DEFAULT_NTP_SERVER "pool.ntp.org"
#define TIME_SERVICE_DEFAULT_TIMEZONE "CST-8"
#define TIME_SERVICE_DEFAULT_SYNC_INTERVAL_MIN 10

void time_service_init(void);
bool time_service_is_synced(void);

void time_service_set_sync_interval(uint16_t minutes);
uint16_t time_service_get_sync_interval(void);

// Request NTP sync (triggered by wifi connected callback)
void time_service_request_sync(void);

// Periodic tick - call from Service task, handles auto re-sync
void time_service_tick(void);

// Simple timezone offset (hours only)
void time_service_set_timezone_offset(int hours);
int  time_service_get_timezone_offset(void);

// NTP server index (preset list)
#define TIME_SERVICE_NTP_SERVER_COUNT 5

void time_service_set_ntp_server_index(int index);
int  time_service_get_ntp_server_index(void);
const char* time_service_get_ntp_server_name(int index);
