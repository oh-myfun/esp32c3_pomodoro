#include "buddy_render.h"
#include "lvgl.h"
#include "ui/custom_font.h"
#include <string.h>

/* ---- Configurable rendering state ---- */
static lv_layer_t *s_layer     = NULL;
static int         s_cur_x     = 0;
static int         s_cur_y     = 0;
static uint16_t    s_fg565     = 0xFFFF;

static int              s_scale    = 2;    /* 2=full (normal), 1=half (attention) */
static int              s_char_adv = 12;   /* effective char spacing: adv_w + letter_space */
static int              s_center_x = 120;  /* horizontal center in screen px */
static int              s_y_off    = 14;   /* vertical offset in screen px */
static const lv_font_t *s_font     = NULL;
static int              s_letter_space = 0;

/* ---- Color conversion ---- */

static lv_color_t rgb565_to_lv(uint16_t c)
{
    uint8_t r5 = (c >> 11) & 0x1F;
    uint8_t g6 = (c >> 5)  & 0x3F;
    uint8_t b5 =  c        & 0x1F;
    uint32_t rgb888 = ((uint32_t)((r5 << 3) | (r5 >> 2)) << 16)
                    | ((uint32_t)((g6 << 2) | (g6 >> 4)) << 8)
                    | ((b5 << 3) | (b5 >> 2));
    return lv_color_hex(rgb888);
}

/* ---- Internal draw ---- */

static void draw_str(int x, int y, const char *text, uint16_t fg)
{
    if (!s_layer || !text || !text[0]) return;
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = s_font;
    dsc.color = rgb565_to_lv(fg);
    dsc.letter_space = s_letter_space;
    lv_area_t coords = { x, y, x + 240, y + 20 };
    lv_draw_label(s_layer, &dsc, &coords);
}

/* ---- Setup (called before each frame) ---- */

void buddy_render_begin(lv_layer_t *layer)
{
    s_layer        = layer;
    s_fg565        = BUDDY_CLR_WHITE;
    s_scale        = 2;
    s_char_adv     = 12;   /* custom_font_16 adv_w(10) + letter_space(2) */
    s_center_x     = 120;
    s_y_off        = 14;
    s_font         = &custom_font_16;
    s_letter_space = 2;

    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.color = lv_color_hex(0x1a1a1a);
    lv_area_t area = { 0, 30, 239, 160 };
    lv_draw_fill(s_layer, &fill_dsc, &area);
}

void buddy_render_begin_small(lv_layer_t *layer, int cx, int cy, int cw, int ch)
{
    s_layer        = layer;
    s_fg565        = BUDDY_CLR_WHITE;
    s_scale        = 1;
    s_char_adv     = 8;    /* custom_font_14 adv_w(8), no extra spacing */
    s_center_x     = cx;
    s_y_off        = cy + 2 - BUDDY_Y_BASE;
    s_font         = &custom_font_14;
    s_letter_space = 0;

    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.color = lv_color_hex(0x1a1a1a);
    lv_area_t area = { cx - cw / 2, cy, cx + cw / 2 - 1, cy + ch - 1 };
    lv_draw_fill(s_layer, &fill_dsc, &area);
}

/* ---- Public rendering API ---- */

void buddyPrintLine(const char *line, int yPx, uint16_t color, int xOff)
{
    int len = (int)strlen(line);
    while (len && line[len - 1] == ' ') len--;
    while (len && *line == ' ') { line++; len--; }
    if (len <= 0) return;

    int w = len * s_char_adv;
    int xoff_px = xOff * s_char_adv / BUDDY_CHAR_W;
    int x = s_center_x - w / 2 + xoff_px;
    draw_str(x, yPx + s_y_off, line, color);
}

void buddyPrintSpriteImpl(const char *const *lines, uint8_t nLines,
                           int yOffset, uint16_t color, int xOff)
{
    int yBase = BUDDY_Y_BASE * s_scale - (s_scale - 1) * 14;
    for (uint8_t i = 0; i < nLines; i++) {
        buddyPrintLine(lines[i],
                       yBase + (yOffset + (int)i * BUDDY_CHAR_H) * s_scale,
                       color, xOff);
    }
}

void buddySetCursor(int x, int y)
{
    int dx = x - BUDDY_X_CENTER;
    s_cur_x = s_center_x + dx * s_char_adv / BUDDY_CHAR_W;
    s_cur_y = y * s_scale + s_y_off;
}

void buddySetColor(uint16_t fg)
{
    s_fg565 = fg;
}

void buddyPrint(const char *s)
{
    draw_str(s_cur_x, s_cur_y, s, s_fg565);
}

/* ---- Species registry ---- */
extern const buddy_species_desc_t CAPYBARA_SPECIES;
extern const buddy_species_desc_t DUCK_SPECIES;
extern const buddy_species_desc_t GOOSE_SPECIES;
extern const buddy_species_desc_t BLOB_SPECIES;
extern const buddy_species_desc_t CAT_SPECIES;
extern const buddy_species_desc_t DRAGON_SPECIES;
extern const buddy_species_desc_t OCTOPUS_SPECIES;
extern const buddy_species_desc_t OWL_SPECIES;
extern const buddy_species_desc_t PENGUIN_SPECIES;
extern const buddy_species_desc_t TURTLE_SPECIES;
extern const buddy_species_desc_t SNAIL_SPECIES;
extern const buddy_species_desc_t GHOST_SPECIES;
extern const buddy_species_desc_t AXOLOTL_SPECIES;
extern const buddy_species_desc_t CACTUS_SPECIES;
extern const buddy_species_desc_t ROBOT_SPECIES;
extern const buddy_species_desc_t RABBIT_SPECIES;
extern const buddy_species_desc_t MUSHROOM_SPECIES;
extern const buddy_species_desc_t CHONK_SPECIES;

const buddy_species_desc_t *buddy_species_table[] = {
    &CAPYBARA_SPECIES, &DUCK_SPECIES, &GOOSE_SPECIES, &BLOB_SPECIES,
    &CAT_SPECIES, &DRAGON_SPECIES, &OCTOPUS_SPECIES, &OWL_SPECIES,
    &PENGUIN_SPECIES, &TURTLE_SPECIES, &SNAIL_SPECIES, &GHOST_SPECIES,
    &AXOLOTL_SPECIES, &CACTUS_SPECIES, &ROBOT_SPECIES, &RABBIT_SPECIES,
    &MUSHROOM_SPECIES, &CHONK_SPECIES,
};
const int buddy_species_count = sizeof(buddy_species_table) / sizeof(buddy_species_table[0]);
