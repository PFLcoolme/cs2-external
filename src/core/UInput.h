#pragma once
#include <cstdint>

namespace wv {

// 常驻虚拟鼠标（/dev/uinput）。XTest 合成的是 X11 事件，CS2 走 SDL raw input
// 时完全忽略；内核级 uinput 设备会被当成真实硬件枚举并消费。
// 设备必须在进程存活期间一直保持：SDL 需要若干秒枚举热插拔设备，
// 临时创建再立刻销毁的探针不会被识别。
class UInput {
public:
    UInput() = default;
    ~UInput();

    UInput(const UInput&) = delete;
    UInput& operator=(const UInput&) = delete;

    // 创建虚拟鼠标。失败时调用方回退到 XTest。
    bool open();
    void close();
    bool ready() const noexcept { return ready_; }

    bool move(int dx, int dy);
    // x11Button：1 = 左键、2 = 中键、3 = 右键，与项目其它配置的编码一致。
    bool button(int x11Button, bool down);

private:
    // 不能叫 emit：Qt6 把 emit 定义为宏，头文件被 Qt 侧包含时会语法错误。
    bool writeEvent(std::uint16_t eventType, std::uint16_t code, std::int32_t value);

    int fd_{-1};
    bool ready_{false};
};

}  // namespace wv
