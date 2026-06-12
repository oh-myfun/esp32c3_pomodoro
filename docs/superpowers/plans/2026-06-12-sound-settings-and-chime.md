# 声音设置页面与整点报时 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把单一全局声音开关拆成 7 个分类开关（含整点/半点报时），新增独立声音设置页，实现基于系统时钟的整点/半点报时与静默时段过滤。

**Architecture:** `sound_service` 内部维护分类映射表与静默时段状态。所有音效（包括报时）走同一套 总开关→分类开关 闸门。`chime_service` 每秒由 UIUpdate 任务调用，检测整点/半点边界，按需触发报时；`ui_screen_settings_sound` 提供 10 项配置 UI（8 个开关 + 2 个静默时段调整）。

**Tech Stack:** ESP-IDF v5.5.4 + LVGL v9.5 + FreeRTOS + C99。硬件测试驱动验证（嵌入式项目无传统单元测试框架）。

---

## 文件结构总览

| 文件 | 操作 | 责任 |
|---|---|---|
| `main/service/storage_service.h` | 修改 | 添加 7 个分类键 + 2 个静默时段键 |
| `main/service/sound_service.h` | 修改 | 暴露分类枚举、分类 API、报时 API、静默 API |
| `main/service/sound_service.c` | 修改 | cat_map 表、cat_enabled 数组、play_hour/half_chime、quiet range |
| `main/service/chime_service.h` | 新建 | 报时检测服务接口 |
| `main/service/chime_service.c` | 新建 | 每秒 tick，整点/半点触发，去重 |
| `main/ui/ui_screen_settings_sound.h` | 新建 | 声音设置页接口 |
| `main/ui/ui_screen_settings_sound.c` | 新建 | 10 项配置页（8 开关 + 2 调整） |
| `main/ui/ui_manager.h` | 修改 | 加 `UI_SCREEN_SETTINGS_SOUND` |
| `main/ui/ui_manager.c` | 修改 | 注册 lazy creator + disposable |
| `main/ui/ui_screen_settings.c` | 修改 | 4→5 项菜单，加 Sound 入口 |
| `main/ui/ui_screen_settings_system.c` | 修改 | 4→3 项，移除 Sound |
| `main/ui/i18n.h` | 修改 | 加 9 个新字符串 ID |
| `main/ui/i18n.c` | 修改 | 加 9 个新字符串双语定义 |
| `main/main.c` | 修改 | 初始化 chime_service，UIUpdate 集成 |
| `main/CMakeLists.txt` | 修改 | 加 chime_service.c、ui_screen_settings_sound.c |

---

## Task 1: 扩展 NVS 键定义

**Files:**
- Modify: `main/service/storage_service.h:19-28`（在 `KEY_SOUND` 之后追加）

- [ ] **Step 1: 在 storage_service.h 中追加 9 个键定义**

打开 `main/service/storage_service.h`，找到 `KEY_SOUND "sound"` 这一行（约第 19 行），在它后面插入 9 个新键：

```c
#define KEY_SOUND        "sound"
#define KEY_SND_CAT_KEY  "snd_key"
#define KEY_SND_CAT_UI   "snd_ui"
#define KEY_SND_CAT_NET  "snd_net"
#define KEY_SND_CAT_POMO "snd_pomo"
#define KEY_SND_CAT_BUDDY "snd_buddy"
#define KEY_SND_CAT_HOUR "snd_hour"
#define KEY_SND_CAT_HALF "snd_half"
#define KEY_QUIET_START  "quiet_st"
#define KEY_QUIET_END    "quiet_ed"
```

注意：NVS key 最长 15 字符，全部在限制内。

- [ ] **Step 2: 编译验证头文件无语法错误**

Run: `./build.sh`
Expected: 编译通过（只是声明，不引用，没有副作用）

- [ ] **Step 3: 提交**

```bash
git add main/service/storage_service.h
git commit -m "feat: 添加声音分类与静默时段的 NVS 键定义"
```

---

## Task 2: 添加 i18n 字符串

**Files:**
- Modify: `main/ui/i18n.h`（在 `STR_COUNT` 之前追加）
- Modify: `main/ui/i18n.c`（在数组末尾追加）

- [ ] **Step 1: 在 i18n.h 中追加 9 个枚举值**

打开 `main/ui/i18n.h`，找到 `STR_ACT_RESET` 行（约第 231 行），在它之后、`STR_COUNT` 之前追加：

```c
    STR_ACT_RESET,      /* "Screen Reset" / "屏幕重置" */

    /* Sound settings */
    STR_T_SOUND,            /* "🔊Sound" / "🔊声音" */
    STR_M_SOUND,            /* "🔊Sound" / "🔊声音" */
    STR_SND_KEY,            /* "🔑Key" / "🔑按键" */
    STR_SND_UI,             /* "✅UI" / "✅操作" */
    STR_SND_NET,            /* "📶Net" / "📶网络" */
    STR_SND_POMO,           /* "🍅Pomo" / "🍅番茄" */
    STR_SND_BUDDY,          /* "🐱Buddy" / "🐱宠物" */
    STR_SND_HOUR,           /* "🔔Hour" / "🔔整点" */
    STR_SND_HALF,           /* "🔎Half" / "🔎半点" */
    STR_QUIET_START,        /* "🌙Q.Start" / "🌙静默起" */
    STR_QUIET_END,          /* "🌙Q.End" / "🌙静默止" */

    STR_COUNT
```

- [ ] **Step 2: 在 i18n.c 数组末尾追加对应字符串**

打开 `main/ui/i18n.c`，找到 `STR_ACT_RESET` 那一行（约末尾），在它之后、数组结束 `};` 之前追加：

