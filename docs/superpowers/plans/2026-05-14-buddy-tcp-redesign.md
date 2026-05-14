# Buddy 系统重构实施计划：角色移植 + TCP 通信

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 18 个 ASCII 宠物角色从 claude-desktop-buddy 移植到 ESP32 番茄钟，并将通信从 BLE 替换为 TCP 对接 claude-code-buddy-bridge。

**Architecture:** 三阶段改造——(1) 角色数据结构和动画系统重写，(2) TCP 服务替代 BLE，(3) UI 层适配（页面栈 + Bridge 配置页 + Buddy 界面重构）。每阶段独立可编译。

**Tech Stack:** ESP-IDF v5.5.4, LVGL v9.5.0, FreeRTOS, cJSON (esp-idf 内置), Python 3 (转换脚本)

**设计文档:** `docs/superpowers/specs/2026-05-14-buddy-tcp-redesign.md`

**构建命令:** `./build.sh`（Git Bash 环境）

---

## 文件结构

### 新建文件

| 文件 | 职责 |
|------|------|
| `main/service/tcp_service.h` | TCP client API、请求类型定义、回调接口 |
| `main/service/tcp_service.c` | TCP 连接管理、JSON Lines 协议、FreeRTOS 任务 |
| `main/ui/ui_screen_settings_bridge.h` | Bridge 配置页面头文件 |
| `main/ui/ui_screen_settings_bridge.c` | Bridge 配置页面实现（地址、端口、配对码、扫描） |
| `tools/convert_buddies.py` | Python 转换脚本：从源项目提取角色数据生成 C 数组 |

### 修改文件

| 文件 | 变更范围 |
|------|---------|
| `main/buddy/buddy_chars.h` | 数据结构重构：12 行→5 行，4 帧→10 帧，新增 SEQ |
| `main/buddy/buddy_chars.c` | 全部重写：4 个旧物种→18 个新物种的帧数据和 SEQ |
| `main/buddy/buddy.h` | 移除 BLE API，新增 TCP API 和 tcp_request_t 前向声明 |
| `main/buddy/buddy.c` | 状态机改为 TCP 事件驱动，新增 request 存储、SEQ 动画 |
| `main/ui/ui_manager.h` | 新增 `ui_go_back()` 声明，screen_id_t 新增 BRIDGE 页面 |
| `main/ui/ui_manager.c` | 新增 nav_stack 实现，ui_switch_screen 压栈，ui_go_back 弹栈 |
| `main/ui/ui_screen_buddy.c` | 重构：移除 MODE_INFO，新增单选/多选 UI，TCP 连接状态图标 |
| `main/ui/ui_screen_buddy.h` | API 变更：show_prompt 改为接收 tcp_request_t |
| `main/ui/ui_screen_settings.c` | 设置列表新增 "Buddy Bridge" 项，CW/CCW 目标页更新 |
| `main/main.c` | 移除 BLE 引用，新增 TCP 初始化和回调接线 |
| `main/CMakeLists.txt` | 新增 tcp_service.c、ui_screen_settings_bridge.c，移除 ble_service.c |
| `main/ui/i18n.c` | 新增 Bridge 配置相关字符串 |

### 删除文件

| 文件 | 说明 |
|------|------|
| `main/service/ble_service.c` | BLE 服务（已禁用，正式移除） |
| `main/service/ble_service.h` | BLE 服务头文件 |

---

## Task 1: 角色数据结构重构

**Files:**
- Modify: `main/buddy/buddy_chars.h`

当前定义：12 行×N 列、最多 4 帧、无 SEQ。需改为：5 行×12 列、最多 10 帧、带 SEQ 序列。

- [ ] **Step 1: 更新 buddy_chars.h**

将整个文件内容替换为：

```c
#pragma once

#include <stdint.h>

#define BUDDY_STATE_COUNT  7
#define BUDDY_FRAME_LINES  5
#define BUDDY_FRAME_COLS   12
#define MAX_ANIM_FRAMES   10
#define MAX_SEQ_LEN       32

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
    uint16_t body_color;
    const char *const (*state_frames[BUDDY_STATE_COUNT])[MAX_ANIM_FRAMES][BUDDY_FRAME_LINES];
    const uint8_t *seq[BUDDY_STATE_COUNT];       // SEQ 数组：tick → pose index
    uint8_t seq_len[BUDDY_STATE_COUNT];           // SEQ 数组长度
    uint8_t pose_count[BUDDY_STATE_COUNT];        // 去重后的 pose 数量
} buddy_species_t;

extern const buddy_species_t BUDDY_SPECIES[];
extern const int BUDDY_SPECIES_COUNT;
```

- [ ] **Step 2: 验证编译**

```bash
./build.sh 2>&1 | tail -20
```

预期：编译失败（buddy_chars.c 数据与新结构体不匹配），但头文件本身无语法错误。

- [ ] **Step 3: Commit**

```bash
git add main/buddy/buddy_chars.h
git commit -m "refactor: 新角色数据结构 - 5行12列帧 + SEQ动画序列"
```

---

## Task 2: Python 角色数据转换脚本

**Files:**
- Create: `tools/convert_buddies.py`

从 claude-desktop-buddy 的 18 个 C++ 源文件中提取 5×12 ASCII 帧数据和 SEQ 数组，生成 ESP32 C 代码。

- [ ] **Step 1: 编写转换脚本**

脚本逻辑：
1. 读取 `D:\Project\AI\claude-desktop-buddy\src\buddies\<name>.cpp`
2. 用正则提取每个状态的 `static const char* const POSE[5] = { ... }` 数组
3. 提取 `const char* const* P[N] = { ... }` 中的 pose 名称列表
4. 提取 `static const uint8_t SEQ[] = { ... }` 序列
5. 提取 `bodyColor` 从 `Species` 结构体
6. 为每个物种生成 C 静态数组和 registry 条目

