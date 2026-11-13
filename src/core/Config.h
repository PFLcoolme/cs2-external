#pragma once
#include <atomic>
#include <memory>
#include <string>

#include "Offsets.h"
#include "OffsetsImporter.h"

namespace wv {

struct AimbotConfig {
    bool enabled{false};
    // 触发条件：开镜键与开火键同时按住才执行；0 = 不要求该按键
    int scopeKey{3};  // X11 键码：3 = 鼠标右键（开镜）
    int fireKey{1};   // 1 = 鼠标左键（开火）
    float fovPixels{220.f};
    int smooth{5};
    float sensitivityFactor{0.45f};
    bool teamCheck{true};
    float maxDistance{0.f};  // 0 = 不限
    // 目标选择：0 = 屏幕最近，1 = 血量最低，2 = 世界最近
    int strategy{0};
    // 锁定当前目标直到它死亡或离开 FOV，避免贴身多目标时准星来回跳
    bool lockTarget{false};
};

struct RecoilConfig {
    bool enabled{false};
    // 补偿强度：正负决定方向。鼠标位移 = (-punch.y, +punch.x) * 像素/度 * strength，
    // 若实机方向相反，把本值改成负数即可，无需改代码。
    float strength{0.6f};
    // 单帧补偿角上限：超过视为数据异常（换枪 / 瞬移 / 读错指针）直接跳过。
    float maxDegrees{12.f};
    bool requireFire{true};  // 只在开火键按住时补偿
};

struct TrackConfig {
    bool enabled{false};
    // 触发键：按住即把准星直接贴到最近敌人头部；0 = 不要求按键。
    int key{1};  // 1 = 鼠标左键（开火）
    // 搜索半径（屏幕像素）：0 = 全屏，不看准星附近，任意位置开火都会拉过去。
    float fovPixels{0.f};
    // 1 = 单帧贴合；>1 分摊到多帧，视觉上更像手动甩枪。
    int smooth{1};
    bool teamCheck{true};
    float maxDistance{0.f};  // 0 = 不限
};

// 静默锁头：按住触发键时本进程自己「贴头 + 点击 + 归位」，
// 玩家全程不需要把准星放到敌人身上，开火瞬间鼠标只在头部停留 holdMs。
struct SilentConfig {
    bool enabled{false};
    int key{2};           // 触发键，2 = 鼠标中键
    int holdMs{30};       // 贴头停留时长，需覆盖一个游戏 tick（64 tick ≈ 16 ms）
    int cooldownMs{120};
    float fovPixels{0.f}; // 0 = 全屏
    bool teamCheck{true};
    float maxDistance{0.f};
};

struct TriggerConfig {
    bool enabled{false};
    int key{0};  // 0 = 不使用按键（仅准星判定）
    int delayMs{30};
    int cooldownMs{120};
    bool teamCheck{true};
};

// 直接内存写入的五组功能。全部默认关闭；任一偏移未配置时对应功能静默跳过。
// 与 XTest 路径的区别：这里改的是游戏进程里的字段，不移动准星、不合成输入。
struct MemoryConfig {
    // 加速：写 m_flVelocityModifier 倍率（幂等）。该偏移未配置时功能跳过并计入缺项。
    bool speed{false};
    float speedFactor{1.5f};

    // 子弹追踪内存锁：不看准星、不看按键、不看 FOV，每帧把射击结算角
    // (m_angEyeAngles + m_angStashedShootAngles) 强制锁到最近敌人头部。
    // 本地相机角 v_angle 全程不写，视角与准星纹丝不动。
    // 清 m_angEyeAnglesVelocity / m_arrOldEyeAngles 防止游戏用历史角回滚。
    bool bulletTrack{false};
    int bulletTrackKey{0};           // 0 = 常驻锁（默认）；>0 时才要求该键按住
    float bulletTrackMaxDistance{0.f};  // 0 = 不限（米）
    bool bulletTrackTeamCheck{true};
    int bulletLockHoldMs{300};       // 目标暂时消失后继续锁住上一帧角度的时长
    // 写 m_angEyeAngles 会联动 v_angle（实机同源）。每帧把 v_angle 写回玩家
    // 真实相机角，压住联动 → 视角纹丝不动，同时射击角仍锁在目标上。
    bool bulletLockProtectView{true};
    // 命中优先：把 v_angle 一起写成锁定角，让下一条 usercmd 视角与射击角一致，
    // 服务器才会按锁定方向结算。代价是相机跟着目标转（视角会动）。
    // 关着 = 视角不动，但服务器可能按玩家真实朝向判 miss →「打中没伤害」。
    bool bulletLockSyncView{false};

    // 飞天：按住上升键写 m_vecVelocity.z，可选同时切 m_MoveType。
    bool fly{false};
    int flyUpKey{0x20};              // XK_space
    int flyDownKey{0};               // 0 = 不启用下降
    float flySpeed{250.f};           // Source 单位/秒
    int flyMoveType{0};              // 0 = 只改垂直速度；>0 时写入 m_MoveType 的值

    // 身体旋转：只写 m_angRotation 的 yaw，相机角与射击角都不动。
    bool spin{false};
    float spinDegreesPerSec{180.f};
    // m_angAbsRotation 是渲染用的世界旋转，写它会带动画面（视角一直在动）。
    // 默认只写 m_angRotation（模型本地旋转），保证视角不变。
    bool spinKeepView{true};

