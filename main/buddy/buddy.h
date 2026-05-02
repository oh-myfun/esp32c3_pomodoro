#pragma once

#include "buddy_chars.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    buddy_state_t state;
    int species_index;
    uint32_t approved_count;
    uint32_t denied_count;
    char owner_name[32];
    char buddy_name[32];
    char prompt_id[64];
    char prompt_tool[32];
    char prompt_hint[256];
    bool has_pending_prompt;
} buddy_info_t;

typedef struct {
    void (*on_state_changed)(buddy_state_t new_state);
} buddy_callbacks_t;

int  buddy_init(void);
void buddy_register_callbacks(const buddy_callbacks_t *cbs);

/* BLE-driven updates */
void buddy_on_heartbeat(int running, int waiting, bool has_prompt,
                        const char *prompt_id, const char *tool, const char *hint);
void buddy_on_ble_connected(void);
void buddy_on_ble_disconnected(void);

/* User actions */
void buddy_approve(void);
void buddy_deny(void);
void buddy_trigger_dizzy(void);

/* Info */
buddy_info_t buddy_get_info(void);
void buddy_set_species(int index);
int  buddy_get_species_count(void);
const char *buddy_get_species_name(int index);

/* Animation */
void buddy_tick(void);
const char *const *buddy_get_current_frame(void);

/* Persistence */
void buddy_save_stats(void);
void buddy_load_stats(void);
