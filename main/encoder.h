#ifndef ENCODER_H
#define ENCODER_H

#include "driver/gpio.h"
#include <stdbool.h>

// EC11编码器引脚定义
#define EC11_A_GPIO     GPIO_NUM_4   // A相
#define EC11_B_GPIO     GPIO_NUM_5   // B相
#define EC11_K_GPIO     GPIO_NUM_21  // K键 (TXD0 / GPIO21)

// 设置按键GPIO
#define SETTINGS_BTN_GPIO   GPIO_NUM_9  // 设置按键

// 编码器事件类型
typedef enum {
    EC11_EVENT_NONE = 0,
    EC11_EVENT_CW,           // 顺时针旋转
    EC11_EVENT_CCW,          // 逆时针旋转
    EC11_EVENT_PRESS,       // 按键按下
    EC11_EVENT_RELEASE,     // 按键释放
    EC11_EVENT_LONG_PRESS   // 长按事件
} ec11_event_t;

// 初始化编码器
void encoder_init(void);

// 获取编码器事件（非阻塞，需定期调用）
ec11_event_t encoder_get_event(void);

// 获取按键按下时长（毫秒）
uint32_t encoder_get_press_duration_ms(void);

// 检测设置按键是否被按下
bool settings_button_get_event(void);

#endif
