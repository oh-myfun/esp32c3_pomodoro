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
    [STR_FMT_DONE]     = {"%lu done",    "%lu次"},

    /* Pomodoro phases */
    [STR_PHASE_WORK]        = {"🎯WORK",       "🎯工作中"},
    [STR_PHASE_BREAK]       = {"☕BREAK",      "☕休息中"},
    [STR_PHASE_LONG_BREAK]  = {"🏖LONG BREAK", "🏖长休息"},
    [STR_PHASE_PAUSED]      = {"⏸PAUSED",     "⏸已暂停"},
    [STR_PHASE_IDLE]        = {"⏹IDLE",       "⏹就绪"},

    /* Light settings */
    [STR_LIGHT]       = {"💡Light",       "💡灯光"},
    [STR_BRIGHT]      = {"☀Bright",      "☀亮度"},
    [STR_SPEED]       = {"⏩Speed",       "⏩速度"},
    [STR_STYLE]       = {"🎨Style",       "🎨样式"},
    [STR_ANIM]        = {"✨Anim",        "✨动画"},
    [STR_DEMO]        = {"▶Demo",        "▶演示"},
    [STR_SLOW]        = {"Slow",        "慢"},
    [STR_MED]         = {"Med",         "中"},
    [STR_FAST]        = {"Fast",        "快"},
    [STR_PURE]        = {"Pure",        "纯色"},
    [STR_COLOR]       = {"Color",       "彩色"},
    [STR_BREATH]      = {"Breath",      "呼吸"},
    [STR_SCAN]        = {"\xf0\x9f\x94\x8dScan",        "\xf0\x9f\x94\x8d\xe6\x89\xab\xe6\x8f\x8f"},
    [STR_GRADIENT]    = {"Gradient",    "渐变"},

    /* Time settings */
    [STR_TIMEZONE]    = {"🌍Timezone",    "🌍时区"},
    [STR_NTP_SERVER]  = {"🖥NTP Server",  "🖥NTP服务器"},
    [STR_NTP_INTERVAL]= {"⏱NTP Interval","⏱NTP间隔"},
    [STR_OFF_VAL]     = {"Off",         "关"},

    /* Buddy settings */
    [STR_SPECIES]     = {"🐾Species",     "🐾物种"},

    /* Buddy page */
    [STR_BUDDY_NAME]  = {"\xf0\x9f\xa4\x96Buddy",       "\xf0\x9f\xa4\x96\xe4\xbc\x99\xe4\xbc\xb4"},
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
    [STR_SET_SYNC]       = {"SET:sync",    "SET:同步"},
    [STR_FMT_IP_OK]      = {"IP:%s [OK]", "IP:%s [OK]"},
    [STR_FMT_IP_SYNC]    = {"IP:%s [Sync..]", "IP:%s [同步..]"},
    [STR_CONNECT_FAILED] = {"Connect Failed", "连接失败"},
    [STR_CONNECTING]     = {"Connecting...", "连接中..."},
    [STR_NO_WIFI]        = {"No WiFi",     "无WiFi"},

    /* Hint bars */
    [STR_H_SET_ENTER_PRESS_BACK]       = {"SET:enter|Press:back",  "SET:进入|Press:返回"},
    [STR_H_SET_ENTER]                   = {"SET:enter",             "SET:进入"},
    [STR_H_SET_TOGGLE_PRESS_BACK]      = {"SET:toggle|Press:back", "SET:切换|Press:返回"},
    [STR_H_SET_SAVE_PRESS_CANCEL]      = {"SET:save|Press:cancel", "SET:保存|Press:取消"},
    [STR_H_SET_EDIT_PRESS_BACK]        = {"SET:edit|Press:back",   "SET:编辑|Press:返回"},
    [STR_H_SET_CONFIRM_DEFAULT]        = {"SET:confirm default",   "SET:确认默认"},
    [STR_H_SET_CONFIRM_RESET]          = {"SET:confirm reset",     "SET:确认重置"},
    [STR_H_SET_SELECT_PRESS_BACK]      = {"SET:select|Press:back", "SET:选择|Press:返回"},
    [STR_H_SET_INPUT_PRESS_BACK]       = {"SET:input|Press:back",  "SET:输入|Press:返回"},
    [STR_H_SET_START_PAUSE_PRESS_STOP] = {"SET:start/pause|Press:stop", "SET:开始/暂停|Press:停止"},
    [STR_H_PRESS_BACK_SET_INFO]        = {"Press:back|SET:info",   "Press:返回|SET:信息"},
    [STR_H_BUDDY_HINT]                 = {"SET:interact|Press:set","SET:互动|Press:设置"},
    [STR_H_PRESS_BACK_SET_SELECT]      = {"Press:back|SET:select", "Press:返回|SET:选择"},
    [STR_H_ANY_KEY_BACK_ENCODER_SCROLL]= {"Any key:back|Encoder:scroll", "任意键:返回|编码器:滚动"},
    [STR_H_SET_PRESS_STOP_DEMO]        = {"SET/Press:stop demo",   "SET/Press:停止演示"},

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
    [STR_CONNECT_ACTION]  = {"\xf0\x9f\x93\xa1Connect",         "\xf0\x9f\x93\xa1\xe8\xbf\x9e\xe6\x8e\xa5"},
    [STR_DISCONNECT]      = {"\xf0\x9f\x93\xb4Disconnect",      "\xf0\x9f\x93\xb4\xe6\x96\xad\xe5\xbc\x80"},

    /* Bridge scan */
    [STR_NO_BRIDGE]       = {"No bridges found",    "未发现Bridge"},
    [STR_FMT_SCAN_RESULT] = {"%d host, %d session", "%d主机 %d会话"},
    [STR_SUBMIT]          = {"Submit",              "提交"},
    [STR_OK]              = {"OK",                  "OK"},
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
