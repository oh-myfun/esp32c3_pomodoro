#include "led_service.h"
#include "ws2812.h"
#include "storage_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

static const char *TAG = "LED";

// LED layout
#define LED_COUNT       9
#define CENTER_INDEX    0
#define OUTER_COUNT     8
#define OUTER_INDEX     1   // outer ring starts at index 1

// Frame timer
#define FRAME_INTERVAL_US   20000   // 20ms = 50fps

// Speed periods in microseconds
#define SPEED_PERIOD_SLOW    2000000U
#define SPEED_PERIOD_MEDIUM  1000000U
#define SPEED_PERIOD_FAST     500000U

// State machine
typedef enum {
    LED_STATE_IDLE,
    LED_STATE_PLAYING_OUTER,
    LED_STATE_WAITING_CENTER,
} led_state_t;

// Demo state
typedef struct {
    bool active;
    led_color_t color;
    led_state_t saved_state;
    led_color_t saved_wait_color;
} demo_state_t;

// Module state
static struct {
    // Settings
    bool enabled;
    uint8_t brightness;     // 1-10
    led_speed_t speed;
    led_style_t style;
    led_anim_t anim;

    // Animation state machine
    led_state_t state;
    led_color_t play_color;
    led_color_t wait_color;
    int64_t play_start_us;      // when PLAYING_OUTER started
    int64_t wait_start_us;      // when WAITING_CENTER started
    uint8_t play_round;         // current round (1 or 2) for outer ring

    // Frame timer
    esp_timer_handle_t frame_timer;
    bool timer_running;

    // Wait flag: set by led_service_wait(), checked after outer ring completes
    bool wait_pending;

    // Demo
    demo_state_t demo;

    // Rendered pixel buffer
    rgb_t pixels[LED_COUNT];
} led;

// Demo color presets
const led_color_t led_demo_colors[LED_DEMO_COLOR_COUNT] = {
    LED_COLOR_WORK,
    LED_COLOR_BREAK,
    LED_COLOR_LONG_BREAK,
    LED_COLOR_PAUSED,
    LED_COLOR_SAD,
};

const char *const led_demo_color_names[LED_DEMO_COLOR_COUNT] = {
    "Work",
    "Break",
    "LongBreak",
    "Paused",
    "Sad",
};

// ---- HSV conversion ----

typedef struct {
    float h;   // 0-360
    float s;   // 0-1
    float v;   // 0-1
} hsv_t;

static hsv_t rgb_to_hsv(led_color_t c)
{
    float rf = c.r / 255.0f;
    float gf = c.g / 255.0f;
    float bf = c.b / 255.0f;

    float max_c = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
    float min_c = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
    float delta = max_c - min_c;

    hsv_t hsv;
    hsv.v = max_c;
    hsv.s = (max_c == 0.0f) ? 0.0f : (delta / max_c);

    if (delta == 0.0f) {
        hsv.h = 0.0f;
    } else if (max_c == rf) {
        hsv.h = fmodf((gf - bf) / delta, 6.0f);
        if (hsv.h < 0) hsv.h += 6.0f;
    } else if (max_c == gf) {
        hsv.h = (bf - rf) / delta + 2.0f;
    } else {
        hsv.h = (rf - gf) / delta + 4.0f;
    }
    hsv.h *= 60.0f;
    if (hsv.h < 0) hsv.h += 360.0f;

    return hsv;
}

