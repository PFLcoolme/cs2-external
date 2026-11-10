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
