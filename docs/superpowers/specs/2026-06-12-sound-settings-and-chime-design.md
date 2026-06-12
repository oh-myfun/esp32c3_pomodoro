# 声音设置页面与整点报时设计

## 目标

把当前散落在系统设置里的单一总开关整理为独立的声音设置页面，按类别细分；新增整点报时与半点报时功能，并允许用户配置静默时段。

## 现状

- `sound_service` 内置 19 种音效，仅靠一个全局开关 `KEY_SOUND` 控制启停
- 总开关藏在 `ui_screen_settings_system.c` 第一项里
- 无报时功能
- 调用点遍布 `input_handler`、`main.c`、`buddy.c`、`time_service.c`、`ui_screen_pomodoro.c`

## 架构

```
+-----------------------------+        +---------------------------+
|  UI: Settings Sound Page    | <----> |  sound_service            |
|  - 总开关 + 7 分类开关      |        |  - 分类开关表 cat_enabled |
|  - 静默时段 start/end       |        |  - sound_service_play()   |
+-----------------------------+        |  - play_hour/half_chime() |
                                      +---------------------------+
                                                ^
                                                |
                                      +---------------------------+
                                      |  chime_tick() in UIUpdate |
                                      |  - 检测整点/半点边界      |
                                      |  - 查静默时段             |
                                      +---------------------------+
```

## 声音分类

7 个分类，每个分类对应一个 NVS 开关。总开关之外的第一道闸。

| 分类枚举 | 含义 | 包含的音效 | 默认 |
|---|---|---|---|
| `SND_CAT_KEY` | 按键反馈 | `SOUND_KEY_CLICK` | ON |
| `SND_CAT_UI` | 操作反馈 | `SOUND_CONFIRM`, `SOUND_CANCEL`, `SOUND_SUCCESS`, `SOUND_FAIL` | ON |
| `SND_CAT_NET` | 网络 | `SOUND_WIFI_CONNECT`, `SOUND_WIFI_CONNECTED`, `SOUND_WIFI_FAILED`, `SOUND_SYNC_START`, `SOUND_SYNC_DONE` | ON |
| `SND_CAT_POMO` | 番茄钟 | `SOUND_POMO_START`, `SOUND_POMO_WORK_START`, `SOUND_POMO_BREAK_START`, `SOUND_POMO_WORK_DONE`, `SOUND_POMO_BREAK_DONE`, `SOUND_POMO_LONG_BREAK` | ON |
| `SND_CAT_BUDDY` | 宠物 | `SOUND_BUDDY_ATTENTION`, `SOUND_BUDDY_HAPPY`, `SOUND_BUDDY_SAD` | ON |
| `SND_CAT_HOUR_CHIME` | 整点报时 | （参数化触发） | OFF |
| `SND_CAT_HALF_CHIME` | 半点报时 | （参数化触发） | OFF |

## sound_service 接口

### 现有 API（保留）
```c
void sound_service_init(void);
void sound_service_play(sound_id_t id);
bool sound_service_is_enabled(void);
void sound_service_set_enabled(bool enabled);
```

### 新增 API

```c
typedef enum {
    SND_CAT_KEY, SND_CAT_UI, SND_CAT_NET, SND_CAT_POMO,
    SND_CAT_BUDDY, SND_CAT_HOUR_CHIME, SND_CAT_HALF_CHIME,
    SND_CAT_COUNT
} sound_category_t;

/* 分类开关 */
bool sound_service_is_category_enabled(sound_category_t cat);
void sound_service_set_category_enabled(sound_category_t cat, bool on);

/* 报时（参数化触发器，内部走分类检查） */
void sound_service_play_hour_chime(int hour12);   /* hour12: 1..12 */
void sound_service_play_half_chime(void);
```

### 分类映射表

`sound_service.c` 内部静态表，每个 `sound_id_t` 映射到分类：

```c
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
```

### 修改后的 play 逻辑

```c
void sound_service_play(sound_id_t id) {
    if (!sound_enabled) return;                       /* 总开关 */
    if (id < 0 || id >= SOUND_COUNT) return;
    sound_category_t cat = cat_map[id];
    if (!cat_enabled[cat]) return;                    /* 分类开关 */
    buzzer_play_melody(melodies[id].notes, melodies[id].count);
}
```

### 报时实现

