#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t type;
    uint8_t step;  /* encoder acceleration step (1 for non-encoder events) */
} input_event_t;

#define INPUT_EVENT_NONE                0
#define INPUT_EVENT_ENCODER_CW          1
#define INPUT_EVENT_ENCODER_CCW         2
#define INPUT_EVENT_ENCODER_PRESS       3
#define INPUT_EVENT_ENCODER_LONG_PRESS  4
#define INPUT_EVENT_SETTINGS_PRESS      5
#define INPUT_EVENT_SETTINGS_LONG_PRESS 6
#define INPUT_EVENT_DUAL_LONG_PRESS     7
#define INPUT_EVENT_ENCODER_HOLD        8
#define INPUT_EVENT_SETTINGS_HOLD       9

void input_handler_init(void);
void input_handler_task(void *arg);
void input_handler_set_reverse(bool reverse);
bool input_handler_get_reverse(void);
