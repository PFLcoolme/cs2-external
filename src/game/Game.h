#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/Math.h"
#include "core/Memory.h"
#include "core/Offsets.h"

namespace wv {

struct PlayerInfo {
    int index{0};
    int team{0};
    int health{0};
    bool visible{true};  // m_bSpotted；偏移未配置时恒为 true
    Vec3 origin{};  // 脚底世界坐标
    Vec3 head{};    // 头部骨骼世界坐标
    Vec2 headScreen{};
    Vec2 feetScreen{};
    float distance{0.f};  // 米
    std::vector<std::pair<Vec2, Vec2>> skeleton;
};

struct Snapshot {
    bool valid{false};
    Mat4 view{};
    Vec3 localOrigin{};
    int localTeam{0};
    int crosshairIndex{-1};
    std::vector<PlayerInfo> players;
};

// 采集游戏状态，产出与渲染/输入无关的纯数据快照。
class Game {
public:
    Game(Offsets offsets, std::string process, std::string module);

    bool ensureAttached();
    void update(const Vec2& screenOrigin, int width, int height);

    Snapshot snapshot() const;
    bool attached() const { return memory_.attached(); }
    int pid() const { return memory_.pid(); }
    std::string status() const;

private:
    std::uintptr_t clientBase() const;
    Vec3 bonePosition(std::uintptr_t sceneNode, int boneIndex) const;
    void resolveOffsets();
    void publish(const Snapshot& snapshot);
    void setStatus(std::string text);

    Memory memory_;
    Offsets offsets_;
    std::string process_;
    std::string module_;
    bool offsetsResolved_{false};
    std::uintptr_t clientBase_{0};

    mutable std::mutex mutex_;
    Snapshot snapshot_;
    std::string status_{"未连接"};
};

}  // namespace wv