```c
    [STR_ACT_RESET]   = {"Screen Reset", "屏幕重置"},

    /* Sound settings */
    [STR_T_SOUND]      = {"🔊Sound",      "🔊声音"},
    [STR_M_SOUND]      = {"🔊Sound",      "🔊声音"},
    [STR_SND_KEY]      = {"🔑Key",        "🔑按键"},
    [STR_SND_UI]       = {"✅UI",         "✅操作"},
    [STR_SND_NET]      = {"📶Net",        "📶网络"},
    [STR_SND_POMO]     = {"🍅Pomo",       "🍅番茄"},
    [STR_SND_BUDDY]    = {"🐱Buddy",      "🐱宠物"},
    [STR_SND_HOUR]     = {"🔔Hour",       "🔔整点"},
    [STR_SND_HALF]     = {"🔎Half",       "🔎半点"},
    [STR_QUIET_START]  = {"🌙Q.Start",    "🌙静默起"},
    [STR_QUIET_END]    = {"🌙Q.End",      "🌙静默止"},
```

注意：复用现有 `STR_FMT_HOUR`（"%dh"/"%d小时"）作为静默时段小时显示，无需新增格式串。

- [ ] **Step 3: 编译验证**

Run: `./build.sh`
Expected: 编译通过，无未定义符号。

- [ ] **Step 4: 提交**

```bash
git add main/ui/i18n.h main/ui/i18n.c
git commit -m "feat: 添加声音设置相关国际化字符串"
```

---

## Task 3: 扩展 sound_service.h 接口

**Files:**
- Modify: `main/service/sound_service.h`（整体重写）

- [ ] **Step 1: 用以下内容完整替换 sound_service.h**

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SOUND_KEY_CLICK,
    SOUND_CONFIRM,
    SOUND_CANCEL,
    SOUND_SUCCESS,
    SOUND_FAIL,
    SOUND_WIFI_CONNECT,
    SOUND_WIFI_CONNECTED,
    SOUND_WIFI_FAILED,
    SOUND_SYNC_START,
    SOUND_SYNC_DONE,
    SOUND_POMO_START,
    SOUND_POMO_WORK_START,
    SOUND_POMO_BREAK_START,
    SOUND_POMO_WORK_DONE,
    SOUND_POMO_BREAK_DONE,
    SOUND_POMO_LONG_BREAK,
    SOUND_BUDDY_ATTENTION,
    SOUND_BUDDY_HAPPY,
    SOUND_BUDDY_SAD,
    SOUND_COUNT
} sound_id_t;

typedef enum {
    SND_CAT_KEY = 0,
    SND_CAT_UI,
    SND_CAT_NET,
    SND_CAT_POMO,
    SND_CAT_BUDDY,
    SND_CAT_HOUR_CHIME,
    SND_CAT_HALF_CHIME,
    SND_CAT_COUNT
} sound_category_t;

void sound_service_init(void);
void sound_service_play(sound_id_t id);

bool sound_service_is_enabled(void);
void sound_service_set_enabled(bool enabled);

/* 分类开关 */
bool sound_service_is_category_enabled(sound_category_t cat);
void sound_service_set_category_enabled(sound_category_t cat, bool on);

/* 报时（参数化触发，内部走总开关 + 分类开关） */
void sound_service_play_hour_chime(int hour12);   /* hour12: 1..12 */
void sound_service_play_half_chime(void);

/* 静默时段（start/end 取值 0..23；start==end 表示无静默） */
bool sound_service_is_quiet_hour(int hour);
void sound_service_set_quiet_range(int start, int end);
void sound_service_get_quiet_range(int *start, int *end);
```

- [ ] **Step 2: 不编译（实现还没改，下一步会做）**

直接进入 Task 4。

---

## Task 4: 实现 sound_service.c 扩展

**Files:**
- Modify: `main/service/sound_service.c`（整体重写）

- [ ] **Step 1: 用以下内容完整替换 sound_service.c**

```c
#include "sound_service.h"
#include "driver/buzzer.h"
#include "service/storage_service.h"
#include "esp_log.h"

static const char *TAG = "SOUND";

/* ---- Note frequencies (Hz) ---- */
#define NOTE_C4   262
#define NOTE_EB4  311
#define NOTE_G4   392
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_E5   659
#define NOTE_G5   784
#define NOTE_A5   880
#define NOTE_B5   988
#define NOTE_C6   1047
#define NOTE_D6   1175
#define NOTE_E6   1319
#define NOTE_G6   1568
#define NOTE_A6   1760
#define NOTE_C7   2093
#define REST      0

/* ---- Existing melodies (unchanged) ---- */
static const buzzer_note_t mel_key_click[] = {
    {NOTE_C6, 30},
};
static const buzzer_note_t mel_confirm[] = {
    {NOTE_E5, 60}, {NOTE_G5, 60},
};
static const buzzer_note_t mel_cancel[] = {
    {NOTE_G5, 60}, {NOTE_E5, 60},
};
static const buzzer_note_t mel_success[] = {
    {NOTE_C5, 80}, {NOTE_E5, 80}, {NOTE_G5, 80},
};
static const buzzer_note_t mel_fail[] = {
    {NOTE_G4, 80}, {NOTE_EB4, 120},
};
static const buzzer_note_t mel_wifi_connect[] = {
    {NOTE_A5, 50},
};
static const buzzer_note_t mel_wifi_connected[] = {
    {NOTE_E5, 60}, {NOTE_B5, 80},
};
static const buzzer_note_t mel_wifi_failed[] = {
    {NOTE_B4, 100}, {REST, 50}, {NOTE_B4, 100},
};
static const buzzer_note_t mel_sync_start[] = {
    {NOTE_D6, 30},
};
static const buzzer_note_t mel_sync_done[] = {
    {NOTE_D6, 30}, {NOTE_A6, 50},
};
static const buzzer_note_t mel_pomo_start[] = {
    {NOTE_C5, 100}, {NOTE_E5, 100}, {NOTE_G5, 100}, {NOTE_C6, 150},
};
static const buzzer_note_t mel_pomo_work_start[] = {
    {NOTE_C5, 80}, {NOTE_E5, 80}, {NOTE_G5, 120},
};
static const buzzer_note_t mel_pomo_break_start[] = {
    {NOTE_G5, 100}, {NOTE_E5, 120},
};
static const buzzer_note_t mel_pomo_work_done[] = {
    {NOTE_G5, 150}, {NOTE_E5, 150}, {NOTE_C5, 200},
};
static const buzzer_note_t mel_pomo_break_done[] = {
    {NOTE_C5, 80}, {REST, 60}, {NOTE_C5, 80}, {REST, 60}, {NOTE_C5, 150},
};
static const buzzer_note_t mel_pomo_long_break[] = {
    {NOTE_G4, 200}, {NOTE_C5, 200}, {NOTE_E5, 200}, {NOTE_G5, 300},
};
static const buzzer_note_t mel_buddy_attention[] = {
    {NOTE_A5, 100}, {REST, 80}, {NOTE_A5, 100}, {REST, 80}, {NOTE_A5, 200},
};
static const buzzer_note_t mel_buddy_happy[] = {
    {NOTE_C6, 60}, {NOTE_E6, 60}, {NOTE_G6, 60}, {NOTE_C7, 120},
};
static const buzzer_note_t mel_buddy_sad[] = {
    {NOTE_E5, 150}, {NOTE_C5, 200},
};

