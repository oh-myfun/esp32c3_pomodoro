# 蜂鸣器音效方案 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为设备所有交互场景添加非阻塞蜂鸣器音效反馈，并在设置中提供声音开关。

**Architecture:** 在现有 `driver/buzzer.c` LEDC 驱动之上，新增 `service/sound_service` 模块封装音效数据和非阻塞播放逻辑。使用 esp_timer 单次定时器驱动音符队列，所有旋律为编译期常量。声音开关通过 NVS 持久化。

**Tech Stack:** ESP-IDF v5.5.4, LEDC PWM, esp_timer, NVS

---

### Task 1: 扩展 buzzer 驱动，添加非阻塞播放接口

**Files:**
- Modify: `main/driver/buzzer.h`
- Modify: `main/driver/buzzer.c`

- [ ] **Step 1: 修改 buzzer.h，添加音符类型和非阻塞播放 API**

在 `main/driver/buzzer.h` 末尾添加：

```c
typedef struct {
    uint32_t freq_hz;    // 0 = 休止符（静音）
    uint16_t duration_ms;
} buzzer_note_t;

// 非阻塞播放：esp_timer 驱动音符序列
void buzzer_play_melody(const buzzer_note_t *notes, uint8_t count);
void buzzer_stop(void);
bool buzzer_is_playing(void);
```

- [ ] **Step 2: 修改 buzzer.c，实现 esp_timer 驱动的非阻塞播放**

在 `main/driver/buzzer.c` 中添加以下代码（在文件末尾）：

```c
static esp_timer_handle_t play_timer = NULL;
static const buzzer_note_t *play_notes = NULL;
static uint8_t play_count = 0;
static uint8_t play_index = 0;
static bool playing = false;

static void play_timer_callback(void *arg)
{
    buzzer_off();
    play_index++;

    if (play_index < play_count) {
        const buzzer_note_t *note = &play_notes[play_index];
        if (note->freq_hz > 0) {
            buzzer_set_frequency(note->freq_hz);
            buzzer_on();
        }
        esp_timer_start_once(play_timer, note->duration_ms * 1000);
    } else {
        playing = false;
    }
}

void buzzer_play_melody(const buzzer_note_t *notes, uint8_t count)
{
    if (!buzzer_initialized || count == 0 || !notes) return;

    // 中断当前播放
    if (play_timer) {
        esp_timer_stop(play_timer);
        esp_timer_delete(play_timer);
        play_timer = NULL;
    }
    buzzer_off();

    play_notes = notes;
    play_count = count;
    play_index = 0;
    playing = true;

    // 创建定时器
    const esp_timer_create_args_t timer_args = {
        .callback = &play_timer_callback,
        .name = "buzzer_play"
    };
    esp_timer_create(&timer_args, &play_timer);

    // 播放第一个音符
    const buzzer_note_t *note = &play_notes[0];
    if (note->freq_hz > 0) {
        buzzer_set_frequency(note->freq_hz);
        buzzer_on();
    }
    esp_timer_start_once(play_timer, note->duration_ms * 1000);
}

void buzzer_stop(void)
{
    if (play_timer) {
        esp_timer_stop(play_timer);
        esp_timer_delete(play_timer);
        play_timer = NULL;
    }
    buzzer_off();
    playing = false;
}

bool buzzer_is_playing(void)
{
    return playing;
}
```

- [ ] **Step 3: 构建**

Run: `./build.sh`
Expected: 编译成功，无报错

- [ ] **Step 4: 提交**

```bash
git add main/driver/buzzer.h main/driver/buzzer.c
git commit -m "feat: add non-blocking melody playback to buzzer driver"
```

---

### Task 2: 创建 sound_service 模块

**Files:**
- Create: `main/service/sound_service.h`
- Create: `main/service/sound_service.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: 创建 sound_service.h**

`main/service/sound_service.h`:

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
    SOUND_POMO_WORK_DONE,
    SOUND_POMO_BREAK_DONE,
    SOUND_POMO_LONG_BREAK,
    SOUND_BUDDY_ATTENTION,
    SOUND_BUDDY_HAPPY,
    SOUND_BUDDY_SAD,
    SOUND_COUNT
} sound_id_t;

void sound_service_init(void);
void sound_service_play(sound_id_t id);

bool sound_service_is_enabled(void);
void sound_service_set_enabled(bool enabled);
```

