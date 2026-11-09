# 阶段开发日志 — 阶段一：Linux 外部辅助骨架

## 1. 开发步骤

1. 确认环境：CMake 4.4.5、GCC 16.2.1、Qt 6.12.0（Widgets）、libX11 1.8.14、libXtst 1.2.6、libXext 1.3.7 均可用。
2. 先按 Windows 方案起稿，按需求切换为 Linux 后重写核心层：
   - `Memory`：`/proc/<pid>/comm` 找进程、`/proc/<pid>/mem` + `pread64` 读内存、`/proc/<pid>/maps` 取模块基址与大小、分块特征码扫描。
   - `Overlay`：Qt 无边框透明窗口 + XShape 空输入区域 + XRaiseWindow。
   - `Features`：XTest 合成鼠标位移与点击，`XQueryPointer` 读按键状态。
3. 装配 `Game → Snapshot` 数据流，渲染与输入只消费快照。
4. 编写 `Menu` 控制面板，配置整体替换（`atomic<shared_ptr<Config>>`）实现无锁跨线程生效。
5. CMake 接入 Qt6 与 X11::X11 / X11::Xext / X11::Xtst，POST_BUILD 复制默认配置。

## 2. 遇到的问题与解决

| 问题 | 现象 | 解决 |
|---|---|---|
| 缺少 `XA_CARDINAL`/`XA_WINDOW` | `Overlay.cpp`、`Features.cpp` 编译报未声明 | 补 `#include <X11/Xatom.h>` |
| `Memory::read` 重载歧义 | 模板内 `read(address, value)` 同时匹配两个重载 | 引用版改名 `readInto`，值返回版只调用 `readInto` |
| `Config` 未限定命名空间 | `Main.cpp` 用 `Config defaults;` 报错 | 改为 `wv::Config` |
| X11 宏污染 Qt | `None/Bool/Status` 等宏与 Qt 枚举冲突 | Qt 头文件先解析，X11 头文件之后 `#undef` 冲突宏 |
| `libX11.so` 开发链接名缺失的疑虑 | 手工 `ls` 找不到 `.so` 符号链接 | `find_package(X11)` 实测可正常链接，无需处理 |

## 3. 变更记录

- 新增 `CMakeLists.txt`、`config/default.json`、`docs/PLAN.md`。
- 新增 `src/core/{Memory,Math,Offsets,Config}`、`src/game/Game`、`src/render/Overlay`、`src/features/Features`、`src/ui/Menu`、`src/Main.cpp`。
- 核心层由 Windows API（`ReadProcessMemory`/`TH32CS_SNAPMODULE`/`SetWindowLongPtr`/`mouse_event`）整体替换为 Linux 等价实现。

## 4. 验证结果

- `cmake --build build --clean-first`：10 个目标全部编译，**0 warning / 0 error**，链接成功。
- 运行 `./build/cs2_lite`：窗口正常创建，控制面板渲染正确（截图确认三个分组与状态文字），4 秒内无崩溃。
- 状态栏在 CS2 未运行时正确显示「未找到进程: cs2」。
- **未验证**：游戏内透视/自瞄/扳机实际效果。原因：当前未运行 CS2，且 `config.json` 中偏移为空；需要玩家侧按当前游戏版本 dump 后填入。

## 5. 遗留风险

1. 偏移为 0 时程序不绘制，属预期；需按版本填写 `offsets` 段或提供特征码。
2. 骨骼布局默认 `CTransform`（stride 0x20，坐标 +0x10/+0x14/+0x18）未经实际验证，若错位改用 3x4 布局参数。
3. 独占全屏（非无边框窗口）下叠加层不可见，属 X11 合成器限制。
4. 读取内存需要 root 或 `kernel.yama.ptrace_scope=0`，否则状态栏提示权限不足。
5. 未做反作弊规避，也不保证线上服务器可用。

---

# 阶段开发日志 — 阶段二：偏移自动化 / 特征码集中 / 可见性检测

## 1. 开发步骤

