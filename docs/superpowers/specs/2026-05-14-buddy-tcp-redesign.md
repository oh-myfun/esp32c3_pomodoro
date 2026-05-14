# Buddy 系统重构设计：角色移植 + TCP 通信

## 概述

将 claude-desktop-buddy 项目的 18 个 ASCII 宠物角色移植到 ESP32 番茄钟项目，同时将通信协议从 BLE 替换为 TCP，对接 claude-code-buddy-bridge 后端。

方案选择：精简精灵格式（5×12），先移植全部 18 个物种快速上线，后续可按需扩展为精细版本。

## Part 1：角色系统重构

### 数据结构

`BUDDY_FRAME_LINES` 从 12 改为 5，`MAX_ANIM_FRAMES` 从 4 改为 10，新增 `BUDDY_FRAME_COLS = 12`。

```c
typedef struct {
    const char *name;
    uint16_t body_color;   // 每物种独立 body 颜色
    const char *const (*state_frames[7])[10][5];
    uint8_t frame_count[7];
} buddy_species_t;
```

### 7 状态

SLEEP, IDLE, BUSY, ATTENTION, CELEBRATE, DIZZY, HEART（与源项目一致）。

### 18 个物种及 body_color

| 物种 | body_color | 颜色 |
|------|-----------|------|
| capybara | 0xC2A6 | 暖棕 |
| duck | 0xFFE0 | 黄 |
| goose | 0xFFE0 | 黄 |
| blob | 0x07F0 | 翠绿 |
| cat | 0xC2A6 | 暖棕 |
| dragon | 0xF800 | 红 |
| octopus | 0xA01F | 紫 |
| owl | TBD | - |
| penguin | 0x041F | 蓝 |
| turtle | TBD | - |
| snail | TBD | - |
| ghost | 0xFFFF | 白 |
| axolotl | 0xFB1E | 粉 |
| cactus | TBD | - |
| robot | 0xC618 | 橙 |
| rabbit | TBD | - |
| mushroom | TBD | - |
| chonk | TBD | - |

TBD 的物种颜色待从源项目源码中提取。

### 动画帧数

idle/busy: 6-10 帧，sleep/attention/celebrate/dizzy/heart: 3-6 帧。帧序列按源项目的 SEQ 数组定义循环顺序。

### 状态颜色映射

保留当前项目的状态颜色（非 body_color）：
- SLEEP: 0x888888 灰
- IDLE: 0x00FF00 绿
- BUSY: 0x4D96FF 蓝
- ATTENTION: 0xFF4444 红
- CELEBRATE: 0xFFFF00 黄
- DIZZY: 0xFF00FF 品红
- HEART: 0xFF6B8A 粉

## Part 2：TCP 服务

### 新增文件

- `service/tcp_service.c/h` — TCP client，完全替代 `ble_service.c/h`

### 连接流程

```
WiFi 连接成功 → DNS 解析 bridge → TCP connect → 发送 {"type":"hello","data":{}} →
收到 waiting_pairing → 用户输入 pairing code → 发送 {"type":"pair","data":{"pairing_code":"..."}} →
收到 paired → 等待 request
```

### 配置

- `tcp_host`：bridge 地址（默认 `192.168.1.100`，NVS 存储）
- `tcp_port`：端口号（默认 `9876`）

### TCP 消息协议（JSON Lines，与 bridge 一致）

**发送**：

| type | data | 说明 |
|------|------|------|
| hello | {} | 连接后立即发送 |
| pair | {"pairing_code":"..."} | 配对请求 |
| decision | {"behavior":"allow/deny","ccbb_request_id":"..."} | 权限决策（Allow/Deny） |
| decision | {"behavior":"allow","ccbb_request_id":"...","updatedInput":{"questions":[...],"answers":{...}}} | AskUserQuestion 单选/多选响应 |

**接收**：