- [ ] **Step 2: 创建 sound_service.c，包含所有音效数据**

`main/service/sound_service.c`:

```c
#include "sound_service.h"
#include "driver/buzzer.h"
#include "service/storage_service.h"
#include "esp_log.h"

static const char *TAG = "SOUND";

// 音高常量 (Hz)
#define NOTE_C4   262
#define NOTE_EB4  311
#define NOTE_E4   330
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_D5   587
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

// --- 音效数据 (编译期常量) ---

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

// 音效查找表
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
    [SOUND_POMO_WORK_DONE]   = {mel_pomo_work_done,    3},
    [SOUND_POMO_BREAK_DONE]  = {mel_pomo_break_done,   5},
    [SOUND_POMO_LONG_BREAK]  = {mel_pomo_long_break,   4},
    [SOUND_BUDDY_ATTENTION]  = {mel_buddy_attention,   5},
    [SOUND_BUDDY_HAPPY]      = {mel_buddy_happy,       4},
    [SOUND_BUDDY_SAD]        = {mel_buddy_sad,         2},
};

static bool sound_enabled = true;

void sound_service_init(void)
{
    int32_t val = 1;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, "sound_on", &val);
    sound_enabled = (val != 0);
    ESP_LOGI(TAG, "Sound service initialized, enabled=%d", sound_enabled);
}

void sound_service_play(sound_id_t id)
{
    if (!sound_enabled) return;
    if (id < 0 || id >= SOUND_COUNT) return;

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
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "sound_on", enabled ? 1 : 0);
    if (!enabled) {
        buzzer_stop();
    }
    ESP_LOGI(TAG, "Sound %s", enabled ? "enabled" : "disabled");
}
```

- [ ] **Step 3: 修改 CMakeLists.txt，添加 sound_service.c**

在 `main/CMakeLists.txt` 的 SRCS 列表中 `service/storage_service.c` 之后添加 `service/sound_service.c`：

```
                          service/storage_service.c
                          service/sound_service.c
```

- [ ] **Step 4: 构建**

Run: `./build.sh`
Expected: 编译成功

- [ ] **Step 5: 提交**

```bash
git add main/service/sound_service.h main/service/sound_service.c main/CMakeLists.txt
git commit -m "feat: add sound_service module with non-blocking buzzer melodies"
```

---

### Task 3: 在 main.c 中初始化 sound_service 并添加 WiFi/番茄钟音效

**Files:**
- Modify: `main/main.c`

- [ ] **Step 1: 添加 include 和初始化**

在 `main/main.c` 的 include 区域添加：

```c
#include "service/sound_service.h"
```

在 `app_main()` 函数中，步骤 4（业务模块）之后、步骤 5（输入）之前添加 sound_service 初始化：

```c
    // 4.5. Sound service (after buzzer)
    sound_service_init();
```

- [ ] **Step 2: 在 WiFi 回调中添加音效**

修改 `on_wifi_connected` 回调函数，在 `ESP_LOGI` 之后添加：

```c
static void on_wifi_connected(const char *ip) {
    ESP_LOGI(TAG, "WiFi connected, IP: %s", ip ? ip : "null");
    wifi_fail_time = 0;
    sound_service_play(SOUND_WIFI_CONNECTED);
    time_service_request_sync();
}
```

修改 `on_wifi_connect_failed` 回调：

```c
static void on_wifi_connect_failed(void) {
    ESP_LOGI(TAG, "WiFi connect failed");
    wifi_fail_time = esp_timer_get_time() / 1000;
    sound_service_play(SOUND_WIFI_FAILED);
}
```

- [ ] **Step 3: 在 pomodoro tick 中添加音效**

在 `ui_update_task` 的 pomodoro tick 部分，`pomodoro_engine_tick()` 之后添加 phase 变化检测。需要修改 pomodoro tick 代码块，在调用 `pomodoro_engine_tick()` 之前保存旧 phase，之后比较：

