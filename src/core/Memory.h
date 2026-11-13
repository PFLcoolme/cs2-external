#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wv {

// Linux 进程内存读取封装。
// 参考：man 2 ptrace / man 5 proc / man 2 pread / man 2 process_vm_readv
//   /proc/<pid>/maps        模块映射（基址/大小/路径）
//   /proc/<pid>/mem         pread64 读取目标地址空间
//   /proc/<pid>/comm        进程名
//
// 权限：内核默认 kernel.yama.ptrace_scope=1 时，非父进程读取需要
// root 或 CAP_SYS_PTRACE，或先执行 sysctl kernel.yama.ptrace_scope=0。
class Memory {
public:
    Memory() = default;
    ~Memory();

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    bool attach(const std::string& exeName);
    void detach();

    bool attached() const noexcept { return pid_ > 0; }
    int pid() const noexcept { return pid_; }
    const std::string& lastError() const { return error_; }

    template <typename T>
    bool readInto(std::uintptr_t address, T& out) const {
        return readBytes(address, &out, sizeof(T));
    }

    // 读取失败时返回 fallback，调用方无需检查。
    template <typename T>
    T read(std::uintptr_t address, T fallback = T{}) const {
        T value{};
        return readInto(address, value) ? value : fallback;
    }

    bool readBytes(std::uintptr_t address, void* out, std::size_t size) const;

    // 直接写目标地址空间（pwrite64）。fd 以 O_RDWR 打开时可用；
    // 只读回退（权限不足）时 writable() 为假，所有写入被拒绝。
    bool writeBytes(std::uintptr_t address, const void* data, std::size_t size) const;
    template <typename T>
    bool write(std::uintptr_t address, const T& value) const {
        return writeBytes(address, &value, sizeof(T));
    }
    bool writable() const noexcept { return writable_.load(); }

    std::uintptr_t moduleBase(const std::string& moduleName) const;
    std::size_t moduleSize(const std::string& moduleName) const;

    // 特征码 "48 8B 05 ?? ?? ?? ?? 48 85 C0"（?? 为通配）。
    // 分块读取模块，避免一次性申请数百 MB。
    std::optional<std::uintptr_t> patternScan(const std::string& moduleName,
                                              const std::string& signature,
                                              std::ptrdiff_t offset = 0) const;

private:
    struct Mapping {
        std::uintptr_t start{0};
        std::uintptr_t end{0};
        std::uintptr_t offset{0};
        std::string permissions;
        std::string path;
    };

    std::vector<Mapping> mappings() const;

    int pid_{0};
    int memFd_{-1};
    // attach/detach 在主线程，引擎线程每 2 ms 读一次，必须原子。
    std::atomic_bool writable_{false};
    std::string error_;
};

}  // namespace wv
