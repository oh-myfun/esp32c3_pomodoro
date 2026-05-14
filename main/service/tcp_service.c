#include "tcp_service.h"
#include "service/storage_service.h"
#include "cJSON.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "TCP";

/* NVS keys */
#define TCP_NAMESPACE "tcp"
#define KEY_HOST         "host"
#define KEY_PORT         "port"
#define KEY_PAIRING_CODE "pairing_code"

/* Connection parameters */
#define RECONNECT_DELAY_MS  5000
#define RX_BUF_SIZE         2048
#define TX_BUF_SIZE         1024
#define TASK_STACK_SIZE     8192
#define TASK_PRIORITY       2

/* Internal state */
static char cfg_host[128] = {0};
static int  cfg_port = 0;
static volatile bool running = false;
static volatile bool user_disconnect = false;
static int  sock = -1;
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

static void invoke_on_session_end(void)
{
    if (callbacks.on_session_end) {
        callbacks.on_session_end();
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

static void parse_request_message(cJSON *data)
{
    if (!data) return;

    tcp_request_t req;
    memset(&req, 0, sizeof(req));
    req.focused = -1;

    /* ccbb_request_id */
    cJSON *id_item = cJSON_GetObjectItem(data, "ccbb_request_id");
    if (id_item && cJSON_IsString(id_item)) {
        strncpy(req.ccbb_request_id, id_item->valuestring, sizeof(req.ccbb_request_id) - 1);
    }

    /* context.tool_name */
    cJSON *context = cJSON_GetObjectItem(data, "context");
    const char *tool_name = NULL;
    if (context) {
        cJSON *tn = cJSON_GetObjectItem(context, "tool_name");
        if (tn && cJSON_IsString(tn)) {
            tool_name = tn->valuestring;
            strncpy(req.tool, tool_name, sizeof(req.tool) - 1);
        }
    }

    /* hint: context.tool_input.description or .command */
    cJSON *tool_input = context ? cJSON_GetObjectItem(context, "tool_input") : NULL;
    if (tool_input) {
        cJSON *desc = cJSON_GetObjectItem(tool_input, "description");
        if (desc && cJSON_IsString(desc)) {
            strncpy(req.hint, desc->valuestring, sizeof(req.hint) - 1);
        } else {
            cJSON *cmd = cJSON_GetObjectItem(tool_input, "command");
            if (cmd && cJSON_IsString(cmd)) {
                strncpy(req.hint, cmd->valuestring, sizeof(req.hint) - 1);
            }
        }
    }

    if (tool_name && strcmp(tool_name, "AskUserQuestion") == 0) {
        /* AskUserQuestion: parse question + options */
        cJSON *questions = tool_input ? cJSON_GetObjectItem(tool_input, "questions") : NULL;
        cJSON *q0 = questions ? cJSON_GetArrayItem(questions, 0) : NULL;

        if (q0) {
            /* question text */
            cJSON *q_text = cJSON_GetObjectItem(q0, "question");
            if (q_text && cJSON_IsString(q_text)) {
                strncpy(req.question, q_text->valuestring, sizeof(req.question) - 1);
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
                        strncpy(req.options[i].label, lbl->valuestring, sizeof(req.options[i].label) - 1);
                    }
                    cJSON *dsc = cJSON_GetObjectItem(opt, "description");
                    if (dsc && cJSON_IsString(dsc)) {
                        strncpy(req.options[i].description, dsc->valuestring, sizeof(req.options[i].description) - 1);
                    }
                }
            }

            /* Store raw questions JSON for response building */
            char *qjson = cJSON_PrintUnformatted(q0);
            if (qjson) {
                strncpy(req.questions_json, qjson, sizeof(req.questions_json) - 1);
                cJSON_free(qjson);
            }
        }

        /* default focus on first option */
        req.focused = 0;
    } else {
        /* Permission request */
        req.type = 0;
        strncpy(req.options[0].label, "Allow", sizeof(req.options[0].label) - 1);
        strncpy(req.options[1].label, "Deny", sizeof(req.options[1].label) - 1);
        req.option_count = 2;
        req.focused = 0;  /* default Allow */
    }

    ESP_LOGI(TAG, "Request parsed: type=%d tool=%s id=%s", req.type, req.tool, req.ccbb_request_id);
    invoke_on_request(&req);
}

static void dispatch_message(const char *type_str, cJSON *root)
{
    cJSON *data = cJSON_GetObjectItem(root, "data");

    if (strcmp(type_str, "waiting_pairing") == 0) {
        ESP_LOGI(TAG, "Waiting for pairing...");
    } else if (strcmp(type_str, "paired") == 0) {
        ESP_LOGI(TAG, "Paired successfully!");
    } else if (strcmp(type_str, "pairing_pending") == 0) {
        ESP_LOGI(TAG, "Pairing pending...");
    } else if (strcmp(type_str, "pairing_failed") == 0) {
        ESP_LOGW(TAG, "Pairing failed");
    } else if (strcmp(type_str, "request") == 0) {
        parse_request_message(data);
    } else if (strcmp(type_str, "done") == 0) {
        ESP_LOGI(TAG, "Request done");
    } else if (strcmp(type_str, "session_end") == 0) {
        ESP_LOGI(TAG, "Session ended");
        invoke_on_session_end();
    } else {
        ESP_LOGD(TAG, "Unknown message type: %s", type_str);
    }
}

/* ------------------------------------------------------------------ */
/* Line-based receive logic                                           */
/* ------------------------------------------------------------------ */

/*
 * Buffered line reader: accumulates bytes into rx_buf, extracts complete
 * \n-terminated lines, and dispatches each as a JSON message.
 * Returns false on connection error.
 */
static bool recv_and_dispatch(void)
{
    static char rx_buf[RX_BUF_SIZE];
    static int rx_len = 0;

    int n = lwip_read(sock, rx_buf + rx_len, sizeof(rx_buf) - rx_len - 1);
    if (n <= 0) {
        return false;  /* disconnected or error */
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
    int idle = 5;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    int interval = 3;
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
        xSemaphoreGive(send_mutex);
    }

    invoke_on_connected();

    /* Receive loop */
    while (running && !user_disconnect) {
        if (!recv_and_dispatch()) {
            break;
        }
    }

    /* Disconnected */
    lwip_close(sock);
    sock = -1;
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
    cfg_port = port;
    user_disconnect = false;

    /* Save config to NVS */
    tcp_service_save_config(host, port);

    /* Start task if not running */
    if (!running) {
        running = true;
        xTaskCreate(tcp_task, "tcp_task", TASK_STACK_SIZE, NULL, TASK_PRIORITY, &tcp_task_handle);
    }
}

void tcp_service_disconnect(void)
{
    user_disconnect = true;

    if (sock >= 0) {
        lwip_close(sock);
        sock = -1;
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

void tcp_service_send_pair(const char *pairing_code)
{
    if (sock < 0 || !pairing_code) return;

    if (xSemaphoreTake(send_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGW(TAG, "Send mutex timeout");
        return;
    }

    char tx_buf[256];
    int len = snprintf(tx_buf, sizeof(tx_buf),
        "{\"type\":\"pair\",\"data\":{\"pairing_code\":\"%s\"}}\n", pairing_code);
    if (len > 0 && len < (int)sizeof(tx_buf)) {
        if (!send_raw(tx_buf, len)) {
            ESP_LOGW(TAG, "Pair send failed");
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
