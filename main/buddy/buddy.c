#include "buddy.h"
#include "buddy_render.h"
#include "service/led_service.h"
#include "service/storage_service.h"
#include "service/sound_service.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "buddy";

static SemaphoreHandle_t s_mutex = NULL;

#define TICK_INTERVAL_MS      200
#define CELEBRATE_DURATION_S  3
#define DIZZY_DURATION_S      3
#define HEART_DURATION_S      3
#define CELEBRATE_TICKS  (CELEBRATE_DURATION_S * 1000 / TICK_INTERVAL_MS)
#define DIZZY_TICKS      (DIZZY_DURATION_S * 1000 / TICK_INTERVAL_MS)
#define HEART_TICKS      (HEART_DURATION_S * 1000 / TICK_INTERVAL_MS)

/* ---- module state ---- */

static buddy_state_t s_state       = BUDDY_SLEEP;
static buddy_state_t s_pre_random  = BUDDY_SLEEP;
static int           s_species     = 0;
static int           s_tick_count  = 0;

static uint32_t s_approved = 0;
static uint32_t s_denied   = 0;

static bool           s_tcp_connected = false;
static bool           s_has_request   = false;
static tcp_request_t  s_current_request = {0};
static bool           s_include_rules = false;

/* Queued status: saved during ATTENTION, restored when temporary state ends */
static char s_pending_status[16] = {0};   /* e.g. "running", "idle", "error", "" */

static int      s_heart_level = 2;
static int64_t  s_last_heart_decay = 0;
static uint32_t s_session_approved = 0;
static uint32_t s_session_denied   = 0;

static buddy_callbacks_t s_cbs = { NULL };

static char   s_answer_labels[8][32];
static int    s_answer_count = 0;
static bool   s_answer_multi = false;

/* ---- forward declarations ---- */

static void set_state_locked(buddy_state_t new_state);
static void apply_pending_status(void);

/* ---- helpers ---- */

static bool is_temporary_state(buddy_state_t s)
{
    return s == BUDDY_CELEBRATE || s == BUDDY_DIZZY || s == BUDDY_HEART;
}

/* Apply queued status after temporary state ends. Caller must hold s_mutex. */
static void apply_pending_status(void)
{
    if (s_pending_status[0] == '\0') return;

    const char *state = s_pending_status;
    ESP_LOGI(TAG, "Applying pending status: %s", state);

    if (strcmp(state, "running") == 0) {
        set_state_locked(BUDDY_BUSY);
    } else if (strcmp(state, "idle") == 0) {
        set_state_locked(BUDDY_IDLE);
    } else if (strcmp(state, "error") == 0) {
        set_state_locked(BUDDY_DIZZY);
    }

    s_pending_status[0] = '\0';
}

/* Caller must hold s_mutex.
 * Releases s_mutex around the callback to avoid deadlock:
 *   service_task: s_mutex → (callback needs lvgl_mutex)
 *   LVGL task:    lvgl_mutex → (draw callback needs s_mutex via buddy_get_info)
 * By releasing s_mutex before the callback, we break the circular wait. */
static void set_state_locked(buddy_state_t new_state)
{
    if (s_state == new_state) return;

    ESP_LOGI(TAG, "state: %d -> %d", s_state, new_state);

    /* Stop previous LED effect (only buddy's wait, not pomodoro's) */
    led_service_wait_done(LED_WAIT_BUDDY);

    s_state      = new_state;
    s_tick_count = 0;

    /* Sound + LED don't need lvgl_mutex — do them under s_mutex */
    switch (new_state) {
        case BUDDY_ATTENTION:
            sound_service_play(SOUND_BUDDY_ATTENTION);
            led_service_play(LED_COLOR_ATTENTION);
            led_service_wait(LED_COLOR_ATTENTION, LED_WAIT_BUDDY);
            break;
        case BUDDY_CELEBRATE:
            led_service_play(LED_COLOR_CELEBRATE);
            break;
        case BUDDY_DIZZY:
            led_service_play(LED_COLOR_SAD);
            break;
        case BUDDY_HEART:
            led_service_play(LED_COLOR_HEART);
            break;
        case BUDDY_BUSY:
            led_service_play(LED_COLOR_BUSY);
            break;
        case BUDDY_IDLE:
            led_service_play(LED_COLOR_IDLE);
            break;
        case BUDDY_SLEEP:
            led_service_play(LED_COLOR_SLEEP);
            break;
        default:
            break;
    }

    /* Release s_mutex before callback — callback may take lvgl_mutex */
    if (s_cbs.on_state_changed) {
        xSemaphoreGiveRecursive(s_mutex);
        s_cbs.on_state_changed(new_state);
        xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    }
}

