#pragma once

#include "lvgl.h"

typedef enum {
    TEXT_INPUT_IP,       /* digits + auto-dot + validate 0-255.0-255.0-255.0-255 */
    TEXT_INPUT_PORT,     /* digits only, validate 1-65535 */
    TEXT_INPUT_ALPHANUM, /* digits + uppercase A-Z (for Session/pairing codes) */
    TEXT_INPUT_TEXT,     /* full keyboard: digits + lower/uppercase + symbols */
} text_input_charset_t;

/* result != NULL: user confirmed with this string
   result == NULL: user cancelled (encoder press) */
typedef void (*text_input_result_cb)(const char *result);

void ui_text_input_configure(const char *title, const char *initial_value,
                              text_input_charset_t charset, int max_len,
                              text_input_result_cb on_result);
lv_obj_t *ui_text_input_create(void);