将原来的 pomodoro tick 块：

```c
        // Pomodoro tick every 1 second
        if (now - last_pomodoro_tick >= 1000) {
            pomodoro_engine_tick();
            pomodoro_state_t state = pomodoro_engine_get_state();
            lvgl_lock();
            ui_screen_pomodoro_update_state(state.phase, state.remaining_seconds, state.completed_count, state.current_cycle);
            lvgl_unlock();
            last_pomodoro_tick = now;
        }
```

替换为：

```c
        // Pomodoro tick every 1 second
        if (now - last_pomodoro_tick >= 1000) {
            pomodoro_state_t prev = pomodoro_engine_get_state();
            pomodoro_engine_tick();
            pomodoro_state_t state = pomodoro_engine_get_state();

            // Phase change sound effects
            if (state.phase != prev.phase) {
                if (prev.phase == POMODORO_PHASE_WORK) {
                    if (state.phase == POMODORO_PHASE_LONG_BREAK) {
                        sound_service_play(SOUND_POMO_LONG_BREAK);
                    } else if (state.phase == POMODORO_PHASE_BREAK) {
                        sound_service_play(SOUND_POMO_WORK_DONE);
                    }
                } else if ((state.phase == POMODORO_PHASE_WORK) &&
                           (prev.phase == POMODORO_PHASE_BREAK || prev.phase == POMODORO_PHASE_LONG_BREAK)) {
                    sound_service_play(SOUND_POMO_BREAK_DONE);
                }
            }

            lvgl_lock();
            ui_screen_pomodoro_update_state(state.phase, state.remaining_seconds, state.completed_count, state.current_cycle);
            lvgl_unlock();
            last_pomodoro_tick = now;
        }
```

注意：需要确认 `pomodoro_engine.h` 中 phase 枚举值。当前值为：IDLE=0, WORK=1, BREAK=2, LONG_BREAK=3, PAUSED=4。

- [ ] **Step 4: 构建**

Run: `./build.sh`
Expected: 编译成功

- [ ] **Step 5: 提交**

```bash
git add main/main.c
git commit -m "feat: add sound effects for WiFi events and pomodoro phase changes"
```

---

### Task 4: 在 time_service 中添加同步音效

**Files:**
- Modify: `main/service/time_service.c`

- [ ] **Step 1: 在同步完成回调中播放音效**

在 `main/service/time_service.c` 顶部添加 include：

```c
#include "service/sound_service.h"
```

修改 `time_sync_notification` 函数：

```c
static void time_sync_notification(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized");
    synced = true;
    time_t now = time(NULL);
    storage_save_time((uint64_t)now);
    last_sync_time = now;
    sound_service_play(SOUND_SYNC_DONE);
}
```

注意：`time_sync_notification` 是由 SNTP 子系统在 esp_timer 任务上下文中调用的，`sound_service_play()` 内部使用 `esp_timer` 是安全的。

- [ ] **Step 2: 构建**

Run: `./build.sh`
Expected: 编译成功

- [ ] **Step 3: 提交**

```bash
git add main/service/time_service.c
git commit -m "feat: add time sync sound effect"
```

---

### Task 5: 在番茄钟界面添加开始/暂停音效

**Files:**
- Modify: `main/ui/ui_screen_pomodoro.c`

- [ ] **Step 1: 添加 include 和音效调用**

在 `main/ui/ui_screen_pomodoro.c` 顶部添加：

```c
#include "service/sound_service.h"
```

修改 `pomo_on_settings_press` 函数：

```c
static void pomo_on_settings_press(void)
{
    pomodoro_state_t state = pomodoro_engine_get_state();
    if (state.phase == POMODORO_PHASE_IDLE) {
        pomodoro_engine_start();
        sound_service_play(SOUND_POMO_START);
    } else if (state.is_paused) {
        pomodoro_engine_resume();
        sound_service_play(SOUND_CONFIRM);
    } else {
        pomodoro_engine_pause();
        sound_service_play(SOUND_CONFIRM);
    }
}
```

- [ ] **Step 2: 构建**

Run: `./build.sh`
Expected: 编译成功

- [ ] **Step 3: 提交**

