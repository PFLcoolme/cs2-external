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

---

# 阶段开发日志 — 阶段七：自瞄改为「开镜 + 开火」双条件触发

## 1. 需求与设计

无条件自瞄会持续跟随准星附近最近目标，与玩家自己的鼠标输入对抗，且在任何时刻都生效，实际体验不可控。改为**仅在开镜与开火同时按住时**执行自瞄步进：

```text
aim.enabled && scopeHeld && fireHeld → aimStep()
scopeKey = 3（鼠标右键，开镜）；0 = 不要求开镜
fireKey  = 1（鼠标左键，开火）；0 = 不要求开火
```

按键状态沿用既有 `XQueryPointer` + `Button1Mask << (key - 1)` 判定，不引入新的 X11 调用。

## 2. 变更记录

| 文件 | 变更 |
|---|---|
| `src/core/Config.h` | `AimbotConfig.key` 拆为 `scopeKey{3}` / `fireKey{1}` |
| `src/core/Config.cpp` | 读写的 `aim.key` 改为 `scopeKey` / `fireKey` |
| `src/features/Features.cpp` | 自瞄条件改为 `scopeHeld && fireHeld` 双键同时按住 |
| `src/ui/Menu.cpp` | 分组标题 `自瞄（无条件）` → `自瞄（开镜 + 开火）` |
| `config/default.json` | `aim` 段写入 `scopeKey: 3`、`fireKey: 1` |
| `build/config.json` | 同步 `scopeKey: 3`、`fireKey: 1` |
| `README.md` | 特性表、`aim` 字段表、输入合成说明同步 |

## 3. 验证结果

- 构建：`cmake --build build -j8` 成功，0 warning / 0 error。
- 配置：`config/default.json` 与 `build/config.json` 的 `aim` 段均含 `scopeKey`/`fireKey`，旧 `key` 字段已移除。
- 字段持久化：`Menu::apply` 以 `Config next = *current` 为基底，仅覆盖面板暴露字段，新键不会在保存时丢失。
- **未验证**：游戏内「按住右键 + 左键」时的实际跟随效果 —— 验证时 CS2 未运行。

## 4. 遗留风险

1. 双键判定依赖全局指针按键掩码，若游戏捕获鼠标（相对模式）不更新掩码，需改用 XInput2 raw events。
2. 开火瞬间自瞄仍在收敛，首帧偏移可能不足；需要更强吸附时可调小 `smooth`。
3. 若玩家把开镜绑到侧键，需把 `scopeKey` 改为对应键码（`1~5`），或置 `0` 取消开镜要求。

---

# 阶段开发日志 — 阶段八：自瞄「按键无效」的取证与焦点判定修复

## 1. 现象与取证

现象：自瞄改为「开镜 + 开火」后，玩家按住右键 + 左键仍无任何拉枪。

取证一（按键掩码）：独立探针在 CS2 运行中调用 `XTestFakeButtonEvent` 按下/释放 btn1、btn3，并在每步用 `XQueryPointer` 读掩码 —— `btn1 down mask=0x100`、`btn3 down mask=0x400`、释放后归零，且 `XGrabPointer` 返回 `Success`（游戏未持有指针抓取）。结论：`buttonDown` 所用掩码在当前环境可靠。

取证二（注入链路）：以 `/proc/<pid>/mem` 读视图矩阵，发送 `XTestFakeRelativeMotionEvent(40,0)` 后 `view[0]` 变化 `>1e-4`。结论：CS2 会消费 XTest 相对位移，自瞄注入方式有效。

取证三（门控日志）：新增 `WV_DEBUG=1` 诊断后实跑，日志出现
`[skip] cfg=1 window=121635231 attached=1 active=0 pid=27797` —— 说明 `Features` 线程在 `gameActive()` 处整段跳过，自瞄与扳机代码一次都没执行。根因：`gameActive()` 只读 `_NET_ACTIVE_WINDOW`，全屏/无窗口管理器接管或属性陈旧时该判定为假。
随后 `xdotool windowactivate <游戏窗口>` 后日志变为 `[gate] ... scope=1 fire=1`，证明按键门控本身正确。

取证四（目标距离）：门控为真时日志为 `[aim] no target in fov=220 nearest=837.6` —— 当时最近敌人头部距屏幕中心 837px，超出配置的 220px FOV，属预期行为而非缺陷。

## 2. 修复

`src/features/Features.cpp`：

- `gameActive()` 改为两级判定：先用 `XGetInputFocus()` 取焦点窗口并沿 `XQueryTree` 父链向上比对（命中游戏窗口或 `_NET_WM_PID` 即视为激活），仅在焦点为 `None` 时回退到 `_NET_ACTIVE_WINDOW`；
- 焦点为 `PointerRoot`（键盘焦点跟随指针）时取指针下的子窗口作为起点；
- 签名改为 `gameActive(display, gameWindow, pid)`，调用处同步。

新增诊断（默认零开销，`WV_DEBUG=1` 才输出）：

- `[skip]` 输出 `cfg/window/attached/active/pid`，用于判定哪一项拦住了循环；
- `[gate]` 输出 `players/scope/fire/fov`；
- `[aim]` 输出 `no target in fov .. nearest ..` 或 `target idx/err/move`。

## 3. 验证结果

- 构建：`cmake --build build -j8` 成功，0 warning / 0 error。
- 门控：焦点切到游戏后日志稳定输出 `scope=1 fire=1`；焦点在控制面板时按设计跳过（`active=0`）。
- 注入：40px 相对位移使视图矩阵首元素变化 0.1179，确认游戏消费。
- 端到端：隔离目录 `/tmp/wvtest` 以 `fovPixels=3000` 运行，按住双键期间视图矩阵首元素由 0.1798 变为 0.2977（同时段未使用其它位移输入）。
- **未验证**：原始 220px FOV 下、玩家真实瞄准敌人时的拉枪手感。

## 4. 遗留风险

1. 目标不在 FOV 圆内时自瞄不动作，属设计；需要更宽覆盖时在面板调大「范围(像素)」。
2. 叠加层探测到窗口不可见（切窗/全屏切换瞬间）会把 `gameWindow` 置 0，该瞬间自瞄随之中断，恢复后自动继续。
3. 窗口管理器是否更新 `_NET_ACTIVE_WINDOW` 已不影响判定，但焦点链最多上溯 16 层。

# 阶段开发日志 — 阶段九：诊断落盘与物理键位取证

## 1. 现象

玩家反馈「左键没效果、右键有效果」。阶段八的诊断只在 `WV_DEBUG=1` 时输出到 stderr，以 `sudo` 启动或关闭终端后证据即丢失；独立探针进程在命令结束后被回收，日志仅剩一行 `mask=0x0000`，无法与玩家按键时段对齐。

## 2. 改动

`src/features/Features.cpp`：

- 新增 `traceFile()` / `traceToStderr()` / `trace(fmt, ...)`：诊断**始终**追加写入 `/tmp/wv_lite.log`（每次写后 `fflush`），`WV_DEBUG=1` 时同时写 stderr；
- 新增 `pointerMask(display)`：封装 `XQueryPointer` 的 `state` 读取；
- 循环最前记录掩码变化：`mask != lastMask_` 时输出 `[btn] mask=0x%04x btn1..btn5`，位于所有门控之前，即使 `gameActive()` 判定为假也能拿到真实键位；
- `[skip]` / `[gate]` / `[aim]` 改为无条件节流输出（门控每 100 ms、自瞄每 200 次）。

`src/features/Features.h`：新增成员 `unsigned int lastMask_{0}`。

`README.md` 6.3：补充 `/tmp/wv_lite.log` 与 `[btn]/[gate]/[aim]/[skip]` 判读表。

## 3. 验证结果

构建：`cmake --build build -j8` 成功，0 warning / 0 error；`strings build/cs2_lite` 确认 `[btn]` 模板已进二进制。

实跑 `/tmp/wv_lite.log`（CS2 pid=27797，窗口 121635231）：

```
[btn] mask=0x0100 btn1=1 ...              ← 左键
[btn] mask=0x0400 btn3=1 ...              ← 右键
[btn] mask=0x0500 btn1=1 btn3=1 ...       ← 双键同按

[gate] scope=0 fire=0   1707 行
[gate] scope=0 fire=1     58 行           ← 仅左键
[gate] scope=1 fire=0     41 行           ← 仅右键
[gate] scope=1 fire=1     91 行           ← 双键，门控放行

[aim] target idx=798 err=(-14.4,23.0) move=(-1,2)
[aim] target idx=939 err=(-184.1,-84.7) move=(-17,-8)
[aim] no target in fov=220 nearest=265.4 players=6
```

结论：`buttonDown` 对 btn1/btn3 均可靠，`XTestFakeRelativeMotionEvent` 被游戏消费，双键门控放行后自瞄产生实际位移；单独左键不触发是 `scope && fire` 的设计行为，非漏读。`[aim] no target` 的 `nearest` 落在 265~692，说明默认 220px FOV 偏小。

## 4. 遗留风险

1. `/tmp/wv_lite.log` 为纯追加，长期运行需人工清理（当前无轮转）。
2. 掩码变化行由主循环（1 ms）轮询得到，理论上可能漏掉短于 1 ms 的点按。
3. `[skip] active=0` 仍会零星出现（切窗/全屏切换瞬间），属叠加层窗口探测的正常抖动，恢复后自动继续。

