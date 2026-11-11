#include "Features.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace wv {
namespace {
constexpr int kLoopIntervalMs = 1;

// 诊断输出：始终追加写入 /tmp/wv_lite.log（带节流），WV_DEBUG=1 时同时写 stderr。
// 这样无论以 sudo 还是普通用户启动，都能事后读取按键/门控/目标证据。
FILE* traceFile() {
    static FILE* file = std::fopen("/tmp/wv_lite.log", "a");
    return file;
}

bool traceToStderr() {
    static const bool enabled = std::getenv("WV_DEBUG") != nullptr;
    return enabled;
}

void trace(const char* format, ...) {
    FILE* file = traceFile();
    va_list args;
    va_start(args, format);
    if (file) {
        std::vfprintf(file, format, args);
        std::fflush(file);
    }
    va_end(args);
    if (traceToStderr()) {
        va_start(args, format);
        std::vfprintf(stderr, format, args);
        va_end(args);
    }
}

// 读取全局指针按键掩码（XQueryPointer 的 state 参数），用于记录物理键位映射。
unsigned int pointerMask(Display* display) {
    Window root = DefaultRootWindow(display);
    Window rootReturn = 0;
    Window childReturn = 0;
    int rootX = 0;
    int rootY = 0;
    int winX = 0;
    int winY = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(display, root, &rootReturn, &childReturn, &rootX, &rootY, &winX, &winY,
                       &mask)) {
        return 0;
    }
    return mask;
}
}  // namespace

Features::Features(Game& game, AppState& state) : game_(game), state_(state) {}

Features::~Features() { stop(); }

void Features::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&Features::run, this);
}

void Features::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void Features::run() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        running_.store(false);
        return;
    }

    while (running_.load()) {
        // 物理键位映射记录：掩码变化即写一行，放在所有门控之前，
        // 这样即使游戏未被判定为前台也能拿到真实的 ButtonN。
        const unsigned int mask = pointerMask(display);
        if (mask != lastMask_) {
            trace("[btn] mask=0x%04x btn1=%d btn2=%d btn3=%d btn4=%d btn5=%d\n", mask,
                  (mask & Button1Mask) ? 1 : 0, (mask & Button2Mask) ? 1 : 0,
                  (mask & Button3Mask) ? 1 : 0, (mask & Button4Mask) ? 1 : 0,
                  (mask & Button5Mask) ? 1 : 0);
            lastMask_ = mask;
        }

        const std::shared_ptr<Config> config = state_.config.load();
        const std::uintptr_t window = state_.gameWindow.load();
        if (!config || window == 0 || !game_.attached() ||
            !gameActive(display, window, game_.pid())) {
            state_.aimGate.store(0);
            if (++debugTick_ % 1000 == 0) {
                trace("[skip] cfg=%d window=%lu attached=%d active=%d pid=%d\n",
                      config ? 1 : 0, static_cast<unsigned long>(window),
                      game_.attached() ? 1 : 0,
                      (config && window != 0)
                          ? (gameActive(display, window, game_.pid()) ? 1 : 0)
                          : -1,
                      game_.pid());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kLoopIntervalMs));
            continue;
        }

        const Snapshot snapshot = game_.snapshot();
        if (!snapshot.valid) {
            state_.aimGate.store(0);
            if (++debugTick_ % 1000 == 0) {
                trace("[skip] snapshot invalid\n");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kLoopIntervalMs));
            continue;
        }

        XWindowAttributes attributes{};
        if (!XGetWindowAttributes(display, static_cast<Window>(window), &attributes)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kLoopIntervalMs));
            continue;
        }

        // 自瞄只在「开镜 + 开火」同时按住时执行；任一键为 0 表示不要求该键。
        const bool scopeHeld =
            config->aim.scopeKey <= 0 || buttonDown(display, config->aim.scopeKey);
        const bool fireHeld =
            config->aim.fireKey <= 0 || buttonDown(display, config->aim.fireKey);
        state_.aimGate.store((scopeHeld && fireHeld) ? 1 : 0);
        if (++debugTick_ % 100 == 0) {
            trace("[gate] window=%lu players=%zu scope=%d fire=%d fov=%.0f\n",
                  static_cast<unsigned long>(window), snapshot.players.size(),
                  scopeHeld ? 1 : 0, fireHeld ? 1 : 0,
                  static_cast<double>(config->aim.fovPixels));
        }
        if (config->aim.enabled && scopeHeld && fireHeld) {
            aimStep(display, *config, snapshot, attributes.width, attributes.height);
        }
        if (config->trigger.enabled &&
            (config->trigger.key <= 0 || buttonDown(display, config->trigger.key))) {
            triggerStep(display, *config, snapshot);
        }
        // 后坐力抵消独立于自瞄：即使自瞄关闭，也按开火状态反向补偿视角。
        if (config->recoil.enabled && (!config->recoil.requireFire || fireHeld)) {
            recoilStep(display, *config, snapshot, attributes.height);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kLoopIntervalMs));
    }

    XCloseDisplay(display);
}

