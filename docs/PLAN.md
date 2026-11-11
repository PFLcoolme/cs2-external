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

## 4. 阶段十扩展（零新增偏移）

本轮扩展的约束：**不读取任何新偏移**，全部基于 `Snapshot` 已采集字段（`health / team / visible / origin / headScreen / feetScreen / distance`）。

| 功能 | 依赖字段 | 配置项 |
|---|---|---|
| 血量数值 | `health` | `esp.healthText` |
| 可见性双态（实线 / 虚线+低透明） | `visible` + `m_bSpotted` | `esp.visibilityCheck` |
| 2D 雷达（固定北向） | `origin`、`localOrigin`、`distance` | `esp.radar`、`esp.radarRange` |
| FOV 圈门控着色 | `AppState::aimGate`（Features 写入） | 无（跟随 `aim.scopeKey/fireKey`） |
| 目标策略（屏幕最近 / 血量最低 / 世界最近） | `headScreen`、`health`、`distance` | `aim.strategy` |

设计取舍：

- 雷达固定北向而非视角对齐 —— 视角 yaw 需从视图矩阵推导，矩阵行列布局未有官方文档确认，按规范不做推测实现。
- 目标策略在 FOV 内做二次筛选，因此「血量最低」也只在准星附近生效，避免跨图甩枪。
- 面板与叠加层共用一套配色：深炭 `#12141A`、表面 `#171A21`、强调琥珀 `#E0A23C`、T 方 `#E6A23C`、CT 方 `#4A90E2`；读数字体 `DejaVu Sans Mono`。


---

## 5. 阶段十一：面板极简化（黑白双色）

约束：只改 `src/ui/Menu.cpp`、`src/ui/Menu.h`，不触碰采集、偏移、配置字段。

| 项 | 方案 |
|---|---|
| 窗口 | `Qt::FramelessWindowHint` + `WA_TranslucentBackground`，`paintEvent` 画圆角玻璃（14 px 圆角、alpha 216 底板、1 px 白色 30 alpha 描边、顶部高光） |
| 交互 | 自定义拖动（`globalPosition` 差值）、首次居中、右上角 `✕` 关闭 |
| 布局 | 移除 QGroupBox，改为小节标签（10 px，字母间距 3 px）+ 1 px 分隔线；复选框两列；字段行左对齐、数字框固定宽度 |
| 配色 | 黑白双色：白 `#F2F3F5` / `#EDEFF2`、灰阶文字 `#E9EAEC`→`#55595F`，无彩色强调 |
| 叠加层 | 不变。ESP 的 T/CT 双色属于数据语义（区分敌我），不参与面板的黑白收敛 |

---

## 6. 阶段十二：双列表布局

| 项 | 方案 |
|---|---|
| 主体 | `QHBoxLayout` 两列 + 1 px 竖分隔线（白色 14 alpha） |
| 左列 | 透视（复选框两列、雷达行）+ 扳机 |
| 右列 | 自瞄（策略、平滑、范围、灵敏度）+ 偏移提示 + 应用按钮 |
| 宽度 | 322 → 520 px；数字框随列宽收窄（雷达 74 / 扳机 78 / 自瞄 80 px） |
| 不变 | 配置字段、apply 逻辑、黑白双色 QSS、玻璃底板绘制 |

---

## 7. 阶段十三：完全透明背景

| 项 | 方案 |
|---|---|
| 底板 | `paintEvent` 不再填充，只画 1 px 圆角线框（白 56 alpha） |
| 输入控件 | 透明底 + 1 px 边线（白 70 alpha），hover/focus 时 26 alpha 白底 |
| 主按钮 | 描边样式（白 90 alpha），hover 反白 |
| 分隔 | 横向 70 alpha、纵向 52 alpha 的 1 px 线 |
| 可读性 | 文字整体提亮至 `#E9EBEE~#9AA0A6`；依赖合成器，`Depth: 32` ARGB 已确认 |

---

## 8. 阶段十四：左导航 + 右分类页

