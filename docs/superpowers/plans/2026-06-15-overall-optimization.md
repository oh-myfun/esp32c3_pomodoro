# Pomodoro 固件整体优化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变任何用户可见功能的前提下，对 ESP32-C3 番茄钟固件进行整体优化 — 清理 dead code、提取重复样板、统一视觉、降低 NVS/渲染开销。

**Architecture:** 分 6 个独立 Phase，每个 Phase 都以"构建通过 + 硬件冒烟测试 + commit"结束。Phase 顺序按"风险递增"排列：视觉统一 → dead code 清理 → UI helper 提取 → 业务层去重 → 性能优化 → 设置子屏幕通用框架（最大重构）。

**Tech Stack:** ESP-IDF v5.5.4 + LVGL v9.5.0 + FreeRTOS。所有"测试"环节为 `./build.sh` 构建 + 在硬件上手动验证关键功能（嵌入式项目无 host 单元测试）。

**TDD 适配说明:** 嵌入式项目无传统单元测试。每个任务的"测试"= (1) `./build.sh` 编译通过；(2) `./build.sh flash` 烧录后手动验证指定场景；(3) commit。修改 LVGL/业务逻辑前先在硬件上验证当前行为，作为对照基线。

---

## Scope Check

本计划范围大（~6 个 Phase，~25 个 task）。每个 Phase 独立可执行、可验证、可独立 commit/push。如果时间紧迫，**Phase 1-2 是 ROI 最高的安全优化**（无风险、立竿见影）；**Phase 6 是最大重构**（涉及 7 个设置子屏幕），可在其他 Phase 完成后单独执行。

---

## File Structure

```
main/
├── ui/
│   ├── ui_theme.h            # 【新建】颜色/字体/间距常量集中定义
│   ├── ui_helpers.h          # 【新建】ui_create_screen / title / hint 等公共构造器
│   ├── ui_helpers.c          # 【新建】同上实现
│   ├── ui_setting_editor.h   # 【新建 Phase 6】通用 NAV/ADJUST 编辑器
│   ├── ui_setting_editor.c   # 【新建 Phase 6】同上实现
│   └── （现有 7 个 ui_screen_settings_*.c 在 Phase 6 中改为数据驱动）
├── service/
│   ├── ble_service.c/h       # 【Phase 2 删除】整体 dead code
│   └── （现有其他文件做局部修改）
└── （driver/ buddy/ pomodoro/ input/ 做局部去重和优化）
```

**关键决策：** 颜色/字体/间距等"设计令牌"集中在 `ui_theme.h`；公共 UI 构造逻辑集中在 `ui_helpers.c`；设置子屏幕通用框架集中在 `ui_setting_editor.c`。三个文件职责清晰、互不依赖。

---

# Phase 1: 视觉统一与颜色常量提取

**目标：** 提取散落各处的颜色字面量到 `ui_theme.h`，统一背景色、标题/提示位置、圆角值。零行为变化。

### Task 1.1: 创建 `ui_theme.h` 集中颜色与设计令牌

**Files:**
- Create: `main/ui/ui_theme.h`

- [ ] **Step 1: 创建主题头文件**

```c
#pragma once

#include "lvgl.h"

/* ===== 背景色 ===== */
#define UI_COLOR_BG          lv_color_hex(0x000000)  /* 统一屏幕背景 */
#define UI_COLOR_CARD_BG     lv_color_hex(0x0a0a0a)  /* 卡片/图表背景 */
#define UI_COLOR_ARC_BG      lv_color_hex(0x333333)  /* 弧形底色 */

/* ===== 文本色 ===== */
#define UI_COLOR_TEXT        lv_color_hex(0xFFFFFF)  /* 主文本 */
#define UI_COLOR_TEXT_DIM    lv_color_hex(0xAAAAAA)  /* 次要文本（日期等） */
#define UI_COLOR_TEXT_HINT   lv_color_hex(0x888888)  /* 底部提示 */
#define UI_COLOR_TEXT_SUB    lv_color_hex(0xCCCCCC)  /* 伙伴次级文本 */
#define UI_COLOR_TEXT_NAV    lv_color_hex(0x666666)  /* 导航提示 */
#define UI_COLOR_DISABLED    lv_color_hex(0x444444)  /* 断连/禁用 */
#define UI_COLOR_LIST_ALT    lv_color_hex(0x555555)  /* 列表次级 */

/* ===== 强调色 ===== */
#define UI_COLOR_ACCENT      lv_color_hex(0xFFFF00)  /* 调整模式 / 选中态 */
#define UI_COLOR_SUCCESS     lv_color_hex(0x00FF00)  /* 选中确认 / 工作完成 */
#define UI_COLOR_WARN        lv_color_hex(0xFFAA00)  /* 警告 */
#define UI_COLOR_DANGER      lv_color_hex(0xFF4400)  /* 错误 */

/* ===== 番茄钟阶段色 ===== */
#define UI_COLOR_POMO_WORK     lv_color_hex(0xFF4400)
#define UI_COLOR_POMO_BREAK    lv_color_hex(0x00FF00)
#define UI_COLOR_POMO_LONG     lv_color_hex(0x00AAFF)
#define UI_COLOR_POMO_IDLE     lv_color_hex(0xAAAAAA)
#define UI_COLOR_POMO_PAUSED   lv_color_hex(0x888888)

/* ===== 排版 ===== */
#define UI_TITLE_Y_OFFSET      6     /* 标题统一 Y 偏移 */
#define UI_RADIUS_CARD         4     /* 卡片圆角 */
#define UI_RADIUS_PILL         8     /* 胶囊圆角 */
#define UI_HINT_BOTTOM_OFFSET  (-8)  /* 底部提示距底距离（保留原 ui_manager.h 定义） */
```

- [ ] **Step 2: 验证构建通过**

Run: `./build.sh`
Expected: 编译成功（文件被包含时才生效，目前无人引用，无副作用）

- [ ] **Step 3: Commit**

```bash
git add main/ui/ui_theme.h
git commit -m "feat(ui): 新增 ui_theme.h 集中颜色与设计令牌"
```

---

### Task 1.2: 统一所有屏幕背景色为纯黑 `0x000000`

**Files:**
- Modify: `main/ui/ui_screen_settings.c:119-121`
- Modify: `main/ui/ui_screen_wifi.c:87-90`
- Modify: `main/ui/ui_screen_buddy.c:472-474`
- Modify: `main/ui/ui_screen_pomodoro.c:317-319`
- Modify: `main/ui/ui_screen_wifi_saved.c:219-221`
- Modify: `main/ui/ui_screen_settings_alarm.c:117-119`
- Modify: `main/ui/ui_screen_settings_pomodoro.c:197-199`
- Modify: `main/ui/ui_screen_settings_time.c:151-153`
- Modify: `main/ui/ui_screen_settings_system.c:153-155`
- Modify: `main/ui/ui_screen_settings_light.c:204-206`
- Modify: `main/ui/ui_screen_settings_buddy.c:229-231`
- Modify: `main/ui/ui_screen_sensor.c:307-309`

- [ ] **Step 1: 阅读现状**

```bash
grep -rn "0x1a1a1a" main/ui/
```
预期：14+ 处出现在屏幕背景设置上。

- [ ] **Step 2: 逐文件替换背景色**

对每个上述文件，将形如：
```c
lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
```
改为：
```c
#include "ui_theme.h"
lv_obj_set_style_bg_color(screen, UI_COLOR_BG, 0);
```

注意：`ui_screen_sensor.c:339` 的 `0x0a0a0a` 是图表背景，保留为 `UI_COLOR_CARD_BG`，不要改成 `UI_COLOR_BG`。

