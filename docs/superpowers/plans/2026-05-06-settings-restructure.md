# 设置页面重构实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将主设置页面从平铺列表重构为 6 个分类导航入口，新建 Buddy/Time/System 三个子设置页，统一 NVS 存储键名，添加 NTP 服务器选择和语言持久化。

**Architecture:** 主设置页简化为纯导航列表，每项 SET 按键导航到子页面。三个新子页面遵循现有 light/pomodoro 子页面的 NAV/ADJUST 模式。storage_service.h 新增键常量，time_service 新增 NTP 服务器索引接口。启动时迁移旧键名。

**Tech Stack:** ESP-IDF v5.5.4, LVGL v9.x, NVS, FreeRTOS

---

## File Structure

### 新建文件
- `main/ui/ui_screen_settings_buddy.c` — Buddy 子设置页（物种选择）
- `main/ui/ui_screen_settings_buddy.h` — 头文件
- `main/ui/ui_screen_settings_time.c` — Time 子设置页（时区、NTP 服务器、NTP 间隔）
- `main/ui/ui_screen_settings_time.h` — 头文件
- `main/ui/ui_screen_settings_system.c` — System 子设置页（声音、编码器方向、语言）
- `main/ui/ui_screen_settings_system.h` — 头文件

### 修改文件
- `main/service/storage_service.h` — 新增键常量 + `STORAGE_NAMESPACE_BUDDY` + 迁移函数声明
- `main/service/storage_service.c` — 实现 NVS 键迁移逻辑
- `main/service/time_service.h` — 新增 NTP 服务器索引接口
- `main/service/time_service.c` — 实现 NTP 服务器索引持久化，使用预设列表
- `main/ui/ui_manager.h` — 枚举添加 3 个新屏幕 ID
- `main/ui/ui_manager.c` — 注册 3 个新屏幕
- `main/ui/ui_screen_settings.c` — 简化为 6 项纯导航
- `main/ui/ui_screen_settings.h` — 移除不再需要的公开 API
- `main/main.c` — 调用存储迁移函数
- `main/CMakeLists.txt` — 添加 3 个新源文件

---

### Task 1: storage_service — 新增键常量和命名空间

**Files:**
- Modify: `main/service/storage_service.h`
- Modify: `main/service/storage_service.c`

- [ ] **Step 1: 在 storage_service.h 添加键常量和迁移函数声明**

在 `STORAGE_NAMESPACE_WIFI` 之后添加 `STORAGE_NAMESPACE_BUDDY`，在文件末尾添加键常量和迁移函数：

```c
// 在已有命名空间定义之后添加
#define STORAGE_NAMESPACE_BUDDY   "buddy"

// NVS key constants for 'settings' namespace
#define KEY_TIMEZONE     "timezone"
#define KEY_NTP_SERVER   "ntp_server"
#define KEY_NTP_INTERVAL "ntp_interval"
#define KEY_TIME_HIGH    "time_high"
#define KEY_TIME_LOW     "time_low"
#define KEY_SOUND        "sound"
#define KEY_ENC_DIR      "enc_dir"
#define KEY_LANG         "lang"
#define KEY_LED_ON       "led_on"
#define KEY_LED_BRIGHT   "led_bright"
#define KEY_LED_SPEED    "led_speed"
#define KEY_LED_STYLE    "led_style"
#define KEY_LED_ANIM     "led_anim"

// NVS key migration (call once at startup)
void storage_migrate_settings_keys(void);
```

- [ ] **Step 2: 在 storage_service.c 实现迁移函数**

在文件末尾（`storage_migrate_wifi_config` 之后）添加：

```c
void storage_migrate_settings_keys(void)
{
    nvs_handle_t handle;
    if (nvs_open(STORAGE_NAMESPACE_SETTINGS, NVS_READWRITE, &handle) != ESP_OK) return;

    int32_t val;
    bool changed = false;

    // Migrate "sound_on" -> "sound"
    if (nvs_get_i32(handle, "sound_on", &val) == ESP_OK) {
        nvs_set_i32(handle, KEY_SOUND, val);
        nvs_erase_key(handle, "sound_on");
        changed = true;
        ESP_LOGI(TAG, "Migrated settings/sound_on -> sound = %ld", (long)val);
    }

    // Migrate "enc_rev" -> "enc_dir"
    if (nvs_get_i32(handle, "enc_rev", &val) == ESP_OK) {
        nvs_set_i32(handle, KEY_ENC_DIR, val);
        nvs_erase_key(handle, "enc_rev");
        changed = true;
        ESP_LOGI(TAG, "Migrated settings/enc_rev -> enc_dir = %ld", (long)val);
    }

    if (changed) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}
```