static led_color_t hsv_to_rgb(hsv_t hsv)
{
    float h = fmodf(hsv.h, 360.0f);
    if (h < 0) h += 360.0f;
    float s = hsv.s < 0 ? 0 : (hsv.s > 1 ? 1 : hsv.s);
    float v = hsv.v < 0 ? 0 : (hsv.v > 1 ? 1 : hsv.v);

    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float rf, gf, bf;
    if (h < 60)       { rf = c;  gf = x;  bf = 0;  }
    else if (h < 120) { rf = x;  gf = c;  bf = 0;  }
    else if (h < 180) { rf = 0;  gf = c;  bf = x;  }
    else if (h < 240) { rf = 0;  gf = x;  bf = c;  }
    else if (h < 300) { rf = x;  gf = 0;  bf = c;  }
    else              { rf = c;  gf = 0;  bf = x;  }

    led_color_t rgb;
    rgb.r = (uint8_t)((rf + m) * 255.0f + 0.5f);
    rgb.g = (uint8_t)((gf + m) * 255.0f + 0.5f);
    rgb.b = (uint8_t)((bf + m) * 255.0f + 0.5f);
    return rgb;
}

// ---- Helpers ----

static uint8_t brightness_to_value(uint8_t level)
{
    // level 1-10 -> actual value (level*255+5)/10
    return (uint8_t)(((uint16_t)level * 255 + 5) / 10);
}

static int64_t get_period_us(void)
{
    switch (led.speed) {
    case LED_SPEED_SLOW:   return SPEED_PERIOD_SLOW;
    case LED_SPEED_MEDIUM: return SPEED_PERIOD_MEDIUM;
    case LED_SPEED_FAST:   return SPEED_PERIOD_FAST;
    }
    return SPEED_PERIOD_MEDIUM;
}

// Gradient pure: brightness oscillates ±1 level around current setting
static float gradient_level_factor(float phase)
{
    float cos_val = cosf(phase * 2.0f * (float)M_PI);  // +1 at start, -1 at mid
    float eff_level = (float)led.brightness + cos_val;  // peak first, dip later
    if (eff_level < 0) eff_level = 0;
    if (eff_level > 10) eff_level = 10;
    uint8_t lo = (uint8_t)eff_level;
    uint8_t hi = lo + 1;
    float frac = eff_level - lo;
    float val = brightness_to_value(lo) + (brightness_to_value(hi) - brightness_to_value(lo)) * frac;
    return val / (float)brightness_to_value(led.brightness);
}

static void apply_pixel(uint8_t index, led_color_t color, float brightness_factor)
{
    float factor = brightness_factor * brightness_to_value(led.brightness) / 255.0f;
    if (factor < 0) factor = 0;
    if (factor > 1) factor = 1;
    led.pixels[index].r = (uint8_t)(color.r * factor + 0.5f);
    led.pixels[index].g = (uint8_t)(color.g * factor + 0.5f);
    led.pixels[index].b = (uint8_t)(color.b * factor + 0.5f);
}

static void start_timer(void)
{
    if (!led.timer_running) {
        esp_timer_start_periodic(led.frame_timer, FRAME_INTERVAL_US);
        led.timer_running = true;
    }
}

static void stop_timer(void)
{
    if (led.timer_running) {
        esp_timer_stop(led.frame_timer);
        led.timer_running = false;
    }
}

static void all_off(void)
{
    memset(led.pixels, 0, sizeof(led.pixels));
    ws2812_set_pixels(led.pixels, LED_COUNT);
}

// ---- Animation rendering ----

