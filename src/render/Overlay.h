#pragma once
#include <QTimer>
#include <QWidget>

#include <chrono>
#include <cstdint>

#include "core/Config.h"
#include "game/Game.h"

namespace wv {

// X11 透明叠加窗口：覆盖游戏客户区、永在最上、输入穿透。
// 仅在检测到游戏窗口且处于活动状态时显示。
class Overlay : public QWidget {
    Q_OBJECT

public:
    Overlay(Game& game, AppState& state);
    ~Overlay() override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void tick();
    std::uintptr_t findGameWindow() const;
    void applyInputPassthrough();

    Game& game_;
    AppState& state_;
    void* display_{nullptr};
    std::uintptr_t rootWindow_{0};
    std::uintptr_t gameWindow_{0};
    int tickCount_{0};
    int traceTick_{0};
    int drawnCount_{0};
    int windowWidth_{0};
    int windowHeight_{0};
    std::chrono::steady_clock::time_point lastPaint_{};
    QTimer timer_;
};

}  // namespace wv
