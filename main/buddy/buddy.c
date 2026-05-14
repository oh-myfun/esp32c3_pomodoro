#include "buddy.h"
#include "service/led_service.h"
#include "service/storage_service.h"
#include "service/sound_service.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>

static const char *TAG = "buddy";

#define TICK_INTERVAL_MS      500
#define CELEBRATE_DURATION_S  3
#define DIZZY_DURATION_S      3
#define HEART_DURATION_S      3
#define ATTENTION_TIMEOUT_S   30

/* Ticks = duration_s * 1000 / TICK_INTERVAL_MS */
#define CELEBRATE_TICKS  (CELEBRATE_DURATION_S * 1000 / TICK_INTERVAL_MS)
#define DIZZY_TICKS      (DIZZY_DURATION_S * 1000 / TICK_INTERVAL_MS)
#define HEART_TICKS      (HEART_DURATION_S * 1000 / TICK_INTERVAL_MS)
#define ATTENTION_TICKS  (ATTENTION_TIMEOUT_S * 1000 / TICK_INTERVAL_MS)

/* ---- module state ---- */

static buddy_state_t s_state       = BUDDY_SLEEP;
static buddy_state_t s_pre_random  = BUDDY_SLEEP;
static int           s_species     = 0;
static int           s_seq_idx     = 0;
static int           s_tick_count  = 0;

static uint32_t s_approved = 0;
static uint32_t s_denied   = 0;

static bool           s_tcp_connected = false;
static bool           s_has_request   = false;
static tcp_request_t  s_current_request = {0};

static buddy_callbacks_t s_cbs = { NULL };

/* ---- forward declarations ---- */

static void set_state(buddy_state_t new_state);

/* ---- helpers ---- */

static void set_state(buddy_state_t new_state)
{
    if (s_state == new_state) return;

    ESP_LOGI(TAG, "state: %d -> %d", s_state, new_state);
    s_state      = new_state;
    s_seq_idx    = 0;
    s_tick_count = 0;

    if (s_cbs.on_state_changed) {
        s_cbs.on_state_changed(new_state);
    }

    if (new_state == BUDDY_ATTENTION) {
        sound_service_play(SOUND_BUDDY_ATTENTION);
        led_service_play(LED_COLOR_ATTENTION);
    }
}

/* ---- init ---- */

int buddy_init(void)
{
    buddy_load_stats();
    ESP_LOGI(TAG, "init  species=%d  approved=%lu  denied=%lu",
             s_species, (unsigned long)s_approved, (unsigned long)s_denied);
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
    s_tcp_connected = true;
    ESP_LOGI(TAG, "TCP connected");
    set_state(BUDDY_IDLE);
}

void buddy_on_tcp_disconnected(void)
{
    s_tcp_connected = false;
    s_has_request   = false;
    memset(&s_current_request, 0, sizeof(s_current_request));
    ESP_LOGI(TAG, "TCP disconnected");
    set_state(BUDDY_SLEEP);
}

void buddy_on_tcp_request(const tcp_request_t *req)
{
    if (!req) return;
    s_current_request = *req;
    s_has_request = true;
    ESP_LOGI(TAG, "TCP request: id=%.16s  type=%d  tool=%s",
             req->ccbb_request_id, req->type, req->tool);
    set_state(BUDDY_ATTENTION);
}

void buddy_on_tcp_session_end(void)
{
    s_has_request = false;
    memset(&s_current_request, 0, sizeof(s_current_request));
    if (s_state == BUDDY_ATTENTION) {
        set_state(BUDDY_IDLE);
    }
}

/* ---- user actions ---- */

void buddy_approve(void)
{
    if (s_state != BUDDY_ATTENTION) return;

    s_approved++;
    s_has_request = false;
    buddy_save_stats();
    ESP_LOGI(TAG, "approved  total=%lu", (unsigned long)s_approved);
    set_state(BUDDY_CELEBRATE);
    sound_service_play(SOUND_BUDDY_HAPPY);
    led_service_play(LED_COLOR_CELEBRATE);
}

void buddy_deny(void)
{
    if (s_state != BUDDY_ATTENTION) return;

    s_denied++;
    s_has_request = false;
    buddy_save_stats();
    ESP_LOGI(TAG, "denied  total=%lu", (unsigned long)s_denied);
    set_state(BUDDY_DIZZY);
    sound_service_play(SOUND_BUDDY_SAD);
    led_service_play(LED_COLOR_SAD);
}

