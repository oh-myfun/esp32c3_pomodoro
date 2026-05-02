# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

基于 ESP32-C3 的番茄钟与智能聊天助手固件。硬件 SPI + DMA 驱动 ST7789 240x240 LCD，EC11 编码器输入，WiFi 连接管理，NTP 时间同步。技术栈：ESP-IDF v5.5.4 + LVGL v9.5.0 + FreeRTOS。

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

## 架构

### 任务模型（3 个 FreeRTOS 任务）

| 任务 | 优先级 | 栈大小 | 职责 |
|------|--------|--------|------|
| LVGL | 5 | 8KB | `lv_timer_handler()` 循环，`esp_timer` 驱动 1ms tick |
| Input | 3 | 8KB | 从 FreeRTOS 队列读取编码器/按键事件，通过 `ui_dispatch_*` 分发到 UI |
| UIUpdate | 1 | 4KB | 100ms 循环：每 1s 驱动番茄钟引擎，更新主界面，触发 NTP 同步 |

### 初始化顺序（`app_main`）

NVS → 蜂鸣器 → LCD+LVGL → UI 界面 → 番茄钟引擎 → 输入处理 → WiFi → 时间服务 → 创建任务

### UI 输入分发模式

每个界面在创建时向 `ui_manager` 注册 `ui_input_callbacks_t`（5 个函数指针）。输入任务调用 `ui_dispatch_encoder_cw/ccw/press/long_press/settings_press()`，由 `ui_manager` 查找当前界面的回调并执行，实现输入与界面逻辑解耦。

界面导航（循环）：
```
MAIN ←→ POMODORO ←→ CHAT
  ↕                        ↕
SETTINGS_POMODORO ← SETTINGS → WIFI_LIST → PASSWORD_INPUT
```

### 模块职责

- **driver/**：硬件驱动（st7789_lcd、buzzer）— 不依赖业务层
- **input/**：编码器 + 按键 → FreeRTOS 队列 → `ui_dispatch_*`
- **pomodoro/**：状态机（IDLE/WORK/BREAK/LONG_BREAK/PAUSED），每秒 tick 一次，状态通过 storage_service 持久化
- **network/**：WiFi STA + 内嵌 HTTP 配置服务器 + SNTP 同步
- **storage/**：NVS 封装，3 个命名空间：`pomodoro`、`settings`、`wifi`
- **time/**：NTP 同步服务，支持时区（POSIX TZ 字符串）
- **ui/**：界面管理器 + 7 个界面。`ui_manager` 负责调度，每个 `ui_screen_*.c` 自包含

### 线程安全

LVGL 任务之外的所有 LVGL API 调用必须用 `lvgl_lock()`/`lvgl_unlock()` 包裹。LVGL 任务本身不需要加锁。

## 代码规范

- 头文件使用 `#pragma once`
- 成功返回 0，失败返回负值
- ESP-IDF API 调用使用 `ESP_ERROR_CHECK()` 或手动检查返回值
- 日志标签：`static const char* TAG = "module_name";`（如 `"wifi_mgr"`、`"pomo_eng"`）
- 使用 LVGL v9.x API，不兼容 v8.x

## 硬件引脚

| GPIO | 功能 | 定义位置 |
|------|------|----------|
| GPIO4 | 编码器 A 相 | `input_handler.c` |
| GPIO5 | 编码器 B 相 | `input_handler.c` |
| GPIO6 | LCD SPI 时钟 (SCK) | `st7789_lcd.c` |
| GPIO7 | LCD SPI 数据 (SDA/MOSI) | `st7789_lcd.c` |
| GPIO8 | WS2812 数据 (DIN) | 待实现 |
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

- 温湿度值为硬编码（无实际传感器）
- `esp_lcd_st7789` 组件声明为依赖但未使用 — 实际使用自定义 `st7789_lcd.c` 直接驱动 SPI
- 设置项（亮度、对比度、语言）未持久化到 NVS
- WiFi 断开后无自动重连
- 聊天助手界面为占位实现
- NTP 同步逻辑在 `wifi_manager.c` 和 `time_service.c` 中重复实现