# 阶段开发日志 — 阶段十：血量数值 / 2D 雷达 / 目标策略 / 可见性样式 / 门控可视化 + 面板重构

## 1. 需求

在现有快照字段（`health / team / visible / origin / headScreen / feetScreen / distance`）上做可视化与策略扩展，不新增任何内存偏移；同时统一叠加层与控制面板的视觉语言。

## 2. 实现

| 功能 | 文件 | 说明 |
|---|---|---|
| 血量数值 | `src/render/Overlay.cpp` | `esp.healthText` 开启时在血条左侧绘制读数，等宽字体避免跳动 |
| 可见性双态样式 | 同上 | 可见 = 实线全亮；`visibilityCheck` 开启且被遮挡 = `Qt::DashLine` + alpha 110 |
| 2D 雷达 | 同上 | 底部居中，半径 92 px；固定北向（世界 +X 向右、+Y 向上，屏幕 y 取负）；按 `radarRange` 米线性映射，中心为自身 |
| FOV 圈门控着色 | 同上 + `src/features/Features.cpp` | `AppState::aimGate` 由 Features 写入，按住双键时圆环转绿并加粗 |
| 目标策略 | `src/features/Features.cpp` | `aim.strategy`：0 屏幕最近、1 血量最低、2 世界最近 |
| 面板重构 | `src/ui/Menu.cpp`、`Menu.h` | QSS 深炭底 + 琥珀强调、8 px 栅格、直角边框；新增血量数值 / 雷达 / 雷达范围 / 目标策略控件 |
| 配置字段 | `src/core/Config.h`、`Config.cpp` | `esp.healthText`、`esp.radar`、`esp.radarRange`、`aim.strategy`、`AppState::aimGate` 的读写 |

## 3. 验证

- 构建：`cmake --build build -j8` → 8 个编译单元，0 warning / 0 error，链接成功。
- 布局：`xvfb-run` + `import -window root` 截取实际渲染，确认分组标题、控件对齐、按钮样式与中文渲染正常（`/tmp/wv_ui.png`）。
- 未验证：血量数值/雷达/FOV 变色的实际叠加效果需游戏内确认（当前无 CS2 进程）。

## 4. 遗留风险

1. 雷达为固定北向，不随视角旋转；如需随视角旋转，须从视图矩阵推导 yaw（当前未实现，避免猜测矩阵布局）。
2. 雷达点按 `radarRange` 截断，超出范围的敌人不显示，可能出现“雷达空但场上有敌”的观感。
3. 面板只暴露部分偏移项，其余仍以 `config/offsets.json` 为准。



---

# 阶段十一：控制面板极简化（黑白双色 / 半透明玻璃）

## 1. 需求

UI 极简、精简、现代化、透明；配色收敛为黑白双色。

## 2. 实现

| 变更 | 文件 | 说明 |
|---|---|---|
| 无边框玻璃底板 | `src/ui/Menu.cpp` | `FramelessWindowHint` + `WA_TranslucentBackground`；`paintEvent` 绘制 14 px 圆角、`rgba(13,14,16,216)` 底板、1 px 冷白描边、顶部白色高光线性渐隐 |
| 面板拖动 | 同上 | 无 WM 装饰，自定义 `mousePressEvent/mouseMoveEvent` 按 `globalPosition` 差值移动窗口 |
| 首次居中 | 同上 | `showEvent` 中按 `primaryScreen()->availableGeometry()` 居中一次 |
| 关闭按钮 | 同上 | 右上角 `✕`（18×18，hover 白字 + 白色 20 alpha 底），点击关闭面板 |
| 分组去盒化 | 同上 | 移除 `QGroupBox`，改用 10 px 字母间距的小节标签 + 1 px 分隔线 |
| 控件精简 | 同上 | 复选框 13 px 圆角 4 px 指示器（勾选纯白填充）；数字框隐藏上下按钮、固定宽度（平滑 64 / 范围 88 / 灵敏度 88 / 扳机 84 / 雷达 78）；策略下拉跨列右对齐 |
| 黑白双色 | 同上 | 移除全部琥珀色，勾选态与主按钮为白 `#F2F3F5` / `#EDEFF2`，其余为中性灰阶 |

## 3. 验证

- 构建：`cmake --build build -j8` → 0 warning / 0 error。
- 渲染：`xvfb-run -a -s "-screen 0 1280x1000x24"` + `import -window root` 截图（`/tmp/ui_bw.png`）确认：圆角与描边绘制正常、标题/状态行、两列复选框、下拉与数字框对齐、分隔线、白色主按钮、无残留彩色。
- 说明：Xvfb 无合成器，半透明区域在截图中呈纯黑；真实桌面有合成器时底板按 alpha 216 透出下层。

## 4. 遗留风险

1. 半透明依赖 X11 合成器；无合成器环境底板退化为接近纯黑，圆角仍由 alpha 生效。
2. 关闭按钮直接退出进程（`QCoreApplication` 主窗口关闭），无托盘常驻入口。

---

# 阶段十二：面板改双列布局

## 1. 需求

面板不要单列竖向堆叠。

## 2. 实现

- `src/ui/Menu.cpp`：主体改为 `QHBoxLayout` 两列（`columns->setSpacing(16)`），中间 1 px 竖向分隔线（`QFrame#vrule`，白色 14 alpha）。
  - 左列：透视（复选框两列 + 2D 雷达行）→ 分隔线 → 扳机。
  - 右列：自瞄（启停 + 策略 + 平滑/范围/灵敏度）。
  - 底部：偏移来源提示 + 「应用并保存」置于右列末端，`addStretch` 顶开。
- 面板宽度 `322 → 520`；雷达半径 78→74 px、扳机数字框 84→78 px、自瞄数字框 88→80 px 以适配半宽列。

## 3. 验证

- `cmake --build build -j8` → 0 warning / 0 error。
- `xvfb-run -a -s "-screen 0 1280x900x24"` + `import -window root` 截图（`/tmp/ui_2col.png`）确认两列并列、竖分隔线可见、无文字截断、底部对齐。

## 4. 遗留风险

- 面板宽 520 px，1366×768 等窄屏下仍可容纳，但若加宽屏外挂布局需再核对。

---

# 阶段十三：面板背景完全透明（仅线条分隔）

## 1. 需求

背景完全透明，仅靠线条划分区域。

## 2. 实现（`src/ui/Menu.cpp`）

- `paintEvent`：删除底板填充与顶部高光，只保留 1 px 圆角线框（白 56 alpha）作为可拖动边界。
- QSS 去底色：
  - `QSpinBox/QDoubleSpinBox/QComboBox` → `background: transparent`，仅留 1 px 边线（白 70 alpha），hover/focus 时加 26 alpha 白底以示输入态。
  - `QPushButton#apply` → 透明底 + 1 px 白 90 边线白字，hover 反白（白 235 底 + 深字）。
  - `QCheckBox::indicator` 未选中态透明底，仅边线（白 110 alpha）；选中态纯白填充。
- 线框与文字提亮以保证任意背景可读：`#rule` 70 alpha、`#vrule` 52 alpha；文字 `#E9EBEE / #C2C7CC / #B0B6BC / #9AA0A6`，小节标签 `#B4B9BF`。

## 3. 验证

- `cmake --build build -j8` → 0 warning / 0 error。
- 透明度实证：Xvfb 下 `picom --backend xrender` 合成 + `display -immutable /tmp/bg.png`（`#5a6b7a` 全屏底色）作背景，`import -window root` 截图（`/tmp/ui_onbg2.png`）→ 面板内部完整透出底色，仅线条/文字/控件边框可见。
- 窗口深度：`xwininfo -id` 报 `Depth: 32`（ARGB visual），证明半透明通道真实存在。

## 4. 遗留风险

1. 无合成器（无 picom/kwin/mutter）时，X11 会忽略 alpha，面板将呈不透明黑底——属平台限制，非代码问题。
2. 面板无底色，在纯白高亮场景下白字对比度下降；当前用亮度分层的灰阶缓解，未引入文字描边。

---

# 阶段十四：左侧类别导航 + 右侧分类页

## 1. 需求

不要竖排堆叠，改为「右边类别分类」式布局：左侧类别，右侧只显示当前类别的设置内容。

## 2. 实现（`src/ui/Menu.cpp`）

- 主体改为 `QHBoxLayout`：`QListWidget#nav`（88 px）+ 1 px 竖分隔线 + `QStackedWidget`。
- 导航项「透 视 / 自 瞄 / 扳 机」，`currentRowChanged → QStackedWidget::setCurrentIndex`；默认 `setCurrentRow(0)`。
- 三页内容：
  - 页一：透视 8 项复选框（两列）+ 2D 雷达行（半径 74 px）。
  - 页二：自瞄（启用/过滤队友 + 策略跨列 + 平滑 60 / 范围 80 / 灵敏度 80）。
  - 页三：扳机（启用 + 按住 78 / 冷却 78）。
- 导航样式：`::item` 透明 + `#B4B9BF`，hover 白 18 alpha，`:selected` 反白（白 235）+ 深字 `#0F1113`，圆角 7 px、字距 2 px。
- 面板宽度 `520 → 440`（导航占 88 px 后内容区仍够两列复选框与目标页控件）。
- 清理：删除已无引用的 `sectionLabel()` helper 与 `QLabel#section` 样式，补 `<QListWidget>` / `<QStackedWidget>` include。