/* ---- init ---- */

int buddy_init(void)
{
    s_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return -1;
    }
    buddy_load_stats();
    s_last_heart_decay = esp_timer_get_time();
    ESP_LOGI(TAG, "init  species=%d  approved=%lu  denied=%lu  heart=%d",
             s_species, (unsigned long)s_approved, (unsigned long)s_denied,
             s_heart_level);
    return 0;
}

void buddy_register_callbacks(const buddy_callbacks_t *cbs)
{
    if (cbs) {
        s_cbs = *cbs;
    }
}

/* ---- TCP events ---- */

void buddy_on_tcp_connected(void)
{
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    s_tcp_connected = true;
    s_session_approved = 0;
    s_session_denied = 0;
    ESP_LOGI(TAG, "TCP connected");
    set_state_locked(BUDDY_IDLE);
    xSemaphoreGiveRecursive(s_mutex);
}

void buddy_on_tcp_disconnected(void)
{
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    s_tcp_connected = false;
    s_has_request   = false;
    s_session_approved = 0;
    s_session_denied = 0;
    memset(&s_current_request, 0, sizeof(s_current_request));
    ESP_LOGI(TAG, "TCP disconnected");
    set_state_locked(BUDDY_SLEEP);
    xSemaphoreGiveRecursive(s_mutex);
}

void buddy_on_tcp_request(const tcp_request_t *req)
{
    if (!req) return;
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    s_pre_random = is_temporary_state(s_state) ? s_pre_random : s_state;
    s_current_request = *req;
    s_has_request = true;
    ESP_LOGI(TAG, "TCP request: id=%.16s  type=%d  tool=%s",
             req->ccbb_request_id, req->type, req->tool);
    set_state_locked(BUDDY_ATTENTION);
    xSemaphoreGiveRecursive(s_mutex);
}

void buddy_on_tcp_session_end(void)
{
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    ESP_LOGI(TAG, "session_end: state=%d has_request=%d pending='%s'",
             s_state, s_has_request, s_pending_status);
    s_has_request = false;
    memset(&s_current_request, 0, sizeof(s_current_request));
    if (s_state == BUDDY_ATTENTION) {
        set_state_locked(BUDDY_IDLE);
    }
    xSemaphoreGiveRecursive(s_mutex);
}

void buddy_on_status(const char *state, const char *message)
{
    if (!state || !state[0]) return;
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);

    ESP_LOGI(TAG, "on_status: state='%s' buddy_state=%d pending='%s'",
             state, s_state, s_pending_status);

    if (s_state == BUDDY_ATTENTION) {
        /* Queue status for later, don't discard */
        strncpy(s_pending_status, state, sizeof(s_pending_status) - 1);
        s_pending_status[sizeof(s_pending_status) - 1] = '\0';
        ESP_LOGI(TAG, "Status queued during ATTENTION: '%s'", state);
        xSemaphoreGiveRecursive(s_mutex);
        return;
    }

    if (strcmp(state, "running") == 0) {
        s_pre_random = is_temporary_state(s_state) ? s_pre_random : s_state;
        ESP_LOGI(TAG, "Status '%s' -> BUSY (pre_random=%d)", state, s_pre_random);
        set_state_locked(BUDDY_BUSY);
    } else if (strcmp(state, "idle") == 0) {
        ESP_LOGI(TAG, "Status '%s' buddy_state=%d is_temp=%d", state, s_state, is_temporary_state(s_state));
        if (is_temporary_state(s_state)) {
            /* Queue for apply after temporary state ends */
            strncpy(s_pending_status, state, sizeof(s_pending_status) - 1);
            s_pending_status[sizeof(s_pending_status) - 1] = '\0';
            ESP_LOGI(TAG, "Status queued during temp state: '%s'", state);
        } else {
            set_state_locked(BUDDY_IDLE);
        }
    } else if (strcmp(state, "error") == 0) {
        s_pre_random = is_temporary_state(s_state) ? s_pre_random : s_state;
        ESP_LOGI(TAG, "Status '%s' -> DIZZY", state);
        set_state_locked(BUDDY_DIZZY);
    } else {
        ESP_LOGW(TAG, "Unknown status: '%s'", state);
    }

    xSemaphoreGiveRecursive(s_mutex);
}

