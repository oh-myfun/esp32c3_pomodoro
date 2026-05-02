#pragma once

#include <stdbool.h>
#include <stdint.h>

// BLE heartbeat data (parsed from Claude desktop JSON)
typedef struct {
    int total;              // total sessions
    int running;            // actively generating sessions
    int waiting;            // sessions blocked on permission
    char msg[128];          // one-line summary
    char prompt_id[64];     // permission request ID (empty if no prompt)
    char prompt_tool[32];   // tool name (e.g. "Bash")
    char prompt_hint[256];  // tool hint (e.g. "rm -rf /tmp/foo")
    bool has_prompt;        // true when permission decision needed
} ble_heartbeat_t;

typedef struct {
    void (*on_connected)(void);
    void (*on_disconnected)(void);
    void (*on_heartbeat)(const ble_heartbeat_t *hb);
} ble_callbacks_t;

int  ble_service_init(void);
void ble_service_register_callbacks(const ble_callbacks_t *cbs);
void ble_service_send_permission(const char *id, const char *decision);
void ble_service_send_ack(const char *cmd, bool ok);
bool ble_service_is_connected(void);
void ble_service_tick(void);  // periodic maintenance in Service task
