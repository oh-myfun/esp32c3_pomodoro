# 重构 + Claude Buddy 集成实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 清理冗余架构，集成 Claude Desktop Buddy 功能（BLE 连接、ASCII 宠物、权限审批），建立健壮的模块化系统。

**Architecture:** 服务层精简（wifi_service 移除 HTTP/NTP，time_service 统一 NTP），新增 BLE 桥接和 Buddy 状态机。UI 层移除透传函数，所有界面统一回调注册模式。main.c 集中回调接线，模块间零直接依赖。

**Tech Stack:** ESP-IDF v5.5.4, LVGL v9.5.0, FreeRTOS, BLE (Nordic UART Service), RMT (WS2812)

**Design Spec:** `docs/superpowers/specs/2026-05-02-refactor-buddy-design.md`

---

## Phase 1: 提交现有修改 + 清理

### Task 1: 提交当前未提交的修改

**Files:**
- Modified: 11 个文件（esp_lvgl_port 替换为自定义 mutex 等）

- [ ] **Step 1: 提交所有已修改文件**

```bash
cd D:/Project/IOT/pomodoro
git add README.md dependencies.lock docs/functional_spec.md main/CMakeLists.txt main/Kconfig.projbuild main/idf_component.yml main/input/input_handler.c main/main.c main/ui/ui_manager.c main/ui/ui_manager.h sdkconfig
git commit -m "refactor: replace esp_lvgl_port with custom mutex, cleanup UI manager"
```

- [ ] **Step 2: 验证构建**

```bash
idf.py build
```

Expected: BUILD SUCCESS

---

### Task 2: 删除未使用的组件和陈旧文档

**Files:**
- Delete: `components/esp_lcd_st7789/`（整个目录）
- Delete: `AGENTS.md`
- Delete: `docs/requirements.md`, `docs/functional_spec.md`, `docs/sw_arch.md`, `docs/hw_arch.md`, `docs/pomodoro_design.md`, `docs/network_design.md`
- Delete: `reference/`（整个目录）
- Modify: `main/idf_component.yml` — 移除 `espressif/esp_lcd_st7789` 依赖
- Modify: `main/CMakeLists.txt` — 从 REQUIRES 移除对 `esp_lcd_st7789` 的引用（如果有）

- [ ] **Step 1: 删除文件**

```bash
cd D:/Project/IOT/pomodoro
rm -rf components/esp_lcd_st7789
rm -rf reference
rm AGENTS.md
rm docs/requirements.md docs/sw_arch.md docs/hw_arch.md docs/pomodoro_design.md docs/network_design.md
```

注意：`docs/functional_spec.md` 已在 Task 1 中提交修改，这里删除它。

- [ ] **Step 2: 更新 idf_component.yml**

将 `main/idf_component.yml` 改为：

```yaml
dependencies:
  lvgl/lvgl: '*'
  espressif/button: '*'
  espressif/knob: '*'
```

移除 `espressif/esp_lcd_st7789` 行。

- [ ] **Step 3: 更新 dependencies.lock**

删除后重新生成：

```bash
idf.py reconfigure
```

或手动编辑 `dependencies.lock` 移除 `esp_lcd_st7789` 条目。

- [ ] **Step 4: 验证构建**

```bash
idf.py build
```

Expected: BUILD SUCCESS（可能需要 `idf.py fullclean` 后重新构建）

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "chore: remove unused esp_lcd_st7789 component and stale docs"
```

---

## Phase 2: 服务层重构

### Task 3: 重命名 wifi_manager 为 wifi_service 并精简

**Files:**
- Rename: `main/network/wifi_manager.c` → `main/service/wifi_service.c`
- Rename: `main/network/wifi_manager.h` → `main/service/wifi_service.h`
- Delete: `main/network/` 目录
- Modify: 所有引用 `wifi_manager.h` 的文件

- [ ] **Step 1: 创建 service 目录并移动文件**

```bash
cd D:/Project/IOT/pomodoro
mkdir -p main/service
git mv main/network/wifi_manager.c main/service/wifi_service.c
git mv main/network/wifi_manager.h main/service/wifi_service.h
rmdir main/network
```

- [ ] **Step 2: 重写 wifi_service.h**

定义新的精简接口：

```c
#pragma once

#include <stdbool.h>

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED
} wifi_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open;
} wifi_ap_info_t;

typedef struct {
    void (*on_connected)(const char *ip);
    void (*on_disconnected)(void);
    void (*on_scan_complete)(int count);
    void (*on_connect_failed)(void);
} wifi_callbacks_t;

int  wifi_service_init(void);
void wifi_service_register_callbacks(const wifi_callbacks_t *cbs);