- [ ] **Step 3: 构建**

Run: `./build.sh`
Expected: 编译成功，无错误

- [ ] **Step 4: 提交**

```bash
git add main/service/storage_service.h main/service/storage_service.c
git commit -m "feat: add NVS key constants and migration for settings restructure"
```

---

### Task 2: time_service — NTP 服务器索引持久化

**Files:**
- Modify: `main/service/time_service.h`
- Modify: `main/service/time_service.c`

- [ ] **Step 1: 在 time_service.h 添加接口声明**

在文件末尾（`time_service_get_timezone_offset` 之后）添加：

```c
// NTP server index (preset list)
#define TIME_SERVICE_NTP_SERVER_COUNT 5

void time_service_set_ntp_server_index(int index);
int  time_service_get_ntp_server_index(void);
const char* time_service_get_ntp_server_name(int index);
const char* time_service_get_ntp_server_address(int index);
```

- [ ] **Step 2: 在 time_service.c 添加预设列表和索引管理**

替换现有的 `ntp_servers` 数组为完整的预设列表，并添加索引管理函数。

首先替换 `static const char *ntp_servers[]` 数组及其 `#define`：

```c
static const char *ntp_servers[] = {
    "pool.ntp.org",
    "cn.ntp.org.cn",
    "ntp.aliyun.com",
    "time.google.com",
    "time.windows.com",
};
#define NTP_SERVER_COUNT TIME_SERVICE_NTP_SERVER_COUNT

static const char *ntp_server_names[] = {
    "NTP Pool",
    "China",
    "Aliyun",
    "Google",
    "Windows",
};

static int ntp_server_index = 0;
```

然后在 `time_service_init()` 中，在 `esp_sntp_init()` 之前，加载 NTP 服务器索引：

```c
    // Load NTP server index
    int32_t stored_ntp_idx = 0;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_NTP_SERVER, &stored_ntp_idx);
    if (stored_ntp_idx >= 0 && stored_ntp_idx < NTP_SERVER_COUNT) {
        ntp_server_index = (int)stored_ntp_idx;
    }
```

注意：需要 `#include "service/storage_service.h"` 已存在，但需确保 `KEY_NTP_SERVER` 常量在 storage_service.h 中已定义（Task 1 已添加）。同时更新 `esp_sntp_setservername` 调用，确保初始化时只用选中服务器：

```c
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_servers[ntp_server_index]);
    esp_sntp_setservername(1, NULL);
    esp_sntp_setservername(2, NULL);
```

然后在文件末尾添加新函数：

```c
void time_service_set_ntp_server_index(int index)
{
    if (index < 0 || index >= NTP_SERVER_COUNT) return;
    ntp_server_index = index;
    strncpy(ntp_server, ntp_servers[index], sizeof(ntp_server) - 1);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_setservername(1, NULL);
    esp_sntp_setservername(2, NULL);
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_NTP_SERVER, (int32_t)index);
    ESP_LOGI(TAG, "NTP server set to %s (%s)", ntp_server_names[index], ntp_servers[index]);
}

int time_service_get_ntp_server_index(void)
{
    return ntp_server_index;
}

const char* time_service_get_ntp_server_name(int index)
{
    if (index < 0 || index >= NTP_SERVER_COUNT) return "Unknown";
    return ntp_server_names[index];
}

const char* time_service_get_ntp_server_address(int index)
{
    if (index < 0 || index >= NTP_SERVER_COUNT) return "";
    return ntp_servers[index];
}
```

- [ ] **Step 3: 构建**

Run: `./build.sh`
Expected: 编译成功

- [ ] **Step 4: 提交**

```bash
git add main/service/time_service.h main/service/time_service.c
git commit -m "feat: add NTP server index persistence with 5 presets"
```

---

### Task 3: ui_manager — 添加新屏幕枚举和注册

**Files:**
- Modify: `main/ui/ui_manager.h`
- Modify: `main/ui/ui_manager.c`

- [ ] **Step 1: 在 ui_manager.h 枚举中添加 3 个新屏幕 ID**

在 `UI_SCREEN_SETTINGS_LIGHT` 之后、`UI_SCREEN_COUNT` 之前添加：

```c
    UI_SCREEN_SETTINGS_LIGHT,
    UI_SCREEN_SETTINGS_BUDDY,
    UI_SCREEN_SETTINGS_TIME,
    UI_SCREEN_SETTINGS_SYSTEM,
    UI_SCREEN_COUNT
```

- [ ] **Step 2: 在 ui_manager.c 添加 include 和注册**

