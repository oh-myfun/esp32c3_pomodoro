# Pomodoro Buddy

基于 ESP32-C3 的番茄钟与 AI Buddy 伴侣设备，通过 BLE 连接 Claude Desktop 实现权限审批交互。

## 硬件连接

| GPIO | 功能 |
|------|------|
| GPIO4 | EC11 编码器 A 相 |
| GPIO5 | EC11 编码器 B 相 |
| GPIO6 | LCD SPI 时钟 (SCK) |
| GPIO7 | LCD SPI 数据 (MOSI) |
| GPIO8 | WS2812 RGB LED |
| GPIO9 | SET 按键 (低电平有效) |
| GPIO10 | LCD 数据/命令 (DC) |
| GPIO20 | 蜂鸣器 (PWM) |
| GPIO21 | EC11 编码器按键 |

## 构建与烧录

```bash
# 设置环境 (PowerShell)
$env:IDF_PATH="D:\Espressif\frameworks\esp-idf-v5.5.4"
& "$env:IDF_PATH\export.ps1"

# 构建
idf.py build

# 烧录
idf.py -p COM7 flash monitor
```

## 功能特性

- **番茄钟计时** — 可配置工作/休息/长休息时长，蜂鸣器提醒
- **AI Buddy 伴侣** — ASCII 像素宠物，多种物种和表情动画
- **BLE 权限审批** — 通过 Nordic UART Service 接收 Claude Desktop 心跳，实时审批工具权限
- **WS2812 LED 指示** — 红色闪烁表示等待审批，绿色表示通过，粉色表示心动
- **WiFi 管理** — 扫描、选择、密码输入，NVS 保存凭据
- **NTP 时间同步** — 连接 WiFi 后自动同步，支持时区配置
- **EC11 编码器控制** — 旋转导航界面，按键确认/返回
- **7 个 LVGL 界面** — 主界面、番茄钟、Buddy、设置、番茄钟设置、WiFi 列表、密码输入
