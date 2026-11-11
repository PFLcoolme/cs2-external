#pragma once
#include <QPointF>
#include <QTimer>
#include <QWidget>

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>

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
    // 掉血浮字：记录本次掉血量与发生时刻，绘制时按年龄衰减上浮。
    struct DamageMark {
        int amount{0};
        std::chrono::steady_clock::time_point at{};
    };

    // 残影轨迹点：脚底屏幕坐标 + 采样时刻，超出 trailMs 的头部逐帧丢弃。
    struct TrailPoint {
        QPointF at;
        std::chrono::steady_clock::time_point when{};
    };

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
    std::map<int, int> lastHealth_;          // 实体序号 → 上一帧血量
    std::map<int, DamageMark> damageMarks_;  // 实体序号 → 待绘制的掉血浮字
    std::map<int, std::deque<TrailPoint>> trails_;  // 实体序号 → 残影轨迹
    QTimer timer_;
};

}  // namespace wv