| 项 | 方案 |
|---|---|
| 结构 | `QHBoxLayout`：`QListWidget#nav`（88 px）+ 1 px 竖线 + `QStackedWidget` |
| 分类 | 透视 / 自瞄 / 扳机，`currentRowChanged → setCurrentIndex`，默认第 0 页 |
| 导航样式 | 透明底灰字 → hover 白 18 alpha → 选中反白 + 深字，圆角 7 px |
| 宽度 | 520 → 440 px |
| 清理 | 删 `sectionLabel()` 与 `QLabel#section`；补 `QListWidget`/`QStackedWidget` include |
| 不变 | 配置字段与 `apply()` 逻辑、黑白双色、全透明仅线框、底部提示与保存按钮 |

---

## 9. 阶段十五：热重载 / 可见性优先 / 偏移守卫

| 项 | 方案 |
|---|---|
| 热重载 | 复用 400 ms `QTimer` + `QFileInfo::lastModified()` 比对 `configStamp_`，变化则 `Config::load` → 替换 `state_.config` → `syncFrom` 回填控件 |
| 写盘一致性 | `apply()` 后同步 `configStamp_`，防止自写触发多余重载 |
| 选靶 | FOV 作为固定门槛；同层内按 strategy 比较，可见目标整层优先（`m_bSpotted`） |
| 偏移守卫 | 检查 6 类关键偏移，缺失时底部改白字加粗并列出缺项 |
| 不实现 | 后坐力补偿（无权威偏移）、雷达扇形（view 矩阵手性无法验证） |

---

## 10. 阶段十六：日志页 / 距离分级 / 骨骼关节 / 触发抖动

| 项 | 方案 |
|---|---|
| 日志页 | `tailOfLog()` 读末尾 8 KB → 末 16 行；仅当日志页可见时刷新 |
| 导航 | 4 个 `QPushButton#tab`（checkable + autoExclusive，88×30）替代 QListWidget，规避视口裁剪 |
| 距离分级 | 透明度 15 m 满亮 → 60 m 0.45，线性；作用于方框 / 骨骼 / 遮挡色 |
| 骨骼关节 | 每段两端 1.8 px 圆点，透明度随距离衰减 |
| 扳机 | 仅可见目标；按压时长 ±20% 随机抖动，下限 1 ms |

---

## 11. 阶段十七：配置保存与「上一次配置」回滚

| 项 | 方案 |
|---|---|
| 备份 | 每次写盘前把现有 `config.json` 复制为 `config.json.bak`（覆盖前先 `QFile::remove`，`QFile::copy` 不覆盖已有文件） |
| 统一落盘 | 抽出 `persist(config, message)`：备份 → `Config::save` → 写入 `state_.config` → 同步 `configStamp_` → 状态栏提示 |
| 回滚 | 底部新增「恢 复 上 一 次」按钮：校验 `.bak` 是可解析的 JSON 对象后 `Config::load` → `syncFrom` → `persist`；因回滚本身也会备份当前配置，再点一次即切回 |
| 布局 | 底部改为 `QHBoxLayout`：回滚按钮拉伸 1、应用按钮拉伸 2，与既有 `#apply` / `#restore` 两套 QSS 对应 |
| 忽略 | `.gitignore` 追加 `config.json.bak` |

---

## 12. 阶段十八：三项内存派生功能（护甲 / 掉血浮字 / 准星目标高亮）

| 项 | 方案 |
|---|---|
| 护甲数值 | 新增可选偏移 `m_ArmorValue`（cs2-dumper 的 `C_CSPlayerPawn` 字段），偏移为 0 时不读取，`PlayerInfo::armor` 保持 -1，叠加层跳过绘制 |
| 掉血浮字 | 不引入新偏移：叠加层按实体序号缓存上一帧 `m_iHealth`，掉血即记录差值 + 时间戳，1 s 内上浮淡出；目标消失或序号复用时清理 |
| 准星高亮 | 复用已有 `m_iIDEntIndex` → `Snapshot::crosshairIndex`，与 `PlayerInfo::index` 相同实体编号空间，命中目标外扩 3 px 白框 |
| 配置 | `esp.armor`、`esp.damageNumbers` 默认 false，面板第 5 行两个复选，`syncFrom` 与 `apply` 同步 |
| 落盘 | 偏移键 `m_ArmorValue` 纳入 `Config` 的 offsets 读写与 `OffsetsImporter` 绑定表，`config/default.json` 补默认值 |

---

