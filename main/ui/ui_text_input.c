#include "ui_text_input.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "UI_TEXT_INPUT";

/* ---- Keyboard layout data ---- */

/* TEXT charset: 5 rows x 10 cols */
static const char text_keys[5][10] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm.-@",
    "!#$%&*_+"
};

/* ALPHANUM charset: 4 rows x 10 cols */
static const char alphanum_keys[4][10] = {
    "1234567890",
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ"
};

/* ---- Static state ---- */

static lv_obj_t *screen = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *subtitle_label = NULL;
static lv_obj_t *display_label = NULL;
static lv_obj_t *hint_label = NULL;
static lv_obj_t *keys[5][10];

static text_input_charset_t s_charset;
static int s_max_len;
static text_input_result_cb s_on_result;

static char s_buffer[64];
static int s_buf_len;
static bool s_uppercase;
static int s_sel_row, s_sel_col;
static int s_num_rows;

/* ---- Forward declarations ---- */

static void update_display(void);
static bool validate_ip(const char *ip);
static bool is_key_hidden(int row, int col);
static int get_last_visible_col(int row);
static void do_cancel(void);
static void do_confirm(void);

/* ---- Key visibility ---- */

static bool is_key_hidden(int row, int col)
{
    switch (s_charset) {
        case TEXT_INPUT_TEXT:
            return false;
        case TEXT_INPUT_ALPHANUM:
            if (row >= 4) return true;
            if (row == 3 && col >= 6 && col <= 7) return true;
            return false;
        case TEXT_INPUT_IP:
            if (row >= 2) return true;
            if (row == 1 && col >= 1 && col < 8) return true;
            return false;
        case TEXT_INPUT_PORT:
            if (row >= 2) return true;
            if (row == 1 && col < 8) return true;
            return false;
        default:
            return false;
    }
}

static int get_last_visible_col(int row)
{
    switch (s_charset) {
        case TEXT_INPUT_TEXT:
            return 9;
        case TEXT_INPUT_ALPHANUM:
            if (row <= 2) return 9;
            if (row == 3) return 9;
            return -1;
        case TEXT_INPUT_IP:
        case TEXT_INPUT_PORT:
            if (row == 0) return 9;
            if (row == 1) return 9;
            return -1;
        default:
            return 9;
    }
}

/* ---- Validation ---- */

static bool validate_ip(const char *ip)
{
    if (!ip || !ip[0]) return false;

    int count = 0;
    char buf[64];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    strncpy(buf, ip, sizeof(buf) - 1);
#pragma GCC diagnostic pop
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, ".");
    while (tok && count < 4) {
        /* Check for empty token (consecutive dots or leading/trailing dot) */
        if (tok[0] == '\0') return false;
        /* Check all chars are digits */
        for (int i = 0; tok[i]; i++) {
            if (tok[i] < '0' || tok[i] > '9') return false;
        }
        int val = atoi(tok);
        if (val < 0 || val > 255) return false;
        /* Reject leading zeros on multi-digit octets (e.g. "01") */
        if (tok[0] == '0' && tok[1] != '\0') return false;
        count++;
        tok = strtok(NULL, ".");
    }
    return count == 4;
}

static bool validate_port(const char *port)
{
    if (!port || !port[0]) return false;
    for (int i = 0; port[i]; i++) {
        if (port[i] < '0' || port[i] > '9') return false;
    }
    int val = atoi(port);
    return val >= 1 && val <= 65535;
}

/* ---- Cancel / Confirm ---- */

static void do_cancel(void)
{
    if (s_on_result) {
        s_on_result(NULL);
    }
    ui_go_back();
}

static void do_confirm(void)
{
    /* Validate based on charset */
    switch (s_charset) {
        case TEXT_INPUT_IP:
            if (!validate_ip(s_buffer)) return;
            break;
        case TEXT_INPUT_PORT:
            if (!validate_port(s_buffer)) return;
            break;
        case TEXT_INPUT_ALPHANUM:
        case TEXT_INPUT_TEXT:
            if (s_buf_len == 0) return;
            break;
        default:
            if (s_buf_len == 0) return;
            break;
    }

    if (s_on_result) {
        s_on_result(s_buffer);
    }
    ui_go_back();
}

