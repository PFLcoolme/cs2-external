#pragma once
#include <atomic>
#include <memory>
#include <string>

#include "Offsets.h"
#include "OffsetsImporter.h"

namespace wv {

struct AimbotConfig {
    bool enabled{false};
    // 触发条件：开镜键与开火键同时按住才执行；0 = 不要求该按键
    int scopeKey{3};  // X11 键码：3 = 鼠标右键（开镜）
    int fireKey{1};   // 1 = 鼠标左键（开火）
    float fovPixels{220.f};
    int smooth{5};
    float sensitivityFactor{0.45f};
    bool teamCheck{true};
    float maxDistance{0.f};  // 0 = 不限
};

struct TriggerConfig {
    bool enabled{false};
    int key{0};  // 0 = 不使用按键（仅准星判定）
    int delayMs{30};
    int cooldownMs{120};
    bool teamCheck{true};
};

struct EspConfig {
    bool enabled{true};
    bool box{true};
    bool health{true};
    bool distance{true};
    bool skeleton{false};
    bool teamCheck{false};
    bool visibilityCheck{true};
    float maxDistance{0.f};
};

struct Config {
    std::string process{"cs2"};
    std::string module{"libclient.so"};
    // 可选的 cs2-dumper 输出文件；导入后覆盖 offsets 段
    std::string offsetsFile;
    std::string offsetsSource{"config.json 内嵌"};
    Offsets offsets;
    AimbotConfig aim;
    TriggerConfig trigger;
    EspConfig esp;

    static Config load(const std::string& path);
    void save(const std::string& path) const;
};

// 跨线程共享：配置以整体替换的方式交换，读取方永远拿到不可变副本。
struct AppState {
    std::atomic<std::shared_ptr<Config>> config{nullptr};
    std::atomic<std::uintptr_t> gameWindow{0};
};

}  // namespace wv