添加 include（在已有 `#include "ui_screen_settings_light.h"` 之后）：

```c
#include "ui_screen_settings_buddy.h"
#include "ui_screen_settings_time.h"
#include "ui_screen_settings_system.h"
```

在 `ui_init()` 中，`screens[UI_SCREEN_SETTINGS_LIGHT]` 之后添加：

```c
    screens[UI_SCREEN_SETTINGS_BUDDY] = ui_screen_settings_buddy_create();
    screens[UI_SCREEN_SETTINGS_TIME] = ui_screen_settings_time_create();
    screens[UI_SCREEN_SETTINGS_SYSTEM] = ui_screen_settings_system_create();
```

- [ ] **Step 3: 构建（会失败，因为头文件还不存在）**

Run: `./build.sh`
Expected: 编译失败，缺少头文件。这是预期的，后续 Task 会创建这些文件。

---

### Task 4: 新建 Buddy 子设置页

**Files:**
- Create: `main/ui/ui_screen_settings_buddy.h`
- Create: `main/ui/ui_screen_settings_buddy.c`

- [ ] **Step 1: 创建头文件 `main/ui/ui_screen_settings_buddy.h`**

```c
#pragma once

#include "lvgl.h"

lv_obj_t* ui_screen_settings_buddy_create(void);
void ui_screen_settings_buddy_refresh(void);
```

- [ ] **Step 2: 创建实现文件 `main/ui/ui_screen_settings_buddy.c`**

```c
#include "ui_screen_settings_buddy.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "buddy/buddy.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_BUDDY";

typedef enum {
    BUDDY_MODE_NAV = 0,
    BUDDY_MODE_ADJUST,
} buddy_edit_mode_t;

#define BUDDY_ITEM_COUNT 1

static buddy_edit_mode_t buddy_mode = BUDDY_MODE_NAV;
static int buddy_selected_item = 0;
static int buddy_species_index = 0;

static lv_obj_t *screen = NULL;
static lv_obj_t *buddy_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[BUDDY_ITEM_COUNT][20];
static char item_values[BUDDY_ITEM_COUNT][16];
static ui_list_item_t items[BUDDY_ITEM_COUNT];

static void update_display(void)
{
    snprintf(item_keys[0], sizeof(item_keys[0]), "Species");
    snprintf(item_values[0], sizeof(item_values[0]), "%s",
             buddy_get_species_name(buddy_species_index));

    for (int i = 0; i < BUDDY_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (buddy_list) {
        lv_color_t color;
        if (buddy_mode == BUDDY_MODE_ADJUST) {
            color = lv_color_hex(0xFFFF00);
        } else {
            color = lv_color_hex(0x00FF00);
        }
        ui_list_set_selected_color(buddy_list, color);
        ui_list_set_items(buddy_list, items, BUDDY_ITEM_COUNT);
        ui_list_set_selected(buddy_list, buddy_selected_item);
    }

    if (hint_label) {
        if (buddy_mode == BUDDY_MODE_ADJUST) {
            lv_label_set_text(hint_label, "SET:save|Press:cancel");
        } else {
            lv_label_set_text(hint_label, "SET:edit|Press:back");
        }
    }
}

static void buddy_on_encoder_cw(void)
{
    if (buddy_mode == BUDDY_MODE_NAV) {
        buddy_selected_item = (buddy_selected_item + 1) % BUDDY_ITEM_COUNT;
        update_display();
    } else {
        int count = buddy_get_species_count();
        buddy_species_index = (buddy_species_index + 1) % count;
        update_display();
    }
}

static void buddy_on_encoder_ccw(void)
{
    if (buddy_mode == BUDDY_MODE_NAV) {
        buddy_selected_item = (buddy_selected_item - 1 + BUDDY_ITEM_COUNT) % BUDDY_ITEM_COUNT;
        update_display();
    } else {
        int count = buddy_get_species_count();
        buddy_species_index = (buddy_species_index - 1 + count) % count;
        update_display();
    }
}

static void buddy_on_encoder_press(void)
{
    if (buddy_mode == BUDDY_MODE_ADJUST) {
        buddy_mode = BUDDY_MODE_NAV;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

static void buddy_on_settings_press(void)
{
    if (buddy_mode == BUDDY_MODE_NAV) {
        buddy_mode = BUDDY_MODE_ADJUST;
        update_display();
    } else {
        buddy_set_species(buddy_species_index);
        buddy_mode = BUDDY_MODE_NAV;
        update_display();
    }
}

static void buddy_on_encoder_long_press(void)
{
    if (buddy_mode == BUDDY_MODE_ADJUST) {
        buddy_mode = BUDDY_MODE_NAV;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

lv_obj_t* ui_screen_settings_buddy_create(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Buddy");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    buddy_list = ui_list_create(screen, 220, 180, 10, 30);

    buddy_info_t info = buddy_get_info();
    buddy_species_index = info.species_index;
    buddy_mode = BUDDY_MODE_NAV;
    buddy_selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, "SET:edit|Press:back");
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = buddy_on_encoder_cw,
        .on_encoder_ccw = buddy_on_encoder_ccw,
        .on_encoder_press = buddy_on_encoder_press,
        .on_encoder_long_press = buddy_on_encoder_long_press,
        .on_settings_press = buddy_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_BUDDY, &cbs);

    ESP_LOGI(TAG, "Settings Buddy screen created");
    return screen;
}

void ui_screen_settings_buddy_refresh(void)
{
    buddy_info_t info = buddy_get_info();
    buddy_species_index = info.species_index;
    update_display();
}
```