## 13. 阶段十九：护甲偏移纳入 schema 生成器

| 项 | 方案 |
|---|---|
| 生成器 | `tools/cs2_linux_offsets.py` 增加 `need_any(["C_CSPlayerPawn","C_CSPlayerPawnBase"], "m_ArmorValue")`，按继承链取第一个命中（字段声明类随版本变化） |
| 解析类 | `wanted` 集合补 `C_CSPlayerPawnBase`，否则基类绑定不会被遍历 |
| 校验 | 本地 pawn 输出增加 `armor=`，与 `health/team` 同批打印；偏移同时写进 `config/offsets.json` |
| 文档 | README 偏移表与 `esp` 配置表补 `m_ArmorValue` / `armor` / `damageNumbers` |

---

## 14. 阶段二十：全功能失效的根因与修复

用户反馈「所有功能都无法使用了」，逐层排查后确认是四条独立缺陷叠加，逐条修复。

| # | 缺陷 | 根因 | 方案 |
|---|---|---|---|
| 1 | 透视 / 方框 / 自瞄全部无输出 | `build/config.json` 的 `offsets` 被写成全 0（1473 字节），`Game` 拿到零偏移后所有读取落空 | `Menu::persist()` 在落盘前拦截三个全局偏移（`dwEntityList` / `dwLocalPlayerPawn` / `dwViewMatrix`）任一为 0 的情况，只提示不写盘；生成器产出的 `config/offsets.json` 通过 `POST_BUILD` 复制到可执行文件目录，`offsetsFile` 的相对路径在 `build/` 启动时也能解析，缺失的偏移可自愈 |
| 2 | 骨骼连线失效 | 生成器把 schema 的 `CSkeletonInstance.m_modelState`（320，CModelState 结构体起点）当成骨骼数组指针偏移写出；实测骨骼数组在 `scene + 0x1c0`（= 448） | 生成器不再输出 `m_modelState`，仅打印说明；`config/offsets.json` 与 `build/config.json` 回填 448 |
| 3 | 面板被整块遮住且点不回前台 | `Qt::FramelessWindowHint \| Qt::Window` 无标题栏，且无置顶 | 补 `Qt::WindowStaysOnTopHint` |
| 4 | 扳机永不触发 | 选靶逻辑用 `m_bSpotted` 做可见性硬门控，而该字段在本版本恒 0 | 移除扳机的 `if (!target->visible) return;`，仅保留队伍检查 |

`Game::resolveOffsets()` 的缺失判定同步放宽：`m_modelState == 0` 不再并入「偏移缺失」，只降级提示「骨骼偏移缺失，连线不可用」，方框与血量照常。

---

## 15. 阶段二十一：后坐力抵消与五项交互增强

| 功能 | 依赖 | 实现要点 |
|---|---|---|
| 后坐力抵消 | `m_aimPunchAngle`（缺失时退回 `m_aimPunchCache`） | `Game` 每帧读 punch 角度进 `Snapshot::aimPunch`；`Features::recoilStep()` 只补偿帧间增量，`maxDegrees` 之外视为脏数据丢弃；强度可为负值以翻转方向 |
| 热键重绑定 | 无 | 面板下拉改写 `aim.scopeKey` / `aim.fireKey` / `trigger.key`；X11 核心协议只暴露 Button1~5，故仅提供 不限/左键/中键/右键 |
| 掉血浮字时长与颜色 | 无 | `esp.damageMs` / `esp.damageColor`，颜色用 4 档预设避免自造取色器 |
| 残血描边 | 无 | `esp.highlight`，取本帧可绘制敌人中的最低血量者画外框；准星目标白框优先 |
| 残影轨迹 | 无 | `esp.trail` / `esp.trailMs`，按实体序号缓存脚底屏幕坐标，窗口外逐点淘汰 |

配置层同步：`Config::RecoilConfig`、`EspConfig` 新增四个字段、`Offsets` 新增两个 punch 字段、`OffsetsImporter` 新增两条绑定、`tools/cs2_linux_offsets.py` 增加 `maybe_any()` 可选探测（未命中不终止生成，只打印 `recoil disabled`）。

面板状态行新增 4 秒驻留：`persist()` 的提示此前会被 400 ms 的连接状态轮询立刻覆盖，用户看不到「已保存」或「后坐力偏移缺失」。

