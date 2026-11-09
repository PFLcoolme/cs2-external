# CS2 外部辅助 — 项目规划文档（Linux）

## 1. 需求目标

Linux 原生 CS2 的外部（external）辅助：

- 透视：方框、血量条、距离、骨骼连线，叠加在游戏画面上且不拦截鼠标。
- 自瞄：按住指定鼠标键，按屏幕像素偏移向目标头部平滑移动。
- 扳机：准星命中敌人时合成左键点击。
- 控制面板：实时开关与参数调整，持久化 JSON。

全程只读目标进程内存，输入通过 XTest 合成；**不注入、不 ptrace 附加调试器、不写游戏内存**。

## 2. 技术栈

| 项 | 选型 |
|---|---|
| 语言 | C++20（`std::atomic<std::shared_ptr<>>` 需要） |
| 编译器 | GCC 16 / Clang 18+ |
| GUI / 叠加层 | Qt 6 Widgets（X11 后端） |
| 构建 | CMake ≥ 3.21 + Ninja，`AUTOMOC` |
| 系统库 | libX11、libXext（XShape）、libXtst（XTest） |
| JSON | Qt `QJsonDocument`，无第三方依赖 |

## 3. 实现方案

### 3.1 分层

```
src/
  core/     Memory(/proc 读取)  Math(Vec2/Vec3/Mat4/w2s)  Offsets  Config
  game/     Game(实体遍历 → Snapshot)
  render/   Overlay(X11 透明叠加窗口，QPainter)
  features/ Features(自瞄 + 扳机工作线程，XTest)
  ui/       Menu(Qt 控制面板)
  Main.cpp
```

### 3.2 数据获取

1. 遍历 `/proc/*/comm` 与 `/proc/*/exe` 定位 `cs2`，`open("/proc/<pid>/mem", O_RDONLY)`。
2. `pread64` 读取任意地址；`/proc/<pid>/maps` 解析 `libclient.so` 的 `start - offset` 得模块基址，最大 `end - 最小 start` 得大小。
3. 偏移来源（按优先级）：
   - `offsetsFile` 指向 cs2-dumper（a2x）输出的 `offsets.json`，启动时导入；
   - `config.json` 内嵌 `offsets` 段的静态偏移；
   - `src/core/Patterns.h` 特征码常量 + RIP 相对寻址 `目标 = 命中地址 + 7 + disp32`（分 8MB 分块扫描，避免整模块驻留内存）。
4. 实体列表分块：`chunk = read(list + chunkBase + ptrStride*(i>>9))`，`entity = read(chunk + 0x78*(i & 0x1FF))`。
5. 过滤：`m_iHealth ∈ (0,100]`、`m_lifeState == 0`、`m_iTeamNum ∈ {2,3}`。
6. 头部/骨骼：`m_pGameSceneNode` → `m_modelState` → 骨骼数组 + `bone * boneStride`（默认 `CTransform` 布局）。
7. 投影：`dwViewMatrix` 4x4 行主序 w2s。

### 3.3 叠加层

`Qt::FramelessWindowHint | Tool | WindowStaysOnTopHint | WindowTransparentForInput | X11BypassWindowManagerHint`，`WA_TranslucentBackground`；`showEvent` 时用 `XShapeCombineRectangles(ShapeInput, 0 rect)` 清空输入区域实现点击穿透，周期性 `XRaiseWindow` 保持置顶。几何每 3ms 对齐游戏窗口客户区（`XGetWindowAttributes` + `XTranslateCoordinates`）。

### 3.4 输入

- 自瞄：`XTestFakeRelativeMotionEvent`，位移 = 屏幕像素偏移 × 灵敏度系数 ÷ 平滑步数。
- 扳机：`XTestFakeButtonEvent(1, True/False)`，按住 `delayMs` 后释放，冷却 `cooldownMs`。
- 按键判定：`XQueryPointer` 读取按键掩码（配置项 `key`：1=左键、2=中键、3=右键）。

### 3.5 偏移自动化与可见性改造

| 改造 | 实现 |
|---|---|
| 偏移导入 | `core/OffsetsImporter`：递归解析 JSON 全部层级，`0x` 字符串 / 数字 / `{"value": N}` 三种取值；同名键按类作用域择优（`C_CSPlayerPawn` > `C_BaseEntity`/`CGameSceneNode`/`CSkeletonInstance` > `C_BaseModelEntity`/`CModelState` > 其他） |
| 特征码集中 | `core/Patterns.h` 常量表（`kEntityList`/`kLocalPlayerPawn`/`kViewMatrix`），`config.json` 的 `sig*` 非空时覆盖常量 |
| 可见性检测 | 读 `m_bSpotted`（1 字节）；`esp.visibilityCheck` 开启时被遮挡目标方框/血量颜色降低不透明度至 100 |
| 界面反馈 | 控制面板底部显示「偏移来源: …」实际导入结果与导入条数 |