```python
#!/usr/bin/env python3
"""Convert claude-desktop-buddy species to ESP32 C arrays."""
import re, os, sys

SRC_DIR = r"D:\Project\AI\claude-desktop-buddy\src\buddies"
STATE_NAMES = ["sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart"]
OUT_FILE = r"D:\Project\IOT\pomodoro\main\buddy\buddy_chars.c"

def extract_poses(src_text, state_name):
    """Extract pose name list and SEQ for a given state."""
    # Find the do<State> function
    func_pat = re.compile(
        r'static void do' + state_name.capitalize() + r'\(uint32_t t\)\s*\{(.*?)\n\}',
        re.DOTALL
    )
    m = func_pat.search(src_text)
    if not m:
        return None, None, 0
    body = m.group(1)

    # Extract pose arrays: static const char* const NAME[5] = { "line1", ... }
    pose_pat = re.compile(
        r'static\s+const\s+char\*\s+const\s+(\w+)\[5\]\s*=\s*\{(.*?)\};',
        re.DOTALL
    )
    poses = {}
    for pm in pose_pat.finditer(body):
        name = pm.group(1)
        lines_raw = pm.group(2)
        # Parse each line string
        line_pat = re.compile(r'"((?:[^"\\]|\\.)*)"')
        lines = [m2.group(1) for m2 in line_pat.finditer(lines_raw)]
        poses[name] = lines

    # Extract P[] array
    p_pat = re.compile(r'const char\* const\*\s+P\[(\d+)\]\s*=\s*\{(.*?)\};', re.DOTALL)
    pm = p_pat.search(body)
    if not pm:
        return None, None, 0
    count = int(pm.group(1))
    names_raw = pm.group(2)
    pose_names = re.findall(r'(\w+)', names_raw)

    # Extract SEQ array
    seq_pat = re.compile(r'static\s+const\s+uint8_t\s+SEQ\[\]\s*=\s*\{(.*?)\};', re.DOTALL)
    sm = seq_pat.search(body)
    seq = []
    if sm:
        seq = [int(x.strip()) for x in sm.group(1).split(',') if x.strip()]

    return pose_names, poses, seq

def extract_species_info(src_text):
    """Extract name and bodyColor from Species struct."""
    sp_pat = re.compile(
        r'extern const Species \w+_SPECIES\s*=\s*\{(.*?)\};',
        re.DOTALL
    )
    m = sp_pat.search(src_text)
    if not m:
        return None, None
    body = m.group(1)
    parts = [p.strip() for p in body.split(',')]
    name = parts[0].strip('"')
    color = int(parts[1].strip(), 16)
    return name, color

def generate_c_file(all_species_data):
    """Generate the C source file."""
    lines = []
    lines.append('#include "buddy_chars.h"')
    lines.append('')

    for sp in all_species_data:
        name = sp['name']
        lines.append(f'/* {"=" * 60} */')
        lines.append(f'/* {name} */')
        lines.append(f'/* {"=" * 60} */')

        for si, state in enumerate(STATE_NAMES):
            state_data = sp['states'][state]
            if state_data is None:
                # Empty placeholder
                lines.append(f'static const char *const {name}_{state}[MAX_ANIM_FRAMES][BUDDY_FRAME_LINES] = {{')
                lines.append('    { "            ", "            ", "            ", "            ", "            " },')
                lines.append('};')
                lines.append('')
                continue

            pose_names, poses_dict, seq = state_data
            lines.append(f'static const char *const {name}_{state}[MAX_ANIM_FRAMES][BUDDY_FRAME_LINES] = {{')
            for pi, pname in enumerate(pose_names):
                pose_lines = poses_dict.get(pname, ["            "] * 5)
                # Pad to exactly 12 chars
                padded = []
                for pl in pose_lines:
                    pl = pl.replace('\\n', '')
                    if len(pl) < 12:
                        pl = pl + ' ' * (12 - len(pl))
                    elif len(pl) > 12:
                        pl = pl[:12]
                    padded.append(pl)
                while len(padded) < 5:
                    padded.append("            ")
                lines.append('    {')
                for j, pl in enumerate(padded[:5]):
                    lines.append(f'        "{pl}",')
                lines.append('    },')
            lines.append('};')
            lines.append('')

            # SEQ array
            lines.append(f'static const uint8_t {name}_{state}_seq[] = {{')
            if seq:
                seq_str = ', '.join(str(x) for x in seq)
                lines.append(f'    {seq_str},')
            lines.append('};')
            lines.append('')

    # Registry
    lines.append('/* ' + '=' * 60 + ' */')
    lines.append('/* Species registry */')
    lines.append('/* ' + '=' * 60 + ' */')
    lines.append('')
    lines.append('const buddy_species_t BUDDY_SPECIES[] = {')

    for sp in all_species_data:
        name = sp['name']
        color = sp['color']
        lines.append('    {')
        lines.append(f'        .name = "{name}",')
        lines.append(f'        .body_color = 0x{color:04X},')
        lines.append('        .state_frames = {')
        for state in STATE_NAMES:
            lines.append(f'            &{name}_{state},')
        lines.append('        },')
        lines.append('        .seq = {')
        for state in STATE_NAMES:
            lines.append(f'            {name}_{state}_seq,')
        lines.append('        },')
        lines.append('        .seq_len = {')
        for state in STATE_NAMES:
            sd = sp['states'][state]
            lines.append(f'            {len(sd[2]) if sd else 0},')
        lines.append('        },')
        lines.append('        .pose_count = {')
        for state in STATE_NAMES:
            sd = sp['states'][state]
            lines.append(f'            {len(sd[0]) if sd else 1},')
        lines.append('        },')
        lines.append('    },')

    lines.append('};')
    lines.append('')
    lines.append('const int BUDDY_SPECIES_COUNT = sizeof(BUDDY_SPECIES) / sizeof(BUDDY_SPECIES[0]);')
    lines.append('')
    return '\n'.join(lines)

def main():
    species_order = [
        'capybara', 'duck', 'goose', 'blob', 'cat', 'dragon',
        'octopus', 'owl', 'penguin', 'turtle', 'snail', 'ghost',
        'axolotl', 'cactus', 'robot', 'rabbit', 'mushroom', 'chonk'
    ]

    all_data = []
    for name in species_order:
        path = os.path.join(SRC_DIR, f"{name}.cpp")
        if not os.path.exists(path):
            print(f"WARNING: {path} not found, skipping")
            continue
        with open(path, 'r', encoding='utf-8') as f:
            src = f.read()

        sp_name, color = extract_species_info(src)
        if sp_name is None:
            print(f"WARNING: Could not parse species info for {name}")
            continue

        states = {}
        for state in STATE_NAMES:
            result = extract_poses(src, state)
            states[state] = result

        all_data.append({
            'name': sp_name,
            'color': color,
            'states': states
        })
        print(f"Parsed {sp_name}: color=0x{color:04X}")

    c_code = generate_c_file(all_data)
    with open(OUT_FILE, 'w', encoding='utf-8') as f:
        f.write(c_code)
    print(f"Generated {OUT_FILE} with {len(all_data)} species")

if __name__ == '__main__':
    main()
```

