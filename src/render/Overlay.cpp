#include "Overlay.h"

#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
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
#include <limits>
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
    // Panic：立刻收掉叠加层，不等连接状态判断。
    if (state_.panicMode.load() != 0) {
        state_.gameWindow.store(0);
        windowWidth_ = 0;
        if (isVisible()) hide();
        return;
    }

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
        std::fprintf(stderr, "[tick] %dx%d valid=%d players=%zu drawn=%d localTeam=%d\n",
                     windowWidth_, windowHeight_, s.valid ? 1 : 0, s.players.size(), drawnCount_,
                     s.localTeam);
        // 逐玩家输出：队伍/血量/距离/投影坐标。feet 为 (0,0) 说明投影失败，
        // 配合 [yaw] 可直接判断是过滤问题还是矩阵问题。
        for (const PlayerInfo& p : s.players) {
            std::fprintf(stderr, "  [p] idx=%d team=%d hp=%d dist=%.1f feet=(%.0f,%.0f)\n", p.index,
                         p.team, p.health, p.distance, p.feetScreen.x, p.feetScreen.y);
        }
        const float yaw = viewYaw(s.view);
        std::fprintf(stderr, "  [yaw] finite=%d value=%.3f\n", std::isfinite(yaw) ? 1 : 0,
                     std::isfinite(yaw) ? yaw : 0.f);
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
    // 每帧先清零统计：提前 return 时面板读到 0，而不是上一帧的旧值。
    state_.espSeen.store(0);
    state_.espDrawn.store(0);
    state_.espTeamFiltered.store(0);
    state_.espDistanceFiltered.store(0);

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

    // 掉血检测：按实体序号比对上一帧血量。统计与浮字开关解耦——
    // 关掉「伤害浮字」也照样累计命中 / 伤害 / 击杀，只是不画飘字。
    {
        std::map<int, int> current;
        for (const PlayerInfo& player : snapshot.players) {
            current[player.index] = player.health;
            const auto previous = lastHealth_.find(player.index);
            if (previous == lastHealth_.end()) continue;
            const int loss = previous->second - player.health;
            if (loss <= 0) continue;  // 回血，或序号被复用给了新玩家

            state_.espHits.fetch_add(1);
            state_.espDamage.fetch_add(loss);
            if (player.health <= 0 && previous->second > 0) state_.espKills.fetch_add(1);
            if (config->esp.damageNumbers) {
                damageMarks_[player.index] = DamageMark{loss, now};
            }
        }
        lastHealth_.swap(current);

        if (config->esp.damageNumbers) {
            const auto damageWindow =
                std::chrono::milliseconds(std::max(100, config->esp.damageMs));
            for (auto it = damageMarks_.begin(); it != damageMarks_.end();) {
                const bool expired = now - it->second.at > damageWindow;
                const bool gone = lastHealth_.find(it->first) == lastHealth_.end();
                it = (expired || gone) ? damageMarks_.erase(it) : std::next(it);
            }
        } else {
            damageMarks_.clear();
        }
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

    int teamFiltered = 0;
    int distanceFiltered = 0;
    for (const PlayerInfo& player : snapshot.players) {
        if (config->esp.teamCheck && player.team == snapshot.localTeam) {
            ++teamFiltered;
            continue;
        }
        if (maxDistance > 0.f && player.distance > maxDistance) {
            ++distanceFiltered;
            continue;
        }

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

        // 高度差：只看 z 轴差值。多层图里光看方框分不清敌人在楼上还是楼下。
        if (config->esp.heightIndicator) {
            const float dz = (player.origin.z - snapshot.localOrigin.z) * 0.01905f;
            if (std::fabs(dz) >= 0.6f) {
                const bool above = dz > 0.f;
                const float centerX = left + width * 0.5f;
                const float tipY = above ? top - 14.f : top - 4.f;
                QPolygonF triangle;
                if (above) {
                    triangle << QPointF(centerX, tipY - 5.f) << QPointF(centerX - 4.f, tipY + 2.f)
                             << QPointF(centerX + 4.f, tipY + 2.f);
                } else {
                    triangle << QPointF(centerX, tipY + 5.f) << QPointF(centerX - 4.f, tipY - 2.f)
                             << QPointF(centerX + 4.f, tipY - 2.f);
                }
                painter.setPen(Qt::NoPen);
                painter.setBrush(color);
                painter.drawPolygon(triangle);
                painter.setBrush(Qt::NoBrush);
                painter.setPen(color);
                painter.drawText(QRectF(centerX + 7.f, tipY - 7.f, 46.f, 14.f),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 QString::number(std::fabs(dz), 'f', 1));
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

    // ---- 准星目标信息：准星指向谁，就在准星右下角标出血量与距离 ----
    if (config->esp.crosshairInfo && snapshot.crosshairIndex >= 0) {
        for (const PlayerInfo& player : snapshot.players) {
            if (player.index != snapshot.crosshairIndex) continue;
            if (player.health <= 0) break;

            // 颜色沿用敌我语义色，残血再切红，和血条判断一致。
            QColor accent = player.team == 2 ? QColor(230, 162, 60) : QColor(74, 144, 226);
            if (player.health <= 30) accent = QColor(240, 90, 90);
            painter.setPen(accent);
            const QString text = QString("%1 hp · %2 m")
                                     .arg(player.health)
                                     .arg(static_cast<int>(std::lround(player.distance)));
            painter.drawText(
                QRectF(static_cast<float>(width()) * 0.5f + 20.f,
                       static_cast<float>(height()) * 0.5f + 16.f, 170.f, 16.f),
                Qt::AlignLeft | Qt::AlignVCenter, text);
            break;
        }
    }

    // 本帧统计交给面板：一眼看出是「没敌人」还是「被过滤」。
    state_.espSeen.store(static_cast<int>(snapshot.players.size()));
    state_.espDrawn.store(drawnCount_);
    state_.espTeamFiltered.store(teamFiltered);
    state_.espDistanceFiltered.store(distanceFiltered);

    // ---- 屏外指示：视野外的敌人在屏幕边缘画箭头指向真实方位 ----
    if (config->esp.offscreen) {
        const float cx = static_cast<float>(width()) * 0.5f;
        const float cy = static_cast<float>(height()) * 0.5f;
        const float margin = 46.f;
        const float halfW = cx - margin;
        const float halfH = cy - margin;

        for (const PlayerInfo& player : snapshot.players) {
            if (player.health <= 0) continue;
            if (config->esp.teamCheck && player.team == snapshot.localTeam) continue;
            if (maxDistance > 0.f && player.distance > maxDistance) continue;

            // 已经在画面内（且离边缘够远）的目标由方框负责，不重复标注。
            Vec2 screen;
            const bool projected = worldToScreen(player.head, snapshot.view, Vec2{0.f, 0.f},
                                                 width(), height(), screen);
            if (projected && screen.x > margin && screen.x < width() - margin &&
                screen.y > margin && screen.y < height() - margin) {
                continue;
            }

            float dx = 0.f;
            float dy = 0.f;
            if (!screenDirection(player.head, snapshot.view, dx, dy)) continue;

            // 从屏幕中心沿该方向投射到留了边距的矩形边界上。
            const float tx = std::fabs(dx) > 1e-4f ? halfW / std::fabs(dx) : 1e9f;
            const float ty = std::fabs(dy) > 1e-4f ? halfH / std::fabs(dy) : 1e9f;
            const float t = std::min(tx, ty);
            const QPointF tip(cx + dx * t, cy + dy * t);

            // 三角形：尖朝屏幕外，底边收回内侧。
            const float angle = std::atan2(dy, dx);
            const float size = 11.f;
            const float spread = 0.42f;
            QPolygonF arrow;
            arrow << tip
                  << QPointF(tip.x() - std::cos(angle - spread) * size,
                             tip.y() - std::sin(angle - spread) * size)
                  << QPointF(tip.x() - std::cos(angle + spread) * size,
                             tip.y() - std::sin(angle + spread) * size);

            QColor color = player.team == 2 ? QColor(230, 162, 60) : QColor(74, 144, 226);
            color.setAlpha(static_cast<int>(std::lround(210.f * distanceFade(player.distance))));
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(arrow);
            painter.setBrush(Qt::NoBrush);

            // 距离数字贴在箭头内侧，避免压到屏幕边缘或箭头本身。
            const QPointF labelCenter(tip.x() - dx * 24.f, tip.y() - dy * 24.f);
            painter.setPen(QPen(color, 1.0));
            painter.drawText(QRectF(labelCenter.x() - 18.f, labelCenter.y() - 9.f, 36.f, 18.f),
                             Qt::AlignCenter,
                             QString::number(static_cast<int>(std::lround(player.distance))));
        }
    }

    // ---- 敌人列表：右侧按距离排序。远处方框只有几个像素，靠列表补信息 ----
    if (config->esp.enemyList) {
        struct Entry {
            float distance{0.f};
            int health{0};
            int team{0};
        };
        std::vector<Entry> entries;
        for (const PlayerInfo& player : snapshot.players) {
            if (player.health <= 0) continue;
            if (config->esp.teamCheck && player.team == snapshot.localTeam) continue;
            if (maxDistance > 0.f && player.distance > maxDistance) continue;
            entries.push_back(Entry{player.distance, player.health, player.team});
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) { return a.distance < b.distance; });

        const int limit = std::clamp(config->esp.enemyListMax, 1, 10);
        const float rowHeight = 21.f;
        const float rowWidth = 132.f;
        const float margin = 26.f;
        const float rowsHeight = rowHeight * static_cast<float>(limit);
        float left = static_cast<float>(width()) - rowWidth - margin;
        float top = static_cast<float>(height()) * 0.24f;
        switch (config->esp.listPos) {
            case 1:  // 左上
                left = margin;
                top = 120.f;
                break;
            case 2:  // 右上
                top = 120.f;
                break;
            case 3:  // 左下
                left = margin;
                top = static_cast<float>(height()) - rowsHeight - 120.f;
                break;
            case 4:  // 右下
                top = static_cast<float>(height()) - rowsHeight - 120.f;
                break;
            default:  // 0 = 右侧居中偏上
                break;
        }

        for (std::size_t i = 0; i < entries.size() && static_cast<int>(i) < limit; ++i) {
            const Entry& entry = entries.at(i);
            const QRectF row(left, top + rowHeight * static_cast<float>(i), rowWidth,
                             rowHeight - 3.f);
            QColor color = entry.team == 2 ? QColor(230, 162, 60) : QColor(74, 144, 226);
            QColor back = color;
            back.setAlpha(38);
            painter.setPen(Qt::NoPen);
            painter.setBrush(back);
            painter.drawRoundedRect(row, 4.0, 4.0);
            painter.setBrush(Qt::NoBrush);

            painter.setPen(color);
            painter.drawText(row.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             QString("%1m").arg(static_cast<int>(std::lround(entry.distance))));
            painter.drawText(row.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight,
                             QString("%1 hp").arg(entry.health));
        }
    }

    // ---- 2D 雷达：底部居中。开启旋转时「自己的朝向」恒在正上方；
    // 关闭时固定北向（世界 +X 向右、+Y 向上）。 ----
    if (config->esp.radar) {
        const float radius = 92.f;
        // 位置：0 = 底部居中（默认），其余按四角摆放并留 24 px 边距。
        const float margin = 24.f;
        const float topInset = radius + 88.f;  // 左上/右上要避开屏幕顶部的自身 HUD
        QPointF center;
        switch (config->esp.radarPos) {
            case 1: center = QPointF(radius + margin, topInset); break;
            case 2: center = QPointF(static_cast<float>(width()) - radius - margin, topInset); break;
            case 3:
                center = QPointF(radius + margin,
                                 static_cast<float>(height()) - radius - margin);
                break;
            case 4:
                center = QPointF(static_cast<float>(width()) - radius - margin,
                                 static_cast<float>(height()) - radius - margin);
                break;
            default:
                center = QPointF(static_cast<float>(width()) * 0.5f,
                                 static_cast<float>(height()) - radius - 56.f);
                break;
        }
        painter.setPen(QPen(QColor(230, 162, 60, 70), 1.0));
        painter.setBrush(QColor(12, 14, 18, 110));
        painter.drawEllipse(center, radius, radius);
        painter.setBrush(Qt::NoBrush);

        painter.setPen(QPen(QColor(230, 162, 60, 45), 1.0));
        painter.drawLine(QPointF(center.x() - radius, center.y()),
                         QPointF(center.x() + radius, center.y()));
        painter.drawLine(QPointF(center.x(), center.y() - radius),
                         QPointF(center.x(), center.y() + radius));

        // 朝向角：矩阵布局不符时 viewYaw 返回 NaN，这里自动退回固定北向，
        // 不至于把整块雷达旋歪。
        const float yaw = viewYaw(snapshot.view);
        const bool rotate = config->esp.radarRotate && std::isfinite(yaw);

        if (rotate) {
            // 顶部小三角：标记「这里是你的正前方」。
            QPolygonF heading;
            heading << QPointF(center.x(), center.y() - radius - 7.0)
                    << QPointF(center.x() - 5.0, center.y() - radius + 3.0)
                    << QPointF(center.x() + 5.0, center.y() - radius + 3.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(230, 162, 60, 150));
            painter.drawPolygon(heading);
            painter.setBrush(Qt::NoBrush);
        }

        const float range = std::max(5.f, config->esp.radarRange);

        // 距离圈：半径三等分并标出对应米数，否则光有圆点判断不出远近。
        for (int ring = 1; ring <= 3; ++ring) {
            const float ringRadius = radius * (static_cast<float>(ring) / 3.f);
            painter.setPen(QPen(QColor(230, 162, 60, 26), 1.0));
            painter.drawEllipse(center, ringRadius, ringRadius);
            painter.setPen(QPen(QColor(230, 162, 60, 80), 1.0));
            painter.drawText(
                QRectF(center.x() + ringRadius - 28.f, center.y() - 13.f, 26.f, 12.f),
                Qt::AlignRight | Qt::AlignVCenter,
                QString::number(static_cast<int>(range * static_cast<float>(ring) / 3.f)));
        }

        for (const PlayerInfo& player : snapshot.players) {
            if (maxDistance > 0.f && player.distance > maxDistance) continue;
            if (player.distance > range) continue;

            const float dx = player.origin.x - snapshot.localOrigin.x;
            const float dy = player.origin.y - snapshot.localOrigin.y;
            const float scaled = radius * (player.distance / range);
            if (scaled <= 0.f || (dx == 0.f && dy == 0.f)) continue;

            QPointF dot;
            if (rotate) {
                // 世界方位角减去朝向角，得到「前方朝上」的局部坐标。
                const float rel = std::atan2(dy, dx) - yaw;
                dot = QPointF(center.x() - std::sin(rel) * scaled,
                              center.y() - std::cos(rel) * scaled);
            } else {
                const float length = std::sqrt(dx * dx + dy * dy);
                dot = QPointF(center.x() + dx / length * scaled,
                              center.y() - dy / length * scaled);
            }

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