- [ ] **Step 3: 同时替换 `0x000000` 字面量为常量**

`ui_screen_main.c:36`、`ui_screen_sensor.c:308` 中已用 `0x000000`，改为 `UI_COLOR_BG`。

- [ ] **Step 4: 验证构建通过**

Run: `./build.sh`
Expected: 编译成功，无 warning。

- [ ] **Step 5: 烧录硬件验证**

Run: `./build.sh flash`
手动验证：依次切到主界面、设置、WiFi 列表、番茄钟、伙伴、传感器界面，背景均为纯黑，无视觉差异。

- [ ] **Step 6: Commit**

```bash
git add main/ui/
git commit -m "refactor(ui): 统一所有屏幕背景为 UI_COLOR_BG"
```

---

### Task 1.3: 替换散落的颜色字面量为主题常量

**Files:**
- Modify: 全部 `main/ui/ui_screen_*.c`

- [ ] **Step 1: 替换文本相关颜色**

| 旧值 | 新值 | 出现位置 |
|------|------|----------|
| `0xFFFFFF` | `UI_COLOR_TEXT` | 各屏幕标题/数字 |
| `0xAAAAAA` | `UI_COLOR_TEXT_DIM` | `ui_screen_main.c:47` 日期、`ui_screen_pomodoro.c:175` 默认阶段色 |
| `0x888888` | `UI_COLOR_TEXT_HINT` | 各屏幕底部提示（13 处） |
| `0xCCCCCC` | `UI_COLOR_TEXT_SUB` | `ui_screen_buddy.c:448,509,582,668` |
| `0x666666` | `UI_COLOR_TEXT_NAV` | `ui_screen_buddy.c:441` |
| `0x555555` | `UI_COLOR_LIST_ALT` | `ui_list.c:223`、`ui_screen_buddy.c:593` |
| `0x444444` | `UI_COLOR_DISABLED` | `ui_screen_buddy.c:478,668,824` |

- [ ] **Step 2: 替换强调色**

| 旧值 | 新值 | 出现位置 |
|------|------|----------|
| `0xFFFF00` | `UI_COLOR_ACCENT` | ~10 处调整模式 |
| `0x00FF00` | `UI_COLOR_SUCCESS` | ~15 处选中态 |
| `0xFF4400` | `UI_COLOR_DANGER` | 番茄钟工作色 |
| `0x333333` | `UI_COLOR_ARC_BG` | `ui_screen_pomodoro.c:345` |
| `0x0a0a0a` | `UI_COLOR_CARD_BG` | `ui_screen_sensor.c:339` |

- [ ] **Step 3: 替换番茄钟阶段色**

`ui_screen_pomodoro.c:170-176` 与 `:391-417` 的 switch 语句，将硬编码阶段颜色替换为 `UI_COLOR_POMO_*` 宏。

- [ ] **Step 4: 验证构建通过**

Run: `./build.sh`
Expected: 编译成功。

- [ ] **Step 5: 烧录硬件冒烟测试**

Run: `./build.sh flash`
验证：启动 → 主界面（时间白色、日期灰色） → 设置（标题白色、提示灰色、选中黄色）→ 番茄钟（工作红、休息绿）→ 伙伴（次级文本灰）→ 传感器（4 象限数字白色、单位灰、图表暗灰）。颜色与之前视觉一致。

- [ ] **Step 6: Commit**

```bash
git add main/ui/
git commit -m "refactor(ui): 替换散落颜色字面量为主题常量"
```

---

### Task 1.4: 统一标题位置与圆角值

**Files:**
- Modify: `main/ui/ui_screen_main.c:56`（标题 y=10 → `UI_TITLE_Y_OFFSET`）
- Modify: `main/ui/ui_screen_wifi.c:96`（y=10 → 6）
- Modify: `main/ui/ui_screen_buddy.c:527`（radius=6 → `UI_RADIUS_CARD`）
- Modify: `main/ui/ui_screen_buddy.c:595`（radius=1 → `UI_RADIUS_CARD`）
- Modify: `main/ui/ui_list.c:225`（radius=1 → `UI_RADIUS_CARD`）

- [ ] **Step 1: 标题 Y 偏移统一为 `UI_TITLE_Y_OFFSET`**

对所有 `lv_obj_align(..., LV_ALIGN_TOP_MID, 0, N)` 形式且 N 是 6/8/10 中之一的标题标签，改为 `UI_TITLE_Y_OFFSET`。

- [ ] **Step 2: 圆角统一**

将分散的 `lv_obj_set_style_radius(obj, 1, 0)` 和 `radius=6` 改为 `UI_RADIUS_CARD`（值=4）。

注意：`ui_screen_buddy.c:595` 的 radius=1 是进度条，圆角过大会变形，**保留为 1 或单独定义 `UI_RADIUS_THIN = 1`**。审查每处半径用途后再改。

- [ ] **Step 3: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：标题/圆角视觉与之前**几乎一致**（6→4 的差异肉眼难察觉，10→6 在主屏标题略微下移 4px，可接受）。

- [ ] **Step 4: Commit**

```bash
git add main/ui/
git commit -m "style(ui): 统一标题 Y 偏移与圆角到 UI_TITLE_Y_OFFSET/UI_RADIUS_CARD"
```

---

# Phase 2: Dead Code 清理

**目标：** 删除从未被调用的导出函数、未引用的字段、整块禁用模块。预计减少 ~700 行代码 + 显著降低 flash 占用。

### Task 2.1: 删除 BLE 整套（最大单块 dead code）

**Files:**
- Delete: `main/service/ble_service.c`（608 行）
- Delete: `main/service/ble_service.h`
- Modify: `main/CMakeLists.txt`（确认无引用 — 当前未列出 ble_service.c）
- Modify: `main/main.c`（移除注释 `// ble_service_tick();` 及任何残留 include）

- [ ] **Step 1: 确认 BLE 在 CMakeLists 中已未编译**

Read: `main/CMakeLists.txt`
预期：`ble_service.c` **不在** SRCS 列表中（CLAUDE.md 已说明 BLE 禁用）。

- [ ] **Step 2: 确认 main.c 中无 `ble_service.h` 引用**

Run: `grep -n "ble_service" main/main.c`
预期：仅注释 `// ble_service_tick();` 一处。

- [ ] **Step 3: 删除 BLE 源文件**

```bash
rm main/service/ble_service.c main/service/ble_service.h
```

- [ ] **Step 4: 移除 main.c 注释**

修改 `main/main.c`：
```c
// ble_service_tick();
```
整行删除（按 grep 结果定位行号）。

- [ ] **Step 5: 检查 sdkconfig 是否有残留 CONFIG_BT 项**

Run: `grep -n "CONFIG_BT" sdkconfig`
预期：已注释（CLAUDE.md 说明）。若仍存在未注释项，保持不变（不在本任务范围）。

- [ ] **Step 6: 验证构建**

Run: `./build.sh`
Expected: 编译成功。

- [ ] **Step 7: 烧录硬件冒烟**