### 3.6 Linux 偏移生成器（tools/cs2_linux_offsets.py）

Linux 版游戏模块是 ELF（`libclient.so`），公开 dumper（a2x/cs2-dumper）的 PE64 + memflow 方案无法在本机运行，其发布数据也与本机版本（ClientVersion 2000942 / 2026-11-04）不一致。因此自带一个只读生成器：

| 环节 | 实现 |
|---|---|
| 进程访问 | `os.pread` 读 `/proc/<pid>/mem`（O_RDONLY），无需 root；模块范围取自 `/proc/<pid>/maps` |
| 全局偏移 | 在 `libclient.so` 可执行段内扫描 Linux 版特征码，支持 `${'}`（rip 相对 disp32，下条指令 +4）等语义，解析为模块 RVA |
| 字段偏移 | 通过 `libschemasystem.so` 的 `SchemaSystem_001` 接口实例遍历运行时 SchemaSystem，按 `libclient.so` scope 取 netfield 偏移 |
| 输出 | `config/offsets.json`（扁平键，直接对接 `OffsetsImporter`） |
| 校验 | 生成后回读：实体列表指针是否为堆地址、pawn 可读、health 在 1..100、view matrix 非退化 |

每次游戏更新后重跑 `python3 tools/cs2_linux_offsets.py` 即可刷新偏移；游戏必须处于运行状态。

## 4. 权限要求

`/proc/<pid>/mem` 读取受 Yama `ptrace_scope` 限制。任选其一：

```bash
sudo sysctl -w kernel.yama.ptrace_scope=0   # 临时放开
sudo ./build/cs2_lite                        # 以 root 运行
```

## 5. 风险与对策

| 风险 | 对策 |
|---|---|
| 游戏更新导致偏移失效 | 全部偏移/骨骼布局/分块参数外置 JSON，支持特征码自动解析 |
| 骨骼布局不确定 | `boneStride/bonePosX/Y/Z` 可配；3x4 布局取 `0x30 / 0x0C / 0x1C / 0x2C` |
| 叠加层抢焦点或挡点击 | `WS_EX` 等价方案：`X11BypassWindowManagerHint` + 空输入形状 + `WA_TransparentForMouseEvents` |
| 独占全屏不出图 | 仅支持窗口化/无边框全屏（X11 合成器限制） |
| 高频重绘占用 CPU | 3ms 定时器、仅前台显示、关闭多余渲染提示 |
| 反作弊 | 仅用于离线/自建服务器研究，无任何规避设计 |

## 6. 验收

1. `cmake -S . -B build -G Ninja && cmake --build build` 零警告零错误。✅ 已通过
2. 启动后控制面板显示，状态栏输出进程/偏移状态。✅ 已通过（截图确认）
3. 填入当前版本偏移后进入游戏：方框贴合目标、自瞄按键生效、扳机命中开火。⏳ 需游戏进程与偏移
4. 改 `config.json` 后重启生效，无需重编译。✅ 设计如此
5. `offsetsFile` 指向 cs2-dumper `offsets.json` 时，界面「偏移来源」显示导入条数，键值映射正确。✅ 已用样例文件验证（12 项）
6. 开启「可见性双色」后被遮挡目标降透明度，关闭时统一颜色。⏳ 需游戏进程验证

## 7. 依赖与可追溯来源

| 依赖 / 设计点 | 来源 |
|---|---|
| `/proc/<pid>/mem` + `pread64` 读取 | Linux `proc(5)`、`pread(2)` |
| 实体分块（`0x10` 块指针表、`0x78` 实体步长、`>>9` / `&0x1FF`） | a2x/cs2-dumper schema 与 Source 2 实体系统公开分析 |
| `offsets.json` 字段命名 | a2x/cs2-dumper 输出格式（`client.dll` / `C_*` 类分层） |
| 叠加层输入穿透 | `XShapeCombineRectangles(ShapeInput, …)`，Xext 官方文档 |
| 鼠标合成 | XTest `XTestFakeRelativeMotionEvent` / `XTestFakeButtonEvent` |
| 参考项目分层 | Osiris（MIT）、TKazer/CS2_External、Exlodium/CS2-External-Base |
