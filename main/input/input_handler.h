#pragma once

#include <stdint.h>
#include <stdbool.h>

// 输入事件类型
typedef enum {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_ENCODER_CW,      // 编码器顺时针
    INPUT_EVENT_ENCODER_CCW,     // 编码器逆时针
    INPUT_EVENT_ENCODER_PRESS,   // 编码器按键按下
    INPUT_EVENT_ENCODER_RELEASE, // 编码器按键释放
    INPUT_EVENT_ENCODER_LONG_PRESS, // 编码器按键长按
    INPUT_EVENT_SETTINGS_PRESS   // 独立设置键按下
} input_event_t;

void input_handler_init(void);
void input_handler_task(void *arg);
void input_handler_set_reverse(bool reverse);
bool input_handler_get_reverse(void);
