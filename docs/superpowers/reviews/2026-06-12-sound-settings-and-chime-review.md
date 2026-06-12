# 声音设置与整点报时 — 实现审查报告

**审查日期**: 2026-06-12
**对应规范**: `docs/superpowers/specs/2026-06-12-sound-settings-and-chime-design.md`
**对应计划**: `docs/superpowers/plans/2026-06-12-sound-settings-and-chime.md`
**审查范围**: Task 1–10 全部代码实现
**审查方法**: 静态代码审查 + Grep 调用点核查 + 边界条件推演

## 综合结论

**整体评价**: 实现质量高，与 spec/plan 高度对齐。所有核心功能（7 分类双层闸门、整点报时分组、半点单响、跨午夜静默、绝对 minute_id 去重）均按设计实现，无功能性 bug。代码简洁，无重复抽取的必要。与 claude-code-buddy-bridge 项目零影响。

**是否阻塞硬件验证**: **不阻塞**。可进入 Task 11 硬件验证。

## Task 完成清单

| Task | Commit | 状态 |
|------|--------|------|
| 1: NVS 键扩展 | `2a2fae2` | ✅ |
| 2: i18n 字符串 | `78a79cb` | ✅ |
| 3+4: sound_service 重写 | `2595615` | ✅ |
| 5: chime_service 新建 | `f733b31` | ✅ |
| 6: 声音设置页 | `4f8a93b` | ✅ |
| 7: ui_manager 注册 | `1af5fe7` | ✅ |
| 8: 设置主页加 Sound 入口 | `58f3d31` | ✅ |
| 9: 系统设置页移除 Sound | `53ed08a` | ✅ |
| 10: main.c 集成 | `bb98827` | ✅ |
| 11: 硬件验证 | — | ⏳ pending |
| 日志文案修复（审查发现） | `b7b06dc` | ✅ |

## 检查 1: 功能符合预期

| 验证点 | 结果 | 位置 |
|---|---|---|
| 7 分类枚举 (KEY/UI/NET/POMO/BUDDY/HOUR/HALF) | ✅ | `sound_service.h:29-38` |
| cat_map 覆盖 19 个 sound_id | ✅ | `sound_service.c:125-145` |
| cat_keys/cat_defaults 顺序对齐 | ✅ | `sound_service.c:148-154` 默认 `[T,T,T,T,T,F,F]` |
| 总开关 + 分类开关双层闸门 | ✅ | `sound_service_play` L187/L190；`play_hour_chime` L226/L227 |
| 整点按 hour12 分组鸣响 | ✅ | `play_hour_chime` L225-244；`chime_service.c:37-38` |
| 半点单响低音 (A5) | ✅ | `mel_half = {NOTE_A5, 200}` |
| 静默跨午夜 22→7 | ✅ | `is_quiet_hour` 三元 `(s<e) ? (h>=s&&h<e) : (h>=s\|\|h<e)` |
| chime_tick 绝对 minute_id 去重 | ✅ | `chime_service.c:29-31` |
| NVS 键默认值 | ✅ | `quiet_start=22`, `quiet_end=7`, 旧 `sound` 复用 |
| 9 个新 NVS 键长度 ≤ 15 字符 | ✅ | 最长 `quiet_st/quiet_ed` = 8 字符 |

**spec 与实现的合理偏差**：spec 提到 `STR_FMT_HOUR_VAL` 字符串，plan 优化为复用既有 `STR_FMT_HOUR`，避免新增冗余字符串。属于合理 YAGNI 决策。

## 检查 2: 代码简洁性

**未发现需修复的简洁性问题**。观察：

1. **NAV/ADJUST 双实例**: `ui_screen_settings_sound.c` 与 `ui_screen_settings_system.c` 回调结构相似，但 spec/plan 明确"2 个实例不算重复"，YAGNI 原则下不抽取。✅
2. **`ui_screen_settings.c:149` 日志文案**: 旧版本写 "6 categories"，4→5 改动后未更新。**已在 `b7b06dc` 修复为 "5 categories"。** ✅
3. **include 一致性**: 新文件 include 干净，`ui_screen_settings_system.c` 已正确移除 `sound_service.h`。✅
4. **TAG 命名一致**: `SOUND`/`CHIME`/`UI_SETTINGS_SOUND` 符合项目惯例。✅
5. **`SOUND_SYNC_START` / `SOUND_WIFI_CONNECT` 死代码**（非本次引入）: 规划于 2026-05-03 buzzer-sound-scheme plan 但从未接线。建议作为独立 backlog 跟进，不在本次范围内处理。

## 检查 3: 与 claude-code-buddy-bridge 项目交互

**风险评估: 低。零影响。**

