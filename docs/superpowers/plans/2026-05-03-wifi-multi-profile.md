# WiFi 多网络存储实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 支持保存最多 10 个 WiFi 网络，开机扫描匹配信号最强的已保存网络自动连接，支持手动删除。

**Architecture:** 在 NVS `wifi` 命名空间中用 `ssid_0`~`ssid_9` / `pwd_0`~`pwd_9` 键值对存储，`count` 键记录数量。存储层封装增删查，WiFi 服务层负责开机扫描匹配和断线重连扫描，UI 层新增已保存网络列表界面。

**Tech Stack:** ESP-IDF v5.5.4, LVGL v9.5.0, NVS, FreeRTOS

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `main/service/storage_service.h` | 修改 | 新增 WiFi profile API 声明 |
| `main/service/storage_service.c` | 修改 | 实现 WiFi profile 增删查 + 向后兼容迁移 |
| `main/service/wifi_service.h` | 修改 | 新增 saved profile 查询 API 声明 |
| `main/service/wifi_service.c` | 修改 | 开机扫描匹配、断线重连扫描、连接成功保存、新增查询 API |
| `main/ui/ui_screen_wifi_saved.h` | 创建 | 已保存网络列表界面头文件 |
| `main/ui/ui_screen_wifi_saved.c` | 创建 | 已保存网络列表界面实现 |
| `main/ui/ui_manager.h` | 修改 | 新增 `UI_SCREEN_WIFI_SAVED` 枚举值 |
| `main/ui/ui_manager.c` | 修改 | 创建并注册 WIFI_SAVED 界面 |
| `main/ui/ui_screen_wifi.c` | 修改 | 扫描列表标记已保存网络（`*` 前缀） |
| `main/ui/ui_screen_settings.c` | 修改 | WiFi 入口跳转改为 WIFI_SAVED |
| `main/main.c` | 修改 | 无改动（WiFi 保存逻辑已在 wifi_service 内部处理） |

---

### Task 1: 存储层 — WiFi Profile CRUD

**Files:**
- Modify: `main/service/storage_service.h`
- Modify: `main/service/storage_service.c`

- [ ] **Step 1: 在 storage_service.h 中添加 WiFi profile API 声明**

在文件末尾 `#pragma once` 保护内添加：

```c
// WiFi multi-profile storage (max 10)
#define WIFI_PROFILE_MAX 10

int  storage_get_wifi_profile_count(void);
void storage_add_wifi_profile(const char *ssid, const char *password);
bool storage_load_wifi_profile(int index, char *ssid, size_t ssid_len, char *password, size_t pwd_len);
void storage_delete_wifi_profile(int index);
void storage_migrate_wifi_config(void);
```

- [ ] **Step 2: 在 storage_service.c 中实现 WiFi profile 函数**

在文件末尾添加：

