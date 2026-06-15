#pragma once

#include "lvgl.h"

/* ===== 背景色 ===== */
#define UI_COLOR_BG          lv_color_hex(0x000000)  /* 统一屏幕背景 */
#define UI_COLOR_CARD_BG     lv_color_hex(0x0a0a0a)  /* 卡片/图表背景 */
#define UI_COLOR_ARC_BG      lv_color_hex(0x333333)  /* 弧形底色 */

/* ===== 文本色 ===== */
#define UI_COLOR_TEXT        lv_color_hex(0xFFFFFF)  /* 主文本 */
#define UI_COLOR_TEXT_DIM    lv_color_hex(0xAAAAAA)  /* 次要文本（日期等） */
#define UI_COLOR_TEXT_HINT   lv_color_hex(0x888888)  /* 底部提示 */
#define UI_COLOR_TEXT_SUB    lv_color_hex(0xCCCCCC)  /* 伙伴次级文本 */
#define UI_COLOR_TEXT_NAV    lv_color_hex(0x666666)  /* 导航提示 */
#define UI_COLOR_DISABLED    lv_color_hex(0x444444)  /* 断连/禁用 */
#define UI_COLOR_LIST_ALT    lv_color_hex(0x555555)  /* 列表次级 */

/* ===== 强调色 ===== */
#define UI_COLOR_ACCENT      lv_color_hex(0xFFFF00)  /* 调整模式 / 选中态 */
#define UI_COLOR_SUCCESS     lv_color_hex(0x00FF00)  /* 选中确认 / 工作完成 */
#define UI_COLOR_WARN        lv_color_hex(0xFFAA00)  /* 警告 */
#define UI_COLOR_DANGER      lv_color_hex(0xFF4400)  /* 错误 */

/* ===== 番茄钟阶段色 ===== */
#define UI_COLOR_POMO_WORK     lv_color_hex(0xFF4400)
#define UI_COLOR_POMO_BREAK    lv_color_hex(0x00FF00)
#define UI_COLOR_POMO_LONG     lv_color_hex(0x00AAFF)
#define UI_COLOR_POMO_IDLE     lv_color_hex(0xAAAAAA)
#define UI_COLOR_POMO_PAUSED   lv_color_hex(0x888888)

/* ===== 排版 ===== */
#define UI_TITLE_Y_OFFSET      6     /* 标题统一 Y 偏移 */
#define UI_RADIUS_CARD         4     /* 卡片圆角 */
#define UI_RADIUS_PILL         8     /* 胶囊圆角 */
#define UI_RADIUS_THIN         1     /* 进度条/分割线圆角 */