## 16. 阶段二十二：启动自动刷新偏移 + 后坐力链路改为实测字段

需求目标：游戏每次更新后 `dwEntityList` / `dwViewMatrix` / `dwLocalPlayerPawn` 与各 netfield 偏移都会漂移，用户必须手动跑生成器才发现偏移过期。改为客户端启动时自动执行一次生成器，并修正阶段二十一里被证伪的后坐力字段。

实现方式：

| 项 | 变更点 | 说明 |
| --- | --- | --- |
| 启动自动生成 | `src/Main.cpp` 新增匿名命名空间 `refreshOffsets()` / `appendLog()` | 从 `applicationDirPath()` 向上最多 5 层找 `tools/cs2_linux_offsets.py`，`QProcess` 以 `python3` 执行，`waitForStarted(3000)` + `waitForFinished(15000)`；成功则把 `PROJECT/config/offsets.json` 复制到可执行文件目录的 `config/`（先 `remove` 再 `copy`，因为 `QFile::copy` 目标存在会失败） |
| 调用时机 | `Config::load()` 之前 | `load()` 会立刻 `importOffsetsFile()`，晚一步只能吃到旧值 |
| 失败降级 | 任一步失败只写日志 | 未找到脚本 / python3 起不来 / 超时（kill 后收尾）/ 非零退出 → 沿用现有 `config/offsets.json`，不影响启动 |
| 跳过开关 | `WV_SKIP_OFFSETS=1` | 生成器异常或不想等待时跳过 |
| 诊断 | 追加写 `/tmp/wv_lite.log`，前缀 `[offsets]` | 与面板日志页同源，可直接看到「已刷新偏移 / 生成失败 / 生成超时」 |
| 后坐力字段 | `Offsets` 的 `m_aimPunchAngle` / `m_aimPunchCache` → `m_aimPunchAngle`(QAngle 内偏移) + `m_pAimPunchServices`(pawn 上的指针) | 本版本 schema 不存在 `m_aimPunchAngle` / `m_aimPunchCache`；实测链路是 `C_CSPlayerPawn.m_pAimPunchServices`(0x1520) → `CCSPlayer_AimPunchServices.m_predictableBaseAngle`(0x50) |
| 生成器 | `tools/cs2_linux_offsets.py` | `wanted` 增加 `CCSPlayer_AimPunchServices`；输出 `m_pAimPunchServices` / `m_aimPunchAngle`，删除 `m_aimPunchCache`；两者仍走 `maybe_any()`，缺失不终止生成 |

依赖 API：`QProcess::start/waitForStarted/waitForFinished/exitStatus/exitCode`、`QDir::cdUp`、`QFile::copy/remove`、`QCoreApplication::applicationDirPath`、`qEnvironmentVariableIsSet`。

潜在风险：

- 生成器耗时随进程内存增长；实测本机 cs2 在跑时 0.59 s，15 s 上限留足余量。若 15 s 仍超时只会放弃刷新，不阻塞主流程。
- 生成器退出码非 0（cs2 未运行）时保持旧偏移，用户会看到面板状态「未找到进程」，不会静默用错值。
- 复制目标在可执行文件目录（`build/config/offsets.json`），仓库 `config/offsets.json` 保持为生成器直写结果，二者都在 `.gitignore` 之外需注意提交内容。

兼容范围：仅 Linux（`/proc/<pid>/mem`、`python3`）。`WV_SKIP_OFFSETS` 供无 python3 环境或调试用。

## 17. 阶段二十三：子弹追踪（开火锁头）

需求目标：不管准星指向哪里，只要按住触发键，准星立刻拉到最近敌人的头部；持续按住则每帧重新贴合，形成「子弹跟着人走」的效果。

实现方式：

