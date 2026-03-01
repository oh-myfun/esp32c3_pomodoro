# AGENTS.md - ESP32-C3 番茄钟与智能聊天助手设备

## 项目概述

本项目是一个基于 ESP32-C3 的嵌入式应用，目标设备为 240x240 ST7789 LCD 显示屏，配合 EC11 编码器输入和独立按键控制，实现番茄钟功能与基本 UI 交互。

## 技术栈

- **芯片**: ESP32-C3 (RISC-V)
- **框架**: ESP-IDF v5.5.2
- **GUI**: LVGL v9.5.0 (via espressif/esp_lvgl_port)
- **系统**: FreeRTOS
- **驱动组件**: espressif/button, espressif/knob, espressif/esp_lcd_st7789

## 项目结构

```
pomodoro/
├── main/
│   ├── driver/               # 硬件驱动
│   │   ├── encoder.c/h      # EC11 编码器 (iot_knob)
│   │   ├── st7789_lcd.c/h   # ST7789 LCD 驱动
│   │   └── buzzer.c/h       # 蜂鸣器驱动
│   ├── input/               # 输入处理
│   │   └── input_handler.c/h
│   ├── network/             # 网络服务
│   │   └── wifi_manager.c/h
│   ├── pomodoro/            # 番茄钟引擎
│   │   └── pomodoro_engine.c/h
│   ├── storage/             # 存储服务
│   │   └── storage_service.c/h
│   ├── time/                # 时间服务
│   │   └── time_service.c/h
│   ├── ui/                  # UI 界面
│   │   ├── ui_manager.c/h
│   │   ├── ui_list.c/h
│   │   ├── ui_screen_main.c/h
│   │   ├── ui_screen_pomodoro.c/h
│   │   ├── ui_screen_settings.c/h
│   │   ├── ui_screen_settings_pomodoro.c/h
│   │   ├── ui_screen_wifi.c/h
│   │   └── ui_screen_chat.c/h
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   └── main.c
├── managed_components/       # IDF Component Manager 依赖
│   ├── espressif__button/
│   ├── espressif__knob/
│   ├── espressif__esp_lvgl_port/
│   └── lvgl__lvgl/
├── docs/                    # 设计文档
├── sdkconfig
└── CMakeLists.txt
```

## 硬件引脚配置

| GPIO | 功能 | 说明 |
|------|------|------|
| GPIO4 | Encoder A | EC11 A相 |
| GPIO5 | Encoder B | EC11 B相 |
| GPIO6 | SPI SCL | 显示屏时钟 |
| GPIO7 | SPI SDA | 显示屏数据 |
| GPIO9 | Button | 独立设置键 |
| GPIO10 | SPI RS | 显示屏命令/数据选择 |
| GPIO21 | Encoder Key | EC11 按键 |

## 构建命令

```bash
# 进入项目目录
cd D:\Project\IOT\pomodoro

# 设置 ESP-IDF 环境 (Windows PowerShell)
$env:IDF_PATH="D:\Espressif\frameworks\esp-idf-v5.5.2"
& "$env:IDF_PATH\export.ps1"

# 或使用 ESP-IDF CLI
idf.py set-target esp32c3
idf.py build

# 烧录
idf.py -p COMX flash monitor
```

## 工具能力支持

本项目支持以下代码编辑和分析工具：

### 1. 代码搜索 (grep)

用于在项目中快速查找代码内容：

```bash
# 搜索函数定义
grep -r "pomodoro_engine_start" --include="*.c"

# 搜索头文件引用
grep -r "ui_manager.h" --include="*.c"

# 搜索特定模式
grep -r "malloc\|free" --include="*.c"
```

### 2. 文件定位 (glob)

用于通过文件名模式查找文件：

```bash
# 查找所有 C 源文件
glob "**/*.c"

# 查找特定目录下的头文件
glob "main/**/*.h"

# 查找 UI 相关文件
glob "main/ui/*.c"
```

### 3. 代码读取 (read)

用于读取特定文件内容：
- 支持指定行范围 (offset, limit)
- 大文件分片读取
- 自动行号前缀

### 4. 代码编辑 (edit)

用于精确修改代码：
- 精确字符串替换
- 支持 replaceAll 选项
- 替换前自动验证

### 5. 项目构建

```bash
# 使用 ninja 构建
ninja -C build

# 或使用 idf.py
idf.py build
```

## 代码规范要求

### 1. 头文件规范
- 所有组件头文件使用 `#pragma once` 防止重复包含
- 使用 `typedef void* handle_t` 模式封装句柄
- 导出类型定义放在 `#pragma once` 之后