// Center LED rendering
static led_color_t render_center(led_color_t base, float phase)
{
    hsv_t hsv = rgb_to_hsv(base);

    switch (led.anim) {
    case LED_ANIM_BREATH:
        if (led.style == LED_STYLE_PURE) {
            // cos + sqrt: starts at peak (main color), holds bright longer
            float raw = 0.5f * (1.0f + cosf(phase * 2.0f * (float)M_PI));
            hsv.v = sqrtf(raw);
        } else {
            // Hue oscillates around base, peak at start
            float hue_offset = 60.0f * cosf(phase * 2.0f * (float)M_PI);
            hsv.h = fmodf(hsv.h + hue_offset + 360.0f, 360.0f);
        }
        break;

    case LED_ANIM_SCAN:
        if (led.style == LED_STYLE_PURE) {
            // Brightness: max -> 0 -> hold dark -> back
            // Phase 0-0.4: fade out, 0.4-0.6: dark, 0.6-1.0: fade in
            float val;
            if (phase < 0.4f) {
                val = 1.0f - (phase / 0.4f);
            } else if (phase < 0.6f) {
                val = 0.0f;
            } else {
                val = (phase - 0.6f) / 0.4f;
            }
            hsv.v = val;
        } else {
            // Hue rotates 360 degrees -> hold -> repeat
            // Phase 0-0.7: rotate, 0.7-1.0: hold
            if (phase < 0.7f) {
                hsv.h = fmodf(hsv.h + phase / 0.7f * 360.0f, 360.0f);
            }
            // else hold current hue
        }
        break;

    case LED_ANIM_GRADIENT:
        if (led.style == LED_STYLE_PURE) {
            // Brightness oscillates ±1 level around current setting
            hsv.v = gradient_level_factor(phase);
        } else {
            // Hue swings +/-90 degrees, peak at start
            float hue_offset = 90.0f * cosf(phase * 2.0f * (float)M_PI);
            hsv.h = fmodf(hsv.h + hue_offset + 360.0f, 360.0f);
        }
        break;
    }

    return hsv_to_rgb(hsv);
}

// Outer ring rendering
static void render_outer(led_color_t base, float phase, float fade)
{
    hsv_t base_hsv = rgb_to_hsv(base);

    for (int i = 0; i < OUTER_COUNT; i++) {
        float led_phase;
        float brightness_factor;
        hsv_t hsv = base_hsv;

        switch (led.anim) {
        case LED_ANIM_BREATH:
            // All 8 same as center
            led_phase = phase;
            if (led.style == LED_STYLE_PURE) {
                float raw = 0.5f * (1.0f + cosf(led_phase * 2.0f * (float)M_PI));
                brightness_factor = sqrtf(raw);
            } else {
                float hue_offset = 60.0f * cosf(led_phase * 2.0f * (float)M_PI);
                hsv.h = fmodf(hsv.h + hue_offset + 360.0f, 360.0f);
                brightness_factor = 1.0f;
            }
            break;

        case LED_ANIM_SCAN: {
            // 1 bright peak rotates, trailing LEDs fade with 0.5^n decay
            float pos = phase * (float)OUTER_COUNT;
            float dist = pos - (float)i;
            // Wrap distance to [-4, 4]
            while (dist > 4.0f) dist -= 8.0f;
            while (dist < -4.0f) dist += 8.0f;

            if (led.style == LED_STYLE_PURE) {
                if (dist >= 0 && dist < 1.0f) {
                    // Bright peak
                    brightness_factor = 1.0f - dist * 0.5f;
                } else if (dist >= 1.0f) {
                    // Trailing fade
                    brightness_factor = powf(0.5f, dist);
                } else {
                    brightness_factor = 0.0f;
                }
            } else {
                // Colorful: peak is base color, trail shifts hue backward
                if (dist >= 0 && dist < 1.0f) {
                    brightness_factor = 1.0f - dist * 0.5f;
                } else if (dist >= 1.0f) {
                    brightness_factor = powf(0.5f, dist);
                    // Trail hue shifts 45° per unit distance from base
                    hsv.h = fmodf(base_hsv.h - dist * 45.0f + 360.0f, 360.0f);
                } else {
                    brightness_factor = 0.0f;
                }
            }
            break;
        }

        case LED_ANIM_GRADIENT: {
            // Each LED has phase offset i/8
            led_phase = phase + (float)i / (float)OUTER_COUNT;
            // Wrap to [0, 1)
            led_phase = led_phase - floorf(led_phase);

            if (led.style == LED_STYLE_PURE) {
                // Brightness oscillates ±1 level around current setting
                brightness_factor = gradient_level_factor(led_phase);
            } else {
                // Hue wave +/-90 degrees, peak at start
                float hue_offset = 90.0f * cosf(led_phase * 2.0f * (float)M_PI);
                hsv.h = fmodf(hsv.h + hue_offset + 360.0f, 360.0f);
                brightness_factor = 1.0f;
            }
            break;
        }

        default:
            brightness_factor = 0.0f;
            break;
        }

        led_color_t pixel_color = hsv_to_rgb(hsv);
        apply_pixel(OUTER_INDEX + i, pixel_color, brightness_factor * fade);
    }
}