/* Hour chime: 每组 3 响成起伏 */
static const buzzer_note_t mel_hour_group3[] = {
    {NOTE_C6, 150}, {NOTE_E6, 150}, {NOTE_G6, 200},
};
static const buzzer_note_t mel_hour_rest[] = {
    {REST, 400},
};
/* Half chime: 单响低音 */
static const buzzer_note_t mel_half[] = {
    {NOTE_A5, 200},
};

typedef struct {
    const buzzer_note_t *notes;
    uint8_t count;
} buzzer_melody_t;

static const buzzer_melody_t melodies[SOUND_COUNT] = {
    [SOUND_KEY_CLICK]        = {mel_key_click,        1},
    [SOUND_CONFIRM]          = {mel_confirm,           2},
    [SOUND_CANCEL]           = {mel_cancel,            2},
    [SOUND_SUCCESS]          = {mel_success,           3},
    [SOUND_FAIL]             = {mel_fail,              2},
    [SOUND_WIFI_CONNECT]     = {mel_wifi_connect,      1},
    [SOUND_WIFI_CONNECTED]   = {mel_wifi_connected,    2},
    [SOUND_WIFI_FAILED]      = {mel_wifi_failed,       3},
    [SOUND_SYNC_START]       = {mel_sync_start,        1},
    [SOUND_SYNC_DONE]        = {mel_sync_done,         2},
    [SOUND_POMO_START]       = {mel_pomo_start,        4},
    [SOUND_POMO_WORK_START]  = {mel_pomo_work_start,   3},
    [SOUND_POMO_BREAK_START] = {mel_pomo_break_start,  2},
    [SOUND_POMO_WORK_DONE]   = {mel_pomo_work_done,    3},
    [SOUND_POMO_BREAK_DONE]  = {mel_pomo_break_done,   5},
    [SOUND_POMO_LONG_BREAK]  = {mel_pomo_long_break,   4},
    [SOUND_BUDDY_ATTENTION]  = {mel_buddy_attention,   5},
    [SOUND_BUDDY_HAPPY]      = {mel_buddy_happy,       4},
    [SOUND_BUDDY_SAD]        = {mel_buddy_sad,         2},
};

/* ---- Category mapping ---- */
static const sound_category_t cat_map[SOUND_COUNT] = {
    [SOUND_KEY_CLICK]        = SND_CAT_KEY,
    [SOUND_CONFIRM]          = SND_CAT_UI,
    [SOUND_CANCEL]           = SND_CAT_UI,
    [SOUND_SUCCESS]          = SND_CAT_UI,
    [SOUND_FAIL]             = SND_CAT_UI,
    [SOUND_WIFI_CONNECT]     = SND_CAT_NET,
    [SOUND_WIFI_CONNECTED]   = SND_CAT_NET,
    [SOUND_WIFI_FAILED]      = SND_CAT_NET,
    [SOUND_SYNC_START]       = SND_CAT_NET,
    [SOUND_SYNC_DONE]        = SND_CAT_NET,
    [SOUND_POMO_START]       = SND_CAT_POMO,
    [SOUND_POMO_WORK_START]  = SND_CAT_POMO,
    [SOUND_POMO_BREAK_START] = SND_CAT_POMO,
    [SOUND_POMO_WORK_DONE]   = SND_CAT_POMO,
    [SOUND_POMO_BREAK_DONE]  = SND_CAT_POMO,
    [SOUND_POMO_LONG_BREAK]  = SND_CAT_POMO,
    [SOUND_BUDDY_ATTENTION]  = SND_CAT_BUDDY,
    [SOUND_BUDDY_HAPPY]      = SND_CAT_BUDDY,
    [SOUND_BUDDY_SAD]        = SND_CAT_BUDDY,
};

/* ---- NVS keys / defaults per category ---- */
static const char *cat_keys[SND_CAT_COUNT] = {
    KEY_SND_CAT_KEY, KEY_SND_CAT_UI, KEY_SND_CAT_NET, KEY_SND_CAT_POMO,
    KEY_SND_CAT_BUDDY, KEY_SND_CAT_HOUR, KEY_SND_CAT_HALF,
};
static const bool cat_defaults[SND_CAT_COUNT] = {
    true, true, true, true, true, false, false,
};

/* ---- State ---- */
static bool sound_enabled = true;
static bool cat_enabled[SND_CAT_COUNT];
static int quiet_start = 22;
static int quiet_end   = 7;

void sound_service_init(void)
{
    int32_t v;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SOUND, &v)) {
        sound_enabled = (v != 0);
    }
    for (int i = 0; i < SND_CAT_COUNT; i++) {
        if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, cat_keys[i], &v)) {
            cat_enabled[i] = (v != 0);
        } else {
            cat_enabled[i] = cat_defaults[i];
        }
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_QUIET_START, &v) && v >= 0 && v <= 23) {
        quiet_start = (int)v;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_QUIET_END, &v) && v >= 0 && v <= 23) {
        quiet_end = (int)v;
    }
    ESP_LOGI(TAG, "Sound init enabled=%d quiet=%d-%d",
             sound_enabled, quiet_start, quiet_end);
}

void sound_service_play(sound_id_t id)
{
    if (!sound_enabled) return;
    if (id < 0 || id >= SOUND_COUNT) return;
    sound_category_t cat = cat_map[id];
    if (!cat_enabled[cat]) return;
    const buzzer_melody_t *m = &melodies[id];
    buzzer_play_melody(m->notes, m->count);
}