void wifi_service_scan(void);
int  wifi_service_get_scan_count(void);
const wifi_ap_info_t* wifi_service_get_ap(int index);

void wifi_service_connect(const char *ssid, const char *password);
void wifi_service_disconnect(void);

bool wifi_service_is_connected(void);
const char* wifi_service_get_ip(void);
wifi_state_t wifi_service_get_state(void);
```

移除的接口（职责转移）：
- `wifi_manager_sync_time()` → 移到 time_service
- `wifi_manager_set_ntp_interval/get_ntp_interval()` → 移到 time_service
- `wifi_manager_set_timezone/get_timezone()` → 移到 time_service
- `wifi_manager_is_connect_failed()` → 改用回调 `on_connect_failed`

- [ ] **Step 3: 重写 wifi_service.c**

从 `wifi_manager.c` 精简：

1. 保留：WiFi STA 初始化、事件处理、扫描、连接/断开、状态查询
2. 移除：HTTP 服务器（全部删除，包括 `http_root_handler`、`http_save_handler`、`start_http_server`）
3. 移除：SNTP 同步（`wifi_manager_sync_time`）
4. 移除：时区管理（`wifi_manager_set_timezone/get_timezone`）
5. 移除：NTP 间隔管理（`wifi_manager_set_ntp_interval/get_ntp_interval`）
6. 新增：回调注册和分发机制
7. 新增：自动重连（指数退避，2s→4s→...→60s）

连接成功时调用 `callbacks.on_connected(ip)`，断开时调用 `callbacks.on_disconnected()`。
不再启动 HTTP 服务器，不再调用 `esp_sntp_*`。

- [ ] **Step 4: 更新所有引用**

在以下文件中将 `#include "network/wifi_manager.h"` 改为 `#include "service/wifi_service.h"`，并更新函数调用：
- `main/main.c`
- `main/ui/ui_screen_wifi.c`（`wifi_scan_result_t` → `wifi_ap_info_t`，`wifi_manager_*` → `wifi_service_*`）

- [ ] **Step 5: 更新 CMakeLists.txt**

`main/CMakeLists.txt` 中：
- 将 `network/wifi_manager.c` 替换为 `service/wifi_service.c`
- 将 `INCLUDE_DIRS` 中的 `"network"` 替换为 `"service"`

- [ ] **Step 6: 验证构建**

```bash
idf.py build
```

Expected: BUILD SUCCESS（可能有编译错误需逐一修复，主要是函数重命名）

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "refactor: rename wifi_manager to wifi_service, remove HTTP server and NTP logic"
```

---

### Task 4: 增强 time_service，统一 NTP 管理

**Files:**
- Modify: `main/service/time_service.c`
- Modify: `main/service/time_service.h`
- Move: `main/time/` → `main/service/`

- [ ] **Step 1: 移动 time_service 到 service 目录**

```bash
cd D:/Project/IOT/pomodoro
git mv main/time/time_service.c main/service/time_service.c
git mv main/time/time_service.h main/service/time_service.h
rmdir main/time
```

- [ ] **Step 2: 更新 time_service.h**

在现有接口基础上新增：

```c
// 新增：主动请求同步（由 wifi 回调触发）
void time_service_request_sync(void);

// 新增：定期 tick，在 Service 任务中调用，处理自动重同步
void time_service_tick(void);

// 简化：set_timezone 只需 offset_hours
void time_service_set_timezone_offset(int hours);
int  time_service_get_timezone_offset(void);
```

- [ ] **Step 3: 重写 time_service.c**

合并 `wifi_manager_sync_time()` 中的 NTP 服务器列表：

1. 保留现有：SNTP 初始化、回调通知、时区设置、格式化
2. 新增 `time_service_request_sync()`：重启 SNTP，配置 3 个服务器（pool.ntp.org, time.windows.com, time.google.com），等待同步完成
3. 新增 `time_service_tick()`：检查距上次同步是否超过 interval，触发重同步
4. 修复时区格式统一为 `"CST-8"` 格式（与原 wifi_manager 一致）
5. 移除重复的 `esp_sntp` 初始化（只在 `request_sync` 中初始化）

- [ ] **Step 4: 更新引用**

- `main/CMakeLists.txt`：`time/time_service.c` → `service/time_service.c`，INCLUDE_DIRS 移除 `"time"`
- `main/main.c`：`#include "time/time_service.h"` → `#include "service/time_service.h"`

- [ ] **Step 5: 验证构建**

