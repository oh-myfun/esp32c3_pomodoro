#pragma once

#include "buddy_render.h"
#include "service/tcp_service.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    buddy_state_t state;
    int species_index;
    uint32_t approved_count;
    uint32_t denied_count;
    bool has_pending_request;
    bool tcp_connected;
    request_type_t request_type;
    int heart_level;
    uint32_t session_approved;
    uint32_t session_denied;
} buddy_info_t;

typedef struct {
    void (*on_state_changed)(buddy_state_t new_state);
    void (*on_decision)(bool approved, const tcp_request_t *req);
} buddy_callbacks_t;

int  buddy_init(void);
void buddy_register_callbacks(const buddy_callbacks_t *cbs);

/* TCP events */
void buddy_on_tcp_connected(void);
void buddy_on_tcp_disconnected(void);
void buddy_on_tcp_request(const tcp_request_t *req);
void buddy_on_tcp_session_end(void);
void buddy_on_status(const char *state, const char *message);

/* User actions */
void buddy_approve(void);
void buddy_deny(void);
void buddy_submit_answer(void);
void buddy_set_answer_labels(const char *labels[], int count, bool multi_select);
void buddy_include_rules(bool include);
bool buddy_should_include_rules(void);
void buddy_trigger_random(void);
/* Info */
buddy_info_t buddy_get_info(void);
int  buddy_get_answer_count(void);
const char *buddy_get_answer_label(int index);
bool buddy_is_answer_multi(void);
void buddy_set_species(int index);
int  buddy_get_species_count(void);
const char *buddy_get_species_name(int index);

/* Animation */
void buddy_tick(void);
uint32_t buddy_get_tick_count(void);
int buddy_get_species_index(void);
/* Persistence */
void buddy_save_stats(void);
void buddy_load_stats(void);
