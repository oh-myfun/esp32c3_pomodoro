/* main.c — PC LVGL 模拟器主程序
 * 离屏渲染到内存，按屏幕顺序生成 PNG 截图。 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define SIM_MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#include <sys/stat.h>
#define SIM_MKDIR(p) mkdir(p, 0777)
#endif

#include "lvgl.h"
#include "lv_conf.h"

/* mocks + fake services come from same compile unit */
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "ui/ui_manager.h"
#include "ui/i18n.h"
#include "ui/ui_screen_main.h"
#include "buddy/buddy.h"
#include "buddy/buddy_render.h"

#include "stb_image_write.h"

#define SCREEN_W 240
#define SCREEN_H 240
#define BG_COLOR_RGB888(c16) (((c16) >> 11 & 0x1F) << 3), \
                              (((c16) >> 5 & 0x3F) << 2), \
                              (((c16) & 0x1F) << 3)

static uint8_t s_framebuffer[SCREEN_W * SCREEN_H * 3];
static lv_color_t s_draw_buf[SCREEN_W * SCREEN_H];  /* full-frame buffer for FULL render mode */

static void sim_sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static int s_flush_count = 0;

static void my_flush_cb(lv_display_t *disp, const lv_area_t *area, const uint8_t *px_map) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    const uint16_t *px = (const uint16_t *)px_map;
    s_flush_count++;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t raw = px[y * w + x];
            int dx = area->x1 + x;
            int dy = area->y1 + y;
            if (dx < 0 || dx >= SCREEN_W || dy < 0 || dy >= SCREEN_H) continue;
            int idx = (dy * SCREEN_W + dx) * 3;
            s_framebuffer[idx]     = ((raw >> 11) & 0x1F) << 3;
            s_framebuffer[idx + 1] = ((raw >> 5) & 0x3F) << 2;
            s_framebuffer[idx + 2] = (raw & 0x1F) << 3;
        }
    }
    lv_display_flush_ready(disp);
}

/* Simulated esp_timer + tick for LVGL. */
static int64_t s_sim_time_us = 0;

int64_t esp_timer_get_time(void) { return s_sim_time_us; }

typedef struct esp_timer {
    esp_timer_cb_t cb;
    void *arg;
    int64_t period_us;       /* 0 = one-shot */
    int64_t fire_at_us;
    int active;
} esp_timer;

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
    esp_timer *t = (esp_timer *)calloc(1, sizeof(esp_timer));
    t->cb = args->callback;
    t->arg = args->arg;
    *out = t;
    return ESP_OK;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t period) {
    t->period_us = period;
    t->fire_at_us = s_sim_time_us + period;
    t->active = 1;
    return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t timeout) {
    t->period_us = 0;
    t->fire_at_us = s_sim_time_us + timeout;
    t->active = 1;
    return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t t) { t->active = 0; return ESP_OK; }
esp_err_t esp_timer_delete(esp_timer_handle_t t) { free(t); return ESP_OK; }

void esp_timer_impl_advance(int64_t us) {
    s_sim_time_us += us;
    /* walk all created timers — but we don't track them globally here.
     * LVGL's lv_tick_inc is driven from the increase_lvgl_tick callback. */
}

/* FreeRTOS stubs. */
void vTaskDelay(TickType_t ticks) { (void)ticks; }
TickType_t xTaskGetTickCount(void) { return (TickType_t)(s_sim_time_us / 1000); }
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                       uint32_t prio, TaskHandle_t *out) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio;
    if (out) *out = NULL;
    return pdTRUE;
}
void vTaskDelete(TaskHandle_t t) { (void)t; }
void vTaskStartScheduler(void) {}
uint32_t uxTaskGetNumberOfTasks(void) { return 4; }

QueueHandle_t xQueueCreate(uint32_t len, uint32_t sz) { (void)len; (void)sz; return NULL; }
BaseType_t xQueueSend(QueueHandle_t q, const void *p, TickType_t to) { (void)q; (void)p; (void)to; return pdTRUE; }
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *p, BaseType_t *hi) {
    (void)q; (void)p; if (hi) *hi = pdFALSE; return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *p, TickType_t to) { (void)q; (void)p; (void)to; return pdFALSE; }
void vQueueDelete(QueueHandle_t q) { (void)q; }

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return (SemaphoreHandle_t)1; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (SemaphoreHandle_t)1; }
SemaphoreHandle_t xSemaphoreCreateCounting(uint32_t max, uint32_t init) {
    (void)max; (void)init; return (SemaphoreHandle_t)1;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t to) { (void)s; (void)to; return pdTRUE; }
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t to) { (void)s; (void)to; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t s) { (void)s; }

