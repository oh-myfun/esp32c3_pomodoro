#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t r, g, b;
} led_color_t;

typedef enum { LED_ANIM_BREATH, LED_ANIM_SCAN, LED_ANIM_GRADIENT } led_anim_t;
typedef enum { LED_STYLE_PURE, LED_STYLE_COLORFUL } led_style_t;
typedef enum { LED_SPEED_SLOW, LED_SPEED_MEDIUM, LED_SPEED_FAST } led_speed_t;

// Color constants (fully saturated for max contrast on WS2812)
#define LED_COLOR_WORK       (led_color_t){255, 0, 0}
#define LED_COLOR_BREAK      (led_color_t){0, 255, 0}
#define LED_COLOR_LONG_BREAK (led_color_t){0, 80, 255}
#define LED_COLOR_PAUSED     (led_color_t){255, 255, 0}
#define LED_COLOR_ATTENTION  (led_color_t){255, 0, 0}
#define LED_COLOR_CELEBRATE  (led_color_t){0, 255, 0}
#define LED_COLOR_SAD        (led_color_t){255, 0, 100}
#define LED_COLOR_BUSY       (led_color_t){0, 120, 255}
#define LED_COLOR_IDLE       (led_color_t){0, 80, 0}
#define LED_COLOR_SLEEP      (led_color_t){0, 0, 40}
#define LED_COLOR_HEART      (led_color_t){255, 50, 100}

// Demo color list
#define LED_DEMO_COLOR_COUNT 5
extern const led_color_t led_demo_colors[LED_DEMO_COLOR_COUNT];
extern const char *const led_demo_color_names[LED_DEMO_COLOR_COUNT];

void led_service_init(void);

// Wait source identifiers (bitmask indices)
#define LED_WAIT_POMODORO  0
#define LED_WAIT_BUDDY     1

// Scene triggers (same timing as sound effects)
void led_service_play(led_color_t color);         // Outer ring: play 2 rounds (overrides current)
void led_service_wait(led_color_t color, uint8_t source);  // Center LED: start looping (source-tracked)
void led_service_wait_done(uint8_t source);       // Clear one source's wait; all-off when none left
void led_service_stop(void);                      // Force all off, clear all waits

// Settings
void led_service_set_enabled(bool on);
bool led_service_is_enabled(void);
void led_service_set_brightness(uint8_t level);  // 1-10
uint8_t led_service_get_brightness(void);
void led_service_set_animation(led_anim_t anim);
led_anim_t led_service_get_animation(void);
void led_service_set_style(led_style_t style);
led_style_t led_service_get_style(void);
void led_service_set_speed(led_speed_t speed);
led_speed_t led_service_get_speed(void);

// Demo
void led_service_demo_start(led_color_t color);
void led_service_demo_change_color(led_color_t color);
void led_service_demo_stop(void);
bool led_service_is_demo_active(void);
