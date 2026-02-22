软件架构概览

运行环境
- ESP32-C3，基于 ESP-IDF + FreeRTOS
- LVGL 图形库用于 UI 渲染
- 核心应用作为嵌入式固件运行在一个较为稳定的软硬件抽象层之上

模块化设计原则
- 所有外设通过硬件抽象层（HAL）暴露统一接口，方便后续替换硬件而不影响应用逻辑
- 界面层与业务逻辑分离，便于扩展和维护

---

主要子系统

HAL 层（硬件抽象层）
- DisplayDriver：提供基本绘制 API、文字/图形渲染能力，底层通过 SPI DMA 与 ST7789 显示屏通信
- MicDriver：I2S 麦克风驱动，负责音频采集
- AudioDriver：I2S 功放驱动，负责音频输出
- EncoderDriver：EC11 编码器驱动（旋转、按键、消抖），提供滚轮增量事件和按下事件
- ButtonDriver：独立设置键的去抖与事件分发

应用模块
- ui_manager：UI 框架，界面切换、绘制、文本更新
- lvgl_example：LVGL 入口和示例场景
- wifi_manager：WiFi 连接、配置、断线重连、NTP 配置
- encoder：EC11 编码器驱动（旋转、按键、消抖）

通用服务
- TimeService：NTP/SNTP 时钟同步、时区处理、系统时钟更新
- WeatherService：天气查询服务（HTTP 请求），解析并缓存天气数据

存储
- NVS：持久化设置（工作/休息时长、时区、WiFi 信息、完成次数等）和简易历史

UI 与状态机
- ScreenManager：多屏切换与布局控制，负责屏幕的渲染调度
- ScreenState 枚举（MAIN, POMODORO, CHAT, SETTINGS）及各子界面的状态机

功能引擎
- PomodoroEngine：工作/休息时间、倒计时、完成次数、循环管理、计时器事件
- ChatEngine：对话引擎框架（当前版本为占位实现，后续实现智能对话）

事件与通信
- 事件队列：EncoderEvent、ButtonEvent、TimerEvent、NetworkEvent 等
- 任务间通信：使用 FreeRTOS 队列/信号量实现解耦

---

任务模型
- UI_Task：处理界面渲染与输入事件调度，基于 LVGL 主循环和状态机驱动
- Net_Task：处理 WiFi、NTP、天气接口的网络事件
- Pomodoro_Task：负责番茄钟的计时与状态转移
- Weather_Task：周期性获取天气并推送更新到 UI

数据模型
- Settings：工作/休息时长（分钟）、时区、地区、WiFi 配置、语言/显示选项等
- PomodoroState：{mode: WORK|BREAK|IDLE, remaining_sec, completed_count, cycle_index}
- WeatherState：{temp, humidity, condition, icon, last_updated}
- NetworkStatus：{connected, ip, rssi, ssid}

与外部系统的交互
- WiFi：启用 STA 模式，支持扫描、选择、输入密码并连接
- 时间：通过 NTP 同步时间，应用层可按时区更改显示
- 天气：对接天气 API，定期刷新；遇到网络异常时缓存最近数据

安全与鲁棒性
- 配网和敏感信息（WiFi 密码）通过 NVS 持久化
- 断网处理：UI 不丢失，显示离线状态并提供重连入口
- 输入消抖与按键去抖处理
- LVGL 主循环与任务调度的合理优先级

架构演进点
- ChatEngine 将接入对话模型（NLP/LLM API），网络层将支持 HTTPS/Socket 调用
- Display 以模块化方式扩展不同屏幕尺寸与分辨率
- 支持更多传感器和扩展外设

---

接口与契约

ui_manager
- ui_init()：初始化 UI 系统
- ui_switch(screen_id)：切换界面
- ui_update_time(time_str)：更新时间显示
- ui_update_wifi(status)：更新 WiFi 状态显示

wifi_manager
- init_wifi(config)：初始化 WiFi
- scan_networks() -> list：扫描可用网络
- connect(ssid, password) -> bool：连接指定网络
- get_status() -> {connected, ip, rssi, etc.}：获取连接状态

encoder
- init_encoder(pin_a, pin_b, pin_key)：初始化编码器
- on_rotate(delta)：旋转事件回调
- on_press()：按键按下回调
- on_long_press()：长按回调

pomodoro_engine
- start()：开始番茄钟
- pause() / resume()：暂停/继续
- reset()：重置计时器
- get_state()：获取当前状态

---

依赖与构建
- ESP-IDF 脚手架
- LVGL 图形库
- ST7789 驱动
- NTP 库
- NVS 存储
- 构建命令：idf.py build / flash / monitor
