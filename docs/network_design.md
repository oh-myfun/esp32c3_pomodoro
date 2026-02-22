网络设计说明

目标
- 通过 WiFi 将设备接入局域网，实现时间同步和天气查询，同时提供简易网络设置界面以便用户配置
- 设计要点：稳定性、容错能力、用户友好性（易于通过编码器导航设置）

---

系统能力

时间同步
- 使用 SNTP/NTP 服务获取标准时间，时区转换后用于 UI 显示
- 同步时机：WiFi 连接成功后自动同步，可手动触发重新同步

天气查询
- 通过公网天气 API 获取当前天气与条件
- 缓存最近一次结果并在网络异常时使用
- 定期刷新（如每 30 分钟）

WiFi 配置
- 提供扫描周围可用网络、显示清单、选择目标、输入密码并连接的流程
- 连接状态通过 UI 显示
- 断网时自动重试与重连策略

---

模块设计和接口

WifiManager
- scanNetworks() -> list<NetworkInfo>：扫描可用网络，返回 SSID、信号强度等
- connect(ssid, password) -> bool：连接指定网络
- disconnect()：断开连接
- getStatus() -> StatusEnum：获取连接状态（IDLE/SCANNING/CONNECTING/CONNECTED/FAILED）
- 事件：scanFinished, connected, disconnected, connectionFailed

TimeService
- syncTime() -> bool：同步 NTP 时间
- getCurrentTime() -> time_t：获取当前时间戳
- setTimeZone(tz_string)：设置时区（如 "UTC+8" 或 "Asia/Shanghai"）
- getTimeZone() -> string：获取当前时区

WeatherService
- fetchWeather(city or coords) -> WeatherState：获取天气数据
- setApiKey(key)：设置天气 API 密钥
- setUnits(metric/imperial)：设置温度单位
-  WeatherState 包含：temp, humidity, condition, icon, last_updated

---

UI 集成

WiFi 设置子界面流程
1. 进入设置 -> WiFi 设置
2. 扫描网络（显示加载中）
3. 显示可用网络列表（SSID + 信号强度）
4. 编码器滚动选择网络，按键确认进入密码输入
5. 输入密码（虚拟键盘或数字滚轮）
6. 连接并显示结果

网络状态显示
- 主界面显示 WiFi 连接状态图标
- 断网时显示离线提示

---

容错与边界

- 无网络时，显示离线状态但保留本地时钟，减少对用户操作的干扰
- 天气接口错误时，回退显示最近一次有效数据
- 扫描结果缓存，避免频繁扫描带来功耗浪费
- WiFi 连接失败时提供明确的错误提示，并允许重试
