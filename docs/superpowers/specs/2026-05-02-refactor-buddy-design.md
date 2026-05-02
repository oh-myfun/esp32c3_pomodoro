# 重构设计：架构清理 + Claude Buddy 集成

日期：2026-05-02

## 目标

在现有番茄钟设备上集成 Claude Desktop Buddy 功能（BLE 连接、ASCII 宠物、权限审批），同时清理冗余代码、修复已知问题、建立健壮的模块化架构，使单个模块故障不影响系统稳定性。

## 前置条件

先提交当前 11 个未提交文件的修改（esp_lvgl_port 替换为自定义 mutex、UI manager 回调分发重构等），在此基础上开始重构。

---

## 1. 整体架构与故障隔离

### 模块分层

```
┌─────────────────────────────────────────────┐
│                   main.c                     │  初始化编排 + 任务创建 + 回调接线
├─────────────────────────────────────────────┤
│               ui_manager                     │  界面调度：回调分发、屏幕切换、LVGL mutex
├──────┬──────┬──────┬──────┬───────┬─────────┤
│ main │pomo  │buddy │settings│wifi  │pwd     │  独立界面模块，互不引用
├──────┴──────┴──────┴──────┴───────┴─────────┤
│              driver/                         │  硬件驱动：lcd, buzzer, ws2812
├─────────────────────────────────────────────┤
│              service/                        │  基础服务：storage, time, wifi, ble
├─────────────────────────────────────────────┤
│              buddy/                          │  Buddy 模块：状态机、BLE 协议、宠物数据
├─────────────────────────────────────────────┤
│              input/                          │  输入处理：编码器 + 按键
└─────────────────────────────────────────────┘
```

### 故障隔离规则

1. **驱动层故障不影响系统**：LCD 初始化失败 → 日志告警，系统继续运行。蜂鸣器/WS2812 初始化失败 → 静默跳过，对应功能禁用。
2. **服务层故障不影响其他服务**：WiFi 连接失败 → Buddy 回退 SLEEP 状态，番茄钟不受影响。BLE 断开 → 设备回到本地模式。
3. **界面模块互不依赖**：每个界面自包含，UI manager 只通过回调接口交互。
4. **任务间松耦合**：任务间只通过 FreeRTOS 队列/事件组通信，不共享可变状态。
5. **所有模块初始化返回错误码**：`app_main` 检查返回值，非致命错误记录日志继续，仅致命错误（NVS 失败）halt。

---

## 2. 服务层重构

### 目录结构

```
service/
├── storage_service.c/h    # NVS 封装（保持现有接口）
├── wifi_service.c/h       # WiFi STA（精简，原 wifi_manager）
├── time_service.c/h       # 时间服务（吸收 NTP，统一入口）
└── ble_service.c/h        # BLE 桥接（新增）
```

### wifi_service

由 `wifi_manager` 精简而来，移除 HTTP 服务器、SNTP 同步、时区管理。只负责：

- 扫描：`wifi_service_scan()` → 结果缓存到内部，通过回调通知
- 连接：`wifi_service_connect(ssid, password)` → 保存凭证到 NVS → 连接 → 回调通知
- 断开：`wifi_service_disconnect()`
- 状态查询：`wifi_service_is_connected()`、`wifi_service_get_ip()`
- 自动重连：指数退避（2s → 4s → 8s → ... → 60s），通过回调通知连接/断开事件

回调接口：

```c
typedef struct {
    void (*on_connected)(const char *ip);
    void (*on_disconnected)(void);
    void (*on_scan_done)(int count);
    void (*on_connect_failed)(void);
} wifi_callbacks_t;

void wifi_service_register_callbacks(const wifi_callbacks_t *cbs);
```

### time_service

吸收 `wifi_manager_sync_time()` 中的 NTP 逻辑，统一管理 SNTP：

- `time_service_init()`：加载保存的时区和同步间隔
- `time_service_request_sync()`：触发一次 SNTP 同步（由 wifi 连接回调触发）
- `time_service_set_timezone(int offset)`：设置时区，持久化到 NVS
- `time_service_set_sync_interval(int minutes)`：设置同步间隔
- `time_service_sync_tick()`：在 Service 任务中定期调用，检查是否需要重同步
- `time_service_format_time(char *buf, size_t len)`：格式化时间
- `time_service_format_date(char *buf, size_t len)`：格式化日期

移除 `wifi_manager` 中所有 SNTP 相关代码。

### ble_service

新增，实现 Nordic UART Service（NUS）BLE 桥接：

- 广播名：`Claude-Buddy-XXXX`（MAC 末 2 字节十六进制）
- NUS UUID：标准 Nordic UART Service
- 接收：行缓冲 JSON 解析 → 解析 heartbeat/prompt/turn events → 通过回调通知
- 发送：构造 JSON（permission response, status ack, name ack）
- 连接管理：自动广播、配对、断开后重新广播