| 项 | 变更点 | 说明 |
| --- | --- | --- |
| 配置 | `TrackConfig`（`enabled` / `key` / `fovPixels` / `smooth` / `teamCheck` / `maxDistance`） | `fovPixels = 0` 表示全屏，不做半径筛选，对应「不管打哪」 |
| 目标选择 | `Features::trackStep()` | 遍历 `Snapshot::players`，跳过死亡与队友，可见者优先、再取屏幕距离最近；`headScreen` 由 `Game` 的 `worldToScreen` 投影（已过滤相机后方） |
| 位移 | `XTestFakeRelativeMotionEvent` 直接写相对位移 | 不乘 `sensitivityFactor`，`smooth = 1` 时单帧贴合；`smooth > 1` 平分到多帧 |
| 互斥 | 先于 `aimStep` 执行并返回 `bool` | 两个都写相对位移会在一帧内互相拉扯，追踪一旦接管本帧就跳过自瞄 |
| 界面 | 自瞄页新增「子弹追踪（锁头）」「过滤队友」、半径（0 显示为「全屏」）、平滑、触发键 | 触发键沿用 `mouseKeyCombo()`，只提供 不限/左键/中键/右键 |

依赖 API：`XTestFakeRelativeMotionEvent`、`XFlush`（libXtst，项目既有依赖）。

潜在风险：

- 外部只读实现无法改写服务端弹道，本质是「开火前把视角钉在头部」；命中判定仍由服务器按客户端视角结算。
- 与后坐力抵消同时开启会互相拉扯：追踪按头部屏幕坐标重定位，后坐力按 punch 增量补偿。建议二选一，或把后坐力强度调低。
- `headScreen` 的屏幕原点由视口尺寸推导，纯全屏下正确；窗口模式会有固定偏移，与既有自瞄一致。

兼容范围：X11 + XTest。Wayland 原生会话下 XTest 不可用（与既有自瞄、扳机同限制）。

## 18. 阶段二十四：静默锁头（不需瞄准即命中）

需求目标：准星完全不对着敌人，只要想开枪就能打到头部。

技术边界：本仓库是外部只读架构（`/proc/<pid>/mem` 以 `O_RDONLY` 打开），不写游戏内存。真正的 silent aim 需要在 `CreateMove` 之后篡改 `CUserCmd.viewangles`，外部实现要求：
1. `mem` 改为 `O_RDWR`（或 ptrace），与「只读」设计冲突；
2. 目标结构在活体内存中可定位且可验证（当前 schema 未暴露 `CUserCmd`，没有权威偏移来源）。

两条都不满足，因此不写内存、不臆造 usercmd 偏移。改为在输入层实现等效效果：**开火瞬间由程序自己把准星落到头部、合成点击、再归位**。

实现方式：

| 项 | 变更点 | 说明 |
| --- | --- | --- |
| 配置 | `SilentConfig`（`enabled` / `key` / `holdMs` / `cooldownMs` / `fovPixels` / `teamCheck` / `maxDistance`） | `key` 默认鼠标中键，避免与真实左键开火冲突 |
| 目标选择 | `Features::silentStep()` | 与 `trackStep` 同一套筛选：死亡跳过、队友过滤、可见优先、屏幕最近 |
| 快照定位 | `XTestFakeRelativeMotionEvent(±offset)` | 相对位移按屏幕像素直接对齐 `headScreen`，不需灵敏度系数 |
| 开火 | `XTestFakeButtonEvent(1, press)` + `holdMs` 后 release | `holdMs` 默认 30 ms，覆盖 64 tick 的 15.6 ms 采样窗口，让游戏一定在贴头状态下处理该次输入 |
| 归位 | 反向相对位移 | 玩家准星回到按下触发键前的朝向，画面只闪一下 |
| 互斥 | 静默锁头优先，命中则跳过 `triggerStep` | 否则同一次按键会打出两发 |

依赖 API：`XTestFakeRelativeMotionEvent`、`XTestFakeButtonEvent`、`XFlush`、`std::this_thread::sleep_for`。

潜在风险：

- 不是内存级 silent aim：视角仍在贴头期间真实移动 `holdMs`，观感是「瞬闪」，不是完全无痕。
- `holdMs` 是命中率与观感的直接权衡：低于一个 tick 可能被取样到归位后的角度而打空。
- 该函数会在特性线程内阻塞 `holdMs`（默认 30 ms），期间自瞄/后坐力暂停；`holdMs` 调的过大（>100）会明显卡顿。
- 与「子弹追踪」同开时两者都抢准星，建议只开一个。

兼容范围：X11 + XTest。


## 19. 阶段二十五：功能单列 + 右键参数弹层

