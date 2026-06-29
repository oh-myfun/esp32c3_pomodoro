/* fake_services.c — PC 模拟器使用的 service mock 实现。
 * 复用 main/service/*.h 的接口签名，提供合理假数据让 UI 代码跑起来。 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"

#include "service/storage_service.h"
#include "service/wifi_service.h"
#include "service/time_service.h"
#include "service/tcp_service.h"
#include "service/sound_service.h"
#include "service/led_service.h"
#include "service/chime_service.h"
#include "service/sensor_service.h"

#include "pomodoro/pomodoro_engine.h"
#include "buddy/buddy.h"
#include "input/input_handler.h"

static const char *TAG = "FAKE";

/* Globals referenced by settings UI screens (defined in device's main.c). */
int sleep_timeout_idx = 3;        /* default: 1 min */
bool keep_awake_on_busy = false;

/* ===== storage_service ===== */
void storage_migrate_settings_keys(void) {}
void storage_migrate_wifi_config(void) {}

bool storage_load_wifi_config(char *ssid, size_t ssid_len, char *password, size_t password_len) {
    if (ssid && ssid_len) ssid[0] = '\0';
    if (password && password_len) password[0] = '\0';
    return false;
}

bool storage_save_pomodoro_state(void *state) { (void)state; return true; }
bool storage_load_pomodoro_state(void *state) {
    if (state) {
        int32_t *p = (int32_t *)state;
        p[0] = 3;  /* completed */
        p[1] = 1;  /* cycle */
    }
    return true;
}

bool storage_save_pomodoro_settings(void *settings) { (void)settings; return true; }
bool storage_load_pomodoro_settings(void *settings) {
    if (settings) {
        int32_t *p = (int32_t *)settings;
        p[0] = 25; p[1] = 5; p[2] = 15; p[3] = 4;  /* work/break/long/cycles */
    }
    return true;
}

bool storage_save_string(const char *ns, const char *key, const char *value) { (void)ns; (void)key; (void)value; return true; }
bool storage_load_string(const char *ns, const char *key, char *value, size_t len) {
    (void)ns; (void)key;
    if (value && len) value[0] = '\0';
    return false;
}

bool storage_save_int(const char *ns, const char *key, int32_t value) { (void)ns; (void)key; (void)value; return true; }
bool storage_load_int(const char *ns, const char *key, int32_t *value) {
    (void)ns; (void)key;
    if (value) *value = 0;
    return false;
}

bool storage_save_blob(const char *ns, const char *key, const void *data, size_t len) { (void)ns; (void)key; (void)data; (void)len; return true; }
bool storage_load_blob(const char *ns, const char *key, void *data, size_t len) {
    (void)ns; (void)key; (void)data; (void)len;
    return false;
}

bool storage_save_time(uint64_t ts) { (void)ts; return true; }
bool storage_load_time(uint64_t *ts) { if (ts) *ts = 0; return false; }
void storage_clear_namespace(const char *ns) { (void)ns; }

int  storage_get_wifi_profile_count(void) { return 2; }
void storage_add_wifi_profile(const char *ssid, const char *password) { (void)ssid; (void)password; }
bool storage_load_wifi_profile(int index, char *ssid, size_t ssid_len, char *password, size_t pwd_len) {
    const char *samples[][2] = {
        {"MyHome-5G",   "password1"},
        {"Office-Lab",  "secret2"},
    };
    if (index < 0 || index >= 2) return false;
    if (ssid) snprintf(ssid, ssid_len, "%s", samples[index][0]);
    if (password) snprintf(password, pwd_len, "%s", samples[index][1]);
    return true;
}
void storage_delete_wifi_profile(int index) { (void)index; }