回调接口：

```c
typedef struct {
    void (*on_connected)(void);
    void (*on_disconnected)(void);
    void (*on_heartbeat)(const char *msg, int running, int waiting,
                         const char *prompt_id, const char *prompt_tool, const char *prompt_hint);
    void (*on_prompt)(const char *id, const char *tool, const char *hint);
} ble_callbacks_t;

void ble_service_register_callbacks(const ble_callbacks_t *cbs);
```

### storage_service

保持现有接口，新增 buddy 相关键：

- `buddy_species`（int）：当前宠物选择
- `buddy_approved`（int）：审批通过计数
- `buddy_denied`（int）：拒绝计数

---

## 3. 驱动层

```
driver/
├── st7789_lcd.c/h    # 不改动
├── buzzer.c/h        # 不改动
└── ws2812.c/h        # 新增：GPIO8 单颗 RGB LED
```

ws2812 使用 ESP-IDF RMT 外设驱动，接口：

```c
int ws2812_init(void);
void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b);
void ws2812_off(void);
```

Buddy 模块通过 `ws2812_set_buddy_state(state)` 间接控制（在 buddy.c 中封装）。

---

## 4. Buddy 模块

```
buddy/
├── buddy.c/h              # 状态机 + 动画调度 + 统计
├── buddy_ble.c/h          # BLE 业务协议（JSON 解析/构造）
└── buddy_chars.c/h        # ASCII 宠物图形数据
```

### 状态机

7 种状态：

| 状态 | 触发 | 显示 |
|------|------|------|
| SLEEP | BLE 未连接 | 闭眼、缓慢呼吸动画 |
| IDLE | BLE 连接，无活跃会话 | 眨眼、环顾动画 |
| BUSY | 有会话正在运行 | 出汗、工作动画 |
| ATTENTION | 有权限审批等待 | 警觉 + LED 闪烁 |
| CELEBRATE | 审批完成（短暂） | 庆祝动画（3s 后回 IDLE） |
| DIZZY | SET 键长按触发 | 螺旋眼、摇晃（3s 后回原状态） |
| HEART | 5 秒内快速审批（可选后续迭代） | 爱心动画（3s 后回 IDLE） |

状态转换规则：

```
SLEEP ──(BLE connected)──→ IDLE
IDLE  ──(sessions running)──→ BUSY
IDLE/BUSY ──(permission pending)──→ ATTENTION
ATTENTION ──(approved/denied)──→ CELEBRATE → IDLE
ATTENTION ──(timeout 30s)──→ IDLE
ANY   ──(BLE disconnected)──→ SLEEP
ANY   ──(SET long press)──→ DIZZY → 恢复原状态
```

### 权限审批交互

Buddy 界面在 ATTENTION 状态下显示审批界面：

- 宠物动画（上方）
- 工具名称和 hint（中间）
- Approve / Deny 选项列表（下方）
- 编码器 CW/CCW：滚动选择
- SET 键：确认选择
- 编码器短按：无操作

### Buddy 界面交互

| 操作 | 正常模式 | 信息页 |
|------|---------|--------|
| 编码器 CW/CCW | 滚动选择 Approve/Deny（ATTENTION 时） | 滚动查看统计 |
| 编码器短按 | 进入信息页 | 返回正常模式 |
| SET 键短按 | 确认审批选择 | 返回正常模式 |
| SET 键长按 | 触发 DIZZY | 触发 DIZZY |

### ASCII 宠物

初始版本包含 4 种宠物（后续可扩展），每种有 7 种状态的动画帧。宠物选择通过信息页进入，持久化到 NVS。

宠物数据结构：

```c
typedef struct {
    const char *name;
    const char *personality;
    const char *frames[BUDDY_STATE_COUNT][MAX_FRAMES];
    uint8_t frame_count[BUDDY_STATE_COUNT];
} buddy_species_t;
```

### 统计数据

持久化到 NVS，在信息页显示：

- 审批通过次数
- 拒绝次数
- 当前宠物名称和个性
- BLE 连接状态

---

## 5. UI 层

### ui_manager 精简

移除所有透传函数，只保留：

```c
void ui_init(void);
void ui_switch_screen(ui_screen_id_t screen_id);
ui_screen_id_t ui_get_current_screen(void);
void ui_register_input_callbacks(ui_screen_id_t screen, const ui_input_callbacks_t *cbs);
void ui_dispatch_encoder_cw(void);
void ui_dispatch_encoder_ccw(void);
void ui_dispatch_encoder_press(void);
void ui_dispatch_encoder_long_press(void);
void ui_dispatch_settings_press(void);
void lvgl_lock(void);
void lvgl_unlock(void);
```

数据更新由 `ui_update_task` 直接调用各界面的更新函数，如 `ui_screen_main_update_time()`、`ui_screen_pomodoro_update_state()`。

