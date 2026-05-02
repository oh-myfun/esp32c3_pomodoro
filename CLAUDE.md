# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

基于 ESP32-C3 的番茄钟与 AI Buddy 伴侣设备固件。硬件 SPI + DMA 驱动 ST7789 240x240 LCD，EC11 编码器输入，WS2812 LED 指示，WiFi 连接管理，BLE (Nordic UART) 与 Claude Desktop 通信。技术栈：ESP-IDF v5.5.4 + LVGL v9.5.0 + FreeRTOS。

## 构建命令

```bash
# 设置 ESP-IDF 环境 (Windows PowerShell)
$env:IDF_PATH="D:\Espressif\frameworks\esp-idf-v5.5.4"
& "$env:IDF_PATH\export.ps1"

# 构建
idf.py build

# 烧录（串口配置见 .vscode/settings.json）
idf.py -p COM7 flash monitor

# 增量构建
ninja -C build
```

## 目录结构

```
main/
├── main.c                    # 主程序入口，硬件初始化与任务创建
├── driver/                   # 硬件驱动层（不依赖业务层）
│   ├── st7789_lcd.c/h        # ST7789 LCD SPI + DMA 驱动
│   ├── buzzer.c/h            # 蜂鸣器 PWM (LEDC) 驱动
│   └── ws2812.c/h            # WS2812 RGB LED 驱动 (GPIO8)
├── input/
│   └── input_handler.c/h     # 编码器 + 按键 → FreeRTOS 队列 → ui_dispatch_*
├── service/                  # 系统服务层
│   ├── wifi_service.c/h      # WiFi STA（扫描、连接、断开，无 HTTP 服务器）
│   ├── time_service.c/h      # 统一 NTP 时间同步，时区管理
│   ├── storage_service.c/h   # NVS 封装（wifi / pomodoro / settings / buddy 命名空间）
│   └── ble_service.c/h       # BLE Nordic UART Service，JSON 心跳协议
├── buddy/                    # AI Buddy 模块
│   ├── buddy.c/h             # 状态机（SLEEP/IDLE/BUSY/ATTENTION/CELEBRATE/DIZZY/HEART）
│   └── buddy_chars.c/h       # ASCII 宠物像素画帧数据（多物种、多状态动画）
├── pomodoro/
│   └── pomodoro_engine.c/h   # 番茄钟引擎（IDLE/WORK/BREAK/LONG_BREAK/PAUSED）
└── ui/                       # LVGL 界面层
    ├── ui_manager.c/h        # 界面调度器（切换、输入分发）
    ├── ui_list.c/h           # 通用滚动列表组件
    ├── ui_screen_main.c/h    # 主界面（时间、WiFi 状态）
    ├── ui_screen_pomodoro.c/h # 番茄钟界面
    ├── ui_screen_buddy.c/h   # Buddy 伴侣界面（ASCII 动画 + 权限审批）
    ├── ui_screen_settings.c/h # 设置界面
    ├── ui_screen_settings_pomodoro.c/h # 番茄钟设置子界面
    ├── ui_screen_wifi.c/h    # WiFi 列表界面
    └── ui_screen_password.c/h # 密码输入界面
```

## 任务模型（4 个 FreeRTOS 任务）

| 任务 | 优先级 | 栈大小 | 职责 |
|------|--------|--------|------|
| LVGL | 5 | 8KB | `lv_timer_handler()` 循环，`esp_timer` 驱动 1ms tick |
| Input | 3 | 6KB | 从 FreeRTOS 队列读取编码器/按键事件，通过 `ui_dispatch_*` 分发到 UI |
| Service | 2 | 6KB | 100ms 循环：`time_service_tick()`、`ble_service_tick()`、buddy 动画 tick (500ms) |
| UIUpdate | 1 | 4KB | 100ms 循环：每 1s 驱动番茄钟引擎，更新主界面时间，刷新 WiFi 列表，更新 Buddy 界面 |

## 初始化顺序（`app_main`）