1. 新增 `src/core/OffsetsImporter.{h,cpp}`：`QJsonDocument` 解析任意 cs2-dumper 风格 `offsets.json`，递归遍历全部对象层级，收集「键 → 数值」；键名匹配绑定表写入 `Offsets`。
2. 数值解析 `toNumber` 支持三类真实字段形态：JSON 数字、`"0x1A2B3C0"` 字符串、`{"value": N}` 对象。
3. 同类键多次出现时按作用域优先级择优：`C_CSPlayerPawn`(3) > `C_BaseEntity`/`CGameSceneNode`/`CSkeletonInstance`(2) > `C_BaseModelEntity`/`CModelState`(1) > 其他(0)；`looksLikeClass` 判定类节点（首字符 `C` 且次字符为 `_` 或大写，不含 `.` `/`）。
4. 绑定字段：`dwEntityList`、`dwLocalPlayerPawn`、`dwViewMatrix`、`m_iHealth`、`m_iTeamNum`、`m_lifeState`、`m_pGameSceneNode`、`m_vecAbsOrigin`、`m_modelState`、`m_boneArray`（别名 `m_pBones`）、`m_iIDEntIndex`、`m_bSpotted`。
5. 新增 `src/core/Patterns.h` 特征码常量表；`Game::resolveOffsets()` 改为「config `sig*` 非空优先，否则取常量表」，仍走 `resolveRip`（`hit + 7 + disp32`）。
6. `Offsets` 增加 `m_bSpotted`；`Config` 增加 `offsetsFile`、`offsetsSource`、`esp.visibilityCheck`，load 时执行导入并把结果消息存入 `offsetsSource`。
7. `Game` 采集时读 `m_bSpotted`（1 字节）写入 `PlayerInfo::visible`，偏移为 0 时不读、保持 `true`。
8. `Overlay` 按可见性渲染：敌方 `(230,162,60)`、友方 `(74,144,226)`；`visibilityCheck` 开启且不可见时 alpha 降至 100。
9. `Menu` 增加「可见性双色」复选框，底部提示改为显示实际偏移来源；`apply()` 写回 `visibilityCheck`。
10. `CMakeLists.txt` 加入 `OffsetsImporter.cpp`；`config/default.json` 补 `offsetsFile`、`m_bSpotted`、`visibilityCheck`。

## 2. 遇到的问题与解决

| 问题 | 现象 | 解决 |
|---|---|---|
| dumper 输出层级不固定 | 同一字段出现在 `client.dll`、`C_BaseEntity`、`C_CSPlayerPawn` 等多个对象 | 递归收集 + 类作用域优先级择优，取最具体类 |
| 偏移值格式不统一 | 有十进制数字、`"0x..."` 字符串、`{"value": N}` | `toNumber` 三态解析，均按 `qulonglong` 截断 |
| 特征码分散在 Game.cpp | 无法统一替换与版本管理 | 抽出 `Patterns.h`，config 仍可覆盖，保留两处入口 |
| 可见性字段宽度 | `m_bSpotted` 为 bool | 按 1 字节读取后判非零 |

## 3. 变更记录

- 新增：`src/core/Patterns.h`、`src/core/OffsetsImporter.h`、`src/core/OffsetsImporter.cpp`。
- 修改：`src/core/Offsets.h`、`src/core/Config.h`、`src/core/Config.cpp`、`src/game/Game.h`、`src/game/Game.cpp`、`src/render/Overlay.cpp`、`src/ui/Menu.h`、`src/ui/Menu.cpp`、`CMakeLists.txt`、`config/default.json`、`docs/PLAN.md`。

## 4. 验证结果

- 构建：`cmake --build build --clean-first -j8` → 11 个目标全部成功，**0 warning / 0 error**。
- 导入探针（独立编译 `Config.cpp` + `OffsetsImporter.cpp`，链接 Qt6Core，读取样例 `offsets.json`）：
  - 输出 `loaded=1 applied=12`，消息为「已从 /tmp/cs2_offsets_sample.json 导入 12 项偏移」。
  - `dwEntityList=0x1A2B3C0`、`dwLocalPlayerPawn=0x1A2B3D0`、`dwViewMatrix=0x1A2B3E0`、`m_iHealth=0x344`、`m_iTeamNum=0x3C3`、`m_lifeState=0x338`、`m_bSpotted=0x93D`、`m_iIDEntIndex=0x13A0`、`m_pGameSceneNode=0x328`、`m_vecAbsOrigin=0x80`、`m_modelState=0x160`、`m_boneArray=0x1A0`，与样例逐一对应。