/* backlight stub (driver/backlight.h) */
void backlight_init(void) {}
void backlight_set_brightness(uint8_t p) { (void)p; }
uint8_t backlight_get_brightness(void) { return 10; }

/* advance LVGL time + dispatch esp_timers within window */
static void sim_advance_ms(int ms) {
    int64_t end = s_sim_time_us + (int64_t)ms * 1000;
    while (s_sim_time_us < end) {
        s_sim_time_us += 1000;
        lv_tick_inc(1);
        lv_timer_handler();
    }
}

static void reset_framebuffer(void) {
    /* pure black */
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

static void capture(const char *fname, ui_screen_id_t sid, int settle_ms) {
    printf("Capturing %s (screen=%d, settle=%dms)...\n", fname, sid, settle_ms);
    s_flush_count = 0;
    reset_framebuffer();
    ui_switch_screen(sid);
    sim_advance_ms(settle_ms);
    /* force an extra redraw pass to ensure framebuffer fully populated */
    lv_obj_invalidate(lv_screen_active());
    sim_advance_ms(120);
    printf("  flush called %d times\n", s_flush_count);
    if (stbi_write_png(fname, SCREEN_W, SCREEN_H, 3, s_framebuffer, SCREEN_W * 3)) {
        printf("  -> %s\n", fname);
    } else {
        fprintf(stderr, "  failed to write %s\n", fname);
    }
}

/* Capture N frames of the buddy screen in CELEBRATE state for GIF assembly.
 * Each frame advances the animation by ~250ms (same as device's service_task). */
static void capture_buddy_gif(const char *dir, int frames) {
    printf("Capturing buddy CELEBRATE gif (%d frames)...\n", frames);
    sim_set_buddy_state(BUDDY_CELEBRATE);
    ui_switch_screen(UI_SCREEN_BUDDY);
    sim_advance_ms(400);
    char path[256];
    for (int i = 0; i < frames; i++) {
        /* advance animation: buddy_tick normally fires every 200ms */
        buddy_tick();
        buddy_tick();
        buddy_tick();
        buddy_tick();
        buddy_tick();  /* ~1s of device time per frame */

        reset_framebuffer();
        lv_obj_invalidate(lv_screen_active());
        sim_advance_ms(80);

        snprintf(path, sizeof(path), "%s/gif_%03d.png", dir, i);
        if (!stbi_write_png(path, SCREEN_W, SCREEN_H, 3, s_framebuffer, SCREEN_W * 3)) {
            fprintf(stderr, "  failed to write %s\n", path);
        }
    }
    printf("  -> %s/gif_*.png (%d frames)\n", dir, frames);
    sim_set_buddy_state(BUDDY_IDLE);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    lv_init();

    lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_buffers(disp, s_draw_buf, NULL, sizeof(s_draw_buf),
                           LV_DISPLAY_RENDER_MODE_FULL);

    /* Set black background for active screen before any UI creation */
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    /* Init i18n (loads language) and UI manager (creates all screens) */
    i18n_init();
    ui_init();

    /* Allow initial animation to settle */
    sim_advance_ms(200);

    /* Populate runtime data on main screen (time + WiFi status) so the
     * capture isn't just empty labels. */
    ui_screen_main_update_wifi_status(i18n(STR_WIFI_CONNECTED), 0x00FF00);
    ui_screen_main_update_time();

    const char *out_dir = "tools/simulator/output";
    SIM_MKDIR(out_dir);

    char path[256];

    snprintf(path, sizeof(path), "%s/01-main.png", out_dir);
    capture(path, UI_SCREEN_MAIN, 300);

    snprintf(path, sizeof(path), "%s/02-pomodoro.png", out_dir);
    capture(path, UI_SCREEN_POMODORO, 300);

    snprintf(path, sizeof(path), "%s/03-buddy.png", out_dir);
    capture(path, UI_SCREEN_BUDDY, 400);

    snprintf(path, sizeof(path), "%s/04-sensor.png", out_dir);
    capture(path, UI_SCREEN_SENSOR, 300);

    snprintf(path, sizeof(path), "%s/05-settings.png", out_dir);
    capture(path, UI_SCREEN_SETTINGS, 300);

    snprintf(path, sizeof(path), "%s/06-settings-system.png", out_dir);
    capture(path, UI_SCREEN_SETTINGS_SYSTEM, 300);

    /* buddy CELEBRATE animation frames for GIF */
    snprintf(path, sizeof(path), "%s/gif_frames", out_dir);
    SIM_MKDIR(path);
    capture_buddy_gif(path, 16);

    printf("Done.\n");
    return 0;
}
