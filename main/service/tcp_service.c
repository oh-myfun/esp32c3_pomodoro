#include "tcp_service.h"
#include "service/storage_service.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

static const char *TAG = "TCP";

/* NVS keys */
#define TCP_NAMESPACE "tcp"
#define KEY_HOST         "host"
#define KEY_PORT         "port"
#define KEY_PAIRING_CODE "pairing_code"

/* Connection parameters */
#define RECONNECT_DELAY_MS  15000
#define RX_BUF_SIZE         8192
#define TX_BUF_SIZE         1024
#define TASK_STACK_SIZE     9216
#define TASK_PRIORITY       3

/* Internal state */
static char cfg_host[128] = {0};
static int  cfg_port = 0;
static volatile bool running = false;
static volatile bool user_disconnect = false;
static int  sock = -1;
static char s_project[32] = {0};
static SemaphoreHandle_t send_mutex = NULL;
static TaskHandle_t tcp_task_handle = NULL;
static tcp_callbacks_t callbacks = {0};

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void invoke_on_connected(void)
{
    if (callbacks.on_connected) {
        callbacks.on_connected();
    }
}

static void invoke_on_disconnected(void)
{
    if (callbacks.on_disconnected) {
        callbacks.on_disconnected();
    }
}

static void invoke_on_request(const tcp_request_t *req)
{
    if (callbacks.on_request) {
        callbacks.on_request(req);
    }
}

static void invoke_on_paired(void)
{
    if (callbacks.on_paired) {
        callbacks.on_paired();
    }
}

static void invoke_on_session_end(void)
{
    if (callbacks.on_session_end) {
        callbacks.on_session_end();
    }
}

static void invoke_on_status(const char *state, const char *message)
{
    if (callbacks.on_status) {
        callbacks.on_status(state, message);
    }
}