## 3. 验证

- `cmake --build build -j8` → 0 warning / 0 error。
- Xvfb + `xdotool` 实测点击导航：`/tmp/nav_p2.png`（自瞄页）、`/tmp/nav_p3.png`（扳机页）、`/tmp/nav_p3_comp.png`（picom 合成下）→ 页面切换正确、选中项反白跟随、无控件错位或文字截断。

## 4. 遗留风险

1. 无窗口管理器（纯 Xvfb）时窗口无法被 WM 聚焦，但鼠标事件正常；真实桌面不受影响。
2. picom xrender 后端在 32 位透明窗口上偶发局部残影（截图里的陈旧内容），属合成器刷新，非布局问题；无合成器直连 X 的截图内容完整。

---

# 阶段十五：配置热重载 / 可见性优先选靶 / 偏移守卫

## 1. 需求

从「还能加什么」清单中落地低风险三项：22 配置热加载、13 可见性优先、27 偏移失效守卫。

## 2. 实现

- 配置热重载（`src/ui/Menu.cpp`、`Menu.h`）
  - 复用已有 400 ms 状态刷新 `QTimer`，用 `QFileInfo::lastModified()` 与成员 `configStamp_` 比对。
  - mtime 变化 → `Config::load()` → `state_.config.store()` → `syncFrom()` 回填全部控件，无需重启。
  - `apply()` 写盘后同步 `configStamp_`，避免自己写盘触发一次多余重载。
- 可见性优先选靶（`src/features/Features.cpp::aimStep`）
  - FOV 改为固定门槛（旧实现把最近目标的屏幕距离当作 FOV 上限，导致策略 1/2 的候选被错误裁掉）。
  - 选靶分两层：同 FOV 内 `visible`（`m_bSpotted`）为真的目标优先，无可见目标才退化到遮挡目标；同层内再按策略（屏幕最近 / 血量最低 / 世界最近）比较。
  - `m_bSpotted` 偏移未配置时 `PlayerInfo::visible` 恒为 true，行为与旧版一致。
- 偏移守卫（`src/ui/Menu.cpp`）
  - 依据 `initial.offsets` 检查实体列表 / 本地玩家 / 视图矩阵 / 血量 / 阵营 / 坐标，缺失时底部提示改为高亮告警（`QLabel#warn`，纯白加粗），并列出缺项。
  - 注意：`Config::load` 会先内嵌偏移、再由 `offsetsFile` 覆盖，守卫判断的是最终生效值，不会误报。
  - 正常态提示文案改为「偏移来源 X · 偏移改动需重启」（配置文件本身已支持热重载）。

## 3. 验证

- `cmake --build build -j8` → 0 warning / 0 error。
- 热重载实证：Xvfb 启动面板后外部改写 `build/config.json`（`esp.box` 取反、`esp.radar=true`、`radarRange=88`），2 s 后截图与改写前逐像素比对，差异带落在 `y=347-361`（方框行）与 `y=456-470`（雷达行含半径），证明控件被回填、`state_.config` 已替换。
- 偏移守卫实证：`build/config.json` 保持 `offsetsFile=config/offsets.json` 时导入真实偏移 → 提示为来源行；清空 `offsetsFile` 且内嵌偏移全 0 → 提示区出现差异带 `y=561-571`（告警行）。

## 4. 未落地项

1. 12 后坐力补偿：`aimPunchCache` 无权威偏移来源，按规范不臆造，暂不实现。
2. 8 雷达视野扇形：需要可靠的本地朝向。`Snapshot::view` 的手性（forward 取 `+` 或 `-` 第 3 列）无法离线确认，猜错会导致扇形镜像，暂不实现。

---

# 阶段十六：日志页 / 距离分级 / 骨骼关节 / 触发抖动

## 1. 需求

继续清单：25 日志面板、6 距离分级配色、7 骨骼关节圆点、17 扳机随机延迟、18 仅可见目标。

## 2. 实现

- 日志页（`src/ui/Menu.cpp`）
  - 新增 `tailOfLog()`：`QFile` seek 到末尾 8 KB 读取，避免日志增长后拖慢 UI 线程；截断半行、取末 16 行。
  - 第四分类「日 志」用只读 `QPlainTextEdit#log`，仅在当前页可见时由 400 ms 定时器刷新，其余页面零开销。
  - 分类导航由 `QListWidget` 改为 4 个互斥可选中的 `QPushButton#tab`（`setCheckable` + `setAutoExclusive`，固定 88×30）。
    原因：`QListWidget` 的视口高度受主体区高度约束，第四项会被裁掉（实测 `visualItemRect(3)` 有效但不可见）；按钮列高度由自身决定，不会被压缩。
- 距离分级（`src/render/Overlay.cpp`）
  - `distanceFade()`：15 m 内满亮、60 m 衰减到 0.45，线性过渡；只调透明度，不动敌我语义色。
  - 方框、骨骼、遮挡虚线全部乘以该系数。
- 骨骼关节圆点：骨骼每段两端画 1.8 px 实心圆，颜色与骨骼同透明度。
- 扳机（`src/features/Features.cpp::triggerStep`）
  - 仅对 `visible` 目标开火（`m_bSpotted` 未配置时恒为 true，行为不变）。
  - 按压时长在 `delayMs` 上做 ±20% 抖动（`std::mt19937` + `std::uniform_int_distribution`），下限夹到 1 ms。

## 3. 验证

- `cmake --build build -j8` → 0 warning / 0 error。
- 导航实证：Xvfb 截图扫描左列文字带，四个分类渲染于 y=345/388/422/457；点击第四项后差异区 `(437,345)-(841,537)`，右侧出现日志正文行（y=347…403，14 px 行距）。
- ESP 与扳机改动依赖真实快照 / 按键，仅完成编译验证，未在实机核对。

## 4. 备注

- 阶段十四的 `QListWidget` 导航已被本阶段替换，QSS `QListWidget#nav` 同步删除。

---

# 阶段十七：配置保存与「上一次配置」回滚

## 1. 目标

面板此前只有「应用并保存」一个落盘入口，误改后无法退回上一版配置，只能手工编辑 JSON。

## 2. 实现

- 备份路径（`src/ui/Menu.cpp` 匿名命名空间）
  - `backupPath(path)` = `path + ".bak"`，与主配置同目录。
- `Menu::persist(const Config&, const QString& message)`
  - 若主配置存在：`QFile::remove(backup)` 后 `QFile::copy(target, backup)`。`QFile::copy` 不覆盖已存在目标，必须先删。
  - `config.save(path_)` → `state_.config.store(...)` → `configStamp_ = QFileInfo(target).lastModified()` → 状态栏文案。
  - `apply()` 改为直接调用 `persist(next, "已保存并生效")`，行为与旧版一致（多出一次备份）。
- `Menu::restoreLast()`
  - 打开 `.bak`，用 `QJsonDocument::fromJson` + `isObject()` 校验；打不开 → 「没有上一次保存的配置」，解析失败 → 「上一次保存的配置已损坏」，两种情况都不改动内存与磁盘配置。
  - 校验通过后 `Config::load(backup)` → `syncFrom(restored)` → `persist(restored, "已恢复上一次保存")`。
  - 由于 `persist` 会先把当前配置备份成新的 `.bak`，回滚是**交换**语义：再点一次即切回。
- 底部布局：`QHBoxLayout`，`恢 复 上 一 次`（`#restore`，拉伸 1）+ `应 用 并 保 存`（`#apply`，拉伸 2）。
- QSS 新增 `QPushButton#restore` / `:hover` / `:pressed`，采用更暗的描边与文字，避免与主按钮抢层级。
- `.gitignore` 追加 `config.json.bak`。

## 3. 验证

- `cmake --build build -j8` → 0 warning / 0 error。
- Xvfb（1280x900）实例化，窗口 440×382 @ (419,258)：
  - 点击应用按钮后生成 `config.json.bak`，且与点击前的 `config.json` 逐字节一致（`diff -q` 通过）。
  - 外部把 `esp.box` 改 false、`esp.skeleton` 改 true 后启动并点击「恢复上一次」：`config.json` 回到 `box=true / skeleton=false`，`.bak` 变为 `box=false / skeleton=true`，证明交换语义成立。
- 像素检查底部按钮行：横向描边段落在 x=437–567 与 578–858，宽度比 1:2，上下边框 y=590 / 639，两个按钮均完整渲染。

## 4. 备注

- 备份策略为单槽位，只保留「上一次」，不做多版本历史；多版本需求另开。
- 回滚只覆盖 `Config` 已有字段，偏移仍按 `config.json` 生效，运行中不重载偏移（沿用既有约定）。

---

# 阶段十八：三项内存派生功能（护甲 / 掉血浮字 / 准星目标高亮）

## 1. 目标

在不臆造偏移的前提下，把已读到的内存用足：补一个公开字段（护甲），再把已有 `m_iHealth` / `m_iIDEntIndex` 的时序与命中信息转成可视化。

## 2. 实现

- 护甲（新偏移，来源可追溯）
  - `Offsets::m_ArmorValue`（cs2-dumper `C_CSPlayerPawn` 字段名），`Config` offsets 读写与 `OffsetsImporter` 绑定表补键。
  - `Game::snapshot` 在 `m_ArmorValue != 0` 时读 `int`，否则 `PlayerInfo::armor` 保持 `-1`。
  - `Overlay` 仅在 `esp.armor && armor >= 0` 时在血量数字下一行绘制，颜色 `rgb(150,180,212)`。
