#include "Features.h"

#include "core/LogPath.h"

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
// F8：显隐控制面板。面板被游戏盖住时不必 Alt+Tab 出去找窗口。
constexpr int kTogglePanelKey = 0xFFC5;  // XK_F8
// F9：Panic。一键停掉叠加层与全部输入合成，按一下回到完全静默。
constexpr int kPanicKey = 0xFFC6;  // XK_F9
// F10：把本局命中 / 伤害 / 击杀计数清零，不必重启进程。
constexpr int kResetStatsKey = 0xFFC7;  // XK_F10

// 诊断输出：追加写入 wv::logPath()（带节流），WV_DEBUG=1 时同时写 stderr。
// 路径由 LogPath.h 统一决定：root 跑过后共享文件可能 644 不可写，会自动退回
// 带 uid 的私有文件，保证普通用户启动也留得下按键/门控/目标证据。
// 文件超过 kTraceMaxBytes 就截断重来：纯追加无上限时，长期挂机能撑到几百 MB。
constexpr long kTraceMaxBytes = 2L * 1024 * 1024;
constexpr long kTraceCheckStep = 64L * 1024;  // 每累计写 64 KB 才检查一次，避免频繁 stat

struct TraceSink {
    FILE* file{nullptr};
    long size{0};
    long pending{0};
};

TraceSink& traceSink() {
    static TraceSink sink = [] {
        TraceSink s;
        s.file = std::fopen(logPath().c_str(), "a");
        if (s.file) {
            std::fseek(s.file, 0, SEEK_END);
            const long end = std::ftell(s.file);
            s.size = end > 0 ? end : 0;
        }
        return s;
    }();
    return sink;
}

// 直接截断而非重命名：面板随时可能读这个文件，留一份 .1 只会白占双倍空间。
void rotateTrace(TraceSink& sink) {
    if (!sink.file) return;
    std::fclose(sink.file);
    sink.file = std::fopen(logPath().c_str(), "w");
    sink.size = 0;
    sink.pending = 0;
}

bool traceToStderr() {
    static const bool enabled = std::getenv("WV_DEBUG") != nullptr;
    return enabled;
}

