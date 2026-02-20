#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

// WiFi模式状态
typedef enum {
    WIFI_STATE_NONE = 0,
    WIFI_STATE_AP,           // 热点模式
    WIFI_STATE_STA,          // 站点模式
    WIFI_STATE_SCANNING,     // 扫描中
    WIFI_STATE_CONNECTING    // 连接中
} wifi_mode_state_t;

// WiFi扫描结果
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open;
} wifi_scan_result_t;

// WiFi状态回调
typedef void (*wifi_status_cb_t)(wifi_mode_state_t mode, const char *info);

// 初始化WiFi管理器
void wifi_manager_init(void);

// 启动AP模式（热点）
void wifi_manager_start_ap(void);

// 停止AP模式
void wifi_manager_stop_ap(void);

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

// 获取当前WiFi模式
wifi_mode_state_t wifi_manager_get_mode(void);

// 获取当前连接的SSID
const char* wifi_manager_get_connected_ssid(void);

// 获取AP的SSID和密码
void wifi_manager_get_ap_info(char *ssid, char *password);

// 注册状态回调
void wifi_manager_set_status_callback(wifi_status_cb_t callback);

// WiFi事件处理（在主循环中调用）
void wifi_manager_process_events(void);

// 检查是否已连接
bool wifi_manager_is_connected(void);

// 获取本机IP地址
const char* wifi_manager_get_ip_address(void);

// 获取MAC地址后3字节用于SSID
const char* wifi_manager_get_mac_suffix(void);

#endif