/* ===== wifi_service ===== */
int  wifi_service_init(void) { return 0; }
void wifi_service_register_callbacks(const wifi_callbacks_t *cbs) { (void)cbs; }
void wifi_service_scan(void) {}
int  wifi_service_get_scan_count(void) { return 3; }
const wifi_ap_info_t* wifi_service_get_ap(int index) {
    static wifi_ap_info_t samples[3];
    if (index < 0 || index >= 3) return NULL;
    const char *ssids[] = {"MyHome-5G", "Office-Lab", "Guest"};
    const int8_t rssis[] = {-45, -60, -78};
    const bool opens[] = {false, false, true};
    snprintf(samples[index].ssid, sizeof(samples[index].ssid), "%s", ssids[index]);
    samples[index].rssi = rssis[index];
    samples[index].open = opens[index];
    return &samples[index];
}
void wifi_service_connect(const char *ssid, const char *password) { (void)ssid; (void)password; }
void wifi_service_disconnect(void) {}
bool wifi_service_is_connected(void) { return true; }
const char* wifi_service_get_connected_ssid(void) { return "MyHome-5G"; }
wifi_state_t wifi_service_get_state(void) { return WIFI_STATE_CONNECTED; }
void wifi_service_scan_and_connect(void) {}
int  wifi_service_get_saved_count(void) { return 2; }
const char* wifi_service_get_saved_ssid(int index) {
    static const char *ssids[] = {"MyHome-5G", "Office-Lab"};
    if (index < 0 || index >= 2) return NULL;
    return ssids[index];
}
void wifi_service_delete_saved(int index) { (void)index; }
bool wifi_service_is_saved(const char *ssid) {
    (void)ssid;
    return true;
}

/* ===== time_service ===== */
static bool s_time_synced = true;
static int s_tz_hours = 8;
static int s_ntp_idx = 0;
static uint16_t s_sync_interval = 10;

void time_service_init(void) {}
bool time_service_is_synced(void) { return s_time_synced; }
void time_service_request_sync(void) {}
void time_service_tick(void) {}
void time_service_set_sync_interval(uint16_t m) { s_sync_interval = m; }
uint16_t time_service_get_sync_interval(void) { return s_sync_interval; }
uint16_t time_service_get_interval_option(int i) {
    static const uint16_t opts[] = {5, 10, 30, 60, 120, 240, 480, 1440};
    if (i < 0 || i >= 8) return 10;
    return opts[i];
}
int time_service_get_interval_index(void) { return 1; }
void time_service_set_timezone_offset(int h) { s_tz_hours = h; }
int  time_service_get_timezone_offset(void) { return s_tz_hours; }
void time_service_set_ntp_server_index(int i) { s_ntp_idx = i; }
int  time_service_get_ntp_server_index(void) { return s_ntp_idx; }
const char* time_service_get_ntp_server_name(int i) {
    static const char *names[] = {"NTP Pool", "China", "Aliyun", "Google", "Windows"};
    if (i < 0 || i >= 5) return "?";
    return names[i];
}

/* ===== tcp_service ===== */
int  tcp_service_init(void) { return 0; }
void tcp_service_register_callbacks(const tcp_callbacks_t *cbs) { (void)cbs; }
void tcp_service_connect(const char *host, int port) { (void)host; (void)port; }
void tcp_service_disconnect(void) {}
bool tcp_service_is_connected(void) { return true; }
void tcp_service_send_decision(const char *json) { (void)json; }
bool tcp_service_load_config(char *host, size_t host_len, int *port) {
    if (host) snprintf(host, host_len, "192.168.1.100");
    if (port) *port = 9876;
    return true;
}
void tcp_service_save_config(const char *host, int port) { (void)host; (void)port; }
void tcp_service_save_pairing_code(const char *code) { (void)code; }
bool tcp_service_load_pairing_code(char *code, size_t len) {
    if (code) snprintf(code, len, "AB12CD34");
    return true;
}
void tcp_service_scan(void) {}
bool tcp_service_is_scan_busy(void) { return false; }
int  tcp_service_get_scan_count(void) { return 1; }
const tcp_scan_result_t *tcp_service_get_scan_result(int index) {
    static tcp_scan_result_t r;
    if (index != 0) return NULL;
    snprintf(r.host, sizeof(r.host), "192.168.1.100");
    r.port = 9876;
    snprintf(r.sessions[0].pairing_code, sizeof(r.sessions[0].pairing_code), "AB12CD34");
    snprintf(r.sessions[0].project, sizeof(r.sessions[0].project), "pomodoro");
    r.session_count = 1;
    return &r;
}
void tcp_service_repair(const char *pairing_code) { (void)pairing_code; }
const char *tcp_service_get_project(void) { return "pomodoro"; }

