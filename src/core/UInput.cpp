#include "UInput.h"

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>
#include <initializer_list>

namespace wv {
namespace {

constexpr const char* kDevicePath = "/dev/uinput";
constexpr const char* kDeviceName = "wv-virtual-mouse";

}  // namespace

UInput::~UInput() { close(); }

bool UInput::open() {
    if (ready_) return true;
    fd_ = ::open(kDevicePath, O_WRONLY | O_NONBLOCK);
    if (fd_ < 0) return false;

    // 6.x 内核把 UI_SET_* 的 arg 当整数值，老内核当指向 int 的指针。
    // 两种都试，避免版本差异直接 EINVAL（本机 6.18 实测走第一种）。
    const auto setBit = [&](unsigned long request, int value) {
        if (::ioctl(fd_, request, value) == 0) return true;
        int copy = value;
        return ::ioctl(fd_, request, &copy) == 0;
    };

    bool ok = setBit(UI_SET_EVBIT, EV_KEY) && setBit(UI_SET_EVBIT, EV_REL) &&
              setBit(UI_SET_EVBIT, EV_SYN);
    for (const int key : {BTN_LEFT, BTN_MIDDLE, BTN_RIGHT}) {
        ok = ok && setBit(UI_SET_KEYBIT, key);
    }
    for (const int rel : {REL_X, REL_Y}) {
        ok = ok && setBit(UI_SET_RELBIT, rel);
    }
    if (!ok) {
        close();
        return false;
    }

    struct uinput_user_dev device{};
    std::strncpy(device.name, kDeviceName, UINPUT_MAX_NAME_SIZE - 1);
    device.id.bustype = BUS_USB;
    device.id.vendor = 0x1234;
    device.id.product = 0x5678;
    device.id.version = 1;
    if (::write(fd_, &device, sizeof(device)) != static_cast<ssize_t>(sizeof(device))) {
        close();
        return false;
    }
    if (::ioctl(fd_, UI_DEV_CREATE) != 0) {
        close();
        return false;
    }
    ready_ = true;
    return true;
}

void UInput::close() {
    if (fd_ < 0) return;
    if (ready_) ::ioctl(fd_, UI_DEV_DESTROY);
    ::close(fd_);
    fd_ = -1;
    ready_ = false;
}

bool UInput::writeEvent(std::uint16_t type, std::uint16_t code, std::int32_t value) {
    if (fd_ < 0) return false;
    struct input_event event{};
    event.type = type;
    event.code = code;
    event.value = value;
    return ::write(fd_, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event));
}

bool UInput::move(int dx, int dy) {
    if (!ready_) return false;
    bool ok = true;
    if (dx != 0) ok = ok && writeEvent(EV_REL, REL_X, dx);
    if (dy != 0) ok = ok && writeEvent(EV_REL, REL_Y, dy);
    return ok && writeEvent(EV_SYN, SYN_REPORT, 0);
}

bool UInput::button(int x11Button, bool down) {
    if (!ready_) return false;
    int code = 0;
    switch (x11Button) {
        case 1: code = BTN_LEFT; break;
        case 2: code = BTN_MIDDLE; break;
        case 3: code = BTN_RIGHT; break;
        default: return false;
    }
    return writeEvent(EV_KEY, code, down ? 1 : 0) && writeEvent(EV_SYN, SYN_REPORT, 0);
}

}  // namespace wv
