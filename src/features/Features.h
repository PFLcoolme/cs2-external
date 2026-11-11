#pragma once
#include <atomic>
#include <chrono>
#include <thread>

#include "core/Config.h"
#include "game/Game.h"

namespace wv {

// 自瞄与扳机：全部输入通过 XTest 扩展合成，不注入游戏进程。
// 参考：XTestFakeRelativeMotionEvent / XTestFakeButtonEvent / XQueryPointer
class Features {
public:
    Features(Game& game, AppState& state);
    ~Features();

    void start();
    void stop();

private:
    void run();
    void aimStep(void* display, const Config& config, const Snapshot& snapshot, int width,
                 int height);
    void triggerStep(void* display, const Config& config, const Snapshot& snapshot);
    // 返回 true 表示本帧已接管准星（找到目标并位移），调用方应跳过自瞄。
    bool trackStep(void* display, const Config& config, const Snapshot& snapshot, int width,
                   int height);
    // 静默锁头：贴头 → 合成点击 → 归位。return true 表示本帧已由静默锁头接管。
    bool silentStep(void* display, const Config& config, const Snapshot& snapshot, int width,
                    int height);
    void recoilStep(void* display, const Config& config, const Snapshot& snapshot, int height);

    static bool gameActive(void* display, std::uintptr_t gameWindow, int pid);
    // 统一按键判定：0 = 不限，1..5 = 鼠标 Button1..5，>5 = X11 keysym（键盘）。
    // 键盘走 XQueryKeymap 轮询，不抓键，游戏侧输入不受影响。
    static bool keyDown(void* display, int code);

    Game& game_;
    AppState& state_;
    std::thread thread_;
    std::atomic_bool running_{false};
    std::chrono::steady_clock::time_point nextTrigger_{};
    std::chrono::steady_clock::time_point nextSilent_{};
    Vec2 lastPunch_{};  // 上一帧的后坐力角度，用于只补偿增量
    int lockedIndex_{-1};  // 当前锁定的实体序号，-1 表示未锁定
    long debugTick_{0};  // 诊断日志节流计数（门控）
    long debugAimTick_{0};  // 诊断日志节流计数（自瞄）
    unsigned int lastMask_{0};  // 上一次指针按键掩码，用于记录物理键位变化
    bool lastTogglePanel_{false};  // F8 的上一帧状态，用来取按下沿
    bool lastPanic_{false};        // F9 的上一帧状态
};

}  // namespace wv