```bash
idf.py build
```

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "refactor: unify NTP in time_service, move to service directory"
```

---

### Task 5: 移动 storage_service 到 service 目录

**Files:**
- Move: `main/storage/` → `main/service/`

- [ ] **Step 1: 移动文件**

```bash
cd D:/Project/IOT/pomodoro
git mv main/storage/storage_service.c main/service/storage_service.c
git mv main/storage/storage_service.h main/service/storage_service.h
rmdir main/storage
```

- [ ] **Step 2: 更新引用**

- `main/CMakeLists.txt`：`storage/storage_service.c` → `service/storage_service.c`，INCLUDE_DIRS 移除 `"storage"`
- `main/main.c`：`#include "storage/storage_service.h"` → `#include "service/storage_service.h"`
- `main/service/wifi_service.c`：更新 storage include 路径
- `main/service/time_service.c`：更新 storage include 路径
- `main/pomodoro/pomodoro_engine.c`：更新 storage include 路径

- [ ] **Step 3: 验证构建**

```bash
idf.py build
```

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "refactor: move storage_service to service directory"
```

---

## Phase 3: 新增驱动

### Task 6: 新增 WS2812 驱动

**Files:**
- Create: `main/driver/ws2812.c`
- Create: `main/driver/ws2812.h`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: 创建 ws2812.h**

```c
#pragma once

#include <stdint.h>

#define WS2812_GPIO GPIO_NUM_8

int  ws2812_init(void);
void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b);
void ws2812_off(void);
```

- [ ] **Step 2: 创建 ws2812.c**

使用 ESP-IDF RMT 外设驱动单颗 WS2812：

```c
#include "ws2812.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

static const char *TAG = "ws2812";
static rmt_channel_handle_t rmt_chan = NULL;
static rmt_encoder_handle_t encoder = NULL;

// WS2812 时序：T0H=0.4us, T0L=0.85us, T1H=0.8us, T1L=0.45us, RESET>280us
#define WS2812_T0H_NS  400
#define WS2812_T0L_NS  850
#define WS2812_T1H_NS  800
#define WS2812_T1L_NS  450
#define WS2812_RESET_NS 280000

int ws2812_init(void) {
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = WS2812_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz → 100ns tick
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    esp_err_t ret = rmt_new_tx_channel(&chan_cfg, &rmt_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = { .level0 = 1, .duration0 = WS2812_T0H_NS / 100,
                   .level1 = 0, .duration1 = WS2812_T0L_NS / 100 },
        .bit1 = { .level0 = 1, .duration0 = WS2812_T1H_NS / 100,
                   .level1 = 0, .duration1 = WS2812_T1L_NS / 100 },
        .flags.msb_first = 1,
    };
    ret = rmt_new_bytes_encoder(&enc_cfg, &encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Encoder init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    rmt_enable(rmt_chan);
    ws2812_off();
    ESP_LOGI(TAG, "WS2812 initialized on GPIO%d", WS2812_GPIO);
    return 0;
}

void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b) {
    if (!rmt_chan || !encoder) return;
    // WS2812 expects GRB order
    uint8_t grb[3] = { g, r, b };
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    rmt_transmit(rmt_chan, encoder, grb, 3, &tx_cfg);
    rmt_tx_wait_all_done(rmt_chan, -1);
}

void ws2812_off(void) {
    ws2812_set_color(0, 0, 0);
}
```

- [ ] **Step 3: 更新 CMakeLists.txt**

在 `main/CMakeLists.txt` 的 SRCS 中添加 `driver/ws2812.c`。REQUIRES 中确认有 `driver`（已有）。

- [ ] **Step 4: 验证构建**

```bash
idf.py build
```

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "feat: add WS2812 RGB LED driver on GPIO8"
```

---

## Phase 4: BLE 服务

### Task 7: 新增 BLE 服务（Nordic UART Service）

**Files:**
- Create: `main/service/ble_service.c`
- Create: `main/service/ble_service.h`
- Modify: `main/CMakeLists.txt` — 添加源文件和 REQUIRES

- [ ] **Step 1: 创建 ble_service.h**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

// BLE 心跳数据
typedef struct {
    int total;
    int running;
    int waiting;
    char msg[128];
    char prompt_id[64];
    char prompt_tool[32];
    char prompt_hint[128];
    bool has_prompt;
} ble_heartbeat_t;

typedef struct {
    void (*on_connected)(void);
    void (*on_disconnected)(void);
    void (*on_heartbeat)(const ble_heartbeat_t *hb);
    void (*on_prompt)(const char *id, const char *tool, const char *hint);
} ble_callbacks_t;

