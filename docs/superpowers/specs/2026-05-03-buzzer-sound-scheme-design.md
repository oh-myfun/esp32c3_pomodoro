# 蜂鸣器音效方案设计

## 目标

为设备所有交互场景添加非阻塞蜂鸣器音效反馈，包括 UI 交互、系统状态、番茄钟事件和 Buddy 事件。在设置中提供声音开关。

## 架构

### 播放引擎

采用 esp_timer 单次定时器驱动音符队列，不占用 FreeRTOS 任务资源。

```
调用方 → sound_service_play(SOUND_xxx)
  → 检查开关，关则返回
  → 查找 melody 的 note 数组
  → 播放第一个音符（设 freq + duty）
  → esp_timer_start_once(duration_ms)
  → 定时器回调：buzzer_off() → 播放下一个音符或结束
```

正在播放时收到新请求：中断当前播放，从头开始新音效。

### 数据结构

```c
typedef struct {
    uint32_t freq_hz;    // 0 = 休止符（静音）
    uint16_t duration_ms;
} buzzer_note_t;

typedef struct {
    const buzzer_note_t *notes;
    uint8_t count;
} buzzer_melody_t;
```

所有旋律定义为编译期常量数组，零运行时内存分配。

## 音效定义

### 音高参考表

| 音名 | 频率(Hz) | | 音名 | 频率(Hz) |
|------|---------|---|------|---------|
| C4 | 262 | | C5 | 523 |
| D4 | 294 | | D5 | 587 |
| Eb4 | 311 | | E5 | 659 |
| E4 | 330 | | F5 | 698 |
| F4 | 349 | | G5 | 784 |
| G4 | 392 | | A5 | 880 |
| A4 | 440 | | B5 | 988 |
| Bb4 | 466 | | C6 | 1047 |
| B4 | 494 | | D6 | 1175 |
| | | | E6 | 1319 |
| | | | G6 | 1568 |
| | | | A6 | 1760 |
| | | | C7 | 2093 |

### UI 交互反馈

| ID | 场景 | 旋律 | 说明 |
|---|---|---|---|
| `SOUND_KEY_CLICK` | 编码器旋转/按键 | [C6 30ms] | 极短点击音 |
| `SOUND_CONFIRM` | 确认操作（SET 键选择） | [E5 60ms, G5 60ms] | 两音上行 |
| `SOUND_CANCEL` | 取消/返回 | [G5 60ms, E5 60ms] | 两音下行 |

### 系统状态提示

| ID | 场景 | 旋律 | 说明 |
|---|---|---|---|
| `SOUND_SUCCESS` | 操作成功（通用） | [C5 80ms, E5 80ms, G5 80ms] | 三音上行 do-mi-sol |
| `SOUND_FAIL` | 操作失败（通用） | [G4 80ms, Eb4 120ms] | 两音下行，低沉 |
| `SOUND_WIFI_CONNECT` | WiFi 开始连接 | [A5 50ms] | 单短音 |
| `SOUND_WIFI_CONNECTED` | WiFi 连接成功 | [E5 60ms, B5 80ms] | 轻快双音 |
| `SOUND_WIFI_FAILED` | WiFi 连接失败 | [B4 100ms, 0 50ms, B4 100ms] | 两声低鸣 |
| `SOUND_SYNC_START` | 时间同步开始 | [D6 30ms] | 极短高音 |
| `SOUND_SYNC_DONE` | 时间同步完成 | [D6 30ms, A6 50ms] | 清脆双音 |

### 番茄钟事件

| ID | 场景 | 旋律 | 说明 |
|---|---|---|---|
| `SOUND_POMO_START` | 工作阶段开始 | [C5 100ms, E5 100ms, G5 100ms, C6 150ms] | 四音上行，激励感 |
| `SOUND_POMO_WORK_DONE` | 工作阶段结束 | [G5 150ms, E5 150ms, C5 200ms] | 三音下行，舒缓 |
| `SOUND_POMO_BREAK_DONE` | 休息结束 | [C5 80ms, 0 60ms, C5 80ms, 0 60ms, C5 150ms] | 三声渐强提醒 |
| `SOUND_POMO_LONG_BREAK` | 长休息开始 | [G4 200ms, C5 200ms, E5 200ms, G5 300ms] | 缓慢上行，放松感 |

### Buddy 事件

| ID | 场景 | 旋律 | 说明 |
|---|---|---|---|
| `SOUND_BUDDY_ATTENTION` | Buddy 请求注意 | [A5 100ms, 0 80ms, A5 100ms, 0 80ms, A5 200ms] | 三声急促呼唤 |
| `SOUND_BUDDY_HAPPY` | 审批通过/开心 | [C6 60ms, E6 60ms, G6 60ms, C7 120ms] | 高音快速上行，欢快 |
| `SOUND_BUDDY_SAD` | 审批拒绝 | [E5 150ms, C5 200ms] | 两音下行，低沉 |

## 声音开关

- 设置界面新增 "Sound" 项，值显示 "On"/"Off"
- NVS 存储：`settings` 命名空间，key `sound_on`，int32 (1=开, 0=关)
- `sound_service_play()` 开头检查开关，关闭时立即返回
- 默认开启

## 音效调用点

### main.c (UI update task)

- WiFi 连接成功回调 `on_wifi_connected()` → `SOUND_WIFI_CONNECTED`
- WiFi 连接失败回调 `on_wifi_connect_failed()` → `SOUND_WIFI_FAILED`
- WiFi 开始连接（`wifi_service_connect()` 调用后）→ `SOUND_WIFI_CONNECT`
- 时间同步完成 `time_sync_notification()` → `SOUND_SYNC_DONE`

### ui_screen_pomodoro.c

- 番茄钟开始（SET 键启动）→ `SOUND_POMO_START`
- 番茄钟暂停/恢复 → `SOUND_CONFIRM`

### main.c (pomodoro tick)

- 工作结束（phase 从 WORK → BREAK）→ `SOUND_POMO_WORK_DONE`
- 休息结束（phase 从 BREAK → WORK）→ `SOUND_POMO_BREAK_DONE`
- 长休息开始 → `SOUND_POMO_LONG_BREAK`

### buddy.c

- 状态变为 ATTENTION → `SOUND_BUDDY_ATTENTION`
- 审批通过 `buddy_approve()` → `SOUND_BUDDY_HAPPY`
- 审批拒绝 `buddy_deny()` → `SOUND_BUDDY_SAD`

### input_handler.c

- 编码器旋转/按键 → `SOUND_KEY_CLICK`

### ui_screen_settings.c

- 进入设置/确认 → `SOUND_CONFIRM`
- 返回/取消 → `SOUND_CANCEL`

## 修改文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `driver/buzzer.h` | 修改 | 新增 note/melody 类型，play 接口 |
| `driver/buzzer.c` | 修改 | 实现 esp_timer 驱动的非阻塞播放 |
| `service/sound_service.h` | 新建 | 音效 ID 枚举，play/开关 API |
| `service/sound_service.c` | 新建 | 音效数据表 + play 调度 |
| `main.c` | 修改 | WiFi/时间同步回调中调用音效 |
| `ui/ui_screen_pomodoro.c` | 修改 | 番茄钟开始时调用音效 |
| `ui/ui_screen_settings.c` | 修改 | 新增 Sound 设置项 |
| `input/input_handler.c` | 修改 | 按键时调用音效 |
| `buddy/buddy.c` | 修改 | 状态变化时调用音效 |
| `service/storage_service.h` | 无修改 | 使用已有 settings 命名空间 |