Run: `./build.sh flash`
验证：设备启动正常，主界面/番茄钟/伙伴/WiFi/设置全部可用。

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "chore: 删除已禁用的 BLE 模块（608 行 dead code）"
```

---

### Task 2.2: 删除 buzzer 未使用的导出 API

**Files:**
- Modify: `main/driver/buzzer.h`
- Modify: `main/driver/buzzer.c`

- [ ] **Step 1: 确认以下 5 个 API 在 main/ 任何非 buzzer.c 文件中无调用**

```bash
grep -rn "buzzer_beep\|buzzer_set_volume\|buzzer_set_frequency\|buzzer_on\|buzzer_off" main/ --include="*.c" --include="*.h" | grep -v "buzzer.c\|buzzer.h"
```
预期：仅 `buzzer.c` 内部 melody 引擎调用 `buzzer_on/off/set_frequency`；`buzzer_beep`、`buzzer_set_volume` 完全无外部调用。

- [ ] **Step 2: 保留内部使用的低层 API**

`buzzer.c` 内部 melody 引擎使用 `buzzer_on/off/set_frequency` — 将这些函数从 `buzzer.h` 中移除声明，并在 `buzzer.c` 中改为 `static`。

- [ ] **Step 3: 删除完全无用的 `buzzer_beep`、`buzzer_set_volume`、`current_volume` 字段**

`buzzer.c:23` 的 `static int current_volume = 50;` 删除。
`buzzer.c:63` 的 `buzzer_set_volume` 整个函数删除。
`buzzer.c:102` 的 `buzzer_beep` 整个函数删除。

- [ ] **Step 4: 同步 buzzer.h**

删除对应的函数声明。

- [ ] **Step 5: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：触发蜂鸣（如番茄钟完成、整点报时），声音正常。

- [ ] **Step 6: Commit**

```bash
git add main/driver/buzzer.c main/driver/buzzer.h
git commit -m "chore(buzzer): 删除未使用的 buzzer_beep/set_volume 导出"
```

---

### Task 2.3: 删除 buddy 未使用的 getter

**Files:**
- Modify: `main/buddy/buddy.h`
- Modify: `main/buddy/buddy.c`

- [ ] **Step 1: 确认未使用**

```bash
grep -rn "buddy_get_tick_count\|buddy_include_rules\|should_include_rules\|buddy_is_answer_multi" main/ --include="*.c" --include="*.h" | grep -v "buddy.c\|buddy.h"
```
预期：无外部调用。

- [ ] **Step 2: 删除定义与声明**

`buddy.c:449` `buddy_is_answer_multi`、`:454-464` `buddy_include_rules/should_include_rules`、`:556` `buddy_get_tick_count` 删除。
同步删除 `buddy.h` 中对应声明。

- [ ] **Step 3: 验证构建**

Run: `./build.sh`
Expected: 编译成功。

- [ ] **Step 4: Commit**

```bash
git add main/buddy/buddy.c main/buddy/buddy.h
git commit -m "chore(buddy): 删除未引用的 getter 函数"
```

---

### Task 2.4: 删除 wifi 未使用的回调与字段

**Files:**
- Modify: `main/service/wifi_service.h`
- Modify: `main/service/wifi_service.c`
- Modify: `main/main.c:646-649`（移除 `on_connect_failed` 注册）

- [ ] **Step 1: 确认 `on_connect_failed` 从未触发**

Read: `main/service/wifi_service.c:72-77`
预期：`invoke_on_connect_failed` 函数定义存在但 service 内部从未调用。

- [ ] **Step 2: 删除 `invoke_on_connect_failed` 与回调字段**

`wifi_service.c:72-77` 删除。
`wifi_service.h` 的 `wifi_callbacks_t` 中 `on_connect_failed` 字段删除。

- [ ] **Step 3: main.c 中移除该回调的初始化**

`main.c:646-649` 的 `.on_connect_failed = ...` 行删除（注意保留其他字段）。

- [ ] **Step 4: 删除未使用的 `sta_netif` 静态字段**

`wifi_service.c:28` 的 `static wifi_netif_obj_t *sta_netif` 仅在 `:294` 赋值后再无读取 — 删除字段与其赋值（保留必要的 netif 创建调用本身）。

注意：仔细审查 `:294`，如果该行包含 `esp_wifi_init` 等必需初始化，仅删除字段存储而非整个调用。

- [ ] **Step 5: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：WiFi 扫描列表正常，能连接已保存网络，NTP 同步生效。

- [ ] **Step 6: Commit**

```bash
git add main/service/wifi_service.c main/service/wifi_service.h main/main.c
git commit -m "chore(wifi): 删除未触发的 on_connect_failed 回调和未读取字段"
```

---

### Task 2.5: 删除未使用的 UI refresh 函数

**Files:**
- Modify: `main/ui/ui_screen_settings_alarm.c:158-161`
- Modify: `main/ui/ui_screen_settings_system.c:201-215`
- Modify: `main/ui/ui_screen_settings_buddy.c:268-277`
- Modify: 对应的 `.h` 文件

- [ ] **Step 1: 确认未在 ui_manager 中注册**

Run: `grep -n "settings_alarm_refresh\|settings_system_refresh\|settings_buddy_refresh" main/ui/ui_manager.c`
预期：无匹配（仅 WIFI_SAVED/SETTINGS/POMODORO 调用 refresh）。

- [ ] **Step 2: 删除函数定义和声明**

3 个 `*_refresh` 函数从 `.c` 和 `.h` 中删除。

- [ ] **Step 3: 验证构建**

Run: `./build.sh`

- [ ] **Step 4: Commit**

```bash
git add main/ui/
git commit -m "chore(ui): 删除未注册到 ui_manager 的 refresh 函数"
```

---

### Task 2.6: 删除其他小型 dead code

**Files:**
- Modify: `main/main.c:122-144`（删除 `lvgl_deinit` 函数）
- Modify: `main/ui/ui_screen_buddy.c:26`（删除未读取的 `screen` 静态变量）
- Modify: `main/ui/ui_screen_buddy.c:65`（删除 `tcp_connected` 字段及对应 setter `ui_screen_buddy_set_connected`，如果完全无外部调用）
- Modify: `main/ui/ui_screen_pomodoro.c:50` 和 `main/ui/ui_screen_settings_alarm.c:13`（去重 `KEY_TIMER_TOTAL` 宏，统一在一处定义）
- Modify: `main/ui/ui_screen_wifi.c:90`（删除无效的 `lv_obj_set_pos(screen, 0, 0)`）
- Modify: `main/ui/ui_screen_wifi.c:59-60`（删除 `wifi_count`/`wifi_results` 冗余存储，如果 ui_list 已持有数据）

- [ ] **Step 1: 逐项确认无引用**

```bash
grep -rn "lvgl_deinit" main/
grep -rn "ui_screen_buddy_set_connected" main/ --include="*.c" --include="*.h"
```

- [ ] **Step 2: 删除 lvgl_deinit 函数**

`main.c:122-144` 整个函数删除（保留 `lvgl_init`）。

- [ ] **Step 3: 删除 buddy screen 中未使用的 static 字段**

`ui_screen_buddy.c:26` 的 `static lv_obj_t *screen;` 删除（确认创建函数直接 return 局部变量）。
`ui_screen_buddy.c:65` 的 `tcp_connected` 删除（连同 `ui_screen_buddy_set_connected` 函数和 `.h` 中声明，**前提是无外部调用**）。

- [ ] **Step 4: 统一 KEY_TIMER_TOTAL 宏**

在 `ui_screen_pomodoro.c:50` 保留 `#define KEY_TIMER_TOTAL "timer_total"`，从 `ui_screen_settings_alarm.c:13` 删除重复定义；如果 alarm 文件需要，包含 pomodoro 的头文件或重新组织到一个共享 `storage_keys.h`。

**推荐做法：** 新建 `main/service/storage_keys.h` 集中所有 NVS key 字符串，两个文件都 include 它。

- [ ] **Step 5: 删除 wifi 屏幕无效调用**

`ui_screen_wifi.c:90` 的 `lv_obj_set_pos(screen, 0, 0)` 删除（screen 是根对象，pos 无意义）。

