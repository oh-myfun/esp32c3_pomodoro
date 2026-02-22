番茄钟设计说明
目标
- 提供可配置的工作时长与休息时长，支持多轮工作-休息循环，记录并显示完成次数。
- UI 驱动：编码器滚轮用于界面导航，按键触发番茄钟开始/暂停/结束、以及在设置界面进行设置。
- 与网络无强耦合，核心计时在本地实现，支持断网情况下仍累计完成次数。

核心数据结构
- Settings
  - work_minutes: uint16
  - break_minutes: uint16
  - long_break_after_cycles: uint16 (可选扩展)
- PomodoroState
  - phase: ENUM {IDLE, WORK, BREAK, PAUSED}
  - remaining_seconds: uint32
  - completed_count: uint32
  - current_cycle: uint16
  - last_start_timestamp: uint32 (epoch seconds)
- PersistentStorage
  - nvs keys: 
    - "pom_work_min", "pom_break_min", "pom_cycles",
    - "pom_completed", "pom_timezone" 等

工作流程
- 启动：在 MAIN 界面选中番茄钟，按键执行开始，进入 WORK 阶段，remaining_seconds = work_minutes * 60。
- 计时机制：使用高精度计时器/任务时钟，1 Hz 或 10 Hz 更新界面剩余时间，显示 mm:ss。
- 结束一轮：WORK 完成后进入 BREAK，若 BREAK 结束，则进入下一轮的 WORK，完成次数累加。
- 暂停/继续：按键在 PAUSED 状态切换到 WORK/BREAK。
- 结束/清零：在 MAIN 或 POMODORO 设置界面进行清零，重设 completed_count。
- 持久化：完成次数、当前设定等在 NVS 保存，设备重启后自动恢复。
- 界面交互：编码器滚轮切换界面、按键进行控制；在设置子界面内可修改 work/break 时长。

界面交互要点
- 主界面显示：当前时间、温湿度、天气、番茄钟计时（mm:ss）、完成计数。
- 番茄钟界面：
  - 显示剩余时间 mm:ss
  - 显示当前轮次完成数 / 总轮次（如果实现总轮次上限）
  - 操作：开始、暂停、重置、下一轮/恢复等由按键触发。
- 设置界面内的番茄钟设置子界面：可设置 work_minutes、break_minutes，重置完成次数。

性能与鲁棒性设计
- 计时分辨率：尽可能提供 1 秒粒度的计时，界面刷新率 1 Hz 即可。
- 时钟漂移处理：通过 NTP 同步时间，确保节拍对齐。
- 能耗控制：当设备进入待机模式时保持计时器唤醒能力，避免频繁唤醒。

与网络相关的注意点（简要）
- 番茄钟本身不依赖网络，网络状态仅影响天气/时钟显示的时钟信息的更新时间。
- 若网络不可用，显示离线状态但番茄钟功能仍可独立运行。
