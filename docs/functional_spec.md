# 功能说明文档 (Functional Specification)

项目名称：基于ESP32-C3的番茄钟与智能聊天助手设备

目标：给定 ESP32-C3 平台，基于 ST7789 LCD、LVGL 图形库、EC11 编码器输入、WiFi/NTP 功能，实现番茄钟计时和智能聊天助手功能。本文档用于澄清需求、系统架构、模块接口和验收标准，方便后续迭代与维护。

## 1. 概要

- 目标平台：ESP32-C3 (RISC-V架构)
- 显示：ST7789 240x240 LCD，通过 SPI 驱动
- UI：LVGL v9.5.0 图形库，创建简易桌面风格 UI，支持导航、设置、时间等信息显示
- 输入：EC11 编码器，旋转用于导航，按键进入/确认，长按返回
- 网络：WiFi 连接管理，NTP 时间同步
- 核心功能：番茄钟计时、智能聊天助手
- 结构：模块化实现，便于扩展更多输入设备与网络功能

## 2. 需求清单

### 2.1 番茄钟功能
- 可配置的工作时长（默认25分钟）
- 可配置的休息时长（默认5分钟）
- 倒计时显示（分钟:秒格式）
- 完成次数记录
- 开始/暂停/重置操作
- 计时完成提醒（蜂鸣器）

### 2.2 聊天助手功能
- 助手表情显示
- 对话内容显示
- 交互式聊天界面

### 2.3 基本功能
- 主界面显示时间、温度/湿度、WiFi 状态、番茄钟状态
- 设置界面：亮度、对比度、语言、WiFi 配置、番茄钟设置等
- WiFi 配网：扫描、选择、输入密码、连接状态显示
- NTP 时间同步，时区配置

### 2.4 输入控制
- EC11编码器：旋转导航，按键返回
- SET设置按键：确认

### 2.5 性能与稳定性
- LVGL 主循环与任务调度的合理优先级
- DMA/硬件 SPI 优化数据传输

### 2.6 安全与容错
- 连接失败时提供可观测提示
- 输入消抖与按键去抖处理

### 2.7 兼容性
- 支持 240x240 分辨率，易扩展到其他分辨率

## 3. 系统架构

### 3.1 硬件抽象
- LCD 显示：ST7789（SPI，DMA 优化）
- 蜂鸣器：PWM 驱动

### 3.2 软件模块
- **ui_manager**：UI 框架，界面切换、绘制、文本更新
- **ui_screen_main**：主界面
- **ui_screen_pomodoro**：番茄钟界面
- **ui_screen_chat**：聊天助手界面
- **ui_screen_settings**：设置界面
- **ui_screen_wifi**：WiFi列表界面
- **ui_screen_password**：密码输入界面
- **wifi_manager**：WiFi 连接、配置、断线重连、NTP 配置
- **input_handler**：输入事件处理（编码器、按键）
- **pomodoro_engine**：番茄钟核心逻辑
- **time_service**：NTP 时间同步服务
- **storage_service**：NVS 存储服务
- **driver**：硬件驱动（LCD、蜂鸣器）
- **main**：系统初始化、任务启动、事件路由

## 4. 接口与契约

### 4.1 ui_manager
- 初始化接口：`ui_init()`
- 切换界面：`ui_switch(screen_id)`
- 更新数据：`ui_update_time(time_str)`、`ui_update_wifi(status)`

### 4.2 wifi_manager
- `init_wifi(config)`
- `scan_networks() -> list`
- `connect(ssid, password)`
- `get_status() -> {connected, ip, rssi, etc.}`

### 4.3 input_handler
- `input_handler_init()`
- `input_handler_get_event() -> {CW, CCW, PRESS, RELEASE, LONG_PRESS}`

### 4.4 pomodoro_engine
- `pomodoro_init()`
- `pomodoro_start()`
- `pomodoro_pause()`
- `pomodoro_reset()`
- `pomodoro_get_status() -> {state, time_remaining, completed_count}`

### 4.5 time_service
- `time_service_init()`
- `time_service_sync()`
- `time_service_get_time() -> struct tm`