`ui_screen_wifi.c:59-60` 的 `wifi_count` / `wifi_results` 静态变量 — 如果 `ui_list_set_items` 已经持有数据快照，删除冗余存储。**先确认 `wifi_list_item_click` 不依赖这两个静态变量**。

- [ ] **Step 6: 验证构建 + 硬件冒烟**

Run: `./build.sh && ./build.sh flash`
验证：启动 → WiFi 列表能加载 → 点击列表项进入密码输入 → 完成番茄钟一周期能记录 timer_total → 设置闹钟正常。

- [ ] **Step 7: Commit**

```bash
git add main/
git commit -m "chore: 删除杂项 dead code（lvgl_deinit/未用字段/重复宏/无效调用）"
```

---

# Phase 3: UI 公共组件提取

**目标：** 提取 13 个屏幕中重复的样板代码（屏幕创建、标题、提示、回调注册）到 `ui_helpers.c`。预计减少 ~300 行重复代码。

### Task 3.1: 创建 `ui_helpers.h` 与 `ui_helpers.c`

**Files:**
- Create: `main/ui/ui_helpers.h`
- Create: `main/ui/ui_helpers.c`
- Modify: `main/CMakeLists.txt`（添加 `ui/ui_helpers.c`）

- [ ] **Step 1: 编写 `ui_helpers.h`**

```c
#pragma once

#include "lvgl.h"
#include "ui_manager.h"

/* 创建标准屏幕：纯黑背景 + 240x240 尺寸 */
lv_obj_t *ui_create_screen(void);

/* 在屏幕顶部创建标题标签（白字、custom_font_16、TOP_MID 偏移 UI_TITLE_Y_OFFSET） */
lv_obj_t *ui_create_title_label(lv_obj_t *parent, const char *text);

/* 在屏幕底部创建提示标签（灰字、custom_font_14、BOTTOM_MID 偏移 UI_HINT_BOTTOM_OFFSET） */
lv_obj_t *ui_create_hint_label(lv_obj_t *parent, const char *text);

/* 创建并注册 input callbacks（NULL 字段会被自动忽略，注册后返回 screen_id 用于链式调用） */
void ui_register_callbacks(ui_screen_id_t screen,
                           void (*on_cw)(void),
                           void (*on_ccw)(void),
                           void (*on_press)(void),
                           void (*on_long_press)(void),
                           void (*on_settings_press)(void),
                           void (*on_settings_long_press)(void),
                           const char *(*on_long_press_hint)(bool));
```

- [ ] **Step 2: 编写 `ui_helpers.c`**

```c
#include "ui_helpers.h"
#include "ui_theme.h"
#include "custom_font.h"

lv_obj_t *ui_create_screen(void) {
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, UI_COLOR_BG, 0);
    lv_obj_set_size(s, 240, 240);
    return s;
}

lv_obj_t *ui_create_title_label(lv_obj_t *parent, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, custom_font_16, 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, UI_TITLE_Y_OFFSET);
    return lbl;
}

lv_obj_t *ui_create_hint_label(lv_obj_t *parent, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_HINT, 0);
    lv_obj_set_style_text_font(lbl, custom_font_14, 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, UI_HINT_BOTTOM_OFFSET);
    return lbl;
}

void ui_register_callbacks(ui_screen_id_t screen,
                           void (*on_cw)(void),
                           void (*on_ccw)(void),
                           void (*on_press)(void),
                           void (*on_long_press)(void),
                           void (*on_settings_press)(void),
                           void (*on_settings_long_press)(void),
                           const char *(*on_long_press_hint)(bool)) {
    ui_input_callbacks_t cbs = {
        .on_encoder_cw = on_cw,
        .on_encoder_ccw = on_ccw,
        .on_encoder_press = on_press,
        .on_encoder_long_press = on_long_press,
        .on_settings_press = on_settings_press,
        .on_settings_long_press = on_settings_long_press,
        .on_long_press_hint = on_long_press_hint,
    };
    ui_register_input_callbacks(screen, &cbs);
}
```

- [ ] **Step 3: 添加到 CMakeLists**

修改 `main/CMakeLists.txt:1`，在 SRCS 列表的 ui 部分加入 `ui/ui_helpers.c`（建议放在 `ui/ui_manager.c` 之后）。

- [ ] **Step 4: 验证构建**

Run: `./build.sh`
Expected: 编译成功。

- [ ] **Step 5: Commit**

```bash
git add main/ui/ui_helpers.h main/ui/ui_helpers.c main/CMakeLists.txt
git commit -m "feat(ui): 新增 ui_helpers 公共构造器（屏幕/标题/提示/回调）"
```

---

### Task 3.2: 迁移所有屏幕到 ui_helpers（第一批：设置子屏幕）

**Files:**
- Modify: `main/ui/ui_screen_settings_alarm.c`、`ui_screen_settings_pomodoro.c`、`ui_screen_settings_time.c`、`ui_screen_settings_system.c`、`ui_screen_settings_light.c`、`ui_screen_settings_buddy.c`、`ui_screen_wifi_saved.c`

- [ ] **Step 1: 每个 settings 子屏幕的 create 函数中，替换前 3 行**

将：
```c
lv_obj_t *screen = lv_obj_create(NULL);
lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
lv_obj_set_size(screen, 240, 240);
```
改为：
```c
lv_obj_t *screen = ui_create_screen();
```

- [ ] **Step 2: 替换标题标签块（5 行 → 1 行）**

将：
```c
lv_obj_t *title = lv_label_create(screen);
lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
lv_obj_set_style_text_font(title, custom_font_16, 0);
lv_label_set_text(title, "...");
lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
```
改为：
```c
lv_obj_t *title = ui_create_title_label(screen, "...");
```

- [ ] **Step 3: 替换底部提示块（4 行 → 1 行）**

将：
```c
hint_label = lv_label_create(screen);
lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
lv_obj_set_style_text_font(hint_label, custom_font_14, 0);
lv_label_set_text(hint_label, "...");
lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, UI_HINT_BOTTOM_OFFSET);
```
改为：
```c
hint_label = ui_create_hint_label(screen, "...");
```

注意：`hint_label` 通常需要保存为 file-scope static（因为 update_display 会改它），保留 static 赋值即可。

- [ ] **Step 4: 替换回调注册块**

将：
```c
static const ui_input_callbacks_t cbs = {
    .on_encoder_cw = xxx_on_cw,
    .on_encoder_ccw = xxx_on_ccw,
    .on_encoder_press = xxx_on_press,
    .on_settings_press = xxx_on_settings_press,
    /* NULL for long_press / settings_long_press / hint */
};
ui_register_input_callbacks(UI_SCREEN_XXX, &cbs);
```
改为：
```c
ui_register_callbacks(UI_SCREEN_XXX,
                      xxx_on_cw, xxx_on_ccw, xxx_on_press,
                      NULL, xxx_on_settings_press, NULL, NULL);
```

- [ ] **Step 5: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：进入设置 → 各子项能进入、能 NAV、能 ADJUST、能返回；底部提示文本随模式切换。

- [ ] **Step 6: Commit**

```bash
git add main/ui/
git commit -m "refactor(ui): 7 个设置子屏幕迁移到 ui_helpers 公共构造器"
```

---

### Task 3.3: 迁移所有屏幕到 ui_helpers（第二批：主屏幕）

**Files:**
- Modify: `main/ui/ui_screen_main.c`、`ui_screen_pomodoro.c`、`ui_screen_buddy.c`、`ui_screen_wifi.c`、`ui_screen_sensor.c`

- [ ] **Step 1: 同 Task 3.2 的 Step 1-4 流程**

