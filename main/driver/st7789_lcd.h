#ifndef ST7789_LCD_H
#define ST7789_LCD_H

#include "lvgl.h"

void st7789_lcd_init(void);
void st7789_lcd_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

#endif
