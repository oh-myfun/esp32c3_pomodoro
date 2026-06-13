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
    STR_M_BRIDGE,

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
    STR_RESET_COUNT,   /* "Reset Count" / "重置计数" (番茄钟) */
    STR_FMT_MIN,       /* "%d min" / "%d分钟" */
    STR_FMT_HOUR,      /* "%dh" / "%d小时" */
    STR_FMT_HOUR_MIN,  /* "%dh%dmin" / "%d小时%d分钟" */
    STR_FMT_SEC,       /* "%ds" / "%d秒" */
    STR_FMT_DONE,      /* "%lu done" / "%lu次" */

    /* Pomodoro phases */
    STR_PHASE_WORK,
    STR_PHASE_BREAK,
    STR_PHASE_LONG_BREAK,
    STR_PHASE_PAUSED,
    STR_PHASE_IDLE,
    STR_PHASE_TIMER,       /* "⏱Timer" / "⏱计时器" */
    STR_PHASE_ALARM,       /* "🔔Alarm" / "🔔闹铃" */

    /* Alarm settings */
    STR_T_ALARM,           /* "⏱Alarm" / "⏱闹钟" */
    STR_ALARM_DURATION,    /* "Duration" / "时长" */

    /* Light settings */
    STR_LIGHT,
    STR_BACKLIGHT,
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
    STR_APPROVE_REMEMBER,
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

    /* Main screen hints  (hint bars: TOP=顶键, SIDE=侧键) */
    STR_SET_SYNC,
    STR_WIFI_CONNECTED,   /* "[● Connected]" / "[● 已连接]" */
    STR_WIFI_SYNCING,     /* "[◎ Syncing]" / "[◎ 同步中]" */
    STR_CONNECT_FAILED,
    STR_CONNECTING,
    STR_SCANNING_MAIN,    /* "[○ Scanning...]" / "[○ 扫描中]" */
    STR_NO_WIFI,

    /* Hint bars */
    STR_H_SET_ENTER_PRESS_BACK,
    STR_H_SET_ENTER,
    STR_H_SET_TOGGLE_PRESS_BACK,
    STR_H_SET_SAVE_PRESS_CANCEL,
    STR_H_SET_EDIT_PRESS_BACK,
    STR_H_SET_SELECT_PRESS_BACK,
    STR_H_SET_INPUT_PRESS_BACK,
    STR_H_SET_START_PAUSE_PRESS_STOP,
    STR_H_PRESS_BACK_SET_INFO,
    STR_H_BUDDY_HINT,
    STR_H_PRESS_BACK_SET_SELECT,
    STR_H_ANY_KEY_BACK_ENCODER_SCROLL,
    STR_H_SET_PRESS_STOP_DEMO,

    /* Weekday abbreviations */
    STR_SUN, STR_MON, STR_TUE, STR_WED, STR_THU, STR_FRI, STR_SAT,

    /* Misc */
    STR_BACK,
    STR_TCP_CONN,
    STR_TCP_DISCONN,

    /* Buddy settings */
    STR_HOST,
    STR_PORT,
    STR_SESSION,
    STR_CONNECT_ACTION,
    STR_DISCONNECT,

    /* Bridge scan */
    STR_NO_BRIDGE,       /* "No bridges found" / "未发现Bridge" */
    STR_FMT_SCAN_RESULT, /* "%d host, %d session" / "%d主机 %d会话" */
    STR_SUBMIT,          /* "Submit" / "提交" */
    STR_OK,              /* "OK" / "OK" */

    /* Sensor page */
    STR_T_SENSOR,          /* "Sensor" / "传感器" */
    STR_T_SENSOR_PAGE,     /* "🌡Temp Hum Press Alt" / "🌡温湿度气压海拔" */
    STR_SENSOR_SETTINGS,   /* "Sensor Settings" / "传感器设置" */
    STR_TEMP_MIN,          /* "Temp Min" / "温度最小" */
    STR_TEMP_MAX,          /* "Temp Max" / "温度最大" */
    STR_PRESS_MIN,         /* "Press Min" / "气压最小" */
    STR_PRESS_MAX,         /* "Press Max" / "气压最大" */
    STR_ALT_MIN,           /* "Alt Min" / "海拔最小" */
    STR_ALT_MAX,           /* "Alt Max" / "海拔最大" */
    STR_TEMP_SOURCE,       /* "Temp Src" / "温度源" */
    STR_SRC_AHT20,         /* "AHT20" / "AHT20" */
    STR_SRC_BMP280,        /* "BMP280" / "BMP280" */
    STR_SRC_AVG,           /* "Average" / "平均值" */
    STR_SAMPLE_INTERVAL,   /* "Interval" / "采样间隔" */
    STR_SEC_LEVEL,         /* "Sec" / "秒级" */
    STR_MIN_LEVEL,         /* "Min" / "分级" */
    STR_HOUR_LEVEL,        /* "Hour" / "时级" */
    STR_DAY_LEVEL,         /* "Day" / "天级" */
    STR_H_SENSOR_HINT,     /* "TOP:View SIDE:Set" / "顶:切换 侧:设置" */
    STR_H_SENSOR_EDIT,     /* "TOP:Save Enc:Adj" / "顶:保存 编码器:调节" */

    /* Sleep timeout */
    STR_SLEEP_TIMEOUT,

    /* Demo colors */
    STR_DEMO_WORK,
    STR_DEMO_BREAK,
    STR_DEMO_LONG_BREAK,
    STR_DEMO_PAUSED,
    STR_DEMO_SAD,

    /* Pressure info */
    STR_PRESSURE_INFO,
    STR_PRESSURE_TIP,
    STR_PI_HDR,         /* table header */
    STR_PI_DAILY,
    STR_PI_RAIN,
    STR_PI_STORM,
    STR_PI_TYPHOON,
    STR_PI_NOTE,        /* bottom note */

    /* Long press hint */
    STR_KEY_DUAL,       /* "BOTH" / "双键" */
    STR_ACT_SWITCH_PET, /* "Switch Pet" / "切换宠物" */
    STR_ACT_DELETE,     /* "Delete" / "删除" */
    STR_ACT_SWITCH_MODE,/* "Switch Mode" / "切换模式" */
    STR_ACT_RESET,      /* "Screen Reset" / "屏幕重置" */

    /* Sound settings */
    STR_T_SOUND,            /* "🔊Sound" / "🔊声音" */
    STR_M_SOUND,            /* "🔊Sound" / "🔊声音" */
    STR_SND_KEY,            /* "🔑Key" / "🔑按键" */
    STR_SND_UI,             /* "✅UI" / "✅操作" */
    STR_SND_NET,            /* "📶Net" / "📶网络" */
    STR_SND_POMO,           /* "🍅Pomo" / "🍅番茄" */
    STR_SND_BUDDY,          /* "🐱Buddy" / "🐱宠物" */
    STR_SND_HOUR,           /* "🔔Hour" / "🔔整点" */
    STR_SND_HALF,           /* "🔎Half" / "🔎半点" */
    STR_QUIET_START,        /* "🌙Q.Start" / "🌙静默起" */
    STR_QUIET_END,          /* "🌙Q.End" / "🌙静默止" */

    /* Light/Sound demo + individual sound names */
    STR_SND_DEMO,           /* "🎬Demo" / "🎬演示" */

    STR_SOUND_KEY_CLICK,        /* "KeyClick" / "按键音" */
    STR_SOUND_CONFIRM,          /* "Confirm" / "确认" */
    STR_SOUND_CANCEL,           /* "Cancel" / "取消" */
    STR_SOUND_SUCCESS,          /* "Success" / "成功" */
    STR_SOUND_FAIL,             /* "Fail" / "失败" */
    STR_SOUND_WIFI_CONNECT,     /* "WiFi..." / "WiFi连接" */
    STR_SOUND_WIFI_CONNECTED,   /* "WiFi OK" / "WiFi已连" */
    STR_SOUND_WIFI_FAILED,      /* "WiFi Fail" / "WiFi失败" */
    STR_SOUND_SYNC_START,       /* "Sync..." / "同步中" */
    STR_SOUND_SYNC_DONE,        /* "SyncDone" / "同步完成" */
    STR_SOUND_POMO_START,       /* "PomoStart" / "番茄开始" */
    STR_SOUND_POMO_WORK_START,  /* "WorkStart" / "工作开始" */
    STR_SOUND_POMO_BREAK_START, /* "BreakStart" / "休息开始" */
    STR_SOUND_POMO_WORK_DONE,   /* "WorkDone" / "工作完成" */
    STR_SOUND_POMO_BREAK_DONE,  /* "BreakDone" / "休息完成" */
    STR_SOUND_POMO_LONG_BREAK,  /* "LongBreak" / "长休息" */
    STR_SOUND_BUDDY_ATTENTION,  /* "Attn" / "注意" */
    STR_SOUND_BUDDY_HAPPY,      /* "Happy" / "开心" */
    STR_SOUND_BUDDY_SAD,        /* "Sad" / "伤心" */

    STR_DEMO_HOUR_CHIME,    /* "HourChime" / "整点报时" */
    STR_DEMO_HALF_CHIME,    /* "HalfChime" / "半点报时" */
    STR_FMT_CHIME_HOUR,    /* "%d:00" / "%d点" */

    /* Pet species names (顺序与 buddy_species_table 一致) */
    STR_PET_CAPYBARA,   /* "Capybara" / "水豚" */
    STR_PET_DUCK,       /* "Duck"     / "鸭子" */
    STR_PET_GOOSE,      /* "Goose"    / "鹅" */
    STR_PET_BLOB,       /* "Blob"     / "史莱姆" */
    STR_PET_CAT,        /* "Cat"      / "猫咪" */
    STR_PET_DRAGON,     /* "Dragon"   / "小龙" */
    STR_PET_OCTOPUS,    /* "Octopus"  / "章鱼" */
    STR_PET_OWL,        /* "Owl"      / "猫头鹰" */
    STR_PET_PENGUIN,    /* "Penguin"  / "企鹅" */
    STR_PET_TURTLE,     /* "Turtle"   / "乌龟" */
    STR_PET_SNAIL,      /* "Snail"    / "蜗牛" */
    STR_PET_GHOST,      /* "Ghost"    / "幽灵" */
    STR_PET_AXOLOTL,    /* "Axolotl"  / "六角龙" */
    STR_PET_CACTUS,     /* "Cactus"   / "仙人掌" */
    STR_PET_ROBOT,      /* "Robot"    / "机器人" */
    STR_PET_RABBIT,     /* "Rabbit"   / "兔子" */
    STR_PET_MUSHROOM,   /* "Mushroom" / "蘑菇" */
    STR_PET_CHONK,      /* "Chonk"    / "胖墩" */

    STR_COUNT
} str_id_t;

const char *i18n(str_id_t id);
const char *i18n_weekday(int wday);
