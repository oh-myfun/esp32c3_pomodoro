# 传感器数据页面设计

## 概述

在主界面和番茄钟页面之间新增传感器页面，实时采集和展示 AHT20（温湿度）+ BMP280（气压/海拔）数据。后台独立任务负责采集和多级聚合，UI 层仅负责显示。

## 页面布局（240x240）

### 传感器页面

```
┌────────────────────────────┐ y=0
│ 24.5°C 65% 1013hPa 42m     │ 顶部当前值（~24px高）
├────────────────────────────┤ y=28
│ 12:30         12:31        │ 时间轴标签（~12px高）
│ ─────┬─────┬─────┬─────   │ 时间轴刻度线
│                            │
│   [合并曲线图表]            │ 中部图表（~155px高）
│   4条归一化彩色曲线         │
│   温度=红 湿度=蓝          │
│   气压=紫 海拔=绿          │
│                            │
├────────────────────────────┤ y=204
│ ●秒级  SET:切换             │ 底部提示（~24px高）
└────────────────────────────┘ y=228
```

### 传感器设置页面

编码器按下进入，遵循现有设置子页面模式（类似 settings_light）：

```
┌────────────────────────────┐
│      传感器设置             │ 标题
├────────────────────────────┤
│ 温度最小  -10°C        ⇨  │ 列表项
│ 温度最大   50°C        ⇨  │
│ 气压最小  900hPa       ⇨  │
│ 气压最大 1100hPa       ⇨  │
│ 海拔最小  -100m        ⇨  │
│ 海拔最大 3000m         ⇨  │
│ 恢复默认          ⇨      │
├────────────────────────────┤
│ SET:编辑 编码器:返回        │ 底部提示
└────────────────────────────┘
```

- NAV 模式：CW/CCW 滚动选择项
- ADJUST 模式：CW/CCW 调节数值，SET 保存并退出
- "恢复默认" 项：SET 键直接恢复所有值为默认并保存
- 编码器按下：NAV 模式下返回传感器页面

## 导航

SENSOR 插在 MAIN 和 POMODORO 之间：

```
SETTINGS ← MAIN → SENSOR → POMODORO → BUDDY
              CCW   CW      CW         CW
```

- CW/CCW：页面导航
- SET 键（传感器页面）：循环切换时间粒度（秒→分→时→天）
- 编码器按下（传感器页面）：进入传感器设置页面
- 编码器按下（传感器设置页面）：返回传感器页面

主界面的温湿度/气压标签移除，这些数据只在传感器页面显示。

## 数据采集与聚合

### 后台任务（sensor_service）

独立的 FreeRTOS 任务，不依赖任何 UI：

- **任务优先级**：1（与 UIUpdate 同级）
- **栈大小**：4KB
- **采样频率**：每 1 秒读取 AHT20 + BMP280
- **聚合时机**：检测系统时间边界触发

### 多级环形缓冲

| 级别 | 缓冲大小 | 数据点间隔 | 保留时长 | 聚合触发 |
|------|---------|-----------|---------|---------|
| 秒级 | 60 | 1秒 | 1分钟 | 每秒写入 |
| 分钟级 | 60 | 1分钟 | 1小时 | 秒=0 时取前60秒均值 |
| 小时级 | 24 | 1小时 | 1天 | 分=0且秒=0 时取前60分均值 |
| 天级 | 30 | 1天 | 30天 | 时=0且分=0且秒=0 时取前24时均值 |

每个采样点存储 4 个 float（温度、湿度、气压、海拔），总计约 2.8KB RAM。

### 聚合算法

```
秒级写入：每秒读取传感器 → 写入 seconds[index % 60]
分钟聚合：当 time.second == 0 时 → avg(seconds[0..59]) → minutes[index % 60]
小时聚合：当 time.second==0 && time.minute==0 → avg(minutes[0..59]) → hours[index % 24]
天级聚合：当 time.second==0 && time.minute==0 && time.hour==0 → avg(hours[0..23]) → days[index % 30]
```

每个级别维护写索引（write_pos）和数据计数（count，最大不超过缓冲大小）。

## 线程安全

- sensor_service 内部使用 FreeRTOS 互斥量保护环形缓冲
- UI 通过 `sensor_service_get_current()` 和 `sensor_service_get_chart_data()` 读取
- 读取时持锁拷贝数据，不持锁操作 LVGL

## 图表渲染

### 归一化

