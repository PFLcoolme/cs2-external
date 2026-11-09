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

    QFont font("DejaVu Sans");
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);

    const float maxDistance = config->esp.maxDistance;
    QPen boxPen;
    boxPen.setWidthF(1.2);

    drawnCount_ = 0;

    for (const PlayerInfo& player : snapshot.players) {
        if (config->esp.teamCheck && player.team == snapshot.localTeam) continue;
        if (maxDistance > 0.f && player.distance > maxDistance) continue;

        QColor color = player.team == 2 ? QColor(230, 162, 60) : QColor(74, 144, 226);
        // 可见性双色：被遮挡目标降低不透明度
        if (config->esp.visibilityCheck && !player.visible) color.setAlpha(100);
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
        }

        if (config->esp.health) {
            const float barWidth = 3.f;
            const float barLeft = left - 6.f;
            const QRectF background(barLeft, top, barWidth, height);
            painter.fillRect(background, QColor(0, 0, 0, 120));

            const float ratio = std::clamp(player.health / 100.f, 0.f, 1.f);
            const QRectF foreground(barLeft, top + height * (1.f - ratio), barWidth, height * ratio);
            painter.fillRect(foreground, QColor::fromHsvF((0.33f * ratio), 0.85f, 0.95f));
        }

        if (config->esp.distance) {
            const QString text = QString::number(player.distance, 'f', 0) + "m";
            painter.setPen(QColor(235, 235, 235));
            painter.drawText(QPointF(player.headScreen.x - 8.f, bottom + 13.f), text);
        }

        if (config->esp.skeleton && !player.skeleton.empty()) {
            painter.setPen(QPen(QColor(255, 255, 255, 170), 1.0));
            for (const auto& segment : player.skeleton) {
                painter.drawLine(QPointF(segment.first.x, segment.first.y),
                                 QPointF(segment.second.x, segment.second.y));
            }
        }
    }

    if (config->aim.enabled) {
        QPen fovPen(QColor(255, 255, 255, 60));
        fovPen.setWidthF(1.f);
        fovPen.setStyle(Qt::DashLine);
        painter.setPen(fovPen);
        painter.drawEllipse(QPointF(width() * 0.5, height() * 0.5),
                            config->aim.fovPixels, config->aim.fovPixels);
    }
}

}  // namespace wv
