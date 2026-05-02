# BLE + Buddy 设计文档

## BLE 通信协议

### Nordic UART Service (NUS)

设备作为 BLE GATT Server，广播名称 `Claude-Buddy-XXXX`（XXXX 为 MAC 地址后两字节）。

| 特征 | UUID | 属性 | 用途 |
|------|------|------|------|
| RX | 6e400002-b5a3-f393-e0a9-e50e24dcca9e | Write | 接收 Claude Desktop 数据 |
| TX | 6e400003-b5a3-f393-e0a9-e50e24dcca9e | Notify | 发送数据到 Claude Desktop |

MTU 设置为 512 字节。连接参数：7.5ms–15ms 间隔，0 延迟，4s 超时。

### JSON 消息格式

所有消息以 `\n` 结尾的单行 JSON 格式传输。

#### 心跳消息（Claude Desktop → 设备）

```json
{
  "total": 5,
  "running": 2,
  "waiting": 1,
  "msg": "2 tasks running, 1 waiting",
  "prompt": {
    "id": "perm_abc123",
    "tool": "Bash",
    "hint": "rm -rf /tmp/foo"
  }
}
```

- `prompt` 字段仅在有权限请求时存在
- `running` > 0 表示有活跃任务
- `waiting` > 0 表示有等待中的任务

#### 权限决策（设备 → Claude Desktop）

```json
{"cmd": "permission", "id": "perm_abc123", "decision": "approve"}
{"cmd": "permission", "id": "perm_abc123", "decision": "deny"}
```

#### 命令消息（Claude Desktop → 设备）

| cmd | 说明 |
|-----|------|
| `status` | 请求设备状态，回复 ack |
| `name` | 设置 Buddy 名称 |
| `owner` | 设置主人名称 |
| `unpair` | 解除配对，清除所有绑定设备 |

#### ACK 响应（设备 → Claude Desktop）

```json
{"ack": "status", "ok": true}
```

## Buddy 状态机

### 状态定义

```
┌─────────────────────────────────────────────────────────┐
│                                                         │
│  SLEEP ──BLE连接──→ IDLE ──running>0──→ BUSY            │
│                       │                    │             │
│                       │   has_prompt       │ running=0   │
│                       ├──→ ATTENTION ←─────┘             │
│                       │         │                        │
│                       │ approve │ deny                   │
│                       │    ↓    ↓                        │
│                       │  CELEBRATE → (3s) → IDLE         │
│                       │                                  │
│                       ├──→ DIZZY → (3s) → 前状态         │
│                       │    (用户触发)                     │
│                       │                                  │
│  BLE断开 ←────── 任何状态 ──────→ SLEEP                  │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### 状态转换表

| 当前状态 | 事件 | 目标状态 | LED |
|----------|------|----------|-----|
| SLEEP | BLE 连接 | IDLE | 灭 |
| IDLE | 收到 `running > 0` | BUSY | 灭 |
| IDLE | 收到 `has_prompt` | ATTENTION | 红色闪烁 |
| IDLE | `buddy_trigger_dizzy()` | DIZZY | — |
| BUSY | 收到 `running = 0` | IDLE | 灭 |
| BUSY | 收到 `has_prompt` | ATTENTION | 红色闪烁 |
| ATTENTION | `buddy_approve()` | CELEBRATE | 绿色 |
| ATTENTION | `buddy_deny()` | IDLE | 灭 |
| ATTENTION | 30s 超时 | IDLE | 灭 |
| CELEBRATE | 3s 超时 | IDLE | 灭 |
| DIZZY | 3s 超时 | 前状态 | — |
| 任何 | BLE 断开 | SLEEP | 灭 |

### LED 颜色映射

| 状态 | LED 颜色 | 说明 |
|------|----------|------|
| SLEEP | 灭 | 设备休眠 |
| IDLE | 灭 | 空闲等待 |
| BUSY | 灭 | Claude 工作中（未来可设蓝色） |
| ATTENTION | 红色闪烁 | 每 500ms 交替亮灭，需要审批 |
| CELEBRATE | 绿色 | 审批通过 |
| DIZZY | — | 趣味动画 |
| HEART | 粉色 (255,50,80) | 预留状态 |

## ASCII 宠物数据结构

每个物种由 `buddy_species_t` 描述：

```c
#define MAX_ANIM_FRAMES   4       // 每个状态最多 4 帧动画
#define BUDDY_STATE_COUNT  7       // 状态总数
#define BUDDY_FRAME_LINES 12       // 每帧 12 行 ASCII

typedef struct {
    const char *name;              // 物种名称（如 "Cat"、"Dog"）
    const char *personality;       // 性格描述
    const char *const (*frames)[MAX_ANIM_FRAMES][BUDDY_FRAME_LINES];  // 动画帧数据
    const uint8_t frame_count[BUDDY_STATE_COUNT];  // 每个状态的帧数
} buddy_species_t;
```

动画由 `buddy_tick()` 每 500ms 驱动一次帧切换，循环播放当前状态的帧序列。

## 权限审批流程

```
Claude Desktop                设备 (BLE)                 用户
    │                           │                         │
    │  心跳 JSON (has_prompt)   │                         │
    ├──────────────────────────>│                         │
    │                           │  set_state(ATTENTION)   │
    │                           │  自动切换到 Buddy 界面    │
    │                           │  LED 红色闪烁            │
    │                           │                         │
    │                           │  显示工具名和命令提示      │
    │                           │<──────── 编码器旋转 ──────│
    │                           │<──────── 按键(approve) ──│
    │                           │                         │
    │  permission JSON          │                         │
    │<──────────────────────────│  CELEBRATE 状态 (3s)     │
    │                           │  LED 绿色               │
    │                           │  → 回到 IDLE            │
    │                           │                         │
```

## NVS 持久化键

所有 Buddy 统计数据存储在 `buddy` NVS 命名空间中：

| 键 | 类型 | 说明 | 读写时机 |
|----|------|------|----------|
| `species` | int32 | 当前选中的宠物物种索引 | `buddy_set_species()` 时写入，`buddy_init()` 时读取 |
| `approved` | int32 | 累计批准次数 | 每次 approve 时递增并写入 |
| `denied` | int32 | 累计拒绝次数 | 每次 deny 时递增并写入 |

统计写入由 `buddy_save_stats()` 完成，在每次 approve/deny 后调用。启动时由 `buddy_load_stats()` 恢复。
