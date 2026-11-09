#include "Memory.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace wv {
namespace {

bool isNumeric(const char* text) {
    if (!text || !*text) return false;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    return true;
}

std::string baseName(const std::string& path) {
    const std::size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

int findPidByName(const std::string& exeName) {
    DIR* dir = ::opendir("/proc");
    if (!dir) return 0;

    int found = 0;
    while (dirent* entry = ::readdir(dir)) {
        if (!isNumeric(entry->d_name)) continue;

        const std::string pidPath = std::string("/proc/") + entry->d_name;
        std::ifstream comm(pidPath + "/comm");
        std::string name;
        if (!std::getline(comm, name)) continue;

        if (!name.empty() && name.back() == '\n') name.pop_back();

        bool match = (name == exeName);
        if (!match) {
            char buffer[PATH_MAX]{};
            const ssize_t len = ::readlink((pidPath + "/exe").c_str(), buffer, sizeof(buffer) - 1);
            if (len > 0) {
                buffer[len] = '\0';
                match = baseName(buffer) == exeName;
            }
        }
        if (match) {
            found = std::atoi(entry->d_name);
            break;
        }
    }
    ::closedir(dir);
    return found;
}

struct PatternByte {
    std::uint8_t value{0};
    bool wildcard{false};
};

bool parsePattern(const std::string& signature, std::vector<PatternByte>& out) {
    std::istringstream stream(signature);
    std::string token;
    while (stream >> token) {
        PatternByte byte;
        if (token == "?" || token == "??") {
            byte.wildcard = true;
        } else {
            unsigned int value = 0;
            if (std::sscanf(token.c_str(), "%x", &value) != 1 || value > 0xFF) return false;
            byte.value = static_cast<std::uint8_t>(value);
        }
        out.push_back(byte);
    }
    return !out.empty();
}

}  // namespace

Memory::~Memory() { detach(); }

bool Memory::attach(const std::string& exeName) {
    detach();

    pid_ = findPidByName(exeName);
    if (pid_ == 0) {
        error_ = "未找到进程: " + exeName;
        return false;
    }

    const std::string memPath = "/proc/" + std::to_string(pid_) + "/mem";
    memFd_ = ::open(memPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (memFd_ < 0) {
        const int saved = errno;
        error_ = "打开 " + memPath + " 失败: " + std::strerror(saved);
        if (saved == EACCES || saved == EPERM) {
            error_ += "（需要 root，或先执行 sudo sysctl -w kernel.yama.ptrace_scope=0）";
        }
        pid_ = 0;
        return false;
    }

    error_.clear();
    return true;
}

void Memory::detach() {
    if (memFd_ >= 0) {
        ::close(memFd_);
        memFd_ = -1;
    }
    pid_ = 0;
}

bool Memory::readBytes(std::uintptr_t address, void* out, std::size_t size) const {
    if (memFd_ < 0 || !out || size == 0) return false;

    std::size_t done = 0;
    auto* bytes = static_cast<std::uint8_t*>(out);
    while (done < size) {
        const ssize_t n = ::pread64(memFd_, bytes + done, size - done,
                                    static_cast<off64_t>(address + done));
        if (n <= 0) return false;  // 跨入不可读页
        done += static_cast<std::size_t>(n);
    }
    return true;
}

std::vector<Memory::Mapping> Memory::mappings() const {
    std::vector<Mapping> result;
    if (pid_ == 0) return result;

    std::ifstream maps("/proc/" + std::to_string(pid_) + "/maps");
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long start = 0;
        unsigned long end = 0;
        unsigned long offset = 0;
        unsigned long inode = 0;
        char permissions[5]{};
        char device[16]{};
        int pathPos = 0;

        if (std::sscanf(line.c_str(), "%lx-%lx %4s %lx %15s %lu %n", &start, &end, permissions,
                        &offset, device, &inode, &pathPos) < 6) {
            continue;
        }

        Mapping mapping;
        mapping.start = start;
        mapping.end = end;
        mapping.offset = offset;
        mapping.permissions = permissions;
        if (pathPos > 0 && static_cast<std::size_t>(pathPos) < line.size()) {
            mapping.path = line.substr(static_cast<std::size_t>(pathPos));
        }
        result.push_back(std::move(mapping));
    }
    return result;
}

std::uintptr_t Memory::moduleBase(const std::string& moduleName) const {
    std::uintptr_t lowest = 0;
    for (const Mapping& mapping : mappings()) {
        if (mapping.path.empty() || baseName(mapping.path) != moduleName) continue;
        if (mapping.permissions.empty() || mapping.permissions[0] != 'r') continue;
        const std::uintptr_t candidate = mapping.start - mapping.offset;
        if (lowest == 0 || candidate < lowest) lowest = candidate;
    }
    return lowest;
}

std::size_t Memory::moduleSize(const std::string& moduleName) const {
    std::uintptr_t lowest = 0;
    std::uintptr_t highest = 0;
    for (const Mapping& mapping : mappings()) {
        if (mapping.path.empty() || baseName(mapping.path) != moduleName) continue;
        if (lowest == 0 || mapping.start < lowest) lowest = mapping.start;
        if (mapping.end > highest) highest = mapping.end;
    }
    return highest > lowest ? static_cast<std::size_t>(highest - lowest) : 0;
}

std::optional<std::uintptr_t> Memory::patternScan(const std::string& moduleName,
                                                  const std::string& signature,
                                                  std::ptrdiff_t offset) const {
    std::vector<PatternByte> pattern;
    if (!parsePattern(signature, pattern)) return std::nullopt;

    const std::uintptr_t base = moduleBase(moduleName);
    const std::size_t size = moduleSize(moduleName);
    if (base == 0 || size < pattern.size()) return std::nullopt;

    constexpr std::size_t kChunk = 8u * 1024u * 1024u;
    const std::size_t overlap = pattern.size() - 1;
    std::vector<std::uint8_t> buffer(kChunk + overlap);

    for (std::size_t position = 0; position < size;) {
        const std::size_t readSize = std::min(kChunk + overlap, size - position);
        if (!readBytes(base + position, buffer.data(), readSize)) return std::nullopt;

        const std::size_t last = readSize - pattern.size();
        for (std::size_t i = 0; i <= last; ++i) {
            bool matched = true;
            for (std::size_t j = 0; j < pattern.size(); ++j) {
                if (!pattern[j].wildcard && buffer[i + j] != pattern[j].value) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return base + position + i + static_cast<std::uintptr_t>(offset);
            }
        }

        if (readSize <= overlap) break;
        position += kChunk;
    }
    return std::nullopt;
}

}  // namespace wv
