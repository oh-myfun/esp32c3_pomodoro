#pragma once

#include "lvgl.h"
#include <stdint.h>

/* Buddy states */
typedef enum {
    BUDDY_SLEEP = 0,
    BUDDY_IDLE,
    BUDDY_BUSY,
    BUDDY_ATTENTION,
    BUDDY_CELEBRATE,
    BUDDY_DIZZY,
    BUDDY_HEART,
} buddy_state_t;

/* Geometry constants (same as original buddy_common.h) */
#define BUDDY_X_CENTER  67
#define BUDDY_CANVAS_W  135
#define BUDDY_Y_BASE    30
#define BUDDY_Y_OVERLAY  6
#define BUDDY_CHAR_W     6
#define BUDDY_CHAR_H     8

/* Color constants (RGB565, same as original) */
#define BUDDY_CLR_BG     0x0000
#define BUDDY_CLR_HEART  0xF810
#define BUDDY_CLR_DIM    0x8410
#define BUDDY_CLR_YEL    0xFFE0
#define BUDDY_CLR_WHITE  0xFFFF
#define BUDDY_CLR_CYAN   0x07FF
#define BUDDY_CLR_GREEN  0x07E0
#define BUDDY_CLR_PURPLE 0xA01F
#define BUDDY_CLR_RED    0xF800
#define BUDDY_CLR_BLUE   0x041F

/* Species state function: takes tick counter, renders to current layer */
typedef void (*buddy_state_fn_t)(uint32_t t);

typedef struct {
    const char *name;
    uint16_t bodyColor;
    buddy_state_fn_t states[7]; /* sleep, idle, busy, attention, celebrate, dizzy, heart */
} buddy_species_desc_t;

/* Rendering API (same signatures as original) */
void buddyPrintLine(const char *line, int yPx, uint16_t color, int xOff);
void buddyPrintSpriteImpl(const char *const *lines, uint8_t nLines,
                          int yOffset, uint16_t color, int xOff);
void buddySetCursor(int x, int y);
void buddySetColor(uint16_t fg);
void buddyPrint(const char *s);

/* C doesn't have default args — macro overloading handles both 4 and 5 arg calls */
#define _bp_nargs(_1,_2,_3,_4,_5,N,...) N
#define _bp_cat_(a,b) a##b
#define _bp_cat(a,b) _bp_cat_(a,b)
#define _bp4(l,n,y,c)  buddyPrintSpriteImpl(l,n,y,c,0)
#define _bp5(l,n,y,c,x) buddyPrintSpriteImpl(l,n,y,c,x)
#define buddyPrintSprite(...) _bp_cat(_bp, _bp_nargs(__VA_ARGS__,5,4,0)(__VA_ARGS__))

/* Setup — call once before rendering a frame */
void buddy_render_begin(lv_layer_t *layer);
void buddy_render_begin_small(lv_layer_t *layer, int cx, int cy, int cw, int ch);

/* Species registry */
extern const buddy_species_desc_t *buddy_species_table[];
extern const int buddy_species_count;
