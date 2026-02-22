# 功能说明文档 (Functional Specification)

项目名称：ST7789 LVGL ESP32-C3 Demo
目标：给定 ESP32-C3 平台，基于 ST7789 LCD、LVGL 图形库、EC11 编码器输入、WiFi/NTP 功能的演示项目。本文档用于澄清需求、系统架构、模块接口和验收标准，方便后续迭代与维护。

## 1. 概要

- 目标平台：ESP32-C3
- 显示：ST7789 240x240 LCD，通过 SPI 驱动
- UI：LVGL 图形库，创建简易桌面风格 UI，支持导航、设置、时间等信息显示
- 输入：EC11 编码器，旋转用于导航，按键进入/确认，长按返回
- 网络：WIFI 连接管理，NTP 时间同步
- 结构：模块化实现，便于扩展更多输入设备与网络功能

## 2. 需求清单

- 基本功能
  - 主界面显示时间、温度/湿度（示例数据）、WiFi 状态
  - 番茄钟界面显示倒计时、完成次数
  - 设置界面：亮度、对比度、语言、WiFi 配置等
  - WiFi 配网：扫描、选择、输入密码、连接状态显示
  - NTP 时间同步，时区配置
- 输入控制
  - EC11编码器：旋转导航，按键返回
  - SET设置按键：确认
- 性能与稳定性
  - LVGL 主循环与任务调度的合理优先级
  - DMA/硬件 SPI 优化数据传输
- 安全与容错
  - 连接失败时提供可观测提示
  - 输入消抖与按键去抖处理
- 兼容性
  - 支持 240x240 分辨率，易扩展到其他分辨率

## 3. 系统架构

- 硬件抽象
  - LCD 显示：ST7789（SPI，DMA 优化）
- 软件模块
  - ui_manager：UI 框架，界面切换、绘制、文本更新
  - lvgl_example：LVGL 入口和示例场景
  - wifi_manager：WiFi 连接、配置、断线重连、NTP 配置
  - encoder：EC11 编码器驱动（旋转、按键、消抖）
  - main：系统初始化、任务启动、事件路由

## 4. 接口与契约

- ui_manager
  - 初始化接口：ui_init()
  - 切换界面：ui_switch(screen_id)
  - 更新数据：ui_update_time(time_str)、ui_update_wifi(status)
- wifi_manager
  - init_wifi(config)
  - scan_networks() -> list
  - connect(ssid, password)
  - get_status() -> {connected, ip, rssi, etc.}
- encoder
  - init_encoder(pin_a, pin_b, pin_key)
  - get_event() -> {CW, CCW, PRESS, RELEASE, LONG_PRESS}

## 5. 输入控制说明

### 硬件连接
- EC11编码器
  - GPIO4: A相 (EC11_A)
  - GPIO5: B相 (EC11_B)
  - GPIO21: 按键 (EC11_K)
- 设置按键
  - GPIO9: SET按键

### 界面导航规则

#### EC11编码器旋转 (CW/CCW)

| 当前界面 | 状态 | 操作 |
| -------- | ---- | ---- |
| 主界面 | - | 顺时针→番茄钟, 逆时针→设置 |
| 番茄钟 | - | 顺时针→设置, 逆时针→主界面 |
| 设置(空闲) | - | 顺时针→主界面, 逆时针→番茄钟 |
| 设置 | 选择 | 切换设置项 |
| 设置 | 调整 | 调整数值 |
| 番茄钟设置 | - | 调整数值 |
| WiFi列表 | - | 切换选中SSID |
| 密码输入 | - | 切换选中字符 |

#### EC11编码器按键 + SET按键

| 当前界面 | 状态 | EC11按键 | SET按键 |
| -------- | ---- | -------- | ------- |
| 主界面/番茄钟 | - | 无动作 | 无动作 |
| 设置 | 空闲 | 无动作 | 进入选择模式 |
| 设置 | 选择 | WiFi项→WiFi列表, Pomodoro项→番茄钟设置, 其他→进入调整模式 | 同EC11按键 |
| 设置 | 调整 | 退出→选择 | 无动作 |
| 番茄钟设置 | - | 返回设置 | 返回设置 |
| WiFi列表 | - | 确认连接 | 确认连接 |
| 密码输入 | - | 取消返回 | 输入字符 |

### 设置界面状态机

```
空闲模式 ──SET按键──> 选择模式 ──SET按键──> 调整模式
   ^                      |                       |
   |                      |                       |
   +───────────<─────────┴───────┬───────────────┘
              EC11旋转              EC11按键
```

### 设置项说明
- 0: Brightness (0-100): 屏幕亮度
- 1: Contrast (0-100): 屏幕对比度
- 2: Language: English / Chinese
- 3: Pomodoro: 番茄钟工作时间设置
- 4: WiFi: 进入WiFi配网流程

## 6. 架构约束与前提

- 资源约束：RAM 充足，LVGL 版本兼容性，240x240 的分辨率
- 依赖：ESP-IDF、LVGL、ST7789 驱动、NTP 库、NVS 等
- 构建：ESP-IDF 脚手架，使用 idf.py 构建与烧录

## 7. 构建与测试

- 构建：idf.py build
- 烧录：idf.py flash
- 监控：idf.py monitor
- 测试用例（建议后续实现）
  - 主界面能正确显示时间与 WiFi 状态
  - 编码器导航可用，设置项可进入及修改
  - 能扫描并连接到 WiFi，NTP 同步成功

## 8. 版本与变更

- 初始版本：2026-02-20
- 版本号：v1.0.0

## 9. 术语表

- LVGL：Light and Versatile Graphics Library，嵌入式 GUI 框架
- ST7789：彩色 LCD 控制器，SPI 总线
- EC11：编码器，带按键
- NTP：网络时间协议，用于时间同步
- NVS：Non-Volatile Storage，非易失性存储
- GPIO：通用输入输出引脚

## 10. 风险与缓解

- 风格统一性：确保界面风格一致，避免混乱的 UI 设计
- 运行时资源预算：LVGL 渲染与 UI 动画对内存影响，按需裁剪选项

---

注：本文档为初版，后续需要根据硬件改动与功能扩展进行更新。