```c
// ---- WiFi profile storage ----

static const char *KEY_WIFI_COUNT = "count";

int storage_get_wifi_profile_count(void)
{
    int32_t count = 0;
    storage_load_int(STORAGE_NAMESPACE_WIFI, KEY_WIFI_COUNT, &count);
    return (int)count;
}

static void storage_set_wifi_profile_count(int count)
{
    storage_save_int(STORAGE_NAMESPACE_WIFI, KEY_WIFI_COUNT, (int32_t)count);
}

bool storage_load_wifi_profile(int index, char *ssid, size_t ssid_len, char *password, size_t pwd_len)
{
    if (index < 0 || index >= WIFI_PROFILE_MAX) return false;

    char key_ssid[16], key_pwd[16];
    snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", index);
    snprintf(key_pwd, sizeof(key_pwd), "pwd_%d", index);

    bool ok = storage_load_string(STORAGE_NAMESPACE_WIFI, key_ssid, ssid, ssid_len);
    if (ok && password && pwd_len > 0) {
        storage_load_string(STORAGE_NAMESPACE_WIFI, key_pwd, password, pwd_len);
    }
    return ok;
}

void storage_add_wifi_profile(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) return;

    int count = storage_get_wifi_profile_count();

    // Check if SSID already exists — if so, remove old entry
    for (int i = 0; i < count; i++) {
        char existing_ssid[33] = {0};
        if (storage_load_wifi_profile(i, existing_ssid, sizeof(existing_ssid), NULL, 0)) {
            if (strcmp(existing_ssid, ssid) == 0) {
                // Shift remaining entries forward
                for (int j = i; j < count - 1; j++) {
                    char tmp_ssid[33] = {0}, tmp_pwd[65] = {0};
                    storage_load_wifi_profile(j + 1, tmp_ssid, sizeof(tmp_ssid), tmp_pwd, sizeof(tmp_pwd));
                    char ks[16], kp[16];
                    snprintf(ks, sizeof(ks), "ssid_%d", j);
                    snprintf(kp, sizeof(kp), "pwd_%d", j);
                    storage_save_string(STORAGE_NAMESPACE_WIFI, ks, tmp_ssid);
                    storage_save_string(STORAGE_NAMESPACE_WIFI, kp, tmp_pwd);
                }
                count--;
                storage_set_wifi_profile_count(count);
                break;
            }
        }
    }

    // If at max capacity, remove oldest (index 0) and shift
    if (count >= WIFI_PROFILE_MAX) {
        for (int i = 0; i < WIFI_PROFILE_MAX - 1; i++) {
            char tmp_ssid[33] = {0}, tmp_pwd[65] = {0};
            storage_load_wifi_profile(i + 1, tmp_ssid, sizeof(tmp_ssid), tmp_pwd, sizeof(tmp_pwd));
            char ks[16], kp[16];
            snprintf(ks, sizeof(ks), "ssid_%d", i);
            snprintf(kp, sizeof(kp), "pwd_%d", i);
            storage_save_string(STORAGE_NAMESPACE_WIFI, ks, tmp_ssid);
            storage_save_string(STORAGE_NAMESPACE_WIFI, kp, tmp_pwd);
        }
        count = WIFI_PROFILE_MAX - 1;
    }

    // Append new profile
    char key_ssid[16], key_pwd[16];
    snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", count);
    snprintf(key_pwd, sizeof(key_pwd), "pwd_%d", count);
    storage_save_string(STORAGE_NAMESPACE_WIFI, key_ssid, ssid);
    storage_save_string(STORAGE_NAMESPACE_WIFI, key_pwd, password ? password : "");
    storage_set_wifi_profile_count(count + 1);

    ESP_LOGI(TAG, "WiFi profile added: %s (total %d)", ssid, count + 1);
}

void storage_delete_wifi_profile(int index)
{
    int count = storage_get_wifi_profile_count();
    if (index < 0 || index >= count) return;

    const char *deleted_ssid = "";
    char tmp[33] = {0};
    if (storage_load_wifi_profile(index, tmp, sizeof(tmp), NULL, 0)) {
        deleted_ssid = tmp;
    }

    // Shift entries after index forward
    for (int i = index; i < count - 1; i++) {
        char next_ssid[33] = {0}, next_pwd[65] = {0};
        storage_load_wifi_profile(i + 1, next_ssid, sizeof(next_ssid), next_pwd, sizeof(next_pwd));
        char ks[16], kp[16];
        snprintf(ks, sizeof(ks), "ssid_%d", i);
        snprintf(kp, sizeof(kp), "pwd_%d", i);
        storage_save_string(STORAGE_NAMESPACE_WIFI, ks, next_ssid);
        storage_save_string(STORAGE_NAMESPACE_WIFI, kp, next_pwd);
    }

    // Clear last slot
    char ks[16], kp[16];
    snprintf(ks, sizeof(ks), "ssid_%d", count - 1);
    snprintf(kp, sizeof(kp), "pwd_%d", count - 1);
    nvs_handle_t handle;
    if (nvs_open(STORAGE_NAMESPACE_WIFI, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, ks);
        nvs_erase_key(handle, kp);
        nvs_commit(handle);
        nvs_close(handle);
    }

    storage_set_wifi_profile_count(count - 1);
    ESP_LOGI(TAG, "WiFi profile deleted: %s (total %d)", deleted_ssid, count - 1);
}

void storage_migrate_wifi_config(void)
{
    // Check if migration needed: old "ssid" key exists but "count" doesn't
    int32_t count = 0;
    bool has_count = storage_load_int(STORAGE_NAMESPACE_WIFI, KEY_WIFI_COUNT, &count);

    char old_ssid[33] = {0};
    bool has_old = storage_load_string(STORAGE_NAMESPACE_WIFI, KEY_WIFI_SSID, old_ssid, sizeof(old_ssid));

    if (!has_count && has_old && strlen(old_ssid) > 0) {
        char old_pwd[65] = {0};
        storage_load_string(STORAGE_NAMESPACE_WIFI, KEY_WIFI_PASSWORD, old_pwd, sizeof(old_pwd));

        // Migrate to profile 0
        storage_save_string(STORAGE_NAMESPACE_WIFI, "ssid_0", old_ssid);
        storage_save_string(STORAGE_NAMESPACE_WIFI, "pwd_0", old_pwd);
        storage_set_wifi_profile_count(1);

        // Erase old keys
        nvs_handle_t handle;
        if (nvs_open(STORAGE_NAMESPACE_WIFI, NVS_READWRITE, &handle) == ESP_OK) {
            nvs_erase_key(handle, KEY_WIFI_SSID);
            nvs_erase_key(handle, KEY_WIFI_PASSWORD);
            nvs_commit(handle);
            nvs_close(handle);
        }

        ESP_LOGI(TAG, "Migrated WiFi config: %s", old_ssid);
    }
}
```