int  ble_service_init(void);
void ble_service_register_callbacks(const ble_callbacks_t *cbs);
void ble_service_send_permission(const char *id, const char *decision);
void ble_service_send_ack(const char *cmd, bool ok);
bool ble_service_is_connected(void);
void ble_service_tick(void);  // 在 Service 任务中定期调用，处理重连/广播
```

- [ ] **Step 2: 创建 ble_service.c**

实现 Nordic UART Service（NUS）BLE 桥接。关键部分：

1. **广播**：设备名 `Claude-Buddy-XXXX`（MAC 末 2 字节），包含 NUS UUID
2. **GATT 服务**：NUS Service UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
   - RX characteristic `6e400002...`（write，接收桌面端数据）
   - TX characteristic `6e400003...`（notify，发送数据到桌面端）
3. **接收处理**：行缓冲（积累字节直到 `\n`），解析 JSON
4. **JSON 解析**：使用 cJSON（ESP-IDF 内置组件）解析 heartbeat/prompt/turn events
5. **发送**：构造 JSON + `\n`，通过 TX characteristic notify
6. **连接管理**：断开后自动重新开始广播

JSON 解析逻辑：
- 收到含 `prompt` 字段的心跳 → 构造 `ble_heartbeat_t`，调用 `on_heartbeat` 和 `on_prompt` 回调
- 收到含 `total/running/waiting` 的心跳 → 构造 `ble_heartbeat_t`，调用 `on_heartbeat` 回调
- 收到 `{"cmd":"status"}` → 构造并回复 status ack
- 收到 `{"cmd":"name",...}` → 存储设备名，回复 ack
- 收到 `{"cmd":"owner",...}` → 存储主人名，回复 ack
- 收到 `{"time":[epoch,tz_offset]}` → 设置系统时间

- [ ] **Step 3: 更新 CMakeLists.txt**

在 SRCS 中添加 `service/ble_service.c`。在 REQUIRES 中添加 `bt`（ESP-IDF 蓝牙组件）和 `json`（cJSON）。

- [ ] **Step 4: 更新 sdkconfig 启用 BLE**

确认 sdkconfig 中包含：
```
CONFIG_BT_ENABLED=y
CONFIG_BT_BLE_ENABLED=y
```

如不存在，通过 `idf.py menuconfig` 或直接编辑 sdkconfig 添加。

- [ ] **Step 5: 验证构建**

```bash
idf.py build
```

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat: add BLE service with Nordic UART Service protocol"
```

---

## Phase 5: Buddy 模块

### Task 8: 新增 ASCII 宠物数据

**Files:**
- Create: `main/buddy/buddy_chars.h`

- [ ] **Step 1: 创建 buddy_chars.h**

定义 4 种宠物的 ASCII 动画数据。每种宠物 7 种状态，每种状态 2-4 帧动画。

```c
#pragma once

#include <stdint.h>

#define MAX_ANIM_FRAMES 4
#define BUDDY_STATE_COUNT 7
#define BUDDY_FRAME_LINES 12
#define BUDDY_FRAME_WIDTH 20

typedef enum {
    BUDDY_SLEEP = 0,
    BUDDY_IDLE,
    BUDDY_BUSY,
    BUDDY_ATTENTION,
    BUDDY_CELEBRATE,
    BUDDY_DIZZY,
    BUDDY_HEART,
} buddy_state_t;

typedef struct {
    const char *name;
    const char *personality;
    const char *const (*frames)[BUDDY_STATE_COUNT][MAX_ANIM_FRAMES][BUDDY_FRAME_LINES];
    const uint8_t frame_count[BUDDY_STATE_COUNT];
} buddy_species_t;

// 4 种宠物（cat, duck, penguin, robot），具体 ASCII 帧数据内联定义
extern const buddy_species_t BUDDY_SPECIES[];
extern const int BUDDY_SPECIES_COUNT;
```

在 `.c` 实现文件中定义 4 种宠物的 ASCII 帧。每帧约 12 行 x 20 列，使用等宽字符绘制简单动物形象。

- [ ] **Step 2: 验证构建**

```bash
idf.py build
```

- [ ] **Step 3: 提交**

```bash
git add -A
git commit -m "feat: add ASCII buddy character data"
```

---

### Task 9: 新增 Buddy 状态机

**Files:**
- Create: `main/buddy/buddy.c`
- Create: `main/buddy/buddy.h`
- Create: `main/buddy/buddy_ble.c`
- Create: `main/buddy/buddy_ble.h`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: 创建 buddy.h**