bool sound_service_is_enabled(void)
{
    return sound_enabled;
}

void sound_service_set_enabled(bool enabled)
{
    sound_enabled = enabled;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_SOUND, enabled ? 1 : 0);
    if (!enabled) {
        buzzer_stop();
    }
    ESP_LOGI(TAG, "Sound %s", enabled ? "enabled" : "disabled");
}

bool sound_service_is_category_enabled(sound_category_t cat)
{
    if (cat < 0 || cat >= SND_CAT_COUNT) return false;
    return cat_enabled[cat];
}

void sound_service_set_category_enabled(sound_category_t cat, bool on)
{
    if (cat < 0 || cat >= SND_CAT_COUNT) return;
    cat_enabled[cat] = on;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, cat_keys[cat], on ? 1 : 0);
    ESP_LOGI(TAG, "Category %d = %d", cat, on);
}

void sound_service_play_hour_chime(int hour12)
{
    if (!sound_enabled) return;
    if (!cat_enabled[SND_CAT_HOUR_CHIME]) return;
    if (hour12 < 1 || hour12 > 12) return;

    /* 动态拼装：每 3 响为一组，组间插入 rest */
    buzzer_note_t buf[16];
    int n = 0;
    int full_groups = hour12 / 3;
    int remainder   = hour12 % 3;
    for (int g = 0; g < full_groups; g++) {
        if (g > 0) buf[n++] = mel_hour_rest[0];
        for (int i = 0; i < 3; i++) buf[n++] = mel_hour_group3[i];
    }
    if (remainder > 0) {
        if (full_groups > 0) buf[n++] = mel_hour_rest[0];
        for (int i = 0; i < remainder; i++) buf[n++] = mel_hour_group3[i];
    }
    buzzer_play_melody(buf, (uint8_t)n);
}

void sound_service_play_half_chime(void)
{
    if (!sound_enabled) return;
    if (!cat_enabled[SND_CAT_HALF_CHIME]) return;
    buzzer_play_melody(mel_half, 1);
}

bool sound_service_is_quiet_hour(int hour)
{
    int s = quiet_start, e = quiet_end;
    if (s == e) return false;
    return (s < e) ? (hour >= s && hour < e)
                   : (hour >= s || hour < e);
}

void sound_service_set_quiet_range(int start, int end)
{
    if (start < 0 || start > 23) return;
    if (end < 0 || end > 23) return;
    quiet_start = start;
    quiet_end = end;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_QUIET_START, start);
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_QUIET_END, end);
    ESP_LOGI(TAG, "Quiet range set: %d-%d", start, end);
}

void sound_service_get_quiet_range(int *start, int *end)
{
    if (start) *start = quiet_start;
    if (end)   *end   = quiet_end;
}
```

- [ ] **Step 2: 编译验证**

Run: `./build.sh`
Expected: 编译通过。原有调用 `sound_service_play(SOUND_xxx)` 全部兼容（参数与返回值未变）。

- [ ] **Step 3: 提交**

```bash
git add main/service/sound_service.h main/service/sound_service.c
git commit -m "feat: sound_service 增加分类开关、报时与静默时段支持"
```

---

## Task 5: 新建 chime_service

**Files:**
- Create: `main/service/chime_service.h`
- Create: `main/service/chime_service.c`

- [ ] **Step 1: 创建 chime_service.h**

写入 `main/service/chime_service.h`：

```c
#pragma once

/* 整点/半点报时检测服务。
 * 由 UIUpdate 任务每秒调用一次。
 * 内部用绝对分钟 ID 去重，确保每个目标分钟只响一次。 */

void chime_service_init(void);
void chime_service_tick(void);
```

- [ ] **Step 2: 创建 chime_service.c**

写入 `main/service/chime_service.c`：

```c
#include "chime_service.h"
#include "sound_service.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "CHIME";

static long s_last_minute_id = -1;

void chime_service_init(void)
{
    time_t now;
    time(&now);
    s_last_minute_id = (long)(now / 60);
    ESP_LOGI(TAG, "Chime service init, current minute_id=%ld", s_last_minute_id);
}

void chime_service_tick(void)
{
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);

    /* 只在整分(秒==0 附近，靠去重处理)且分钟为 0 或 30 时考虑触发 */
    if (t.tm_min != 0 && t.tm_min != 30) return;

    long mid = (long)(now / 60);
    if (mid == s_last_minute_id) return;
    s_last_minute_id = mid;

    /* 静默时段不响 */
    if (sound_service_is_quiet_hour(t.tm_hour)) return;

    if (t.tm_min == 0) {
        int h12 = t.tm_hour % 12;
        if (h12 == 0) h12 = 12;
        ESP_LOGI(TAG, "Hour chime: %d", h12);
        sound_service_play_hour_chime(h12);
    } else {
        ESP_LOGI(TAG, "Half chime");
        sound_service_play_half_chime();
    }
}
```

- [ ] **Step 3: 添加到 CMakeLists.txt**

打开 `main/CMakeLists.txt`，找到 `service/sound_service.c` 那一行（约第 35 行），在它后面加一行：

```
                          service/sound_service.c
                          service/chime_service.c
```

- [ ] **Step 4: 编译验证**

Run: `./build.sh`
Expected: 编译通过。`chime_service_tick()` 还未被调用，仅符号存在。

- [ ] **Step 5: 提交**

```bash
git add main/service/chime_service.h main/service/chime_service.c main/CMakeLists.txt
git commit -m "feat: 新建 chime_service 整点/半点报时检测服务"
```

---

## Task 6: 新建声音设置页 ui_screen_settings_sound

**Files:**
- Create: `main/ui/ui_screen_settings_sound.h`
- Create: `main/ui/ui_screen_settings_sound.c`

- [ ] **Step 1: 创建头文件**

写入 `main/ui/ui_screen_settings_sound.h`：

```c
#pragma once

#include "lvgl.h"

lv_obj_t* ui_screen_settings_sound_create(void);
void ui_screen_settings_sound_refresh(void);
```

- [ ] **Step 2: 创建实现文件**

写入 `main/ui/ui_screen_settings_sound.c`：

```c
#include "ui_screen_settings_sound.h"
#include "custom_font.h"
#include "i18n.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/storage_service.h"
#include "service/sound_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_SOUND";