主屏 `ui_screen_main.c` 没有标题，仅替换背景创建和 hint 标签。
`ui_screen_pomodoro.c`、`ui_screen_buddy.c`、`ui_screen_wifi.c`、`ui_screen_sensor.c` 完整迁移。

注意 `ui_screen_buddy.c:26` 的 `static lv_obj_t *screen;` 已在 Phase 2 删除，此处仅做局部替换。

- [ ] **Step 2: 验证构建 + 完整硬件冒烟**

Run: `./build.sh && ./build.sh flash`
完整功能冒烟：主屏时间显示 → 旋钮切换到番茄钟 → 启动工作 → 倒计时 → 完成 → 切伙伴 → 切传感器 → 进设置 → 切 WiFi → 连接 → 返回主屏。

- [ ] **Step 3: Commit**

```bash
git add main/ui/
git commit -m "refactor(ui): 5 个主屏幕迁移到 ui_helpers 公共构造器"
```

---

### Task 3.4: 提取 ui_screen_sensor 四象限容器样板

**Files:**
- Modify: `main/ui/ui_screen_sensor.c:398-467`

- [ ] **Step 1: 阅读现状**

`ui_screen_sensor.c:398-467` — 4 个 `rt_containers[0..3]` 每个 14 行代码完全重复（容器 + 值标签 + 单位标签）。

- [ ] **Step 2: 在 ui_helpers 中新增 `ui_create_quadrant`**

```c
// ui_helpers.h
typedef struct {
    lv_obj_t *container;
    lv_obj_t *value;
    lv_obj_t *unit;
} ui_quadrant_t;

ui_quadrant_t ui_create_quadrant(lv_obj_t *parent, const char *unit_text,
                                  int x, int y, int w, int h);
```

```c
// ui_helpers.c
ui_quadrant_t ui_create_quadrant(lv_obj_t *parent, const char *unit_text,
                                  int x, int y, int w, int h) {
    ui_quadrant_t q;
    q.container = lv_obj_create(parent);
    lv_obj_set_size(q.container, w, h);
    lv_obj_align(q.container, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(q.container, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_radius(q.container, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_width(q.container, 0, 0);
    lv_obj_set_style_pad_all(q.container, 4, 0);

    q.value = lv_label_create(q.container);
    lv_obj_set_style_text_font(q.value, custom_font_16, 0);
    lv_obj_set_style_text_color(q.value, UI_COLOR_TEXT, 0);
    lv_label_set_text(q.value, "--");
    lv_obj_center(q.value);

    q.unit = lv_label_create(q.container);
    lv_obj_set_style_text_font(q.unit, custom_font_14, 0);
    lv_obj_set_style_text_color(q.unit, UI_COLOR_TEXT_DIM, 0);
    lv_label_set_text(q.unit, unit_text);
    lv_obj_align(q.unit, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    return q;
}
```

注意：以上代码是模板，**实施前先 Read 现有 4 个象限代码**确认实际的样式属性（颜色/字体/对齐/内边距）以避免行为变化。

- [ ] **Step 3: 用循环替换 4 个重复块**

`ui_screen_sensor.c:398-467` 改为：
```c
static const struct { const char *unit; int x, y; } quad_layout[4] = {
    {"°C",  0,  0}, {"%", 120,  0},
    {"hPa", 0,120}, {"m",120,120},
};
for (int i = 0; i < 4; i++) {
    rt_quads[i] = ui_create_quadrant(screen, quad_layout[i].unit,
                                      quad_layout[i].x, quad_layout[i].y, 120, 120);
}
```

- [ ] **Step 4: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：传感器界面 4 象限布局与之前一致，温湿度气压海拔显示正确。

- [ ] **Step 5: Commit**

```bash
git add main/ui/ui_helpers.h main/ui/ui_helpers.c main/ui/ui_screen_sensor.c
git commit -m "refactor(ui): 提取 sensor 四象限容器到 ui_create_quadrant"
```

---

# Phase 4: 业务层去重

**目标：** 合并状态机重复分支、相似函数、相似 setter。零行为变化。

### Task 4.1: 合并 buddy 临时状态分支

**Files:**
- Modify: `main/buddy/buddy.c:498-540`

- [ ] **Step 1: 阅读现状**

Read: `main/buddy/buddy.c:498-540`
确认 CELEBRATE/DIZZY/HEART 三个 case 主体完全相同。

- [ ] **Step 2: 抽取 `is_temporary_state` helper**

在 `buddy.c` 顶部 static helper 区添加：
```c
static bool is_temporary_state(buddy_state_t s) {
    return s == BUDDY_CELEBRATE || s == BUDDY_DIZZY || s == BUDDY_HEART;
}
```

- [ ] **Step 3: 合并三个 case 为单分支**

将：
```c
case BUDDY_CELEBRATE:
    if (...tick...) {
        if (pending_status) apply_pending_status();
        else s_state = pre_random_state;
    }
    break;
case BUDDY_DIZZY:
    /* 完全相同的代码 */
case BUDDY_HEART:
    /* 完全相同的代码 */
```
改为：
```c
case BUDDY_CELEBRATE:
case BUDDY_DIZZY:
case BUDDY_HEART:
    if (...tick...) {
        if (pending_status) apply_pending_status();
        else s_state = pre_random_state;
    }
    break;
```

- [ ] **Step 4: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：触发伙伴庆祝（如番茄钟完成）、dizzy、heart 状态切换正常。

- [ ] **Step 5: Commit**

```bash
git add main/buddy/buddy.c
git commit -m "refactor(buddy): 合并 CELEBRATE/DIZZY/HEART 三处相同 case"
```

---

### Task 4.2: 合并 buddy submit_answer 与 approve

**Files:**
- Modify: `main/buddy/buddy.c:322-374`

- [ ] **Step 1: 阅读确认主体一致**

Read: `main/buddy/buddy.c:322-338`（approve）和 `:358-374`（submit_answer）
确认仅日志 tag 不同，业务逻辑完全相同。

- [ ] **Step 2: 抽取内部 helper**

```c
static void apply_decision(bool approved) {
    /* 公共业务逻辑 */
    s_cbs.on_decision(approved);
    if (approved) { /* ... */ }
    buddy_save_stats();
}
```

- [ ] **Step 3: 改写两个公共 API 调用 helper**

```c
void buddy_approve(void) {
    ESP_LOGI(TAG, "approve");
    apply_decision(true);
}
void buddy_submit_answer(bool approved) {
    ESP_LOGI(TAG, "submit_answer=%d", approved);
    apply_decision(approved);
}
```

- [ ] **Step 4: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：伙伴权限请求弹出后，旋钮确认/拒绝都触发正确状态转换。

- [ ] **Step 5: Commit**

```bash
git add main/buddy/buddy.c
git commit -m "refactor(buddy): 合并 approve/submit_answer 共同业务到 apply_decision"
```

---

### Task 4.3: 合并 pomodoro_engine 四个 setter

**Files:**
- Modify: `main/pomodoro/pomodoro_engine.c:149-187`

- [ ] **Step 1: 阅读现状**

确认 4 个 setter (`set_work_minutes/break_minutes/long_break_minutes/cycles`) 都构造相同的 `int32_t data[4]` 数组并调用 `storage_save_pomodoro_settings`。

- [ ] **Step 2: 抽取内部 helper**

```c
static void persist_settings(void) {
    int32_t data[4] = {
        s_settings.work_minutes,
        s_settings.break_minutes,
        s_settings.long_break_minutes,
        s_settings.cycles,
    };
    storage_save_pomodoro_settings(data);
}
```

- [ ] **Step 3: 改写 4 个 setter**