// ---- Frame callback ----

static void frame_callback(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();

    if (led.demo.active) {
        // Demo mode: both center and outer animate continuously
        int64_t elapsed = now - led.play_start_us;
        float phase = fmodf((float)elapsed / (float)get_period_us(), 1.0f);

        led_color_t center = render_center(led.demo.color, phase);
        apply_pixel(CENTER_INDEX, center, 1.0f);
        render_outer(led.demo.color, phase, 1.0f);

        ws2812_set_pixels(led.pixels, LED_COUNT);
        return;
    }

    switch (led.state) {
    case LED_STATE_PLAYING_OUTER: {
        int64_t elapsed = now - led.play_start_us;
        int64_t period = get_period_us();
        float phase = (float)elapsed / (float)period;

        if (phase >= 1.0f) {
            if (led.play_round < 2) {
                led.play_round++;
                led.play_start_us = now;
                phase = 0.0f;
            } else {
                memset(&led.pixels[OUTER_INDEX], 0, OUTER_COUNT * sizeof(rgb_t));
                ws2812_set_pixels(led.pixels, LED_COUNT);
                if (led.wait_pending) {
                    led.state = LED_STATE_WAITING_CENTER;
                    led.wait_start_us = now;
                } else {
                    led.state = LED_STATE_IDLE;
                    stop_timer();
                    all_off();
                }
                break;
            }
        }

        float fade = (led.play_round == 1) ? phase : (1.0f - phase);
        render_outer(led.play_color, phase, fade);
        ws2812_set_pixels(led.pixels, LED_COUNT);
        break;
    }

    case LED_STATE_WAITING_CENTER: {
        int64_t elapsed = now - led.wait_start_us;
        float phase = fmodf((float)elapsed / (float)get_period_us(), 1.0f);

        memset(&led.pixels[OUTER_INDEX], 0, OUTER_COUNT * sizeof(rgb_t));
        led_color_t center = render_center(led.wait_color, phase);
        apply_pixel(CENTER_INDEX, center, 1.0f);
        ws2812_set_pixels(led.pixels, LED_COUNT);
        break;
    }

    case LED_STATE_IDLE:
    default:
        stop_timer();
        all_off();
        break;
    }
}

// ---- NVS persistence ----

static void load_settings(void)
{
    int32_t val;

    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, "led_on", &val)) {
        led.enabled = (val != 0);
    } else {
        led.enabled = true;
    }

    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, "led_bright", &val) && val >= 1 && val <= 9) {
        led.brightness = (uint8_t)val;
    } else {
        led.brightness = 5;
    }

    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, "led_speed", &val) && val >= 0 && val <= 2) {
        led.speed = (led_speed_t)val;
    } else {
        led.speed = LED_SPEED_MEDIUM;
    }

    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, "led_style", &val) && val >= 0 && val <= 1) {
        led.style = (led_style_t)val;
    } else {
        led.style = LED_STYLE_PURE;
    }

    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, "led_anim", &val) && val >= 0 && val <= 2) {
        led.anim = (led_anim_t)val;
    } else {
        led.anim = LED_ANIM_BREATH;
    }
}

// ---- Public API ----