| type | data | 说明 |
|------|------|------|
| waiting_pairing | {"message":"..."} | 等待配对 |
| paired | {"pairing_code":"...","session_id":"..."} | 配对成功 |
| pairing_pending | {"pairing_code":"..."} | 配对码已提交但 session 未就绪 |
| pairing_failed | {"reason":"..."} | 配对失败 |
| request | {完整 CC 事件 + ccbb_request_id} | 权限请求推送 |
| done | {"id":"...","decision":"..."} | 决策已处理 |
| session_end | {"session_id":"..."} | 会话结束 |

### 内部实现

- FreeRTOS 任务（优先级 2），循环 recv + JSON 行解析
- WiFi 断开自动断开 TCP，WiFi 重连后自动重连
- TCP keepalive（idle=5s, interval=3s）

### 回调接口

```c
typedef struct {
    void (*on_connected)(void);
    void (*on_disconnected)(void);
    void (*on_request)(const tcp_request_t *req);
    void (*on_session_end)(void);
} tcp_callbacks_t;
```

## Part 2.5：Bridge 配置页面与页面栈

### 页面栈（ui_manager 新增）

当前项目无页面栈，`ui_switch_screen()` 是直接跳转，返回目标硬编码。新增页面栈支持多入口页面的正确返回。

```c
#define UI_NAV_STACK_SIZE 8

void ui_switch_screen(ui_screen_id_t id);  // 现有接口不变，内部增加压栈逻辑
void ui_go_back(void);                      // 新增：弹栈返回上一页
```

- `ui_switch_screen()` 跳转前将当前页面压入 `nav_stack`
- `ui_go_back()` 弹出栈顶，跳转到上一页面
- 现有硬编码返回调用逐步替换为 `ui_go_back()`

### Bridge 配置页面（新增 `ui_screen_settings_bridge.c`）

**入口**：
1. Buddy 界面按编码器按键进入（任意状态均可）
2. 设置主界面选择 "Buddy Bridge" 项进入

**操作**（与项目其他页面一致）：
- 编码器旋转：选择配置项 / 修改值
- **编码器按键：返回上一页**（`ui_go_back()`）
- **SET 键：确认/执行**（保存配置、触发扫描、提交配对码）

**配置项**：
- Bridge 地址（手动输入或从扫描结果选择）
- 端口号（默认 9876）
- 配对码输入
- "扫描局域网" 操作项（SET 键触发，搜索局域网中 9876 端口的 bridge 服务）
- 连接/断开操作

**配置存储**：NVS namespace "tcp"，keys: "host", "port", "pairing_code"

## Part 3：Buddy 状态机适配

### 状态驱动（TCP 事件驱动）

| 事件 | 状态转换 |
|------|---------|
| tcp_connected | SLEEP → IDLE |
| tcp_request_received | IDLE → ATTENTION |
| user_approve | ATTENTION → CELEBRATE（发 allow，3s → IDLE） |
| user_deny | ATTENTION → DIZZY（发 deny，3s → IDLE） |
| user_submit_answer | ATTENTION → CELEBRATE（发 allow + updatedInput，3s → IDLE） |
| tcp_disconnected | 任意 → SLEEP |
| tcp_session_end | → IDLE |
| SET 键（仅 IDLE/SLEEP 时） | → 随机 CELEBRATE/DIZZY/HEART（3s → 原状态） |
| 编码器按键（任意状态） | → 进入 Bridge 配置页面 |

### 请求类型与响应格式

设备通过 TCP 接收的请求分为两类：**权限请求**（Bash/Write/Edit/MCP 等工具调用）和 **AskUserQuestion**（单选/多选）。

#### 权限请求

请求中 `tool_name` 为 `Bash`、`Write`、`Edit`、MCP 工具等。设备显示工具名 + hint，用户选择 Allow 或 Deny。

响应格式：
```json
{"behavior": "allow"}
```
或
```json
{"behavior": "deny"}
```

#### AskUserQuestion — 单选（multiSelect=false）