注意：需要在 `storage_service.c` 顶部添加 `#include "nvs.h"` （已有）。

- [ ] **Step 3: 构建**

Run: `./build.sh build`
Expected: 编译通过

- [ ] **Step 4: 提交**

```bash
git add main/service/storage_service.h main/service/storage_service.c
git commit -m "feat: add WiFi profile CRUD storage layer"
```

---

### Task 2: WiFi 服务 — 开机扫描匹配 + 断线重连扫描 + 查询 API

**Files:**
- Modify: `main/service/wifi_service.h`
- Modify: `main/service/wifi_service.c`

- [ ] **Step 1: 在 wifi_service.h 中添加新 API 声明**

在文件末尾添加：

```c
// Saved profile management
int         wifi_service_get_saved_count(void);
const char* wifi_service_get_saved_ssid(int index);
void        wifi_service_delete_saved(int index);
bool        wifi_service_is_saved(const char *ssid);
```

- [ ] **Step 2: 在 wifi_service.c 中添加 saved profiles 缓存和 API 实现**

在文件顶部 static 变量区域添加：

```c
#define MAX_SAVED_PROFILES 10

typedef struct {
    char ssid[33];
    char password[65];
} wifi_saved_profile_t;

static wifi_saved_profile_t saved_profiles[MAX_SAVED_PROFILES];
static int saved_count = 0;
static bool auto_connect_pending = false;
```

在 `wifi_service_init()` 中，替换旧的"加载并连接"逻辑。将原来的：

```c
// Load and apply saved WiFi credentials
char saved_ssid[33] = {0};
char saved_password[65] = {0};
if (storage_load_wifi_config(saved_ssid, sizeof(saved_ssid), saved_password, sizeof(saved_password))) {
    if (strlen(saved_ssid) > 0) {
        ESP_LOGI(TAG, "Loading saved WiFi config: %s", saved_ssid);
        wifi_service_connect(saved_ssid, saved_password);
    }
}
```

替换为：

```c
// Migrate old config and load saved profiles
storage_migrate_wifi_config();
wifi_service_load_profiles();

// Auto-connect: scan first, then connect to strongest saved network
if (saved_count > 0) {
    ESP_LOGI(TAG, "%d saved WiFi profiles, starting auto-connect scan", saved_count);
    auto_connect_pending = true;
    wifi_service_scan();
}
```

添加新函数：