每个 setter 只做：参数边界 clamp → 写 s_settings 字段 → 调 `persist_settings()` → 触发状态刷新（如有）。

- [ ] **Step 4: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：设置 → 番茄钟 → 调整工作/休息/长休息/周期 → 返回番茄钟界面 → 重启设备 → 设置值持久化。

- [ ] **Step 5: Commit**

```bash
git add main/pomodoro/pomodoro_engine.c
git commit -m "refactor(pomodoro): 抽取 persist_settings 合并 4 个 setter 的 NVS 持久化"
```

---

### Task 4.4: 合并 input_handler 两个持有定时器回调

**Files:**
- Modify: `main/input/input_handler.c:100-116`

- [ ] **Step 1: 阅读现状**

`encoder_hold_timer_cb` 和 `settings_hold_timer_cb` 仅 event.type 字段不同。

- [ ] **Step 2: 合并为单函数 + 参数**

```c
static void hold_timer_cb(lv_timer_t *t) {
    input_event_t *e = (input_event_t *)lv_timer_get_user_data(t);
    /* 公共逻辑 */
}
```

或者保留两个回调，但抽取公共逻辑到 `dispatch_hold_event(input_event_type_t type)` 内部函数。

**推荐：** 抽取内部函数（保持 FreeRTOS 定时器接口不变）。

- [ ] **Step 3: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：编码器长按和侧键长按都触发各自的事件。

- [ ] **Step 4: Commit**

```bash
git add main/input/input_handler.c
git commit -m "refactor(input): 合并编码器/侧键长按定时器公共逻辑"
```

---

### Task 4.5: 合并 service 层 invoke_on_* 包装样板

**Files:**
- Modify: `main/service/wifi_service.c:51-77`
- Modify: `main/service/tcp_service.c:45-90`

- [ ] **Step 1: 评估收益与风险**

11 个 `invoke_on_*` 函数模式相同（`if (callbacks.x) callbacks.x(args)`）但参数列表各异。**宏观抽象**（如宏）会降低可读性；**保留现状**是合理的。

**决策：** 如果 callback 数量 ≤ 7 且都是单行函数，**不改**。本任务标记为 SKIP，跳到下一个 Task。

- [ ] **Step 2: （可选）使用宏减少样板**

如果决定做，定义：
```c
#define INVOKE_CB(name, ...) \
    do { if (s_cbs.name) s_cbs.name(__VA_ARGS__); } while (0)
```
然后所有 `invoke_on_xxx` 调用点改为 `INVOKE_CB(on_xxx, args...)`。

**仅在审查者明确要求时执行**，否则跳过。

- [ ] **Step 3: Commit（仅当 Step 2 执行时）**

```bash
git add main/service/wifi_service.c main/service/tcp_service.c
git commit -m "refactor(service): 用 INVOKE_CB 宏统一回调触发"
```

---

# Phase 5: 性能优化

**目标：** 降低 NVS 提交次数、消除热路径 malloc、减少无效 LVGL 调用。需逐项硬件验证无回归。

### Task 5.1: storage_save_pomodoro_settings 单次 NVS 提交

**Files:**
- Modify: `main/service/storage_service.c:121`（`storage_save_pomodoro_settings`）

- [ ] **Step 1: 阅读现状**

Read: `main/service/storage_service.c:121`
当前实现调用 4 次 `storage_save_int`，等于 4 次 `nvs_open/commit/close`。

- [ ] **Step 2: 重写为单次 open/commit/close**

```c
void storage_save_pomodoro_settings(const int32_t data[4]) {
    nvs_handle_t h;
    if (nvs_open("pomodoro", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "work_min",  data[0]);
    nvs_set_i32(h, "break_min", data[1]);
    nvs_set_i32(h, "lbreak_min",data[2]);
    nvs_set_i32(h, "cycles",    data[3]);
    nvs_commit(h);
    nvs_close(h);
}
```

注意：实际 key 名以 `storage_service.c` 现有 key 为准。

- [ ] **Step 3: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：设置 → 番茄钟 → 修改 4 项 → 重启 → 值已持久化。

- [ ] **Step 4: Commit**

```bash
git add main/service/storage_service.c
git commit -m "perf(storage): 番茄钟设置保存改为单次 NVS 提交"
```

---

### Task 5.2: buddy_save_stats 单次 NVS 提交

**Files:**
- Modify: `main/buddy/buddy.c:568-574`

- [ ] **Step 1: 阅读现状**

`buddy_save_stats` 当前 4 次 NVS open/commit/close。

- [ ] **Step 2: 在 storage_service 中新增 `storage_save_buddy_stats` 批量接口**

```c
// storage_service.h
void storage_save_buddy_stats(int32_t approved, int32_t denied,
                              int32_t hearts, int32_t streak);

// storage_service.c
void storage_save_buddy_stats(int32_t approved, int32_t denied,
                              int32_t hearts, int32_t streak) {
    nvs_handle_t h;
    if (nvs_open("buddy", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "approved", approved);
    nvs_set_i32(h, "denied",   denied);
    nvs_set_i32(h, "hearts",   hearts);
    nvs_set_i32(h, "streak",   streak);
    nvs_commit(h);
    nvs_close(h);
}
```

- [ ] **Step 3: 改写 `buddy_save_stats`**

```c
void buddy_save_stats(void) {
    storage_save_buddy_stats(s_stats.approved, s_stats.denied,
                              s_stats.hearts, s_stats.streak);
}
```

- [ ] **Step 4: 删除 storage_service 中已被替代的 4 个 save_int 调用**

如果有 `storage_save_int("buddy", "approved", ...)` 等专用函数被 `buddy_save_stats` 使用，移除这些包装。

- [ ] **Step 5: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：触发伙伴决策（如权限批准/拒绝）→ 重启 → stats 数据保留。

- [ ] **Step 6: Commit**

```bash
git add main/buddy/buddy.c main/service/storage_service.c main/service/storage_service.h
git commit -m "perf(buddy): stats 保存改为单次 NVS 提交"
```

---

### Task 5.3: ws2812 单像素路径避免 malloc

**Files:**
- Modify: `main/driver/ws2812.c:70-86`

- [ ] **Step 1: 阅读现状**

Read: `main/driver/ws2812.c:70-86`
`ws2812_set_color` 内部走 `ws2812_set_pixels` → 每次 malloc 3 字节。

- [ ] **Step 2: 单像素路径用栈缓冲**

```c
void ws2812_set_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t buf[3] = {g, r, b};  /* WS2812 实际字节序为 GRB */
    ws2812_send(buf, 3);  /* 直接调用底层发送，跳过 set_pixels 的 malloc */
}
```

或者重写 `ws2812_set_pixels` 让 `count==1` 时使用栈缓冲（但会复杂化接口）。**推荐方案 1**：抽取 `ws2812_send` 为内部 static 函数，set_color 和 set_pixels 都走它。

- [ ] **Step 3: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：LED 颜色切换正常（如伙伴状态改变时 LED 颜色变化、LED demo 界面）。

- [ ] **Step 4: Commit**

```bash
git add main/driver/ws2812.c
git commit -m "perf(ws2812): 单像素路径走栈缓冲避免 malloc"
```

---

### Task 5.4: 关键路径 label_set_text 加比较保护

**Files:**
- Modify: `main/ui/ui_screen_main.c:85-94`
- Modify: `main/ui/ui_screen_pomodoro.c:424-440`
- Modify: `main/ui/ui_screen_sensor.c:492-494`

- [ ] **Step 1: 阅读现状**