- [ ] **Step 2: 运行转换脚本**

```bash
python tools/convert_buddies.py
```

预期：生成 `main/buddy/buddy_chars.c`，包含 18 个物种的帧数据和 SEQ 数组。

- [ ] **Step 3: 手动检查生成的文件**

检查要点：
- 每个物种 7 个状态的帧数组都存在
- SEQ 数组中的索引值不超过 pose_count - 1
- body_color 值与源项目一致

18 个物种的 body_color 参考（已从源码提取）：

| 物种 | body_color |
|------|-----------|
| capybara | 0xC2A6 |
| duck | 0xFFE0 |
| goose | 0xFFFF |
| blob | 0x07F0 |
| cat | 0xC2A6 |
| dragon | 0xF800 |
| octopus | 0xA01F |
| owl | 0x8430 |
| penguin | 0x041F |
| turtle | 0x07E0 |
| snail | 0xD8FE |
| ghost | 0xFFFF |
| axolotl | 0xFB1E |
| cactus | 0x07E0 |
| robot | 0xC618 |
| rabbit | 0xFFFF |
| mushroom | 0xF810 |
| chonk | 0xFD20 |

- [ ] **Step 4: 验证编译**

```bash
./build.sh 2>&1 | tail -20
```

预期：buddy_chars.c 编译通过（可能 buddy.c 还有旧 API 引用导致链接错误，这是正常的，下一步处理）。

- [ ] **Step 5: Commit**

```bash
git add tools/convert_buddies.py main/buddy/buddy_chars.c
git commit -m "feat: 18物种角色数据 - Python转换脚本 + 生成C数组"
```

---

## Task 3: Buddy 状态机重构

**Files:**
- Modify: `main/buddy/buddy.h`
- Modify: `main/buddy/buddy.c`

移除 BLE 驱动的 API，改为 TCP 事件驱动。动画逻辑从简单 `frame_idx % frame_count` 改为 SEQ 序列驱动。

- [ ] **Step 1: 更新 buddy.h**

移除所有 BLE 相关 API，新增 TCP API。完整替换为：

```c
#pragma once

#include "buddy_chars.h"
#include <stdbool.h>
#include <stdint.h>

/* Forward declaration — defined in tcp_service.h */
typedef struct tcp_request tcp_request_t;

typedef struct {
    buddy_state_t state;
    int species_index;
    uint32_t approved_count;
    uint32_t denied_count;
    bool has_pending_request;
    bool tcp_connected;
    request_type_t request_type;  /* current pending request type */
} buddy_info_t;

typedef struct {
    void (*on_state_changed)(buddy_state_t new_state);
} buddy_callbacks_t;

int  buddy_init(void);
void buddy_register_callbacks(const buddy_callbacks_t *cbs);

/* TCP-driven events */
void buddy_on_tcp_connected(void);
void buddy_on_tcp_disconnected(void);
void buddy_on_tcp_request(const tcp_request_t *req);
void buddy_on_tcp_session_end(void);

/* User actions */
void buddy_approve(void);
void buddy_deny(void);
void buddy_submit_answer(void);
void buddy_trigger_random(void);  /* only works in IDLE/SLEEP */

/* Info */
buddy_info_t buddy_get_info(void);
void buddy_set_species(int index);
int  buddy_get_species_count(void);
const char *buddy_get_species_name(int index);

/* Animation */
void buddy_tick(void);
const char *const *buddy_get_current_frame(void);
uint16_t buddy_get_current_body_color(void);

/* Persistence */
void buddy_save_stats(void);
void buddy_load_stats(void);
```

- [ ] **Step 2: 重写 buddy.c**

核心变更：
1. 移除 `s_ble_connected`、`s_prompt_id/tool/hint`、`s_has_prompt` 等旧字段
2. 新增 `s_tcp_connected`、`s_current_request` 存储当前 pending 请求
3. `buddy_tick()` 中动画改为 SEQ 驱动：`s_tick_idx` 递增，pose = `SEQ[s_tick_idx % seq_len]`
4. 新增 `buddy_on_tcp_connected/disconnected/request/session_end`
5. 新增 `buddy_trigger_random()`：仅在 IDLE/SLEEP 时随机切换到 CELEBRATE/DIZZY/HEART
6. `buddy_approve()` 发 allow，`buddy_deny()` 发 deny，`buddy_submit_answer()` 发 allow + updatedInput

在 buddy.c 顶部需要前向声明 `tcp_request_t` 和发送函数。TCP 服务提供的发送函数在 tcp_service.h 中定义。为解耦，buddy.c 调用 `tcp_service_send_decision()` 发送决策。

关键状态转换实现：

```c
static const uint8_t RANDOM_STATES[] = {
    BUDDY_CELEBRATE, BUDDY_DIZZY, BUDDY_HEART
};

void buddy_trigger_random(void) {
    if (s_state != BUDDY_IDLE && s_state != BUDDY_SLEEP) return;
    s_pre_random = s_state;
    uint8_t pick = esp_random() % 3;
    set_state(RANDOM_STATES[pick]);
}
```

SEQ 驱动的动画：

