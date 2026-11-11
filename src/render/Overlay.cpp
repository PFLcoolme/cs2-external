#include "Overlay.h"

#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QPen>
#include <QScreen>
#include <QString>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

// X11 与 Qt 的符号冲突：Qt 头文件已在上方解析完毕，这里解除宏污染。
#undef None
#undef Bool
#undef Status
#undef CursorShape
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef FontChange
#undef Expose

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace wv {
namespace {

constexpr int kOverlayIntervalMs = 5;
// 游戏窗口在获得焦点/进入全屏时会重新抬升自身，叠加层必须高频重新置顶，
// 否则会被压在下面（表现为「没有透视」）。
constexpr int kRaiseEveryTicks = 6;
constexpr int kWindowProbeTicks = 10;
const bool kDebugTrace = std::getenv("WV_DEBUG") != nullptr;

long windowPid(Display* display, Window window) {
    Atom atom = XInternAtom(display, "_NET_WM_PID", False);
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    unsigned char* data = nullptr;
    long pid = 0;

    if (XGetWindowProperty(display, window, atom, 0, 1, False, XA_CARDINAL, &type, &format,
                           &count, &after, &data) == Success &&
        data != nullptr) {
        pid = static_cast<long>(*reinterpret_cast<unsigned long*>(data));
        XFree(data);
    }
    return pid;
}

// 递归枚举窗口树，取面积最大的可见匹配窗口（跳过最小化/隐藏的辅助窗口）。
void collectWindows(Display* display, Window parent, int pid, Window* best, long* bestArea) {
    Window root = 0;
    Window parentReturn = 0;
    Window* children = nullptr;
    unsigned int childCount = 0;

    if (!XQueryTree(display, parent, &root, &parentReturn, &children, &childCount)) return;

    for (unsigned int i = 0; i < childCount; ++i) {
        const Window child = children[i];
        if (windowPid(display, child) == pid) {
            XWindowAttributes attributes{};
            if (XGetWindowAttributes(display, child, &attributes) &&
                attributes.map_state == IsViewable) {
                const long area = static_cast<long>(attributes.width) * attributes.height;
                if (area > *bestArea) {
                    *best = child;
                    *bestArea = area;
                }
            }
        }
        collectWindows(display, child, pid, best, bestArea);
    }

    if (children) XFree(children);
}

}  // namespace

Overlay::Overlay(Game& game, AppState& state) : QWidget(nullptr), game_(game), state_(state) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint |
                   Qt::WindowTransparentForInput | Qt::X11BypassWindowManagerHint |
                   Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);

    Display* display = XOpenDisplay(nullptr);
    if (display) {
        display_ = display;
        rootWindow_ = static_cast<std::uintptr_t>(DefaultRootWindow(display));
    }

    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(kOverlayIntervalMs);
    connect(&timer_, &QTimer::timeout, this, &Overlay::tick);
    timer_.start();
}

Overlay::~Overlay() {
    if (display_) XCloseDisplay(static_cast<Display*>(display_));
}

void Overlay::tick() {
    if (!game_.ensureAttached()) {
        state_.gameWindow.store(0);
        windowWidth_ = 0;
        if (isVisible()) hide();
        return;
    }

    Display* display = static_cast<Display*>(display_);
    if (!display) {
        return;
    }

    // 窗口几何低频探测（每 10 tick ≈ 30ms）：XGetWindowAttributes / XTranslateCoordinates
    // 都是同步往返请求，每帧执行会白吃主线程时间。
    const bool probe = windowWidth_ == 0 || ++tickCount_ % kWindowProbeTicks == 0;
    if (probe) {
        const std::uintptr_t found = findGameWindow();
        if (found == 0) {
            gameWindow_ = 0;
            state_.gameWindow.store(0);
            windowWidth_ = 0;
            if (isVisible()) hide();
            return;
        }

        gameWindow_ = found;
        const Window window = static_cast<Window>(found);

        XWindowAttributes attributes{};
        if (!XGetWindowAttributes(display, window, &attributes) ||
            attributes.map_state != IsViewable || attributes.width < 64 ||
            attributes.height < 64) {
            state_.gameWindow.store(0);
            windowWidth_ = 0;
            if (isVisible()) hide();
            return;
        }

        int absoluteX = 0;
        int absoluteY = 0;
        Window child = 0;
        XTranslateCoordinates(display, window, static_cast<Window>(rootWindow_), 0, 0, &absoluteX,
                              &absoluteY, &child);

        const QRect target(absoluteX, absoluteY, attributes.width, attributes.height);
        if (geometry() != target) setGeometry(target);

        windowWidth_ = attributes.width;
        windowHeight_ = attributes.height;
        state_.gameWindow.store(found);
        if (!isVisible()) show();
    }

    game_.update(Vec2{0.f, 0.f}, windowWidth_, windowHeight_);

    // 诊断开关：WV_DEBUG=1 时每 60 tick 输出一次采集/绘制状态。
    if (kDebugTrace && ++traceTick_ % 60 == 0) {
        const Snapshot s = game_.snapshot();
        std::fprintf(stderr, "[tick] %dx%d valid=%d players=%zu drawn=%d\n", windowWidth_,
                     windowHeight_, s.valid ? 1 : 0, s.players.size(), drawnCount_);
    }

    if (tickCount_ % kRaiseEveryTicks == 0) {
        XRaiseWindow(display, static_cast<Window>(winId()));
        XFlush(display);
    }

    // 采样保持 3ms（自瞄需要新鲜数据），重绘限速到 60Hz：整张 1920x1080 半透明表面
    // 每次提交都昂贵，按 333Hz 重绘会吃满 CPU 并引入可见延迟。
    const auto now = std::chrono::steady_clock::now();
    if (now - lastPaint_ >= std::chrono::milliseconds(33)) {
        lastPaint_ = now;
        update();
    }
}