namespace {

// 读取窗口的 _NET_WM_PID；属性不存在时返回 0。
long windowPid(Display* display, Window window) {
    if (window == 0) return 0;
    const Atom pidAtom = XInternAtom(display, "_NET_WM_PID", False);
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    unsigned char* data = nullptr;
    long pid = 0;
    if (XGetWindowProperty(display, window, pidAtom, 0, 1, False, XA_CARDINAL, &type, &format,
                           &count, &after, &data) == Success &&
        data != nullptr) {
        pid = static_cast<long>(*reinterpret_cast<unsigned long*>(data));
        XFree(data);
    }
    return pid;
}

// 从焦点窗口沿父链向上查找，命中游戏窗口或其 PID 即视为游戏获得焦点。
bool focusChainBelongsTo(Display* display, std::uintptr_t gameWindow, int pid) {
    Window focus = 0;
    int revert = 0;
    XGetInputFocus(display, &focus, &revert);

    if (focus == PointerRoot) {
        // PointerRoot 表示「键盘焦点跟随指针」，此时应取指针下的子窗口。
        const Window root = DefaultRootWindow(display);
        Window rootReturn = 0;
        Window child = 0;
        int rootX = 0;
        int rootY = 0;
        int winX = 0;
        int winY = 0;
        unsigned int mask = 0;
        if (!XQueryPointer(display, root, &rootReturn, &child, &rootX, &rootY, &winX, &winY,
                           &mask) ||
            child == 0) {
            return false;
        }
        focus = child;
    }
    if (focus == None || focus == 0) return false;

    Window probe = focus;
    for (int depth = 0; depth < 16 && probe != 0; ++depth) {
        if (probe == static_cast<Window>(gameWindow)) return true;
        if (windowPid(display, probe) == pid) return true;

        Window root = 0;
        Window parent = 0;
        Window* children = nullptr;
        unsigned int childCount = 0;
        if (!XQueryTree(display, probe, &root, &parent, &children, &childCount)) break;
        if (children) XFree(children);
        if (parent == 0 || parent == probe) break;
        probe = parent;
    }
    return false;
}

}  // namespace

bool Features::gameActive(void* displayPtr, std::uintptr_t gameWindow, int pid) {
    Display* display = static_cast<Display*>(displayPtr);
    if (!display || pid <= 0) return false;

    // 首选键盘焦点链：全屏且无窗口管理器接管时，_NET_ACTIVE_WINDOW 可能长期陈旧，
    // 只靠该属性会导致自瞄/扳机整套停摆（表现为「按键没反应」）。
    if (focusChainBelongsTo(display, gameWindow, pid)) return true;

    // 回退：_NET_ACTIVE_WINDOW 的 PID 匹配。
    const Window root = DefaultRootWindow(display);
    const Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);

    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    unsigned char* data = nullptr;
    Window active = 0;

    if (XGetWindowProperty(display, root, activeAtom, 0, 1, False, XA_WINDOW, &type, &format,
                           &count, &after, &data) == Success &&
        data != nullptr) {
        active = static_cast<Window>(*reinterpret_cast<unsigned long*>(data));
        XFree(data);
    }
    if (active == 0) return false;

    return windowPid(display, active) == pid;
}

bool Features::buttonDown(void* displayPtr, int button) {
    Display* display = static_cast<Display*>(displayPtr);
    if (!display || button < 1 || button > 5) return false;

    Window root = DefaultRootWindow(display);
    Window rootReturn = 0;
    Window childReturn = 0;
    int rootX = 0;
    int rootY = 0;
    int winX = 0;
    int winY = 0;
    unsigned int mask = 0;

    if (!XQueryPointer(display, root, &rootReturn, &childReturn, &rootX, &rootY, &winX, &winY,
                       &mask)) {
        return false;
    }
    return (mask & (Button1Mask << (button - 1))) != 0;
}

