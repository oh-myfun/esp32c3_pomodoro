# WS2812 LED 灯效系统设计规格

## 概述

为 9 颗 WS2812 灯珠（1 中心 + 8 外圈）设计分层灯效系统。中心灯用于简洁状态提示（仅暂停等待时循环），外圈用于显眼的瞬间提醒（状态切换时播放 1 轮）。灯效与音效触发时机一致，正常倒计时期间灭灯省电。

## 硬件布局

- 索引 0：中心灯
- 索引 1-8：外圈灯，顺时针排列
- 驱动：GPIO8，RMT TX，10MHz 分辨率
- 当前驱动仅支持单灯 `ws2812_set_color(r,g,b)`，需扩展为 `ws2812_set_pixels(rgb[9], count)`

## 配色

各场景主色复用番茄钟界面颜色定义：

| 触发事件 | 主色 RGB | 色值 |
|----------|----------|------|
| 番茄钟开始 / 休息结束 | 红 | (255, 107, 107) |
| 工作结束 / 休息开始 | 绿 | (76, 175, 80) |
| 长休息开始 | 蓝 | (77, 150, 255) |
| 暂停等待 | 黄 | (255, 255, 0) |
| Buddy Attention | 红 | (255, 0, 0) |
| Buddy Celebrate | 绿 | (0, 255, 0) |
| Buddy Sad | 粉 | (255, 80, 120) |

## 风格

两种风格，决定灯珠如何使用主色：

- **纯色**：色度固定为主色，亮度在周期内变化
- **彩色**：亮度固定，色度在主色附近沿色环偏移变化

## 动效

三种动效，中间灯和外圈各自渲染。

### 中间灯（单颗，简洁提示）

| 动效 | 纯色 | 彩色 |
|------|------|------|
| 呼吸 | 亮度 0 ↔ 设定亮度，正弦往返 | 色相从主色出发沿色环来回摆动 |
| 扫描 | 周期：设定亮度 → 淡出到 0 → 静止暗 → 回亮 | 色相沿色环单向旋转 360° → 静止 → 重复 |
| 渐变 | 亮度在设定值附近小幅波动（不到 0） | 色相在主色附近小幅来回偏移 |

### 外圈（8 颗，显眼提醒）

| 动效 | 纯色 | 彩色 |
|------|------|------|
| 呼吸 | 8 颗同步亮度起伏 | 8 颗同步色相漂移 |
| 扫描 | 1 颗最亮，后续按距离渐隐拖尾，顺时针旋转 | 彩色拖尾旋转 |
| 渐变 | 亮度波浪（相位偏移） | 彩虹波浪（相位偏移） |

### 扫描拖尾

当前主灯亮度 100%，后续第 n 颗灯亮度 = `100% × 0.5^n`（即 50%, 25%, 12.5%...），形成 4-5 颗可见的渐隐尾巴。

## 场景映射

灯效与音效触发时机完全对应。状态切换时只有外圈闪动，中间灯仅在外圈结束后且需要等待用户操作时才循环亮起。

| 状态 | 中间灯 | 外圈 |
|------|--------|------|
| 正常倒计时进行中 | 灭 | 灭 |
| 状态切换瞬间（与音效同步） | 灭 | 播放 1 轮扫描，结束后灭 |
| 暂停/等待用户操作 | 循环播放选中动效 | 灭 |
| 恢复瞬间（与音效同步） | 灭 | 播放 1 轮扫描，结束后灭 |
| Buddy 事件 | 灭 | 播放 1-2 轮，结束后灭 |
| 待机 | 灭 | 灭 |

### 流程举例（手动模式）

1. SET 开始 → 外圈红扫 1 轮 + 音效 → 全灭，倒计时
2. 工作结束 → 外圈绿扫 1 轮 + 音效 → 中间灯循环绿呼吸（等待确认）
3. SET 继续 → 外圈绿扫 1 轮 + 音效 → 全灭，休息倒计时
4. 休息结束 → 外圈红扫 1 轮 + 音效 → 中间灯循环红呼吸（等待确认）

### 流程举例（自动模式）

1. SET 开始 → 外圈红扫 1 轮 + 音效 → 全灭，倒计时
2. 工作结束 → 外圈绿扫 1 轮 + 音效 → 全灭，自动进入休息
3. 休息结束 → 外圈红扫 1 轮 + 音效 → 全灭，自动进入工作

## 亮度与速度

- **亮度**：1-10 档，映射到内部值 26-255。0 档等效关灯。所有输出颜色乘以亮度系数。
- **速度**：三档控制动效周期
  - 慢：3 秒
  - 中：1.5 秒
  - 快：0.8 秒