std::uintptr_t Overlay::findGameWindow() const {
    Display* display = static_cast<Display*>(display_);
    if (!display || game_.pid() <= 0) return 0;

    // 缓存失效时重新搜索
    if (gameWindow_ != 0) {
        XWindowAttributes attributes{};
        if (XGetWindowAttributes(display, static_cast<Window>(gameWindow_), &attributes) &&
            windowPid(display, static_cast<Window>(gameWindow_)) == game_.pid()) {
            return gameWindow_;
        }
    }

    Window best = 0;
    long bestArea = 0;
    collectWindows(display, static_cast<Window>(rootWindow_), game_.pid(), &best, &bestArea);
    return best;
}

void Overlay::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    applyInputPassthrough();
}

void Overlay::applyInputPassthrough() {
    Display* display = static_cast<Display*>(display_);
    if (!display) return;
    // 输入区域清空 = 所有点击/移动穿透到下层游戏窗口
    XShapeCombineRectangles(display, static_cast<Window>(winId()), ShapeInput, 0, 0, nullptr, 0,
                            ShapeSet, Unsorted);
    XFlush(display);
}

void Overlay::paintEvent(QPaintEvent* /*event*/) {
    const std::shared_ptr<Config> config = state_.config.load();
    if (!config || !config->esp.enabled) return;

    const Snapshot snapshot = game_.snapshot();
    if (!snapshot.valid) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 数字统一走等宽字体：血量和距离是读数，不是文案，等宽才能不跳动。
    QFont font("DejaVu Sans Mono");
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);

    const float maxDistance = config->esp.maxDistance;
    QPen boxPen;
    boxPen.setWidthF(1.2);

    // 距离分级：15 m 内满亮，60 m 处衰减到 0.45，中间线性。
    // 只用透明度区分远近，不引入第三种颜色，敌我语义色保持不变。
    const auto distanceFade = [](float distance) {
        const float t = std::clamp((distance - 15.f) / (60.f - 15.f), 0.f, 1.f);
        return 1.f - 0.55f * t;
    };

    drawnCount_ = 0;
    const auto now = std::chrono::steady_clock::now();

    // 掉血浮字：按实体序号比对上一帧血量。只记录「掉血」，回血或换人（序号复用）
    // 不产生浮字；超过 damageMs 或目标消失即清理，避免吃内存。
    if (config->esp.damageNumbers) {
        const auto damageWindow = std::chrono::milliseconds(std::max(100, config->esp.damageMs));
        std::map<int, int> current;
        for (const PlayerInfo& player : snapshot.players) {
            current[player.index] = player.health;
            const auto previous = lastHealth_.find(player.index);
            if (previous != lastHealth_.end() && previous->second > player.health) {
                damageMarks_[player.index] = DamageMark{previous->second - player.health, now};
            }
        }
        lastHealth_.swap(current);
        for (auto it = damageMarks_.begin(); it != damageMarks_.end();) {
            const bool expired = now - it->second.at > damageWindow;
            const bool gone = lastHealth_.find(it->first) == lastHealth_.end();
            it = (expired || gone) ? damageMarks_.erase(it) : std::next(it);
        }
    } else {
        damageMarks_.clear();
        lastHealth_.clear();
    }

    // 残影轨迹：每帧记录脚底屏幕坐标，按 trailMs 窗口裁剪；目标消失即整条丢弃。
    if (config->esp.trail) {
        const auto trailWindow = std::chrono::milliseconds(std::max(50, config->esp.trailMs));
        std::map<int, std::deque<TrailPoint>> next;
        for (const PlayerInfo& player : snapshot.players) {
            auto& trail = next[player.index];
            const auto previous = trails_.find(player.index);
            if (previous != trails_.end()) trail = previous->second;
            trail.push_back(TrailPoint{QPointF(player.feetScreen.x, player.feetScreen.y), now});
            while (!trail.empty() && now - trail.front().when > trailWindow) trail.pop_front();
        }
        trails_.swap(next);
    } else {
        trails_.clear();
    }

    // 最低血量目标：只统计本帧真正会绘制的敌人，供描边高亮使用。
    int lowestHealth = 101;
    if (config->esp.highlight) {
        for (const PlayerInfo& player : snapshot.players) {
            if (player.health <= 0 || player.health >= lowestHealth) continue;
            if (config->esp.teamCheck && player.team == snapshot.localTeam) continue;
            if (maxDistance > 0.f && player.distance > maxDistance) continue;
            lowestHealth = player.health;
        }
    }

    for (const PlayerInfo& player : snapshot.players) {
        if (config->esp.teamCheck && player.team == snapshot.localTeam) continue;
        if (maxDistance > 0.f && player.distance > maxDistance) continue;

        const float fade = distanceFade(player.distance);
        QColor color = player.team == 2 ? QColor(230, 162, 60) : QColor(74, 144, 226);
        // 可见性双色：可见 = 实线全亮，被遮挡 = 虚线 + 低不透明度。
        boxPen.setStyle(Qt::SolidLine);
        color.setAlpha(static_cast<int>(std::lround((config->esp.visibilityCheck && !player.visible)
                                                       ? 110.f * fade
                                                       : 255.f * fade)));
        if (config->esp.visibilityCheck && !player.visible) boxPen.setStyle(Qt::DashLine);
        boxPen.setColor(color);
        painter.setPen(boxPen);

        const float top = player.headScreen.y;
        const float bottom = player.feetScreen.y;
        const float height = bottom - top;
        if (height < 6.f || height > 3000.f) continue;

        const float width = height * 0.45f;
        const float left = player.headScreen.x - width * 0.5f;

        if (config->esp.box) {
            painter.drawRect(QRectF(left, top, width, height));
            ++drawnCount_;
            // 准星目标高亮：m_iIDEntIndex 命中谁，就给谁套一圈白框。
            if (player.index == snapshot.crosshairIndex) {
                painter.setPen(QPen(QColor(255, 255, 255, 210), 1.0));
                painter.drawRect(QRectF(left - 3.f, top - 3.f, width + 6.f, height + 6.f));
                painter.setPen(boxPen);
            } else if (config->esp.highlight && player.health == lowestHealth) {
                // 最低血量目标：橙红外框，准星目标仍优先白框，避免两圈叠画。
                painter.setPen(QPen(QColor(255, 96, 96, 200), 1.0));
                painter.drawRect(QRectF(left - 3.f, top - 3.f, width + 6.f, height + 6.f));
                painter.setPen(boxPen);
            }
        }

        if (config->esp.health) {
            const float barWidth = 3.f;
            const float barLeft = left - 6.f;
            const QRectF background(barLeft, top, barWidth, height);
            painter.fillRect(background, QColor(0, 0, 0, 120));

            const float ratio = std::clamp(player.health / 100.f, 0.f, 1.f);
            const QRectF foreground(barLeft, top + height * (1.f - ratio), barWidth, height * ratio);
            painter.fillRect(foreground, QColor::fromHsvF((0.33f * ratio), 0.85f, 0.95f));

            if (config->esp.healthText) {
                painter.setPen(QColor(235, 235, 235, 220));
                painter.drawText(QPointF(barLeft - 21.f, top + 9.f),
                                 QString::number(player.health));
                painter.setPen(boxPen);
            }

            // 护甲读数排在血量下一行；偏移未配置时 armor 为 -1，不绘制。
            if (config->esp.armor && player.armor >= 0) {
                painter.setPen(QColor(150, 180, 212, 220));
                painter.drawText(QPointF(barLeft - 21.f, top + 20.f),
                                 QString::number(player.armor));
                painter.setPen(boxPen);
            }
        }

        if (config->esp.distance) {
            const QString text = QString::number(player.distance, 'f', 0) + "m";
            painter.setPen(QColor(235, 235, 235));
            painter.drawText(QPointF(player.headScreen.x - 8.f, bottom + 13.f), text);
        }

        // 残影轨迹：脚底历史点连成折线，透明度随距离衰减，不干扰实体框。
        if (config->esp.trail) {
            const auto trail = trails_.find(player.index);
            if (trail != trails_.end() && trail->second.size() >= 2) {
                QColor trailColor = color;
                trailColor.setAlpha(static_cast<int>(std::lround(150.f * fade)));
                painter.setPen(QPen(trailColor, 1.2));
                QPointF previous = trail->second.front().at;
                for (std::size_t i = 1; i < trail->second.size(); ++i) {
                    const QPointF current = trail->second[i].at;
                    painter.drawLine(previous, current);
                    previous = current;
                }
                painter.setPen(boxPen);
            }
        }

        // 掉血浮字：damageMs 内由头顶上浮 18 px 并淡出，颜色取配置值。
        if (config->esp.damageNumbers) {
            const auto mark = damageMarks_.find(player.index);
            if (mark != damageMarks_.end()) {
                const auto window = static_cast<float>(std::max(100, config->esp.damageMs));
                const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now - mark->second.at)
                                     .count();
                const float progress = std::clamp(static_cast<float>(age) / window, 0.f, 1.f);
                const int rgb = config->esp.damageColor;
                QColor damageColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF,
                                   static_cast<int>(std::lround(255.f * (1.f - progress))));
                painter.setPen(damageColor);
                painter.drawText(QPointF(player.headScreen.x - 10.f, top - 8.f - 18.f * progress),
                                 "-" + QString::number(mark->second.amount));
                painter.setPen(boxPen);
            }
        }

        if (config->esp.skeleton && !player.skeleton.empty()) {
            const QColor boneColor(255, 255, 255, static_cast<int>(std::lround(170.f * fade)));
            painter.setPen(QPen(boneColor, 1.0));
            for (const auto& segment : player.skeleton) {
                painter.drawLine(QPointF(segment.first.x, segment.first.y),
                                 QPointF(segment.second.x, segment.second.y));
            }
            // 关节圆点：每段端点画小圆，让骨骼有落点而不是一堆悬空线。
            painter.setBrush(boneColor);
            painter.setPen(Qt::NoPen);
            for (const auto& segment : player.skeleton) {
                painter.drawEllipse(QPointF(segment.first.x, segment.first.y), 1.8, 1.8);
                painter.drawEllipse(QPointF(segment.second.x, segment.second.y), 1.8, 1.8);
            }
            painter.setBrush(Qt::NoBrush);
        }
    }

    // ---- 2D 雷达：固定北向，底部居中。世界 +X 向右、+Y 向上，
    // 屏幕 y 轴向下因此取负号；半径按 radarRange 米线性映射。 ----
    if (config->esp.radar) {
        const float radius = 92.f;
        const QPointF center(static_cast<float>(width()) * 0.5f,
                             static_cast<float>(height()) - radius - 56.f);
        painter.setPen(QPen(QColor(230, 162, 60, 70), 1.0));
        painter.setBrush(QColor(12, 14, 18, 110));
        painter.drawEllipse(center, radius, radius);
        painter.setBrush(Qt::NoBrush);

        painter.setPen(QPen(QColor(230, 162, 60, 45), 1.0));
        painter.drawLine(QPointF(center.x() - radius, center.y()),
                         QPointF(center.x() + radius, center.y()));
        painter.drawLine(QPointF(center.x(), center.y() - radius),
                         QPointF(center.x(), center.y() + radius));

        const float range = std::max(5.f, config->esp.radarRange);
        for (const PlayerInfo& player : snapshot.players) {
            if (maxDistance > 0.f && player.distance > maxDistance) continue;
            if (player.distance > range) continue;

            float dx = player.origin.x - snapshot.localOrigin.x;
            float dy = player.origin.y - snapshot.localOrigin.y;
            if (dx == 0.f && dy == 0.f) continue;
            const float length = std::sqrt(dx * dx + dy * dy);
            dx /= length;
            dy /= length;

            const float scaled = radius * (player.distance / range);
            const QPointF dot(center.x() + dx * scaled, center.y() - dy * scaled);
            painter.setPen(Qt::NoPen);
            painter.setBrush(player.team == 2 ? QColor(230, 162, 60) : QColor(74, 144, 226));
            painter.drawEllipse(dot, 3.0, 3.0);
        }

        // 自身位置
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(235, 235, 235));
        painter.drawEllipse(center, 2.5, 2.5);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QColor(160, 166, 175));
        painter.drawText(QPointF(center.x() - radius, center.y() + radius + 14.f),
                         QString::number(static_cast<int>(range)) + "m");
    }

    if (config->aim.enabled) {
        // 门控可视化：开镜 + 开火同时按住时圆环转绿，松开回到白色虚线。
        const bool gateHeld = state_.aimGate.load() != 0;
        QPen fovPen(gateHeld ? QColor(96, 220, 130, 190) : QColor(255, 255, 255, 60));
        fovPen.setWidthF(gateHeld ? 1.6 : 1.0);
        fovPen.setStyle(Qt::DashLine);
        painter.setPen(fovPen);
        painter.drawEllipse(QPointF(width() * 0.5, height() * 0.5),
                            config->aim.fovPixels, config->aim.fovPixels);
    }
}

}  // namespace wv