```c
void wifi_service_load_profiles(void)
{
    saved_count = storage_get_wifi_profile_count();
    if (saved_count > MAX_SAVED_PROFILES) saved_count = MAX_SAVED_PROFILES;

    for (int i = 0; i < saved_count; i++) {
        memset(&saved_profiles[i], 0, sizeof(wifi_saved_profile_t));
        storage_load_wifi_profile(i, saved_profiles[i].ssid, sizeof(saved_profiles[i].ssid),
                                  saved_profiles[i].password, sizeof(saved_profiles[i].password));
    }
    ESP_LOGI(TAG, "Loaded %d WiFi profiles", saved_count);
}

int wifi_service_get_saved_count(void)
{
    return saved_count;
}

const char* wifi_service_get_saved_ssid(int index)
{
    if (index < 0 || index >= saved_count) return NULL;
    return saved_profiles[index].ssid;
}

void wifi_service_delete_saved(int index)
{
    if (index < 0 || index >= saved_count) return;
    storage_delete_wifi_profile(index);
    wifi_service_load_profiles();
}

bool wifi_service_is_saved(const char *ssid)
{
    if (!ssid) return false;
    for (int i = 0; i < saved_count; i++) {
        if (strcmp(saved_profiles[i].ssid, ssid) == 0) return true;
    }
    return false;
}
```

- [ ] **Step 3: 修改扫描完成回调，支持开机自动连接**

在 `WIFI_EVENT_SCAN_DONE` case 中，`invoke_on_scan_complete(scan_count)` 调用之后添加：

```c
// Auto-connect: find strongest saved network from scan results
if (auto_connect_pending && scan_count > 0) {
    auto_connect_pending = false;
    int best_idx = -1;
    int best_rssi = -128;

    for (int i = 0; i < scan_count; i++) {
        if (wifi_service_is_saved(scan_results[i].ssid)) {
            if (scan_results[i].rssi > best_rssi) {
                best_rssi = scan_results[i].rssi;
                best_idx = i;
            }
        }
    }

    if (best_idx >= 0) {
        const char *ssid = scan_results[best_idx].ssid;
        // Find saved password
        for (int i = 0; i < saved_count; i++) {
            if (strcmp(saved_profiles[i].ssid, ssid) == 0) {
                ESP_LOGI(TAG, "Auto-connecting to saved network: %s (rssi %d)", ssid, best_rssi);
                wifi_service_connect(ssid, saved_profiles[i].password);
                break;
            }
        }
    } else {
        ESP_LOGI(TAG, "No saved networks found in scan results");
    }
}
```

- [ ] **Step 4: 修改连接成功保存逻辑**

在 `IP_EVENT_STA_GOT_IP` case 中，替换旧的保存逻辑：

旧代码：
```c
// Save WiFi credentials
if (strlen(connected_ssid) > 0) {
    char password[65] = {0};
    wifi_config_t cfg;
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    strncpy(password, (char*)cfg.sta.password, sizeof(password) - 1);
    storage_save_wifi_config(connected_ssid, password);
    ESP_LOGI(TAG, "WiFi config saved: %s", connected_ssid);
}
```

替换为：
```c
// Save WiFi credentials to profile storage
if (strlen(connected_ssid) > 0) {
    char password[65] = {0};
    wifi_config_t cfg;
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    strncpy(password, (char*)cfg.sta.password, sizeof(password) - 1);
    storage_add_wifi_profile(connected_ssid, password);
    wifi_service_load_profiles();
    ESP_LOGI(TAG, "WiFi profile saved: %s", connected_ssid);
}
```

- [ ] **Step 5: 修改断线重连逻辑**

在 `WIFI_EVENT_STA_DISCONNECTED` case 中，替换 `start_reconnect_timer()` 调用为扫描匹配重连。

将原来的：
```c
// Auto-reconnect if not user-initiated
if (!user_initiated_disconnect) {
    start_reconnect_timer();
}
```

替换为：
```c
// Auto-reconnect: rescan to find best saved network
if (!user_initiated_disconnect) {
    if (saved_count > 0) {
        auto_connect_pending = true;
        wifi_service_scan();
    } else {
        start_reconnect_timer();
    }
}
```

- [ ] **Step 6: 在文件顶部添加 forward declaration**

```c
static void wifi_service_load_profiles(void);
```

- [ ] **Step 7: 构建**

Run: `./build.sh build`
Expected: 编译通过