- 掉血浮字（零新偏移）
  - `Overlay` 新增 `lastHealth_`（实体序号 → 上一帧血量）与 `damageMarks_`（序号 → 掉血量 + 时间戳）。
  - 每帧比对：`previous > current` 记为掉血事件；回血不产生浮字。
  - 绘制：`-N` 头顶上浮 18 px、1 s 内 alpha 255 → 0，色 `rgb(255,132,96)`。
  - 清理：超过 1 s 或该序号已不在快照中即 `erase`；功能关闭时两张表整体清空。
- 准星目标高亮（复用已有 `m_iIDEntIndex`）
  - `snapshot.crosshairIndex` 与 `PlayerInfo::index` 同为实体列表序号，直接比较。
  - 命中者在方框外 3 px 追加 1 px 白框；未开启方框时不绘制。
- 配置与面板
  - `EspConfig::armor / damageNumbers` 默认 false；`Config::load/save` 增键。
  - 面板 ESP 页第 5 行：`护甲数值` / `伤害数值`；`syncFrom` 与 `apply` 同步。

## 3. 验证

- `cmake --build build -j8` → 0 warning / 0 error。
- Xvfb 实例化，右侧内容区文字行带由 4 行增至 5 行（新增 y=450–464 一行）。
- 点击第 5 行左侧复选后 `config.json` 中 `esp.armor` 由 false 变 true；像素 diff 仅 `(554,449)-(570,466)` 的 16×17 指标框，未波及其他控件。
- 护甲读数、掉血浮字、准星高亮依赖真实对局内存，当前仅编译与渲染路径验证，未做实机截图。

## 4. 备注

- 本机 `offsets.json` 尚无 `m_ArmorValue`，因此护甲默认不可见；用新版 cs2-dumper 输出该键后导入即生效，未配置时不会误显示 0。
- 掉血浮字展示的是该实体血量下降，包含队友/其他人造成的伤害，不是「我方命中数」。

---

# 阶段十九：护甲偏移纳入 schema 生成器

## 1. 目标

阶段十八加了护甲读数的代码路径，但本机 `config/offsets.json` 没有 `m_ArmorValue`，功能等于关着。补齐生成器，让偏移从活体 SchemaSystem 取，而不是手填。

## 2. 实现

- `wanted` 集合补 `C_CSPlayerPawnBase`：字段如果由基类声明，不遍历该绑定就取不到。
- 新增 `need_any(classes, name)`：按继承链顺序查找，命中即返回；全部未命中才 `SystemExit`，不产生默认值。
- `m_ArmorValue = need_any(["C_CSPlayerPawn", "C_CSPlayerPawnBase"], "m_ArmorValue")`。
- `[verify]` 行补 `armor=`，读本地 pawn 的护甲值用于合理性判断。
- 结果字典写入 `m_ArmorValue`，与其余字段同一份 `config/offsets.json`。

## 3. 验证（真实运行进程）

- `python3 -m py_compile tools/cs2_linux_offsets.py` 通过。
- 在对局中的 cs2 进程上实跑：
  - `[field] C_CSPlayerPawn.m_ArmorValue = 11616 (0x2d60)`
  - `[verify] localPawn health=100 armor=96 team=3 lifeState=0 ...`
  - `config/offsets.json` 写出 `"m_ArmorValue": 11616`。
- `armor=96` 与「满血 100 + 护甲 96」自洽，说明读到的是真实字段而非垃圾值；偏移由 schema 解析而非猜测。

## 4. 备注

- 生成器每跑一次都会用当前游戏版本覆盖 `offsets.json`，游戏更新后重跑即可，无需改代码。
- 护甲是否显示仍由 `esp.armor` 控制，默认关闭。

---

# 阶段二十：全功能失效的根因与修复

## 1. 目标

用户报告「所有功能都无法使用了」，且经追问确认不是某一层失效，而是透视、方框、自瞄、扳机、骨骼、面板全部无反应。目标是定位全部根因而非逐条猜修。

## 2. 排查与实现

四条独立缺陷，按影响面排序：

- 根因一：零偏移配置传播
  - `build/config.json` 为 1473 字节，`offsets` 全 0；`Game` 构造时按值拷贝 `Offsets`，读到零偏移 → 所有内存读取落空，画面无任何玩家。
  - `importOffsetsFile()` 在文件打不开时提前 `return`、不清空 offset，所以缺失 `config/offsets.json` 不会自愈，只会保持零值。
  - 修复：`Menu::persist()` 落盘前拦截 `dwEntityList / dwLocalPlayerPawn / dwViewMatrix` 任一为 0，直接 `return` 并在状态栏提示「偏移缺失，未保存」。
  - 修复：`CMakeLists.txt` 增加 `POST_BUILD`，把 `config/offsets.json` 复制到 `$<TARGET_FILE_DIR>/config/`，使相对路径 `offsetsFile` 无论从仓库根还是 `build/` 启动都能解析。
- 根因二：`m_modelState` 语义错位
  - schema 的 `CSkeletonInstance.m_modelState = 320` 是 `CModelState` 结构体起点，骨骼数组指针在 `scene + 0x1c0`（结构体内 +0x80，实测 448）。
  - 修复：生成器不再把该字段写入结果，只打印注释；`config/offsets.json` 与 `build/config.json` 取 448。
- 根因三：面板无置顶
  - `Qt::FramelessWindowHint | Qt::Window` 后没有标题栏，也没有置顶标志，被 IDE / 浏览器整块覆盖且无法点回前台。
  - 修复：窗口标志补 `Qt::WindowStaysOnTopHint`。
- 根因四：扳机被恒 0 字段卡死
  - 选靶函数用 `m_bSpotted` 判可见，本版本该字段恒 0，`if (!target->visible) return;` 导致扳机永不出手。
  - 修复：删除该门控，保留 `config.trigger.teamCheck` 队伍检查。

`Game::resolveOffsets()`：`m_modelState` 缺失从「偏移缺失」列表移除，只走降级状态文案；`m_boneArray` 允许为 0。

## 3. 验证

- `cmake --build build -j8` → `[3/3] Linking CXX executable cs2_lite`，0 warning / 0 error。
- 面板置顶（Xvfb）：`xprop _NET_WM_STATE` 输出 `_NET_WM_STATE_ABOVE, _NET_WM_STATE_STAYS_ON_TOP`，`Map State: IsViewable`，截图渲染完整（含「恢 复 上 一 次」/「应 用 并 保 存」与第 5 行「护甲数值 / 伤害数值」）。
- 保存不丢偏移：以真实值配置点击「应用并保存」，`offsets` 保持 `dwEntityList=79275856`、`dwLocalPlayerPawn=76829880`、`dwViewMatrix=76860160`。
- 自愈：把 `config.json` 的 `dw*` 与 `m_iHealth` 清零，同时放置 `config/offsets.json` → 启动后点击保存，`offsets` 恢复为真实值。
- 拦截：清零配置且**无** `offsets.json` 时点击保存，文件 `stat` 前后完全一致（`before=[1794321665 1748] after=[1794321665 1748]`），未生成 `.bak`。
- 偏移正确性（活体只读探针，pid 679372）：`m_modelState=448` 读到 10 个健康玩家，骨骼校验 10/10，`bone6.z - origin.z ≈ 60.1–60.3`；同一进程用 320 读不到骨骼。
- 最终状态截图 `/tmp/status_fixed.png`：状态行「已连接 pid=679372」，无「骨骼偏移缺失」；底部「偏移来源 已从 config/offsets.json 导入 12 项偏移 · 偏移改动需重启」。

## 4. 备注

- `Game::offsets_` 是构造时的值拷贝，配置热重载只更新 `AppState::config`，不刷新 `Game`。偏移变更必须重启 `cs2_lite`。
- 本轮未实机验证方框 / 自瞄 / 扳机的游戏内画面，Xvfb 内无法让游戏窗口出现在该 display；验证收敛到偏移可读性与渲染路径。
- `m_bSpotted` 在本版本恒 0，已记入 README，后续不得再作为门控条件。

---

# 阶段二十一：后坐力抵消与五项交互增强

## 1. 目标

实现后坐力抵消（无后坐力）与五项零新偏移的交互增强：热键重绑定、掉血浮字时长/颜色、残血描边、残影轨迹。

## 2. 实现

- 偏移层
  - `Offsets` 新增 `m_aimPunchAngle`、`m_aimPunchCache`，默认 0 表示未配置。
  - `OffsetsImporter` 增加两条绑定；`config/default.json` 同步两个键。
  - 生成器新增 `maybe_any()`：可选字段未命中时只打印 `not present in schema (recoil disabled)`，不 `SystemExit`，避免因为一个可选字段让整份偏移生成失败。
- 读取层
  - `Snapshot::aimPunch`（`Vec2`，x = pitch，y = yaw）。
  - 优先直读 `QAngle` 前两个 float；缺失时走 `CUtlVector<QAngle>`：指针 `+0`，`m_Size` 在 `+16`（Source SDK `CUtlMemory`：`m_pMemory`/`m_nAllocationCount`/`m_nGrowSize` → 16 字节），`m_Size` 限定 1..64 防脏指针。