每秒更新的 label 在 `update_*_display` 中无条件 `lv_label_set_text`。LVGL v9 内部有字符串相等早返回，但仍执行指针/strcmp 开销。

- [ ] **Step 2: 主屏时间/日期 label 加缓存比较**

在 `ui_screen_main.c` 添加 file-scope 缓冲：
```c
static char last_time[16] = {0};
static char last_date[32] = {0};
```

`ui_screen_main_update_time` 中：
```c
char time_buf[16], date_buf[32];
snprintf(time_buf, sizeof(time_buf), "%02d:%02d", h, m);
snprintf(date_buf, sizeof(date_buf), "%s %d", weekday, day);
if (strcmp(time_buf, last_time) != 0) {
    strcpy(last_time, time_buf);
    lv_label_set_text(time_label, time_buf);
}
if (strcmp(date_buf, last_date) != 0) {
    strcpy(last_date, date_buf);
    lv_label_set_text(date_label, date_buf);
}
```

- [ ] **Step 3: 番茄钟 phase/cycle label 同样加保护**

`ui_screen_pomodoro.c:424, 430, 435` 的 completed/phase/cycle label 加缓存比较。

- [ ] **Step 4: 传感器实时视图 label 加保护**

`ui_screen_sensor.c:492-494` 的 i18n 标签（标题/提示等）加保护（虽然文本不变，但每周期都调）。

- [ ] **Step 5: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：主屏时间每秒变化，日期变化；番茄钟倒计时与阶段切换显示正常；传感器界面刷新正常。

- [ ] **Step 6: Commit**

```bash
git add main/ui/
git commit -m "perf(ui): 关键热路径 label_set_text 加缓存比较保护"
```

---

### Task 5.5: ui_screen_sensor update_chart 预分配缓冲

**Files:**
- Modify: `main/ui/ui_screen_sensor.c:84-134`

- [ ] **Step 1: 阅读现状**

`update_chart` 每次调用 `malloc(pt_count * sizeof(sensor_sample_t))` + `malloc(...sensor_time_t)`，最大 60 项。

- [ ] **Step 2: 改为 file-scope 静态缓冲**

```c
static sensor_sample_t  chart_samples[60];
static sensor_time_t    chart_times[60];
```

`update_chart` 中：
```c
int n = sensor_service_get_samples(chart_samples, chart_times, 60);
/* 使用 n */
```

注意：如果 `sensor_service_get_samples` 接口签名要求传入 buffer，确认其语义。

- [ ] **Step 3: 验证构建 + 硬件**

Run: `./build.sh && ./build.sh flash`
验证：传感器图表视图正常显示，长时间运行（>10 分钟）无内存碎片导致的问题。

- [ ] **Step 4: Commit**

```bash
git add main/ui/ui_screen_sensor.c
git commit -m "perf(ui): sensor 图表缓冲改为静态预分配避免堆碎片"
```

---

# Phase 6: 设置子屏幕通用框架（最大重构，可独立执行）

**目标：** 7 个设置子屏幕（alarm/pomodoro/time/system/light/buddy/wifi_saved）的 NAV/ADJUST 模式 + update_display + 4 个回调完全数据驱动。预计减少 ~700 行重复代码。

**风险提示：** 这是最大重构。建议在 Phase 1-5 全部完成、稳定运行一周后再执行。每个子屏幕迁移独立 commit，可分批进行。

### Task 6.1: 设计 `ui_setting_editor` 数据结构

**Files:**
- Create: `main/ui/ui_setting_editor.h`

- [ ] **Step 1: 调研 7 个子屏幕的共同模式**

每个子屏幕都有：
- 4-7 个可调整项（min/max/step）
- NAV 模式旋钮切换选中项，ADJUST 模式旋钮调整值
- 侧键切换 NAV ↔ ADJUST
- 长按返回上级
- update_display 重新格式化所有 label

- [ ] **Step 2: 定义数据驱动接口**

```c
#pragma once

#include "lvgl.h"
#include "ui_list.h"

typedef struct {
    const char *key;            /* 显示的 key 字符串 */
    const char *(*fmt_value)(int value, char *buf, size_t buf_size);  /* 格式化 value */
    int min_value;
    int max_value;
    int step;
    int (*get_current)(void);   /* 读取当前值 */
    void (*set_current)(int v); /* 写入新值（含持久化） */
} ui_setting_item_t;

typedef struct {
    ui_screen_id_t screen_id;
    const char *title;
    const char *hint_nav;
    const char *hint_adjust;
    const ui_setting_item_t *items;
    int item_count;
} ui_setting_editor_config_t;

/* 创建并注册一个设置编辑器屏幕 */
lv_obj_t *ui_setting_editor_create(const ui_setting_editor_config_t *cfg);

/* 工具函数：标准 "(Xh Ym)" / "(Xm)" / "(Xh)" 格式化 */
const char *ui_setting_fmt_duration(int minutes, char *buf, size_t buf_size);
```

- [ ] **Step 3: Commit**

```bash
git add main/ui/ui_setting_editor.h
git commit -m "feat(ui): 定义通用设置编辑器数据接口"
```

---

### Task 6.2: 实现 `ui_setting_editor.c`

**Files:**
- Create: `main/ui/ui_setting_editor.c`
- Modify: `main/CMakeLists.txt`（添加新文件）

- [ ] **Step 1: 实现编辑器主体**

```c
#include "ui_setting_editor.h"
#include "ui_helpers.h"
#include "ui_theme.h"
#include "custom_font.h"
#include <stdio.h>

typedef enum { MODE_NAV, MODE_ADJUST } editor_mode_t;

typedef struct {
    const ui_setting_editor_config_t *cfg;
    lv_obj_t *list;
    lv_obj_t *hint;
    int selected;
    editor_mode_t mode;
} editor_ctx_t;

static editor_ctx_t s_ctx;

static void refresh_list(void) {
    /* 构造 ui_list_item_t 数组，逐项调用 cfg->items[i].fmt_value */
    /* ... */
}

static void update_hint(void) {
    lv_label_set_text(s_ctx.hint,
                      s_ctx.mode == MODE_NAV ? s_ctx.cfg->hint_nav : s_ctx.cfg->hint_adjust);
}

static void on_cw(void) {
    if (s_ctx.mode == MODE_NAV) {
        s_ctx.selected = (s_ctx.selected + 1) % s_ctx.cfg->item_count;
        ui_list_set_selected(s_ctx.list, s_ctx.selected);
    } else {
        const ui_setting_item_t *it = &s_ctx.cfg->items[s_ctx.selected];
        int v = it->get_current() + it->step;
        if (v > it->max_value) v = it->min_value;
        it->set_current(v);
        refresh_list();
    }
}

static void on_ccw(void) { /* 对称 */ }
static void on_press(void) {
    s_ctx.mode = (s_ctx.mode == MODE_NAV) ? MODE_ADJUST : MODE_NAV;
    ui_list_set_selected_color(s_ctx.list,
        s_ctx.mode == MODE_ADJUST ? UI_COLOR_ACCENT : UI_COLOR_SUCCESS);
    update_hint();
}
static void on_settings_press(void) { on_press(); }
static void on_settings_long_press(void) { ui_go_back(); }

lv_obj_t *ui_setting_editor_create(const ui_setting_editor_config_t *cfg) {
    s_ctx.cfg = cfg;
    s_ctx.selected = 0;
    s_ctx.mode = MODE_NAV;

    lv_obj_t *screen = ui_create_screen();
    ui_create_title_label(screen, cfg->title);
    s_ctx.list = ui_list_create(screen, 220, 180, 10, 30);
    s_ctx.hint = ui_create_hint_label(screen, cfg->hint_nav);

    refresh_list();
    ui_list_set_selected_color(s_ctx.list, UI_COLOR_SUCCESS);

    ui_register_callbacks(cfg->screen_id,
                          on_cw, on_ccw, on_press, NULL,
                          on_settings_press, on_settings_long_press, NULL);
    return screen;
}

const char *ui_setting_fmt_duration(int minutes, char *buf, size_t buf_size) {
    int h = minutes / 60, m = minutes % 60;
    if (h && m) snprintf(buf, buf_size, "%dh %dm", h, m);
    else if (h) snprintf(buf, buf_size, "%dh", h);
    else snprintf(buf, buf_size, "%dm", m);
    return buf;
}
```