1. **tcp_service 调用点未破坏**: grep `tcp_service_` 找到 8 个文件，但 `tcp_service.c` 自身不含任何 `sound_/buzzer_` 调用。bridge 协议处理路径完全无声音依赖。

2. **无新增声音调用进入 bridge 路径**: 通过 `on_tcp_request` 回调（`main.c:217-239`），buddy 模块在 tcp_task 上下文调用 `sound_service_play(SOUND_BUDDY_ATTENTION)`（`buddy.c:105`）。这是已存在行为，本次重构未新增任何调用点。

3. **报时旋律不阻塞 UI 任务**: `buzzer_play_melody` 非阻塞（`buzzer.c:126-155` 用 `esp_timer` 异步播放）。`sound_service_play_hour_chime` 只做：检查开关 + 拼装栈上 buf[16] + 调一次 `buzzer_play_melody`（< 1ms），立即返回。

4. **线程安全（潜在风险，非本次引入）**:
   - `buzzer.c` 的 `play_notes/play_count/play_index/playing` 是非原子全局变量，无 mutex 保护。
   - 多 task 调用 `buzzer_play_melody` 的上下文：input_task、service_task、tcp_task、SNTP task、ui_update_task。
   - 若两个 task 同时调用（如 tcp_task 触发 ATTENTION 同时 ui_update_task 触发 chime），可能存在 timer 句柄悬空。
   - **这是 2026-05-03 buzzer 重构遗留问题，本次声音设置功能未引入新风险**（chime 1 秒一次的频率远低于已有调用）。建议作为独立 issue 跟进。

5. **`storage_save_int` 线程安全**: NVS API 内部线程安全，每次 open/commit/close 是独立事务。

## 检查 4: 边界条件

| 边界 | 结果 | 说明 |
|---|---|---|
| `buf[16]` 在 hour12=12 时 | ✅ | 4 组×3 音 + 3 组间 rest = 15，余量 1 |
| `cat_map[SOUND_COUNT]` 覆盖 | ✅ | 19 个枚举全部映射，无缺漏 |
| `quiet_start == quiet_end` 语义 | ✅ | L256 `if (s==e) return false` 明确"无静默" |
| 跨午夜只用 `tm_hour` + `\|\|` | ✅ | 22→7: 23→T, 0-6→T, 7→F（end 不含）, 21→F |
| ADJUST 模式 quiet_vals wraparound | ✅ | `(+step)%24`、`(-step+24)%24` 正确处理 0↔23 |
| `hour12` 边界 | ✅ | `play_hour_chime` L228 显式 `if (hour12<1 \|\| hour12>12) return` |
| `chime_service_init` 不立即响 | ✅ | 启动时记录当前 minute_id |
| 漏秒容错 | ✅ | 不依赖 `tm_sec==0`，min==0/30 任何一秒进入都能响 |
| `vTaskDelay(500) + chime 1000ms` 闸门 | ✅ | 即使 UI 任务阻塞几秒，下次唤醒 mid 仍是新值 |

## 已知非阻塞问题

### Minor — 已修复
- `ui_screen_settings.c:149` 日志文案错误（`b7b06dc` 已修复）

### Minor — 可选改进（未处理）
- `sound_service.c:188, 212, 218` 的 `id < 0` / `cat < 0` 判断对无符号枚举永远为 false。保持与项目其他位置一致的防御性写法，无功能性影响。

### 已知风险 — 非本次引入
- `buzzer.c` 多 task 并发调用 `buzzer_play_melody` 无 mutex 保护，可能 timer 句柄悬空。属于 2026-05-03 重构遗留，应作为独立 issue 跟进。
- `SOUND_SYNC_START` / `SOUND_WIFI_CONNECT` 是规划但从未接线的死代码。建议清理或接线。

## 后续行动项

1. **Task 11 硬件验证**（待硬件就绪）— 按 plan 的 14 个验证步骤手动测试
2. **独立 issue: buzzer 并发安全**（非本次范围）— 评估是否需要加 mutex 或迁移到单 task 串行化
3. **独立 issue: 死代码清理**（非本次范围）— 决定 `SOUND_SYNC_START` / `SOUND_WIFI_CONNECT` 是清理还是接线

## 引用文件

- 规范: `docs/superpowers/specs/2026-06-12-sound-settings-and-chime-design.md`
- 计划: `docs/superpowers/plans/2026-06-12-sound-settings-and-chime.md`
- 实现:
  - `main/service/sound_service.c/h`
  - `main/service/chime_service.c/h`
  - `main/ui/ui_screen_settings_sound.c/h`
  - `main/ui/ui_screen_settings.c`
  - `main/ui/ui_screen_settings_system.c`
  - `main/ui/ui_manager.c/h`
  - `main/ui/i18n.c/h`
  - `main/service/storage_service.h`
  - `main/main.c`
  - `main/CMakeLists.txt`