```bash
git add main/ui/ui_screen_pomodoro.c
git commit -m "feat: add pomodoro start/pause sound effects"
```

---

### Task 6: 在 Buddy 模块添加状态音效

**Files:**
- Modify: `main/buddy/buddy.c`

- [ ] **Step 1: 添加 include 和音效调用**

在 `main/buddy/buddy.c` 顶部添加：

```c
#include "service/sound_service.h"
```

修改 `set_state` 函数，在 `s_cbs.on_state_changed` 调用之后添加音效：

```c
static void set_state(buddy_state_t new_state)
{
    if (s_state == new_state) return;

    ESP_LOGI(TAG, "state: %d -> %d", s_state, new_state);
    s_state      = new_state;
    s_frame_idx  = 0;
    s_tick_count = 0;

    update_led();

    if (s_cbs.on_state_changed) {
        s_cbs.on_state_changed(new_state);
    }

    // Sound effects for state changes
    if (new_state == BUDDY_ATTENTION) {
        sound_service_play(SOUND_BUDDY_ATTENTION);
    }
}
```

修改 `buddy_approve` 函数，在 `set_state(BUDDY_CELEBRATE)` 之后添加：

```c
void buddy_approve(void)
{
    if (s_state != BUDDY_ATTENTION) return;

    s_approved++;
    s_has_prompt   = false;
    s_prompt_id[0] = '\0';
    buddy_save_stats();
    ESP_LOGI(TAG, "approved  total=%lu", (unsigned long)s_approved);
    set_state(BUDDY_CELEBRATE);
    sound_service_play(SOUND_BUDDY_HAPPY);
}
```

修改 `buddy_deny` 函数，在 `set_state(BUDDY_IDLE)` 之后添加：

```c
void buddy_deny(void)
{
    if (s_state != BUDDY_ATTENTION) return;

    s_denied++;
    s_has_prompt   = false;
    s_prompt_id[0] = '\0';
    buddy_save_stats();
    ESP_LOGI(TAG, "denied  total=%lu", (unsigned long)s_denied);
    set_state(BUDDY_IDLE);
    sound_service_play(SOUND_BUDDY_SAD);
}
```

- [ ] **Step 2: 构建**

Run: `./build.sh`
Expected: 编译成功

- [ ] **Step 3: 提交**

```bash
git add main/buddy/buddy.c
git commit -m "feat: add buddy state change sound effects"
```

---

### Task 7: 在设置界面添加声音开关

**Files:**
- Modify: `main/ui/ui_screen_settings.c`

- [ ] **Step 1: 添加 include**

在 `main/ui/ui_screen_settings.c` 顶部添加：

```c
#include "service/sound_service.h"
```

- [ ] **Step 2: 修改设置项数量和名称数组**

将 `SETTINGS_ITEM_COUNT` 从 7 改为 8，并在 `settings_names` 数组末尾添加 "Sound"：

```c
#define SETTINGS_ITEM_COUNT 8
```

```c
static const char *settings_names[SETTINGS_ITEM_COUNT] = {
    "Brightness", "Contrast", "Language",
    "Timezone", "Pomodoro", "WiFi", "Direction", "Sound"
};
```

- [ ] **Step 3: 修改初始值数组**

将初始值数组从 7 个元素扩展到 8 个，新增 Sound 默认值 1（开启）：

```c
static int settings_values[SETTINGS_ITEM_COUNT] = {50, 50, 0, 8, 25, 0, 0, 1};
```

- [ ] **Step 4: 修改 update_display 中的 value 显示逻辑**

在 `update_display` 函数的 switch 中，将 case 5 (WiFi) 的 `>` 扩展为包含新的 case 7 (Sound)，同时为 Sound 显示 On/Off：

