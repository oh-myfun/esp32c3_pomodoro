#pragma once

#include <stdint.h>

typedef enum { LANG_EN = 0, LANG_ZH = 1 } lang_t;

void i18n_init(void);
lang_t i18n_get_lang(void);
void i18n_set_lang(lang_t lang);

typedef enum {
    /* Page titles */
    STR_T_SETTINGS,
    STR_T_POMODORO,
    STR_T_BUDDY,
    STR_T_WIFI,
    STR_T_PASSWORD,
    STR_T_SYSTEM,
    STR_T_TIME,
    STR_T_LIGHT,
    STR_T_DEBUG,

    /* Settings menu items */
    STR_M_POMODORO,
    STR_M_BUDDY,
    STR_M_LIGHT,
    STR_M_WIFI,
    STR_M_TIME,
    STR_M_SYSTEM,
    STR_M_DEBUG,

    /* System settings */
    STR_SOUND,
    STR_DIRECTION,
    STR_LANGUAGE,
    STR_ON,
    STR_OFF,
    STR_NORMAL,
    STR_REV,
    STR_LANG_EN,
    STR_LANG_ZH,

    /* Pomodoro settings */
    STR_WORK,
    STR_BREAK,
    STR_LONG_BREAK,
    STR_CYCLES,
    STR_MODE,
    STR_MANUAL,
    STR_AUTO,
    STR_DEFAULT,
    STR_RESET,
    STR_FMT_MIN,       /* "%d min" / "%d分钟" */
    STR_FMT_DONE,      /* "%lu done" / "%lu次" */

    /* Pomodoro phases */
    STR_PHASE_WORK,
    STR_PHASE_BREAK,
    STR_PHASE_LONG_BREAK,
    STR_PHASE_PAUSED,
    STR_PHASE_IDLE,

    /* Light settings */
    STR_LIGHT,
    STR_BRIGHT,
    STR_SPEED,
    STR_STYLE,
    STR_ANIM,
    STR_DEMO,
    STR_SLOW,
    STR_MED,
    STR_FAST,
    STR_PURE,
    STR_COLOR,
    STR_BREATH,
    STR_SCAN,
    STR_GRADIENT,

    /* Time settings */
    STR_TIMEZONE,
    STR_NTP_SERVER,
    STR_NTP_INTERVAL,
    STR_OFF_VAL,

    /* Buddy settings */
    STR_SPECIES,

    /* Buddy page */
    STR_BUDDY_NAME,
    STR_PERMISSION,
    STR_APPROVE,
    STR_DENY,
    STR_NEXT_PET,
    STR_TOOL,
    STR_FMT_STATE,     /* "State: %s" / "状态: %s" */
    STR_FMT_TOOL,      /* "Tool: %s" / "工具: %s" */
    STR_PET_LABEL,     /* "Pet: " / "宠物: " */
    STR_OWNER_LABEL,   /* "Owner: " / "主人: " */
    STR_APPROVED_LABEL,/* "Approved: " / "已批准: " */
    STR_DENIED_LABEL,  /* "Denied: " / "已拒绝: " */
    STR_BLE_CONN,      /* "BLE: Connected" / "BLE: 已连接" */
    STR_BLE_DISCONN,   /* "BLE: Disconnected" / "BLE: 未连接" */

    /* Buddy states */
    STR_STATE_SLEEP,
    STR_STATE_IDLE,
    STR_STATE_BUSY,
    STR_STATE_ATTENTION,
    STR_STATE_CELEBRATE,
    STR_STATE_DIZZY,
    STR_STATE_HEART,

    /* WiFi */
    STR_WIFI_NETWORKS,
    STR_SCAN_FOR_NEW,
    STR_CONNECT,
    STR_EDIT_PASSWORD,
    STR_DELETE,
    STR_SCANNING,
    STR_FMT_SSID,      /* "SSID: %s" / "SSID: %s" */
    STR_OPEN_NET,      /* "[open]" */
    STR_UPPERCASE,
    STR_LOWERCASE,

    /* Main screen hints */
    STR_SET_SYNC,
    STR_FMT_IP_OK,     /* "IP:%s [OK]" */
    STR_FMT_IP_SYNC,   /* "IP:%s [Sync..]" */
    STR_CONNECT_FAILED,
    STR_CONNECTING,
    STR_NO_WIFI,

    /* Hint bars */
    STR_H_SET_ENTER_PRESS_BACK,
    STR_H_SET_ENTER,
    STR_H_SET_TOGGLE_PRESS_BACK,
    STR_H_SET_SAVE_PRESS_CANCEL,
    STR_H_SET_EDIT_PRESS_BACK,
    STR_H_SET_CONFIRM_DEFAULT,
    STR_H_SET_CONFIRM_RESET,
    STR_H_SET_SELECT_PRESS_BACK,
    STR_H_SET_INPUT_PRESS_BACK,
    STR_H_SET_START_PAUSE_PRESS_STOP,
    STR_H_PRESS_BACK_SET_INFO,
    STR_H_PRESS_BACK_SET_SELECT,
    STR_H_ANY_KEY_BACK_ENCODER_SCROLL,
    STR_H_SET_PRESS_STOP_DEMO,

    /* Weekday abbreviations */
    STR_SUN, STR_MON, STR_TUE, STR_WED, STR_THU, STR_FRI, STR_SAT,

    /* Misc */
    STR_BACK,

    STR_COUNT
} str_id_t;

const char *i18n(str_id_t id);
const char *i18n_weekday(int wday);