### 2. 错误处理
- 返回值: 成功返回 `0`，失败返回 `-1` 或负值错误码
- 参数校验: 所有入口函数需检查空指针
- ESP-API 调用使用 `ESP_ERROR_CHECK()` 或手动检查返回值

### 3. 内存管理
- 使用 `malloc()`/`free()` 时需包含 `<stdlib.h>`
- 使用 `memset()`/`memcpy()` 时需包含 `<string.h>`
- 优先使用静态内存池或 ESP-IDF 内存分配 API

### 4. 任务与同步
- FreeRTOS 任务使用 `xTaskCreatePinnedToCore()` 绑定核心
- ESP32-C3 为单核 RISC-V 架构，所有任务运行在同一核心，可根据优先级分配
- 建议：高优先级任务处理实时交互，中等优先级处理 UI，低优先级处理网络

### 5. UI 输入架构

采用回调注册模式，实现界面与输入解耦：

```c
// ui_manager.h 定义输入回调接口
typedef struct {
    void (*on_encoder_cw)(void);
    void (*on_encoder_ccw)(void);
    void (*on_encoder_press)(void);
    void (*on_encoder_long_press)(void);
    void (*on_settings_press)(void);
} ui_input_callbacks_t;

// 各界面在创建时注册自己的回调
static const ui_input_callbacks_t cbs = {
    .on_encoder_cw = screen_on_cw,
    .on_encoder_ccw = screen_on_ccw,
};
ui_register_input_callbacks(UI_SCREEN_MAIN, &cbs);
```

### 6. 组件依赖
- 避免组件间循环依赖
- 驱动层 (driver/) 不依赖业务层
- 服务层 (network/, storage/, time/) 可互相调用

## 当前实现状态

### 已完成
- [x] 硬件驱动 (encoder, st7789_lcd, buzzer)
- [x] 输入处理 (input_handler) - 回调注册模式
- [x] 番茄钟引擎 (pomodoro)
- [x] UI 管理器 (ui_manager) - 回调分发
- [x] 6 个界面 (Main, Pomodoro, Chat, Settings, WiFi List, Password Input)
- [x] WiFi 管理器 (network/wifi_manager)
- [x] 时间服务 (time/time_service)
- [x] 存储服务 (storage/storage_service)

### 待实现
- [ ] 天气服务 API 集成
- [ ] 聊天助手功能 (预留)

## 常用 API 参考

### 番茄钟引擎
```c
void pomodoro_engine_init(void);
void pomodoro_engine_start(void);
void pomodoro_engine_pause(void);
void pomodoro_engine_resume(void);
void pomodoro_engine_stop(void);
void pomodoro_engine_reset(void);
void pomodoro_engine_tick(void);
pomodoro_state_t pomodoro_engine_get_state(void);
pomodoro_settings_t pomodoro_engine_get_settings(void);
```

### UI 管理器
```c
// 界面切换
void ui_switch_screen(ui_screen_id_t screen_id);
ui_screen_id_t ui_get_current_screen(void);

// 输入回调注册
void ui_register_input_callbacks(ui_screen_id_t screen, const ui_input_callbacks_t *cbs);

// 输入事件分发
void ui_dispatch_encoder_cw(void);
void ui_dispatch_encoder_ccw(void);
void ui_dispatch_encoder_press(void);
void ui_dispatch_settings_press(void);

// 显示更新
void ui_update_time(void);
void ui_update_temp(float temp);
void ui_update_humidity(float humidity);
void ui_pomodoro_update_state(uint8_t phase, uint32_t remaining_seconds, uint32_t completed);
```

### WiFi 管理
```c
void wifi_manager_init(void);
void wifi_manager_scan_start(void);
void wifi_manager_connect(const char *ssid, const char *password);
bool wifi_manager_is_connected(void);
const char* wifi_manager_get_ip_address(void);
void wifi_manager_sync_time(void);
```

## 调试建议

1. **串口日志**: 使用 `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()` 输出调试信息
2. **组件日志 TAG**: 统一格式为组件名，如 `static const char* TAG = "wifi_mgr";`
3. **最小化构建**: 修改 `sdkconfig` 中 `CONFIG_ESPTOOLPY_FLASHSIZE` 为合适大小可加快编译
4. **查看分区表**: `idf.py partition_table`

## 注意事项

1. LVGL v9.x API 较 v8.x 有较大变化，需参考官方迁移文档
2. ST7789 显示屏需配置正确的 RGB 顺序和时序参数
3. 编码器消抖由 iot_knob 组件内部处理，无需额外消抖逻辑
4. WiFi 配网信息需持久化到 NVS
5. 番茄钟倒计时需在独立任务中处理，避免阻塞 UI 任务
6. UI 输入采用回调注册模式，添加新界面时需在界面创建函数中注册回调