- 补偿层
  - `Features::recoilStep()` 每帧只补偿 `punch - lastPunch` 的**增量**：绝对值每帧重发会在压枪时过转，停火衰减期也会被持续反向追打。
  - 单帧角度超过 `maxDegrees` 直接丢弃并清零基准；像素换算沿用 aimStep 的 `屏幕高 / 90°`。
  - 强度支持负值，用于实机方向相反时翻转，不需要改代码。
- 绘制层
  - 残影轨迹：`std::map<int, std::deque<TrailPoint>>`，`trailMs` 窗口淘汰，目标消失整条丢弃。
  - 残血描边：本帧可绘制敌人中血量最低者画橙红外框，准星目标白框优先。
  - 掉血浮字：时长与颜色改为配置驱动，颜色取 `0xRRGGBB`。
- 面板层
  - 自瞄页新增 开镜键 / 开火键 下拉、后坐力抵消 / 仅开火时 勾选、强度 / 上限 数字框。
  - 扳机页新增 触发键 下拉；ESP 页新增 残影轨迹 + 时长、残血描边、浮字时长 + 颜色。
  - 状态行新增 4 秒驻留（`messageUntil_`），保存提示不再被 400 ms 连接状态轮询秒覆盖。

## 3. 验证

- `cmake --build build -j8` → 10 个编译单元全部通过，0 warning / 0 error。
- Xvfb :91 截图 `/tmp/ui_new.png`：ESP 页新增「残影轨迹 220 ms / 残血描边」「浮字 800 ms / 橙红」，面板 440×429 无裁切。
- Xvfb :92 截图 `/tmp/ui_aim.png`、`/tmp/ui_trigger.png`：自瞄页出现 开镜键=右键 / 开火键=左键 / 后坐力抵消 / 仅开火时 / 强度 0.60 / 上限 12°；扳机页出现 触发键=不限。
- 落盘往返（`/tmp/cs2t` 独立副本，勾选后点「应用并保存」）：
  - `recoil = {'enabled': True, 'strength': 0.6000000238418579, 'maxDegrees': 12, 'requireFire': True}`
  - `esp.trail=False, trailMs=220, damageMs=800, damageColor=0xff8460`
  - `aim.scopeKey=3, aim.fireKey=1, trigger.key=0`，`offsets.dwEntityList=79275856` 未被破坏。
- 状态行（`/tmp/ui_msg.png`）：`已保存 · 后坐力偏移缺失，先跑 tools/cs2_linux_offsets.py`。

## 4. 未验证 / 风险

- 采集时 `cs2` 未运行，`m_aimPunchAngle` / `m_aimPunchCache` 未能实机确认，配置里两者仍为 0；后坐力功能处于「代码就绪、偏移待生成」状态。实机需跑一次生成器让字段落盘，再重启客户端。
- 补偿方向按公开实现约定 `dx = -Δyaw`、`dy = +Δpitch`；若实机方向相反，把强度改为负值。
- 尚未验证：残影轨迹与残血描边的游戏内观感、后坐力补偿手感。

## 5. 后续批次

面板预设、武器名（`m_iItemDefinitionIndex`）、开镜状态（`m_bIsScoped`）、速度显示（`m_vecVelocity`）、闪光遮罩（`m_flFlashDuration`）、连跳（`m_fFlags`）、休眠过滤（`m_bDormant`）——均需生成器先在活体 schema 命中字段后再实现。

# 阶段二十二：启动自动刷新偏移 + 后坐力链路改为实测字段

## 1. 开发过程

1. 在 `src/Main.cpp` 加入 `refreshOffsets()`：定位仓库根（向上找 `tools/cs2_linux_offsets.py`）→ `QProcess` 跑 `python3` → 成功后把 `config/offsets.json` 复制到可执行文件目录。调用点放在 `Config::load()` 之前。
2. 生成器改为采集实测字段：`wanted` 增加 `CCSPlayer_AimPunchServices`；`m_pAimPunchServices = maybe_any(["C_CSPlayerPawn","C_CSPlayerPawnBase"], "m_pAimPunchServices")`、`m_aimPunchAngle = maybe_any(["CCSPlayer_AimPunchServices"], "m_predictableBaseAngle")`；结果字典删除 `m_aimPunchCache`。
3. 客户端侧一把改齐：`Offsets.h`、`Config.cpp`（load/save 键名）、`OffsetsImporter.cpp`（绑定表）、`Game.cpp`（两级解引用读 `Vec2`）、`Menu.cpp`（`punchReady` 改为两个偏移同时非零）、`config/default.json`。
4. 重新生成 + 编译 + 双模式启动验证。

## 2. 实测证据

- 生成器输出：`[field] C_CSPlayerPawn.m_pAimPunchServices = 5408 (0x1520)`、`[field] CCSPlayer_AimPunchServices.m_predictableBaseAngle = 80 (0x50)`。
- 同类的宽度反证：`CCSPlayer_AimPunchServices.m_predictableBaseAngleVel = 92 (0x5c)`，即 `m_predictableBaseAngle` 为 12 字节 QAngle，与 `Vec2` 读取前两个 float 一致。
- 枚举规模：`classes_in_libclient=364`，说明 `wanted` 之外没有遗漏同名候选。
- 活体解引用（对局内，localPawn health=100）：`punchServices = 0x2abc2417c00 readable=True`，`baseAngle @0x50 = [0.0, 0.0, 0.0]`（未开火时为 0，结构对齐正确）。曾捕获一次 `0x0900050051000051` 的非指针值，同一 pawn 复测即恢复为有效堆指针，判断为出生瞬间字段未初始化，不影响稳态读取。
- 交叉对照：`C_BasePlayerPawn.m_pCameraServices`(0x12b0) → `CPlayer_CameraServices.m_vecCsViewPunchAngle`(0x48) 同样可读且为 `[0,0,0]`，作为备用链路记录，本次未接入。
- 生成器耗时：cs2 运行中 0.587 s（两次测量一致），15 s 超时上限留有 25 倍余量。
- 启动验证（Xvfb `:96`，1280x800x24）：
  - 默认启动 → `/tmp/wv_lite.log` 出现 `[offsets] 已刷新偏移: /mnt/data/home/项目/cs2/build/config/offsets.json`，`build/config/offsets.json` 与仓库同值（`dwEntityList=79275856`、`m_pAimPunchServices=5408`、`m_aimPunchAngle=80`）。
  - `WV_SKIP_OFFSETS=1` → `[offsets] 跳过自动生成（WV_SKIP_OFFSETS 已设置）`。
  - 两次进程均正常存活到 `timeout` 终止（exit 124）。中途出现的 exit 134 是测试用 Xvfb 已被前一条命令回收、Qt 连不上 X server 导致，非程序缺陷；同一 shell 内保活 Xvfb 后不再复现。
- 构建：`cmake --build build -j8` 10/10 成功，0 warning。

## 3. 变更记录

- `src/Main.cpp`：`refreshOffsets()` / `appendLog()`，`refreshOffsets()` 在 `Config::load` 前调用。
- `src/core/Offsets.h`：`m_aimPunchCache` → `m_pAimPunchServices`。
- `src/core/Config.cpp`：`m_aimPunchCache` 键名替换为 `m_pAimPunchServices`（load + save）。
- `src/core/OffsetsImporter.cpp`：绑定表同步。
- `src/game/Game.cpp`：`pawn + m_pAimPunchServices` 取指针，再 `+ m_aimPunchAngle` 读 `Vec2`。
- `src/ui/Menu.cpp`：`punchReady` 改为 `m_pAimPunchServices != 0 && m_aimPunchAngle != 0`。
- `tools/cs2_linux_offsets.py`：punch 采集换成两级跳字段，`wanted` 增加 `CCSPlayer_AimPunchServices`。
- `config/default.json`：键名同步。
- `config/offsets.json`：重新生成，`m_aimPunchAngle=80`、`m_pAimPunchServices=5408`。

## 4. 未验证 / 风险

- 未实机开火：`m_predictableBaseAngle` 在未射击时恒为 0，无法区分「字段正确」与「字段恒 0」。需在实战中打一梭子确认数值随散布变化；若仍恒 0，改用已记录备用的 `m_pCameraServices → m_vecCsViewPunchAngle` 链路。
- 补偿方向 `dx = -Δyaw`、`dy = +Δpitch` 仍按公开实现约定，方向若相反把强度改负值。
- 启动自动刷新依赖 `python3` 与仓库目录结构；打包分发（只带走可执行文件）时向上找不到 `tools/`，会走「沿用现有偏移」分支。

# 阶段二十三：子弹追踪（开火锁头）

## 1. 需求

不管准星指向哪里，只要按住触发键，准星立刻拉到最近敌人头部；持续按住则每帧重新贴合。

## 2. 实现

- `src/core/Config.h`：新增 `TrackConfig{enabled, key, fovPixels, smooth, teamCheck, maxDistance}`，挂在 `Config::track`。
- `src/core/Config.cpp`：`track` 节点 load / save，缺失键走结构体默认值。
- `src/features/Features.cpp`：新增 `trackStep()`，遍历 `snapshot.players`（跳过死亡、队友、超距），可见者优先、再取屏幕距离最近的 `headScreen`；`fovPixels = 0` 不设半径（全屏）；位移用 `XTestFakeRelativeMotionEvent`，除以 `smooth` 后取整，不乘 `sensitivityFactor`。
- `src/features/Features.h`：声明 `bool trackStep(...)`。
- 调用顺序：追踪先执行并返回本帧是否接管；接管则跳过 `aimStep`，避免两者在同一帧互相拉扯。
- `src/ui/Menu.cpp` / `Menu.h`：自瞄页新增「子弹追踪（锁头）」「过滤队友」、半径（0 显示「全屏」）、平滑、触发键；`apply()` / `syncFrom()` 同步。
- `config/default.json`：新增 `track` 节点（`enabled=false`，`fovPixels=0`，`smooth=1`，`key=1`）。