## 设置项

添加到主设置界面（ui_screen_settings.c）：

| 序号 | 设置项 | 类型 | 范围 | NVS 键 |
|------|--------|------|------|--------|
| 1 | LED | 切换 | On/Off | `settings/led_on` |
| 2 | LED 亮度 | 调节 | 1-10 | `settings/led_bright` |
| 3 | LED 速度 | 选择 | 慢/中/快 | `settings/led_speed` |
| 4 | LED 风格 | 选择 | 纯色/彩色 | `settings/led_style` |
| 5 | LED 动效 | 选择 | 呼吸/扫描/渐变 | `settings/led_anim` |
| 6 | LED 演示 | 子操作 | 选择主色后进入演示 | — |

### 演示模式

1. 选中"LED 演示"，滚动选择主色（红/绿/蓝/黄/粉等系统预设色）
2. 按 SET 进入：保存并暂停当前循环灯效 → 中间灯+外圈同时用选中主色循环播放当前动效
3. 演示中滚轮切换主色，灯效立即响应
4. 按 SET 或编码器按键退出 → 恢复之前的循环灯效

## 架构

### 文件结构

```
main/driver/ws2812.c/h       — 扩展：ws2812_set_pixels(rgb_t *pixels, uint8_t count)
main/service/led_service.c/h — 新增：场景接口、设置管理、动效渲染
```

### ws2812 驱动扩展

```c
typedef struct {
    uint8_t r, g, b;
} rgb_t;

void ws2812_set_pixels(const rgb_t *pixels, uint8_t count);
```

### led_service 接口

```c
typedef struct {
    uint8_t r, g, b;
} led_color_t;

typedef enum { LED_ANIM_BREATH, LED_ANIM_SCAN, LED_ANIM_GRADIENT } led_anim_t;
typedef enum { LED_STYLE_PURE, LED_STYLE_COLORFUL } led_style_t;
typedef enum { LED_SPEED_SLOW, LED_SPEED_MEDIUM, LED_SPEED_FAST } led_speed_t;

void led_service_init(void);

// 场景触发（与音效触发点一一对应）
void led_service_play(led_color_t color);        // 外圈播放 1 轮
void led_service_wait(led_color_t color);         // 中间灯开始循环
void led_service_stop(void);                      // 全灭

// 设置
void led_service_set_enabled(bool on);
bool led_service_is_enabled(void);
void led_service_set_brightness(uint8_t level);   // 1-10
uint8_t led_service_get_brightness(void);
void led_service_set_animation(led_anim_t anim);
led_anim_t led_service_get_animation(void);
void led_service_set_style(led_style_t style);
led_style_t led_service_get_style(void);
void led_service_set_speed(led_speed_t speed);
led_speed_t led_service_get_speed(void);

// 演示
void led_service_demo_start(led_color_t color);
void led_service_demo_change_color(led_color_t color);
void led_service_demo_stop(void);
```

### 渲染机制

- `esp_timer` 以 50ms 间隔触发帧回调
- 无活跃灯效时定时器停止（省电）
- 每帧分别计算中间灯（1 颗）和外圈（8 颗），合并为 9 颗调用 `ws2812_set_pixels()`
- `led_service_play()` 启动外圈动效，播放 1 轮后自动停止并检查是否需要启动中间灯等待
- `led_service_wait()` 启动中间灯循环，直到 `led_service_stop()` 或 `led_service_play()` 被调用

### 调用方

- `main.c`：音效触发的同一位置调用 `led_service_play(color)`
- `ui_screen_pomodoro.c`：暂停时调用 `led_service_wait(color)`，恢复/停止时调用 `led_service_stop()`
- `ui_screen_settings.c`：LED 设置项（开关、亮度、速度、风格、动效、演示）
- `buddy/buddy.c`：现有 `ws2812_set_color()` 直接调用迁移为 `led_service_play()`，移除对 ws2812 驱动的直接依赖

### 关键颜色常量

```c
#define LED_COLOR_WORK       (led_color_t){255, 107, 107}
#define LED_COLOR_BREAK      (led_color_t){76, 175, 80}
#define LED_COLOR_LONG_BREAK (led_color_t){77, 150, 255}
#define LED_COLOR_PAUSED     (led_color_t){255, 255, 0}
#define LED_COLOR_ATTENTION  (led_color_t){255, 0, 0}
#define LED_COLOR_CELEBRATE  (led_color_t){0, 255, 0}
#define LED_COLOR_SAD        (led_color_t){255, 80, 120}
```