/* ---- user actions ---- */

void buddy_approve(void)
{
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    if (s_state != BUDDY_ATTENTION) {
        xSemaphoreGiveRecursive(s_mutex);
        return;
    }

    s_approved++;
    s_session_approved++;
    s_has_request = false;
    buddy_save_stats();
    ESP_LOGI(TAG, "approved  total=%lu", (unsigned long)s_approved);
    if (s_cbs.on_decision) s_cbs.on_decision(true, &s_current_request);
    set_state_locked(BUDDY_CELEBRATE);
    sound_service_play(SOUND_BUDDY_HAPPY);
    xSemaphoreGiveRecursive(s_mutex);
}

void buddy_deny(void)
{
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    if (s_state != BUDDY_ATTENTION) {
        xSemaphoreGiveRecursive(s_mutex);
        return;
    }

    s_denied++;
    s_session_denied++;
    s_has_request = false;
    buddy_save_stats();
    ESP_LOGI(TAG, "denied  total=%lu", (unsigned long)s_denied);
    if (s_cbs.on_decision) s_cbs.on_decision(false, &s_current_request);
    set_state_locked(BUDDY_DIZZY);
    sound_service_play(SOUND_BUDDY_SAD);
    xSemaphoreGiveRecursive(s_mutex);
}

void buddy_submit_answer(void)
{
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    if (s_state != BUDDY_ATTENTION) {
        xSemaphoreGiveRecursive(s_mutex);
        return;
    }
    /* Inline approve logic (already holding mutex) */
    s_approved++;
    s_session_approved++;
    s_has_request = false;
    buddy_save_stats();
    ESP_LOGI(TAG, "approved (answer)  total=%lu", (unsigned long)s_approved);
    if (s_cbs.on_decision) s_cbs.on_decision(true, &s_current_request);
    set_state_locked(BUDDY_CELEBRATE);
    sound_service_play(SOUND_BUDDY_HAPPY);
    led_service_play(LED_COLOR_CELEBRATE);
    xSemaphoreGiveRecursive(s_mutex);
}

void buddy_set_answer_labels(const char *labels[], int count, bool multi_select)
{
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    s_answer_count = 0;
    s_answer_multi = multi_select;
    if (!labels || count <= 0) {
        xSemaphoreGiveRecursive(s_mutex);
        return;
    }
    if (count > 8) count = 8;
    for (int i = 0; i < count; i++) {
        if (labels[i]) {
            strncpy(s_answer_labels[i], labels[i], sizeof(s_answer_labels[i]) - 1);
            s_answer_labels[i][sizeof(s_answer_labels[i]) - 1] = '\0';
        } else {
            s_answer_labels[i][0] = '\0';
        }
    }
    s_answer_count = count;
    xSemaphoreGiveRecursive(s_mutex);
}

void buddy_trigger_random(void)
{
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    if (s_state != BUDDY_IDLE && s_state != BUDDY_SLEEP) {
        xSemaphoreGiveRecursive(s_mutex);
        return;
    }
    s_pre_random = s_state;
    buddy_state_t picks[] = { BUDDY_CELEBRATE, BUDDY_DIZZY, BUDDY_HEART };
    buddy_state_t pick = picks[esp_random() % 3];
    if (pick == BUDDY_HEART && s_heart_level < 5) {
        s_heart_level++;
        buddy_save_stats();
    }
    set_state_locked(pick);
    xSemaphoreGiveRecursive(s_mutex);
}

/* ---- info ---- */

