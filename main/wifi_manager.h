#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

// WiFi模式状态
typedef enum {
    WIFI_STATE_NONE = 0,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED
} wifi_mode_state_t;

// WiFi扫描结果
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open;
} wifi_scan_result_t;

// 初始化WiFi管理器
void wifi_manager_init(void);

// 扫描WiFi网络
void wifi_manager_scan_start(void);

// 获取扫描结果数量
int wifi_manager_get_scan_count(void);

// 获取扫描结果
wifi_scan_result_t* wifi_manager_get_scan_result(int index);

// 连接到WiFi
void wifi_manager_connect(const char *ssid, const char *password);

// 断开WiFi连接
void wifi_manager_disconnect(void);

// 获取当前WiFi状态
wifi_mode_state_t wifi_manager_get_state(void);

// 获取当前连接的SSID
const char* wifi_manager_get_connected_ssid(void);

// 检查是否已连接
bool wifi_manager_is_connected(void);

// 获取本机IP地址
const char* wifi_manager_get_ip_address(void);

// 检查连接是否失败
bool wifi_manager_is_connect_failed(void);

// 检查扫描是否完成
bool wifi_manager_is_scan_done(void);

// NTP时间同步
void wifi_manager_sync_time(void);
void wifi_manager_set_ntp_interval(int minutes);
int wifi_manager_get_ntp_interval(void);
void wifi_manager_set_timezone(int tz);
int wifi_manager_get_timezone(void);

#endif
