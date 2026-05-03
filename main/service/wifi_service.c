#include "wifi_service.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "service/storage_service.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "WIFI";

#define MAX_SCAN_RESULTS 20
#define RECONNECT_DELAY_INITIAL_MS 2000
#define RECONNECT_DELAY_MAX_MS 60000

static wifi_state_t current_state = WIFI_STATE_DISCONNECTED;
static char connected_ssid[33] = {0};
static char ip_address[16] = {0};
static bool user_initiated_disconnect = false;

static wifi_ap_info_t scan_results[MAX_SCAN_RESULTS];
static int scan_count = 0;

static esp_netif_t *sta_netif = NULL;

static wifi_callbacks_t callbacks = {0};

static esp_timer_handle_t reconnect_timer = NULL;
static int reconnect_delay_ms = RECONNECT_DELAY_INITIAL_MS;

// Forward declarations
static void start_reconnect_timer(void);
static void stop_reconnect_timer(void);

static void invoke_on_connected(void)
{
    if (callbacks.on_connected) {
        callbacks.on_connected(ip_address);
    }
}

static void invoke_on_disconnected(void)
{
    if (callbacks.on_disconnected) {
        callbacks.on_disconnected();
    }
}

static void invoke_on_scan_complete(int count)
{
    if (callbacks.on_scan_complete) {
        callbacks.on_scan_complete(count);
    }
}

static void invoke_on_connect_failed(void)
{
    if (callbacks.on_connect_failed) {
        callbacks.on_connect_failed();
    }
}

static void reconnect_timer_callback(void *arg)
{
    if (current_state == WIFI_STATE_DISCONNECTED && !user_initiated_disconnect) {
        ESP_LOGI(TAG, "Auto-reconnect attempt (delay was %d ms)", reconnect_delay_ms);
        esp_wifi_connect();
        current_state = WIFI_STATE_CONNECTING;
    }
}

static void start_reconnect_timer(void)
{
    stop_reconnect_timer();

    const esp_timer_create_args_t timer_args = {
        .callback = &reconnect_timer_callback,
        .name = "wifi_reconnect"
    };
    esp_timer_create(&timer_args, &reconnect_timer);
    esp_timer_start_once(reconnect_timer, reconnect_delay_ms * 1000LL);

    // Exponential backoff: double delay, capped at max
    reconnect_delay_ms *= 2;
    if (reconnect_delay_ms > RECONNECT_DELAY_MAX_MS) {
        reconnect_delay_ms = RECONNECT_DELAY_MAX_MS;
    }
}

static void stop_reconnect_timer(void)
{
    if (reconnect_timer) {
        esp_timer_stop(reconnect_timer);
        esp_timer_delete(reconnect_timer);
        reconnect_timer = NULL;
    }
}

static void reset_reconnect_delay(void)
{
    reconnect_delay_ms = RECONNECT_DELAY_INITIAL_MS;
    stop_reconnect_timer();
}

// WiFi event handler
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
                {
                    bool was_connecting = (current_state == WIFI_STATE_CONNECTING);
                    current_state = WIFI_STATE_DISCONNECTED;
                    connected_ssid[0] = '\0';
                    ip_address[0] = '\0';

                    if (was_connecting) {
                        invoke_on_connect_failed();
                    }

                    invoke_on_disconnected();

                    // Auto-reconnect if not user-initiated
                    if (!user_initiated_disconnect) {
                        start_reconnect_timer();
                    }
                }
                break;

            case WIFI_EVENT_SCAN_DONE:
                {
                    wifi_event_sta_scan_done_t *scan_event = (wifi_event_sta_scan_done_t *)event_data;
                    uint16_t count = scan_event->number;
                    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * count);
                    if (ap_list) {
                        esp_wifi_scan_get_ap_records(&count, ap_list);
                        scan_count = (count > MAX_SCAN_RESULTS) ? MAX_SCAN_RESULTS : count;
                        for (int i = 0; i < scan_count; i++) {
                            strncpy(scan_results[i].ssid, (char*)ap_list[i].ssid, 32);
                            scan_results[i].ssid[32] = '\0';
                            scan_results[i].rssi = ap_list[i].rssi;
                            scan_results[i].open = (ap_list[i].authmode == WIFI_AUTH_OPEN);
                        }
                        free(ap_list);
                    }
                    if (current_state != WIFI_STATE_CONNECTED) {
                        current_state = WIFI_STATE_DISCONNECTED;
                    }
                    ESP_LOGI(TAG, "Scan done, found %d APs", scan_count);
                    invoke_on_scan_complete(scan_count);
                }
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&event->ip_info.ip));
            current_state = WIFI_STATE_CONNECTED;
            ESP_LOGI(TAG, "Got IP: %s", ip_address);

            // Reset reconnect backoff on successful connection
            reset_reconnect_delay();

            // Save WiFi credentials
            if (strlen(connected_ssid) > 0) {
                char password[65] = {0};
                wifi_config_t cfg;
                esp_wifi_get_config(WIFI_IF_STA, &cfg);
                strncpy(password, (char*)cfg.sta.password, sizeof(password) - 1);
                storage_save_wifi_config(connected_ssid, password);
                ESP_LOGI(TAG, "WiFi config saved: %s", connected_ssid);
            }

            invoke_on_connected();
        }
    }
}

int wifi_service_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sta_netif = esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Load and apply saved WiFi credentials
    char saved_ssid[33] = {0};
    char saved_password[65] = {0};
    if (storage_load_wifi_config(saved_ssid, sizeof(saved_ssid), saved_password, sizeof(saved_password))) {
        if (strlen(saved_ssid) > 0) {
            ESP_LOGI(TAG, "Loading saved WiFi config: %s", saved_ssid);
            wifi_service_connect(saved_ssid, saved_password);
        }
    }

    ESP_LOGI(TAG, "WiFi service initialized");
    return 0;
}

void wifi_service_register_callbacks(const wifi_callbacks_t *cbs)
{
    if (cbs) {
        callbacks = *cbs;
    }
}

void wifi_service_scan(void)
{
    if (current_state == WIFI_STATE_SCANNING) return;

    current_state = WIFI_STATE_SCANNING;
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

int wifi_service_get_scan_count(void)
{
    return scan_count;
}

const wifi_ap_info_t* wifi_service_get_ap(int index)
{
    if (index < 0 || index >= scan_count) return NULL;
    return &scan_results[index];
}

void wifi_service_connect(const char *ssid, const char *password)
{
    user_initiated_disconnect = false;
    stop_reconnect_timer();
    reset_reconnect_delay();

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    current_state = WIFI_STATE_CONNECTING;
    strncpy(connected_ssid, ssid, sizeof(connected_ssid) - 1);
    ip_address[0] = '\0';

    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI(TAG, "Connecting to: %s", ssid);
}

void wifi_service_disconnect(void)
{
    user_initiated_disconnect = true;
    stop_reconnect_timer();
    reset_reconnect_delay();

    esp_wifi_disconnect();
    current_state = WIFI_STATE_DISCONNECTED;
    connected_ssid[0] = '\0';
    ip_address[0] = '\0';
}

bool wifi_service_is_connected(void)
{
    return current_state == WIFI_STATE_CONNECTED;
}

const char* wifi_service_get_ip(void)
{
    return ip_address[0] ? ip_address : NULL;
}

wifi_state_t wifi_service_get_state(void)
{
    return current_state;
}