buddy_info_t buddy_get_info(void)
{
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    buddy_info_t info = {
        .state              = s_state,
        .species_index      = s_species,
        .approved_count     = s_approved,
        .denied_count       = s_denied,
        .has_pending_request = s_has_request,
        .tcp_connected      = s_tcp_connected,
        .request_type       = s_has_request ? s_current_request.type : REQ_PERMISSION,
        .heart_level        = s_heart_level,
        .session_approved   = s_session_approved,
        .session_denied     = s_session_denied,
    };
    xSemaphoreGiveRecursive(s_mutex);
    return info;
}

int buddy_get_answer_count(void)
{
    return s_answer_count;
}

const char *buddy_get_answer_label(int index)
{
    if (index < 0 || index >= s_answer_count) return NULL;
    return s_answer_labels[index];
}

bool buddy_is_answer_multi(void)
{
    return s_answer_multi;
}

void buddy_include_rules(bool include)
{
    s_include_rules = include;
}

bool buddy_should_include_rules(void)
{
    bool val = s_include_rules;
    s_include_rules = false;
    return val;
}

void buddy_set_species(int index)
{
    if (index < 0 || index >= buddy_species_count) return;
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    s_species = index;
    storage_save_int("buddy", "species", (int32_t)s_species);
    ESP_LOGI(TAG, "species set to %d (%s)", index, buddy_species_table[index]->name);
    xSemaphoreGiveRecursive(s_mutex);
}

int buddy_get_species_count(void)
{
    return buddy_species_count;
}

const char *buddy_get_species_name(int index)
{
    if (index < 0 || index >= buddy_species_count) return NULL;
    return buddy_species_table[index]->name;
}

/* ---- animation ---- */

void buddy_tick(void)
{
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    s_tick_count++;

    switch (s_state) {
    case BUDDY_CELEBRATE:
        if (s_tick_count >= CELEBRATE_TICKS) {
            if (s_pending_status[0]) {
                apply_pending_status();
            } else {
                set_state_locked(s_pre_random);
            }
        }
        break;

    case BUDDY_DIZZY:
        if (s_tick_count >= DIZZY_TICKS) {
            if (s_pending_status[0]) {
                apply_pending_status();
            } else {
                set_state_locked(s_pre_random);
            }
        }
        break;

    case BUDDY_HEART:
        if (s_tick_count >= HEART_TICKS) {
            if (s_pending_status[0]) {
                apply_pending_status();
            } else {
                set_state_locked(s_pre_random);
            }
        }
        break;

    case BUDDY_ATTENTION:
        led_service_wait(LED_COLOR_ATTENTION, LED_WAIT_BUDDY);
        break;

    default:
        break;
    }

    /* Heart decay: -1 per hour */
    if (s_heart_level > 0) {
        int64_t now = esp_timer_get_time();
        if (now - s_last_heart_decay >= 3600000000LL) {
            s_heart_level--;
            s_last_heart_decay = now;
            buddy_save_stats();
            ESP_LOGI(TAG, "heart decayed to %d", s_heart_level);
        }
    }

    xSemaphoreGiveRecursive(s_mutex);
}

uint32_t buddy_get_tick_count(void)
{
    return (uint32_t)s_tick_count;
}

int buddy_get_species_index(void)
{
    return s_species;
}

/* ---- persistence ---- */

void buddy_save_stats(void)
{
    storage_save_int("buddy", "species",   (int32_t)s_species);
    storage_save_int("buddy", "approved",  (int32_t)s_approved);
    storage_save_int("buddy", "denied",    (int32_t)s_denied);
    storage_save_int("buddy", "heart",     (int32_t)s_heart_level);
}

void buddy_load_stats(void)
{
    int32_t val = 0;

    if (storage_load_int("buddy", "species", &val)) {
        if (val >= 0 && val < buddy_species_count) {
            s_species = (int)val;
        }
    }

    if (storage_load_int("buddy", "approved", &val)) {
        s_approved = (uint32_t)val;
    }

    if (storage_load_int("buddy", "denied", &val)) {
        s_denied = (uint32_t)val;
    }

    if (storage_load_int("buddy", "heart", &val)) {
        if (val >= 0 && val <= 5) s_heart_level = (int)val;
    }
}