/* ---- Display update ---- */

static void set_key_label(int row, int col)
{
    lv_obj_t *lbl = keys[row][col];
    if (!lbl) return;

    if (is_key_hidden(row, col)) {
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_HIDDEN);

    /* Determine label text */
    switch (s_charset) {
        case TEXT_INPUT_TEXT: {
            if (row == 2 && col == 9) {
                lv_label_set_text(lbl, s_uppercase ? "⇧*" : "⇧");
            } else if (row == 4 && col == 8) {
                lv_label_set_text(lbl, "⇦");
            } else if (row == 4 && col == 9) {
                lv_label_set_text(lbl, i18n(STR_OK));
            } else {
                char c = text_keys[row][col];
                if (s_uppercase && c >= 'a' && c <= 'z') {
                    char ch[2] = { (char)(c - 'a' + 'A'), '\0' };
                    lv_label_set_text(lbl, ch);
                } else {
                    char ch[2] = { c, '\0' };
                    lv_label_set_text(lbl, ch);
                }
            }
            break;
        }
        case TEXT_INPUT_ALPHANUM: {
            if (row == 3 && col == 8) {
                lv_label_set_text(lbl, "⇦");
            } else if (row == 3 && col == 9) {
                lv_label_set_text(lbl, i18n(STR_OK));
            } else {
                char ch[2] = { alphanum_keys[row][col], '\0' };
                lv_label_set_text(lbl, ch);
            }
            break;
        }
        case TEXT_INPUT_IP: {
            if (row == 1 && col == 0) {
                lv_label_set_text(lbl, ".");
            } else if (row == 1 && col == 8) {
                lv_label_set_text(lbl, "⇦");
            } else if (row == 1 && col == 9) {
                lv_label_set_text(lbl, i18n(STR_OK));
            } else if (row == 0) {
                char ch[2] = { text_keys[0][col], '\0' };
                lv_label_set_text(lbl, ch);
            }
            break;
        }
        case TEXT_INPUT_PORT: {
            if (row == 1 && col == 8) {
                lv_label_set_text(lbl, "⇦");
            } else if (row == 1 && col == 9) {
                lv_label_set_text(lbl, i18n(STR_OK));
            } else if (row == 0) {
                char ch[2] = { text_keys[0][col], '\0' };
                lv_label_set_text(lbl, ch);
            }
            break;
        }
    }
}

static void update_display(void)
{
    /* Display buffer with cursor */
    if (display_label) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        char disp[68];
        snprintf(disp, sizeof(disp), "%s_", s_buffer);
#pragma GCC diagnostic pop
        lv_label_set_text(display_label, disp);
    }

    /* Update subtitle with initial/current value */
    /* (subtitle is set in configure, no need to update here) */

    /* Update all key labels and highlight */
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 10; col++) {
            if (!keys[row][col]) continue;

            /* Update text */
            set_key_label(row, col);

            /* Color: selected = green, else gray */
            if (row == s_sel_row && col == s_sel_col && !is_key_hidden(row, col)) {
                lv_obj_set_style_text_color(keys[row][col], lv_color_hex(0x00FF00), 0);
            } else {
                lv_obj_set_style_text_color(keys[row][col], lv_color_hex(0x888888), 0);
            }
        }
    }

    /* Update hint */
    if (hint_label) {
        lv_label_set_text(hint_label, i18n(STR_H_SET_INPUT_PRESS_BACK));
    }
}

/* ---- Input callbacks ---- */

static void text_input_on_encoder_cw(void)
{
    /* Move right, wrap to next row, skip hidden keys */
    int col = s_sel_col;
    int row = s_sel_row;

    for (int attempt = 0; attempt < 60; attempt++) {
        col++;
        if (col > get_last_visible_col(row)) {
            row++;
            col = 0;
            if (row >= s_num_rows) {
                row = 0;
            }
        }
        if (!is_key_hidden(row, col)) break;
    }

    s_sel_row = row;
    s_sel_col = col;
    update_display();
}