- [ ] **Step 3: 构建**

Run: `./build.sh`
Expected: 此文件编译成功，但整体可能因 Task 3 的其他缺失文件而失败（如果 Task 5/6 还没做的话）

- [ ] **Step 4: 提交**

```bash
git add main/ui/ui_screen_settings_buddy.h main/ui/ui_screen_settings_buddy.c
git commit -m "feat: add Buddy settings sub-page with species selection"
```

---

### Task 5: 新建 Time 子设置页

**Files:**
- Create: `main/ui/ui_screen_settings_time.h`
- Create: `main/ui/ui_screen_settings_time.c`

- [ ] **Step 1: 创建头文件 `main/ui/ui_screen_settings_time.h`**

```c
#pragma once

#include "lvgl.h"

lv_obj_t* ui_screen_settings_time_create(void);
void ui_screen_settings_time_refresh(void);
```

- [ ] **Step 2: 创建实现文件 `main/ui/ui_screen_settings_time.c`**

```c
#include "ui_screen_settings_time.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/time_service.h"
#include "service/storage_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_TIME";

typedef enum {
    TIME_MODE_NAV = 0,
    TIME_MODE_ADJUST,
} time_edit_mode_t;

#define TIME_ITEM_COUNT 3
// 0: Timezone, 1: NTP Server, 2: NTP Interval

static time_edit_mode_t time_mode = TIME_MODE_NAV;
static int time_selected_item = 0;
static int time_values[TIME_ITEM_COUNT] = {8, 0, 10};
// Timezone=UTC+8, NTP Server index=0, NTP Interval=10min

static lv_obj_t *screen = NULL;
static lv_obj_t *time_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[TIME_ITEM_COUNT][20];
static char item_values[TIME_ITEM_COUNT][20];
static ui_list_item_t items[TIME_ITEM_COUNT];

static void update_display(void)
{
    snprintf(item_keys[0], sizeof(item_keys[0]), "Timezone");
    snprintf(item_values[0], sizeof(item_values[0]), "UTC%+d", time_values[0]);

    snprintf(item_keys[1], sizeof(item_keys[1]), "NTP Server");
    snprintf(item_values[1], sizeof(item_values[1]), "%s",
             time_service_get_ntp_server_name(time_values[1]));

    snprintf(item_keys[2], sizeof(item_keys[2]), "NTP Interval");
    if (time_values[2] == 0) {
        snprintf(item_values[2], sizeof(item_values[2]), "Off");
    } else {
        snprintf(item_values[2], sizeof(item_values[2]), "%d min", time_values[2]);
    }

    for (int i = 0; i < TIME_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (time_list) {
        lv_color_t color;
        if (time_mode == TIME_MODE_ADJUST) {
            color = lv_color_hex(0xFFFF00);
        } else {
            color = lv_color_hex(0x00FF00);
        }
        ui_list_set_selected_color(time_list, color);
        ui_list_set_items(time_list, items, TIME_ITEM_COUNT);
        ui_list_set_selected(time_list, time_selected_item);
    }

    if (hint_label) {
        if (time_mode == TIME_MODE_ADJUST) {
            lv_label_set_text(hint_label, "SET:save|Press:cancel");
        } else {
            lv_label_set_text(hint_label, "SET:edit|Press:back");
        }
    }
}

static void time_on_encoder_cw(void)
{
    if (time_mode == TIME_MODE_NAV) {
        time_selected_item = (time_selected_item + 1) % TIME_ITEM_COUNT;
        update_display();
    } else {
        switch (time_selected_item) {
            case 0: // Timezone
                if (time_values[0] < 14) time_values[0]++;
                break;
            case 1: // NTP Server
                time_values[1] = (time_values[1] + 1) % TIME_SERVICE_NTP_SERVER_COUNT;
                break;
            case 2: // NTP Interval
                if (time_values[2] < 120) time_values[2]++;
                break;
        }
        update_display();
    }
}

static void time_on_encoder_ccw(void)
{
    if (time_mode == TIME_MODE_NAV) {
        time_selected_item = (time_selected_item - 1 + TIME_ITEM_COUNT) % TIME_ITEM_COUNT;
        update_display();
    } else {
        switch (time_selected_item) {
            case 0: // Timezone
                if (time_values[0] > -12) time_values[0]--;
                break;
            case 1: // NTP Server
                time_values[1] = (time_values[1] - 1 + TIME_SERVICE_NTP_SERVER_COUNT) % TIME_SERVICE_NTP_SERVER_COUNT;
                break;
            case 2: // NTP Interval
                if (time_values[2] > 0) time_values[2]--;
                break;
        }
        update_display();
    }
}

static void time_on_encoder_press(void)
{
    if (time_mode == TIME_MODE_ADJUST) {
        // Cancel: reload current values from service
        time_values[0] = time_service_get_timezone_offset();
        time_values[1] = time_service_get_ntp_server_index();
        time_values[2] = (int)time_service_get_sync_interval();
        time_mode = TIME_MODE_NAV;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

static void time_on_settings_press(void)
{
    if (time_mode == TIME_MODE_NAV) {
        time_mode = TIME_MODE_ADJUST;
        update_display();
    } else {
        // Save: persist values
        time_service_set_timezone_offset(time_values[0]);
        time_service_set_ntp_server_index(time_values[1]);
        time_service_set_sync_interval((uint16_t)time_values[2]);
        time_mode = TIME_MODE_NAV;
        update_display();
    }
}

static void time_on_encoder_long_press(void)
{
    if (time_mode == TIME_MODE_ADJUST) {
        time_values[0] = time_service_get_timezone_offset();
        time_values[1] = time_service_get_ntp_server_index();
        time_values[2] = (int)time_service_get_sync_interval();
        time_mode = TIME_MODE_NAV;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

lv_obj_t* ui_screen_settings_time_create(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Time");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    time_list = ui_list_create(screen, 220, 180, 10, 30);

    // Load current values from time_service
    time_values[0] = time_service_get_timezone_offset();
    time_values[1] = time_service_get_ntp_server_index();
    time_values[2] = (int)time_service_get_sync_interval();

    time_mode = TIME_MODE_NAV;
    time_selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, "SET:edit|Press:back");
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = time_on_encoder_cw,
        .on_encoder_ccw = time_on_encoder_ccw,
        .on_encoder_press = time_on_encoder_press,
        .on_encoder_long_press = time_on_encoder_long_press,
        .on_settings_press = time_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_TIME, &cbs);

    ESP_LOGI(TAG, "Settings Time screen created");
    return screen;
}

void ui_screen_settings_time_refresh(void)
{
    time_values[0] = time_service_get_timezone_offset();
    time_values[1] = time_service_get_ntp_server_index();
    time_values[2] = (int)time_service_get_sync_interval();
    update_display();
}
```