/* ===== sound_service ===== */
void sound_service_init(void) {}
void sound_service_play(sound_id_t id) { (void)id; }
bool sound_service_is_enabled(void) { return true; }
void sound_service_set_enabled(bool e) { (void)e; }
bool sound_service_is_category_enabled(sound_category_t c) { (void)c; return true; }
void sound_service_set_category_enabled(sound_category_t c, bool on) { (void)c; (void)on; }
void sound_service_play_hour_chime(int h12) { (void)h12; }
void sound_service_play_half_chime(void) {}
void sound_service_play_raw(sound_id_t id) { (void)id; }
void sound_service_play_hour_chime_raw(int h12) { (void)h12; }
void sound_service_play_half_chime_raw(void) {}
bool sound_service_is_playing(void) { return false; }
bool sound_service_is_quiet_hour(int h) { (void)h; return false; }
void sound_service_set_quiet_range(int s, int e) { (void)s; (void)e; }
void sound_service_get_quiet_range(int *s, int *e) { if (s) *s = 0; if (e) *e = 0; }

/* ===== led_service ===== */
const led_color_t led_demo_colors[LED_DEMO_COLOR_COUNT] = {
    {255, 0, 0}, {0, 255, 0}, {0, 80, 255}, {255, 255, 0}, {255, 0, 100},
};
const char *const led_demo_color_names[LED_DEMO_COLOR_COUNT] = {
    "Work", "Break", "Long", "Paused", "Sad",
};
void led_service_init(void) {}
void led_service_play(led_color_t c) { (void)c; }
void led_service_wait(led_color_t c, uint8_t src) { (void)c; (void)src; }
void led_service_wait_done(uint8_t src) { (void)src; }
void led_service_stop(void) {}
void led_service_set_enabled(bool on) { (void)on; }
bool led_service_is_enabled(void) { return true; }
void led_service_set_brightness(uint8_t l) { (void)l; }
uint8_t led_service_get_brightness(void) { return 5; }
void led_service_set_animation(led_anim_t a) { (void)a; }
led_anim_t led_service_get_animation(void) { return LED_ANIM_BREATH; }
void led_service_set_style(led_style_t s) { (void)s; }
led_style_t led_service_get_style(void) { return LED_STYLE_COLORFUL; }
void led_service_set_speed(led_speed_t s) { (void)s; }
led_speed_t led_service_get_speed(void) { return LED_SPEED_MEDIUM; }
void led_service_demo_start(led_color_t c) { (void)c; }
void led_service_demo_change_color(led_color_t c) { (void)c; }
void led_service_demo_stop(void) {}
bool led_service_is_demo_active(void) { return false; }

/* ===== chime_service ===== */
void chime_service_init(void) {}
void chime_service_tick(void) {}