/* Send a raw string over the socket (caller must hold send_mutex) */
static bool send_raw(const char *data, size_t len)
{
    if (sock < 0) return false;

    size_t sent = 0;
    while (sent < len) {
        int n = lwip_write(sock, data + sent, len - sent);
        if (n <= 0) {
            ESP_LOGW(TAG, "Send failed");
            return false;
        }
        sent += n;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Message parsing                                                    */
/* ------------------------------------------------------------------ */

/* Safe string copy: always null-terminated, never overflows */
static void safe_strcpy(char *dst, const char *src, size_t dst_size)
{
    if (!dst || dst_size == 0) return;
    if (src) {
        size_t len = strlen(src);
        if (len >= dst_size) len = dst_size - 1;
        memcpy(dst, src, len);
        dst[len] = '\0';
    } else {
        dst[0] = '\0';
    }
}

/* Format permission_suggestions JSON into readable text
 * Format: +Tool: rule → behavior (scope)
 * e.g. "+Bash: npm install → allow (local)"
 */
static void format_suggestions_text(const cJSON *suggestions, char *out, size_t out_size)
{
    if (!suggestions || !cJSON_IsArray(suggestions) || !out || out_size == 0) {
        if (out) out[0] = '\0';
        return;
    }

    int off = 0;
    int count = cJSON_GetArraySize(suggestions);

    for (int i = 0; i < count && off < (int)out_size - 2; i++) {
        const cJSON *item = cJSON_GetArrayItem(suggestions, i);
        if (!item) continue;

        const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(item, "type"));
        const char *behavior = cJSON_GetStringValue(cJSON_GetObjectItem(item, "behavior"));
        const char *dest = cJSON_GetStringValue(cJSON_GetObjectItem(item, "destination"));
        const cJSON *rules = cJSON_GetObjectItem(item, "rules");

        /* Type prefix */
        char prefix = '+';  /* default addRules */
        if (type) {
            if (strcmp(type, "removeRules") == 0)       prefix = '-';
            else if (strcmp(type, "replaceRules") == 0)  prefix = '=';
            else if (strcmp(type, "setMode") == 0)       prefix = 'M';
            else if (strcmp(type, "addDirectories") == 0) prefix = '>';
            else if (strcmp(type, "removeDirectories") == 0) prefix = '<';
        }

        /* Destination short name */
        const char *scope = "";
        if (dest) {
            if (strcmp(dest, "session") == 0)            scope = "tmp";
            else if (strcmp(dest, "localSettings") == 0)  scope = "local";
            else if (strcmp(dest, "projectSettings") == 0) scope = "proj";
            else if (strcmp(dest, "userSettings") == 0)    scope = "user";
        }

        if (rules && cJSON_IsArray(rules)) {
            int rule_count = cJSON_GetArraySize(rules);
            for (int r = 0; r < rule_count && off < (int)out_size - 2; r++) {
                const cJSON *rule = cJSON_GetArrayItem(rules, r);
                const char *tool_name = cJSON_GetStringValue(cJSON_GetObjectItem(rule, "toolName"));
                const char *rule_content = cJSON_GetStringValue(cJSON_GetObjectItem(rule, "ruleContent"));

                if (off > 0) off += snprintf(out + off, out_size - off, "\n");
                if (off >= (int)out_size - 1) break;

                off += snprintf(out + off, out_size - off, "%c%s",
                                prefix, tool_name ? tool_name : "?");
                if (rule_content) {
                    off += snprintf(out + off, out_size - off, ": %s", rule_content);
                }
                if (behavior) {
                    off += snprintf(out + off, out_size - off, " \xe2\x86\x92 %s", behavior);
                }
                if (scope[0]) {
                    off += snprintf(out + off, out_size - off, " (%s)", scope);
                }
            }
        } else {
            /* No rules array, just show type summary */
            if (off > 0) off += snprintf(out + off, out_size - off, "\n");
            if (off >= (int)out_size - 1) break;
            off += snprintf(out + off, out_size - off, "%c", prefix);
            if (behavior) off += snprintf(out + off, out_size - off, " \xe2\x86\x92 %s", behavior);
            if (scope[0]) off += snprintf(out + off, out_size - off, " (%s)", scope);
        }
    }

    out[off < (int)out_size ? off : (int)out_size - 1] = '\0';
}

static void parse_request_message(cJSON *data)
{
    if (!data) {
        ESP_LOGW(TAG, "Request message has no data");
        return;
    }

    tcp_request_t req;
    memset(&req, 0, sizeof(req));
    req.focused = -1;

    /* ccbb_request_id — required */
    cJSON *id_item = cJSON_GetObjectItem(data, "ccbb_request_id");
    if (id_item && cJSON_IsString(id_item)) {
        safe_strcpy(req.ccbb_request_id, id_item->valuestring, sizeof(req.ccbb_request_id));
    } else {
        ESP_LOGW(TAG, "Request missing ccbb_request_id");
        /* Still process — use fallback for display, decision will be best-effort */
        safe_strcpy(req.ccbb_request_id, "unknown", sizeof(req.ccbb_request_id));
    }

    /* tool_name — directly in data */
    const char *tool_name = NULL;
    cJSON *tn = cJSON_GetObjectItem(data, "tool_name");
    if (tn && cJSON_IsString(tn)) {
        tool_name = tn->valuestring;
        safe_strcpy(req.tool, tool_name, sizeof(req.tool));
    } else {
        ESP_LOGW(TAG, "Request missing tool_name");
        safe_strcpy(req.tool, "?", sizeof(req.tool));
    }

    /* Extract command + description from tool_input */
    cJSON *tool_input = cJSON_GetObjectItem(data, "tool_input");
    if (tool_input && cJSON_IsObject(tool_input)) {
        /* Command: the actual operation (command > file_path > url > path > ...) */
        static const char *cmd_keys[] = {"command", "file_path", "url", "path", "pattern", "query", "prompt", "input"};
        for (int i = 0; i < 8 && req.command[0] == '\0'; i++) {
            cJSON *v = cJSON_GetObjectItem(tool_input, cmd_keys[i]);
            if (v && cJSON_IsString(v) && v->valuestring[0]) {
                safe_strcpy(req.command, v->valuestring, sizeof(req.command));
            }
        }
        /* Description: human-readable summary */
        cJSON *desc = cJSON_GetObjectItem(tool_input, "description");
        if (desc && cJSON_IsString(desc) && desc->valuestring[0]) {
            safe_strcpy(req.description, desc->valuestring, sizeof(req.description));
        }
        /* Fallback: if no command found, use description as command */
        if (req.command[0] == '\0' && req.description[0] != '\0') {
            safe_strcpy(req.command, req.description, sizeof(req.command));
        }
        /* Fallback hint = command for backwards compat */
        if (req.hint[0] == '\0' && req.command[0] != '\0') {
            safe_strcpy(req.hint, req.command, sizeof(req.hint));
        }
    }

    if (tool_name && strcmp(tool_name, "AskUserQuestion") == 0) {
        /* AskUserQuestion: parse question + options */
        cJSON *questions = cJSON_GetObjectItem(tool_input, "questions");
        cJSON *q0 = (questions && cJSON_IsArray(questions)) ? cJSON_GetArrayItem(questions, 0) : NULL;

        if (q0) {
            /* question text */
            cJSON *q_text = cJSON_GetObjectItem(q0, "question");
            if (q_text && cJSON_IsString(q_text)) {
                safe_strcpy(req.question, q_text->valuestring, sizeof(req.question));
            }

            /* multiSelect */
            cJSON *ms = cJSON_GetObjectItem(q0, "multiSelect");
            req.multi_select = (ms && cJSON_IsBool(ms) && cJSON_IsTrue(ms));
            req.type = req.multi_select ? 2 : 1;

            /* options */
            cJSON *opts = cJSON_GetObjectItem(q0, "options");
            if (opts && cJSON_IsArray(opts)) {
                int count = cJSON_GetArraySize(opts);
                if (count > 8) count = 8;
                req.option_count = count;
                for (int i = 0; i < count; i++) {
                    cJSON *opt = cJSON_GetArrayItem(opts, i);
                    if (!opt) continue;
                    cJSON *lbl = cJSON_GetObjectItem(opt, "label");
                    if (lbl && cJSON_IsString(lbl)) {
                        safe_strcpy(req.options[i].label, lbl->valuestring, sizeof(req.options[i].label));
                    }
                    cJSON *dsc = cJSON_GetObjectItem(opt, "description");
                    if (dsc && cJSON_IsString(dsc)) {
                        safe_strcpy(req.options[i].description, dsc->valuestring, sizeof(req.options[i].description));
                    }
                }
            } else {
                ESP_LOGW(TAG, "AskUserQuestion missing options array");
            }

            /* Store raw questions JSON for response building */
            char *qjson = cJSON_PrintUnformatted(q0);
            if (qjson) {
                safe_strcpy(req.questions_json, qjson, sizeof(req.questions_json));
                cJSON_free(qjson);
            }
        } else {
            ESP_LOGW(TAG, "AskUserQuestion missing questions array");
        }

        /* default focus on first option */
        req.focused = 0;
    } else {
        /* Permission request */
        req.type = 0;
        safe_strcpy(req.options[0].label, "Allow", sizeof(req.options[0].label));
        safe_strcpy(req.options[1].label, "Deny", sizeof(req.options[1].label));
        req.option_count = 2;
        req.focused = 0;

        /* Parse permission_suggestions if present */
        cJSON *suggestions = cJSON_GetObjectItem(data, "permission_suggestions");
        if (suggestions && cJSON_IsArray(suggestions) && cJSON_GetArraySize(suggestions) > 0) {
            char *sjson = cJSON_PrintUnformatted(suggestions);
            if (sjson) {
                safe_strcpy(req.permission_suggestions_json, sjson, sizeof(req.permission_suggestions_json));
                cJSON_free(sjson);
            }
            format_suggestions_text(suggestions, req.permission_suggestions_text,
                                   sizeof(req.permission_suggestions_text));
        }
    }

    /* For AskUserQuestion: use question text as command display */
    if (req.question[0] != '\0') {
        if (req.command[0] == '\0') {
            safe_strcpy(req.command, req.question, sizeof(req.command));
        }
        if (req.hint[0] == '\0') {
            safe_strcpy(req.hint, req.question, sizeof(req.hint));
        }
    }

    ESP_LOGI(TAG, "Request parsed: type=%d tool=%s id=%s cmd=%.40s desc=%.40s",
             req.type, req.tool, req.ccbb_request_id,
             req.command, req.description);
    invoke_on_request(&req);
}

static void dispatch_message(const char *type_str, cJSON *root)
{
    cJSON *data = cJSON_GetObjectItem(root, "data");

    if (strcmp(type_str, "waiting_pairing") == 0) {
        ESP_LOGI(TAG, "Waiting for pairing...");
    } else if (strcmp(type_str, "paired") == 0) {
        ESP_LOGI(TAG, "Paired successfully!");
        cJSON *proj = cJSON_GetObjectItem(data, "project");
        if (proj && cJSON_IsString(proj) && proj->valuestring[0]) {
            safe_strcpy(s_project, proj->valuestring, sizeof(s_project));
            ESP_LOGI(TAG, "Project: %s", s_project);
        } else {
            s_project[0] = '\0';
        }
        invoke_on_paired();
    } else if (strcmp(type_str, "pairing_pending") == 0) {
        ESP_LOGI(TAG, "Pairing pending...");
    } else if (strcmp(type_str, "pairing_failed") == 0) {
        ESP_LOGW(TAG, "Pairing failed");
    } else if (strcmp(type_str, "request") == 0) {
        parse_request_message(data);
    } else if (strcmp(type_str, "done") == 0) {
        ESP_LOGI(TAG, "Request done");
        invoke_on_session_end();
    } else if (strcmp(type_str, "session_end") == 0) {
        ESP_LOGI(TAG, "Session ended");
        invoke_on_session_end();
    } else if (strcmp(type_str, "status") == 0) {
        if (data) {
            cJSON *st = cJSON_GetObjectItem(data, "state");
            cJSON *msg = cJSON_GetObjectItem(data, "message");
            const char *state_str = (st && cJSON_IsString(st)) ? st->valuestring : "";
            const char *msg_str = (msg && cJSON_IsString(msg)) ? msg->valuestring : "";
            ESP_LOGI(TAG, "Status received: state='%s' msg='%s'", state_str, msg_str);
            invoke_on_status(state_str, msg_str);
        } else {
            ESP_LOGW(TAG, "Status message with no data field");
        }
    } else {
        ESP_LOGW(TAG, "Unhandled message type: '%s'", type_str);
        invoke_on_status("error", "unknown message");
    }
}

/* ------------------------------------------------------------------ */
/* Line-based receive logic                                           */
/* ------------------------------------------------------------------ */

static char rx_buf[RX_BUF_SIZE];
static int rx_len = 0;

static void rx_buf_reset(void)
{
    rx_len = 0;
    rx_buf[0] = '\0';
}

/*
 * Buffered line reader: accumulates bytes into rx_buf, extracts complete
 * \n-terminated lines, and dispatches each as a JSON message.
 * Returns false on connection error.
 */
static bool recv_and_dispatch(void)
{
    int space = (int)sizeof(rx_buf) - rx_len - 1;
    if (space <= 0) {
        /* Buffer full — scan for last newline to salvage what we can */
        char *last_nl = strrchr(rx_buf, '\n');
        if (last_nl) {
            int keep = (rx_buf + rx_len) - (last_nl + 1);
            if (keep > 0) {
                memmove(rx_buf, last_nl + 1, keep);
            }
            rx_len = keep;
        } else {
            /* No newline at all — single oversized message, discard it */
            ESP_LOGW(TAG, "Oversized message, discarding %d bytes", rx_len);
            rx_len = 0;
        }
        space = (int)sizeof(rx_buf) - rx_len - 1;
    }

    int n = lwip_read(sock, rx_buf + rx_len, space);
    if (n <= 0) {
        ESP_LOGW(TAG, "Read returned %d (errno=%d)", n, errno);
        return false;
    }
    rx_len += n;
    rx_buf[rx_len] = '\0';

    /* Extract all complete lines */
    char *line_start = rx_buf;
    char *newline;
    while ((newline = strchr(line_start, '\n')) != NULL) {
        *newline = '\0';

        /* Skip empty lines */
        if (line_start[0] != '\0') {
            cJSON *root = cJSON_Parse(line_start);
            if (root) {
                cJSON *type_item = cJSON_GetObjectItem(root, "type");
                if (type_item && cJSON_IsString(type_item)) {
                    dispatch_message(type_item->valuestring, root);
                }
                cJSON_Delete(root);
            } else {
                ESP_LOGW(TAG, "Invalid JSON: %.80s", line_start);
            }
        }

        line_start = newline + 1;
    }

    /* Shift remaining partial data to front */
    int remaining = (rx_buf + rx_len) - line_start;
    if (remaining > 0 && line_start != rx_buf) {
        memmove(rx_buf, line_start, remaining);
    }
    rx_len = remaining;

    return true;
}

/* ------------------------------------------------------------------ */
/* TCP task                                                           */
/* ------------------------------------------------------------------ */

static void do_connect(void)
{
    rx_buf_reset();

    /* DNS resolve */
    struct hostent *he = gethostbyname(cfg_host);
    if (!he) {
        ESP_LOGW(TAG, "DNS resolve failed for %s", cfg_host);
        return;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(cfg_port);
    memcpy(&dest.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));

    /* Create socket */
    sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGW(TAG, "Socket create failed");
        return;
    }

    /* TCP keepalive */
    int keepalive = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    int idle = 30;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    int interval = 10;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    int count = 3;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));

    /* Connect */
    ESP_LOGI(TAG, "Connecting to %s:%d...", cfg_host, cfg_port);
    if (lwip_connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        ESP_LOGW(TAG, "Connect failed");
        lwip_close(sock);
        sock = -1;
        return;
    }

    ESP_LOGI(TAG, "Connected to %s:%d", cfg_host, cfg_port);

    /* Send hello */
    const char *hello = "{\"type\":\"hello\",\"data\":{}}\n";
    if (xSemaphoreTake(send_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (!send_raw(hello, strlen(hello))) {
            ESP_LOGW(TAG, "Failed to send hello");
            xSemaphoreGive(send_mutex);
            lwip_close(sock);
            sock = -1;
            return;
        }
        /* Send pairing code if configured */
        char pair_code[9] = {0};
        if (storage_load_string(TCP_NAMESPACE, KEY_PAIRING_CODE, pair_code, sizeof(pair_code)) && pair_code[0]) {
            char pair_msg[128];
            int plen = snprintf(pair_msg, sizeof(pair_msg),
                "{\"type\":\"pair\",\"data\":{\"pairing_code\":\"%s\"}}\n", pair_code);
            if (plen > 0 && plen < (int)sizeof(pair_msg)) {
                send_raw(pair_msg, plen);
                ESP_LOGI(TAG, "Pairing code sent");
            }
        }
        xSemaphoreGive(send_mutex);
    }

    s_project[0] = '\0';
    invoke_on_connected();

    /* Receive loop */
    while (running && !user_disconnect) {
        if (!recv_and_dispatch()) {
            break;
        }
    }

    /* Disconnected — graceful close so remote detects EOF */
    lwip_shutdown(sock, SHUT_WR);
    lwip_close(sock);
    sock = -1;
    s_project[0] = '\0';
    invoke_on_disconnected();
    ESP_LOGW(TAG, "Disconnected");
}