需求目标：类别分类保留（左栏四个类别页），页内改成「一个功能一行」；该功能的参数收进右键弹层，右键点行即可调。

实现方式：

| 项 | 变更点 | 说明 |
| --- | --- | --- |
| 布局 | `Menu::buildUi()` | 类别栏改为顶部 `QHBoxLayout` 横排四等分按钮（`tabs_[4]`，高 30），下方 `QStackedWidget` 四页；每页一个 `QScrollArea`（横滚关闭，最小高 330）内 `QVBoxLayout` 单列行表 |
| 行 | `Menu::makeRow()` | `QWidget#row` + `QCheckBox`（功能开关）+ 右侧 `QLabel#hint`；有参数的行走「右键参数」提示 |
| 弹层 | `Menu::attachParams()` / `ensurePopup()` | `Qt::Popup` 子窗口，行与勾选框都挂 `CustomContextMenu`；首次右键惰性构建并缓存到 `QHash<QString, QWidget*> popups_`，之后再右键只 `move/show/raise` |
| 落盘 | `Menu::eventFilter()` | 监听弹层 `QEvent::Hide` → `apply()`；弹层一关即写 `config.json`，不必回底部点保存 |
| 控件归属 | `buildUi()` | 所有参数控件在构造期一次创建（父为 `Menu`），由弹层布局接管；`apply()` / `syncFrom()` 字段对应关系不变 |
| 面板底板 | `Menu::paintEvent()` / `resizeEvent()` | 本机 X 会话无合成器（`_NET_WM_CM_S0` 缺失），ARGB 半透明窗口的透明区会直接贴屏成未初始化像素；改为不透明底色 + `setMask()` 圆角裁切 |
| 参数控件归属 | `buildUi()` | 只存在于弹层里的 `QSpinBox` / `QDoubleSpinBox` / `QComboBox` / `QCheckBox` 先收进隐藏容器 `paramHolder`，避免首次右键前堆在面板 `(0,0)` 画成一片 |

依赖 API：`QScrollArea`、`QWidget(Qt::Popup)`、`QEvent::Hide`、`QWidget::customContextMenuRequested`、`QWidget::mapToGlobal`、`QWidget::setMask()` / `QRegion`。

潜在风险：

- `Qt::Popup` 在部分合成器下会单独占一个覆盖窗口；无合成器的 X11 为普通子窗口，焦点丢失由 Qt 自己处理。
- 弹层关闭才落盘：只改勾选框、既不开弹层也不点「应用并保存」时不会写入，底部按钮仍保留为兜底路径。
- 透视页 13 行约 325 px，刚好在 330 px 内；再增加功能行时该页会出现竖向滚动。
- 不透明底板牺牲了玻璃质感，换取无合成器环境下的可读性；若日后确认会话有合成器，可再恢复半透明分支。

兼容范围：Qt6 Widgets（X11 / Wayland 均可，弹层为纯 Qt 控件，不依赖 XTest）。

## 20. 阶段二十六：左上角控件堆叠修复

需求目标：面板左上角出现「一堆按键糊一块」的残影，横排类别栏后仍未消失，需要定位真因并修掉。

实现方式：

| 项 | 变更点 | 说明 |
| --- | --- | --- |
| 参数控件 | `Menu::buildUi()` | 只在右键弹层里使用的 25 个参数控件，构造后统一 `setParent(paramHolder)`（`Menu` 下隐藏容器），首次右键由 `paramRow()` 重新挂到弹层行上 |
| 面板底板 | `Menu::paintEvent()` + `Menu::resizeEvent()` | 去掉 `WA_TranslucentBackground`，改为不透明 `(15,17,19)` 填充 + 1px 圆角描边；圆角用 `QPainterPath` → `setMask(QRegion)` 裁切 |
| 声明 | `Menu.h` | 新增 `resizeEvent()` 覆写 |

依赖 API：`QWidget::setParent()`、`QWidget::setMask()`、`QRegion`、`QPainterPath::addRoundedRect()`。

潜在风险：

- 底图不再透明，玻璃质感消失；本机无合成器（`_NET_WM_CM_S0` 缺失），透明反而会露出未初始化像素。
- 参数控件由隐藏容器托管，若后续把它们直接放进页面需改回常规父子关系。
