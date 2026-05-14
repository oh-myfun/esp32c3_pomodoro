#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    char ccbb_request_id[64];
    int type;  /* 0=permission, 1=single_select, 2=multi_select */
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
    void (*on_connected)(void);
    void (*on_disconnected)(void);
    void (*on_request)(const tcp_request_t *req);
    void (*on_session_end)(void);
} tcp_callbacks_t;

int  tcp_service_init(void);
void tcp_service_register_callbacks(const tcp_callbacks_t *cbs);
void tcp_service_connect(const char *host, int port);
void tcp_service_disconnect(void);
bool tcp_service_is_connected(void);
void tcp_service_send_decision(const char *json);
void tcp_service_send_pair(const char *pairing_code);

/* NVS config */
bool tcp_service_load_config(char *host, size_t host_len, int *port);
void tcp_service_save_config(const char *host, int port);
void tcp_service_save_pairing_code(const char *code);
bool tcp_service_load_pairing_code(char *code, size_t len);