static void tcp_task(void *arg)
{
    ESP_LOGI(TAG, "TCP task started");

    while (running) {
        if (user_disconnect) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (cfg_host[0] == '\0' || cfg_port == 0) {
            /* No config yet, wait */
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }

        do_connect();

        /* Reconnect delay */
        if (running && !user_disconnect) {
            ESP_LOGI(TAG, "Reconnecting in %d ms...", RECONNECT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
        }
    }

    ESP_LOGI(TAG, "TCP task stopped");
    tcp_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int tcp_service_init(void)
{
    send_mutex = xSemaphoreCreateMutex();
    if (!send_mutex) {
        ESP_LOGE(TAG, "Failed to create send mutex");
        return -1;
    }
    ESP_LOGI(TAG, "TCP service initialized");
    return 0;
}

void tcp_service_register_callbacks(const tcp_callbacks_t *cbs)
{
    if (cbs) {
        callbacks = *cbs;
    }
}

void tcp_service_connect(const char *host, int port)
{
    strncpy(cfg_host, host, sizeof(cfg_host) - 1);
    cfg_host[sizeof(cfg_host) - 1] = '\0';
    cfg_port = port;
    user_disconnect = false;

    /* Save config to NVS */
    tcp_service_save_config(host, port);

    if (!running) {
        /* Start task if not running */
        running = true;
        xTaskCreate(tcp_task, "tcp_task", TASK_STACK_SIZE, NULL, TASK_PRIORITY, &tcp_task_handle);
    } else if (sock >= 0) {
        /* Force-close existing connection so task reconnects with new config */
        lwip_shutdown(sock, SHUT_RDWR);
    }
}

void tcp_service_disconnect(void)
{
    user_disconnect = true;

    /* Interrupt blocking read — TCP task will close the socket itself */
    if (sock >= 0) {
        lwip_shutdown(sock, SHUT_WR);
    }
}

bool tcp_service_is_connected(void)
{
    return sock >= 0;
}

void tcp_service_send_decision(const char *json)
{
    if (sock < 0 || !json) return;

    if (xSemaphoreTake(send_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGW(TAG, "Send mutex timeout");
        return;
    }

    /* Build: {"type":"decision","data":<json>}\n */
    char tx_buf[TX_BUF_SIZE];
    int len = snprintf(tx_buf, sizeof(tx_buf), "{\"type\":\"decision\",\"data\":%s}\n", json);
    if (len > 0 && len < (int)sizeof(tx_buf)) {
        if (!send_raw(tx_buf, len)) {
            ESP_LOGW(TAG, "Decision send failed");
        }
    }

    xSemaphoreGive(send_mutex);
}

/* ------------------------------------------------------------------ */
/* NVS config                                                         */
/* ------------------------------------------------------------------ */

bool tcp_service_load_config(char *host, size_t host_len, int *port)
{
    bool ok = storage_load_string(TCP_NAMESPACE, KEY_HOST, host, host_len);
    int32_t p = 0;
    ok &= storage_load_int(TCP_NAMESPACE, KEY_PORT, &p);
    if (port) *port = (int)p;
    return ok;
}

void tcp_service_save_config(const char *host, int port)
{
    storage_save_string(TCP_NAMESPACE, KEY_HOST, host);
    storage_save_int(TCP_NAMESPACE, KEY_PORT, (int32_t)port);
}

void tcp_service_save_pairing_code(const char *code)
{
    storage_save_string(TCP_NAMESPACE, KEY_PAIRING_CODE, code);
}

bool tcp_service_load_pairing_code(char *code, size_t len)
{
    return storage_load_string(TCP_NAMESPACE, KEY_PAIRING_CODE, code, len);
}

void tcp_service_repair(const char *pairing_code)
{
    if (!pairing_code || !pairing_code[0]) return;
    if (sock < 0) {
        ESP_LOGW(TAG, "Cannot re-pair: not connected");
        return;
    }

    /* Save new pairing code to NVS */
    tcp_service_save_pairing_code(pairing_code);

    /* Send pair message on existing connection */
    char pair_msg[128];
    int plen = snprintf(pair_msg, sizeof(pair_msg),
        "{\"type\":\"pair\",\"data\":{\"pairing_code\":\"%s\"}}\n", pairing_code);

    if (xSemaphoreTake(send_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (send_raw(pair_msg, plen)) {
            ESP_LOGI(TAG, "Re-pair sent with code: %s", pairing_code);
        } else {
            ESP_LOGW(TAG, "Re-pair send failed");
        }
        xSemaphoreGive(send_mutex);
    }
}

#define KEY_PROJECT "project"

const char *tcp_service_get_project(void)
{
    return s_project[0] ? s_project : NULL;
}

/* ------------------------------------------------------------------ */
/* UDP Broadcast Scan / Discovery                                     */
/* ------------------------------------------------------------------ */

#define MAX_SCAN_RESULTS 8
#define SCAN_TIMEOUT_MS  3000

static tcp_scan_result_t s_scan_results[MAX_SCAN_RESULTS];
static int s_scan_count = 0;
static bool s_scan_busy = false;

static bool is_host_duplicated(const char *host, int count)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(s_scan_results[i].host, host) == 0) {
            return true;
        }
    }
    return false;
}

static void scan_task(void *arg)
{
    int udp_sock = -1;
    int found = 0;

    udp_sock = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        ESP_LOGW(TAG, "Scan: socket create failed");
        s_scan_busy = false;
        vTaskDelete(NULL);
        return;
    }

    /* Allow broadcast */
    int broadcast = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    /* Receive timeout */
    struct timeval tv = {
        .tv_sec = SCAN_TIMEOUT_MS / 1000,
        .tv_usec = (SCAN_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Bind to any port */
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(0);

    if (lwip_bind(udp_sock, (struct sockaddr *)&local, sizeof(local)) != 0) {
        ESP_LOGW(TAG, "Scan: bind failed");
        lwip_close(udp_sock);
        s_scan_busy = false;
        vTaskDelete(NULL);
        return;
    }

    /* Send discover broadcast */
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(9876);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const char *discover_msg = "{\"type\":\"discover\"}";
    lwip_sendto(udp_sock, discover_msg, strlen(discover_msg), 0,
                (struct sockaddr *)&dest, sizeof(dest));

    ESP_LOGI(TAG, "Scan: broadcast sent, waiting for replies...");

    /* Receive loop */
    int64_t start_ms = esp_timer_get_time() / 1000;
    char rx_buf[512];

    while (1) {
        int64_t elapsed = (esp_timer_get_time() / 1000) - start_ms;
        if (elapsed >= SCAN_TIMEOUT_MS) {
            break;
        }

        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = lwip_recvfrom(udp_sock, rx_buf, sizeof(rx_buf) - 1, 0,
                              (struct sockaddr *)&from, &from_len);
        if (n <= 0) {
            /* Timeout or error - done */
            break;
        }
        rx_buf[n] = '\0';

        ESP_LOGD(TAG, "Scan: received %d bytes: %s", n, rx_buf);

        /* Parse JSON response */
        cJSON *root = cJSON_Parse(rx_buf);
        if (!root) {
            continue;
        }

        cJSON *type_item = cJSON_GetObjectItem(root, "type");
        if (type_item && cJSON_IsString(type_item) &&
            strcmp(type_item->valuestring, "discover_response") == 0) {

            /* Bridge returns flat: {"type":"discover_response","host":"...","port":9876,"sessions":[...]} */
            char host_str[48];
            cJSON *host_item = cJSON_GetObjectItem(root, "host");
            if (host_item && cJSON_IsString(host_item)) {
                safe_strcpy(host_str, host_item->valuestring, sizeof(host_str));
            } else {
                inet_ntop(AF_INET, &from.sin_addr, host_str, sizeof(host_str));
            }

            /* Deduplicate */
            if (is_host_duplicated(host_str, found)) {
                ESP_LOGD(TAG, "Scan: duplicate host %s, skipping", host_str);
                cJSON_Delete(root);
                continue;
            }

            if (found >= MAX_SCAN_RESULTS) {
                cJSON_Delete(root);
                continue;
            }

            /* Extract fields */
            tcp_scan_result_t *r = &s_scan_results[found];
            memset(r, 0, sizeof(*r));
            strncpy(r->host, host_str, sizeof(r->host) - 1);

            cJSON *port_item = cJSON_GetObjectItem(root, "port");
            if (port_item && cJSON_IsNumber(port_item)) {
                r->port = port_item->valueint;
            } else {
                r->port = 9876;
            }

            /* Parse sessions array */
            cJSON *sessions = cJSON_GetObjectItem(root, "sessions");
            if (sessions && cJSON_IsArray(sessions)) {
                int sc = cJSON_GetArraySize(sessions);
                if (sc > MAX_SCAN_SESSIONS) sc = MAX_SCAN_SESSIONS;
                r->session_count = sc;
                for (int si = 0; si < sc; si++) {
                    cJSON *s = cJSON_GetArrayItem(sessions, si);
                    cJSON *pc = s ? cJSON_GetObjectItem(s, "pairing_code") : NULL;
                    if (pc && cJSON_IsString(pc)) {
                        safe_strcpy(r->sessions[si].pairing_code, pc->valuestring,
                                    sizeof(r->sessions[si].pairing_code));
                    }
                    cJSON *pj = s ? cJSON_GetObjectItem(s, "project") : NULL;
                    if (pj && cJSON_IsString(pj) && pj->valuestring[0]) {
                        safe_strcpy(r->sessions[si].project, pj->valuestring,
                                    sizeof(r->sessions[si].project));
                    }
                }
            }

            ESP_LOGI(TAG, "Scan: found %s:%d (%d sessions)",
                     r->host, r->port, r->session_count);
            found++;
        }

        cJSON_Delete(root);
    }

    lwip_close(udp_sock);
    udp_sock = -1;

    /* Publish results atomically */
    s_scan_count = found;
    ESP_LOGI(TAG, "Scan: done, found %d device(s)", found);
    s_scan_busy = false;
    vTaskDelete(NULL);
}

void tcp_service_scan(void)
{
    if (s_scan_busy) {
        ESP_LOGW(TAG, "Scan already in progress");
        return;
    }

    s_scan_busy = true;
    s_scan_count = 0;

    xTaskCreate(scan_task, "tcp_scan", 6144, NULL, TASK_PRIORITY, NULL);
}

bool tcp_service_is_scan_busy(void)
{
    return s_scan_busy;
}

int tcp_service_get_scan_count(void)
{
    return s_scan_count;
}

const tcp_scan_result_t *tcp_service_get_scan_result(int index)
{
    if (index < 0 || index >= s_scan_count) {
        return NULL;
    }
    return &s_scan_results[index];
}