void led_service_init(void)
{
    memset(&led, 0, sizeof(led));
    load_settings();

    led.state = LED_STATE_IDLE;
    led.timer_running = false;

    const esp_timer_create_args_t timer_args = {
        .callback = frame_callback,
        .name = "led_frame",
    };
    esp_timer_create(&timer_args, &led.frame_timer);

    ESP_LOGI(TAG, "LED service initialized: on=%d bright=%d speed=%d style=%d anim=%d",
             led.enabled, led.brightness, led.speed, led.style, led.anim);
}

void led_service_play(led_color_t color)
{
    if (!led.enabled) return;

    // Ensure previous animation is fully stopped
    stop_timer();

    led.play_color = color;
    led.state = LED_STATE_PLAYING_OUTER;
    led.play_start_us = esp_timer_get_time();
    led.play_round = 1;
    led.wait_pending = false;

    // Start from clean state
    memset(&led.pixels[OUTER_INDEX], 0, OUTER_COUNT * sizeof(rgb_t));
    ws2812_set_pixels(led.pixels, LED_COUNT);

    start_timer();
}

void led_service_wait(led_color_t color)
{
    if (!led.enabled) return;

    led.wait_color = color;
    led.wait_pending = true;

    if (led.state == LED_STATE_IDLE) {
        led.state = LED_STATE_WAITING_CENTER;
        led.wait_start_us = esp_timer_get_time();
        start_timer();
    }
    // If PLAYING_OUTER, wait will start after outer ring completes (see frame_callback)
}

void led_service_stop(void)
{
    led.state = LED_STATE_IDLE;
    stop_timer();
    all_off();
}

// Settings
void led_service_set_enabled(bool on)
{
    led.enabled = on;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "led_on", on ? 1 : 0);
    if (!on) {
        led_service_stop();
    }
    ESP_LOGI(TAG, "LED %s", on ? "enabled" : "disabled");
}

bool led_service_is_enabled(void)
{
    return led.enabled;
}

void led_service_set_brightness(uint8_t level)
{
    if (level < 1) level = 1;
    if (level > 9) level = 9;
    led.brightness = level;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "led_bright", level);
}

uint8_t led_service_get_brightness(void)
{
    return led.brightness;
}

void led_service_set_animation(led_anim_t anim)
{
    led.anim = anim;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "led_anim", (int32_t)anim);
}

led_anim_t led_service_get_animation(void)
{
    return led.anim;
}

void led_service_set_style(led_style_t style)
{
    led.style = style;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "led_style", (int32_t)style);
}

led_style_t led_service_get_style(void)
{
    return led.style;
}

void led_service_set_speed(led_speed_t speed)
{
    led.speed = speed;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "led_speed", (int32_t)speed);
}

led_speed_t led_service_get_speed(void)
{
    return led.speed;
}

// Demo mode
void led_service_demo_start(led_color_t color)
{
    if (led.demo.active) return;

    // Save current state for restore
    led.demo.saved_state = led.state;
    if (led.state == LED_STATE_WAITING_CENTER) {
        led.demo.saved_wait_color = led.wait_color;
    }

    led.demo.active = true;
    led.demo.color = color;
    led.play_start_us = esp_timer_get_time();

    start_timer();
    ESP_LOGI(TAG, "Demo started");
}

void led_service_demo_change_color(led_color_t color)
{
    if (!led.demo.active) return;
    led.demo.color = color;
}

void led_service_demo_stop(void)
{
    if (!led.demo.active) return;

    led.demo.active = false;

    // Turn everything off first
    stop_timer();
    all_off();

    // Restore previous state
    led.state = led.demo.saved_state;
    if (led.state == LED_STATE_WAITING_CENTER) {
        led.wait_color = led.demo.saved_wait_color;
        led.wait_start_us = esp_timer_get_time();
        start_timer();
    } else if (led.state == LED_STATE_PLAYING_OUTER) {
        led.play_start_us = esp_timer_get_time();
        start_timer();
    }

    ESP_LOGI(TAG, "Demo stopped, restored state %d", led.state);
}

bool led_service_is_demo_active(void)
{
    return led.demo.active;
}