```c
#pragma once

#include "buddy_chars.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    buddy_state_t state;
    int species_index;
    uint32_t approved_count;
    uint32_t denied_count;
    char owner_name[32];
    char buddy_name[32];
    // 当前 prompt 信息
    char prompt_id[64];
    char prompt_tool[32];
    char prompt_hint[128];
    bool has_pending_prompt;
} buddy_info_t;

typedef struct {
    void (*on_state_changed)(buddy_state_t new_state);
    void (*on_prompt_cleared)(void);
} buddy_callbacks_t;

int  buddy_init(void);
void buddy_register_callbacks(const buddy_callbacks_t *cbs);

// BLE 回调驱动的更新
void buddy_on_heartbeat(int running, int waiting, bool has_prompt,
                        const char *prompt_id, const char *tool, const char *hint);
void buddy_on_ble_connected(void);
void buddy_on_ble_disconnected(void);

// 用户操作
void buddy_approve(void);
void buddy_deny(void);
void buddy_trigger_dizzy(void);

// 信息查询
buddy_info_t buddy_get_info(void);
void buddy_set_species(int index);
int  buddy_get_species_count(void);
const char* buddy_get_species_name(int index);

// 动画
void buddy_tick(void);  // 在 Service 任务中定期调用，推进动画帧
const char* const* buddy_get_current_frame(void);  // 返回当前帧的行数组

// 统计持久化
void buddy_save_stats(void);
void buddy_load_stats(void);
```

- [ ] **Step 2: 创建 buddy.c**

状态机实现：

1. `buddy_init()`：从 NVS 加载物种选择和统计数据
2. `buddy_on_ble_connected()`：SLEEP → IDLE
3. `buddy_on_ble_disconnected()`：任意 → SLEEP
4. `buddy_on_heartbeat()`：
   - `running > 0 && !has_prompt` → BUSY
   - `has_prompt` → ATTENTION
   - `!running && !has_prompt` → IDLE
5. `buddy_approve()`：递增 approved 计数，ATTENTION → CELEBRATE（3s 后 → IDLE）
6. `buddy_deny()`：递增 denied 计数，ATTENTION → IDLE
7. `buddy_trigger_dizzy()`：保存当前状态，切换到 DIZZY（3s 后恢复）
8. `buddy_tick()`：每 500ms 推进动画帧索引

状态变化时调用 `on_state_changed` 回调。

- [ ] **Step 3: 创建 buddy_ble.h**

```c
#pragma once

// BLE 协议层：解析接收数据，构造发送数据
// 由 ble_service 的回调驱动

void buddy_ble_init(void);

// 供 ble_service 回调调用
void buddy_ble_on_line_received(const char *line);

// 供 buddy 模块调用
void buddy_ble_send_permission(const char *id, const char *decision);
void buddy_ble_send_status(void);
```

- [ ] **Step 4: 创建 buddy_ble.c**

业务层协议处理：

1. `buddy_ble_on_line_received(line)`：解析 JSON 行
   - 含 `total/running/waiting` 字段 → 解析为 heartbeat → 调用 `buddy_on_heartbeat()`
   - 含 `prompt` 字段 → 提取 id/tool/hint → 调用 `buddy_on_heartbeat()` with prompt
   - 含 `cmd:"status"` → 调用 `buddy_ble_send_status()`
   - 含 `cmd:"name"` → 存储 buddy name
   - 含 `cmd:"owner"` → 存储 owner name
   - 含 `time` → 设置系统时间
2. `buddy_ble_send_permission()`：调用 `ble_service_send_permission(id, decision)`
3. `buddy_ble_send_status()`：构造 status JSON，调用 `ble_service_send_ack()`

- [ ] **Step 5: 更新 CMakeLists.txt**

在 SRCS 中添加：
- `buddy/buddy.c`
- `buddy/buddy_ble.c`
- `buddy/buddy_chars.c`（如果数据在 .c 文件中）

在 INCLUDE_DIRS 中添加 `"buddy"`。

- [ ] **Step 6: 验证构建**

```bash
idf.py build
```

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "feat: add buddy state machine and BLE protocol handler"
```

---

## Phase 6: UI 层重构

### Task 10: 精简 ui_manager，移除透传函数

**Files:**
- Modify: `main/ui/ui_manager.c`
- Modify: `main/ui/ui_manager.h`
- Modify: `main/main.c` — 更新调用方式

- [ ] **Step 1: 精简 ui_manager.h**

移除所有透传函数声明，只保留：

```c
#pragma once

#include "lvgl.h"

typedef enum {
    UI_SCREEN_MAIN = 0,
    UI_SCREEN_POMODORO,
    UI_SCREEN_BUDDY,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_SETTINGS_POMODORO,
    UI_SCREEN_WIFI_LIST,
    UI_SCREEN_PASSWORD_INPUT,
    UI_SCREEN_COUNT
} ui_screen_id_t;