#define SOUND_ITEM_COUNT 10

/* 索引含义：
 *   0..7  开关项（总开关 + 7 分类）
 *   8     Quiet Start
 *   9     Quiet End
 */
typedef enum { MODE_NAV, MODE_ADJUST } sound_mode_t;

static sound_mode_t sound_mode = MODE_NAV;
static int selected_item = 0;
static bool bool_vals[8];     /* items 0..7 */
static int  quiet_vals[2];    /* items 8,9: start,end */

static lv_obj_t *screen = NULL;
static lv_obj_t *sound_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[SOUND_ITEM_COUNT][20];
static char item_values[SOUND_ITEM_COUNT][12];
static ui_list_item_t items[SOUND_ITEM_COUNT];

static const str_id_t bool_key_ids[8] = {
    STR_T_SOUND, STR_SND_KEY, STR_SND_UI, STR_SND_NET,
    STR_SND_POMO, STR_SND_BUDDY, STR_SND_HOUR, STR_SND_HALF,
};
static const sound_category_t cat_map[8] = {
    SND_CAT_KEY,        /* index 0 = 总开关, 占位无意义 */
    SND_CAT_KEY,
    SND_CAT_UI,
    SND_CAT_NET,
    SND_CAT_POMO,
    SND_CAT_BUDDY,
    SND_CAT_HOUR_CHIME,
    SND_CAT_HALF_CHIME,
};

static void load_values(void)
{
    bool_vals[0] = sound_service_is_enabled();
    for (int i = 1; i < 8; i++) {
        bool_vals[i] = sound_service_is_category_enabled(cat_map[i]);
    }
    int qs, qe;
    sound_service_get_quiet_range(&qs, &qe);
    quiet_vals[0] = qs;
    quiet_vals[1] = qe;
}

static void update_display(void)
{
    const char *on_off[] = {i18n(STR_OFF), i18n(STR_ON)};

    for (int i = 0; i < 8; i++) {
        snprintf(item_keys[i], sizeof(item_keys[i]), "%s", i18n(bool_key_ids[i]));
        snprintf(item_values[i], sizeof(item_values[i]), "%s", on_off[bool_vals[i] ? 1 : 0]);
    }
    snprintf(item_keys[8], sizeof(item_keys[8]), "%s", i18n(STR_QUIET_START));
    snprintf(item_values[8], sizeof(item_values[8]), i18n(STR_FMT_HOUR), quiet_vals[0]);
    snprintf(item_keys[9], sizeof(item_keys[9]), "%s", i18n(STR_QUIET_END));
    snprintf(item_values[9], sizeof(item_values[9]), i18n(STR_FMT_HOUR), quiet_vals[1]);

    for (int i = 0; i < SOUND_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (sound_list) {
        ui_list_set_selected_color(sound_list,
            sound_mode == MODE_ADJUST ? lv_color_hex(0xFFAA00) : lv_color_hex(0x00FF00));
        ui_list_set_items(sound_list, items, SOUND_ITEM_COUNT);
        ui_list_set_selected(sound_list, selected_item);
    }

    if (hint_label) {
        if (sound_mode == MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_TOGGLE_PRESS_BACK));
        }
    }
}

static void sound_on_encoder_cw(void)
{
    int step = ui_get_encoder_step();
    if (sound_mode == MODE_ADJUST) {
        if (selected_item == 8 || selected_item == 9) {
            quiet_vals[selected_item - 8] = (quiet_vals[selected_item - 8] + step) % 24;
        }
        update_display();
    } else {
        selected_item = (selected_item + 1) % SOUND_ITEM_COUNT;
        update_display();
    }
}

static void sound_on_encoder_ccw(void)
{
    int step = ui_get_encoder_step();
    if (sound_mode == MODE_ADJUST) {
        if (selected_item == 8 || selected_item == 9) {
            quiet_vals[selected_item - 8] = (quiet_vals[selected_item - 8] - step + 24) % 24;
        }
        update_display();
    } else {
        selected_item = (selected_item - 1 + SOUND_ITEM_COUNT) % SOUND_ITEM_COUNT;
        update_display();
    }
}

static void sound_on_encoder_press(void)
{
    if (sound_mode == MODE_ADJUST) {
        /* 取消：恢复加载的值 */
        load_values();
        sound_mode = MODE_NAV;
        update_display();
    } else {
        ui_go_back();
    }
}

static void sound_on_settings_press(void)
{
    if (sound_mode == MODE_ADJUST) {
        /* 保存静默时段并退出 ADJUST */
        sound_service_set_quiet_range(quiet_vals[0], quiet_vals[1]);
        sound_mode = MODE_NAV;
        update_display();
        return;
    }

    if (selected_item == 0) {
        /* 总开关 */
        bool_vals[0] = !bool_vals[0];
        sound_service_set_enabled(bool_vals[0]);
    } else if (selected_item >= 1 && selected_item <= 7) {
        /* 分类开关 */
        bool_vals[selected_item] = !bool_vals[selected_item];
        sound_service_set_category_enabled(cat_map[selected_item], bool_vals[selected_item]);
    } else {
        /* 8 / 9 = 进入 ADJUST */
        sound_mode = MODE_ADJUST;
    }
    update_display();
}

lv_obj_t* ui_screen_settings_sound_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    sound_list = NULL;
    hint_label = NULL;
    sound_mode = MODE_NAV;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_T_SOUND));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    sound_list = ui_list_create(screen, 220, 196, 10, 30);

    load_values();
    selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_TOGGLE_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = sound_on_encoder_cw,
        .on_encoder_ccw = sound_on_encoder_ccw,
        .on_encoder_press = sound_on_encoder_press,
        .on_encoder_long_press = NULL,
        .on_settings_press = sound_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_SOUND, &cbs);

    ESP_LOGI(TAG, "Settings Sound screen created");
    return screen;
}

