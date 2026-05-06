# 设置页面重构设计规格

## 概述

将主设置页面从平铺列表重构为分类导航。主设置页只显示6个分类入口，每个入口导航到对应子页面。操作逻辑统一：NAV 滚动 → SET 进入/切换 → 编码器按键返回。对于只有两种值的设置项（如开关），按 SET 直接切换，不进入编辑模式。

## 主设置页面

纯导航列表，6 个分类入口，无内联调整项。

| 序号 | 名称 | 目标子页面 |
|------|------|-----------|
| 0 | Pomodoro | `UI_SCREEN_SETTINGS_POMODORO`（已有） |
| 1 | Buddy | `UI_SCREEN_SETTINGS_BUDDY`（新建） |
| 2 | Light | `UI_SCREEN_SETTINGS_LIGHT`（已有） |
| 3 | WiFi | `UI_SCREEN_WIFI_SAVED`（已有） |
| 4 | Time | `UI_SCREEN_SETTINGS_TIME`（新建） |
| 5 | System | `UI_SCREEN_SETTINGS_SYSTEM`（新建） |

### 操作

- IDLE：编码器滚动无响应，SET 进入选择模式
- SELECT（绿色）：编码器滚动选分类，SET 进入子页面，编码器按键返回 IDLE/回主界面

## 子页面：Buddy（新建）

| 序号 | 名称 | 类型 | 范围 | NVS 键 |
|------|------|------|------|--------|
| 0 | Species | ADJUST 增减 | 物种列表循环 | `buddy`/`species` |

- SET 进入 ADJUST，编码器增减切换物种，SET 保存退出，编码器按键取消退出
- 编码器按键/长按返回设置主页

## 子页面：Time（新建）

| 序号 | 名称 | 类型 | 范围 | NVS 键 |
|------|------|------|------|--------|
| 0 | Timezone | ADJUST 增减 | UTC-12 ~ UTC+14 | `settings`/`timezone` |
| 1 | NTP Server | ADJUST 增减 | 预设服务器列表 | `settings`/`ntp_server` |
| 2 | NTP Interval | ADJUST 增减 | 0 ~ 120 分钟（0=禁用） | `settings`/`ntp_interval` |

### NTP 预设服务器列表

| 索引 | 地址 | 显示名 |
|------|------|--------|
| 0 | pool.ntp.org | NTP Pool |
| 1 | cn.ntp.org.cn | China |
| 2 | ntp.aliyun.com | Aliyun |
| 3 | time.google.com | Google |
| 4 | time.windows.com | Windows |

- Timezone：SET 进入 ADJUST，编码器增减，SET 保存退出，编码器按键取消退出
- NTP Server：SET 进入 ADJUST，编码器增减选择服务器，SET 保存退出并调用 `time_service_set_ntp_server()` 持久化索引到 NVS，编码器按键取消退出
- NTP Interval：SET 进入 ADJUST，编码器增减调整间隔分钟数，SET 保存退出，编码器按键取消退出
- `time_service` 需增加 `time_service_set_ntp_server_index(int index)` / `time_service_get_ntp_server_index()` 接口

## 子页面：System（新建）

| 序号 | 名称 | 类型 | 范围 | NVS 键 |
|------|------|------|------|--------|
| 0 | Sound | SET 直接切换 | On / Off | `settings`/`sound` |
| 1 | Direction | SET 直接切换 | Normal / Rev | `settings`/`enc_dir` |
| 2 | Language | SET 直接切换 | English / Chinese | `settings`/`lang` |

- 所有项按 SET 直接切换值（无编辑模式）
- 编码器按键/长按返回设置主页
- Language 切换后持久化到 NVS

## 已有子页面（不变）

### Pomodoro（已有，不变）

7 项：Work, Break, Long Break, Cycles, Mode, Default, Reset

### Light（已有，不变）

6 项：On/Off, Brightness, Speed, Style, Anim, Demo

### WiFi（已有 wifi_saved，不变）

WiFi 管理：扫描、连接、编辑密码、删除

## 存储键统一规范

所有设置项通过 `storage_service.h` 中定义的常量统一管理。键名使用 `模块_属性` 格式。

