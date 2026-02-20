#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "lwip/inet.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "WIFI";

static wifi_mode_state_t current_mode = WIFI_STATE_NONE;
static char ap_ssid[32] = {0};
static char ap_password[9] = {0};
static char connected_ssid[33] = {0};
static char ip_address[16] = {0};
static char mac_suffix[7] = {0};

static wifi_scan_result_t scan_results[20];
static int scan_count = 0;
static bool scan_done = false;
static bool connected = false;

static httpd_handle_t http_server = NULL;
static wifi_status_cb_t status_callback = NULL;

static esp_netif_t *ap_netif = NULL;
static esp_netif_t *sta_netif = NULL;

// 生成随机密码
static void generate_random_password(char *password, int len)
{
    srand((unsigned int)time(NULL));
    for (int i = 0; i < len; i++) {
        password[i] = '0' + (rand() % 10);
    }
    password[len] = '\0';
}

// HTTP处理函数 - 主页
static esp_err_t http_root_handler(httpd_req_t *req)
{
    const char *html = "<!DOCTYPE html>"
        "<html><head><meta charset='UTF-8'>"
        "<title>Pomodoro Settings</title>"
        "<style>body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff;}"
        "h1{color:#00ff00;}.setting{margin:15px 0;padding:10px;background:#2d2d2d;border-radius:5px;}"
        "label{display:block;margin:5px 0;}input,select{padding:8px;width:200px;border-radius:3px;border:none;}"
        "button{padding:10px 20px;background:#00ff00;color:#000;border:none;border-radius:5px;cursor:pointer;margin-top:10px;}"
        "button:hover{background:#00cc00;}</style></head>"
        "<body><h1>🍅 Pomodoro Settings</h1>"
        "<form action='/save' method='post'>"
        "<div class='setting'><h3>Timer Settings</h3>"
        "<label>Work Duration (minutes): <input type='number' name='work_time' value='25' min='1' max='60'></label>"
        "<label>Short Break (minutes): <input type='number' name='short_break' value='5' min='1' max='30'></label>"
        "<label>Long Break (minutes): <input type='number' name='long_break' value='15' min='1' max='60'></label>"
        "<label>Sessions until Long Break: <input type='number' name='sessions' value='4' min='1' max='10'></label></div>"
        "<div class='setting'><h3>Display Settings</h3>"
        "<label>Brightness: <input type='range' name='brightness' value='50' min='0' max='100'></label>"
        "<label>Contrast: <input type='range' name='contrast' value='50' min='0' max='100'></label></div>"
        "<button type='submit'>Save Settings</button></form></body></html>";
    
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

// HTTP处理函数 - 保存设置
static esp_err_t http_save_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        ESP_LOGI(TAG, "Received settings: %s", buf);
    }
    
    const char *resp = "<!DOCTYPE html>"
        "<html><head><meta charset='UTF-8'>"
        "<title>Saved</title><style>body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff;}"
        "h1{color:#00ff00;}a{color:#00ff00;}</style></head>"
        "<body><h1>Settings Saved!</h1><a href='/'>Back to Settings</a></body></html>";
    
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// 启动HTTP服务器
static void start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    
    if (httpd_start(&http_server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = http_root_handler,
        };
        httpd_register_uri_handler(http_server, &root);
        
        httpd_uri_t save = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = http_save_handler,
        };
        httpd_register_uri_handler(http_server, &save);
        
        ESP_LOGI(TAG, "HTTP server started");
    }
}

// 停止HTTP服务器
static void stop_http_server(void)
{
    if (http_server) {
        httpd_stop(http_server);
        http_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}

// WiFi事件处理
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Station connected to AP");
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "Station disconnected from AP");
                break;
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "STA disconnected");
                connected = false;
                connected_ssid[0] = '\0';
                ip_address[0] = '\0';
                if (current_mode == WIFI_STATE_CONNECTING) {
                    current_mode = WIFI_STATE_STA;
                }
                if (status_callback) {
                    status_callback(current_mode, "Disconnected");
                }
                break;
            case WIFI_EVENT_SCAN_DONE:
                scan_done = true;
                {
                    wifi_event_sta_scan_done_t *scan_event = (wifi_event_sta_scan_done_t *)event_data;
                    uint16_t count = scan_event->number;
                    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * count);
                    if (ap_list) {
                        esp_wifi_scan_get_ap_records(&count, ap_list);
                        scan_count = (count > 20) ? 20 : count;
                        for (int i = 0; i < scan_count; i++) {
                            strncpy(scan_results[i].ssid, (char*)ap_list[i].ssid, 32);
                            scan_results[i].ssid[32] = '\0';
                            scan_results[i].rssi = ap_list[i].rssi;
                            scan_results[i].open = (ap_list[i].authmode == WIFI_AUTH_OPEN);
                        }
                        free(ap_list);
                    }
                    current_mode = WIFI_STATE_STA;
                    ESP_LOGI(TAG, "Scan done, found %d APs", scan_count);
                    if (status_callback) {
                        status_callback(current_mode, "Scan done");
                    }
                }
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&event->ip_info.ip));
            connected = true;
            current_mode = WIFI_STATE_STA;
            ESP_LOGI(TAG, "Got IP: %s", ip_address);
            if (status_callback) {
                status_callback(current_mode, ip_address);
            }
        }
    }
}