### 界面列表

```
UI_SCREEN_MAIN             # 主界面
UI_SCREEN_POMODORO         # 番茄钟
UI_SCREEN_BUDDY            # 替代 CHAT
UI_SCREEN_SETTINGS         # 设置
UI_SCREEN_SETTINGS_POMODORO # 番茄钟设置
UI_SCREEN_WIFI_LIST        # WiFi 列表
UI_SCREEN_PASSWORD_INPUT   # 密码输入
```

### 界面修复

- **密码输入界面**：补充回调注册（当前为 NULL），编码器/SET 键可操作虚拟键盘
- **设置界面**：亮度/对比度实际生效（调用 LCD 驱动），番茄钟设置值正确写入 engine
- **主界面**：温湿度显示占位符（"--" 或 "N/A"）

### 屏幕导航

```
MAIN ←→ POMODORO ←→ BUDDY
  ↕                        ↕
SETTINGS_POMODORO ← SETTINGS → WIFI_LIST → PASSWORD_INPUT
```

---

## 6. 任务模型

| 任务 | 优先级 | 栈 | 职责 |
|------|--------|----|------|
| LVGL | 5 | 8KB | `lv_timer_handler()` 循环，不改动 |
| Input | 3 | 6KB | 读取队列 → `lvgl_lock` → `ui_dispatch_*`，不改动 |
| Service | 2 | 6KB | WiFi 回调处理、NTP 同步、Buddy 状态刷新、WS2812 动画、BLE 接收 |
| UIUpdate | 1 | 4KB | 番茄钟 tick（1s）、主界面时间刷新 |

UIUpdate 移除 WiFi 轮询和 NTP 逻辑，改由 Service 任务通过回调处理。

### main.c 初始化流程

```
1. nvs_flash_init()           → 致命：失败则 halt
2. storage_service_init()     → 致命：失败则 halt
3. buzzer_init()              → 非致命：失败跳过
4. st7789_lcd_init()          → 非致命：失败跳过（无显示）
5. ws2812_init()              → 非致命：失败跳过
6. lvgl_init()                → 依赖 LCD 成功
7. ui_init()                  → 依赖 LVGL 成功
8. pomodoro_engine_init()     → 非致命
9. buddy_init()               → 非致命
10. input_handler_init()      → 非致命
11. wifi_service_init()       → 非致命
12. ble_service_init()        → 非致命
13. 注册回调（接线）
14. 创建任务
```

### 回调接线（在 main.c 中集中定义）

```c
// WiFi → time_service + UI
static void on_wifi_connected(const char *ip) {
    time_service_request_sync();
    lvgl_lock();
    ui_screen_main_update_wifi(ip, true);
    lvgl_unlock();
}

static void on_wifi_disconnected(void) {
    lvgl_lock();
    ui_screen_main_update_wifi(NULL, false);
    lvgl_unlock();
}

// BLE → buddy + ws2812 + UI
static void on_ble_heartbeat(const char *msg, int running, int waiting,
                             const char *prompt_id, const char *tool, const char *hint) {
    buddy_update_heartbeat(running, waiting, prompt_id, tool, hint);
}

static void on_buddy_state_changed(buddy_state_t state) {
    ws2812_set_buddy_state(state);
    if (state == BUDDY_ATTENTION) {
        lvgl_lock();
        ui_switch_screen(UI_SCREEN_BUDDY);
        lvgl_unlock();
    }
}
```

---

## 7. 删除清单

| 删除项 | 原因 |
|--------|------|
| `components/esp_lcd_st7789/` | 未使用的组件，代码用自定义 SPI 驱动 |
| `main/network/` 目录 | 重构为 `main/service/wifi_service.c` |
| `main/ui/ui_screen_chat.c/h` | 替代为 `ui_screen_buddy.c` |
| `AGENTS.md` | CLAUDE.md 已涵盖 |
| `docs/requirements.md` | 内容并入架构文档 |
| `docs/functional_spec.md` | 过时，删除 |
| `docs/sw_arch.md` | 过时，删除 |
| `docs/hw_arch.md` | 过时，删除 |
| `docs/pomodoro_design.md` | 过时，删除 |
| `docs/network_design.md` | 过时，删除 |
| `reference/` 目录 | LCD 参考手册，已无参考价值 |

## 8. 新增文档

| 文件 | 内容 |
|------|------|
| `docs/architecture.md` | 总架构：模块图、任务模型、数据流、回调接线 |
| `docs/ble_buddy_design.md` | BLE 协议 + Buddy 状态机 + 宠物数据结构 |

## 9. 更新文档

| 文件 | 更新内容 |
|------|---------|
| `CLAUDE.md` | 反映重构后架构、新模块、新 GPIO、新任务模型 |
| `README.md` | 精简为项目简介、硬件连接、构建命令、功能列表 |
