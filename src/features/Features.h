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

    static bool gameActive(void* display, int pid);
    static bool buttonDown(void* display, int button);

    Game& game_;
    AppState& state_;
    std::thread thread_;
    std::atomic_bool running_{false};
    std::chrono::steady_clock::time_point nextTrigger_{};
};

}  // namespace wv