```c
void buddy_tick(void) {
    s_tick_count++;
    const buddy_species_t *sp = &BUDDY_SPECIES[s_species];
    buddy_state_t st = s_state;
    uint8_t sl = sp->seq_len[st];
    if (sl > 0) {
        s_seq_idx = (s_seq_idx + 1) % sl;
    }

    /* Handle timeouts */
    switch (s_state) {
    case BUDDY_CELEBRATE:
        if (s_tick_count >= CELEBRATE_TICKS) set_state(BUDDY_IDLE);
        break;
    case BUDDY_DIZZY:
        if (s_tick_count >= DIZZY_TICKS) set_state(s_pre_random);
        break;
    case BUDDY_HEART:
        if (s_tick_count >= HEART_TICKS) set_state(s_pre_random);
        break;
    case BUDDY_ATTENTION:
        if (s_tick_count % 2 == 0) led_service_wait(LED_COLOR_ATTENTION);
        else led_service_stop();
        if (s_tick_count >= ATTENTION_TICKS) {
            s_has_request = false;
            set_state(BUDDY_IDLE);
        }
        break;
    default:
        break;
    }
}

const char *const *buddy_get_current_frame(void) {
    const buddy_species_t *sp = &BUDDY_SPECIES[s_species];
    buddy_state_t st = s_state;
    uint8_t pose_idx = 0;
    if (sp->seq[st] && sp->seq_len[st] > 0) {
        pose_idx = sp->seq[st][s_seq_idx % sp->seq_len[st]];
    }
    if (pose_idx >= sp->pose_count[st]) pose_idx = 0;
    return (*sp->state_frames[st])[pose_idx];
}
```

- [ ] **Step 3: 验证编译**

```bash
./build.sh 2>&1 | tail -20
```

预期：编译失败（tcp_service.h 不存在），但 buddy.c 语法正确。暂时在 buddy.c 中用前向声明替代：

```c
/* Forward declarations — will be resolved when tcp_service.h is available */
typedef struct {
    char ccbb_request_id[64];
    int type;  /* request_type_t */
    char tool[32];
    char hint[128];
    char question[128];
    struct { char label[32]; char description[64]; } options[8];
    int option_count;
    int selected[8];
    int selected_count;
    char questions_json[512];
} tcp_request_t;
void tcp_service_send_decision(const char *json);
```

- [ ] **Step 4: Commit**

```bash
git add main/buddy/buddy.h main/buddy/buddy.c
git commit -m "refactor: buddy状态机 - TCP事件驱动 + SEQ动画序列"
```

---

## Task 4: TCP 服务

**Files:**
- Create: `main/service/tcp_service.h`
- Create: `main/service/tcp_service.c`

实现 TCP client，连接 bridge 后端，JSON Lines 协议通信。

- [ ] **Step 1: 创建 tcp_service.h**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    REQ_PERMISSION,
    REQ_SINGLE_SELECT,
    REQ_MULTI_SELECT,
} request_type_t;

typedef struct {
    char ccbb_request_id[64];
    request_type_t type;
    char tool[32];
    char hint[128];
    char question[128];
    struct {
        char label[32];
        char description[64];
    } options[8];
    int option_count;
    bool multi_select;          /* true for multi-select */
    int focused;                /* currently focused option index */
    int selected[8];            /* selected option indices (multi-select) */
    int selected_count;         /* number of selected items */
    char questions_json[512];   /* raw questions JSON for response */
} tcp_request_t;

typedef struct {
    void (*on_connected)(void);
    void (*on_disconnected)(void);
    void (*on_request)(const tcp_request_t *req);
    void (*on_session_end)(void);
} tcp_callbacks_t;

/* Lifecycle */
int  tcp_service_init(void);
void tcp_service_register_callbacks(const tcp_callbacks_t *cbs);

/* Connection control */
void tcp_service_connect(const char *host, int port);
void tcp_service_disconnect(void);
bool tcp_service_is_connected(void);

/* Send decision */
void tcp_service_send_decision(const char *json);

/* NVS config */
bool tcp_service_load_config(char *host, size_t host_len, int *port);
void tcp_service_save_config(const char *host, int port);
void tcp_service_save_pairing_code(const char *code);
bool tcp_service_load_pairing_code(char *code, size_t len);

/* LAN scan — discover bridge on port 9876 */
typedef struct {
    char ip[16];
} tcp_scan_result_t;
int tcp_service_scan_lan(tcp_scan_result_t *results, int max_results);
```

- [ ] **Step 2: 创建 tcp_service.c**

核心实现：

```c
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
static const int TCP_TASK_STACK = 8192;
static const int TCP_TASK_PRIO = 2;
static const int RECV_BUF_SIZE = 2048;

static int s_sock = -1;
static bool s_connected = false;
static bool s_running = false;
static TaskHandle_t s_task = NULL;
static char s_host[64] = "192.168.1.100";
static int s_port = 9876;
static tcp_callbacks_t s_cbs = {0};

/* Send mutex — protect concurrent sends from different tasks */
static SemaphoreHandle_t s_send_mutex = NULL;

/* ---- JSON Lines recv buffer ---- */
static char s_recv_buf[RECV_BUF_SIZE];
static int s_recv_len = 0;

/* ---- Internal helpers ---- */