- 运行：`./build/cs2_lite` 稳定运行；窗口截图确认「可见性双色」默认勾选、底部显示「偏移来源: 已从 /tmp/cs2_offsets_sample.json 导入 12 项偏移」，状态「未找到进程: cs2」。
- **未验证**：游戏内可见性双色实际效果、改动后透视/自瞄/扳机行为。原因：当前未运行 CS2，且样例偏移非当前版本真实值。

## 5. 遗留风险

1. `Patterns.h` 常量当前为空字符串，需按版本填入；留空时只有 `offsetsFile` 或 config 静态偏移可用。
2. 导入采用键名精确匹配，若 dumper 改名（如 `m_bSpottedByMask`）需同步更新绑定表。
3. `m_bSpotted` 语义为「被本地玩家观测」，与光栅可见性不完全等价；需要更严格判定时需射线检测，当前不做。
4. 骨骼布局仍未实测，沿用阶段一风险项。

---

# 阶段开发日志 — 阶段三：实机偏移获取（Linux 生成器）

## 1. 背景与实测条件

- 游戏进程 `cs2`（pid 88026），`ClientVersion=2000942`、`PatchVersion=1.41.9.6`、`VersionDate=Nov 04 2026`。
- 模块 `libclient.so` 基址 `0x7FAC2BA00000`，大小 `0x46C8000`。
- 非 root 用户可直接 `open("/proc/<pid>/mem", O_RDONLY)` 并 pread，无需 sudo（Yama 未拦截）。
- a2x/cs2-dumper 主干数据为 2026-10-30，与本机版本不符：按其 `dwEntityList=41020168` 读出的 pawn 为机器码 `0xC920408B48078B48`；其 `dwViewMatrix` 特征码在本机 `.text` 命中数为 0。cs2-dumper 依赖 memflow connector，Linux 无可用插件（`-c native` 报 plugin not found），无法直接运行。

## 2. 开发步骤

1. 编写 `tools/cs2_linux_offsets.py`（仅读）：从 `/proc/maps` 取 `libclient.so` 段范围，用 `os.pread` 分块读取可执行段。
2. 实现 Linux 侧特征码语义（`??` 通配、`${}` rip 相对、`${'}` 解析目标），定位 `dwEntityList`、`dwLocalPlayerPawn`、`dwViewMatrix` 三个模块 RVA。
3. 字段偏移：经 `libschemasystem.so` 的 `SchemaSystem_001` 接口（实例 rva `0xba080`）遍历运行时 SchemaSystem，按 `libclient.so` scope 取 `C_CSPlayerPawn`/`C_BaseEntity`/`CSkeletonInstance`/`CGameSceneNode` 的 netfield 偏移。
4. 生成 `config/offsets.json` 并回读自检；`config/default.json` 设 `offsetsFile=config/offsets.json`。

## 3. 实测结果（游戏运行中）

| 键 | 值 | 证据 |
|---|---|---|
| dwEntityList | `0x4B9A750` | 特征码唯一命中 rva `0x2F3AD04`；读出 list=`0x51A76C79800`（堆指针） |
| dwLocalPlayerPawn | `0x49454B8` | 读出 pawn=`0x51ADF146000`（堆指针） |
| dwViewMatrix | `0x494CB00` | 64 字节读成功，float 序列 `0.750 0.000 0.000 -912.000 …`，两次采样不同 |
| m_iHealth | 1212 (`0x4BC`) | pawn 读取为 100 |
| m_iTeamNum | 1367 (`0x557`) | 读取为 2 |
| m_lifeState | 1220 (`0x4C4`) | 按 1 字节读为 0（按 4 字节读为 256，故必须按字节） |
| m_pGameSceneNode | 1184 (`0x4A0`) | 读出 `0x51AB8BAE880` |
| m_vecAbsOrigin | 200 (`0xC8`) | 世界坐标 `1216.00 -16.00 -163.97` |
| m_modelState | 320 (`0x140`) | 由 SchemaSystem 取得 |
| m_iIDEntIndex | 17796 (`0x4584`) | 由 SchemaSystem 取得 |
| m_bSpotted | 11552 (`0x2D20`) | 按 1 字节读取，当前为 0 |
| entityChunkStride | **112 (`0x70`)** | 同一列表下 0x70 命中 2 个健康实体，0x78 命中 0 个 |