void ui_screen_settings_sound_refresh(void)
{
    load_values();
    sound_mode = MODE_NAV;
    update_display();
}
```

- [ ] **Step 3: 添加到 CMakeLists.txt**

打开 `main/CMakeLists.txt`，找到 `ui/ui_screen_settings_system.c` 那一行（约第 21 行），在它后面加一行：

```
                          ui/ui_screen_settings_system.c
                          ui/ui_screen_settings_sound.c
```

- [ ] **Step 4: 编译验证（此时会有链接错误，下一步 Task 7 解决）**

Run: `./build.sh`
Expected: 报错 `UI_SCREEN_SETTINGS_SOUND undeclared`，因为还没在 ui_manager.h 添加。这是预期的，进入 Task 7。

- [ ] **Step 5: 提交**

```bash
git add main/ui/ui_screen_settings_sound.h main/ui/ui_screen_settings_sound.c main/CMakeLists.txt
git commit -m "feat: 新建声音设置页（10 项配置）"
```

---

## Task 7: 在 ui_manager 注册新屏幕

**Files:**
- Modify: `main/ui/ui_manager.h`（枚举）
- Modify: `main/ui/ui_manager.c`（include、lazy creator、disposable）

- [ ] **Step 1: 在 ui_manager.h 枚举中添加 UI_SCREEN_SETTINGS_SOUND**

打开 `main/ui/ui_manager.h`，找到 `UI_SCREEN_SETTINGS_SYSTEM,`（约第 18 行），在它后面加一行：

```c
    UI_SCREEN_SETTINGS_SYSTEM,
    UI_SCREEN_SETTINGS_SOUND,
    UI_SCREEN_SETTINGS_DEBUG,
```

- [ ] **Step 2: 在 ui_manager.c 加 include**

打开 `main/ui/ui_manager.c`，找到 `#include "ui_screen_settings_system.h"`（约第 15 行），在它后面加一行：

```c
#include "ui_screen_settings_system.h"
#include "ui_screen_settings_sound.h"
```

- [ ] **Step 3: 在 lazy_creators 数组中注册**

找到 `lazy_creators[UI_SCREEN_SETTINGS_SYSTEM] = ui_screen_settings_system_create;`（约第 112 行），在它后面加一行：

```c
    lazy_creators[UI_SCREEN_SETTINGS_SYSTEM] = ui_screen_settings_system_create;
    lazy_creators[UI_SCREEN_SETTINGS_SOUND]  = ui_screen_settings_sound_create;
```

- [ ] **Step 4: 在 screen_is_disposable() 中注册**

找到 `id == UI_SCREEN_SETTINGS_SYSTEM ||`（约第 52 行），在它后面加一行：

```c
           id == UI_SCREEN_SETTINGS_SYSTEM ||
           id == UI_SCREEN_SETTINGS_SOUND  ||
```

- [ ] **Step 5: 编译验证**

Run: `./build.sh`
Expected: 编译通过。

- [ ] **Step 6: 提交**

```bash
git add main/ui/ui_manager.h main/ui/ui_manager.c
git commit -m "feat: ui_manager 注册 SETTINGS_SOUND 屏幕"
```

---

## Task 8: 设置主页添加 Sound 入口

**Files:**
- Modify: `main/ui/ui_screen_settings.c`

- [ ] **Step 1: 改 SETTINGS_ITEM_COUNT 为 5**

打开 `main/ui/ui_screen_settings.c`，找到 `#define SETTINGS_ITEM_COUNT 4`（约第 11 行），改为：

```c
#define SETTINGS_ITEM_COUNT 5
```

- [ ] **Step 2: 在 settings_name_ids 中插入 STR_M_SOUND**

找到 `static const str_id_t settings_name_ids[SETTINGS_ITEM_COUNT]` 数组（约第 24-26 行），改为：

```c
static const str_id_t settings_name_ids[SETTINGS_ITEM_COUNT] = {
    STR_M_LIGHT, STR_M_SOUND, STR_M_WIFI, STR_M_TIME, STR_M_SYSTEM
};
```

- [ ] **Step 3: 更新 navigate_to_subpage 的 case 编号**

找到 `navigate_to_subpage()` 函数（约第 92 行），改为：

```c
static void navigate_to_subpage(void)
{
    settings_mode = SETTINGS_MODE_IDLE;
    update_display();

    switch (current_settings_item) {
        case 0: ui_switch_screen(UI_SCREEN_SETTINGS_LIGHT);  break;
        case 1: ui_switch_screen(UI_SCREEN_SETTINGS_SOUND);  break;
        case 2: ui_switch_screen(UI_SCREEN_WIFI_SAVED);      break;
        case 3: ui_switch_screen(UI_SCREEN_SETTINGS_TIME);   break;
        case 4: ui_switch_screen(UI_SCREEN_SETTINGS_SYSTEM); break;
    }
}
```

- [ ] **Step 4: 编译验证**

Run: `./build.sh`
Expected: 编译通过。

- [ ] **Step 5: 提交**

```bash
git add main/ui/ui_screen_settings.c
git commit -m "feat: 设置主页菜单加入 Sound 入口（4→5 项）"
```

---

## Task 9: 系统设置页移除 Sound 项

**Files:**
- Modify: `main/ui/ui_screen_settings_system.c`

- [ ] **Step 1: 改 SYSTEM_ITEM_COUNT 为 3**

打开 `main/ui/ui_screen_settings_system.c`，找到 `#define SYSTEM_ITEM_COUNT 4`（约第 14 行），改为：

```c
#define SYSTEM_ITEM_COUNT 3
```

- [ ] **Step 2: 缩小 system_values 数组并调整注释**

找到 `static int system_values[SYSTEM_ITEM_COUNT] = {1, 0, 0, 1};`（约第 20 行），改为：

```c
/* system_values[0..2]: dir, lang, sleep_idx（sound 已移到独立页） */
static int system_values[SYSTEM_ITEM_COUNT] = {0, 0, 1};
```

- [ ] **Step 3: 重写 update_display() 中的赋值**

找到 `update_display()` 函数（约第 32-79 行），完整替换为以下内容：