- [ ] **Step 3: 构建**

Run: `./build.sh`

- [ ] **Step 4: 提交**

```bash
git add main/ui/ui_screen_settings_time.h main/ui/ui_screen_settings_time.c
git commit -m "feat: add Time settings sub-page with timezone, NTP server, NTP interval"
```

---

### Task 6: 新建 System 子设置页

**Files:**
- Create: `main/ui/ui_screen_settings_system.h`
- Create: `main/ui/ui_screen_settings_system.c`

- [ ] **Step 1: 创建头文件 `main/ui/ui_screen_settings_system.h`**

```c
#pragma once

#include "lvgl.h"

lv_obj_t* ui_screen_settings_system_create(void);
void ui_screen_settings_system_refresh(void);
```

- [ ] **Step 2: 创建实现文件 `main/ui/ui_screen_settings_system.c`**

System 子页面有 3 个二值项：Sound、Direction、Language。按规格，二值项在 NAV 模式下 SET 直接切换，不进入 ADJUST 模式。

```c
#include "ui_screen_settings_system.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/storage_service.h"
#include "service/sound_service.h"
#include "input/input_handler.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_SYSTEM";

#define SYSTEM_ITEM_COUNT 3
// 0: Sound, 1: Direction, 2: Language

static int system_selected_item = 0;
static int system_values[SYSTEM_ITEM_COUNT] = {1, 0, 0};
// Sound=On, Direction=Normal, Language=English

static lv_obj_t *screen = NULL;
static lv_obj_t *system_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[SYSTEM_ITEM_COUNT][20];
static char item_values[SYSTEM_ITEM_COUNT][12];
static ui_list_item_t items[SYSTEM_ITEM_COUNT];

static void update_display(void)
{
    static const char *on_off[] = {"Off", "On"};
    static const char *dir_opts[] = {"Normal", "Rev"};
    static const char *lang_opts[] = {"English", "Chinese"};

    snprintf(item_keys[0], sizeof(item_keys[0]), "Sound");
    snprintf(item_values[0], sizeof(item_values[0]), "%s", on_off[system_values[0] % 2]);

    snprintf(item_keys[1], sizeof(item_keys[1]), "Direction");
    snprintf(item_values[1], sizeof(item_values[1]), "%s", dir_opts[system_values[1] % 2]);

    snprintf(item_keys[2], sizeof(item_keys[2]), "Language");
    snprintf(item_values[2], sizeof(item_values[2]), "%s", lang_opts[system_values[2] % 2]);

    for (int i = 0; i < SYSTEM_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (system_list) {
        ui_list_set_selected_color(system_list, lv_color_hex(0x00FF00));
        ui_list_set_items(system_list, items, SYSTEM_ITEM_COUNT);
        ui_list_set_selected(system_list, system_selected_item);
    }

    if (hint_label) {
        lv_label_set_text(hint_label, "SET:toggle|Press:back");
    }
}

static void system_on_encoder_cw(void)
{
    system_selected_item = (system_selected_item + 1) % SYSTEM_ITEM_COUNT;
    update_display();
}

static void system_on_encoder_ccw(void)
{
    system_selected_item = (system_selected_item - 1 + SYSTEM_ITEM_COUNT) % SYSTEM_ITEM_COUNT;
    update_display();
}

static void system_on_encoder_press(void)
{
    ui_switch_screen(UI_SCREEN_SETTINGS);
}

static void system_on_settings_press(void)
{
    // All items are binary toggle — SET directly switches value
    switch (system_selected_item) {
        case 0: // Sound
            system_values[0] = !system_values[0];
            sound_service_set_enabled(system_values[0]);
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_SOUND, system_values[0]);
            break;
        case 1: // Direction
            system_values[1] = !system_values[1];
            input_handler_set_reverse(system_values[1]);
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, system_values[1]);
            break;
        case 2: // Language
            system_values[2] = !system_values[2];
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, system_values[2]);
            break;
    }
    update_display();
}

static void system_on_encoder_long_press(void)
{
    ui_switch_screen(UI_SCREEN_SETTINGS);
}

lv_obj_t* ui_screen_settings_system_create(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "System");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    system_list = ui_list_create(screen, 220, 180, 10, 30);

    // Load stored values
    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SOUND, &val)) {
        system_values[0] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, &val)) {
        system_values[1] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, &val)) {
        system_values[2] = (int)val;
    }

    system_selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, "SET:toggle|Press:back");
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = system_on_encoder_cw,
        .on_encoder_ccw = system_on_encoder_ccw,
        .on_encoder_press = system_on_encoder_press,
        .on_encoder_long_press = system_on_encoder_long_press,
        .on_settings_press = system_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_SYSTEM, &cbs);

    ESP_LOGI(TAG, "Settings System screen created");
    return screen;
}

void ui_screen_settings_system_refresh(void)
{
    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SOUND, &val)) {
        system_values[0] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, &val)) {
        system_values[1] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, &val)) {
        system_values[2] = (int)val;
    }
    update_display();
}
```