static void text_input_on_encoder_ccw(void)
{
    /* Move left, wrap to previous row, skip hidden keys */
    int col = s_sel_col;
    int row = s_sel_row;

    for (int attempt = 0; attempt < 60; attempt++) {
        col--;
        if (col < 0) {
            row--;
            if (row < 0) {
                row = s_num_rows - 1;
            }
            col = get_last_visible_col(row);
        }
        if (!is_key_hidden(row, col)) break;
    }

    s_sel_row = row;
    s_sel_col = col;
    update_display();
}

static void text_input_on_encoder_press(void)
{
    do_cancel();
}

static void text_input_on_encoder_long_press(void)
{
    if (s_buf_len > 0) {
        s_buffer[--s_buf_len] = '\0';
        update_display();
    } else {
        do_cancel();
    }
}

static void text_input_on_settings_press(void)
{
    int row = s_sel_row;
    int col = s_sel_col;

    /* Check for special keys based on charset */
    switch (s_charset) {
        case TEXT_INPUT_TEXT: {
            /* Shift */
            if (row == 2 && col == 9) {
                s_uppercase = !s_uppercase;
                update_display();
                return;
            }
            /* Backspace */
            if (row == 4 && col == 8) {
                if (s_buf_len > 0) {
                    s_buffer[--s_buf_len] = '\0';
                    update_display();
                } else {
                    do_cancel();
                }
                return;
            }
            /* OK */
            if (row == 4 && col == 9) {
                do_confirm();
                return;
            }
            break;
        }
        case TEXT_INPUT_ALPHANUM: {
            /* Backspace */
            if (row == 3 && col == 8) {
                if (s_buf_len > 0) {
                    s_buffer[--s_buf_len] = '\0';
                    update_display();
                } else {
                    do_cancel();
                }
                return;
            }
            /* OK */
            if (row == 3 && col == 9) {
                do_confirm();
                return;
            }
            break;
        }
        case TEXT_INPUT_IP: {
            /* Dot key */
            if (row == 1 && col == 0) {
                if (s_buf_len > 0 && s_buf_len < s_max_len && s_buffer[s_buf_len - 1] != '.') {
                    s_buffer[s_buf_len++] = '.';
                    s_buffer[s_buf_len] = '\0';
                    update_display();
                }
                return;
            }
            /* Backspace */
            if (row == 1 && col == 8) {
                if (s_buf_len > 0) {
                    s_buffer[--s_buf_len] = '\0';
                    update_display();
                } else {
                    do_cancel();
                }
                return;
            }
            /* OK */
            if (row == 1 && col == 9) {
                do_confirm();
                return;
            }
            break;
        }
        case TEXT_INPUT_PORT: {
            /* Backspace */
            if (row == 1 && col == 8) {
                if (s_buf_len > 0) {
                    s_buffer[--s_buf_len] = '\0';
                    update_display();
                } else {
                    do_cancel();
                }
                return;
            }
            /* OK */
            if (row == 1 && col == 9) {
                do_confirm();
                return;
            }
            break;
        }
    }

    /* Regular character insertion */
    if (s_buf_len >= s_max_len || s_buf_len >= (int)sizeof(s_buffer) - 1) return;

    char c = '\0';
    switch (s_charset) {
        case TEXT_INPUT_TEXT:
            c = text_keys[row][col];
            if (s_uppercase && c >= 'a' && c <= 'z') {
                c = (char)(c - 'a' + 'A');
            }
            break;
        case TEXT_INPUT_ALPHANUM:
            if (row < 4 && col < 10) {
                c = alphanum_keys[row][col];
            }
            break;
        case TEXT_INPUT_IP:
            c = text_keys[0][col]; /* digits */
            break;
        case TEXT_INPUT_PORT:
            c = text_keys[0][col]; /* digits */
            break;
    }

    if (c == '\0') return;

    /* Insert the character */
    s_buffer[s_buf_len++] = c;
    s_buffer[s_buf_len] = '\0';

    /* IP auto-dot logic */
    if (s_charset == TEXT_INPUT_IP && c >= '0' && c <= '9') {
        /* Count consecutive digits at end of buffer (after last dot) */
        int digits_since_dot = 0;
        for (int i = s_buf_len - 1; i >= 0; i--) {
            if (s_buffer[i] == '.') break;
            digits_since_dot++;
        }
        /* Auto-insert dot after 3 digits in current octet */
        if (digits_since_dot == 3 && s_buf_len < 15) {
            s_buffer[s_buf_len++] = '.';
            s_buffer[s_buf_len] = '\0';
        }
    }

    update_display();
}

