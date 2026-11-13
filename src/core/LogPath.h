#pragma once
#include <unistd.h>

#include <cstdio>
#include <string>

namespace wv {

// 诊断日志路径。优先沿用共享的 /tmp/wv_lite.log（与历史日志、root 实例一致）；
// 但该文件被 root 跑过一次后会变成 644 root:root，普通用户再启动就完全写不进去，
// 所有诊断静默丢失——「功能没反应」时看不到任何证据。此时退回带 uid 的私有文件。
// 四个写入/读取点（Main / Features / MemoryEngine / Menu）必须共用这一个函数，
// 否则会出现「写到 A、面板读 B」的分叉。
inline const std::string& logPath() {
    static const std::string path = []() -> std::string {
        constexpr const char* kShared = "/tmp/wv_lite.log";
        if (FILE* probe = std::fopen(kShared, "a")) {
            std::fclose(probe);
            return kShared;
        }
        return "/tmp/wv_lite_" + std::to_string(static_cast<long>(::getuid())) + ".log";
    }();
    return path;
}

}  // namespace wv