```c
static void update_display(void)
{
    const char *dir_opts[] = {i18n(STR_NORMAL), i18n(STR_REV)};
    const char *lang_opts[] = {i18n(STR_LANG_EN), i18n(STR_LANG_ZH)};

    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_DIRECTION));
    snprintf(item_values[0], sizeof(item_values[0]), "%s", dir_opts[system_values[0] % 2]);

    snprintf(item_keys[1], sizeof(item_keys[1]), "%s", i18n(STR_LANGUAGE));
    snprintf(item_values[1], sizeof(item_values[1]), "%s", lang_opts[system_values[1] % 2]);

    snprintf(item_keys[2], sizeof(item_keys[2]), "%s", i18n(STR_SLEEP_TIMEOUT));
    {
        int idx = system_values[2] % SLEEP_OPT_COUNT;
        int val = sleep_mins[idx];
        if (val == 0) {
            snprintf(item_values[2], sizeof(item_values[2]), "%s", i18n(STR_OFF_VAL));
        } else if (val < 0) {
            snprintf(item_values[2], sizeof(item_values[2]), i18n(STR_FMT_SEC), -val);
        } else {
            snprintf(item_values[2], sizeof(item_values[2]), i18n(STR_FMT_MIN), val);
        }
    }

    for (int i = 0; i < SYSTEM_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (system_list) {
        ui_list_set_selected_color(system_list,
            sys_mode == MODE_ADJUST ? lv_color_hex(0xFFAA00) : lv_color_hex(0x00FF00));
        ui_list_set_items(system_list, items, SYSTEM_ITEM_COUNT);
        ui_list_set_selected(system_list, system_selected_item);
    }

    if (hint_label) {
        if (sys_mode == MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_TOGGLE_PRESS_BACK));
        }
    }
}
```

- [ ] **Step 4: 修改 system_on_encoder_cw 和 ccw 中的索引**

找到 `system_on_encoder_cw()` 函数（约第 81-92 行），完整替换为：

```c
static void system_on_encoder_cw(void)
{
    if (sys_mode == MODE_ADJUST) {
        if (system_selected_item == 2) {
            system_values[2] = (system_values[2] + 1) % SLEEP_OPT_COUNT;
        }
        update_display();
    } else {
        system_selected_item = (system_selected_item + 1) % SYSTEM_ITEM_COUNT;
        update_display();
    }
}

static void system_on_encoder_ccw(void)
{
    if (sys_mode == MODE_ADJUST) {
        if (system_selected_item == 2) {
            system_values[2] = (system_values[2] - 1 + SLEEP_OPT_COUNT) % SLEEP_OPT_COUNT;
        }
        update_display();
    } else {
        system_selected_item = (system_selected_item - 1 + SYSTEM_ITEM_COUNT) % SYSTEM_ITEM_COUNT;
        update_display();
    }
}
```

- [ ] **Step 5: 修改 system_on_encoder_press 的取消逻辑**

找到 `system_on_encoder_press()` 函数（约第 107-121 行），完整替换为：

```c
static void system_on_encoder_press(void)
{
    if (sys_mode == MODE_ADJUST) {
        /* Cancel: reload saved value */
        int32_t val;
        if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)SLEEP_OPT_COUNT) {
            system_values[2] = (int)val;
        }
        sys_mode = MODE_SELECT;
    } else {
        ui_go_back();
        return;
    }
    update_display();
}
```

- [ ] **Step 6: 修改 system_on_settings_press 的逻辑**

找到 `system_on_settings_press()` 函数（约第 123-157 行），完整替换为：

```c
static void system_on_settings_press(void)
{
    if (sys_mode == MODE_ADJUST) {
        /* Save and exit adjust */
        if (system_selected_item == 2) {
            extern int sleep_timeout_idx;
            sleep_timeout_idx = system_values[2];
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, system_values[2]);
        }
        sys_mode = MODE_SELECT;
        update_display();
        return;
    }

    switch (system_selected_item) {
        case 0:
            system_values[0] = !system_values[0];
            input_handler_set_reverse(system_values[0]);
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, system_values[0]);
            break;
        case 1:
            system_values[1] = !system_values[1];
            i18n_set_lang(system_values[1] ? LANG_ZH : LANG_EN);
            break;
        case 2:
            sys_mode = MODE_ADJUST;
            break;
    }
    update_display();
}
```

- [ ] **Step 7: 修改 create 和 refresh 函数中的 NVS 加载**

找到 `ui_screen_settings_system_create()` 函数（约第 159 行），将其中的 NVS 加载段：

```c
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
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)SLEEP_OPT_COUNT) {
        system_values[3] = (int)val;
    }
```

替换为：

```c
    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, &val)) {
        system_values[0] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, &val)) {
        system_values[1] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)SLEEP_OPT_COUNT) {
        system_values[2] = (int)val;
    }
```

- [ ] **Step 8: 修改 refresh 函数的 NVS 加载（同样逻辑）**

找到 `ui_screen_settings_system_refresh()` 函数（约第 214 行），将其中 NVS 加载段：

```c
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
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)SLEEP_OPT_COUNT) {
        system_values[3] = (int)val;
    }
```

替换为：

```c
    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, &val)) {
        system_values[0] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, &val)) {
        system_values[1] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)SLEEP_OPT_COUNT) {
        system_values[2] = (int)val;
    }
```

- [ ] **Step 9: 移除 sound_service.h include（已不再使用）**

打开 `main/ui/ui_screen_settings_system.c`，找到 `#include "service/sound_service.h"`（约第 7 行），删除该行。

- [ ] **Step 10: 编译验证**

Run: `./build.sh`
Expected: 编译通过，无未使用变量警告。

- [ ] **Step 11: 提交**

```bash
git add main/ui/ui_screen_settings_system.c
git commit -m "refactor: 系统设置页移除 Sound 项（4→3 项）"
```

---

## Task 10: main.c 集成 chime_service

**Files:**
- Modify: `main/main.c`（include、初始化、UIUpdate 集成）

- [ ] **Step 1: 添加 include**

打开 `main/main.c`，找到 `#include "service/sound_service.h"`（约第 33 行），在它后面加一行：

```c
#include "service/sound_service.h"
#include "service/chime_service.h"
```

- [ ] **Step 2: 在 app_main 初始化序列加 chime_service_init**