void buddy_submit_answer(void)
{
    if (s_state != BUDDY_ATTENTION) return;
    /* Delegate to approve — the UI already filled s_current_request.selected[] */
    buddy_approve();
}

void buddy_trigger_random(void)
{
    if (s_state != BUDDY_IDLE && s_state != BUDDY_SLEEP) return;
    s_pre_random = s_state;
    buddy_state_t picks[] = { BUDDY_CELEBRATE, BUDDY_DIZZY, BUDDY_HEART };
    set_state(picks[esp_random() % 3]);
}

/* ---- info ---- */

buddy_info_t buddy_get_info(void)
{
    buddy_info_t info = {
        .state              = s_state,
        .species_index      = s_species,
        .approved_count     = s_approved,
        .denied_count       = s_denied,
        .has_pending_request = s_has_request,
        .tcp_connected      = s_tcp_connected,
        .request_type       = s_has_request ? s_current_request.type : REQ_PERMISSION,
    };
    return info;
}

void buddy_set_species(int index)
{
    if (index < 0 || index >= BUDDY_SPECIES_COUNT) return;
    s_species  = index;
    s_seq_idx  = 0;
    storage_save_int("buddy", "species", (int32_t)s_species);
    ESP_LOGI(TAG, "species set to %d (%s)", index, BUDDY_SPECIES[index].name);
}

int buddy_get_species_count(void)
{
    return BUDDY_SPECIES_COUNT;
}

const char *buddy_get_species_name(int index)
{
    if (index < 0 || index >= BUDDY_SPECIES_COUNT) return NULL;
    return BUDDY_SPECIES[index].name;
}

/* ---- animation ---- */

void buddy_tick(void)
{
    s_tick_count++;

    const buddy_species_t *sp = &BUDDY_SPECIES[s_species];
    uint8_t sl = sp->seq_len[s_state];

    /* Advance SEQ index */
    if (sl > 0) {
        s_seq_idx = (s_seq_idx + 1) % sl;
    }

    /* Handle timeouts */
    switch (s_state) {
    case BUDDY_CELEBRATE:
        if (s_tick_count >= CELEBRATE_TICKS) {
            set_state(s_pre_random);
        }
        break;

    case BUDDY_DIZZY:
        if (s_tick_count >= DIZZY_TICKS) {
            set_state(s_pre_random);
        }
        break;

    case BUDDY_HEART:
        if (s_tick_count >= HEART_TICKS) {
            set_state(s_pre_random);
        }
        break;

    case BUDDY_ATTENTION:
        /* Blink LED: toggle red/off each tick */
        if (s_tick_count % 2 == 0) {
            led_service_wait(LED_COLOR_ATTENTION);
        } else {
            led_service_stop();
        }
        /* Timeout after 30s — auto-deny */
        if (s_tick_count >= ATTENTION_TICKS) {
            buddy_deny();
        }
        break;

    default:
        break;
    }
}

const char *const *buddy_get_current_frame(void)
{
    const buddy_species_t *sp = &BUDDY_SPECIES[s_species];
    uint8_t pose_idx = 0;
    if (sp->seq[s_state] && sp->seq_len[s_state] > 0) {
        pose_idx = sp->seq[s_state][s_seq_idx % sp->seq_len[s_state]];
    }
    if (pose_idx >= sp->pose_count[s_state]) pose_idx = 0;
    return (*sp->state_frames[s_state])[pose_idx];
}

uint16_t buddy_get_current_body_color(void)
{
    return BUDDY_SPECIES[s_species].body_color;
}

/* ---- persistence ---- */

void buddy_save_stats(void)
{
    storage_save_int("buddy", "species",   (int32_t)s_species);
    storage_save_int("buddy", "approved",  (int32_t)s_approved);
    storage_save_int("buddy", "denied",    (int32_t)s_denied);
}

void buddy_load_stats(void)
{
    int32_t val = 0;

    if (storage_load_int("buddy", "species", &val)) {
        if (val >= 0 && val < BUDDY_SPECIES_COUNT) {
            s_species = (int)val;
        }
    }

    if (storage_load_int("buddy", "approved", &val)) {
        s_approved = (uint32_t)val;
    }

    if (storage_load_int("buddy", "denied", &val)) {
        s_denied = (uint32_t)val;
    }
}