- [ ] **Step 8: 提交**

```bash
git add main/service/wifi_service.h main/service/wifi_service.c
git commit -m "feat: WiFi auto-connect by strongest saved network on boot and reconnect"
```

---

### Task 3: UI — 新增已保存网络列表界面

**Files:**
- Create: `main/ui/ui_screen_wifi_saved.h`
- Create: `main/ui/ui_screen_wifi_saved.c`
- Modify: `main/ui/ui_manager.h` — 添加 `UI_SCREEN_WIFI_SAVED`
- Modify: `main/ui/ui_manager.c` — 创建并注册新界面
- Modify: `main/CMakeLists.txt` — 添加新源文件

- [ ] **Step 1: 创建 ui_screen_wifi_saved.h**

```c
#pragma once

#include "lvgl.h"

lv_obj_t* ui_screen_wifi_saved_create(void);
void ui_screen_wifi_saved_refresh(void);
```

- [ ] **Step 2: 在 ui_manager.h 中添加 WIFI_SAVED 枚举**

在 `UI_SCREEN_PASSWORD_INPUT` 后添加：

```c
    UI_SCREEN_WIFI_SAVED,
```

- [ ] **Step 3: 创建 ui_screen_wifi_saved.c**

```c
#include "ui_screen_wifi_saved.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/wifi_service.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_WIFI_SAVED";

static lv_obj_t *screen = NULL;
static lv_obj_t *wifi_saved_list = NULL;
static lv_obj_t *hint_label = NULL;
static int saved_selected = 0;

static char item_keys[12][33];
static char item_values[12][8];
static ui_list_item_t items[12];

static void update_display(void)
{
    int count = wifi_service_get_saved_count();
    int total = 1 + count;  // "Scan" + saved networks

    snprintf(item_keys[0], sizeof(item_keys[0]), "Scan for new...");
    snprintf(item_values[0], sizeof(item_values[0]), ">");
    items[0].key = item_keys[0];
    items[0].value = item_values[0];

    for (int i = 0; i < count; i++) {
        const char *ssid = wifi_service_get_saved_ssid(i);
        strncpy(item_keys[i + 1], ssid ? ssid : "?", sizeof(item_keys[i + 1]) - 1);
        item_keys[i + 1][sizeof(item_keys[i + 1]) - 1] = '\0';
        snprintf(item_values[i + 1], sizeof(item_values[i + 1]), "");
        items[i + 1].key = item_keys[i + 1];
        items[i + 1].value = item_values[i + 1];
    }

    if (wifi_saved_list) {
        ui_list_set_items(wifi_saved_list, items, total);
        if (saved_selected >= total) saved_selected = total - 1;
        if (saved_selected < 0) saved_selected = 0;
        ui_list_set_selected(wifi_saved_list, saved_selected);
    }
}

static void saved_on_encoder_cw(void)
{
    int total = 1 + wifi_service_get_saved_count();
    saved_selected = (saved_selected + 1) % total;
    update_display();
}

static void saved_on_encoder_ccw(void)
{
    int total = 1 + wifi_service_get_saved_count();
    saved_selected = (saved_selected - 1 + total) % total;
    update_display();
}

static void saved_on_encoder_press(void)
{
    ui_switch_screen(UI_SCREEN_SETTINGS);
}

static void saved_on_settings_press(void)
{
    if (saved_selected == 0) {
        // "Scan for new..."
        wifi_service_scan();
        ui_switch_screen(UI_SCREEN_WIFI_LIST);
    } else {
        // Connect to saved network
        int idx = saved_selected - 1;
        const char *ssid = wifi_service_get_saved_ssid(idx);
        if (ssid && strlen(ssid) > 0) {
            // Load password from storage
            char password[65] = {0};
            storage_load_wifi_profile(idx, NULL, 0, password, sizeof(password));
            wifi_service_connect(ssid, password);
            ui_switch_screen(UI_SCREEN_MAIN);
        }
    }
}

static void saved_on_settings_long_press(void)
{
    if (saved_selected <= 0) return;  // Can't delete "Scan" item

    int idx = saved_selected - 1;
    const char *ssid = wifi_service_get_saved_ssid(idx);
    ESP_LOGI(TAG, "Deleting saved network: %s", ssid ? ssid : "?");
    wifi_service_delete_saved(idx);
    saved_selected = 0;
    update_display();
}

lv_obj_t* ui_screen_wifi_saved_create(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "WiFi");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    wifi_saved_list = ui_list_create(screen, 220, 180, 10, 30);

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, "SET:select|LongSET:del");
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    saved_selected = 0;
    update_display();

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = saved_on_encoder_cw,
        .on_encoder_ccw = saved_on_encoder_ccw,
        .on_encoder_press = saved_on_encoder_press,
        .on_settings_press = saved_on_settings_press,
        .on_encoder_long_press = saved_on_settings_long_press,
    };
    ui_register_input_callbacks(UI_SCREEN_WIFI_SAVED, &cbs);

    ESP_LOGI(TAG, "WiFi saved screen created");
    return screen;
}

void ui_screen_wifi_saved_refresh(void)
{
    update_display();
}
```