- [ ] **Step 3: 构建**

Run: `./build.sh`

- [ ] **Step 4: 提交**

```bash
git add main/ui/ui_screen_settings_system.h main/ui/ui_screen_settings_system.c
git commit -m "feat: add System settings sub-page with sound, direction, language"
```

---

### Task 7: 简化主设置页面为纯导航

**Files:**
- Modify: `main/ui/ui_screen_settings.c`
- Modify: `main/ui/ui_screen_settings.h`

- [ ] **Step 1: 重写 `ui_screen_settings.h`，简化为纯导航接口**

```c
#pragma once

#include "lvgl.h"

typedef enum {
    SETTINGS_MODE_IDLE = 0,
    SETTINGS_MODE_SELECT,
} settings_mode_t;

lv_obj_t* ui_screen_settings_create(void);
settings_mode_t ui_screen_settings_get_mode(void);
void ui_screen_settings_set_mode(settings_mode_t mode);
int ui_screen_settings_get_current_item(void);
```

移除以下不再需要的声明：`ui_screen_settings_update`, `ui_screen_settings_set_hint`, `ui_screen_settings_enter`, `ui_screen_settings_exit`, `ui_screen_settings_select_next`, `ui_screen_settings_select_prev`, `ui_screen_settings_enter_adjust`, `ui_screen_settings_adjust_up`, `ui_screen_settings_adjust_down`, `ui_screen_settings_get_values`，以及 `SETTINGS_MODE_ADJUST`。

