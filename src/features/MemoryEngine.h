#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "core/Config.h"
#include "game/Game.h"

namespace wv {

// 直接内存引擎：独立工作线程，按配置把六组功能写进游戏进程。
// 与 Features 的输入合成解耦：这里不发送鼠标事件；是否修改相机角由
// bulletLockProtectView / bulletLockSyncView 与 spinKeepView 显式控制。
//
// 写入前置条件：/proc/<pid>/mem 以 O_RDWR 打开，且该功能的偏移已配置。
// 任一条件不满足就静默跳过并在 AppState::memoryMissing 里计数，不猜测布局。
class MemoryEngine {
public:
    MemoryEngine(Game& game, AppState& state);
    ~MemoryEngine();

    void start();
    void stop();

private:
    using Angle3 = std::array<float, 3>;  // QAngle: pitch / yaw / roll

    void run();
    void step(void* display, const Config& config, const Snapshot& snapshot);
    // 退出功能或 Panic 时把改写过的字段写回进入前的值，不留永久痕迹。
    void restore(const Config& config, const Snapshot& snapshot);

    Game& game_;
    AppState& state_;
    std::thread thread_;
    std::atomic_bool running_{false};
    std::chrono::steady_clock::time_point lastStep_{};

    bool eyeSaved_{false};
    Angle3 savedEye_{};
    bool shootSaved_{false};
    Angle3 savedShoot_{};
    bool moveTypeSaved_{false};
    std::uint8_t savedMoveType_{0};
    bool rotationSaved_{false};
    Angle3 savedRotation_{};
    bool absRotationSaved_{false};
    Angle3 savedAbsRotation_{};
    bool maxspeedSaved_{false};
    float savedMaxspeed_{0.f};
    float spinAngle_{0.f};
    int lastTargetDistance_{-1};  // 本帧真子弹追踪选中的目标距离（米），-1 = 无目标
    // 内存角度锁：目标暂时消失时在 holdMs 内继续锁上一帧角度，不恢复、不抖动。
    Angle3 lastLockAngles_{};
    bool lockAnglesValid_{false};
    std::chrono::steady_clock::time_point lastTargetSeen_{};
};

}  // namespace wv