typedef struct {
    void (*on_encoder_cw)(void);
    void (*on_encoder_ccw)(void);
    void (*on_encoder_press)(void);
    void (*on_encoder_long_press)(void);
    void (*on_settings_press)(void);
} ui_input_callbacks_t;

// 初始化
void ui_init(void);

// 屏幕切换
void ui_switch_screen(ui_screen_id_t screen_id);
ui_screen_id_t ui_get_current_screen(void);

// 回调注册/分发
void ui_register_input_callbacks(ui_screen_id_t screen, const ui_input_callbacks_t *cbs);
void ui_unregister_input_callbacks(ui_screen_id_t screen);
void ui_dispatch_encoder_cw(void);
void ui_dispatch_encoder_ccw(void);
void ui_dispatch_encoder_press(void);
void ui_dispatch_encoder_long_press(void);
void ui_dispatch_settings_press(void);

// LVGL mutex
void lvgl_lock(void);
void lvgl_unlock(void);
```

移除的声明（调用方改为直接调用各界面的接口）：
- `ui_update_time/temp/humidity`
- `ui_update_wifi_status/wifi_status_ex`
- `ui_pomodoro_update_*`
- `ui_password_input_*`
- `ui_enter_settings/exit_settings`
- `ui_settings_select_*/enter_adjust/adjust_*`
- `settings_mode_t`, `settings_item_t`, `pomodoro_op_t` 枚举

- [ ] **Step 2: 精简 ui_manager.c**

移除所有透传函数实现，只保留：
- `ui_init()`：创建所有界面，注册回调，加载主界面
- `ui_switch_screen()`：切换屏幕
- `ui_register_input_callbacks/unregister_input_callbacks`
- `ui_dispatch_*`：5 个分发函数
- `lvgl_lock/unlock`：mutex

- [ ] **Step 3: 更新 main.c 中的 ui_update_task**

直接调用各界面的更新函数：

```c
// 原来：
ui_update_time();
// 改为：
ui_screen_main_update_time();

// 原来：
ui_pomodoro_update_state(...);
// 改为：
ui_screen_pomodoro_update_state(...);

// 原来：
ui_update_wifi_status_ex(status, color);
// 改为：
ui_screen_main_update_wifi_status_ex(status, color);
```

需要在 `ui_screen_main.h` 中添加 `ui_screen_main_update_wifi_status_ex` 声明。

- [ ] **Step 4: 更新各 screen 文件中的 include**

移除对已删除 enum 的依赖。`settings_mode_t` 移到 `ui_screen_settings.h` 内部定义（它只是 settings 界面的内部状态）。

- [ ] **Step 5: 验证构建**

```bash
idf.py build
```

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "refactor: slim down ui_manager, remove facade functions"
```

---

### Task 11: 新增 Buddy 界面，替代 Chat 界面

**Files:**
- Delete: `main/ui/ui_screen_chat.c`, `main/ui/ui_screen_chat.h`
- Create: `main/ui/ui_screen_buddy.c`
- Create: `main/ui/ui_screen_buddy.h`
- Modify: `main/CMakeLists.txt`
- Modify: `main/ui/ui_manager.c`（UI_SCREEN_BUDDY 替代 UI_SCREEN_CHAT）

- [ ] **Step 1: 删除 Chat 界面**

```bash
cd D:/Project/IOT/pomodoro
git rm main/ui/ui_screen_chat.c main/ui/ui_screen_chat.h
```

- [ ] **Step 2: 创建 ui_screen_buddy.h**

```c
#pragma once

#include "lvgl.h"

lv_obj_t* ui_screen_buddy_create(void);

// 数据更新（由 Service 任务通过 lvgl_lock 调用）
void ui_screen_buddy_update_state(void);       // 刷新宠物动画和状态文字
void ui_screen_buddy_show_prompt(const char *tool, const char *hint);
void ui_screen_buddy_clear_prompt(void);
void ui_screen_buddy_set_connected(bool connected);
void ui_screen_buddy_update_stats(uint32_t approved, uint32_t denied);
```

- [ ] **Step 3: 创建 ui_screen_buddy.c**

界面布局：

```
┌──────────────────────┐
│  [状态图标] Buddy名   │  ← 顶栏：BLE 连接状态 + 名字
│                      │
│    ┌────────────┐    │
│    │  ASCII 动画  │    │  ← 中间：宠物动画（等宽字体 label）
│    │  12 行文本   │    │
│    └────────────┘    │
│                      │
│  状态: Idle / Busy   │  ← 状态文字
│  最近消息显示        │  ← msg 字段（如果有）
│                      │
│  [导航提示]           │  ← 底部提示
└──────────────────────┘
```

ATTENTION 状态时覆盖为审批界面：