## 4. 变更记录

- 新增：`tools/cs2_linux_offsets.py`、`config/offsets.json`。
- 修改：`config/default.json`（`offsetsFile`、`entityChunkStride=112`）、`src/game/Game.cpp`（`m_modelState`/`m_boneArray` 缺失不再阻断连接状态，仅提示骨骼不可用）、`docs/PLAN.md`。

## 5. 验证结果

- 构建：`cmake --build build -j8` 成功，0 error。
- 导入：控制面板显示「偏移来源: 已从 config/offsets.json 导入 11 项偏移」。
- 数据：独立探针（复用 `Memory`/`Math`）读取 pawn、health=100、team=2、origin、view matrix 均正常。
- **未验证**：游戏内方框叠加画面。验证过程中游戏进程退出，叠加层截图全黑（无绘制内容），无法据此判断。需游戏重新进入对局后复测。

## 6. 遗留风险

1. 游戏每次更新后必须重跑生成器，否则全部偏移失效（表现为读到机器码或指针为 0）。
2. `m_boneArray` 未被 SchemaSystem 解析到（当前版本该字段未暴露），骨骼连线不可用，默认关闭。
3. 生成器依赖 `libschemasystem.so` 的接口实例 rva（`0xba080`），版本更新后该定位方式可能失效，需要重新定位。

---

# 阶段开发日志 — 阶段四：ESP 全目标丢失的根因修复

## 1. 现象与证据

- 现象：进程状态栏显示「已连接 pid=…（骨骼偏移缺失，连线不可用）」，透视全部勾选，但游戏内无任何方框/血条/距离。
- 排查一（数据层）：独立探针复用 `Config`/`Memory`/`Math` 遍历实体列表，同一时刻 `candidates=13 drawn=7`，`worldToScreen` 对 7 个目标返回有效屏幕坐标 —— 说明偏移、实体遍历、视图矩阵均正确。
- 排查二（呈现层）：叠加层窗口 `0x86000f` 1920x1080 位于堆叠顶端、几何与游戏窗口一致；同实例的自瞄 FOV 圈在屏幕上可见 —— 说明窗口创建、合成、绘制调用链全部正常。
- 排查三（差异定位）：探针按「原点 + 62」构造头部，而 `Game::update` 走 `bonePosition`。直接读内存验证：
  `m_boneArray = 0` 时 `scene + 0x140`（`m_modelState`）内存放的是共享库指针 `0x7f1580ce9ca8`，
  以其为骨骼数组基址读出的是量级 `1e37` 的垃圾浮点，`worldToScreen` 全部失败 → 每个目标都被 `continue` 丢弃。

## 2. 修复

`src/game/Game.cpp`：

- 新增 `bonesReady = m_modelState != 0 && m_boneArray != 0`；未就绪时不调用 `bonePosition`，头部按 `origin + 62` 退化。
- 骨骼连线链同样以 `bonesReady` 为前置条件，避免用垃圾坐标生成无效线段。

## 3. 验证结果

- 构建：`cmake --build build -j8` 成功，0 error。
- 逻辑验证：垃圾骨骼读数（`1e37` 量级）已确认会使 `worldToScreen` 失败；改为退化头部后，探针给出的 7 个目标均由 `worldToScreen` 成功投影。
- **未验证**：游戏内实际方框画面 —— 修复后游戏进程已退出，无法复测。

## 4. 桌面环境变更（可回滚）

- `~/.config/picom/picom.conf` 追加 `unredir-if-possible = false;`（备份 `picom.conf.bak-*`）。
  目的：全屏游戏时保持合成，避免叠加层被直通扫描输出覆盖。