void trace(const char* format, ...) {
    TraceSink& sink = traceSink();
    va_list args;
    va_start(args, format);
    if (sink.file) {
        const int written = std::vfprintf(sink.file, format, args);
        std::fflush(sink.file);
        if (written > 0) {
            sink.size += written;
            sink.pending += written;
            if (sink.pending >= kTraceCheckStep) {
                sink.pending = 0;
                if (sink.size > kTraceMaxBytes) rotateTrace(sink);
            }
        }
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
    uinput_.close();
}

void Features::emitMove(void* displayPtr, int dx, int dy) {
    if (uinput_.move(dx, dy)) return;
    [[maybe_unused]] Display* display = static_cast<Display*>(displayPtr);
    XTestFakeRelativeMotionEvent(display, dx, dy, CurrentTime);
    XFlush(display);
}

void Features::emitButton(void* displayPtr, int button, bool down) {
    if (uinput_.button(button, down)) return;
    [[maybe_unused]] Display* display = static_cast<Display*>(displayPtr);
    XTestFakeButtonEvent(display, button, down ? True : False, CurrentTime);
    XFlush(display);
}

void Features::run() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        running_.store(false);
        return;
    }

    // 常驻虚拟鼠标：SDL 需要几秒枚举热插拔设备，设备必须全程保持，
    // 不能每次开火临时创建。XTest 在 raw input 下被游戏完全忽略。
    const bool uinputReady = uinput_.open();
    trace("[input] backend=%s uinput_ready=%d\n",
          uinputReady ? "uinput" : "xtest-fallback", uinputReady ? 1 : 0);

    while (running_.load()) {
        // 热键从配置读，面板可录制改键；配置缺失时退回 F8 / F9 默认值。
        // 键位 <= 0 视为禁用（keyDown 对 0 恒真，不能直接传进去）。
        const std::shared_ptr<Config> hotkeyConfig = state_.config.load();
        const int toggleKey = hotkeyConfig ? hotkeyConfig->togglePanelKey : kTogglePanelKey;
        const int panicKey = hotkeyConfig ? hotkeyConfig->panicKey : kPanicKey;
        const int resetStatsKey =
            hotkeyConfig ? hotkeyConfig->resetStatsKey : kResetStatsKey;

        // 面板热键：只认按下沿，长按不会连续切换。放在所有早退分支之前，
        // 这样即使没连上游戏、偏移缺失，面板也还能收起来。
        const bool toggleHeld = toggleKey > 0 && keyDown(display, toggleKey);
        if (toggleHeld && !lastTogglePanel_) state_.panelToggle.store(1);
        lastTogglePanel_ = toggleHeld;

        // 开关式：按一下进入静默，再按一下恢复。
        const bool panicHeld = panicKey > 0 && keyDown(display, panicKey);
        if (panicHeld && !lastPanic_) {
            state_.panicMode.store(state_.panicMode.load() != 0 ? 0 : 1);
        }
        lastPanic_ = panicHeld;

        // 清零战绩：只认按下沿。放在 panic 早退之前，静默期间也能清掉旧数据。
        const bool resetHeld = resetStatsKey > 0 && keyDown(display, resetStatsKey);
        if (resetHeld && !lastResetStats_) state_.statsReset.store(1);
        lastResetStats_ = resetHeld;

        // 静默期间不做任何输入合成，连诊断输出都停掉。
        if (state_.panicMode.load() != 0) {
            state_.aimGate.store(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(kLoopIntervalMs));
            continue;
        }

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
            config->aim.scopeKey <= 0 || keyDown(display, config->aim.scopeKey);
        const bool fireHeld =
            config->aim.fireKey <= 0 || keyDown(display, config->aim.fireKey);
        state_.aimGate.store((scopeHeld && fireHeld) ? 1 : 0);
        if (++debugTick_ % 100 == 0) {
            trace("[gate] window=%lu players=%zu scope=%d fire=%d fov=%.0f\n",
                  static_cast<unsigned long>(window), snapshot.players.size(),
                  scopeHeld ? 1 : 0, fireHeld ? 1 : 0,
                  static_cast<double>(config->aim.fovPixels));
        }
        // 子弹追踪：只要按住触发键就把准星直接贴到最近敌人头部，与自瞄互斥。
        // 自瞄有开镜前置与灵敏度衰减，追踪没有——准星去哪都先拉回头上。
        // 必须先于自瞄执行并抢占：两个都写相对位移会在一帧内互相拉扯。
        const bool trackHeld =
            config->track.key <= 0 || keyDown(display, config->track.key);
        const bool tracking =
            config->track.enabled && trackHeld &&
            trackStep(display, *config, snapshot, attributes.width, attributes.height);
        if (!tracking && config->aim.enabled && scopeHeld && fireHeld) {
            aimStep(display, *config, snapshot, attributes.width, attributes.height);
        }
        // 子弹追踪走 MemoryEngine 的纯内存角度锁：本线程一个鼠标事件都不发，
        // 准星与视角完全不动，射击角由引擎每帧锁到最近敌人头部。
        // 静默锁头：按住触发键时由本进程完成「贴头 + 点击 + 归位」，
        // 玩家不需要把准星放到敌人身上。与普通扳机互斥，否则一次按键会打出两发。
        const bool silentHeld =
            config->silent.key <= 0 || keyDown(display, config->silent.key);
        const bool silentFired =
            config->silent.enabled && silentHeld &&
            silentStep(display, *config, snapshot, attributes.width, attributes.height);
        if (!silentFired && config->trigger.enabled &&
            (config->trigger.key <= 0 || keyDown(display, config->trigger.key))) {
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
    [[maybe_unused]] Display* display = static_cast<Display*>(displayPtr);
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

bool Features::keyDown(void* displayPtr, int code) {
    if (code <= 0) return true;  // 0 = 不限，恒真

    [[maybe_unused]] Display* display = static_cast<Display*>(displayPtr);
    if (!display) return false;

    if (code <= 5) {
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
        return (mask & (Button1Mask << (code - 1))) != 0;
    }

    // 键盘：keysym 先转成本布局下的 keycode，再查 256 位按键掩码。
    // 用 XQueryKeymap 而不是 XGrabKey，避免把按键从游戏手里抢走。
    const KeyCode keycode = XKeysymToKeycode(display, static_cast<KeySym>(code));
    if (keycode == 0) return false;
    char keys[32]{};
    XQueryKeymap(display, keys);
    return (keys[keycode / 8] & (1 << (keycode % 8))) != 0;
}

void Features::aimStep(void* displayPtr, const Config& config, const Snapshot& snapshot, int width,
                       int height) {
    [[maybe_unused]] Display* display = static_cast<Display*>(displayPtr);
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

        Vec2 delta{};
        if (!screenDelta(player.headScreen, width, height, delta)) continue;
        const float dx = delta.x;
        const float dy = delta.y;
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

        // 目标锁定：已锁定的目标只要还在 FOV 内且存活就直接续上，
        // 避免两个敌人贴身时准星每帧在两者之间来回跳。
        if (config.aim.lockTarget && player.index == lockedIndex_) {
            best = &player;
            bestVisible = visible;
            bestMetric = metric;
            break;
        }

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
        lockedIndex_ = -1;  // 目标死亡或出了 FOV，解锁
        if (++debugAimTick_ % 200 == 0) {
            trace("[aim] no target in fov=%.0f nearest=%.1f players=%zu\n",
                  static_cast<double>(config.aim.fovPixels), static_cast<double>(nearest),
                  snapshot.players.size());
        }
        return;
    }
    lockedIndex_ = best->index;

    const int steps = std::max(1, config.aim.smooth);
    const float deltaX =
        (best->headScreen.x - centerX) * config.aim.sensitivityFactor / static_cast<float>(steps);
    const float deltaY =
        (best->headScreen.y - centerY) * config.aim.sensitivityFactor / static_cast<float>(steps);

    const int moveX = static_cast<int>(std::lround(deltaX));
    const int moveY = static_cast<int>(std::lround(deltaY));
    if (moveX == 0 && moveY == 0) return;

    emitMove(displayPtr, moveX, moveY);
    if (++debugAimTick_ % 200 == 0) {
        trace("[aim] target idx=%d err=(%.1f,%.1f) move=(%d,%d)\n", best->index,
                     static_cast<double>(best->headScreen.x - centerX),
                     static_cast<double>(best->headScreen.y - centerY), moveX, moveY);
    }
}

bool Features::trackStep(void* displayPtr, const Config& config, const Snapshot& snapshot,
                         int width, int height) {
    [[maybe_unused]] Display* display = static_cast<Display*>(displayPtr);
    const float centerX = static_cast<float>(width) * 0.5f;
    const float centerY = static_cast<float>(height) * 0.5f;

    const PlayerInfo* best = nullptr;
    bool bestVisible = false;
    float bestMetric = 0.f;

    for (const PlayerInfo& player : snapshot.players) {
        if (player.health <= 0) continue;
        if (config.track.teamCheck && player.team == snapshot.localTeam) continue;
        if (config.track.maxDistance > 0.f && player.distance > config.track.maxDistance) continue;

        Vec2 delta{};
        if (!screenDelta(player.headScreen, width, height, delta)) continue;
        const float dx = delta.x;
        const float dy = delta.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        // fovPixels = 0 表示全屏：不设半径，屏幕上任何位置的敌人都是候选。
        if (config.track.fovPixels > 0.f && distance >= config.track.fovPixels) continue;

        const bool visible = player.visible;
        bool better = best == nullptr;
        if (!better) {
            better = (visible != bestVisible) ? visible : (distance < bestMetric);
        }
        if (!better) continue;

        best = &player;
        bestVisible = visible;
        bestMetric = distance;
    }
    if (!best) return false;

    // 不乘 sensitivityFactor：追踪的目标是「准星落在头上」，留系数会永远差一截。
    // smooth = 1 时单帧贴合；调大则分摊到多帧，看起来更像手动甩枪。
    const int steps = std::max(1, config.track.smooth);
    const int moveX = static_cast<int>(
        std::lround((best->headScreen.x - centerX) / static_cast<float>(steps)));
    const int moveY = static_cast<int>(
        std::lround((best->headScreen.y - centerY) / static_cast<float>(steps)));
    if (moveX == 0 && moveY == 0) return true;

    emitMove(displayPtr, moveX, moveY);
    if (++debugAimTick_ % 200 == 0) {
        trace("[track] target idx=%d err=(%.1f,%.1f) move=(%d,%d)\n", best->index,
              static_cast<double>(best->headScreen.x - centerX),
              static_cast<double>(best->headScreen.y - centerY), moveX, moveY);
    }
    return true;
}

void Features::triggerStep(void* displayPtr, const Config& config, const Snapshot& snapshot) {
    [[maybe_unused]] Display* display = static_cast<Display*>(displayPtr);
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

    emitButton(displayPtr, 1, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(hold(rng)));
    emitButton(displayPtr, 1, false);

    nextTrigger_ = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(std::max(1, config.trigger.cooldownMs));
}

bool Features::silentStepWith(void* displayPtr, const SilentConfig& silent,
                              const Snapshot& snapshot, int width, int height) {
    [[maybe_unused]] Display* display = static_cast<Display*>(displayPtr);
    const auto now = std::chrono::steady_clock::now();
    if (now < nextSilent_) return false;

    const float centerX = static_cast<float>(width) * 0.5f;
    const float centerY = static_cast<float>(height) * 0.5f;

    const PlayerInfo* best = nullptr;
    bool bestVisible = false;
    float bestMetric = 0.f;
    int rejected = 0;
    for (const PlayerInfo& player : snapshot.players) {
        if (player.health <= 0) continue;
        if (silent.teamCheck && player.team == snapshot.localTeam) continue;
        if (silent.maxDistance > 0.f && player.distance > silent.maxDistance) continue;

        // 投影退化的目标会把准星甩到几千像素外：只接受屏幕矩形内的候选。
        Vec2 delta{};
        if (!screenDelta(player.headScreen, width, height, delta)) {
            ++rejected;
            continue;
        }
        const float dx = delta.x;
        const float dy = delta.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (silent.fovPixels > 0.f && distance >= silent.fovPixels) continue;

        const bool visible = player.visible;
        bool better = best == nullptr;
        if (!better) better = (visible != bestVisible) ? visible : (distance < bestMetric);
        if (!better) continue;

        best = &player;
        bestVisible = visible;
        bestMetric = distance;
    }
    if (!best) {
        if (++debugAimTick_ % 200 == 0) {
            trace("[silent] no on-screen target players=%zu rejected=%d\n",
                  snapshot.players.size(), rejected);
        }
        return false;
    }

    // 相对位移直接对齐屏幕像素：鼠标移动 N 像素，准星就移动 N 像素，
    // 不需要灵敏度系数。贴头后立刻合成点击，等游戏过一个 tick 再归位，
    // 归位后玩家的准星回到原位，看起来只是闪了一下。
    const int offsetX = static_cast<int>(std::lround(best->headScreen.x - centerX));
    const int offsetY = static_cast<int>(std::lround(best->headScreen.y - centerY));
    // 双保险：位移绝不超过半屏。实机出现过的 (1938,-4260) 会让准星飞出画面。
    const int limitX = width / 2;
    const int limitY = height / 2;
    if (offsetX > limitX || offsetX < -limitX || offsetY > limitY || offsetY < -limitY) {
        return false;
    }
    if (offsetX != 0 || offsetY != 0) {
        emitMove(displayPtr, offsetX, offsetY);
        // 投递后再按下左键，避免点击发生在旧坐标上。
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    emitButton(displayPtr, 1, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, silent.holdMs)));
    emitButton(displayPtr, 1, false);

    if (offsetX != 0 || offsetY != 0) {
        emitMove(displayPtr, -offsetX, -offsetY);
    }

    nextSilent_ = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(std::max(1, silent.cooldownMs));
    if (++debugAimTick_ % 50 == 0) {
        trace("[silent] target idx=%d offset=(%d,%d) hold=%dms\n", best->index, offsetX, offsetY,
              silent.holdMs);
    }
    return true;
}

bool Features::silentStep(void* displayPtr, const Config& config, const Snapshot& snapshot,
                          int width, int height) {
    return silentStepWith(displayPtr, config.silent, snapshot, width, height);
}

void Features::recoilStep(void* displayPtr, const Config& config, const Snapshot& snapshot,
                          int height) {
    [[maybe_unused]] Display* display = static_cast<Display*>(displayPtr);
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

    emitMove(displayPtr, moveX, moveY);
}

}  // namespace wv