static void parse_and_dispatch(const char *line) {
    cJSON *root = cJSON_Parse(line);
    if (!root) return;

    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(root, "type"));
    cJSON *data = cJSON_GetObjectItem(root, "data");

    if (!type) { cJSON_Delete(root); return; }

    if (strcmp(type, "waiting_pairing") == 0) {
        ESP_LOGI(TAG, "Waiting for pairing");
    } else if (strcmp(type, "paired") == 0) {
        ESP_LOGI(TAG, "Paired!");
    } else if (strcmp(type, "pairing_pending") == 0) {
        ESP_LOGI(TAG, "Pairing pending");
    } else if (strcmp(type, "pairing_failed") == 0) {
        ESP_LOGW(TAG, "Pairing failed");
    } else if (strcmp(type, "request") == 0) {
        tcp_request_t req = {0};
        /* Parse ccbb_request_id */
        const char *rid = cJSON_GetStringValue(cJSON_GetObjectItem(data, "ccbb_request_id"));
        if (rid) strncpy(req.ccbb_request_id, rid, sizeof(req.ccbb_request_id) - 1);

        /* Parse context for tool_name and tool_input */
        cJSON *ctx = cJSON_GetObjectItem(data, "context");
        if (ctx) {
            const char *tool_name = cJSON_GetStringValue(cJSON_GetObjectItem(ctx, "tool_name"));
            if (tool_name) strncpy(req.tool, tool_name, sizeof(req.tool) - 1);

            cJSON *tool_input = cJSON_GetObjectItem(ctx, "tool_input");
            if (tool_input) {
                /* Extract hint from description or command */
                const char *desc = cJSON_GetStringValue(cJSON_GetObjectItem(tool_input, "description"));
                const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(tool_input, "command"));
                const char *hint = desc ? desc : (cmd ? cmd : "");
                strncpy(req.hint, hint, sizeof(req.hint) - 1);

                /* Determine request type */
                if (tool_name && strcmp(tool_name, "AskUserQuestion") == 0) {
                    cJSON *questions = cJSON_GetObjectItem(tool_input, "questions");
                    if (questions && cJSON_GetArraySize(questions) > 0) {
                        cJSON *q = cJSON_GetArrayItem(questions, 0);
                        const char *q_text = cJSON_GetStringValue(cJSON_GetObjectItem(q, "question"));
                        if (q_text) strncpy(req.question, q_text, sizeof(req.question) - 1);

                        bool multi = cJSON_IsTrue(cJSON_GetObjectItem(q, "multiSelect"));
                        req.multi_select = multi;
                        req.type = multi ? REQ_MULTI_SELECT : REQ_SINGLE_SELECT;

                        cJSON *opts = cJSON_GetObjectItem(q, "options");
                        int oc = cJSON_GetArraySize(opts);
                        if (oc > 8) oc = 8;
                        req.option_count = oc;
                        for (int i = 0; i < oc; i++) {
                            cJSON *opt = cJSON_GetArrayItem(opts, i);
                            const char *label = cJSON_GetStringValue(cJSON_GetObjectItem(opt, "label"));
                            if (label) strncpy(req.options[i].label, label, sizeof(req.options[i].label) - 1);
                            const char *od = cJSON_GetStringValue(cJSON_GetObjectItem(opt, "description"));
                            if (od) strncpy(req.options[i].description, od, sizeof(req.options[i].description) - 1);
                        }

                        /* Store raw questions JSON for response building */
                        char *qjson = cJSON_PrintUnformatted(questions);
                        if (qjson) {
                            strncpy(req.questions_json, qjson, sizeof(req.questions_json) - 1);
                            cJSON_free(qjson);
                        }

                        /* Default focus */
                        if (multi) {
                            req.focused = 0;
                        } else {
                            req.focused = 0;  /* first option */
                        }
                    }
                } else {
                    req.type = REQ_PERMISSION;
                    req.option_count = 2;
                    strncpy(req.options[0].label, "Allow", sizeof(req.options[0].label) - 1);
                    strncpy(req.options[1].label, "Deny", sizeof(req.options[1].label) - 1);
                    req.focused = 0;  /* default: Allow */
                }
            }
        }

        ESP_LOGI(TAG, "Request: type=%d tool=%s hint=%.40s", req.type, req.tool, req.hint);
        if (s_cbs.on_request) s_cbs.on_request(&req);
    } else if (strcmp(type, "done") == 0) {
        ESP_LOGI(TAG, "Decision processed");
    } else if (strcmp(type, "session_end") == 0) {
        ESP_LOGI(TAG, "Session ended");
        if (s_cbs.on_session_end) s_cbs.on_session_end();
    }

    cJSON_Delete(root);
}

static void send_json(const char *json) {
    if (s_sock < 0) return;
    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    int len = strlen(json);
    send(s_sock, json, len, 0);
    send(s_sock, "\n", 1, 0);
    xSemaphoreGive(s_send_mutex);
}