    // 真无后坐力：直接把 punch 角度写零，比 XTest 反向补偿更彻底。
    bool noRecoil{false};
    // 弹着点收成一点。安全模式（默认开）只清两个纯视模动画字段：
    // m_flFlinchStack（受击抖动）与 m_flLastShakeTime（枪口抖动时间戳）。
    bool accuracy{false};
    // 关掉安全模式才会清散布预测组：m_flInterpolatedInaccuracy、
    // m_vClientScopeInaccuracy、m_iShotsFired、m_fAccuracyPenalty、
    // m_fAccuracySmoothedForZoom、m_flTurningInaccuracy(+Delta)、后坐力索引。
    // 这些是客户端算散布的输入，清掉 = 本地预测以为必中、服务器按真实散布
    // 判 miss，表现正是「打中却没伤害」。默认必须保持开启。
    bool accuracySafeMode{true};
};

struct EspConfig {
    bool enabled{true};
    bool box{true};
    bool health{true};
    bool healthText{true};  // 血量数值文字
    bool armor{false};      // 护甲数值文字（需 m_ArmorValue 偏移）
    bool damageNumbers{false};  // 血量掉血浮字
    bool distance{true};
    bool skeleton{false};
    bool radar{false};  // 2D 雷达（底部居中）
    bool radarRotate{true};  // 雷达随视角旋转（false = 固定北向）
    float radarRange{40.f};  // 雷达半径对应的世界距离（米）
    bool offscreen{false};   // 视野外敌人：屏幕边缘画箭头指方位
    bool enemyList{false};   // 右侧敌人列表：按距离排序，补远处方框看不清
    int enemyListMax{5};     // 列表最多显示行数
    bool heightIndicator{false};  // 目标与本地的高度差（▲ 楼上 / ▼ 楼下）
    bool crosshairInfo{false};    // 准星指向的敌人：在准星旁显示血量与距离
    // 战绩 HUD：把本局命中 / 伤害 / 击杀直接画在叠加层上，配合 resetStatsKey 清零。
    bool statsHud{false};
    int statsHudPos{1};  // 复用下面的 HUD 位置编码，默认左上
    // HUD 位置：0 底部居中 / 1 左上 / 2 右上 / 3 左下 / 4 右下
    int radarPos{0};
    int listPos{2};
    bool trail{false};       // 残影轨迹：脚底屏幕坐标的历史折线
    int trailMs{220};        // 轨迹保留时长（毫秒）
    bool highlight{false};   // 最低血量目标描边（准星目标仍用白框）
    int damageMs{800};       // 掉血浮字保留时长（毫秒）
    int damageColor{0xFF8460};  // 掉血浮字颜色（0xRRGGBB）
    bool teamCheck{false};
    bool visibilityCheck{true};
    float maxDistance{0.f};
};

struct Config {
    std::string process{"cs2"};
    std::string module{"libclient.so"};
    // 可选的 cs2-dumper 输出文件；导入后覆盖 offsets 段
    std::string offsetsFile;
    std::string offsetsSource{"config.json 内嵌"};
    Offsets offsets;
    // 全局快捷键（X11 keysym）：F8 显隐面板、F9 一键静默、F10 清零战绩。面板可录制改键。
    int togglePanelKey{0xFFC5};
    int panicKey{0xFFC6};
    int resetStatsKey{0xFFC7};
    AimbotConfig aim;
    RecoilConfig recoil;
    TrackConfig track;
    SilentConfig silent;
    TriggerConfig trigger;
    MemoryConfig memory;
    EspConfig esp;

    static Config load(const std::string& path);
    void save(const std::string& path) const;
};

// 跨线程共享：配置以整体替换的方式交换，读取方永远拿到不可变副本。
struct AppState {
    std::atomic<std::shared_ptr<Config>> config{nullptr};
    std::atomic<std::uintptr_t> gameWindow{0};
    // 1 = 开镜与开火同时按住（自瞄门控状态），叠加层据此给 FOV 圈着色。
    std::atomic<int> aimGate{0};

    // 叠加层每帧统计，供面板显示「采集 / 绘制 / 过滤」——
    // 没有这组数就只能靠 WV_DEBUG 日志判断「是没敌人还是被过滤了」。
    std::atomic<int> espSeen{0};
    std::atomic<int> espDrawn{0};
    std::atomic<int> espTeamFiltered{0};
    std::atomic<int> espDistanceFiltered{0};

    // 命中统计：从进程启动累计。掉血检测与浮字开关解耦，关掉浮字也照样计数。
    std::atomic<int> espHits{0};
    std::atomic<int> espDamage{0};
    std::atomic<int> espKills{0};

    // 1 = 请求切换面板显隐（Features 线程检测热键，主线程消费后清零）。
    std::atomic<int> panelToggle{0};
    // 1 = 请求把命中 / 伤害 / 击杀计数清零（Features 置位，面板消费后清零）。
    std::atomic<int> statsReset{0};

    // 1 = Panic 模式：叠加层不绘制、所有输入合成与内存写入停止。按 F9 切换。
    std::atomic<int> panicMode{0};

    // 直接内存引擎状态：writable = /proc/<pid>/mem 以 O_RDWR 打开；
    // missing = 本次采样发现的缺失偏移项数；writes = 累计成功写入次数。
    std::atomic<int> memoryWritable{0};
    std::atomic<int> memoryMissing{0};
    std::atomic<int> memoryWrites{0};
};

}  // namespace wv
