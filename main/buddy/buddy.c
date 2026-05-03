#include "buddy.h"
#include "driver/ws2812.h"
#include "service/storage_service.h"
#include "service/sound_service.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "buddy";

#define TICK_INTERVAL_MS      500
#define CELEBRATE_DURATION_S  3
#define DIZZY_DURATION_S      3
#define ATTENTION_TIMEOUT_S   30

/* Ticks = duration_s * 1000 / TICK_INTERVAL_MS */
#define CELEBRATE_TICKS  (CELEBRATE_DURATION_S * 1000 / TICK_INTERVAL_MS)
#define DIZZY_TICKS      (DIZZY_DURATION_S * 1000 / TICK_INTERVAL_MS)
#define ATTENTION_TICKS  (ATTENTION_TIMEOUT_S * 1000 / TICK_INTERVAL_MS)

/* ---- module state ---- */

static buddy_state_t s_state      = BUDDY_SLEEP;
static buddy_state_t s_pre_dizzy  = BUDDY_SLEEP;
static int           s_species    = 0;
static int           s_frame_idx  = 0;
static int           s_tick_count = 0;

static uint32_t s_approved = 0;
static uint32_t s_denied   = 0;

static char s_owner_name[32]  = "";
static char s_buddy_name[32]  = "";
static char s_prompt_id[64]   = "";
static char s_prompt_tool[32] = "";
static char s_prompt_hint[256] = "";
static bool s_has_prompt      = false;
static bool s_ble_connected   = false;

static buddy_callbacks_t s_cbs = { NULL };

/* ---- forward declarations ---- */

static void set_state(buddy_state_t new_state);
static void update_led(void);

/* ---- helpers ---- */

static void set_state(buddy_state_t new_state)
{
    if (s_state == new_state) return;

    ESP_LOGI(TAG, "state: %d -> %d", s_state, new_state);
    s_state      = new_state;
    s_frame_idx  = 0;
    s_tick_count = 0;

    update_led();

    if (s_cbs.on_state_changed) {
        s_cbs.on_state_changed(new_state);
    }

    if (new_state == BUDDY_ATTENTION) {
        sound_service_play(SOUND_BUDDY_ATTENTION);
    }
}

