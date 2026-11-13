#include "Game.h"

#include <cmath>
#include <cstring>
#include <sstream>

#include "core/Patterns.h"

namespace wv {
namespace {
constexpr float kUnitsToMeters = 1.f / 39.37f;  // Source 单位 ≈ 英寸
constexpr int kMaxEntities = 2048;
}  // namespace

Game::Game(Offsets offsets, std::string process, std::string module)
    : offsets_(offsets), process_(std::move(process)), module_(std::move(module)) {}

bool Game::ensureAttached() {
    if (!memory_.attached()) {
        if (!memory_.attach(process_)) {
            setStatus(memory_.lastError());
            return false;
        }
        offsetsResolved_ = false;
    }
    if (memory_.attached() && !offsetsResolved_) {
        resolveOffsets();
    }
    return memory_.attached();
}

void Game::reloadOffsets(const Offsets& offsets) {
    offsets_ = offsets;
    // 重新走一遍解析：模块基址可能随版本变化，特征码也要重新扫。
    offsetsResolved_ = false;
    clientBase_ = 0;
}

void Game::resolveOffsets() {
    const std::uintptr_t client = memory_.moduleBase(module_);
    clientBase_ = client;
    if (client == 0) {
        setStatus("未找到模块: " + module_);
        return;
    }

    auto resolveRip = [&](const std::string& signature) -> std::uintptr_t {
        if (signature.empty()) return 0;
        const auto hit = memory_.patternScan(module_, signature);
        if (!hit) return 0;
        // mov/lea reg, [rip+disp32]：目标 = 命中地址 + 7 + disp32
        const std::int32_t displacement = memory_.read<std::int32_t>(*hit + 3, 0);
        const std::uintptr_t target = *hit + 7 + displacement;
        return target > client ? target - client : 0;
    };

    if (offsets_.dwEntityList == 0) {
        offsets_.dwEntityList =
            resolveRip(!offsets_.sigEntityList.empty() ? offsets_.sigEntityList
                                                       : std::string(patterns::kEntityList));
    }
    if (offsets_.dwLocalPlayerPawn == 0) {
        offsets_.dwLocalPlayerPawn = resolveRip(
            !offsets_.sigLocalPlayerPawn.empty() ? offsets_.sigLocalPlayerPawn
                                                 : std::string(patterns::kLocalPlayerPawn));
    }
    if (offsets_.dwViewMatrix == 0) {
        offsets_.dwViewMatrix =
            resolveRip(!offsets_.sigViewMatrix.empty() ? offsets_.sigViewMatrix
                                                       : std::string(patterns::kViewMatrix));
    }

    offsetsResolved_ = true;

    std::string missing;
    if (!offsets_.entityListReady()) missing += "dwEntityList ";
    if (!offsets_.localPawnReady()) missing += "dwLocalPlayerPawn ";
    if (!offsets_.viewMatrixReady()) missing += "dwViewMatrix ";
    if (offsets_.m_iHealth == 0) missing += "m_iHealth ";
    if (offsets_.m_pGameSceneNode == 0) missing += "m_pGameSceneNode ";
    if (offsets_.m_vecAbsOrigin == 0) missing += "m_vecAbsOrigin ";
    // m_modelState 仅骨骼连线使用，缺失不影响方框与血量绘制；
    // m_boneArray 允许为 0（本版本骨骼数组就在 m_modelState 处二级偏移 0）。
    if (!missing.empty()) {
        setStatus("偏移缺失: " + missing);
    } else if (offsets_.m_modelState == 0) {
        setStatus("已连接 pid=" + std::to_string(memory_.pid()) + "（骨骼偏移缺失，连线不可用）");
    } else {
        setStatus("已连接 pid=" + std::to_string(memory_.pid()));
    }
}

std::vector<DiagItem> Game::diagnose() const {
    std::vector<DiagItem> items;
    const auto hex = [](std::uintptr_t value) {
        std::ostringstream out;
        out << "0x" << std::hex << value;
        return out.str();
    };

    if (!memory_.attached()) {
        items.push_back({"进程 " + process_, false, "未连接"});
        return items;
    }
    items.push_back({"进程 " + process_, true, "pid=" + std::to_string(memory_.pid())});

    const std::uintptr_t base = clientBase_;
    items.push_back({"模块 " + module_, base != 0, base != 0 ? hex(base) : "未找到"});
    if (base == 0) return items;

    const auto checkOffset = [&](const char* name, std::uintptr_t value) {
        items.push_back({name, value != 0, value != 0 ? hex(value) : "缺失"});
    };
    checkOffset("dwEntityList", offsets_.dwEntityList);
    checkOffset("dwLocalPlayerPawn", offsets_.dwLocalPlayerPawn);
    checkOffset("dwViewMatrix", offsets_.dwViewMatrix);
    checkOffset("m_iHealth", static_cast<std::uintptr_t>(offsets_.m_iHealth));
    checkOffset("m_iTeamNum", static_cast<std::uintptr_t>(offsets_.m_iTeamNum));
    checkOffset("m_ArmorValue", static_cast<std::uintptr_t>(offsets_.m_ArmorValue));
    checkOffset("m_pGameSceneNode", static_cast<std::uintptr_t>(offsets_.m_pGameSceneNode));
    checkOffset("m_vecAbsOrigin", static_cast<std::uintptr_t>(offsets_.m_vecAbsOrigin));
    checkOffset("m_modelState", static_cast<std::uintptr_t>(offsets_.m_modelState));

    // 运行时指针：偏移非零却读出 0，基本就是偏移与当前版本不匹配。
    const std::uintptr_t list = memory_.read<std::uintptr_t>(base + offsets_.dwEntityList, 0);
    const std::uintptr_t local = memory_.read<std::uintptr_t>(base + offsets_.dwLocalPlayerPawn, 0);
    items.push_back({"实体列表指针", list != 0, list != 0 ? hex(list) : "读到 0，偏移可能已失效"});
    items.push_back({"本地玩家指针", local != 0, local != 0 ? hex(local) : "读到 0，偏移可能已失效"});

    return items;
}

std::uintptr_t Game::clientBase() const { return clientBase_; }

Vec3 Game::bonePosition(std::uintptr_t sceneNode, int boneIndex) const {
    const std::uintptr_t modelState = sceneNode + static_cast<std::uintptr_t>(offsets_.m_modelState);
    const std::uintptr_t bones = memory_.read<std::uintptr_t>(modelState +
                                                             offsets_.m_boneArray);
    if (bones == 0) return {};

    const std::uintptr_t boneBase =
        bones + static_cast<std::uintptr_t>(offsets_.boneStride) * boneIndex;
    Vec3 position;
    position.x = memory_.read<float>(boneBase + offsets_.bonePosX);
    position.y = memory_.read<float>(boneBase + offsets_.bonePosY);
    position.z = memory_.read<float>(boneBase + offsets_.bonePosZ);
    return position;
}

void Game::update(const Vec2& screenOrigin, int width, int height) {
    if (!memory_.attached()) {
        Snapshot empty;
        publish(empty);
        return;
    }

    const std::uintptr_t client = clientBase();
    if (client == 0) {
        setStatus("未找到模块: " + module_);
        return;
    }
    if (!offsets_.entityListReady() || !offsets_.localPawnReady() || !offsets_.viewMatrixReady()) {
        Snapshot empty;
        publish(empty);
        return;
    }

    Snapshot snap;
    snap.view = memory_.read<Mat4>(client + offsets_.dwViewMatrix);
    const std::uintptr_t list = memory_.read<std::uintptr_t>(client + offsets_.dwEntityList);
    const std::uintptr_t localPawn =
        memory_.read<std::uintptr_t>(client + offsets_.dwLocalPlayerPawn);

    if (list == 0 || localPawn == 0) {
        publish(snap);
        return;
    }
    snap.localPawn = localPawn;

    const std::uintptr_t localScene =
        memory_.read<std::uintptr_t>(localPawn + offsets_.m_pGameSceneNode);
    if (localScene != 0) {
        snap.localScene = localScene;
        snap.localOrigin = memory_.read<Vec3>(localScene + offsets_.m_vecAbsOrigin);
    }
    snap.localTeam = memory_.read<int>(localPawn + offsets_.m_iTeamNum, 0);

    // 后坐力角度两级跳：pawn + m_pAimPunchServices 取到服务对象指针，
    // 再读 m_predictableBaseAngle 的前两个 float（QAngle 的 pitch / yaw）。
    if (offsets_.m_pAimPunchServices != 0 && offsets_.m_aimPunchAngle != 0) {
        const std::uintptr_t punchServices = memory_.read<std::uintptr_t>(
            localPawn + static_cast<std::uintptr_t>(offsets_.m_pAimPunchServices));
        if (punchServices != 0) {
            snap.punchServices = punchServices;
            snap.aimPunch = memory_.read<Vec2>(
                punchServices + static_cast<std::uintptr_t>(offsets_.m_aimPunchAngle));
        }
    }

    if (offsets_.m_iIDEntIndex != 0) {
        const int handle = memory_.read<int>(localPawn + offsets_.m_iIDEntIndex, -1);
        snap.crosshairIndex = handle >= 0 ? (handle & offsets_.entityHandleMask) : -1;
    }

    // 实体列表按 chunk 页组织：整页一次读入，避免逐槽 pread 拖慢每帧。
    const int pageSize = offsets_.entityChunkMask + 1;
    const int pageCount = (kMaxEntities + pageSize - 1) / pageSize;
    const std::size_t pageBytes =
        static_cast<std::size_t>(pageSize) * static_cast<std::size_t>(offsets_.entityChunkStride);
    std::vector<std::uint8_t> pageBuffer(pageBytes);

    const float unitsToMeters = kUnitsToMeters;
    for (int page = 0; page < pageCount; ++page) {
        const std::uintptr_t chunk = memory_.read<std::uintptr_t>(
            list + offsets_.entityListChunkBase +
            offsets_.entityChunkPtrStride * static_cast<std::uintptr_t>(page));
        if (chunk == 0) continue;
        const bool bulk = memory_.readBytes(chunk, pageBuffer.data(), pageBytes);

        for (int slot = 0; slot < pageSize; ++slot) {
            const int i = page * pageSize + slot;
            if (i == 0) continue;

            const std::size_t slotOffset =
                static_cast<std::size_t>(slot) * offsets_.entityChunkStride;
            std::uintptr_t pawn = 0;
            if (bulk) {
                std::memcpy(&pawn, pageBuffer.data() + slotOffset, sizeof(pawn));
            } else {
                pawn = memory_.read<std::uintptr_t>(chunk + slotOffset);
            }
            if (pawn == 0 || pawn == localPawn) continue;

            const int health = memory_.read<int>(pawn + offsets_.m_iHealth, 0);
            if (health <= 0 || health > 100) continue;

            const int team = memory_.read<int>(pawn + offsets_.m_iTeamNum, 0);
            if (team != 2 && team != 3) continue;

            if (offsets_.m_lifeState != 0 &&
                memory_.read<std::uint8_t>(pawn + offsets_.m_lifeState, 1) != 0) {
                continue;
            }

            const std::uintptr_t scene =
                memory_.read<std::uintptr_t>(pawn + offsets_.m_pGameSceneNode);
            if (scene == 0) continue;

            Vec3 origin = memory_.read<Vec3>(scene + offsets_.m_vecAbsOrigin);
            if (origin.x == 0.f && origin.y == 0.f && origin.z == 0.f) continue;
            origin.z += 8.f;  // m_vecAbsOrigin 在胶囊中心附近，下移到脚底

            // 骨骼数组指针位于 scene + m_modelState（本机实测 0x1c0），
            // m_boneArray 作为相对该位置的二次偏移，为 0 时直接取该指针。
            const bool bonesReady = offsets_.m_modelState != 0;
            Vec3 head;
            if (bonesReady) head = bonePosition(scene, offsets_.boneHead);
            if (!bonesReady || (head.x == 0.f && head.y == 0.f && head.z == 0.f)) {
                head = origin;
                head.z += 62.f;  // 骨骼不可用时退化为固定身高
            }

            PlayerInfo info;
            info.index = i;
            info.team = team;
            info.health = health;
            // 护甲偏移未配置时保持 -1，叠加层据此跳过绘制。
            if (offsets_.m_ArmorValue != 0) {
                info.armor = memory_.read<int>(pawn + offsets_.m_ArmorValue, -1);
            }
            info.origin = origin;
            info.head = head;

            if (offsets_.m_bSpotted != 0) {
                info.visible = memory_.read<std::uint8_t>(pawn + offsets_.m_bSpotted, 1) != 0;
            }

            if (!worldToScreen(origin, snap.view, screenOrigin, width, height, info.feetScreen)) {
                continue;
            }
            if (!worldToScreen(head, snap.view, screenOrigin, width, height, info.headScreen)) {
                continue;
            }
            info.distance = (origin - snap.localOrigin).length() * unitsToMeters;

            const int chain[4] = {offsets_.boneHead, offsets_.boneNeck, offsets_.boneChest,
                                  offsets_.bonePelvis};
            Vec2 chainScreen[4];
            bool chainValid = bonesReady;
            if (chainValid) {
                for (int k = 0; k < 4; ++k) {
                    const Vec3 bone = bonePosition(scene, chain[k]);
                    if (!worldToScreen(bone, snap.view, screenOrigin, width, height, chainScreen[k])) {
                        chainValid = false;
                        break;
                    }
                }
            }
            if (chainValid) {
                for (int k = 0; k < 3; ++k) {
                    info.skeleton.emplace_back(chainScreen[k], chainScreen[k + 1]);
                }
            }

            snap.players.push_back(std::move(info));
        }
    }

    snap.valid = true;
    publish(snap);
}

void Game::publish(const Snapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snapshot;
}

Snapshot Game::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void Game::setStatus(std::string text) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = std::move(text);
}

std::string Game::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

}  // namespace wv