```c
        switch (i) {
            case 2:  // Language
                snprintf(item_values[i], sizeof(item_values[i]), "%s", lang_opts[settings_values[i] % 2]);
                break;
            case 3:  // Timezone
                snprintf(item_values[i], sizeof(item_values[i]), "UTC%+d", settings_values[i]);
                break;
            case 4:  // Pomodoro (sub-screen)
            case 5:  // WiFi (sub-screen)
                snprintf(item_values[i], sizeof(item_values[i]), ">");
                break;
            case 6:  // Direction
                snprintf(item_values[i], sizeof(item_values[i]), "%s", settings_values[i] ? "Rev" : "Normal");
                break;
            case 7:  // Sound
                snprintf(item_values[i], sizeof(item_values[i]), "%s", settings_values[i] ? "On" : "Off");
                break;
            default:
                snprintf(item_values[i], sizeof(item_values[i]), "%d", settings_values[i]);
                break;
        }
```

- [ ] **Step 5: 修改 settings_on_settings_press 中的 WiFi 跳转索引**

WiFi 项的索引从 5 改为不变（还是 5），但需要确认所有索引。当前索引：Brightness=0, Contrast=1, Language=2, Timezone=3, Pomodoro=4, WiFi=5, Direction=6, Sound=7。索引没变，无需修改。

- [ ] **Step 6: 在 create 函数中加载 Sound 设置值**

在 `ui_screen_settings_create` 函数中，已有的 NVS 加载代码之后添加：

```c
    // Load sound setting
    int32_t sound_val = 1;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, "sound_on", &sound_val);
    settings_values[7] = sound_val;
```

- [ ] **Step 7: 修改 adjust_up/down，支持 Sound 切换**

在 `ui_screen_settings_adjust_up` 的 switch 中，在 case 6 (Direction) 之前添加 case 7：

```c
            case 7:  // Sound
                settings_values[current_settings_item] = !settings_values[current_settings_item];
                sound_service_set_enabled(settings_values[current_settings_item]);
                break;
```

在 `ui_screen_settings_adjust_down` 中同样添加相同的 case 7。

- [ ] **Step 8: 构建**

Run: `./build.sh`
Expected: 编译成功

- [ ] **Step 9: 提交**

```bash
git add main/ui/ui_screen_settings.c
git commit -m "feat: add Sound on/off toggle in settings"
```

---

### Task 8: 在 input_handler 添加按键音效

**Files:**
- Modify: `main/input/input_handler.c`

- [ ] **Step 1: 添加 include 和按键音效**

在 `main/input/input_handler.c` 顶部添加：

```c
#include "service/sound_service.h"
```

在 `input_handler_task` 的 switch 中，为每个按键事件添加音效。注意：编码器旋转不播放音效（太频繁），只在按键按下时播放：

修改 `input_handler_task` 函数中的 case 处理，在 `ui_dispatch_encoder_press()` 调用前添加音效：

```c
                case INPUT_EVENT_ENCODER_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ui_dispatch_encoder_press();
                    break;
                case INPUT_EVENT_ENCODER_LONG_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ui_dispatch_encoder_long_press();
                    break;
                case INPUT_EVENT_SETTINGS_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ui_dispatch_settings_press();
                    break;
```

注意：`sound_service_play()` 在开关关闭时直接返回，开销极小，不会影响输入响应。

- [ ] **Step 2: 构建**

Run: `./build.sh`
Expected: 编译成功

- [ ] **Step 3: 提交**

```bash
git add main/input/input_handler.c
git commit -m "feat: add key click sound effects"
```

---

### Task 9: 集成测试 — 构建并烧录验证

**Files:** 无修改

- [ ] **Step 1: 完整构建**

Run: `./build.sh`
Expected: 编译成功，无 warning

- [ ] **Step 2: 烧录并验证**

Run: `./build.sh flash -p COM6`

手动测试清单：
1. 开机：蜂鸣器应静音（启动时无音效）
2. 旋转编码器：无声音（旋转不触发）
3. 按编码器：短促点击音
4. 按 SET 键：短促点击音
5. 进入设置 → 旋转选择 Sound → 按 SET 切换 → 应听到音效变化
6. 设置 Sound 为 Off → 按键无声音 → 设回 On → 有声音
7. WiFi 连接成功：轻快双音
8. WiFi 连接失败：两声低鸣
9. 番茄钟 SET 启动：四音上行
10. 番茄钟工作结束（可设 1 分钟测试）：三音下行
11. 番茄钟休息结束：三声提醒

- [ ] **Step 3: 提交所有修改（如有修复）**
