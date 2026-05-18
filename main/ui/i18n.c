#include "i18n.h"
#include "service/storage_service.h"
#include "esp_log.h"

static const char *TAG = "I18N";

static lang_t current_lang = LANG_EN;

static const char *strings[STR_COUNT][2] = {
    /* Page titles */
    [STR_T_SETTINGS]   = {"⚙Settings",    "⚙设置"},
    [STR_T_POMODORO]   = {"🍅Pomodoro",    "🍅番茄钟"},
    [STR_T_BUDDY]      = {"🐱Buddy",       "🐱伙伴"},
    [STR_T_WIFI]       = {"📶WiFi",        "📶WiFi"},
    [STR_T_PASSWORD]   = {"🔑Password",    "🔑密码"},
    [STR_T_SYSTEM]     = {"⚙System",       "⚙系统"},
    [STR_T_TIME]       = {"🕐Time",         "🕐时间"},
    [STR_T_LIGHT]      = {"💡Light",       "💡灯光"},
    [STR_T_DEBUG]      = {"🔧Debug",       "🔧调试"},

    /* Settings menu items */
    [STR_M_POMODORO]   = {"🍅Pomodoro",    "🍅番茄钟"},
    [STR_M_BUDDY]      = {"🐱Buddy",       "🐱伙伴"},
    [STR_M_LIGHT]      = {"💡Light",       "💡灯光"},
    [STR_M_WIFI]       = {"📶WiFi",        "📶网络"},
    [STR_M_TIME]       = {"🕐Time",         "🕐时间"},
    [STR_M_SYSTEM]     = {"⚙System",       "⚙系统"},
    [STR_M_DEBUG]      = {"🔧Debug",       "🔧调试"},
    [STR_M_BRIDGE]     = {"🌉Bridge",      "🌉Bridge"},

    /* System settings */
    [STR_SOUND]        = {"🔊Sound",       "🔊声音"},
    [STR_DIRECTION]    = {"↔Direction",   "↔方向"},
    [STR_LANGUAGE]     = {"🌐Language",    "🌐语言"},
    [STR_ON]           = {"✅On",          "✅开"},
    [STR_OFF]          = {"❎Off",         "❎关"},
    [STR_NORMAL]       = {"Normal",      "正常"},
    [STR_REV]          = {"Rev",         "反转"},
    [STR_LANG_EN]      = {"English",     "English"},
    [STR_LANG_ZH]      = {"Chinese",     "中文"},

    /* Pomodoro settings */
    [STR_WORK]         = {"🎯Work",        "🎯工作"},
    [STR_BREAK]        = {"☕Break",       "☕休息"},
    [STR_LONG_BREAK]   = {"🏖Long Break",  "🏖长休息"},
    [STR_CYCLES]       = {"🔁Cycles",      "🔁轮次"},
    [STR_MODE]         = {"📋Mode",        "📋模式"},
    [STR_MANUAL]       = {"✋Manual",      "✋手动"},
    [STR_AUTO]         = {"⚡Auto",        "⚡自动"},
    [STR_DEFAULT]      = {"📌Default",     "📌默认值"},
    [STR_RESET]        = {"🔄Reset",       "🔄重置"},
    [STR_FMT_MIN]      = {"%d min",      "%d分钟"},
    [STR_FMT_SEC]      = {"%ds",         "%d秒"},
    [STR_FMT_DONE]     = {"%lu done",    "%lu次"},

    /* Pomodoro phases */
    [STR_PHASE_WORK]        = {"🎯WORK",       "🎯工作中"},
    [STR_PHASE_BREAK]       = {"☕BREAK",      "☕休息中"},
    [STR_PHASE_LONG_BREAK]  = {"🏖LONG BREAK", "🏖长休息"},
    [STR_PHASE_PAUSED]      = {"⏸PAUSED",     "⏸已暂停"},
    [STR_PHASE_IDLE]        = {"⏹IDLE",       "⏹就绪"},

    /* Light settings */
    [STR_LIGHT]       = {"💡Light",       "💡灯光"},
    [STR_BACKLIGHT]   = {"🔆Backlight",  "🔆背光"},
    [STR_BRIGHT]      = {"☀Bright",      "☀亮度"},
    [STR_SPEED]       = {"⏩Speed",       "⏩速度"},
    [STR_STYLE]       = {"🎨Style",       "🎨样式"},
    [STR_ANIM]        = {"✨Anim",        "✨动画"},
    [STR_DEMO]        = {"Demo",        "演示"},
    [STR_SLOW]        = {"Slow",        "慢"},
    [STR_MED]         = {"Med",         "中"},
    [STR_FAST]        = {"Fast",        "快"},
    [STR_PURE]        = {"Pure",        "纯色"},
    [STR_COLOR]       = {"Color",       "彩色"},
    [STR_BREATH]      = {"Breath",      "呼吸"},
    [STR_SCAN]        = {"Scan",        "扫描"},
    [STR_GRADIENT]    = {"Gradient",    "渐变"},

    /* Time settings */
    [STR_TIMEZONE]    = {"🌍Timezone",    "🌍时区"},
    [STR_NTP_SERVER]  = {"🖥NTP Server",  "🖥NTP服务器"},
    [STR_NTP_INTERVAL]= {"⏱NTP Interval","⏱NTP间隔"},
    [STR_OFF_VAL]     = {"Off",         "关"},

    /* Buddy settings */
    [STR_SPECIES]     = {"🐾Species",     "🐾物种"},

    /* Buddy page */
    [STR_BUDDY_NAME]  = {"\xf0\x9f\xa4\x96" "Buddy",       "\xf0\x9f\xa4\x96" "\xe4\xbc\x99\xe4\xbc\xb4"},
    [STR_PERMISSION]  = {"!! PERMISSION !!", "!! 权限请求 !!"},
    [STR_APPROVE]     = {"Approve",     "批准"},
    [STR_APPROVE_REMEMBER] = {"Allow+Remember", "允许并记住"},
    [STR_DENY]        = {"Deny",        "拒绝"},
    [STR_NEXT_PET]    = {"Next Pet",    "下一只"},
    [STR_TOOL]        = {"Tool:",       "工具:"},
    [STR_FMT_STATE]   = {"State: %s",   "状态: %s"},
    [STR_FMT_TOOL]    = {"Tool: %s",    "工具: %s"},
    [STR_PET_LABEL]   = {"Pet: ",       "宠物: "},
    [STR_OWNER_LABEL] = {"Owner: ",     "主人: "},
    [STR_APPROVED_LABEL] = {"Approved: ", "已批准: "},
    [STR_DENIED_LABEL]= {"Denied: ",    "已拒绝: "},
    [STR_BLE_CONN]    = {"BLE: Connected",    "BLE: 已连接"},
    [STR_BLE_DISCONN] = {"BLE: Disconnected", "BLE: 未连接"},

    /* Buddy states */
    [STR_STATE_SLEEP]     = {"😴Sleep",      "😴睡觉"},
    [STR_STATE_IDLE]      = {"😊Idle",       "😊空闲"},
    [STR_STATE_BUSY]      = {"💼Busy",       "💼忙碌"},
    [STR_STATE_ATTENTION] = {"⚠Attention!", "⚠注意!"},
    [STR_STATE_CELEBRATE] = {"🎉Celebrate!", "🎉庆祝!"},
    [STR_STATE_DIZZY]     = {"😵Dizzy...",   "😵晕了..."},
    [STR_STATE_HEART]     = {"❤+1",         "❤+1"},

    /* WiFi */
    [STR_WIFI_NETWORKS] = {"📶Networks", "📶WiFi网络"},
    [STR_SCAN_FOR_NEW]  = {"🔍Scan...",  "🔍扫描..."},
    [STR_CONNECT]       = {"🔗Connect",     "🔗连接"},
    [STR_EDIT_PASSWORD] = {"✏Password",    "✏修改密码"},
    [STR_DELETE]        = {"🗑Delete",      "🗑删除"},
    [STR_SCANNING]      = {"Scanning...", "扫描中..."},
    [STR_FMT_SSID]      = {"SSID: %s",    "SSID: %s"},
    [STR_OPEN_NET]      = {"🔓[open]",      "🔓[开放]"},
    [STR_UPPERCASE]     = {"Uppercase",   "大写"},
    [STR_LOWERCASE]     = {"Lowercase",   "小写"},

    /* Main screen hints */
    [STR_SET_SYNC]       = {"TOP:sync",    "顶:同步"},
    [STR_WIFI_CONNECTED] = {"[● Connected]", "[● 已连接]"},
    [STR_WIFI_SYNCING]   = {"[◎ Syncing]", "[◎ 同步中]"},
    [STR_CONNECT_FAILED] = {"[✕ Failed]", "[✕ 连接失败]"},
    [STR_CONNECTING]     = {"[◎ Connecting]", "[◎ 连接中]"},
    [STR_SCANNING_MAIN]  = {"[○ Scanning]", "[○ 扫描中]"},
    [STR_NO_WIFI]        = {"[○ No WiFi]", "[○ 无WiFi]"},

    /* Hint bars: TOP=顶键(SET), SIDE=侧键(Encoder Press) */
    [STR_H_SET_ENTER_PRESS_BACK]       = {"TOP:enter|SIDE:back",  "顶:进入|侧:返回"},
    [STR_H_SET_ENTER]                   = {"TOP:enter",             "顶:进入"},
    [STR_H_SET_TOGGLE_PRESS_BACK]      = {"TOP:toggle|SIDE:back", "顶:切换|侧:返回"},
    [STR_H_SET_SAVE_PRESS_CANCEL]      = {"TOP:save|SIDE:cancel", "顶:保存|侧:取消"},
    [STR_H_SET_EDIT_PRESS_BACK]        = {"TOP:edit|SIDE:back",   "顶:编辑|侧:返回"},
    [STR_H_SET_CONFIRM_DEFAULT]        = {"TOP:confirm default",   "顶:确认默认"},
    [STR_H_SET_CONFIRM_RESET]          = {"TOP:confirm reset",     "顶:确认重置"},
    [STR_H_SET_SELECT_PRESS_BACK]      = {"TOP:select|SIDE:back", "顶:选择|侧:返回"},
    [STR_H_SET_INPUT_PRESS_BACK]       = {"TOP:input|SIDE:back",  "顶:输入|侧:返回"},
    [STR_H_SET_START_PAUSE_PRESS_STOP] = {"TOP:start/pause|SIDE:stop", "顶:开始/暂停|侧:停止"},
    [STR_H_PRESS_BACK_SET_INFO]        = {"SIDE:back|TOP:info",   "侧:返回|顶:信息"},
    [STR_H_BUDDY_HINT]                 = {"TOP:interact|SIDE:set","顶:互动|侧:设置"},
    [STR_H_PRESS_BACK_SET_SELECT]      = {"SIDE:back|TOP:select", "侧:返回|顶:选择"},
    [STR_H_ANY_KEY_BACK_ENCODER_SCROLL]= {"Any key:back|Encoder:scroll", "任意键:返回|编码器:滚动"},
    [STR_H_SET_PRESS_STOP_DEMO]        = {"TOP/SIDE:stop demo",   "顶/侧:停止演示"},

    /* Weekday abbreviations */
    [STR_SUN]  = {"Sun", "周日"},
    [STR_MON]  = {"Mon", "周一"},
    [STR_TUE]  = {"Tue", "周二"},
    [STR_WED]  = {"Wed", "周三"},
    [STR_THU]  = {"Thu", "周四"},
    [STR_FRI]  = {"Fri", "周五"},
    [STR_SAT]  = {"Sat", "周六"},

    /* Misc */
    [STR_BACK]        = {"Back",        "返回"},
    [STR_TCP_CONN]    = {"TCP: Connected",    "TCP: 已连接"},
    [STR_TCP_DISCONN] = {"TCP: Disconnected", "TCP: 未连接"},

    /* Buddy settings */
    [STR_HOST]            = {"\xf0\x9f\x8c\x90Host",            "\xf0\x9f\x8c\x90\xe4\xb8\xbb\xe6\x9c\xba"},
    [STR_PORT]            = {"\xf0\x9f\x94\x8cPort",            "\xf0\x9f\x94\x8c\xe7\xab\xaf\xe5\x8f\xa3"},
    [STR_SESSION]         = {"\xf0\x9f\x94\x91Session",         "\xf0\x9f\x94\x91\xe4\xbc\x9a\xe8\xaf\x9d"},
    [STR_CONNECT_ACTION]  = {"\xf0\x9f\x93\xa1" "Connect",         "\xf0\x9f\x93\xa1" "\xe8\xbf\x9e\xe6\x8e\xa5"},
    [STR_DISCONNECT]      = {"\xf0\x9f\x93\xb4" "Disconnect",      "\xf0\x9f\x93\xb4" "\xe6\x96\xad\xe5\xbc\x80"},

    /* Bridge scan */
    [STR_NO_BRIDGE]       = {"No bridges found",    "未发现Bridge"},
    [STR_FMT_SCAN_RESULT] = {"%d host, %d session", "%d主机 %d会话"},
    [STR_SUBMIT]          = {"Submit",              "提交"},
    [STR_OK]              = {"OK",                  "OK"},

    /* Sensor page */
    [STR_T_SENSOR]         = {"Sensor",              "传感器"},
    [STR_T_SENSOR_PAGE]    = {"\xf0\x9f\x8c\xa1Temp Hum Press Alt", "\xf0\x9f\x8c\xa1\xe6\xb8\xa9\xe6\xb9\xbf\xe5\xba\xa6\xe6\xb0\x94\xe5\x8e\x8b\xe6\xb5\xb7\xe6\x8b\x94"},
    [STR_SENSOR_SETTINGS]  = {"Sensor Settings",     "传感器设置"},
    [STR_TEMP_MIN]         = {"Temp Min",            "温度最小"},
    [STR_TEMP_MAX]         = {"Temp Max",            "温度最大"},
    [STR_PRESS_MIN]        = {"Press Min",           "气压最小"},
    [STR_PRESS_MAX]        = {"Press Max",           "气压最大"},
    [STR_ALT_MIN]          = {"Alt Min",             "海拔最小"},
    [STR_ALT_MAX]          = {"Alt Max",             "海拔最大"},
    [STR_TEMP_SOURCE]      = {"Temp Src",            "温度源"},
    [STR_SRC_AHT20]        = {"AHT20",               "AHT20"},
    [STR_SRC_BMP280]       = {"BMP280",              "BMP280"},
    [STR_SRC_AVG]          = {"Average",             "平均值"},
    [STR_SAMPLE_INTERVAL]  = {"Interval",            "采样间隔"},
    [STR_SEC_LEVEL]        = {"Sec",                 "秒级"},
    [STR_MIN_LEVEL]        = {"Min",                 "分钟级"},
    [STR_HOUR_LEVEL]       = {"Hour",                "小时级"},
    [STR_DAY_LEVEL]        = {"Day",                 "日级"},
    [STR_H_SENSOR_HINT]    = {"TOP:View SIDE:Set", "顶:切换 侧:设置"},
    [STR_H_SENSOR_EDIT]    = {"TOP:Save Enc:Adj",    "顶:保存 编码器:调节"},

    /* Sleep timeout */
    [STR_SLEEP_TIMEOUT] = {"💤Sleep",      "💤休眠"},

    /* Demo colors */
    [STR_DEMO_WORK]       = {"Work",        "工作"},
    [STR_DEMO_BREAK]      = {"Break",       "休息"},
    [STR_DEMO_LONG_BREAK] = {"LongBreak",   "长休息"},
    [STR_DEMO_PAUSED]     = {"Paused",      "暂停"},
    [STR_DEMO_SAD]        = {"Sad",         "悲伤"},

    /* Pressure info */
    [STR_PRESSURE_INFO] = {"\xf0\x9f\x92\xa1" "Pressure Info", "\xf0\x9f\x92\xa1" "\xe6\xb0\x94\xe5\x8e\x8b\xe5\xb0\x8f\xe7\x9f\xa5\xe8\xaf\x86"},
    [STR_PRESSURE_TIP]   = {"Any key:back",          "\xe4\xbb\xbb\xe6\x84\x8f\xe9\x94\xae:\xe8\xbf\x94\xe5\x9b\x9e"},
    [STR_PI_HDR]         = {"Weather   -hPa    +m",  "\xe5\xa4\xa9\xe6\xb0\x94     -hPa    +m"},
    [STR_PI_DAILY]       = {"Daily     1~4    8~33", "\xe6\x97\xa5\xe5\x8f\x98\xe5\x8c\x96    1~4    8~33"},
    [STR_PI_RAIN]        = {"Rain      5~15   42~126","\xe9\x99\x8d\xe9\x9b\xa8\xe5\x89\x8d    5~15   42~126"},
    [STR_PI_STORM]       = {"Storm    15~30  126~253","\xe6\x9a\xb4\xe9\xa3\x8e\xe9\x9b\xa8   15~30  126~253"},
    [STR_PI_TYPHOON]     = {"Typhoon   100+   868+", "\xe5\x8f\xb0\xe9\xa3\x8e      100+   868+"},
    [STR_PI_NOTE]        = {"Lower pressure =\nhigher altitude.", "\xe6\xb0\x94\xe5\x8e\x8b\xe8\xb6\x8a\xe4\xbd\x8e\n\xe6\xb5\xb7\xe6\x8b\x94\xe8\xb6\x8a\xe9\xab\x98"},
};

void i18n_init(void)
{
    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, &val)) {
        current_lang = (val != 0) ? LANG_ZH : LANG_EN;
    }
    ESP_LOGI(TAG, "Language: %s", current_lang == LANG_ZH ? "Chinese" : "English");
}

lang_t i18n_get_lang(void)
{
    return current_lang;
}

void i18n_set_lang(lang_t lang)
{
    current_lang = lang;
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, (int32_t)lang);
    ESP_LOGI(TAG, "Language set to: %s", lang == LANG_ZH ? "Chinese" : "English");
}

const char *i18n(str_id_t id)
{
    if (id < 0 || id >= STR_COUNT) return "";
    return strings[id][current_lang];
}

const char *i18n_weekday(int wday)
{
    static const str_id_t day_ids[7] = {
        STR_SUN, STR_MON, STR_TUE, STR_WED, STR_THU, STR_FRI, STR_SAT
    };
    if (wday < 0 || wday > 6) return "";
    return i18n(day_ids[wday]);
}