- [ ] **Step 2: 重写 `ui_screen_settings.c` 为 6 项纯导航**

完整替换文件内容：

```c
#include "ui_screen_settings.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/sound_service.h"
#include "service/storage_service.h"
#include "input/input_handler.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS";

#define SETTINGS_ITEM_COUNT 6
// 0: Pomodoro, 1: Buddy, 2: Light, 3: WiFi, 4: Time, 5: System

static lv_obj_t *settings_title = NULL;
static lv_obj_t *settings_list = NULL;
static lv_obj_t *settings_hint = NULL;

static settings_mode_t settings_mode = SETTINGS_MODE_IDLE;
static int current_settings_item = 0;

static char item_keys[SETTINGS_ITEM_COUNT][20];
static char item_values[SETTINGS_ITEM_COUNT][4];
static ui_list_item_t items[SETTINGS_ITEM_COUNT];

static const char *settings_names[SETTINGS_ITEM_COUNT] = {
    "Pomodoro", "Buddy", "Light", "WiFi", "Time", "System"
};

static void update_display(void)
{
    for (int i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        strncpy(item_keys[i], settings_names[i], sizeof(item_keys[i]) - 1);
        snprintf(item_values[i], sizeof(item_values[i]), ">");
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (settings_list) {
        lv_color_t color;
        if (settings_mode == SETTINGS_MODE_SELECT) {
            color = lv_color_hex(0x00FF00);
        } else {
            color = lv_color_hex(0xFFFFFF);
        }
        ui_list_set_selected_color(settings_list, color);
        ui_list_set_items(settings_list, items, SETTINGS_ITEM_COUNT);
        ui_list_set_selected(settings_list, current_settings_item);
    }

    if (settings_hint) {
        if (settings_mode == SETTINGS_MODE_SELECT) {
            lv_label_set_text(settings_hint, "SET:enter|Press:back");
        } else {
            lv_label_set_text(settings_hint, "SET:enter");
        }
    }
}

static void settings_on_encoder_cw(void)
{
    if (settings_mode == SETTINGS_MODE_IDLE) {
        ui_switch_screen(UI_SCREEN_MAIN);
    } else if (settings_mode == SETTINGS_MODE_SELECT) {
        current_settings_item = (current_settings_item + 1) % SETTINGS_ITEM_COUNT;
        update_display();
    }
}

static void settings_on_encoder_ccw(void)
{
    if (settings_mode == SETTINGS_MODE_IDLE) {
        ui_switch_screen(UI_SCREEN_BUDDY);
    } else if (settings_mode == SETTINGS_MODE_SELECT) {
        current_settings_item = (current_settings_item - 1 + SETTINGS_ITEM_COUNT) % SETTINGS_ITEM_COUNT;
        update_display();
    }
}

static void settings_on_encoder_press(void)
{
    if (settings_mode == SETTINGS_MODE_SELECT) {
        settings_mode = SETTINGS_MODE_IDLE;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_MAIN);
    }
}

static void navigate_to_subpage(void)
{
    settings_mode = SETTINGS_MODE_IDLE;
    update_display();

    switch (current_settings_item) {
        case 0: ui_switch_screen(UI_SCREEN_SETTINGS_POMODORO); break;
        case 1: ui_switch_screen(UI_SCREEN_SETTINGS_BUDDY);    break;
        case 2: ui_switch_screen(UI_SCREEN_SETTINGS_LIGHT);    break;
        case 3: ui_switch_screen(UI_SCREEN_WIFI_SAVED);        break;
        case 4: ui_switch_screen(UI_SCREEN_SETTINGS_TIME);     break;
        case 5: ui_switch_screen(UI_SCREEN_SETTINGS_SYSTEM);   break;
    }
}

static void settings_on_settings_press(void)
{
    if (settings_mode == SETTINGS_MODE_IDLE) {
        settings_mode = SETTINGS_MODE_SELECT;
        current_settings_item = 0;
        update_display();
    } else if (settings_mode == SETTINGS_MODE_SELECT) {
        navigate_to_subpage();
    }
}

static void settings_on_encoder_long_press(void)
{
    if (settings_mode == SETTINGS_MODE_SELECT) {
        settings_mode = SETTINGS_MODE_IDLE;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_MAIN);
    }
}

lv_obj_t* ui_screen_settings_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    settings_title = lv_label_create(screen);
    lv_obj_set_style_text_color(settings_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(settings_title, "SETTINGS");
    lv_obj_set_style_text_font(settings_title, &lv_font_montserrat_16, 0);
    lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 6);

    settings_list = ui_list_create(screen, 220, 180, 10, 28);

    settings_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(settings_hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(settings_hint, "SET:enter");
    lv_obj_set_style_text_font(settings_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(settings_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = settings_on_encoder_cw,
        .on_encoder_ccw = settings_on_encoder_ccw,
        .on_encoder_press = settings_on_encoder_press,
        .on_encoder_long_press = settings_on_encoder_long_press,
        .on_settings_press = settings_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS, &cbs);

    update_display();

    ESP_LOGI(TAG, "Settings screen created (6 categories)");
    return screen;
}

settings_mode_t ui_screen_settings_get_mode(void)
{
    return settings_mode;
}

void ui_screen_settings_set_mode(settings_mode_t mode)
{
    settings_mode = mode;
}

int ui_screen_settings_get_current_item(void)
{
    return current_settings_item;
}
```