找到 `sound_service_init();`（约第 639 行），在它后面加一行：

```c
    sound_service_init();
    chime_service_init();
```

- [ ] **Step 3: 在 ui_update_task 中每秒调用 chime_service_tick（使用独立计数器）**

找到 `ui_update_task()` 函数顶部的局部变量声明（约第 385 行）：

```c
    int64_t last_pomodoro_tick = 0;
    int64_t last_wifi_ui_tick = 0;
    int64_t last_mem_tick = 0;
    int64_t last_debug_tick = 0;
```

在 `last_debug_tick` 后追加一个独立计数器：

```c
    int64_t last_pomodoro_tick = 0;
    int64_t last_wifi_ui_tick = 0;
    int64_t last_mem_tick = 0;
    int64_t last_debug_tick = 0;
    int64_t last_chime_tick = 0;
```

然后在循环体内（推荐位置：紧贴 `last_mem_tick` 块之前，约第 525 行 `// Memory monitor every 30 seconds` 注释前）插入：

```c
        /* Chime service: check hour/half-hour boundary every 1 second */
        if (now - last_chime_tick >= 1000) {
            chime_service_tick();
            last_chime_tick = now;
        }

        // Memory monitor every 30 seconds
```

用独立计数器避免与番茄钟 tick 状态耦合。

- [ ] **Step 4: 编译验证**

Run: `./build.sh`
Expected: 编译通过。

- [ ] **Step 5: 提交**

```bash
git add main/main.c
git commit -m "feat: main.c 集成 chime_service 初始化与每秒 tick"
```

---

## Task 11: 硬件功能验证

**Files:** （无代码改动，硬件烧录验证）

- [ ] **Step 1: 烧录固件**

Run: `./build.sh flash`
Expected: 编译并烧录成功。

- [ ] **Step 2: 验证设置主页 5 项**

操作：进入设置页（侧键长按）→ 应看到 Light / Sound / WiFi / Time / System 5 项。
预期：菜单显示 5 项，"🔊Sound" 在第二位。

- [ ] **Step 3: 进入声音设置页验证 10 项**

操作：旋转编码器到 "🔊Sound" → 按侧键进入。
预期：标题 "🔊Sound"，列表 10 项：
1. 🔊Sound: On
2. 🔑Key: On
3. ✅UI: On
4. 📶Net: On
5. 🍅Pomo: On
6. 🐱Buddy: On
7. 🔔Hour: Off
8. 🔎Half: Off
9. 🌙Q.Start: 22h
10. 🌙Q.End: 7h

- [ ] **Step 4: 验证总开关**

操作：选中 "🔊Sound" 按侧键 → 关闭。
预期：所有声音消失。再次按侧键 → 开启。

- [ ] **Step 5: 验证分类开关独立**

操作：关闭 "🍅Pomo" 分类 → 旋转编码器到番茄钟页 → 启动番茄。
预期：番茄开始/结束都不响（其他分类如 UI 反馈仍响）。

- [ ] **Step 6: 验证静默时段调整**

操作：进入声音设置 → 选 "🌙Q.Start" → 按侧键进入 ADJUST → 旋转编码器调整。
预期：值在 0-23 之间循环；按侧键保存退出；按编码器取消（恢复原值）。

- [ ] **Step 7: 验证系统设置页 3 项**

操作：回到设置主页 → 进 System。
预期：只剩 Direction / Language / Sleep 三项，无 Sound。

- [ ] **Step 8: 验证整点报时**

操作：开启 "🔔Hour" 分类 → 等待到下一个整点（或将设备时钟调到整点）。
预期：当前小时数对应响数（如下午 3 点 = 3 响成起伏组，下午 12 点 = 4 组）。

- [ ] **Step 9: 验证半点报时**

操作：开启 "🔎Half" 分类 → 等待到 xx:30。
预期：单响低音。

- [ ] **Step 10: 验证静默时段拦截**

操作：临时设置 Q.Start = 当前小时，Q.End = 当前小时+1 → 等到整点。
预期：在静默时段内不响。恢复 Q.Start = Q.End → 不再静默。

- [ ] **Step 11: 验证去重**

操作：等待整点，听一次响 → 之后在该分钟内不响；下一分钟（非 0/30）也不响；下一个整点（下一小时 0 分）再响。
预期：每个目标分钟只响一次。

- [ ] **Step 12: 验证 UIUpdate 漏秒容错**

操作：触发 Sleep（等待设备休眠）→ 整点附近唤醒。
预期：唤醒后只要仍在 0 分或 30 分区间，会响一次（因为 minute_id 是新的）。

- [ ] **Step 13: 验证跨午夜静默**

操作：设置 Q.Start=22, Q.End=7 → 等到 23:00 整点。
预期：不响（23 在静默段）。等到 7:00 整点：响（7 不在 [22, 24) ∪ [0, 7) 内）。

- [ ] **Step 14: 提交测试通过标记**

无代码改动，无需提交。在最终 PR 描述中列出验证结果。

---

## 验证清单（嵌入式替代 TDD）

由于 ESP-IDF 项目无传统单元测试框架，用以下方式替代：

| 维度 | 验证方式 |
|---|---|
| 编译 | 每个 Task 后 `./build.sh` |
| 类型一致 | cat_map 数组、cat_keys 数组、cat_defaults 数组索引与 SND_CAT_COUNT 对齐 |
| 数据完整 | sound_id_t 19 个 → cat_map 19 项映射；SOUND_COUNT 与 melodies 数组对齐 |
| NVS 兼容 | 旧 `sound` 键无破坏；新键首次加载走默认值 |
| 功能 | 烧录硬件，按 Task 11 步骤手动验证 |
| 边界 | 整点 12 点（4 组）、跨午夜静默、休眠唤醒容错 |

---

## 自审与执行移交

**计划完成并保存到 `docs/superpowers/plans/2026-06-12-sound-settings-and-chime.md`。两种执行方式：**

**1. Subagent-Driven（推荐）** - 每个 Task 派发独立 subagent，每 Task 后做规范符合性 + 代码质量两阶段审查，迭代快。

**2. Inline Execution** - 在当前会话内顺序执行，按 checkpoint 暂停审查。

**选哪种？**