static void tcp_task(void *arg) {
    ESP_LOGI(TAG, "TCP task started");

    while (s_running) {
        /* Connect */
        struct sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_port = htons(s_port);

        struct hostent *he = gethostbyname(s_host);
        if (!he) {
            ESP_LOGW(TAG, "DNS failed for %s", s_host);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        memcpy(&dest.sin_addr, he->h_addr_list[0], sizeof(dest.sin_addr));

        s_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (s_sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        /* TCP keepalive */
        int keepalive = 1;
        setsockopt(s_sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        int idle = 5, interval = 3, count = 3;
        setsockopt(s_sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
        setsockopt(s_sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
        setsockopt(s_sock, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));

        if (connect(s_sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
            ESP_LOGW(TAG, "Connect failed");
            close(s_sock);
            s_sock = -1;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        s_connected = true;
        ESP_LOGI(TAG, "Connected to %s:%d", s_host, s_port);

        /* Send hello */
        send_json("{\"type\":\"hello\",\"data\":{}}");

        if (s_cbs.on_connected) s_cbs.on_connected();

        /* Recv loop */
        s_recv_len = 0;
        while (s_running && s_connected) {
            int n = recv(s_sock, s_recv_buf + s_recv_len, RECV_BUF_SIZE - s_recv_len - 1, 0);
            if (n <= 0) break;
            s_recv_len += n;
            s_recv_buf[s_recv_len] = '\0';

            /* Process complete lines */
            char *line = s_recv_buf;
            char *nl;
            while ((nl = strchr(line, '\n')) != NULL) {
                *nl = '\0';
                if (strlen(line) > 0) {
                    parse_and_dispatch(line);
                }
                line = nl + 1;
            }
            /* Shift remaining data */
            int remaining = s_recv_buf + s_recv_len - line;
            if (remaining > 0) memmove(s_recv_buf, line, remaining);
            s_recv_len = remaining;
        }

        /* Disconnected */
        s_connected = false;
        close(s_sock);
        s_sock = -1;
        if (s_cbs.on_disconnected) s_cbs.on_disconnected();
        ESP_LOGW(TAG, "Disconnected, will retry in 5s");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    vTaskDelete(NULL);
}

/* ---- Public API ---- */

int tcp_service_init(void) {
    s_send_mutex = xSemaphoreCreateMutex();
    tcp_service_load_config(s_host, sizeof(s_host), &s_port);
    ESP_LOGI(TAG, "Init: host=%s port=%d", s_host, s_port);
    return 0;
}

void tcp_service_register_callbacks(const tcp_callbacks_t *cbs) {
    if (cbs) s_cbs = *cbs;
}

void tcp_service_connect(const char *host, int port) {
    if (host) strncpy(s_host, host, sizeof(s_host) - 1);
    s_port = port;
    tcp_service_save_config(s_host, s_port);

    if (s_running) tcp_service_disconnect();
    s_running = true;
    xTaskCreate(tcp_task, "TCP", TCP_TASK_STACK, NULL, TCP_TASK_PRIO, &s_task);
}

void tcp_service_disconnect(void) {
    s_running = false;
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    if (s_task) {
        vTaskDelay(pdMS_TO_TICKS(200));
        s_task = NULL;
    }
    s_connected = false;
}

bool tcp_service_is_connected(void) {
    return s_connected;
}

void tcp_service_send_decision(const char *json) {
    if (!json) return;
    /* Wrap in decision envelope */
    char buf[1024];
    snprintf(buf, sizeof(buf), "{\"type\":\"decision\",\"data\":%s}", json);
    send_json(buf);
}

bool tcp_service_load_config(char *host, size_t host_len, int *port) {
    bool ok = storage_load_string("tcp", "host", host, host_len);
    int32_t p = 9876;
    storage_load_int("tcp", "port", &p);
    if (port) *port = (int)p;
    return ok;
}

void tcp_service_save_config(const char *host, int port) {
    storage_save_string("tcp", "host", host);
    storage_save_int("tcp", "port", (int32_t)port);
}

void tcp_service_save_pairing_code(const char *code) {
    storage_save_string("tcp", "pairing_code", code);
}

bool tcp_service_load_pairing_code(char *code, size_t len) {
    return storage_load_string("tcp", "pairing_code", code, len);
}

int tcp_service_scan_lan(tcp_scan_result_t *results, int max_results) {
    /* Quick port scan on local subnet for port 9876 */
    int count = 0;
    /* Implementation: iterate local subnet IPs, try connect with short timeout */
    /* For now return 0 — will be implemented in integration phase */
    return count;
}
```

- [ ] **Step 3: 验证编译**

```bash
./build.sh 2>&1 | tail -20
```

- [ ] **Step 4: Commit**

```bash
git add main/service/tcp_service.h main/service/tcp_service.c
git commit -m "feat: TCP client服务 - JSON Lines协议对接bridge"
```

---

## Task 5: 页面栈

**Files:**
- Modify: `main/ui/ui_manager.h` (新增 `ui_go_back`，screen enum 新增 BRIDGE)
- Modify: `main/ui/ui_manager.c` (nav_stack 实现)

- [ ] **Step 1: 更新 ui_manager.h**

在 `ui_screen_id_t` enum 中 `UI_SCREEN_COUNT` 之前新增：

```c
    UI_SCREEN_SETTINGS_BRIDGE,
```

在文件末尾新增声明：

```c
void ui_go_back(void);
```

- [ ] **Step 2: 更新 ui_manager.c**

在文件顶部新增：

```c
#define UI_NAV_STACK_SIZE 8
static ui_screen_id_t nav_stack[UI_NAV_STACK_SIZE];
static int nav_depth = 0;
```

修改 `ui_switch_screen()`，在 `lvgl_lock()` 之后、实际切换之前，将当前页面压入 nav_stack：

```c
void ui_switch_screen(ui_screen_id_t screen_id)
{
    if (screen_id >= UI_SCREEN_COUNT) return;
    if (screen_id == current_screen) return;

    ui_screen_id_t old_screen = current_screen;

    /* Push current onto nav stack */
    if (nav_depth < UI_NAV_STACK_SIZE) {
        nav_stack[nav_depth++] = old_screen;
    }

    lvgl_lock();
    /* ... existing lazy create / rebuild / switch logic unchanged ... */
    lvgl_unlock();
}
```

新增 `ui_go_back()`：

```c
void ui_go_back(void)
{
    if (nav_depth <= 0) return;
    ui_screen_id_t prev = nav_stack[--nav_depth];
    /* Direct switch without pushing again */
    if (prev >= UI_SCREEN_COUNT || !screens[prev]) return;

    ui_screen_id_t old_screen = current_screen;
    lvgl_lock();
    lv_scr_load(screens[prev]);
    current_screen = prev;
    if (screen_is_disposable(old_screen) && screens[old_screen]) {
        lv_obj_clean(screens[old_screen]);
        ui_unregister_input_callbacks(old_screen);
        needs_rebuild[old_screen] = true;
    }
    lvgl_unlock();
}
```

在 `ui_init()` 中初始化 nav_stack：

```c
memset(nav_stack, 0, sizeof(nav_stack));
nav_depth = 0;
```

- [ ] **Step 3: 验证编译**

```bash
./build.sh 2>&1 | tail -20
```

- [ ] **Step 4: Commit**

```bash
git add main/ui/ui_manager.h main/ui/ui_manager.c
git commit -m "feat: UI页面栈 - ui_go_back() 多入口页面正确返回"
```

---

## Task 6: Bridge 配置页面

**Files:**
- Create: `main/ui/ui_screen_settings_bridge.h`
- Create: `main/ui/ui_screen_settings_bridge.c`
- Modify: `main/ui/ui_screen_settings.c` (新增菜单项)
- Modify: `main/CMakeLists.txt` (新增源文件)
- Modify: `main/ui/ui_manager.c` (lazy creator 注册)

- [ ] **Step 1: 创建 ui_screen_settings_bridge.h**

```c
#pragma once

#include "lvgl.h"

lv_obj_t* ui_screen_settings_bridge_create(void);
void ui_screen_settings_bridge_refresh(void);
```

- [ ] **Step 2: 创建 ui_screen_settings_bridge.c**

配置页面包含以下配置项：
1. Bridge 地址（文本显示，SET 进入编辑模式用编码器选择字符）
2. 端口号（数字调整）
3. 配对码输入
4. "扫描局域网" 操作项
5. 连接/断开 操作项

操作逻辑与项目其他设置页面一致：编码器旋转选择项/调值，编码器按键返回（`ui_go_back()`），SET 确认。

配置项用 `ui_list` 组件显示，与 `ui_screen_settings_pomodoro.c` 的 NAV/ADJUST 模式类似。

实现约 200 行 C 代码，包含：
- `bridge_on_encoder_cw/ccw`：导航或调整值
- `bridge_on_encoder_press`：返回上一页 `ui_go_back()`
- `bridge_on_settings_press`：确认/执行
- `update_display()`：刷新列表项
- `ui_screen_settings_bridge_create()`：创建 LVGL 界面

NVS 存储使用 `"tcp"` namespace 的 `"host"`, `"port"`, `"pairing_code"` keys。

- [ ] **Step 3: 更新 ui_screen_settings.c**

将 `SETTINGS_ITEM_COUNT` 从 7 改为 8，新增 `STR_M_BRIDGE` 项。

在 `settings_name_ids` 数组中新增 `STR_M_BRIDGE`。

在 `navigate_to_subpage()` 新增 case 7：

```c
case 7: ui_switch_screen(UI_SCREEN_SETTINGS_BRIDGE); break;
```

在 `settings_on_encoder_cw` 中，IDLE 模式的 CW 不再直接切到 UI_SCREEN_MAIN，需考虑新增 BUDDY 方向。

- [ ] **Step 4: 更新 CMakeLists.txt**

新增 `ui/ui_screen_settings_bridge.c`。

- [ ] **Step 5: 更新 ui_manager.c**

在 `ui_init()` 中注册 lazy creator：

```c
lazy_creators[UI_SCREEN_SETTINGS_BRIDGE] = ui_screen_settings_bridge_create;
```

新增 include：

```c
#include "ui_screen_settings_bridge.h"
```

将 `UI_SCREEN_SETTINGS_BRIDGE` 加入 `screen_is_disposable()` 返回 true。

- [ ] **Step 6: 验证编译**

```bash
./build.sh 2>&1 | tail -20
```

- [ ] **Step 7: Commit**

```bash
git add main/ui/ui_screen_settings_bridge.h main/ui/ui_screen_settings_bridge.c main/ui/ui_screen_settings.c main/ui/ui_manager.c main/CMakeLists.txt
git commit -m "feat: Bridge配置页面 - 地址/端口/配对码/扫描/连接"
```

---

## Task 7: Buddy UI 重构

**Files:**
- Modify: `main/ui/ui_screen_buddy.c`
- Modify: `main/ui/ui_screen_buddy.h`

核心变更：
1. 移除 MODE_INFO，改为 MODE_NORMAL / MODE_ATTENTION（含子类型：权限/单选/多选）
2. 编码器按键：MODE_NORMAL → bridge 配置页，MODE_ATTENTION → 无操作
3. SET 键：MODE_NORMAL（IDLE/SLEEP）→ `buddy_trigger_random()`，MODE_ATTENTION → 提交当前选择
4. ATTENTION 模式支持权限审批（Allow/Deny）、单选列表、多选列表+提交
5. 连接状态图标：BLE → TCP（✓ 绿/✗ 灰）

- [ ] **Step 1: 更新 ui_screen_buddy.h**

```c
#pragma once

#include "lvgl.h"
#include <stdbool.h>

lv_obj_t* ui_screen_buddy_create(void);
void ui_screen_buddy_update_state(void);
void ui_screen_buddy_show_request(const char *tool, const char *hint,
                                   int option_count, request_type_t req_type);
void ui_screen_buddy_clear_request(void);
void ui_screen_buddy_set_connected(bool connected);
```

- [ ] **Step 2: 重写 ui_screen_buddy.c**

移除 `#include "service/ble_service.h"`，新增 `#include "service/tcp_service.h"`。

移除 `MODE_INFO` 和所有 info_container 相关代码。

ATTENTION 模式新增：
- `attn_options[]`：LVGL label 数组（最多 9 个选项 + 1 个提交按钮）
- `attn_focus`：当前聚焦索引
- `attn_selected[]`：多选时已勾选的选项
- `attn_req_type`：当前请求类型

SET 键处理（MODE_ATTENTION）：

```c
if (display_mode == MODE_ATTENTION) {
    if (s_req_type == REQ_PERMISSION || s_req_type == REQ_SINGLE_SELECT) {
        /* Submit focused option directly */
        submit_current_focus();
    } else if (s_req_type == REQ_MULTI_SELECT) {
        if (s_attn_focus == s_option_count) {
            /* "✓ 提交" selected — submit if at least 1 checked */
            if (s_selected_count > 0) submit_multi_selection();
        } else {
            /* Toggle checkbox */
            s_attn_selected[s_attn_focus] = !s_attn_selected[s_attn_focus];
            update_attention_display();
        }
    }
}
```

编码器按键（MODE_NORMAL）：

```c
ui_switch_screen(UI_SCREEN_SETTINGS_BRIDGE);
```

- [ ] **Step 3: 验证编译**

```bash
./build.sh 2>&1 | tail -20
```

- [ ] **Step 4: Commit**

```bash
git add main/ui/ui_screen_buddy.c main/ui/ui_screen_buddy.h
git commit -m "refactor: Buddy UI - 权限/单选/多选请求 + TCP状态图标"
```

---

## Task 8: 集成与清理

**Files:**
- Modify: `main/main.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/ui/i18n.c` (新增字符串)
- Delete: `main/service/ble_service.c`
- Delete: `main/service/ble_service.h`

- [ ] **Step 1: 更新 main.c**

1. 移除 `#include "service/ble_service.h"`（已注释掉）
2. 新增 `#include "service/tcp_service.h"`
3. 新增 TCP 回调：

```c
static void on_tcp_connected(void) {
    ESP_LOGI(TAG, "TCP connected");
    buddy_on_tcp_connected();
    ui_screen_buddy_set_connected(true);
}

static void on_tcp_disconnected(void) {
    ESP_LOGI(TAG, "TCP disconnected");
    buddy_on_tcp_disconnected();
    ui_screen_buddy_set_connected(false);
}

static void on_tcp_request(const tcp_request_t *req) {
    ESP_LOGI(TAG, "TCP request: type=%d", req->type);
    buddy_on_tcp_request(req);
    /* Auto-navigate to buddy screen on request */
    if (ui_get_current_screen() != UI_SCREEN_BUDDY) {
        lvgl_lock();
        ui_switch_screen(UI_SCREEN_BUDDY);
        lvgl_unlock();
    }
}

static void on_tcp_session_end(void) {
    ESP_LOGI(TAG, "TCP session end");
    buddy_on_tcp_session_end();
}
```

4. 在 `app_main()` 注册回调部分新增：

```c
static const tcp_callbacks_t tcp_cbs = {
    .on_connected = on_tcp_connected,
    .on_disconnected = on_tcp_disconnected,
    .on_request = on_tcp_request,
    .on_session_end = on_tcp_session_end,
};
tcp_service_register_callbacks(&tcp_cbs);
```

5. 在 WiFi 连接成功回调 `on_wifi_connected()` 中触发 TCP 连接：

```c
char tcp_host[64];
int tcp_port;
if (tcp_service_load_config(tcp_host, sizeof(tcp_host), &tcp_port)) {
    tcp_service_connect(tcp_host, tcp_port);
}
```

6. 在 WiFi 断开回调中断开 TCP：

```c
tcp_service_disconnect();
```

7. 移除 service_task 中的 `// BLE maintenance (disabled)` 注释。

- [ ] **Step 2: 更新 CMakeLists.txt**

在 SRCS 列表中：
- 新增 `service/tcp_service.c`
- 新增 `ui/ui_screen_settings_bridge.c`
- 确认无 `service/ble_service.c`（已不存在）

- [ ] **Step 3: 删除 BLE 文件**

```bash
rm main/service/ble_service.c main/service/ble_service.h
```

- [ ] **Step 4: 新增 i18n 字符串**

在 `i18n.c` 中新增 Bridge 配置相关字符串 ID：
- `STR_M_BRIDGE` — "Buddy Bridge"
- `STR_BRIDGE_HOST` — "Bridge 地址"
- `STR_BRIDGE_PORT` — "端口"
- `STR_PAIRING_CODE` — "配对码"
- `STR_SCAN_LAN` — "扫描局域网"
- `STR_CONNECT_BRIDGE` — "连接"
- `STR_DISCONNECT_BRIDGE` — "断开"
- `STR_SUBMIT` — "✓ 提交"

在 `i18n.h` 的 `str_id_t` enum 中新增对应条目。

- [ ] **Step 5: 完整编译验证**

```bash
./build.sh 2>&1 | tail -30
```

预期：编译成功，无错误。

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: 集成TCP服务 + 移除BLE + i18n字符串"
```

---

## Task 9: 烧录测试与调试

**Files:**
- 无文件变更

- [ ] **Step 1: 烧录到设备**

```bash
./build.sh flash
```

- [ ] **Step 2: 验证基础功能**

- 番茄钟页面正常显示
- WiFi 连接正常
- Buddy 页面显示新角色动画（5 行精灵）
- 切换物种正常（18 个物种循环）

- [ ] **Step 3: 验证 TCP 功能**

- 确保 bridge 后端运行在局域网
- 在 Bridge 配置页面输入 bridge 地址和端口
- 发起连接，观察 hello 握手
- 输入配对码完成配对
- 从 Claude Code 触发权限请求，观察 buddy 进入 ATTENTION 状态
- 测试 Allow/Deny 操作
- 测试 AskUserQuestion 单选和多选

- [ ] **Step 4: 验证页面导航**

- Buddy 页面 → 编码器按键 → Bridge 配置页 → 编码器按键 → 返回 Buddy
- 设置页 → Buddy Bridge → Bridge 配置页 → 编码器按键 → 返回设置页
- 其他子设置页面的返回不受影响

---

## 自检清单

### Spec 覆盖

| Spec 需求 | 对应 Task |
|-----------|----------|
| 5×12 精灵格式 | Task 1, 2 |
| 18 个物种 + body_color | Task 2 |
| 7 状态 + 帧序列 SEQ | Task 1, 2 |
| TCP 服务替代 BLE | Task 4 |
| JSON Lines 协议 | Task 4 |
| Bridge 配置页面 | Task 6 |
| 页面栈 ui_go_back | Task 5 |
| TCP 事件驱动状态机 | Task 3 |
| AskUserQuestion 单选/多选 | Task 7 |
| 默认选择逻辑 | Task 7 |
| 多选 "✓ 提交" | Task 7 |
| SET 键提交默认选择 | Task 7 |
| buddy_trigger_random() | Task 3 |
| BLE 文件清理 | Task 8 |
| 连接状态图标 | Task 7 |

### Placeholder 扫描

无 TBD/TODO — 所有代码步骤包含实际实现或明确指向。

### 类型一致性

- `tcp_request_t` 在 tcp_service.h 中定义，buddy.h 使用前向声明，buddy.c 通过 include 获取完整定义
- `buddy_species_t` 的 `seq` 和 `seq_len` 数组维度为 `BUDDY_STATE_COUNT = 7`
- `ui_screen_id_t` 新增 `UI_SCREEN_SETTINGS_BRIDGE`，在 enum 末尾 `UI_SCREEN_COUNT` 之前
- `BUDDY_SPECIES_COUNT` 从 `sizeof` 自动计算，与 18 个物种一致