/* ===== sensor_service ===== */
void sensor_service_init(void) {}
sensor_sample_t sensor_service_get_current(void) {
    sensor_sample_t s = {0};
    s.temperature = 26.5f; s.temp_valid = true;
    s.humidity = 58.0f;    s.hum_valid = true;
    s.pressure = 1013.0f;  s.press_valid = true;
    s.altitude = 120.0f;   s.alt_valid = true;
    return s;
}
int sensor_service_get_chart_data(sensor_level_t level, sensor_sample_t *buf,
                                  sensor_time_t *time_buf, int buf_size) {
    (void)level;
    if (buf_size <= 0) return 0;
    int n = buf_size < 20 ? buf_size : 20;
    for (int i = 0; i < n; i++) {
        buf[i].temperature = 25.0f + (i % 5);
        buf[i].temp_valid = true;
        buf[i].humidity = 50.0f + (i % 10);
        buf[i].hum_valid = true;
        buf[i].pressure = 1010.0f + (i % 3);
        buf[i].press_valid = true;
        buf[i].altitude = 120.0f;
        buf[i].alt_valid = true;
        if (time_buf) {
            time_buf[i] = (sensor_time_t){2026, 6, 28, 12, i, 0};
        }
    }
    return n;
}
void sensor_service_get_settings(sensor_settings_t *out) {
    if (!out) return;
    out->temp_min = -100; out->temp_max = 500;
    out->press_min = 900; out->press_max = 1100;
    out->alt_min = -100; out->alt_max = 3000;
    out->temp_source = TEMP_SRC_AHT20;
    out->sample_interval = 10;
}
void sensor_service_set_settings(const sensor_settings_t *in) { (void)in; }
void sensor_service_reset_settings(void) {}
void sensor_service_set_idle_mode(bool idle) { (void)idle; }

/* ===== pomodoro_engine (real impl is reused, but if not linked, provide stub) ===== */
/* NOTE: pomodoro_engine.c is compiled directly into the sim target — no stub needed. */

/* ===== buddy (we provide stub to avoid pulling buddy.c deps) ===== */
void buddy_register_callbacks(const buddy_callbacks_t *cbs) { (void)cbs; }
void buddy_on_tcp_connected(void) {}
void buddy_on_tcp_disconnected(void) {}
void buddy_on_tcp_request(const tcp_request_t *req) { (void)req; }
void buddy_on_tcp_session_end(void) {}
void buddy_on_request_done(const char *id) { (void)id; }
void buddy_on_status(const char *state, const char *msg) { (void)state; (void)msg; }
void buddy_approve(void) {}
void buddy_deny(void) {}
void buddy_submit_answer(void) {}
void buddy_set_answer_labels(const char *labels[], int count, bool multi) { (void)labels; (void)count; (void)multi; }
void buddy_include_rules(bool inc) { (void)inc; }
bool buddy_should_include_rules(void) { return false; }
void buddy_trigger_random(void) {}

static int s_species_idx = 0;
static uint32_t s_tick = 0;
static buddy_info_t s_info = {
    .state = BUDDY_IDLE,
    .species_index = 0,
    .approved_count = 12,
    .denied_count = 2,
    .has_pending_request = false,
    .tcp_connected = true,
    .request_type = REQ_PERMISSION,
    .heart_level = 3,
    .session_approved = 5,
    .session_denied = 1,
};

buddy_info_t buddy_get_info(void) {
    s_info.species_index = s_species_idx;
    return s_info;
}
int  buddy_get_answer_count(void) { return 0; }
const char *buddy_get_answer_label(int i) { (void)i; return ""; }
bool buddy_is_answer_multi(void) { return false; }
void buddy_set_species(int idx) { s_species_idx = idx; }
int  buddy_get_species_count(void) { return 18; }
const char *buddy_get_species_name(int idx) {
    static const char *names[] = {
        "Capybara","Duck","Goose","Blob","Cat","Dragon","Octopus","Owl",
        "Penguin","Turtle","Snail","Ghost","Axolotl","Cactus","Robot","Rabbit","Mushroom","Chonk"
    };
    if (idx < 0 || idx >= 18) return "?";
    return names[idx];
}
void buddy_tick(void) { s_tick++; }
uint32_t buddy_get_tick_count(void) { return s_tick; }
int  buddy_get_species_index(void) { return s_species_idx; }
void buddy_save_stats(void) {}
void buddy_load_stats(void) {}
int  buddy_init(void) { return 0; }

/* ===== input_handler ===== */
void input_handler_init(void) {}
void input_handler_task(void *arg) { (void)arg; }
void input_handler_set_reverse(bool r) { (void)r; }
bool input_handler_get_reverse(void) { return false; }

/* Override buddy state setter for screenshot scenarios */
void sim_set_buddy_state(buddy_state_t s) { s_info.state = s; }
void sim_set_buddy_heart(int h) { s_info.heart_level = h; }