注意：需要在 `ui_screen_wifi_saved.c` 顶部添加 `#include "service/storage_service.h"`。

- [ ] **Step 4: 修改 ui_manager.c — 添加头文件引用和界面创建**

在文件顶部添加：
```c
#include "ui_screen_wifi_saved.h"
```

在 `ui_init()` 函数中，在 `screens[UI_SCREEN_PASSWORD_INPUT]` 行之后添加：
```c
    screens[UI_SCREEN_WIFI_SAVED] = ui_screen_wifi_saved_create();
```

- [ ] **Step 5: 检查 input_handler 的 settings_long_press 分发**

检查 `main/input/input_handler.c` 中是否已分发 `on_settings_long_press` 或类似回调。查看 `ui_manager.h` 的 `ui_input_callbacks_t` 结构体 — 当前没有 `on_settings_long_press` 字段，需要用 `on_encoder_long_press` 替代，或在 callbacks 结构体中添加。

**决策：** 复用现有的 `on_encoder_long_press` 回调字段。在 WiFi saved 界面中，`on_encoder_long_press` = 删除操作。这样不需要修改 callbacks 结构体。

更新 Step 3 中 `ui_screen_wifi_saved.c` 的回调注册：将 `on_settings_long_press` 改为使用 `on_encoder_long_press`（编码器长按触发删除，不是 SET 长按）。

**修正：** 需要检查 input_handler 是否支持 SET 键长按。如果不支持，则用编码器长按作为删除操作。

查看 `input_handler.c` 中的按键处理 — 当前支持 `on_encoder_long_press`（编码器长按）。SET 键长按没有单独的回调。因此用 **编码器长按 = 删除** 是现有框架下的可行方案。

最终回调：
```c
    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = saved_on_encoder_cw,
        .on_encoder_ccw = saved_on_encoder_ccw,
        .on_encoder_press = saved_on_encoder_press,
        .on_encoder_long_press = saved_on_encoder_long_press,
        .on_settings_press = saved_on_settings_press,
    };
```

提示文字改为：
```c
    lv_label_set_text(hint_label, "SET:select|EncLong:del");
```

- [ ] **Step 6: 修改 CMakeLists.txt**

在 `main/CMakeLists.txt` 的源文件列表中添加 `ui/ui_screen_wifi_saved.c`。

- [ ] **Step 7: 构建**

Run: `./build.sh build`
Expected: 编译通过

- [ ] **Step 8: 提交**

```bash
git add main/ui/ui_screen_wifi_saved.h main/ui/ui_screen_wifi_saved.c main/ui/ui_manager.h main/ui/ui_manager.c main/CMakeLists.txt
git commit -m "feat: add WiFi saved networks list screen"
```

---

### Task 4: UI — 设置界面跳转 + 扫描列表标记已保存

**Files:**
- Modify: `main/ui/ui_screen_settings.c`
- Modify: `main/ui/ui_screen_wifi.c`

- [ ] **Step 1: 修改设置界面 WiFi 跳转目标**