```
┌──────────────────────┐
│  ⚠ 需要审批          │
│                      │
│    [宠物-警觉动画]    │
│                      │
│  工具: Bash           │
│  rm -rf /tmp/foo      │
│                      │
│  > Approve            │  ← 编码器滚动
│    Deny               │  ← SET 确认
└──────────────────────┘
```

信息页（编码器短按进入）：

```
┌──────────────────────┐
│  Buddy 信息           │
│                      │
│  宠物: Cat            │
│  主人: Felix          │
│  已审批: 42           │
│  已拒绝: 3            │
│  BLE: 已连接          │
│                      │
│  切换宠物 >           │
│                      │
│  [SET 返回]           │
└──────────────────────┘
```

回调注册：
- `on_encoder_cw`：正常模式→无操作；ATTENTION→滚动 Approve/Deny；信息页→滚动列表
- `on_encoder_ccw`：同上反方向
- `on_encoder_press`：正常模式→进入信息页；ATTENTION→无操作；信息页→返回正常模式
- `on_encoder_long_press`：所有模式→触发 DIZZY（调用 `buddy_trigger_dizzy()`）
- `on_settings_press`：正常模式→进入导航（下一屏）；ATTENTION→确认选择（approve/deny）；信息页→返回正常模式

屏幕导航：BUDDY → CW → SETTINGS, BUDDY → CCW → POMODORO

- [ ] **Step 4: 更新 ui_manager.c**

在 `ui_init()` 中：
```c
// 原来：
screens[UI_SCREEN_CHAT] = ui_screen_chat_create();
// 改为：
screens[UI_SCREEN_BUDDY] = ui_screen_buddy_create();
```

更新 `#include "ui_screen_chat.h"` → `#include "ui_screen_buddy.h"`

- [ ] **Step 5: 更新 CMakeLists.txt**

替换 `ui/ui_screen_chat.c` 为 `ui/ui_screen_buddy.c`

- [ ] **Step 6: 验证构建**

```bash
idf.py build
```

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "feat: replace chat screen with buddy screen"
```

---

### Task 12: 修复已知 UI 问题

**Files:**
- Modify: `main/ui/ui_screen_main.c` — 温湿度显示占位符
- Modify: `main/ui/ui_screen_settings.c` — 修复设置不生效
- Modify: `main/ui/ui_screen_settings_pomodoro.c` — 修复循环调用 bug
- Modify: `main/ui/ui_screen_wifi.c` — 补充密码输入回调
- Modify: `main/ui/ui_screen_pomodoro.c` — 更新导航（CHAT→BUDDY）

- [ ] **Step 1: 修复主界面温湿度占位符**

在 `ui_screen_main.c` 的 `ui_screen_main_update_temp/humidity` 中：
- 不再接受 float 参数
- 改为在创建时显示 `--` 占位符
- 更新函数签名在 `.h` 中

- [ ] **Step 2: 修复设置界面**

在 `ui_screen_settings.c` 中：
- 亮度/对比度调整时调用 LCD 背光控制（如果 ST7789 驱动支持）或记录日志
- 语言切换标记为"待实现"
- 番茄钟设置子界面导航修复

- [ ] **Step 3: 修复番茄钟设置循环调用 bug**

在 `ui_screen_settings_pomodoro.c` 中：
- 编码器 CW/CCW 直接调用 `pomodoro_engine_set_*` 函数
- 不再通过 `ui_settings_adjust_up/down` 转发（因为 ui_manager 已精简）
- 每次调整后调用 `ui_screen_settings_pomodoro_refresh()` 更新显示

- [ ] **Step 4: 补充密码输入回调**

在 `ui_screen_wifi.c` 中：
- 创建密码输入界面时注册输入回调
- 编码器 CW/CCW：移动光标
- SET：确认输入（调用 `wifi_service_connect()`）
- 编码器短按：添加字符

- [ ] **Step 5: 更新番茄钟界面导航**

在 `ui_screen_pomodoro.c` 中将导航从 `UI_SCREEN_CHAT` 改为 `UI_SCREEN_BUDDY`。

- [ ] **Step 6: 验证构建**

```bash
idf.py build
```

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "fix: resolve UI bugs - placeholder temp/humidity, settings persistence, password input callbacks"
```

---

## Phase 7: 主程序编排

### Task 13: 重写 main.c 编排

**Files:**
- Modify: `main/main.c`

- [ ] **Step 1: 重写 main.c**

新的 `app_main()` 实现：

