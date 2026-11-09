#include "Features.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <algorithm>
#include <cmath>

namespace wv {
namespace {
constexpr int kLoopIntervalMs = 1;
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
        const std::shared_ptr<Config> config = state_.config.load();
        const std::uintptr_t window = state_.gameWindow.load();
        if (!config || window == 0 || !game_.attached() || !gameActive(display, game_.pid())) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kLoopIntervalMs));
            continue;
        }

        const Snapshot snapshot = game_.snapshot();
        if (!snapshot.valid) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kLoopIntervalMs));
            continue;
        }

        XWindowAttributes attributes{};
        if (!XGetWindowAttributes(display, static_cast<Window>(window), &attributes)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kLoopIntervalMs));
            continue;
        }

        if (config->aim.enabled && (config->aim.key <= 0 || buttonDown(display, config->aim.key))) {
            aimStep(display, *config, snapshot, attributes.width, attributes.height);
        }
        if (config->trigger.enabled &&
            (config->trigger.key <= 0 || buttonDown(display, config->trigger.key))) {
            triggerStep(display, *config, snapshot);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kLoopIntervalMs));
    }

    XCloseDisplay(display);
}

bool Features::gameActive(void* displayPtr, int pid) {
    Display* display = static_cast<Display*>(displayPtr);
    if (!display || pid <= 0) return false;

    const Window root = DefaultRootWindow(display);
    const Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    const Atom pidAtom = XInternAtom(display, "_NET_WM_PID", False);

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

    long windowPidValue = 0;
    data = nullptr;
    if (XGetWindowProperty(display, active, pidAtom, 0, 1, False, XA_CARDINAL, &type, &format,
                           &count, &after, &data) == Success &&
        data != nullptr) {
        windowPidValue = static_cast<long>(*reinterpret_cast<unsigned long*>(data));
        XFree(data);
    }

    return windowPidValue == pid;
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
    float bestDistance = config.aim.fovPixels;

    for (const PlayerInfo& player : snapshot.players) {
        if (player.health <= 0) continue;
        if (config.aim.teamCheck && player.team == snapshot.localTeam) continue;
        if (config.aim.maxDistance > 0.f && player.distance > config.aim.maxDistance) continue;

        const float dx = player.headScreen.x - centerX;
        const float dy = player.headScreen.y - centerY;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &player;
        }
    }
    if (!best) return;

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

    XTestFakeButtonEvent(display, 1, True, CurrentTime);
    XFlush(display);
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, config.trigger.delayMs)));
    XTestFakeButtonEvent(display, 1, False, CurrentTime);
    XFlush(display);

    nextTrigger_ = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(std::max(1, config.trigger.cooldownMs));
}

}  // namespace wv
