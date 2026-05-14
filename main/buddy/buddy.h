#pragma once

#include "buddy_chars.h"
#include <stdbool.h>
#include <stdint.h>

/* Request type enum — matches tcp_service.h definitions */
typedef enum {
    REQ_PERMISSION = 0,
    REQ_SINGLE_SELECT,
    REQ_MULTI_SELECT,
} request_type_t;

typedef struct {
    char ccbb_request_id[64];
    request_type_t type;
    char tool[32];
    char hint[128];
    char question[128];
    struct { char label[32]; char description[64]; } options[8];
    int option_count;
    bool multi_select;
    int focused;
    int selected[8];
    int selected_count;
    char questions_json[512];
} tcp_request_t;

typedef struct {
    buddy_state_t state;
    int species_index;
    uint32_t approved_count;
    uint32_t denied_count;
    bool has_pending_request;
    bool tcp_connected;
    request_type_t request_type;
} buddy_info_t;

typedef struct {
    void (*on_state_changed)(buddy_state_t new_state);
} buddy_callbacks_t;

int  buddy_init(void);
void buddy_register_callbacks(const buddy_callbacks_t *cbs);

/* TCP events */
void buddy_on_tcp_connected(void);
void buddy_on_tcp_disconnected(void);
void buddy_on_tcp_request(const tcp_request_t *req);
void buddy_on_tcp_session_end(void);

/* User actions */
void buddy_approve(void);
void buddy_deny(void);
void buddy_submit_answer(void);
void buddy_trigger_random(void);

/* Info */
buddy_info_t buddy_get_info(void);
void buddy_set_species(int index);
int  buddy_get_species_count(void);
const char *buddy_get_species_name(int index);

/* Animation */
void buddy_tick(void);
const char *const *buddy_get_current_frame(void);
uint16_t buddy_get_current_body_color(void);

/* Persistence */
void buddy_save_stats(void);
void buddy_load_stats(void);
