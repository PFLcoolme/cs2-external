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
    int armor{-1};  // 护甲值；-1 = 偏移未配置，不绘制
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
    // 后坐力角度（度）：x = pitch，y = yaw；无偏移或未开火时为零向量。
    Vec2 aimPunch{};
    int crosshairIndex{-1};
    // 直接内存引擎写入的基址：本地 pawn 与 punch 服务对象。
    // 随快照发布，避免引擎线程跨线程访问 Game 内部状态。
    std::uintptr_t localPawn{0};
    std::uintptr_t localScene{0};  // CGameSceneNode：m_angRotation 的基址
    std::uintptr_t punchServices{0};
    std::vector<PlayerInfo> players;
};

// 自检条目：逐项列出关键偏移与运行时指针是否可用。
struct DiagItem {
    std::string name;
    bool ok{false};
    std::string detail;
};

// 采集游戏状态，产出与渲染/输入无关的纯数据快照。
class Game {
public:
    Game(Offsets offsets, std::string process, std::string module);

    // 逐项自检：偏移是否非零、模块基址与关键指针是否读得到。
    // 面板据此给出「缺什么」，不必再去翻日志。
    std::vector<DiagItem> diagnose() const;

    bool ensureAttached();
    void update(const Vec2& screenOrigin, int width, int height);

    // 运行时替换偏移：清掉已解析标记，下一次 ensureAttached 会重新解析模块基址与特征码。
    // 调用方需保证与 update() 同线程（本项目都在主线程）。
    void reloadOffsets(const Offsets& offsets);

    Snapshot snapshot() const;
    bool attached() const { return memory_.attached(); }
    int pid() const { return memory_.pid(); }
    std::string status() const;

    // 直接内存通道：与采集共用同一个 fd，pread/pwrite 都带显式偏移，线程安全。
    bool writable() const { return memory_.writable(); }
    bool writeBytes(std::uintptr_t address, const void* data, std::size_t size) const {
        return memory_.writeBytes(address, data, size);
    }
    template <typename T>
    bool write(std::uintptr_t address, const T& value) const {
        return memory_.write(address, value);
    }
    template <typename T>
    T read(std::uintptr_t address, T fallback = T{}) const {
        return memory_.read<T>(address, fallback);
    }
    // 模块基址：引擎解析实体列表、定位 VData 链时需要。
    std::uintptr_t clientBase() const;

private:
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
