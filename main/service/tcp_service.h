#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    REQ_PERMISSION = 0,
    REQ_SINGLE_SELECT,
    REQ_MULTI_SELECT,
} request_type_t;

typedef struct {
    char ccbb_request_id[64];
    request_type_t type;
    char tool[32];
    char command[128];
    char hint[128];
    char description[128];
    char question[128];
    struct { char label[32]; char description[64]; } options[8];
    int option_count;
    bool multi_select;
    int focused;
    int selected[8];
    int selected_count;
    char questions_json[512];
    char permission_suggestions_json[512];
} tcp_request_t;

typedef struct {
    void (*on_connected)(void);
    void (*on_disconnected)(void);
    void (*on_request)(const tcp_request_t *req);
    void (*on_session_end)(void);
    void (*on_status)(const char *state, const char *message);
    void (*on_paired)(void);
} tcp_callbacks_t;

int  tcp_service_init(void);
void tcp_service_register_callbacks(const tcp_callbacks_t *cbs);
void tcp_service_connect(const char *host, int port);
void tcp_service_disconnect(void);
bool tcp_service_is_connected(void);
void tcp_service_send_decision(const char *json);
/* NVS config */
bool tcp_service_load_config(char *host, size_t host_len, int *port);
void tcp_service_save_config(const char *host, int port);
void tcp_service_save_pairing_code(const char *code);
bool tcp_service_load_pairing_code(char *code, size_t len);

/* Scan / Discovery */
#define MAX_SCAN_SESSIONS 8

typedef struct {
    char host[48];
    int  port;
    struct { char pairing_code[9]; char project[32]; } sessions[MAX_SCAN_SESSIONS];
    int  session_count;
} tcp_scan_result_t;

void tcp_service_scan(void);
int  tcp_service_get_scan_count(void);
const tcp_scan_result_t *tcp_service_get_scan_result(int index);

/* Project name (set on paired, runtime only) */
const char *tcp_service_get_project(void);