## 3. 验证结果

- 构建：`cmake --build build -j8` 成功，0 warning / 0 error。
- 界面（Xvfb `:97`，1280x800x24）：自瞄页渲染出完整两行新控件（勾选行 + 半径/平滑/触发键行），无裁切。
- 配置装载：把 `build/config.json` 的 `track` 置 `enabled=true, fovPixels=380, smooth=2` 后启动，面板显示勾选、`380 px`、`2`。
- 保存往返：点击「应用并保存」后重读 `build/config.json`，`track` 三字段保持 `true / 380 / 2`，未被其它字段覆盖。
- **未验证**：实机开火贴头效果。当前 CS2 处于非对局状态，无法构造「开火 + 屏幕内有敌人」的时序；`[track]` 诊断行（每 200 次输出）需实战确认。

## 4. 遗留风险

1. 外部只读实现无法改写服务端弹道，本质是开火前把视角钉在头部。
2. 与后坐力抵消同时开启会互相拉扯（见 PLAN 第 17 节），建议二选一。
3. `smooth = 1` 时是单帧瞬移，观感极硬；被打断（切窗）后恢复也硬。
4. Wayland 原生会话无 XTest，功能不可用，与既有自瞄一致。

# 阶段二十四：静默锁头（不需瞄准即命中）

## 1. 需求

「直接内存，准星没瞄准敌人也可以打到」。按技术边界拆成两问：能否写游戏内存做 silent aim，能否在输入层做到同等效果。

## 2. 技术边界结论

- 内存级 silent aim 需要篡改 `CUserCmd.viewangles`。外部进程必须满足：`/proc/<pid>/mem` 以 `O_RDWR` 打开（本仓库明确是 `O_RDONLY` 只读架构），并且活体内存中 `CUserCmd` 可定位。当前 schema 只覆盖实体与 netfield，未暴露 `CUserCmd`，也没有可核验的权威偏移。两个前提都不成立，故**不写内存、不臆造偏移**。
- 输入层等效方案：程序自己「贴头 → 合成点击 → 归位」，玩家不需要瞄准，命中判定发生在贴头期间，符合「准星没瞄准也可以打到」。

## 3. 实现

- `src/core/Config.h`：新增 `SilentConfig{enabled, key, holdMs, cooldownMs, fovPixels, teamCheck, maxDistance}`，默认 `key=2`（鼠标中键）、`holdMs=30`。
- `src/core/Config.cpp`：`silent` 节点 load / save。
- `src/features/Features.cpp`：
  - `silentStep()`：复用 `trackStep` 的筛选规则（死亡跳过 / 队友过滤 / 可见优先 / 屏幕最近），`fovPixels=0` 为全屏；
  - 相对位移对齐 `headScreen` → `XTestFakeButtonEvent(1, True)` → `sleep_for(holdMs)` → `Release` → 反向位移归位；
  - 触发后按 `cooldownMs` 冷却。
  - `run()`：静默锁头先于普通扳机，命中本帧则跳过 `triggerStep`，避免一次按键两发。
- `src/features/Features.h`：声明 `silentStep`，新增 `nextSilent_`。
- `src/ui/Menu.cpp` / `Menu.h`：扳机页新增「静默锁头（自动开火）」「过滤队友」、半径 / 停留 / 触发键，`apply()` / `syncFrom()` 同步。
- `config/default.json`：新增 `silent` 节点（默认关闭）。

## 4. 验证结果

- 构建：`cmake --build build -j8` 8/8 成功，0 warning / 0 error。
- 界面（Xvfb `:98`，1280x800x24）：扳机页渲染两行新控件，面板 bbox `420,154 - 858,642`，与加入前尺寸一致（488 高），无裁切、无溢出。
- 配置装载 + 保存往返：把 `build/config.json` 的 `silent` 置 `enabled=true, holdMs=45, fovPixels=500` 后启动 → 点「应用并保存」→ 重读为 `enabled=True, holdMs=45, fovPixels=500, key=2, teamCheck=True`，字段未被覆盖。
- **未验证**：真实命中。需要「触发键按下 + 屏幕内有敌人」的对局时序，当前 CS2 不在对局中；`[silent]` 诊断行（每 50 次）需实战确认。

## 5. 遗留风险

1. 观感为「瞬闪」而非完全无痕：贴头期间视角真实移动 `holdMs`。
2. `holdMs` 太短（<16 ms）可能被服务器取样到归位后角度而打空；太长会明显卡顿特性线程。
3. 仅 X11 + XTest；Wayland 不可用。
4. 与「子弹追踪」同开会互抢准星，建议只开一个。

# 阶段二十五：功能单列 + 右键参数弹层

## 1. 需求

「ui改成一个功能直接站一行然后右键下拉可调参」，随后修正为「还是类别分类 只不过改一个功能站一小行然后右键展开调参」。即：左侧四个类别页保留，页内一个功能一行，参数收进右键弹层。

## 2. 实现

- `src/ui/Menu.h`：恢复 `tabs_[4]` 与 `pages_`（`QStackedWidget`）成员，保留 `popups_` / `popupBuilders_` 与 `makeRow` / `attachParams` / `ensurePopup` / `eventFilter` 声明；`status_`、`logView_` 与全部参数控件指针不变。
- `src/ui/Menu.cpp`：
  - `buildUi()`：header → status → 分隔线 → **顶部横向类别栏**（`QHBoxLayout`、四等分、按钮高 30、`autoExclusive`、`#tab` 常态半透明底 + 选中白底反色）→ `QStackedWidget` 四页 → 分隔线 → hint → 底部「恢 复 上 一 次 / 应 用 并 保 存」。首版把类别按钮竖排在左栏，用户反馈「左上角有一堆按键糊一块」，改为顶部横排铺满一行。
  - `addPage()` 页工厂：每页一个 `QScrollArea`（`setWidgetResizable(true)`、`NoFrame`、横滚 `AlwaysOff`、`setMinimumHeight(330)`）+ 单列 `QVBoxLayout`（`spacing 2`、右边距 6）。`setMinimumHeight(330)` 是为了避免空页把 `QStackedWidget` 压扁（首版曾压到窗口 440x375）。
  - 行分布：透视 13 行（伤害浮字 / 残影轨迹 / 2D 雷达 带参数）、自瞄 3 行（自瞄 / 后坐力抵消 / 子弹追踪）、扳机 2 行（自动扳机 / 静默锁头）、日志页放 `logView_`（`minimumHeight 200`）。
  - `makeRow()`：`QWidget#row` 内 `QCheckBox` + `addStretch(1)` + 右侧 `QLabel#hint`，行高由 `contentsMargins(8,3,8,3)` 决定，实测行距 25 px。
  - `attachParams()`：行与勾选框都设 `Qt::CustomContextMenu` 并连 `customContextMenuRequested` → `ensurePopup(title)->move(row->mapToGlobal(pos))->show()`。
  - `ensurePopup()`：`QWidget(this, Qt::Popup)` + `WA_StyledBackground` + 统一 QSS，标题 + builder 内容 + 「松开即生效」脚注，构建后缓存。
  - `eventFilter()`：弹层 `QEvent::Hide` → `apply()`，右键调完即落盘。
  - QSS 追加 `QPushButton#tab(/(:hover)/(:checked))`、`QWidget#row(/(:hover))`、`QWidget#popup`；移除已无引用的 `QLabel#section`。
- `apply()` / `persist()` / `restoreLast()` / `syncFrom()` 逻辑未改。

## 3. 验证结果

- 构建：`cmake --build build -j8` 成功，0 warning / 0 error。
- 界面（Xvfb `:98`，1280x800x24，`WV_SKIP_OFFSETS=1`）：顶部类别栏横排，窗口 `440x553`；透视页 13 行一次排完（行带 `251-579`，行距 25 px）；选中项为白底按钮 `437-533`，切到「自 瞄」后选中项移到 `541-634`。
- 类别切换：点「自 瞄」选中态移到第二格，右键首行弹出 `258x338` 的自瞄弹层；扳机页 2 行、日志页渲染日志区。
- 右键弹层：伤害浮字 `258x147`、自瞄参数 `258x338`、自动扳机 `258x195`，均未越出窗口/屏幕。
- 弹层落盘：伤害浮字弹层内「显示时长」`800`→`802`，关闭后 `build/config.json` 的 `esp.damageMs=802`；勾选框 `aim.enabled` true→false 经弹层关闭后落盘。
- 无参数行（方框 / 血量条 / 过滤队友等）右键不出弹层，符合预期。

## 4. 遗留风险

1. 只有「弹层关闭」和底部「应用并保存」两条落盘路径；只改勾选框不会自动保存。
2. 透视页 13 行 ≈325 px 已贴近 330 px 下限，再加功能行会出现竖向滚动。
3. `Qt::Popup` 弹层位置跟行绑定，窗口拖到屏幕下缘时可能被裁；当前未做翻转/夹取。
4. Xvfb 下曾残留一个 root 权限的历史实例（`kill` 被拒），它会用旧配置写 `build/config.json`；验证时改用独立 display `:98` 规避。