1. NVS 初始化（致命错误，失败则 halt）
2. 驱动：buzzer → LCD → WS2812
3. LVGL + UI 界面
4. 业务模块：pomodoro_engine → buddy
5. 输入处理：input_handler
6. 网络服务：wifi_service → ble_service → time_service
7. 注册回调（WiFi/BLE/Buddy）
8. 创建 4 个任务

## UI 输入分发模式

每个界面在创建时向 `ui_manager` 注册 `ui_input_callbacks_t`（5 个函数指针）。输入任务调用 `ui_dispatch_encoder_cw/ccw/press/long_press/settings_press()`，由 `ui_manager` 查找当前界面的回调并执行，实现输入与界面逻辑解耦。

界面导航：
```
MAIN ←→ POMODORO ←→ BUDDY
  ↕                        ↕
SETTINGS_POMODORO ← SETTINGS → WIFI_LIST → PASSWORD_INPUT
```

## 模块职责

- **driver/**：硬件驱动（st7789_lcd、buzzer、ws2812）— 不依赖业务层
- **input/**：编码器 + 按键 → FreeRTOS 队列 → `ui_dispatch_*`
- **service/wifi_service**：WiFi STA（扫描、连接、状态回调，无 HTTP/NTP）
- **service/time_service**：统一 NTP 同步，时区（POSIX TZ 字符串），自动重同步
- **service/storage_service**：NVS 封装，4 个命名空间：`wifi`、`pomodoro`、`settings`、`buddy`
- **service/ble_service**：BLE GATT Server (Nordic UART Service)，解析 JSON 心跳，发送权限决策
- **buddy/**：ASCII 宠物状态机，BLE 驱动状态转换，WS2812 LED 反馈，NVS 持久化统计
- **pomodoro/**：番茄钟状态机（IDLE/WORK/BREAK/LONG_BREAK/PAUSED），每秒 tick，NVS 持久化
- **ui/**：界面管理器 + 7 个界面。`ui_manager` 负责调度，每个 `ui_screen_*.c` 自包含

## 线程安全

LVGL 任务之外的所有 LVGL API 调用必须用 `lvgl_lock()`/`lvgl_unlock()` 包裹（FreeRTOS 互斥量）。LVGL 任务本身不需要加锁。

## 代码规范

- 头文件使用 `#pragma once`
- 成功返回 0，失败返回负值
- ESP-IDF API 调用使用 `ESP_ERROR_CHECK()` 或手动检查返回值
- 日志标签：`static const char* TAG = "module_name";`（如 `"buddy"`、`"BLE"`）
- 使用 LVGL v9.x API，不兼容 v8.x
- 文档使用中文编写

## 硬件引脚

| GPIO | 功能 | 定义位置 |
|------|------|----------|
| GPIO4 | 编码器 A 相 | `input_handler.c` |
| GPIO5 | 编码器 B 相 | `input_handler.c` |
| GPIO6 | LCD SPI 时钟 (SCK) | `st7789_lcd.c` |
| GPIO7 | LCD SPI 数据 (SDA/MOSI) | `st7789_lcd.c` |
| GPIO8 | WS2812 数据 (DIN) | `ws2812.h` |
| GPIO9 | SET 按键 (低电平有效) | `input_handler.c` |
| GPIO10 | LCD 数据/命令选择 (RS/DC) | `st7789_lcd.c` |
| GPIO20 | 蜂鸣器 (PWM/LEDC) | `buzzer.c` |
| GPIO21 | 编码器按键 (低电平有效) | `input_handler.c` |

## Kconfig 默认值

- 工作时长：25 分钟（1–60）
- 休息时长：5 分钟（1–30）
- NTP 同步间隔：10 分钟（0 = 禁用）
- 时区偏移：+8（UTC+8）

## 已知问题

- 温湿度值为硬编码占位（无实际传感器）
- 设置项（亮度、对比度、语言）未持久化到 NVS
- WiFi 断开后无自动重连
- BLE 心跳超时检测尚未实现（`ble_service_tick()` 为空操作）