`tool_name` 为 `AskUserQuestion`，`tool_input.questions` 中只有一个 question 且 `multiSelect=false`。
设备显示问题文本，列出选项，用户选择一个。

响应格式：
```json
{
  "behavior": "allow",
  "updatedInput": {
    "questions": [...],
    "answers": {"问题文本": "选中的label"}
  }
}
```

#### AskUserQuestion — 多选（multiSelect=true）

`multiSelect=true`。设备显示问题文本，列出选项，用户可勾选多个。末尾追加 "✓ 提交" 选项作为确认提交动作。

响应格式：
```json
{
  "behavior": "allow",
  "updatedInput": {
    "questions": [...],
    "answers": {"问题文本": ["label1", "label2"]}
  }
}
```

#### 数据结构

```c
typedef enum {
    REQ_PERMISSION,     // 权限审批 (Allow/Deny)
    REQ_SINGLE_SELECT,  // 单选
    REQ_MULTI_SELECT,   // 多选
} request_type_t;

typedef struct {
    char ccbb_request_id[64];
    request_type_t type;
    char tool[32];          // 工具名
    char hint[128];         // 操作摘要
    char question[128];     // 问题文本 (AskUserQuestion)
    struct {
        char label[32];
        char description[64];
    } options[8];
    int option_count;
    int selected[8];        // 多选时已勾选的选项索引
    int selected_count;     // 单选/权限: 默认选中的索引
    // 原始 questions JSON 用于回填响应
    char questions_json[512];
} tcp_request_t;
```

### 默认选择

| 请求类型 | 默认选中 | SET 键行为 |
|---------|---------|-----------|
| 权限请求 | Allow（索引 0） | 直接提交 Allow |
| 单选 | 第一个选项 | 直接提交选中项 |
| 多选 | 无选中 | 勾选/取消勾选当前聚焦选项 |

### API 变更

**移除**：
- `buddy_on_heartbeat()`, `buddy_on_ble_connected()`, `buddy_on_ble_disconnected()`
- `buddy_trigger_dizzy()`

**新增**：
- `buddy_on_tcp_connected()`, `buddy_on_tcp_disconnected()`
- `buddy_on_tcp_request(const tcp_request_t *req)`
- `buddy_on_tcp_session_end()`
- `buddy_trigger_random()` — 仅 IDLE/SLEEP 状态可用，随机触发 CELEBRATE/DIZZY/HEART

### UI 输入适配（ui_screen_buddy.c）

| 输入 | MODE_NORMAL (任意状态) | MODE_ATTENTION |
|------|----------------------|----------------|
| CW | 切到设置页 | 聚焦下一项 |
| CCW | 切到番茄钟页 | 聚焦上一项 |
| 编码器按键 | 进入 Bridge 配置页 | 无操作（按 SET 确认当前聚焦项） |
| SET 键 | buddy_trigger_random()（仅 IDLE/SLEEP） | 提交默认/当前选择 |
| 长按 | 无 | 无 |

**MODE_ATTENTION 详细行为**：

权限请求时，选项列表为 `[Allow, Deny]`，默认聚焦 Allow。SET 键直接提交当前聚焦项。

单选时，选项列表为 question 的 options，默认聚焦第一项。SET 键提交选中项。

多选时，选项列表为 question 的 options + 末尾 `✓ 提交`，默认无选中。CW/CCW 移动聚焦。SET 键：
- 聚焦在普通选项上：勾选/取消勾选当前项
- 聚焦在 `✓ 提交` 上：提交当前勾选项（至少选 1 项才能提交）

### 连接状态图标

BLE 图标改为 TCP/WiFi 图标：已连接=绿色 ✓，断开=灰色 ✗。

### 文件清理

- 删除 `service/ble_service.c/h`
- CMakeLists.txt 移除 ble_service.c
- main.c 中用 tcp_service_init() 替换 ble_service 初始化