/* ---- Public API ---- */

lv_obj_t *ui_text_input_create(void)
{
    if (screen) return screen;

    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    title_label = lv_label_create(screen);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title_label, "");
    lv_obj_set_style_text_font(title_label, &custom_font_16, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 5);

    subtitle_label = lv_label_create(screen);
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(subtitle_label, "");
    lv_obj_set_style_text_font(subtitle_label, &custom_font_16, 0);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_MID, 0, 28);

    display_label = lv_label_create(screen);
    lv_obj_set_style_text_color(display_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(display_label, "_");
    lv_obj_set_style_text_font(display_label, &custom_font_16, 0);
    lv_obj_align(display_label, LV_ALIGN_TOP_MID, 0, 50);

    /* Create all 50 key labels (5x10 grid) */
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 10; col++) {
            keys[row][col] = lv_label_create(screen);
            lv_obj_set_style_text_font(keys[row][col], &custom_font_16, 0);
            lv_obj_set_style_text_color(keys[row][col], lv_color_hex(0x888888), 0);
            lv_label_set_text(keys[row][col], "");
            int x = 10 + col * 22;
            int y = 72 + row * 18;
            lv_obj_set_pos(keys[row][col], x, y);
            lv_obj_add_flag(keys[row][col], LV_OBJ_FLAG_HIDDEN);
        }
    }

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, "");
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* Register input callbacks */
    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = text_input_on_encoder_cw,
        .on_encoder_ccw = text_input_on_encoder_ccw,
        .on_encoder_press = text_input_on_encoder_press,
        .on_encoder_long_press = text_input_on_encoder_long_press,
        .on_settings_press = text_input_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_TEXT_INPUT, &cbs);

    ESP_LOGI(TAG, "Text input screen created");
    return screen;
}

void ui_text_input_configure(const char *title, const char *initial_value,
                              text_input_charset_t charset, int max_len,
                              text_input_result_cb on_result)
{
    s_charset = charset;
    s_max_len = max_len > 63 ? 63 : max_len;
    s_on_result = on_result;

    /* Clear and optionally pre-fill buffer */
    memset(s_buffer, 0, sizeof(s_buffer));
    s_buf_len = 0;
    if (initial_value && initial_value[0]) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        strncpy(s_buffer, initial_value, (size_t)s_max_len);
#pragma GCC diagnostic pop
        s_buf_len = (int)strlen(s_buffer);
    }

    s_uppercase = false;
    s_sel_row = 0;
    s_sel_col = 0;

    /* Determine number of visible rows */
    switch (charset) {
        case TEXT_INPUT_TEXT:    s_num_rows = 5; break;
        case TEXT_INPUT_ALPHANUM: s_num_rows = 4; break;
        case TEXT_INPUT_IP:      s_num_rows = 2; break;
        case TEXT_INPUT_PORT:    s_num_rows = 2; break;
        default:                 s_num_rows = 5; break;
    }

    /* Update labels */
    if (title_label) {
        lv_label_set_text(title_label, title ? title : "");
    }

    if (subtitle_label) {
        if (initial_value && initial_value[0]) {
            char sub[64];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            snprintf(sub, sizeof(sub), "%s", initial_value);
#pragma GCC diagnostic pop
            lv_label_set_text(subtitle_label, sub);
        } else {
            lv_label_set_text(subtitle_label, "");
        }
    }

    update_display();
}