### 4.6 storage_service
- `storage_init()`
- `storage_save_config(key, value)`
- `storage_load_config(key) -> value`

## 5. 输入控制说明

### 5.1 硬件连接
- EC11编码器
  - GPIO4: A相 (EC11_A)
  - GPIO5: B相 (EC11_B)
  - GPIO21: 按键 (EC11_K)
- 设置按键
  - GPIO9: SET按键

### 5.2 界面导航规则

#### EC11编码器旋转 (CW/CCW)

| 当前界面 | 状态 | 操作 |
| -------- | ---- | ---- |
| 主界面 | - | 顺时针->番茄钟, 逆时针->设置 |
| 番茄钟 | - | 顺时针->聊天, 逆时针->主界面 |
| 聊天 | - | 顺时针->设置, 逆时针->番茄钟 |
| 设置(空闲) | - | 顺时针->主界面, 逆时针->聊天 |
| 设置 | 选择 | 切换设置项 |
| 设置 | 调整 | 调整数值 |
| 番茄钟设置 | - | 调整数值 |
| WiFi列表 | - | 切换选中SSID |
| 密码输入 | - | 切换选中字符 |

#### EC11编码器按键 + SET按键

| 当前界面 | 状态 | EC11按键 | SET按键 |
| -------- | ---- | -------- | ------- |
| 主界面/番茄钟/聊天 | - | 无动作 | 无动作 |
| 设置 | 空闲 | 无动作 | 进入选择模式 |
| 设置 | 选择 | WiFi项->WiFi列表, Pomodoro项->番茄钟设置, 其他->进入调整模式 | 同EC11按键 |
| 设置 | 调整 | 退出->选择 | 无动作 |
| 番茄钟设置 | - | 返回设置 | 返回设置 |
| WiFi列表 | - | 确认连接 | 确认连接 |
| 密码输入 | - | 取消返回 | 输入字符 |

### 5.3 设置界面状态机

```
空闲模式 --SET按键--> 选择模式 --SET按键--> 调整模式
   ^                      |                       |
   |                      |                       |
   +-----------<---------+-------+---------------+
              EC11旋转              EC11按键
```

### 5.4 设置项说明
- 0: Brightness (0-100): 屏幕亮度
- 1: Contrast (0-100): 屏幕对比度
- 2: Language: English / Chinese
- 3: Pomodoro: 番茄钟工作时间设置
- 4: WiFi: 进入WiFi配网流程

## 6. 架构约束与前提

- 资源约束：RAM 充足，LVGL 版本兼容性，240x240 的分辨率
- 依赖：ESP-IDF v5.5.2、LVGL v9.5.0、ST7789 驱动、NTP 库、NVS 等
- 构建：ESP-IDF 脚手架，使用 idf.py 构建与烧录

## 7. 构建与测试

- 构建：`idf.py build`
- 烧录：`idf.py flash`
- 监控：`idf.py monitor`
- 测试用例（建议后续实现）
  - 主界面能正确显示时间与 WiFi 状态
  - 编码器导航可用，设置项可进入及修改
  - 能扫描并连接到 WiFi，NTP 同步成功
  - 番茄钟计时功能正常工作

## 8. 版本与变更

- 初始版本：2026-02-20
- 版本号：v1.0.0
- 更新日期：2026-03-11

## 9. 术语表

- LVGL：Light and Versatile Graphics Library，嵌入式 GUI 框架
- ST7789：彩色 LCD 控制器，SPI 总线
- EC11：编码器，带按键
- NTP：网络时间协议，用于时间同步
- NVS：Non-Volatile Storage，非易失性存储
- GPIO：通用输入输出引脚
- ESP-IDF：Espressif IoT Development Framework，乐鑫物联网开发框架

## 10. 风险与缓解

- 风格统一性：确保界面风格一致，避免混乱的 UI 设计
- 运行时资源预算：LVGL 渲染与 UI 动画对内存影响，按需裁剪选项
- 网络稳定性：WiFi 连接断开时的重连机制

---

注：本文档为初版，后续需要根据硬件改动与功能扩展进行更新。