# 阶段二十六：左上角按键糊成一团的真因修复

## 1. 需求

「左上角有一堆按键湖一块」→ 横排类别栏后仍「还是有」。

## 2. 定位过程

- 先排除叠加层：`Overlay::paintEvent()` 只绘制敌人框 / 血量文字 / 骨骼 / 伤害浮字 / 残影 / 底部居中雷达，顶部无任何绘制。
- 再排除状态文案：`Game::setStatus()` 只有「已连接 pid=…」「偏移缺失: …」等短句。
- Xvfb `:98` 截图 + PIL 逐像素分析：面板 `440x553+419+123`，在 `x 0..35 / y 8..29` 有一块白色圆角矩形和叠在一起的白色字形，位置与任何布局项都不重合。
- 二分验证：用 `WV_HIDE` 临时隐藏 `findChildren<QWidget*>()` 后同一区域变空 → 该内容确由子控件绘制。
- 打印 `mapToGlobal()` 几何后坐实：`espRadarRange_`、`espDamageMs_`、`espDamageColor_`、`aimStrategy_`、`aimSmooth_`、`recoilStrength_`、`trackFov_`、`triggerDelay_`、`silentFov_` 等 25 个参数控件全部 `vis=1`、`local=0,0`、`30 px` 高，叠在面板左上角。
- 根因：这些控件构造时父对象是 `Menu`，而它们只在右键弹层里使用，弹层是首次右键才惰性构建的；在那之前没有任何布局接管它们，Qt 就把它们画在 `(0,0)`。
- 附带发现：本机 `:0` 没有合成器（`xprop -root _NET_WM_CM_S0` 无结果），`WA_TranslucentBackground` 的透明区域会被直接贴屏，画面上同样会留下不可控残影。

## 3. 实现

- `src/ui/Menu.cpp` `buildUi()`：参数控件构造后统一 `setParent(paramHolder)`，`paramHolder` 是 `Menu` 下 `hide()` 的容器；首次右键时 `paramRow()` 把它们加进弹层布局，由 Qt 重新 reparent 到弹层行上。
- `src/ui/Menu.cpp`：去掉 `WA_TranslucentBackground`；`paintEvent()` 先 `fillRect` 不透明底色 `(15,17,19)` 再画 1px 圆角描边；新增 `resizeEvent()` 用 `QPainterPath + setMask(QRegion)` 裁圆角，不依赖合成器。
- `src/ui/Menu.h`：新增 `resizeEvent()` 声明。

## 4. 验证结果

- 构建：`cmake --build build -j8` 成功，0 warning / 0 error。
- 左上角：Xvfb `:98` 截图，`y 0..30 / x 0..70` 区域除 1px 描边外全为底色 `(15,17,19)`，无任何叠字。
- 行表：首行文本带 `y 132..146`，行距 26 px，13 行依次排到 `444..456`，与修复前一致。
- 右键弹层：伤害浮字行 `(519,496)` 右键 → 新窗口 `258x147+519+496`，内含「显示时长」spinbox 与「颜色」combo，控件正常显示在弹层内（reparent 生效）。
- 首次启动无弹层时窗口树只有 `1x1` 叠加层、`440x553` 面板与 Qt Selection Owner 三个窗口，无游离控件窗口。

## 5. 遗留风险

1. 不透明底板：无合成器环境下这是唯一稳的画法，玻璃质感丢失。
2. `paramHolder` 只做托管，不在布局里；若将来把参数控件直接放进页面，需要改回正常父子关系。
3. 行表贴近 330 px 下限的问题未变，再加行仍会出现竖向滚动。

---

# 阶段二十七：左侧分类面板 + iOS 开关 + 毛玻璃 + 行内下拉参数 + 雷达旋转/屏外指示

## 1. 需求

1. 顶部横排类别改为左侧分类列表。
2. 方框勾选框改为 iOS 形制开关。
3. 面板背景做半透明毛玻璃，并随背景画面变化。
4. 参数从独立弹窗改为行内下拉展开。
5. 雷达随视角旋转；视野外敌人在屏幕边缘显示方位箭头。

## 2. 实现

- `src/ui/Menu.cpp` 主体布局：`body` 横向两列，左列 `navColumn`（固定 92 px）竖排四个类别按钮，右列 `content` 承载 `QStackedWidget`；窗口宽 440 → 540。
- 侧栏样式挂在按钮自身 `kNavStyle`：ID 选择器配 `:checked` 时 Qt 会回退原生绘制（选中变系统高亮蓝），必须用内联样式表。
- 新增 `ToggleSwitch : QCheckBox`：34×18 轨道 + 14 px 滑块，`QVariantAnimation` 140 ms `OutCubic` 过渡，`paintEvent` 全自绘，保留 `isChecked/setChecked` 语义，22 个开关零改动替换。
- 毛玻璃两条路径：`compositorRunning()` 探测 `_NET_WM_CM_S0`；有合成器走 ARGB 半透明（背景实时跟随），无合成器退回「全屏快照 + 降采样模糊」。透明底板 `kGlassAlpha = 188`（近白 `252,253,254`），噪点 alpha 2，描边 `rgba(0,0,0,30)`。
- 参数改为行内展开：`makeRow(toggle, name, hint, build)` 返回「功能行 + 隐藏参数卡」单元，点击右侧「参数 ▸」或右键行切换，收起时 `apply()` 落盘。删除 `ensurePopup/attachParams/popups_/popupBuilders_/eventFilter/paramHolder`。
- `src/render/Overlay.cpp` 新增 `viewYaw()`：视图矩阵第三行取反得相机 forward，`atan2` 得 yaw，先做行长度≈1 的正交性自检，不符返回 NaN 由调用方回退固定北向。
- 雷达：`radarRotate` 为真且 yaw 有效时按 `rel = atan2(dy,dx) - yaw` 映射「前方朝上」，圆周顶部画朝向三角。
- 新增 `screenDirection()`：目标相对屏幕中心的方向单位向量，`w < 0`（相机后方）取反。
- 屏外指示：`esp.offscreen` 开启时，画面外敌人在留 46 px 边距的矩形边缘画指向箭头，颜色沿用敌我语义色，透明度按距离衰减。
- 配置：`EspConfig` 新增 `radarRotate{true}` / `offscreen{false}`，`Config.cpp` 读写，`config/default.json` 同步。

## 3. 验证结果

- 构建：`cmake --build build -j` 成功，0 warning / 0 error。
- 面板：截图 `/tmp/n1c.png` 确认左侧分类列表、iOS 开关、白色玻璃底、「屏外指示」行；`/tmp/s3c.png` 确认参数行内展开（显示时长 / 颜色），「残影轨迹」紧随其后。
- 导航选中态：`/tmp/nav4.png` 由系统蓝改为浅灰底 + 白字。
- 毛玻璃：`/tmp/panel_smooth2.png` 白色底板平滑；`/tmp/panel_b.png` 证明 ARGB 路径下背景实时透出。
- **未验证**：雷达旋转跟随与屏外箭头的实际方位。当前无 CS2 进程，`Snapshot` 为空，叠加层不产生绘制。

## 4. 遗留风险

1. yaw 依赖「行主序 + 行向量右乘」的矩阵假设；实机若旋转方向相反，对调 `rel` 的符号或 `sin/cos` 组合即可。
2. 屏外箭头用 head 投影判定，头部接近相机平面时 NDC 不稳定，极小角度下可能有抖动。
3. 雷达半径按水平距离映射，高低差大的地形存在偏差。
4. 无合成器会话只出静态快照模糊，背景不跟随画面。

---

# 阶段二十八：键盘热键 + 实时统计 + 敌人列表 + 日志轮转 + 单元测试

## 1. 需求

1. 触发键只能绑鼠标 Button1~5，需要支持键盘并做录制式绑定。
2. 「采集到了但画不出来」时无法在界面上分辨原因。
3. 远处敌人方框只有几个像素，需要一份紧凑的信息列表。
4. `/tmp/wv_lite.log` 纯追加无上限，长期挂机会占满 `/tmp`。
5. 纯数学与配置读写没有任何自动化验证。

## 2. 实现

- `Features::keyDown()` 取代 `buttonDown()`，统一编码：`0` 不限、`1..5` 鼠标、`>5` 为 X11 keysym。键盘走 `XQueryKeymap` 轮询而非 `XGrabKey`，避免把按键从游戏手里抢走。
- `Menu` 新增 `KeyBindButton`：点击后延迟 180 ms 进入录制（避开触发本次点击的左键释放），按任意键或鼠标中/右键绑定，`Esc` 取消；键名由 `XKeysymToString` 解析，下划线替换为空格。
- `AppState` 增加 `espSeen / espDrawn / espTeamFiltered / espDistanceFiltered`；`Overlay::paintEvent()` 每帧清零后写入，面板 400 ms 轮询显示「采集 N · 方框 N · 队友 N · 距离 N」。
- 屏外指示箭头内侧补距离数字，位置沿方向向量内推 24 px。
- 敌人列表：右侧按距离排序，最多 10 行，颜色随敌我；配置 `esp.enemyList` / `esp.enemyListMax`。
- 诊断日志轮转：打开时记录已有大小，累计写满 64 KB 检查一次，超过 2 MB 直接截断（不重命名，避免白占双份空间）。
- `viewYaw()` / `screenDirection()` 从 `Overlay.cpp` 匿名 namespace 移入 `core/Math.h`，Overlay 只保留绘制。
- 新增 `tests/`：`test_math.cpp`、`test_config.cpp`、`test_util.h`、`test_main.cpp`；`CMakeLists.txt` 增加 `CS2_LITE_BUILD_TESTS` 选项与 `add_test`。

