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
    AimbotConfig aim;
    RecoilConfig recoil;
    TrackConfig track;
    SilentConfig silent;
    TriggerConfig trigger;
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

    // 1 = 请求切换面板显隐（Features 线程检测热键，主线程消费后清零）。
    std::atomic<int> panelToggle{0};

    // 1 = Panic 模式：叠加层不绘制、所有输入合成停止。按 F9 切换。
    std::atomic<int> panicMode{0};
};

}  // namespace wv