在 `ui_screen_settings.c` 的 `settings_on_settings_press()` 函数中，将 WiFi 跳转从 `UI_SCREEN_WIFI_LIST` 改为 `UI_SCREEN_WIFI_SAVED`：

```c
        } else if (item == 5) {  // WiFi
            settings_mode = SETTINGS_MODE_IDLE;
            update_display();
            ui_switch_screen(UI_SCREEN_WIFI_SAVED);
```

- [ ] **Step 2: 修改扫描列表，已保存网络显示 `*` 标记**

在 `ui_screen_wifi.c` 的 `ui_screen_wifi_list_update()` 函数中，修改 SSID 显示逻辑。

将原来的：
```c
            if (ap->open) {
                snprintf(wifi_item_keys[i], sizeof(wifi_item_keys[i]), "%.*s[open]", 32 - 6, ap->ssid);
            } else {
                strncpy(wifi_item_keys[i], (char*)ap->ssid, sizeof(wifi_item_keys[i]) - 1);
                wifi_item_keys[i][sizeof(wifi_item_keys[i]) - 1] = '\0';
            }
```

替换为：
```c
            bool is_saved = wifi_service_is_saved((char*)ap->ssid);
            if (ap->open) {
                snprintf(wifi_item_keys[i], sizeof(wifi_item_keys[i]), "%s%.*s[open]",
                         is_saved ? "*" : "", 32 - 8, ap->ssid);
            } else {
                snprintf(wifi_item_keys[i], sizeof(wifi_item_keys[i]), "%s%.*s",
                         is_saved ? "*" : "", 32 - 1, ap->ssid);
            }
```

需要在 `ui_screen_wifi.c` 顶部确保有 `#include "service/wifi_service.h"` （已有）。

- [ ] **Step 3: 修改扫描列表返回导航**

扫描列表的编码器按键目前返回设置界面，应改为返回已保存网络列表：

在 `wifi_on_encoder_press()` 中：
```c
static void wifi_on_encoder_press(void)
{
    ui_switch_screen(UI_SCREEN_WIFI_SAVED);
}
```

密码输入界面的编码器按键也改为返回 WiFi 已保存列表：

在 `pwd_on_encoder_press()` 中：
```c
static void pwd_on_encoder_press(void)
{
    ui_switch_screen(UI_SCREEN_WIFI_SAVED);
}
```

同时更新 `ui_screen_wifi.c` 中所有 `UI_SCREEN_SETTINGS` 引用（WiFi 连接成功/失败后的跳转）改为 `UI_SCREEN_WIFI_SAVED`。

- [ ] **Step 4: 构建**

Run: `./build.sh build`
Expected: 编译通过

- [ ] **Step 5: 提交**

```bash
git add main/ui/ui_screen_settings.c main/ui/ui_screen_wifi.c
git commit -m "feat: settings WiFi entry goes to saved list, scan marks saved networks"
```

---

### Task 5: 集成测试 — 构建烧录验证

**Files:** 无代码改动

- [ ] **Step 1: 构建固件**

Run: `./build.sh build`
Expected: 编译通过，无 warning

- [ ] **Step 2: 烧录到设备**

Run: `./build.sh "-p COM6 flash"`
Expected: 烧录成功，设备重启

- [ ] **Step 3: 验证场景**

手动测试以下场景：

1. **开机自动连接**：设备重启后自动扫描并连接之前保存的 WiFi
2. **旧配置迁移**：如果之前保存过 WiFi，迁移到新的 profile 存储格式
3. **设置 → WiFi**：进入已保存网络列表，显示已保存的 SSID
4. **扫描新网络**：从已保存列表选择 "Scan for new..." 进入扫描列表
5. **已保存标记**：扫描列表中已保存的网络显示 `*` 前缀
6. **连接新网络**：选择未保存网络，输入密码连接，连接成功后自动保存
7. **从已保存列表连接**：在已保存列表中短按 SET 连接已知网络
8. **删除已保存网络**：编码器长按删除已保存网络
9. **超过 10 个**：保存 10+ 个网络，最旧的自动被删除
10. **断线重连**：断开后自动扫描重连到信号最强的已保存网络

- [ ] **Step 4: 修复发现的问题**（如有）