```c
/* 整点报时旋律：每组 3 响连续起伏，组间停顿 */
static const buzzer_note_t group3[] = {
    {NOTE_C6, 150}, {NOTE_E6, 150}, {NOTE_G6, 200},
};
static const buzzer_note_t group_rest[] = {
    {REST, 400},
};

void sound_service_play_hour_chime(int hour12) {
    if (!sound_enabled) return;
    if (!cat_enabled[SND_CAT_HOUR_CHIME]) return;
    if (hour12 < 1 || hour12 > 12) return;

    /* 动态拼装旋律 */
    buzzer_note_t buf[12 + 4];   /* 12 响 + 3 组间 rest + 末尾 */
    int n = 0;
    int full_groups = hour12 / 3;
    int remainder   = hour12 % 3;
    for (int g = 0; g < full_groups; g++) {
        if (g > 0) buf[n++] = group_rest[0];
        for (int i = 0; i < 3; i++) buf[n++] = group3[i];
    }
    if (remainder > 0) {
        if (full_groups > 0) buf[n++] = group_rest[0];
        for (int i = 0; i < remainder; i++) buf[n++] = group3[i];
    }
    buzzer_play_melody(buf, n);
}

/* 半点报时：单响低音，与整点不同音色 */
static const buzzer_note_t mel_half[] = {
    {NOTE_A5, 200},
};

void sound_service_play_half_chime(void) {
    if (!sound_enabled) return;
    if (!cat_enabled[SND_CAT_HALF_CHIME]) return;
    buzzer_play_melody(mel_half, 1);
}
```

报时函数和 `sound_service_play()` 都先经总开关，再经对应分类开关——分类机制对报时和普通音效完全一致。

### 初始化加载

```c
static const char *cat_keys[SND_CAT_COUNT] = {
    "snd_key", "snd_ui", "snd_net", "snd_pomo",
    "snd_buddy", "snd_hour", "snd_half",
};
static const bool cat_defaults[SND_CAT_COUNT] = {
    true, true, true, true, true, false, false,
};

void sound_service_init(void) {
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
}
```

## NVS 键

全部在 `STORAGE_NAMESPACE_SETTINGS`：

| Key | 类型 | 默认 | 用途 |
|---|---|---|---|
| `sound` (已有) | 0/1 | 1 | 总开关 |
| `snd_key` | 0/1 | 1 | 按键分类 |
| `snd_ui` | 0/1 | 1 | 操作反馈分类 |
| `snd_net` | 0/1 | 1 | 网络分类 |
| `snd_pomo` | 0/1 | 1 | 番茄钟分类 |
| `snd_buddy` | 0/1 | 1 | Buddy 分类 |
| `snd_hour` | 0/1 | 0 | 整点报时 |
| `snd_half` | 0/1 | 0 | 半点报时 |
| `quiet_start` | 0–23 | 22 | 静默起 |
| `quiet_end` | 0–23 | 7 | 静默止 |

旧 `sound` 键直接复用，无破坏性变更。

## 静默时段判断

```c
static int quiet_start = 22;
static int quiet_end   = 7;

bool sound_service_is_quiet_hour(int hour) {
    int s = quiet_start, e = quiet_end;
    if (s == e) return false;                  /* 起止相同 = 无静默 */
    return (s < e) ? (hour >= s && hour < e)
                   : (hour >= s || hour < e);  /* 跨午夜 */
}
```

加载/保存：
```c
void sound_service_set_quiet_range(int start, int end);
void sound_service_get_quiet_range(int *start, int *end);
```

## UI：声音设置页

### 屏幕 ID

新增 `UI_SCREEN_SETTINGS_SOUND`，加入 `ui_manager` 的 lazy creator / disposable 列表。

### 设置主页菜单（4 → 5 项）

```
💡 Light
🔊 Sound   (新)
📶 WiFi
🕐 Time
⚙ System
```

`ui_screen_settings.c` 的 `settings_name_ids[]` 在 Light 后插入 `STR_M_SOUND`，`navigate_to_subpage()` 加 case 1 → `UI_SCREEN_SETTINGS_SOUND`，后续 case 编号顺延。

### 系统设置页（4 → 3 项）

移除 Sound 项，剩 Direction / Language / Sleep。`system_values[]` 数组与对应 case 一并调整。

### 声音设置页布局（10 项）

| # | Key | Value | 操作 |
|---|---|---|---|
| 0 | 🔊 Sound | On/Off | 顶键切换 |
| 1 | 🔑 Key | On/Off | 顶键切换 |
| 2 | ✅ UI | On/Off | 顶键切换 |
| 3 | 📶 Net | On/Off | 顶键切换 |
| 4 | 🍅 Pomo | On/Off | 顶键切换 |
| 5 | 🐱 Buddy | On/Off | 顶键切换 |
| 6 | 🔔 Hour | On/Off | 顶键切换 |
| 7 | 🔎 Half | On/Off | 顶键切换 |
| 8 | 🌙 Quiet Start | 0–23 | 进入 ADJUST，编码器调 |
| 9 | 🌙 Quiet End | 0–23 | 进入 ADJUST，编码器调 |