### `settings` 命名空间（通用设置）

| NVS 键常量 | 键值 | 类型 | 用途 | 原键名 |
|------------|------|------|------|--------|
| `KEY_TIMEZONE` | `"timezone"` | int32 | 时区偏移 | 不变 |
| `KEY_NTP_SERVER` | `"ntp_server"` | int32 | NTP 服务器索引 | 新增 |
| `KEY_NTP_INTERVAL` | `"ntp_interval"` | int32 | NTP 同步间隔 | 保留兼容 |
| `KEY_TIME_HIGH` | `"time_high"` | int32 | 时间戳高位 | 不变 |
| `KEY_TIME_LOW` | `"time_low"` | int32 | 时间戳低位 | 不变 |
| `KEY_SOUND` | `"sound"` | int32 | 声音开关 | 原 `"sound_on"` |
| `KEY_ENC_DIR` | `"enc_dir"` | int32 | 编码器方向 | 原 `"enc_rev"` |
| `KEY_LANG` | `"lang"` | int32 | 语言 | 新增 |
| `KEY_LED_ON` | `"led_on"` | int32 | LED 开关 | 不变 |
| `KEY_LED_BRIGHT` | `"led_bright"` | int32 | LED 亮度 | 不变 |
| `KEY_LED_SPEED` | `"led_speed"` | int32 | LED 速度 | 不变 |
| `KEY_LED_STYLE` | `"led_style"` | int32 | LED 风格 | 不变 |
| `KEY_LED_ANIM` | `"led_anim"` | int32 | LED 动效 | 不变 |

### `pomodoro` 命名空间（番茄钟）

已有，不变。

### `wifi` 命名空间（WiFi）

已有，不变。

### `buddy` 命名空间（宠物）

已有，不变。

### 迁移

启动时检测旧键名，如存在则迁移到新键名：
- `"settings"` / `"sound_on"` → `"settings"` / `"sound"`
- `"settings"` / `"enc_rev"` → `"settings"` / `"enc_dir"`

`time_service_init()` 中增加 NTP 服务器索引的 NVS 加载。

## 架构变更

### 新增文件

- `main/ui/ui_screen_settings_buddy.c` / `.h` — Buddy 子设置页
- `main/ui/ui_screen_settings_time.c` / `.h` — Time 子设置页
- `main/ui/ui_screen_settings_system.c` / `.h` — System 子设置页

### 修改文件

- `main/ui/ui_manager.h` — 枚举添加 `UI_SCREEN_SETTINGS_BUDDY`, `UI_SCREEN_SETTINGS_TIME`, `UI_SCREEN_SETTINGS_SYSTEM`
- `main/ui/ui_manager.c` — 注册新屏幕
- `main/ui/ui_screen_settings.c` — 简化为纯导航，移除内联调整逻辑
- `main/service/storage_service.h` — 添加新键常量
- `main/service/time_service.c` / `.h` — 添加 NTP 服务器索引持久化接口
- `main/main.c` — 初始化顺序调整
- `main/CMakeLists.txt` — 添加新源文件

### 新增枚举值

```c
typedef enum {
    ...
    UI_SCREEN_SETTINGS_LIGHT,
    UI_SCREEN_SETTINGS_BUDDY,
    UI_SCREEN_SETTINGS_TIME,
    UI_SCREEN_SETTINGS_SYSTEM,
    UI_SCREEN_COUNT
} ui_screen_id_t;
```

## 操作模式统一

所有子页面遵循相同模式（与现有 pomodoro/light 子页面一致）：

| 模式 | 高亮色 | 编码器旋转 | SET | 编码器按键 |
|------|--------|-----------|-----|-----------|
| NAV | 绿色 | 滚动选择项 | 进入编辑/直接切换/导航 | 返回上层 |
| ADJUST | 黄色 | 调整值 | 保存退出 | 取消退出 |

二值项（Sound, Direction, Language, Mode）在 NAV 模式下 SET 直接切换，不进入 ADJUST。
多值项（Species, NTP Server, Timezone, 各 Pomodoro 时间等）SET 进入 ADJUST 模式，编码器增减选择。
