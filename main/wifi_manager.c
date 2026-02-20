#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "lwip/inet.h"
#include <string.h>

static const char *TAG = "WIFI";

static wifi_mode_state_t current_state = WIFI_STATE_NONE;
static char connected_ssid[33] = {0};
static char ip_address[16] = {0};
static bool connect_failed = false;

static wifi_scan_result_t scan_results[20];
static int scan_count = 0;
static bool scan_done = false;

static httpd_handle_t http_server = NULL;
static esp_netif_t *sta_netif = NULL;

// HTTP处理函数 - 主页
static esp_err_t http_root_handler(httpd_req_t *req)
{
    const char *html = "<!DOCTYPE html>"
        "<html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Pomodoro Timer</title>"
        "<style>body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff;}"
        "h1{color:#00ff00;}.setting{margin:15px 0;padding:15px;background:#2d2d2d;border-radius:8px;}"
        "label{display:block;margin:8px 0;}input,select{padding:10px;width:100%;max-width:300px;border-radius:4px;border:none;box-sizing:border-box;}"
        "button{padding:12px 24px;background:#00ff00;color:#000;border:none;border-radius:5px;cursor:pointer;margin-top:15px;font-size:16px;}"
        "button:hover{background:#00cc00;}</style></head>"
        "<body><h1>🍅 Pomodoro Timer Settings</h1>"
        "<form action='/save' method='post'>"
        "<div class='setting'><h3>Timer Settings</h3>"
        "<label>Work Duration (minutes): <input type='number' name='work_time' value='25' min='1' max='60'></label>"
        "<label>Short Break (minutes): <input type='number' name='short_break' value='5' min='1' max='30'></label>"
        "<label>Long Break (minutes): <input type='number' name='long_break' value='15' min='1' max='60'></label>"
        "<label>Sessions until Long Break: <input type='number' name='sessions' value='4' min='1' max='10'></label></div>"
        "<div class='setting'><h3>Display Settings</h3>"
        "<label>Brightness (0-100): <input type='number' name='brightness' value='50' min='0' max='100'></label>"
        "<label>Contrast (0-100): <input type='number' name='contrast' value='50' min='0' max='100'></label></div>"
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
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Saved</title><style>body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff;}"
        "h1{color:#00ff00;}a{color:#00ff00;}</style></head>"
        "<body><h1>✓ Settings Saved!</h1><p><a href='/'>Back to Settings</a></p></body></html>";

    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// 启动HTTP服务器
static void start_http_server(void)
{
    if (http_server) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

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

        ESP_LOGI(TAG, "HTTP server started at http://%s/", ip_address);
    }
}

// WiFi事件处理
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi disconnected");
                if (current_state == WIFI_STATE_CONNECTING) {
                    connect_failed = true;
                }
                current_state = WIFI_STATE_NONE;
                connected_ssid[0] = '\0';
                ip_address[0] = '\0';
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
                    current_state = WIFI_STATE_NONE;
                    ESP_LOGI(TAG, "Scan done, found %d APs", scan_count);
                }
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&event->ip_info.ip));
            current_state = WIFI_STATE_CONNECTED;
            ESP_LOGI(TAG, "Got IP: %s", ip_address);
            start_http_server();
        }
    }
}

void wifi_manager_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sta_netif = esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi manager initialized");
}

void wifi_manager_scan_start(void)
{
    if (current_state == WIFI_STATE_SCANNING) return;

    current_state = WIFI_STATE_SCANNING;
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
    // 先断开之前的连接
    esp_wifi_disconnect();
    
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    connect_failed = false;
    current_state = WIFI_STATE_CONNECTING;
    strncpy(connected_ssid, ssid, sizeof(connected_ssid) - 1);
    ip_address[0] = '\0';

    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI(TAG, "Connecting to: %s", ssid);
}

void wifi_manager_disconnect(void)
{
    esp_wifi_disconnect();
    current_state = WIFI_STATE_NONE;
    connected_ssid[0] = '\0';
    ip_address[0] = '\0';
}

wifi_mode_state_t wifi_manager_get_state(void)
{
    return current_state;
}

const char* wifi_manager_get_connected_ssid(void)
{
    return (current_state == WIFI_STATE_CONNECTED) ? connected_ssid : NULL;
}

bool wifi_manager_is_connected(void)
{
    return current_state == WIFI_STATE_CONNECTED;
}

const char* wifi_manager_get_ip_address(void)
{
    return ip_address[0] ? ip_address : NULL;
}

bool wifi_manager_is_connect_failed(void)
{
    return connect_failed;
}

bool wifi_manager_is_scan_done(void)
{
    return scan_done;
}