## 5. 遗留风险

1. 头部按固定 62 units 退化，与真实身高存在小幅偏差，骨骼可用后应恢复骨骼头部。
2. `m_boneArray` 当前版本仍未取得，骨骼连线长期不可用。
3. 游戏进程为 root 启动时，普通用户无法结束该实例，需 `sudo pkill -x cs2_lite` 后重起。

---

# 阶段开发日志 — 阶段五：卡顿与不出框的根因修复

## 1. 现象与证据

- 现象：修复骨骼退化后仍然「没有功能」，且明显延迟；`cs2_lite` 进程常驻约 100% CPU，同时存在两个实例（root 与普通用户各一个），CPU 合计约 200%。
- 取证一（tick 分段计时，`WV_DEBUG=1`）：
  `ensureAttached 29ms` → `findGameWindow 165ms` → `game_.update()` **超过 3000ms 未返回**（打印永远停在 `[t] geom`）。
- 取证二（update 内部计时）：实体页循环本身仅 `0.49~0.67ms`，`pages=4/4 bulk=4`，说明整页读取已命中，遍历不是瓶颈。
- 取证三（差值定位）：`update()` 开头调用 `clientBase()`，而 `clientBase()` 走 `Memory::moduleBase()`；
  `moduleBase()` 每次调用都会打开并完整解析一遍 `/proc/<pid>/maps`（CS2 有数千行映射）。
  改动前 `Game::entity(index)` 内部再次调用 `clientBase()`，即**每个实体一次 maps 解析**，2048 实体/帧 → 单帧 3 秒以上。

## 2. 修复

`src/game/Game.cpp` / `src/game/Game.h`：

- 删除 `Game::entity(int)`；实体列表改为按 chunk 页遍历：每页只取一次页指针，整页 `readBytes` 一次读入（512 × 112 = 57344 字节），槽位指针从缓冲区 `memcpy`；
  `readBytes` 失败时回退为逐槽 `pread`。
- 新增成员 `clientBase_`，在 `resolveOffsets()` 中取一次模块基址，`clientBase()` 直接返回缓存值 —— 彻底消除逐实体解析 `/proc/<pid>/maps` 的开销。

`src/render/Overlay.cpp` / `Overlay.h`：

- 采样间隔 3ms → **5ms**（自瞄仍为 200Hz 的新鲜快照）。
- 重绘与采样解耦：采样每 5ms，重绘限速 **33ms（30fps）**。原先 333Hz 重绘等于每秒把整张 1920x1080 半透明表面提交 333 次，是 CPU 与延迟的主要来源。
- 窗口几何探测降频：`XGetWindowAttributes` / `XTranslateCoordinates` 改为每 10 tick（约 50ms）一次，其余 tick 复用 `windowWidth_/windowHeight_` 缓存；这两者是同步 X 往返请求，原先每帧都在付钱。
- 新增 `WV_DEBUG=1` 环境变量诊断：每 60 tick 输出 `窗口尺寸 / valid / players / drawn`，不设置时零开销。

`config/default.json` + 运行时 `build/config.json`：

- `esp.visibilityCheck` 默认改为 `false`。本机版本 `m_bSpotted` 恒为 0，开启后所有目标 alpha 降到 100，方框在明亮场景里几乎不可见，正是「看起来完全没有透视」的第二个原因。

## 3. 验证结果

- 构建：`cmake --build build -j8` 成功，0 error。
- 性能：单次 `tick` 由 **>3000ms** 降到 **0.8ms**；同一份二进制的 CPU 占用 100% → **约 25%**（单实例）。
- 画面：截图确认队友蓝色方框、血条、距离文字（`5m`/`7m`/`8m`）同时出现在画面内，颜色与几何随视角正确跟随。
- 数据：诊断行 `[tick] 1920x1080 valid=1 players=10 drawn=10`，绘制数与采集数一致。

## 4. 遗留风险

