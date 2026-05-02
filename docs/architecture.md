# 系统架构文档

## 模块分层图

```
┌─────────────────────────────────────────────────────────────────┐
│                        ui/ (LVGL 界面层)                         │
│  ui_manager  ui_screen_main  ui_screen_pomodoro  ui_screen_buddy │
│  ui_screen_settings  ui_screen_wifi  ui_screen_password          │
├─────────────────────────────────────────────────────────────────┤
│                     buddy/    pomodoro/    input/                │
│  buddy (状态机)    pomodoro_engine    input_handler               │
├─────────────────────────────────────────────────────────────────┤
│                        service/ (系统服务层)                      │
│  wifi_service    time_service    storage_service    ble_service  │
├─────────────────────────────────────────────────────────────────┤
│                        driver/ (硬件驱动层)                      │
│  st7789_lcd (SPI+DMA)    buzzer (PWM)    ws2812 (RMT)           │
├─────────────────────────────────────────────────────────────────┤
│  ESP-IDF  │  LVGL v9.5  │  FreeRTOS  │  NVS  │  BLE  │  WiFi   │
└─────────────────────────────────────────────────────────────────┘
```

## 任务模型

| 任务 | 优先级 | 栈大小 | 循环周期 | 职责 |
|------|--------|--------|----------|------|
| LVGL | 5 | 8KB | 1–10ms | `lv_timer_handler()` 渲染循环，1ms tick 由 `esp_timer` 驱动 |
| Input | 3 | 6KB | 阻塞队列 | 读取编码器/按键事件，调用 `ui_dispatch_*` 分发到当前界面 |
| Service | 2 | 6KB | 100ms | `time_service_tick()` NTP 重同步、`ble_service_tick()` BLE 维护、`buddy_tick()` 动画 (500ms) |
| UIUpdate | 1 | 4KB | 100ms | 番茄钟引擎 tick (1s)、主界面时间刷新、WiFi 列表刷新、Buddy 界面状态更新 |

## 数据流（回调链路）

所有跨模块通信通过在 `main.c` 中注册的回调函数完成：

```
BLE 心跳 (ble_service)
  → on_ble_heartbeat()
    → buddy_on_heartbeat()
      → set_state() → on_buddy_state_changed()
        → ui_switch_screen(UI_SCREEN_BUDDY) [当进入 ATTENTION 状态]

WiFi 连接 (wifi_service)
  → on_wifi_connected()
    → time_service_request_sync()  [触发 NTP 同步]
    → ui_screen_main_update_wifi_status()
  → on_wifi_disconnected()
    → ui_screen_main_update_wifi_status()

用户操作 (input_handler → ui_dispatch)
  → ui_screen_buddy: buddy_approve() / buddy_deny()
    → ble_service_send_permission()  [发送决策到 Claude Desktop]
    → buddy_save_stats()             [NVS 持久化]
```

## GPIO 引脚映射

| GPIO | 方向 | 功能 | 驱动模块 |
|------|------|------|----------|
| GPIO4 | IN | EC11 编码器 A 相 | `input_handler.c` |
| GPIO5 | IN | EC11 编码器 B 相 | `input_handler.c` |
| GPIO6 | OUT | LCD SPI 时钟 (SCK) | `st7789_lcd.c` |
| GPIO7 | OUT | LCD SPI 数据 (MOSI) | `st7789_lcd.c` |
| GPIO8 | OUT | WS2812 RGB LED 数据 | `ws2812.c` |
| GPIO9 | IN | SET 按键 (低电平有效) | `input_handler.c` |
| GPIO10 | OUT | LCD 数据/命令选择 (DC) | `st7789_lcd.c` |
| GPIO20 | OUT | 蜂鸣器 PWM (LEDC) | `buzzer.c` |
| GPIO21 | IN | EC11 编码器按键 (低电平有效) | `input_handler.c` |

## 界面导航图

```
                    ┌──────────┐
                    │   MAIN   │
                    └────┬─────┘
                   CW/CCW │
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
    ┌───────────┐   ┌───────────┐   ┌──────────┐
    │ SETTINGS  │   │ POMODORO  │   │  BUDDY   │
    └─────┬─────┘   └───────────┘   └──────────┘
          │ 按键进入
    ┌─────┼──────────────┐
    ▼                    ▼
┌──────────────┐   ┌──────────────┐
│ SETTINGS_POMO│   │  WIFI_LIST   │
└──────────────┘   └──────┬───────┘
                          │ 选择加密 AP
                          ▼
                   ┌──────────────┐
                   │PASSWORD_INPUT│
                   └──────────────┘

特殊导航:
  Buddy ATTENTION 状态 → 自动切换到 BUDDY 界面
  长按编码器 → 返回上级界面
```

## NVS 命名空间与键

### `wifi` 命名空间
| 键 | 类型 | 说明 |
|----|------|------|
| `ssid` | string | WiFi SSID |
| `password` | string | WiFi 密码 |

### `pomodoro` 命名空间
| 键 | 类型 | 默认值 | 说明 |
|----|------|--------|------|
| `work_min` | int32 | 25 | 工作时长（分钟） |
| `break_min` | int32 | 5 | 休息时长（分钟） |
| `long_break` | int32 | 15 | 长休息时长（分钟） |
| `cycles` | int32 | 4 | 长休息前循环数 |
| `completed` | int32 | 0 | 已完成番茄数 |
| `cycle` | int32 | 0 | 当前循环计数 |

### `settings` 命名空间
| 键 | 类型 | 说明 |
|----|------|------|
| `time_high` | int32 | 时间戳高 32 位 |
| `time_low` | int32 | 时间戳低 32 位 |

### `buddy` 命名空间
| 键 | 类型 | 默认值 | 说明 |
|----|------|--------|------|
| `species` | int32 | 0 | 宠物物种索引 |
| `approved` | int32 | 0 | 已批准权限次数 |
| `denied` | int32 | 0 | 已拒绝权限次数 |
