# WiFi 多网络存储设计

## 目标

支持保存最多 10 个 WiFi 网络及对应密码，开机自动扫描匹配信号最强的已保存网络连接，支持手动删除已保存网络。

## 约束

- ESP32-C3，NVS 分区 24KB（当前用量约 500 字节，空间充足）
- SSID 最长 32 字节，密码最长 64 字节
- 最多保存 10 个网络配置

## 数据模型

NVS namespace `wifi` 中存储：

| 键 | 类型 | 说明 |
|---|---|---|
| `count` | i32 | 已保存网络数量 (0-10) |
| `ssid_0` ~ `ssid_9` | string | SSID |
| `pwd_0` ~ `pwd_9` | string | 密码 |

索引 0 = 最旧，索引 count-1 = 最新。

### 操作规则

- **添加**：追加到末尾（`ssid_{count}`），count++。若 count >= 10，先删掉索引 0 并全部前移，再追加。
- **已存在**：SSID 匹配时移到最新位置（删掉旧位置，后面的前移，追加到末尾）。
- **删除**：删掉指定索引，后面的前移，count--。

## 存储层 API（storage_service）

```c
int  storage_get_wifi_profile_count(void);
void storage_add_wifi_profile(const char *ssid, const char *password);
bool storage_load_wifi_profile(int index, char *ssid, size_t ssid_len, char *password, size_t pwd_len);
void storage_delete_wifi_profile(int index);
```

### 存储占用估算

每个网络配置约 100 字节（SSID 32 + 密码 64 + NVS 开销），10 个约 1KB。当前 NVS 分区 24KB，远够用。

### 向后兼容

启动时检测：若存在旧的 `ssid`/`password` 键但 `count` 不存在，自动迁移为 profile 0 并设置 count=1，然后删掉旧键。

## WiFi 服务改动（wifi_service）

### 开机自动连接流程

```
wifi_service_init()
  → 启动 WiFi STA
  → 若有已保存网络，触发扫描
  → 扫描完成回调中：
      1. 遍历扫描结果，匹配已保存 SSID
      2. 按信号强度排序匹配结果
      3. 连接信号最强的匹配网络
      4. 无匹配则等待用户手动操作
```

### 断线重连改动

断线后不再只重连同一 SSID，而是重新扫描匹配信号最强的已保存网络。重连策略保持指数退避（2s → 60s）。

### 连接成功保存

连接成功获取 IP 后，调用 `storage_add_wifi_profile()` 保存/更新配置。

### 新增 API

```c
int         wifi_service_get_saved_count(void);
const char* wifi_service_get_saved_ssid(int index);
void        wifi_service_delete_saved(int index);
bool        wifi_service_is_saved(const char *ssid);  // 检查扫描结果中的SSID是否已保存
```

## UI 层改动

### 新增界面：已保存网络列表（ui_screen_wifi_saved.c）

复用 `ui_list` 组件，显示内容：

```
WiFi Networks
─────────────
> Scan for new...    ← 固定第一项
  MyHome *
  Office
  Guest *
─────────────
SET:select|Press:back
```

#### 输入操作

| 操作 | 行为 |
|------|------|
| 编码器旋转 | 上下导航列表 |
| SET + "Scan..." | 进入 WiFi 扫描列表 |
| 第一次 SET + 已保存网络 | 提示 "SET:confirm del"，等待确认 |
| 第二次 SET + 已保存网络 | 执行删除，刷新列表 |
| 编码器旋转（确认状态下） | 取消确认，恢复正常导航 |
| 编码器短按 | 返回设置界面 |

#### 删除确认流程

1. SET 按选中网络 → 提示变为 "SET:confirm del"，进入确认状态
2. 再次 SET → 执行删除，刷新列表，退出确认状态
3. 编码器旋转离开该项 → 自动取消确认状态

### 扫描列表改动（ui_screen_wifi.c）

- 已保存的 SSID 前显示 `*` 标记（通过 `wifi_service_is_saved()` 判断）
- 连接成功后自动调用 `storage_add_wifi_profile()` 保存

### 设置界面改动

WiFi 入口跳转目标从扫描列表改为已保存网络列表：

```
SETTINGS → WiFi(已保存网络列表) → Scan(扫描列表) → PASSWORD_INPUT
```

### 导航更新

新增 `UI_SCREEN_WIFI_SAVED`，位于设置和扫描列表之间：

```
SETTINGS → WIFI_SAVED → WIFI_LIST → PASSWORD_INPUT
               ↕
        SET:连接/扫描/删除(两步确认)
```

## 不做的事

- 不存储 WiFi 密码加密（NVS 本身有 flash 加密选项，设备级别安全）
- 不支持隐藏网络
- 不支持企业级认证（WPA2-Enterprise）
- 不做网络优先级手动排序（始终按信号强度自动选择）