1. `m_bSpotted` 在本版本恒为 0，可见性双色不可用（已默认关闭）。
2. `m_boneArray` 仍未取得，骨骼连线保持关闭；头部按 `origin + 62` 退化。
3. 整页读取假设 chunk 在 57344 字节内连续；若某版本分页布局变化，会回退为逐槽读取（功能不受影响，仅性能下降）。
4. 同时运行多个实例会叠加 CPU 与重复叠加层，需保证只保留一个进程。

---

# 阶段开发日志 — 阶段六：骨骼偏移实测落地 / 自瞄无条件 / 叠加层置顶

## 1. 骨骼偏移取证（游戏运行中，pid 293003）

用独立只读探针（`/proc/<pid>/mem` + `pread`，非 root）在 `CGameSceneNode` 附近扫描指针并验证坐标语义：

- 骨骼数组指针位于 `scene + 0x1c0`：`m_modelState = 448`，`m_boneArray = 0`（即直接取该位置的指针）。
- 单条骨骼步长 `0x20`，位置分量在 `+0x0 / +0x4 / +0x8`，四元数在 `+0x10`。
- `bone[0]` 与 `m_vecAbsOrigin` 基本重合（差值 0.0~6.8），`bone[6]` 比原点高约 **54~60** 单位（头部）。
- 对照：`scene + 0x140` 读出的是 libclient 数据段指针（所有玩家相同），不是骨骼数组，正是阶段四垃圾坐标的来源。

交叉验证 16 个存活玩家：`checked=16 ok=16`，全部满足 `|bone[0].z - origin.z| < 8` 且 `20 < head.z - origin.z < 100`。

## 2. 代码与配置变更

- `src/game/Game.cpp`：`bonesReady` 由 `m_modelState != 0 && m_boneArray != 0` 改为仅判断 `m_modelState != 0`（数组指针直接读自 `scene + m_modelState`，二次偏移为 0 时仍成立）。
- `config/offsets.json`：`m_modelState` 320 → **448**，新增 `m_boneArray: 0`。
- `config/default.json`：`m_modelState` 448、`m_boneArray` 0、`bonePosX/Y/Z` 16/20/24 → **0/4/8**；`aim.enabled=true`、`aim.key=0`。
- `build/config.json`（运行时真身）：同步上述骨骼键与 `aim.key=0`。
- `src/features/Features.cpp` 无需改动：既有逻辑 `config->aim.key <= 0 || buttonDown(...)` 已支持无条件自瞄；`key=0` 即每 tick 都执行。
- `src/ui/Menu.cpp`：自瞄分组标题 `自瞄（按住鼠标侧键）` → `自瞄（无条件）`。
- `src/render/Overlay.cpp`：`kRaiseEveryTicks` 30 → **6**。游戏窗口在获得焦点/进入全屏时会重新抬升自身，150ms 一次的置顶会被压回去，表现为「透视消失」；改为约 30ms 一次。

## 3. 验证结果

- 构建：`cmake --build build -j8` 成功，0 error。
- 数据：`[tick] 1920x1080 valid=1 players=15 drawn=15`，绘制数与采集数一致。
- 画面：全屏截图裁剪确认同一目标同时出现**绿色血条 + 橙色方框边线 + 距离文字（43m/49m）+ 白色骨骼连线**，即骨骼数组与连线链路生效。
- 性能：单实例 CPU 约 **33%**（采样 5ms / 重绘 33ms / 置顶 30ms）。
- 自瞄：`aim.key=0` 条件下无需任何按键，`Features::aimStep` 每 tick 按 FOV 内最近目标输出相对位移。

## 4. 遗留风险

1. 骨骼索引 6/5/4/0 为本版本实测对应头/颈/胸/骨盆；版本更新后需重新核对索引语义。
2. 骨骼数组固定取 `scene + 0x1c0`，与 `m_modelState` 强绑定；游戏更新需重跑探针确认。
3. 无条件自瞄会持续跟随准星附近最近目标，移动鼠标时可能对抗玩家输入；需要时可把 `aim.key` 改回按键值（1~5）。
4. 叠加层置顶靠 30ms 一次的 `XRaiseWindow`，若游戏使用独占全屏（非无边框窗口）仍可能压不住，需改为无边框全屏。