void Features::aimStep(void* displayPtr, const Config& config, const Snapshot& snapshot, int width,
                       int height) {
    Display* display = static_cast<Display*>(displayPtr);
    const float centerX = static_cast<float>(width) * 0.5f;
    const float centerY = static_cast<float>(height) * 0.5f;

    const PlayerInfo* best = nullptr;
    bool bestVisible = false;
    float bestMetric = 0.f;
    float nearest = 1e9f;  // 全屏最近目标的屏幕距离，用于诊断 FOV 是否过小

    for (const PlayerInfo& player : snapshot.players) {
        if (player.health <= 0) continue;
        if (config.aim.teamCheck && player.team == snapshot.localTeam) continue;
        if (config.aim.maxDistance > 0.f && player.distance > config.aim.maxDistance) continue;

        const float dx = player.headScreen.x - centerX;
        const float dy = player.headScreen.y - centerY;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance < nearest) nearest = distance;
        if (distance >= config.aim.fovPixels) continue;

        // 可见性分层：同一 FOV 内优先 m_bSpotted 为真的目标，没有可见目标才退化到遮挡目标。
        // 偏移未配置时 Game 侧 visible 恒为 true，行为与旧版一致。
        // 目标策略：0 = 屏幕最近，1 = 血量最低，2 = 世界最近。
        const bool visible = player.visible;
        const float metric = config.aim.strategy == 1
                                 ? static_cast<float>(player.health)
                                 : (config.aim.strategy == 2 ? player.distance : distance);
        bool better = best == nullptr;
        if (!better) {
            better = (visible != bestVisible) ? visible : (metric < bestMetric);
        }
        if (!better) continue;

        best = &player;
        bestVisible = visible;
        bestMetric = metric;
    }
    if (!best) {
        if (++debugAimTick_ % 200 == 0) {
            trace("[aim] no target in fov=%.0f nearest=%.1f players=%zu\n",
                  static_cast<double>(config.aim.fovPixels), static_cast<double>(nearest),
                  snapshot.players.size());
        }
        return;
    }

    const int steps = std::max(1, config.aim.smooth);
    const float deltaX =
        (best->headScreen.x - centerX) * config.aim.sensitivityFactor / static_cast<float>(steps);
    const float deltaY =
        (best->headScreen.y - centerY) * config.aim.sensitivityFactor / static_cast<float>(steps);

    const int moveX = static_cast<int>(std::lround(deltaX));
    const int moveY = static_cast<int>(std::lround(deltaY));
    if (moveX == 0 && moveY == 0) return;

    XTestFakeRelativeMotionEvent(display, moveX, moveY, CurrentTime);
    XFlush(display);
    if (++debugAimTick_ % 200 == 0) {
        trace("[aim] target idx=%d err=(%.1f,%.1f) move=(%d,%d)\n", best->index,
                     static_cast<double>(best->headScreen.x - centerX),
                     static_cast<double>(best->headScreen.y - centerY), moveX, moveY);
    }
}

void Features::triggerStep(void* displayPtr, const Config& config, const Snapshot& snapshot) {
    Display* display = static_cast<Display*>(displayPtr);
    if (snapshot.crosshairIndex < 0) return;

    const PlayerInfo* target = nullptr;
    for (const PlayerInfo& player : snapshot.players) {
        if (player.index == snapshot.crosshairIndex && player.health > 0) {
            target = &player;
            break;
        }
    }
    if (!target) return;
    if (config.trigger.teamCheck && target->team == snapshot.localTeam) return;

    const auto now = std::chrono::steady_clock::now();
    if (now < nextTrigger_) return;

    // 按压时长在 delayMs 基础上做 ±20% 抖动，避免每次都是同一长度的时间戳。
    const int base = std::max(1, config.trigger.delayMs);
    const int jitter = std::max(1, base / 5);
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> hold(std::max(1, base - jitter), base + jitter);

    XTestFakeButtonEvent(display, 1, True, CurrentTime);
    XFlush(display);
    std::this_thread::sleep_for(std::chrono::milliseconds(hold(rng)));
    XTestFakeButtonEvent(display, 1, False, CurrentTime);
    XFlush(display);

    nextTrigger_ = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(std::max(1, config.trigger.cooldownMs));
}

void Features::recoilStep(void* displayPtr, const Config& config, const Snapshot& snapshot,
                          int height) {
    Display* display = static_cast<Display*>(displayPtr);
    const float punchX = snapshot.aimPunch.x;
    const float punchY = snapshot.aimPunch.y;
    const float limit = std::max(1.f, config.recoil.maxDegrees);

    // 越界即视为读到垃圾值（换枪 / 瞬移 / 指针错位）：丢弃本帧并把基准归零。
    if (std::fabs(punchX) > limit || std::fabs(punchY) > limit) {
        lastPunch_ = {};
        return;
    }

    // 只补偿「本帧新增」的角度：绝对值每帧重发会在压枪时过转，
    // 且停火后 punch 回落的过程会被持续反向追打。
    const float deltaX = punchX - lastPunch_.x;
    const float deltaY = punchY - lastPunch_.y;
    lastPunch_ = Vec2{punchX, punchY};
    if (std::fabs(deltaX) > limit || std::fabs(deltaY) > limit) return;

    // 竖直 90° 视场近似：一屏高 ≈ 90°，与 aimStep 共用同一个 XTest 相对位移通道。
    const float pixelsPerDegree = static_cast<float>(std::max(1, height)) / 90.f;
    const float k = pixelsPerDegree * config.recoil.strength;
    const int moveX = static_cast<int>(std::lround(-deltaY * k));
    const int moveY = static_cast<int>(std::lround(deltaX * k));
    if (moveX == 0 && moveY == 0) return;

    XTestFakeRelativeMotionEvent(display, moveX, moveY, CurrentTime);
    XFlush(display);
}

}  // namespace wv