4 个指标量纲不同，需归一化到 LVGL chart 的公共 Y 轴范围（0-100）。

归一化范围由用户设置的最大最小值决定（sensor_settings），默认值如下：

| 指标 | 默认最小值 | 默认最大值 | 归一化公式 |
|------|-----------|-----------|-----------|
| 温度 | -10°C | 50°C | (val - min) / (max - min) × 100 |
| 湿度 | 0% | 100% | val（直接使用） |
| 气压 | 900hPa | 1100hPa | (val - min) / (max - min) × 100 |
| 海拔 | -100m | 3000m | (val - min) / (max - min) × 100 |

用户通过传感器设置页面修改 min/max 后，归一化公式自动使用新值，超出范围的值截断到 0-100。

### 时间轴标注

图表横轴显示时间刻度：

- **左侧**（第 0 个数据点）和**右侧**（最新数据点）各标注一个时间文字
- 中间均匀分布 3-4 个刻度线（tick mark，仅短竖线，无文字）
- 时间格式根据当前级别：

| 级别 | 时间格式 | 示例（左→右） |
|------|---------|-------------|
| 秒级 | mm:ss | 30:45 → 30:52 → 31:45 |
| 分钟级 | HH:mm | 14:30 → 14:45 → 15:30 |
| 小时级 | dd HH | 17 08 → 17 14 → 18 08 |
| 天级 | MM/dd | 04/17 → 04/24 → 05/17 |

时间值由 sensor_service 根据采集时间戳计算，UI 层仅负责显示。

### 时间戳存储

为了支持时间轴标注，每个聚合级别额外存储一个时间戳：

```c
typedef struct {
    int16_t year;
    int8_t month;
    int8_t day;
    int8_t hour;
    int8_t minute;
    int8_t second;
} sensor_time_t;

// 每个级别维护一个时间戳数组，与数据点一一对应
static sensor_time_t seconds_time[60];
static sensor_time_t minutes_time[60];
static sensor_time_t hours_time[24];
static sensor_time_t days_time[30];
```

时间戳总计约 0.5KB（每条 8 字节 × 174 个槽位）。

### LVGL chart 配置

- 类型：`LV_CHART_TYPE_LINE`
- 点数：根据当前时间级别（秒=60, 分=60, 时=24, 天=30）
- 4 个 series，不同颜色
- 无分界线、无刻度标签（空间有限）
- 暗色背景，亮色曲线
- 时间轴标签使用 custom_font_12 或 custom_font_14 在 chart 上下方独立绘制

## 传感器设置

### 存储键（settings 命名空间）

```c
#define KEY_SENSOR_TEMP_MIN   "s_temp_min"   // 默认 -10 (存储为 int，单位 0.1°C)
#define KEY_SENSOR_TEMP_MAX   "s_temp_max"   // 默认 50
#define KEY_SENSOR_PRESS_MIN  "s_press_min"  // 默认 900 (单位 hPa)
#define KEY_SENSOR_PRESS_MAX  "s_press_max"  // 默认 1100
#define KEY_SENSOR_ALT_MIN    "s_alt_min"    // 默认 -100 (单位 m)
#define KEY_SENSOR_ALT_MAX    "s_alt_max"    // 默认 3000
```

温度用 0.1°C 精度存储（如 -10.0 → -100），其他为整数精度。

### sensor_service API 扩展

```c
typedef struct {
    int temp_min, temp_max;      // 0.1°C 单位
    int press_min, press_max;    // hPa
    int alt_min, alt_max;        // m
} sensor_settings_t;

void sensor_service_get_settings(sensor_settings_t *out);
void sensor_service_set_settings(const sensor_settings_t *in);
void sensor_service_reset_settings(void);
```

设置变更后立即生效（影响归一化范围），NVS 持久化。

### 设置页面行为

- 进入时从 sensor_service 读取当前设置
- ADJUST 模式下调节时实时更新归一化效果（需返回传感器页面才能看到图表变化）
- "恢复默认" 会同时更新 NVS 和 sensor_service 内部状态
- SET 保存当前项到 NVS，退出 ADJUST

## 文件结构

### 新增文件

