#pragma once

#include "lvgl.h"

void st7789_lcd_init(void);
void st7789_lcd_reinit(void);
void st7789_lcd_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

/* Sleep / wakeup for deep-sleep entry/exit. SLPIN=0x10, SLPOUT=0x11. */
void st7789_lcd_sleep(void);
void st7789_lcd_wakeup(void);
