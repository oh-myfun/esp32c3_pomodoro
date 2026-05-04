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
#define FRAME_INTERVAL_US   50000   // 50ms = 20fps

// Speed periods in microseconds
#define SPEED_PERIOD_SLOW    3000000U
#define SPEED_PERIOD_MEDIUM  1500000U
#define SPEED_PERIOD_FAST     800000U

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

    // Frame timer
    esp_timer_handle_t frame_timer;
    bool timer_running;

    // Demo
    demo_state_t demo;

    // Rendered pixel buffer
    rgb_t pixels[LED_COUNT];
} led;

// Demo color presets
const led_color_t led_demo_colors[LED_DEMO_COLOR_COUNT] = {
    {255, 107, 107},   // Warm Red
    {76, 175, 80},     // Green
    {77, 150, 255},    // Blue
    {255, 255, 0},     // Yellow
    {180, 80, 255},    // Purple
};

const char *const led_demo_color_names[LED_DEMO_COLOR_COUNT] = {
    "Warm Red",
    "Green",
    "Blue",
    "Yellow",
    "Purple",
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
            // Brightness 0 <-> max sinusoidal
            float val = 0.5f * (1.0f + sinf(phase * 2.0f * (float)M_PI));
            hsv.v = val;
        } else {
            // Hue oscillates +/-60 degrees around base
            float hue_offset = 60.0f * sinf(phase * 2.0f * (float)M_PI);
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
            // Brightness wobbles +/-30% around max
            float val = 1.0f + 0.3f * sinf(phase * 2.0f * (float)M_PI);
            if (val > 1.0f) val = 1.0f;
            hsv.v = val;
        } else {
            // Hue wobbles +/-30 degrees around base
            float hue_offset = 30.0f * sinf(phase * 2.0f * (float)M_PI);
            hsv.h = fmodf(hsv.h + hue_offset + 360.0f, 360.0f);
        }
        break;
    }

    return hsv_to_rgb(hsv);
}

// Outer ring rendering
static void render_outer(led_color_t base, float phase)
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
                brightness_factor = 0.5f * (1.0f + sinf(led_phase * 2.0f * (float)M_PI));
                hsv.v = brightness_factor;
            } else {
                float hue_offset = 60.0f * sinf(led_phase * 2.0f * (float)M_PI);
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
                // Colorful: each LED has rotating hue
                float led_idx_phase = (float)i / (float)OUTER_COUNT;
                float hue_offset = (phase + led_idx_phase) * 360.0f;
                hsv.h = fmodf(base_hsv.h + hue_offset, 360.0f);
                if (dist >= 0 && dist < 1.0f) {
                    brightness_factor = 1.0f - dist * 0.5f;
                } else if (dist >= 1.0f) {
                    brightness_factor = powf(0.5f, dist);
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
                float val = 1.0f + 0.3f * sinf(led_phase * 2.0f * (float)M_PI);
                if (val > 1.0f) val = 1.0f;
                brightness_factor = val;
            } else {
                float hue_offset = 30.0f * sinf(led_phase * 2.0f * (float)M_PI);
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
        apply_pixel(OUTER_INDEX + i, pixel_color, brightness_factor);
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
        render_outer(led.demo.color, phase);

        ws2812_set_pixels(led.pixels, LED_COUNT);
        return;
    }

    switch (led.state) {
    case LED_STATE_PLAYING_OUTER: {
        int64_t elapsed = now - led.play_start_us;
        int64_t period = get_period_us();
        float phase = (float)elapsed / (float)period;

        if (phase >= 1.0f) {
            // One round complete -> move to WAITING_CENTER or IDLE
            memset(&led.pixels[OUTER_INDEX], 0, OUTER_COUNT * sizeof(rgb_t));
            ws2812_set_pixels(led.pixels, LED_COUNT);
            led.state = LED_STATE_WAITING_CENTER;
            led.wait_start_us = now;
        } else {
            render_outer(led.play_color, phase);
            ws2812_set_pixels(led.pixels, LED_COUNT);
        }
        break;
    }

    case LED_STATE_WAITING_CENTER: {
        int64_t elapsed = now - led.wait_start_us;
        float phase = fmodf((float)elapsed / (float)get_period_us(), 1.0f);

        led_color_t center = render_center(led.wait_color, phase);
        apply_pixel(CENTER_INDEX, center, 1.0f);
        // Outer ring stays off during wait
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

    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, "led_bright", &val) && val >= 1 && val <= 10) {
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

static void __attribute__((unused)) save_settings(void)
{
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "led_on", led.enabled ? 1 : 0);
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "led_bright", led.brightness);
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "led_speed", (int32_t)led.speed);
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "led_style", (int32_t)led.style);
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, "led_anim", (int32_t)led.anim);
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

    // If currently in WAITING_CENTER, save the wait color for potential restore
    if (led.state == LED_STATE_WAITING_CENTER) {
        led.wait_color = color;
    }

    led.play_color = color;
    led.state = LED_STATE_PLAYING_OUTER;
    led.play_start_us = esp_timer_get_time();

    // Ensure timer is running
    start_timer();
}

void led_service_wait(led_color_t color)
{
    if (!led.enabled) return;

    led.wait_color = color;
    led.state = LED_STATE_WAITING_CENTER;
    led.wait_start_us = esp_timer_get_time();

    start_timer();
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
    if (level > 10) level = 10;
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

    // Restore previous state
    led.state = led.demo.saved_state;
    if (led.state == LED_STATE_WAITING_CENTER) {
        led.wait_color = led.demo.saved_wait_color;
        led.wait_start_us = esp_timer_get_time();
        start_timer();
    } else if (led.state == LED_STATE_PLAYING_OUTER) {
        led.play_start_us = esp_timer_get_time();
        start_timer();
    } else {
        stop_timer();
        all_off();
    }

    ESP_LOGI(TAG, "Demo stopped, restored state %d", led.state);
}

bool led_service_is_demo_active(void)
{
    return led.demo.active;
}