1. **初始化**（按依赖顺序，非致命错误跳过）：
   - `nvs_flash_init()` → 致命
   - `storage_service_init()` → 致命
   - `buzzer_init()` → 非致命
   - `st7789_lcd_init()` → 非致命
   - `ws2812_init()` → 非致命
   - `lvgl_init()` → 依赖 LCD
   - `ui_init()` → 依赖 LVGL
   - `pomodoro_engine_init()` → 非致命
   - `buddy_init()` → 非致命
   - `input_handler_init()` → 非致命
   - `wifi_service_init()` → 非致命
   - `ble_service_init()` → 非致命

2. **回调接线**：
   - `wifi_service_register_callbacks(&wifi_cbs)` — on_connected → 触发 NTP + 更新 UI
   - `ble_service_register_callbacks(&ble_cbs)` — on_heartbeat → buddy 更新
   - `buddy_register_callbacks(&buddy_cbs)` — on_state_changed → WS2812 + UI 切换

3. **创建 4 个任务**：
   - LVGL（优先级 5，8KB）
   - Input（优先级 3，6KB）
   - Service（优先级 2，6KB）— 新增
   - UIUpdate（优先级 1，4KB）

4. **Service 任务**（新增）循环：
   ```c
   while (1) {
       wifi_service_tick();      // 自动重连
       time_service_tick();      // NTP 定期同步
       ble_service_tick();       // BLE 广播/维护
       buddy_tick();             // 动画帧推进
       vTaskDelay(100 / portTICK_PERIOD_MS);
   }
   ```

5. **UIUpdate 任务**（精简）：
   ```c
   while (1) {
       if (now - last_tick >= 1000) {
           pomodoro_engine_tick();
           // 直接调用界面更新函数
           lvgl_lock();
           ui_screen_pomodoro_update_state(...);
           ui_screen_main_update_time();
           lvgl_unlock();
           last_tick = now;
       }
       vTaskDelay(100 / portTICK_PERIOD_MS);
   }
   ```

- [ ] **Step 2: 更新头文件 include**

```c
#include "driver/st7789_lcd.h"
#include "driver/buzzer.h"
#include "driver/ws2812.h"
#include "input/input_handler.h"
#include "ui/ui_manager.h"
#include "ui/ui_screen_main.h"
#include "ui/ui_screen_pomodoro.h"
#include "service/wifi_service.h"
#include "service/time_service.h"
#include "service/storage_service.h"
#include "service/ble_service.h"
#include "pomodoro/pomodoro_engine.h"
#include "buddy/buddy.h"
```

- [ ] **Step 3: 验证构建**

```bash
idf.py build
```

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "refactor: rewrite main.c with service task and callback wiring"
```

---

## Phase 8: 文档更新

### Task 14: 更新文档

**Files:**
- Modify: `CLAUDE.md`
- Modify: `README.md`
- Create: `docs/architecture.md`
- Create: `docs/ble_buddy_design.md`

- [ ] **Step 1: 编写 docs/architecture.md**

总架构文档：模块图、任务模型、数据流、回调接线、GPIO 表。内容反映重构后的实际代码。

- [ ] **Step 2: 编写 docs/ble_buddy_design.md**

BLE 协议设计：NUS 配置、JSON 格式、状态机、宠物数据结构。

- [ ] **Step 3: 更新 CLAUDE.md**

反映重构后的架构：service 目录、buddy 模块、WS2812、4 个任务、BLE。

- [ ] **Step 4: 精简 README.md**

项目简介、硬件连接表、构建命令、功能列表。移除过时描述。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "docs: update documentation for refactored architecture"
```

---

## Phase 9: 集成验证

### Task 15: 全量构建验证 + 烧录测试

- [ ] **Step 1: 全量清理构建**

```bash
cd D:/Project/IOT/pomodoro
idf.py fullclean
idf.py build
```

Expected: BUILD SUCCESS，无 warning（或仅有已知无害 warning）

- [ ] **Step 2: 检查 binary size**

```bash
idf.py size
```

确认 flash 使用量在 ESP32-C3 4MB flash 范围内。

- [ ] **Step 3: 烧录测试**

```bash
idf.py -p COM7 flash monitor
```

验证：
- 主界面显示正常（时间、日期、WiFi 状态、温湿度占位符）
- 编码器旋转切换界面正常（Main → Pomodoro → Buddy → Settings → 循环）
- 番茄钟启动/暂停/停止正常
- WiFi 扫描和连接正常
- Buddy 界面显示 SLEEP 状态宠物
- BLE 广播可见（手机蓝牙扫描能看到 "Claude-Buddy-XXXX"）
- WS2812 LED 可控

- [ ] **Step 4: 修复集成问题**

记录发现的任何问题并修复。单独提交每个修复。

- [ ] **Step 5: 最终提交**

```bash
git add -A
git commit -m "fix: resolve integration issues from refactor"
```