| 文件 | 职责 |
|------|------|
| `main/service/sensor_service.c` | 后台采集任务 + 环形缓冲 + 聚合 + 设置管理 + 线程安全 API |
| `main/service/sensor_service.h` | 公共接口 + 数据结构定义 |
| `main/ui/ui_screen_sensor.c` | 传感器页面 LVGL 界面（数值 + 图表 + 时间轴） |
| `main/ui/ui_screen_sensor.h` | 页面接口 |
| `main/ui/ui_screen_settings_sensor.c` | 传感器设置子页面（min/max + 恢复默认） |
| `main/ui/ui_screen_settings_sensor.h` | 设置页面接口 |

### 修改文件

| 文件 | 改动 |
|------|------|
| `main/CMakeLists.txt` | 添加 sensor_service.c、ui_screen_sensor.c、ui_screen_settings_sensor.c |
| `main/ui/ui_manager.h` | 添加 `UI_SCREEN_SENSOR` 和 `UI_SCREEN_SETTINGS_SENSOR` 枚举值 |
| `main/ui/ui_screen_main.c` | 移除温湿度气压 label，CW 导航改为 SENSOR |
| `main/ui/ui_screen_pomodoro.c` | CCW 导航改为 SENSOR（当未运行时） |
| `main/main.c` | 移除主界面传感器读取逻辑，调用 sensor_service_init() |
| `main/service/storage_service.h` | 添加传感器设置的 NVS key |
| `main/ui/i18n.h` + `i18n.c` | 添加传感器相关字符串 |

## sensor_service 公共接口

```c
typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float altitude;
} sensor_sample_t;

typedef struct {
    int16_t year;
    int8_t month;
    int8_t day;
    int8_t hour;
    int8_t minute;
    int8_t second;
} sensor_time_t;

typedef struct {
    int temp_min, temp_max;      // 0.1°C
    int press_min, press_max;    // hPa
    int alt_min, alt_max;        // m
} sensor_settings_t;

typedef enum {
    SENSOR_LEVEL_SECONDS = 0,  // 60 points, 1s interval
    SENSOR_LEVEL_MINUTES,      // 60 points, 1min interval
    SENSOR_LEVEL_HOURS,        // 24 points, 1h interval
    SENSOR_LEVEL_DAYS,         // 30 points, 1d interval
    SENSOR_LEVEL_COUNT
} sensor_level_t;

void sensor_service_init(void);
sensor_sample_t sensor_service_get_current(void);
int sensor_service_get_chart_data(sensor_level_t level, sensor_sample_t *buf, sensor_time_t *time_buf, int buf_size);
void sensor_service_get_settings(sensor_settings_t *out);
void sensor_service_set_settings(const sensor_settings_t *in);
void sensor_service_reset_settings(void);
```

## 初始化顺序

在 `app_main()` 中，sensor_service_init() 放在 aht20_init() 和 bmp280_init() 之后，创建后台采集任务。

## i18n 新增字符串

```c
STR_T_SENSOR,          // "Sensor" / "传感器"
STR_SENSOR_SETTINGS,   // "Sensor Settings" / "传感器设置"
STR_TEMP_MIN,          // "Temp Min" / "温度最小"
STR_TEMP_MAX,          // "Temp Max" / "温度最大"
STR_PRESS_MIN,         // "Press Min" / "气压最小"
STR_PRESS_MAX,         // "Press Max" / "气压最大"
STR_ALT_MIN,           // "Alt Min" / "海拔最小"
STR_ALT_MAX,           // "Alt Max" / "海拔最大"
STR_FMT_TEMP_RANGE,    // "%.1f°C" / "%.1f°C"
STR_FMT_PRESS_RANGE,   // "%dhPa" / "%dhPa"
STR_FMT_ALT_RANGE,     // "%dm" / "%dm"
STR_SEC_LEVEL,         // "Sec" / "秒级"
STR_MIN_LEVEL,         // "Min" / "分级"
STR_HOUR_LEVEL,        // "Hour" / "时级"
STR_DAY_LEVEL,         // "Day" / "天级"
```

## 验证

1. 编译烧录，确认后台任务正常启动，串口无错误
2. 进入传感器页面，确认实时数值正确显示
3. SET 键切换时间级别，图表点数和提示文字正确变化
4. 编码器按下进入传感器设置，调节温度最小值，返回后图表 Y 轴范围变化
5. "恢复默认" 恢复所有设置
6. 切换到其他页面，等待 1 分钟后返回，确认分钟级数据已聚合
7. 时间轴标注随级别切换正确显示格式
8. 主界面不再显示温湿度气压
9. 导航：MAIN CW→SENSOR CW→POMODORO，反向 CCW