## 3. 验证结果

- `cmake --build build -j` 0 warning / 0 error。
- `ctest --output-on-failure`：`100% tests passed out of 1`。
- 首轮测试挂了 5 条，全部是测试自身期望值算错（把 NDC=1 当成屏幕 3/4 处、`viewYaw` 测试矩阵漏了第三行第三个分量），修正后通过——顺带证明正交性自检确实能拦下异常矩阵。
- 键盘录制：点击后按 `F` 可绑定，面板显示 `F`；鼠标键显示「左键 / 中键 / 右键」。
- 实时统计行：截图 `/tmp/statc.png` 确认「采集 0 · 当前视野内没有实体」。
- 面板「屏外指示」「敌人列表」行渲染正常。
- **未验证**：敌人列表与屏外箭头的游戏内观感（截图时游戏窗口不在前台，叠加层未绘制）。

## 4. 遗留风险

1. `XQueryKeymap` 每个键位每 tick 查询一次（当前最多 5 个），未做单帧缓存；键位增多后应合并为一次查询。
2. `KeyBindButton` 用 180 ms 延迟区分「点击按钮」和「绑定左键」，快速连点仍可能误绑。
3. `Offsets.h` 默认 `entityChunkStride{0x78}` 与 `config/default.json` 的 112 不一致，走默认值时扫不到实体。
4. `Patterns.h` 三条特征码仍为空串，`sig*` 分支不可达。
5. 敌人列表固定在右侧、按屏幕高度 24% 起排，极端分辨率下可能与雷达/屏外箭头重叠。

---

# 阶段二十九：高度差指示 + 雷达距离圈 + F8 显隐面板

## 1. 需求

1. 多层图（Inferno / Nuke）里光看方框分不清敌人在楼上还是楼下。
2. 雷达只有十字准线，圆点的实际距离判断不了。
3. 面板被游戏盖住时必须 Alt+Tab 才能调参。

## 2. 实现

- 高度差：`(player.origin.z - localOrigin.z) × 0.01905` 换成米，超过 0.6 m 才在方框顶部外侧画 ▲（楼上）/ ▼（楼下）并附米数；配置 `esp.heightIndicator`。
- 雷达距离圈：半径三等分，每圈画淡色圆并标对应米数（`radarRange × ring / 3`），圈线与刻度分别用 alpha 26 / 80，不抢玩家点。
- F8 显隐面板：`Features::run()` 每 tick 用 `keyDown()` 查 F8 按下沿（`XQueryKeymap` 全局状态，不依赖窗口焦点），置位 `AppState::panelToggle`；面板 400 ms 轮询消费后 `show()` / `hide()`。
- 面板行表最小高度 330 → 400：透视页加到 16 行后，「屏外指示 / 敌人列表 / 高度差」会被挤进滚动区。

## 3. 验证结果

- `cmake --build build -j` 0 warning / 0 error；`ctest` 100% passed。
- F8 实测：`/tmp/f8a_c.png`（面板在）→ `/tmp/f8b_c.png`（按 F8 消失）→ `/tmp/f8c_c.png`（再按恢复）。
- 面板截图 `/tmp/pv.png` 确认「屏外指示 / 敌人列表 / 高度差」三行一屏可见。
- **未验证**：高度差三角与雷达距离圈的游戏内观感，需要实际对局。

## 4. 遗留风险

1. F8 目前硬编码，未做成可配置键位；后续可复用 `KeyBindButton` 挂到面板设置里。
2. 高度差只比较脚底 z，斜坡/跳跃瞬间会短暂误判为不同层。
3. 雷达距离圈按水平距离标定，与雷达点映射一致，但上下层目标仍会重叠在同一圈内。
4. `panelToggle` 是单标志位，快速连按 F8 会丢掉中间的切换请求（400 ms 轮询窗口）。

---

# 阶段三十：目标锁定

## 1. 需求

自瞄每帧重新选目标，两个敌人贴身时准星会在两者之间来回跳。

## 2. 实现

- `AimbotConfig` 新增 `lockTarget{false}`；`Features` 增加成员 `lockedIndex_{-1}`。
- `aimStep()` 的目标循环里，已锁定目标只要仍通过存活 / 队伍 / 距离 / FOV 过滤就直接续用并 `break`，跳过 metric 比较。
- 循环结束统一更新：无目标时 `lockedIndex_ = -1` 解锁，有目标时记为本次锁定。
- 面板自瞄参数区新增「锁定目标」开关（策略下方）。

## 3. 验证结果

- `cmake --build build -j` 0 warning / 0 error；`ctest` 100% passed。
- 面板截图 `/tmp/lockc.png` 确认「锁定目标」开关已就位，位于「策略」与「平滑」之间。
- **未验证**：贴身多目标时的实际跟随稳定性，需要实战确认。

## 4. 遗留风险

1. 锁定无视 `strategy`：开启后即使策略是「血量最低」，也会一直打第一个锁上的目标，直到它死亡或出 FOV。
2. 目标短暂被遮挡（`m_bSpotted` 为 0）时仍保持锁定，不会切换到可见目标。
3. 实体序号在目标死亡后会被复用，若同一帧内复用给新玩家，可能出现「锁到新人」的错觉；当前依赖 `health <= 0` 过滤来规避。

---

# 阶段三十一：参数防抖保存 + 开关命中修复 + F9 Panic

## 1. 需求

1. 只有键位改完立即保存，其余参数仍要收起参数区或点「应用并保存」才写盘，容易漏。
2. 自绘的 iOS 开关点不动：命中区域仍按 style 里 13 px 指示器计算。
3. F8 只收面板，叠加层与输入合成还在，缺少一键全静默的应急开关。

## 2. 实现

- 防抖保存：`Menu` 新增 `persistTimer_`（单次 400 ms）与 `armPersist()`；`buildUi()` 末尾用 `findChildren` 给全部 `QCheckBox` / `QSpinBox` / `QDoubleSpinBox` / `QComboBox` 挂上，键位按钮的 `onChanged` 也统一走它。
- 回填保护：`syncFrom()` 期间置 `syncing_`，`armPersist()` 直接返回，避免热重载把自己刚读到的值再写回去形成循环。
- 开关命中：`ToggleSwitch` 重写 `hitButton`，改为 `rect().contains(pos)`，不再依赖 style 的 13 px 指示器判定。
- F9 Panic：`Features::run()` 取 F9 按下沿翻转 `AppState::panicMode`；置位时跳过全部输入合成并清零 `aimGate`，`Overlay::tick()` 直接隐藏叠加层。

## 3. 验证结果

- `cmake --build build -j` 0 warning / 0 error。
- 防抖：改开关后 `config.json` 在 400 ms 内更新（用户实测确认可用）。
- F8 之前已验证有效；F9 的按下沿检测与 F8 走同一条链路。
- **未验证**：F9 的实际静默效果。验证时游戏不在前台，叠加层本就不绘制，`[tick]` 恒为 0，无法区分 panic 前后。

## 4. 遗留风险

1. 防抖窗口内直接关程序，最后一次改动可能来不及写盘（窗口关闭时没有 flush）。
2. `findChildren` 会匹配后续动态创建的控件，将来若加入非参数复选框会被误挂。
3. F9 状态不持久化，重启后回到正常模式（符合预期，但没写进配置）。
4. Panic 只停渲染与输入；采样由 Overlay 驱动，叠加层隐藏后 `Game::update` 也不再被调用。

---

# 阶段三十二：HUD 位置可选

## 1. 需求

雷达固定底部居中、敌人列表固定右侧，撞上准星或血条时无法调整。

## 2. 实现

- `EspConfig` 新增 `radarPos{0}` / `listPos{2}`，编码：0 底部居中 / 1 左上 / 2 右上 / 3 左下 / 4 右下。
- 雷达：按 `radarPos` 计算圆心，四角位置留 24 px 边距，顶部额外让出 88 px 避开自身 HUD。
- 敌人列表：按 `listPos` 计算左上角，左下/右下用 `rowsHeight` 反推，避免越界。
- 面板：`posCombo()` 生成统一下拉，分别挂到「2D 雷达 → 位置」与「敌人列表 → 位置」。

## 3. 验证结果

- `cmake --build build -j` 0 warning / 0 error；`ctest` 100% passed。
- 面板截图 `/tmp/p2c.png` 确认「位置」下拉正常展开，五个选项齐全。
- **未验证**：四角摆放的实际观感与遮挡情况，需要游戏内确认。

## 4. 遗留风险

1. 位置是离散五档，不能自由拖动或微调偏移。
2. 四角摆位用的是固定边距，超宽屏/带鱼屏下视觉比例会变化。
3. 左上/右上让出了 88 px 顶部空间，若将来 HUD 增删元素需同步调整。
4. 雷达与敌人列表可以选到同一角，两者不互斥会重叠。