- [ ] **Step 3: 检查旧 API 的所有调用者**

旧函数 `ui_screen_settings_enter`, `ui_screen_settings_exit`, `ui_screen_settings_enter_adjust`, `ui_screen_settings_adjust_up`, `ui_screen_settings_adjust_down`, `ui_screen_settings_select_next`, `ui_screen_settings_select_prev` 只在 `ui_screen_settings.c` 内部被调用（通过回调），没有外部调用者，所以移除是安全的。

- [ ] **Step 4: 构建**

Run: `./build.sh`
Expected: 编译成功

- [ ] **Step 5: 提交**

```bash
git add main/ui/ui_screen_settings.h main/ui/ui_screen_settings.c
git commit -m "refactor: simplify main settings to 6-category navigation"
```

---

### Task 8: 更新 CMakeLists.txt 和 main.c

**Files:**
- Modify: `main/CMakeLists.txt`
- Modify: `main/main.c`

- [ ] **Step 1: 在 CMakeLists.txt 添加 3 个新源文件**

在 `ui/ui_screen_settings_light.c` 之后添加：

```
                          ui/ui_screen_settings_buddy.c
                          ui/ui_screen_settings_time.c
                          ui/ui_screen_settings_system.c
```

- [ ] **Step 2: 在 main.c 中调用存储迁移**

在 `app_main()` 中，NVS 初始化（步骤 1）之后、驱动初始化（步骤 2）之前添加迁移调用：

```c
    // 1.5. Migrate old NVS keys
    storage_migrate_settings_keys();
```

需要在 main.c 顶部确认已有 `#include "service/storage_service.h"`（已存在）。

- [ ] **Step 3: 构建**

Run: `./build.sh`
Expected: 编译成功

- [ ] **Step 4: 提交**

```bash
git add main/CMakeLists.txt main/main.c
git commit -m "feat: register new settings sub-pages and add NVS key migration"
```

---

### Task 9: 集成构建验证和最终提交

**Files:** 无新增

- [ ] **Step 1: 完整构建**

Run: `./build.sh`
Expected: 编译成功，0 错误 0 警告

- [ ] **Step 2: 检查所有规格覆盖**

对照设计规格验证：

| 规格要求 | 对应 Task |
|---------|-----------|
| 6 分类主设置导航 | Task 7 |
| Buddy 子页（物种） | Task 4 |
| Time 子页（时区 + NTP 服务器 + NTP 间隔） | Task 5 |
| System 子页（声音 + 方向 + 语言） | Task 6 |
| NTP 服务器预设选择 | Task 2 |
| NVS 键常量统一 | Task 1 |
| 旧键名迁移 | Task 1, Task 8 |
| 语言持久化 | Task 6 |
| NTP 间隔持久化 | Task 5 |
| 多值项 ADJUST 模式 | Task 4, 5 |
| 二值项 SET 直接切换 | Task 6 |
| 3 个新枚举值 | Task 3 |
| 3 个新屏幕注册 | Task 3 |
| CMakeLists.txt | Task 8 |

- [ ] **Step 3: 如果有编译问题，修复并 amend 最后一个 commit**

```bash
./build.sh
# 如果有错误，修复后：
git add -A
git commit -m "fix: resolve integration build issues"
```