void wifi_manager_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 注册事件处理
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    // 获取MAC地址
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(mac_suffix, sizeof(mac_suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "WiFi manager initialized, MAC suffix: %s", mac_suffix);
}

void wifi_manager_start_ap(void)
{
    if (current_mode == WIFI_STATE_AP) return;
    
    // 停止可能的STA模式
    if (sta_netif) {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }
    
    // 生成SSID和密码
    snprintf(ap_ssid, sizeof(ap_ssid), "Pomodoro_%s", mac_suffix);
    generate_random_password(ap_password, 8);
    
    // 创建AP网络接口
    if (!ap_netif) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    wifi_config_t wifi_config = {0};
    strcpy((char*)wifi_config.ap.ssid, ap_ssid);
    strcpy((char*)wifi_config.ap.password, ap_password);
    wifi_config.ap.ssid_len = strlen(ap_ssid);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    current_mode = WIFI_STATE_AP;
    connected = true;
    
    // 获取AP IP
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(ap_netif, &ip_info);
    snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&ip_info.ip));
    
    // 启动HTTP服务器
    start_http_server();
    
    ESP_LOGI(TAG, "AP started: SSID=%s, Password=%s, IP=%s", ap_ssid, ap_password, ip_address);
    
    if (status_callback) {
        status_callback(current_mode, ap_password);
    }
}

void wifi_manager_stop_ap(void)
{
    if (current_mode != WIFI_STATE_AP) return;
    
    stop_http_server();
    esp_wifi_stop();
    current_mode = WIFI_STATE_NONE;
    connected = false;
    
    ESP_LOGI(TAG, "AP stopped");
}

void wifi_manager_scan_start(void)
{
    if (current_mode == WIFI_STATE_AP) {
        wifi_manager_stop_ap();
    }
    
    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        // 可能已经初始化过
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    current_mode = WIFI_STATE_SCANNING;
    scan_done = false;
    scan_count = 0;
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };
    
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));
    ESP_LOGI(TAG, "WiFi scan started");
    
    if (status_callback) {
        status_callback(current_mode, "Scanning...");
    }
}

int wifi_manager_get_scan_count(void)
{
    return scan_count;
}

wifi_scan_result_t* wifi_manager_get_scan_result(int index)
{
    if (index < 0 || index >= scan_count) return NULL;
    return &scan_results[index];
}

void wifi_manager_connect(const char *ssid, const char *password)
{
    if (current_mode == WIFI_STATE_AP) {
        wifi_manager_stop_ap();
    }
    
    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    current_mode = WIFI_STATE_CONNECTING;
    strncpy(connected_ssid, ssid, sizeof(connected_ssid) - 1);
    
    ESP_LOGI(TAG, "Connecting to: %s", ssid);
    
    if (status_callback) {
        status_callback(current_mode, ssid);
    }
}

void wifi_manager_disconnect(void)
{
    if (current_mode == WIFI_STATE_STA || current_mode == WIFI_STATE_CONNECTING) {
        esp_wifi_disconnect();
        connected = false;
        connected_ssid[0] = '\0';
        ip_address[0] = '\0';
    }
}

wifi_mode_state_t wifi_manager_get_mode(void)
{
    return current_mode;
}

const char* wifi_manager_get_connected_ssid(void)
{
    return connected ? connected_ssid : NULL;
}

void wifi_manager_get_ap_info(char *ssid, char *password)
{
    if (ssid) strcpy(ssid, ap_ssid);
    if (password) strcpy(password, ap_password);
}

void wifi_manager_set_status_callback(wifi_status_cb_t callback)
{
    status_callback = callback;
}

void wifi_manager_process_events(void)
{
    // 事件通过event handler处理，这里可以添加额外的轮询逻辑
}

bool wifi_manager_is_connected(void)
{
    return connected;
}

const char* wifi_manager_get_ip_address(void)
{
    return ip_address[0] ? ip_address : NULL;
}

const char* wifi_manager_get_mac_suffix(void)
{
    return mac_suffix;
}