注意：以上是骨架，**实施前需 Read 每个子屏幕**确认实际行为细节（特别是 ADJUST 模式循环边界、值改变后是否触发额外副作用如立即生效）。

- [ ] **Step 2: 添加到 CMakeLists**

`main/CMakeLists.txt` 添加 `ui/ui_setting_editor.c`。

- [ ] **Step 3: 验证构建（仅编译，未接入）**

Run: `./build.sh`

- [ ] **Step 4: Commit**

```bash
git add main/ui/ui_setting_editor.c main/CMakeLists.txt
git commit -m "feat(ui): 实现通用设置编辑器主体"
```

---

### Task 6.3 ~ 6.9: 逐个迁移 7 个设置子屏幕

每个子屏幕是独立 task，独立 commit。流程一致：

**通用流程：**
1. Read 当前子屏幕源码，记录所有 item 的 (key/min/max/step/get/set/fmt)
2. 在 ui_manager.c 中把该屏幕的 create 调用替换为 `ui_setting_editor_create(&xxx_cfg)`
3. 删除原 `ui_screen_settings_xxx.c/h` 中已被通用编辑器替代的代码（保留必要的 get/set 包装函数）
4. 验证构建 + 硬件（NAV/ADJUST/返回都正常）
5. Commit

**迁移示例（alarm）：**

- [ ] **Task 6.3: 迁移 `ui_screen_settings_alarm.c`**

Files:
- Modify: `main/ui/ui_screen_settings_alarm.c`（删除大部分，保留 get/set）
- Modify: `main/ui/ui_manager.c`（替换 create 调用）

Step 1: 提取 alarm 的 4-5 个 item 配置到 `static const ui_setting_item_t alarm_items[] = {...}`，引用现有的 `storage_load_int/alarm_get/alarm_set` 类函数。

Step 2: 定义 `static const ui_setting_editor_config_t alarm_cfg = { UI_SCREEN_SETTINGS_ALARM, "Alarm", "Rotate: navigate", "Rotate: adjust", alarm_items, ARRAY_SIZE(alarm_items) };`

Step 3: `ui_screen_settings_alarm_create()` 改为 `return ui_setting_editor_create(&alarm_cfg);`

Step 4: 删除原 update_display、on_cw/ccw/press/settings_press 函数。

Step 5: 验证 + Commit:
```bash
git commit -m "refactor(ui): alarm 设置子屏迁移到通用编辑器"
```

- [ ] **Task 6.4: 迁移 `ui_screen_settings_pomodoro.c`**（同上流程）
- [ ] **Task 6.5: 迁移 `ui_screen_settings_time.c`**
- [ ] **Task 6.6: 迁移 `ui_screen_settings_system.c`**
- [ ] **Task 6.7: 迁移 `ui_screen_settings_light.c`**
- [ ] **Task 6.8: 迁移 `ui_screen_settings_buddy.c`**
- [ ] **Task 6.9: 迁移 `ui_screen_wifi_saved.c`**

每个 Task 结束后：
- Run: `./build.sh && ./build.sh flash`
- 手动验证该子屏的完整 NAV/ADJUST/返回流程
- 单独 commit

---

## Final Verification

### Task F.1: 全量回归测试

- [ ] **Step 1: 完整功能冒烟**

硬件验证清单：
- [ ] 主屏：时间每秒刷新，日期正确
- [ ] 旋钮顺/逆时针切屏（主 → 番茄钟 → 伙伴 → 传感器 → 主）
- [ ] 番茄钟：启动/暂停/恢复/完成 → 进入休息 → 完成 → 长休息
- [ ] 伙伴：状态切换（sleep/idle/busy/attention/celebrate/dizzy/heart）
- [ ] 设置：所有子项能 NAV/ADJUST/返回，值持久化（重启验证）
- [ ] WiFi：扫描 → 列表 → 选择 → 密码输入 → 连接 → NTP 同步
- [ ] 整点报时（chime）正常
- [ ] LED 颜色切换（伙伴状态变化时）正常
- [ ] 蜂鸣（按键反馈、番茄钟完成、整点）正常

- [ ] **Step 2: 性能对比**

对比优化前后：
- Flash 占用：`./build.sh` 输出的 size 报告，记录 `.text` / `.rodata` 大小
- RAM 占用：启动日志的 heap free size
- 期望：Phase 1-6 完成后 flash 显著降低（删除 BLE + 多个 helper 减少重复），RAM 略增（静态缓冲替代 malloc）或持平

- [ ] **Step 3: 长时间稳定性测试**

让设备运行 30 分钟，观察：
- 无 crash / panic
- heap free size 稳定（无泄漏）
- 番茄钟能完成至少 1 个完整周期
- 伙伴状态机能正常切换

---

## Self-Review

### Spec coverage
- 去除多余代码或逻辑 → Phase 2 + Phase 4 + Phase 6
- 精简代码实现 → Phase 3 + Phase 4 + Phase 6
- 优化性能 → Phase 5
- 去除重复和冗余 → Phase 3 + Phase 4 + Phase 6
- 美化视觉效果 → Phase 1（统一颜色/字体/间距）
- 不牺牲性能/增加复杂度/资源开销 → 所有 Phase 严格保持零行为变化
- 项目功能严格不变 → 每个 Task 都有硬件冒烟测试

### Placeholder scan
- 无 "TBD/TODO/implement later"
- 每个 step 都有具体代码或命令
- Phase 6 的迁移任务给出完整模板，每个子任务复用相同流程（skill 要求"repeat the code"）— 详见 Task 6.3 的完整流程示例

### Type consistency
- `ui_setting_item_t` / `ui_setting_editor_config_t` 在 Task 6.1 定义、6.2 实现、6.3-6.9 使用 — 一致
- `UI_COLOR_*` 宏在 Task 1.1 定义、1.2-1.3 使用、3.1 中 ui_helpers 引用 — 一致
- `ui_create_screen/title_label/hint_label/register_callbacks` 在 Task 3.1 定义、3.2-3.4 使用 — 一致
- `is_temporary_state` / `apply_decision` / `persist_settings` 在 Phase 4 内部使用 — 一致

### 注意事项
- 所有 Task 都假设硬件在手边可烧录验证。若无可硬件，**先执行 Phase 1（颜色提取）和 Phase 2（dead code 删除）** — 这两个 Phase 风险最低、收益明确，仅靠 `./build.sh` 也能验证大部分正确性。
- Phase 6 是最大重构。建议分多次 PR，每个子屏幕迁移一个 PR，便于 review 和回滚。

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-06-15-overall-optimization.md`. Two execution options:**

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**

由于本计划较大（~25 tasks 跨 6 phases），**建议**：
- 如果想完整执行 → 选择 Subagent-Driven，每个 Task 一个 subagent
- 如果只想做安全部分 → 直接执行 Phase 1-2（视觉统一 + dead code 清理），ROI 最高、零风险
- Phase 6（设置子屏幕通用框架）建议作为独立 PR，最后再考虑