static void update_led(void)
{
    switch (s_state) {
    case BUDDY_ATTENTION:
        ws2812_set_color(255, 0, 0);
        break;
    case BUDDY_CELEBRATE:
        ws2812_set_color(0, 255, 0);
        break;
    case BUDDY_HEART:
        ws2812_set_color(255, 50, 80);
        break;
    case BUDDY_SLEEP:
    case BUDDY_IDLE:
    default:
        ws2812_off();
        break;
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

/* ---- BLE-driven updates ---- */

void buddy_on_heartbeat(int running, int waiting, bool has_prompt,
                        const char *prompt_id, const char *tool, const char *hint)
{
    if (!s_ble_connected) return;

    /* Update prompt data */
    s_has_prompt = has_prompt;
    if (prompt_id) {
        strncpy(s_prompt_id, prompt_id, sizeof(s_prompt_id) - 1);
        s_prompt_id[sizeof(s_prompt_id) - 1] = '\0';
    }
    if (tool) {
        strncpy(s_prompt_tool, tool, sizeof(s_prompt_tool) - 1);
        s_prompt_tool[sizeof(s_prompt_tool) - 1] = '\0';
    }
    if (hint) {
        strncpy(s_prompt_hint, hint, sizeof(s_prompt_hint) - 1);
        s_prompt_hint[sizeof(s_prompt_hint) - 1] = '\0';
    }

    /* State transitions driven by heartbeat */
    if (has_prompt) {
        if (s_state != BUDDY_ATTENTION && s_state != BUDDY_CELEBRATE && s_state != BUDDY_DIZZY) {
            set_state(BUDDY_ATTENTION);
        }
    } else if (running > 0) {
        if (s_state == BUDDY_IDLE || s_state == BUDDY_ATTENTION) {
            set_state(BUDDY_BUSY);
        }
    } else {
        if (s_state == BUDDY_BUSY) {
            set_state(BUDDY_IDLE);
        }
    }
}

void buddy_on_ble_connected(void)
{
    s_ble_connected = true;
    ESP_LOGI(TAG, "BLE connected");
    set_state(BUDDY_IDLE);
}

void buddy_on_ble_disconnected(void)
{
    s_ble_connected = false;
    s_has_prompt    = false;
    s_prompt_id[0]  = '\0';
    ESP_LOGI(TAG, "BLE disconnected");
    set_state(BUDDY_SLEEP);
}

/* ---- user actions ---- */

void buddy_approve(void)
{
    if (s_state != BUDDY_ATTENTION) return;

    s_approved++;
    s_has_prompt   = false;
    s_prompt_id[0] = '\0';
    buddy_save_stats();
    ESP_LOGI(TAG, "approved  total=%lu", (unsigned long)s_approved);
    set_state(BUDDY_CELEBRATE);
    sound_service_play(SOUND_BUDDY_HAPPY);
}

void buddy_deny(void)
{
    if (s_state != BUDDY_ATTENTION) return;

    s_denied++;
    s_has_prompt   = false;
    s_prompt_id[0] = '\0';
    buddy_save_stats();
    ESP_LOGI(TAG, "denied  total=%lu", (unsigned long)s_denied);
    set_state(BUDDY_IDLE);
    sound_service_play(SOUND_BUDDY_SAD);
}

void buddy_trigger_dizzy(void)
{
    s_pre_dizzy = s_state;
    ESP_LOGI(TAG, "dizzy triggered (from state %d)", s_pre_dizzy);
    set_state(BUDDY_DIZZY);
}

/* ---- info ---- */

buddy_info_t buddy_get_info(void)
{
    buddy_info_t info = {
        .state            = s_state,
        .species_index    = s_species,
        .approved_count   = s_approved,
        .denied_count     = s_denied,
        .has_pending_prompt = s_has_prompt,
    };
    strncpy(info.owner_name,  s_owner_name,  sizeof(info.owner_name)  - 1);
    strncpy(info.buddy_name,  s_buddy_name,  sizeof(info.buddy_name)  - 1);
    strncpy(info.prompt_id,   s_prompt_id,   sizeof(info.prompt_id)   - 1);
    strncpy(info.prompt_tool, s_prompt_tool,  sizeof(info.prompt_tool) - 1);
    strncpy(info.prompt_hint, s_prompt_hint,  sizeof(info.prompt_hint) - 1);
    return info;
}

void buddy_set_species(int index)
{
    if (index < 0 || index >= BUDDY_SPECIES_COUNT) return;
    s_species = index;
    s_frame_idx = 0;
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
    uint8_t fc = sp->frame_count[s_state];

    /* Advance frame */
    s_frame_idx = (s_frame_idx + 1) % fc;

    /* Handle timeouts */
    switch (s_state) {
    case BUDDY_CELEBRATE:
        if (s_tick_count >= CELEBRATE_TICKS) {
            set_state(BUDDY_IDLE);
        }
        break;

    case BUDDY_DIZZY:
        if (s_tick_count >= DIZZY_TICKS) {
            set_state(s_pre_dizzy);
        }
        break;

    case BUDDY_ATTENTION:
        /* Blink LED: toggle red/off each tick */
        if (s_tick_count % 2 == 0) {
            ws2812_set_color(255, 0, 0);
        } else {
            ws2812_off();
        }
        /* Timeout after 30s */
        if (s_tick_count >= ATTENTION_TICKS) {
            s_has_prompt   = false;
            s_prompt_id[0] = '\0';
            set_state(BUDDY_IDLE);
        }
        break;

    default:
        break;
    }
}

const char *const *buddy_get_current_frame(void)
{
    const buddy_species_t *sp = &BUDDY_SPECIES[s_species];
    return (*sp->state_frames[s_state])[s_frame_idx];
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