沿用 `ui_screen_settings_system.c` 的 NAV/ADJUST 双模式：项 0–7 用 SET 键切换 On/Off（不需要 ADJUST 模式）；项 8、9 进入 ADJUST 后用编码器增减小时。

### 国际化新增

```c
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
STR_FMT_HOUR_VAL,       /* "%dh" / "%d时" */
```

## 报时触发

### chime_tick 函数

放在新文件 `main/service/chime_service.c/h`，由 UIUpdate 任务每秒调用一次。

```c
/* chime_service.c */
#include "chime_service.h"
#include "sound_service.h"
#include "time.h"
#include <sys/time.h>

static long last_minute_id = -1;

void chime_service_init(void) {
    /* 让首次评估只记录，不立即报时 */
    time_t now; time(&now);
    last_minute_id = (long)(now / 60);
}

void chime_service_tick(void) {
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);

    if (t.tm_min != 0 && t.tm_min != 30) return;

    long mid = (long)(now / 60);
    if (mid == last_minute_id) return;
    last_minute_id = mid;

    if (sound_service_is_quiet_hour(t.tm_hour)) return;

    if (t.tm_min == 0) {
        int h12 = t.tm_hour % 12;
        if (h12 == 0) h12 = 12;
        sound_service_play_hour_chime(h12);
    } else {
        sound_service_play_half_chime();
    }
}
```

### 关键设计点

- **不依赖 NTP 同步**：`localtime_r` 在时钟走动后即返回有效时间，无需 `tm_year > 70` 判断
- **绝对分钟 ID**：`time_t / 60` 全局唯一，跨天/跨年不冲突
- **去重**：UIUpdate 任务在 min==0 这 60 秒内可能跑很多次，但每次进入 `mid` 相同 → return，只响一次
- **漏秒容错**：不依赖 `tm_sec==0`，只要在 min==0 或 min==30 区间内任何一秒跑进来都能触发
- **跨错过**：UIUpdate 即使阻塞几分钟，下一小时 min==0 时 `mid` 仍是新值，正常触发

### 集成位置

`main.c` 的 UIUpdate 任务（100ms 循环）每秒调用：

```c
static int sec_counter = 0;
sec_counter++;
if (sec_counter >= 10) {
    sec_counter = 0;
    chime_service_tick();
}
```

`app_main` 初始化序列加 `chime_service_init()`。

## 文件清单

| 文件 | 操作 |
|---|---|
| `main/service/sound_service.h` | 修改：加分类枚举、新 API |
| `main/service/sound_service.c` | 修改：cat_map、cat_enabled、play_hour/half_chime、quiet range |
| `main/service/chime_service.h` | 新建 |
| `main/service/chime_service.c` | 新建 |
| `main/service/storage_service.h` | 修改：加新 NVS key 定义 |
| `main/ui/ui_screen_settings_sound.h` | 新建 |
| `main/ui/ui_screen_settings_sound.c` | 新建 |
| `main/ui/ui_manager.h` | 修改：加 `UI_SCREEN_SETTINGS_SOUND` |
| `main/ui/ui_manager.c` | 修改：注册 lazy creator、disposable |
| `main/ui/ui_screen_settings.c` | 修改：5 项菜单，加 Sound 入口 |
| `main/ui/ui_screen_settings_system.c` | 修改：4→3 项，移除 Sound |
| `main/ui/i18n.h/c` | 修改：加新字符串 |
| `main/main.c` | 修改：初始化 chime_service，UIUpdate 集成 |
| `main/CMakeLists.txt` | 修改：加 chime_service.c、ui_screen_settings_sound.c |

## 兼容性

- 旧 NVS `sound` 键直接复用
- 新键不存在时按默认值加载，旧固件用户升级后行为不变（除报时默认 OFF）
- 现有所有 `sound_service_play(SOUND_xxx)` 调用点零修改（分类透明）

## 验证清单

1. 编译通过
2. 设置主页 5 项，Sound 项可进入
3. 声音设置页 10 项可显示、可调
4. 总开关 OFF 时所有声音静音；ON 时按各分类开关决定
5. 各分类开关独立生效（关闭 Pomo 不影响 Buddy 等）
6. 整点报时按当前小时数鸣响（1 点 1 响、3 点 3 响成组、6 点 2 组、12 点 4 组）
7. 半点报时为单响低音，与整点音色不同
8. 静默时段内不响（包含跨午夜场景 22→7）
9. UIUpdate 任务在 sec=0 错过后，sec=20 进入仍能响一次
10. 跨小时同分钟不会重复响
