#include "MemoryEngine.h"

#include <X11/Xlib.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/LogPath.h"
#include "features/Features.h"

namespace wv {
namespace {

constexpr auto kStepInterval = std::chrono::milliseconds(2);
// 角度锁开启时提到 1 ms：客户端每 tick 会重算 eye angles，写得更密才能压住。
constexpr auto kLockStepInterval = std::chrono::milliseconds(1);

// 真子弹追踪的目标选择：不看准星、不看 FOV，取距离最近的存活敌人。
// 准星可以指向任何地方，弹道结算角度由 m_angEyeAngles 单独决定。
const PlayerInfo* pickTarget(const Config& config, const Snapshot& snapshot) {
    const PlayerInfo* best = nullptr;
    for (const PlayerInfo& player : snapshot.players) {
        if (player.health <= 0) continue;
        if (config.memory.bulletTrackTeamCheck && player.team == snapshot.localTeam) continue;
        if (config.memory.bulletTrackMaxDistance > 0.f &&
            player.distance > config.memory.bulletTrackMaxDistance) {
            continue;
        }
        if (!best || player.distance < best->distance) best = &player;
    }
    return best;
}

// pawn → weaponServices → m_hActiveWeapon → 实体列表 → 武器实体。
// 任一级断链返回 0，调用方按「无武器」跳过，不猜测布局。
std::uintptr_t resolveActiveWeapon(Game& game, std::uintptr_t pawn, const Offsets& o) {
    if (o.m_pWeaponServices == 0 || o.m_hActiveWeapon == 0 || o.dwEntityList == 0) return 0;
    const std::uintptr_t services =
        game.read<std::uintptr_t>(pawn + static_cast<std::uintptr_t>(o.m_pWeaponServices), 0);
    if (services == 0) return 0;
    const std::uint32_t handle = game.read<std::uint32_t>(
        services + static_cast<std::uintptr_t>(o.m_hActiveWeapon), 0xFFFFFFFFu);
    const int index = static_cast<int>(handle & 0x7FFFu);
    if (handle == 0xFFFFFFFFu || index <= 0) return 0;

    const std::uintptr_t base = game.clientBase();
    if (base == 0) return 0;
    const std::uintptr_t system = game.read<std::uintptr_t>(base + o.dwEntityList, 0);
    if (system == 0) return 0;
    const std::uintptr_t chunk = game.read<std::uintptr_t>(
        system + o.entityListChunkBase +
            o.entityChunkPtrStride *
                static_cast<std::uintptr_t>(index >> o.entityChunkShift),
        0);
    if (chunk == 0) return 0;
    return game.read<std::uintptr_t>(
        chunk + static_cast<std::uintptr_t>(o.entityChunkStride) *
                    static_cast<std::uintptr_t>(index & o.entityChunkMask),
        0);
}

// 每 0.5 s 落一行证据：功能「没反应」时先看这行，能区分
// 只读回退 / 快照无效 / 偏移缺失 / 开关没开，不必靠猜。
void memTrace(const char* note, const AppState& state, const Snapshot& snapshot,
              const MemoryConfig& m, int missing, int targetDistance, int lockValid) {
    static long tick = 0;
    if (++tick % 250 != 0) return;
    if (FILE* file = std::fopen(logPath().c_str(), "a")) {
        std::fprintf(file,
                     "[mem] %s writable=%d pawn=0x%lx scene=0x%lx punch=0x%lx "
                     "missing=%d writes=%d target=%d lock=%d speed=%d bullet=%d fly=%d "
                     "spin=%d recoil=%d acc=%d accsafe=%d syncview=%d\n",
                     note, state.memoryWritable.load(),
                     static_cast<unsigned long>(snapshot.localPawn),
                     static_cast<unsigned long>(snapshot.localScene),
                     static_cast<unsigned long>(snapshot.punchServices), missing,
                     state.memoryWrites.load(), targetDistance, lockValid, m.speed ? 1 : 0,
                     m.bulletTrack ? 1 : 0, m.fly ? 1 : 0, m.spin ? 1 : 0,
                     m.noRecoil ? 1 : 0, m.accuracy ? 1 : 0,
                     m.accuracySafeMode ? 1 : 0, m.bulletLockSyncView ? 1 : 0);
        std::fclose(file);
    }
}

}  // namespace

MemoryEngine::MemoryEngine(Game& game, AppState& state) : game_(game), state_(state) {
    lastStep_ = std::chrono::steady_clock::now();
}

MemoryEngine::~MemoryEngine() { stop(); }

void MemoryEngine::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&MemoryEngine::run, this);
}

void MemoryEngine::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void MemoryEngine::run() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        running_.store(false);
        return;
    }

    while (running_.load()) {
        const std::shared_ptr<Config> config = state_.config.load();
        state_.memoryWritable.store(game_.writable() ? 1 : 0);
        const Snapshot snapshot = game_.snapshot();
        const auto interval =
            (config && config->memory.bulletTrack) ? kLockStepInterval : kStepInterval;

        // Panic / 未连接 / 未开功能：只做收尾恢复，不再写任何功能字段。
        // 恢复是一次性的，标志清零后 restore 变成 no-op。
        if (!config || state_.panicMode.load() != 0 || !game_.attached() ||
            !game_.writable() || !snapshot.valid || snapshot.localPawn == 0) {
            if (config) restore(*config, snapshot);
            state_.memoryMissing.store(0);
            memTrace("skip", state_, snapshot, config ? config->memory : MemoryConfig{}, 0,
                     -1, 0);
            std::this_thread::sleep_for(interval);
            continue;
        }

        step(display, *config, snapshot);
        memTrace("step", state_, snapshot, config->memory, state_.memoryMissing.load(),
                 lastTargetDistance_, lockAnglesValid_ ? 1 : 0);
        std::this_thread::sleep_for(interval);
    }

    // 退出前再恢复一次，避免进程结束留下被改写的字段。
    if (const std::shared_ptr<Config> config = state_.config.load()) {
        restore(*config, game_.snapshot());
    }
    XCloseDisplay(display);
}

void MemoryEngine::step(void* display, const Config& config, const Snapshot& snapshot) {
    const Offsets& o = config.offsets;
    const MemoryConfig& m = config.memory;
    const std::uintptr_t pawn = snapshot.localPawn;
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::clamp(
        std::chrono::duration<float>(now - lastStep_).count(), 0.f, 0.05f);
    lastStep_ = now;

    int missing = 0;
    int writes = 0;
    const auto commit = [&](std::uintptr_t address, const void* data, std::size_t size) {
        if (game_.writeBytes(address, data, size)) ++writes;
    };

    // ---- 1. 真无后坐力：直接清 punch 源，不是 XTest 的反向补偿 ----
    // punch 角度在 punchServices 上（两级跳），不在 pawn 上。
    if (m.noRecoil) {
        const Angle3 zero{0.f, 0.f, 0.f};
        if (o.m_aimPunchAngle == 0) {
            ++missing;
        } else if (snapshot.punchServices != 0) {
            const std::uintptr_t services = snapshot.punchServices;
            commit(services + static_cast<std::uintptr_t>(o.m_aimPunchAngle), zero.data(),
                   sizeof(Angle3));
            // 实机取证：只清 base 时 m_predictableBaseAngleVel 仍恒为 -26，
            // 游戏每 tick 用角速度重建 punch，视觉后坐力完全没消失。
            if (o.m_aimPunchVel != 0) {
                commit(services + static_cast<std::uintptr_t>(o.m_aimPunchVel), zero.data(),
                       sizeof(Angle3));
            }
            if (o.m_unpredictableBaseAngle != 0) {
                commit(services + static_cast<std::uintptr_t>(o.m_unpredictableBaseAngle),
                       zero.data(), sizeof(Angle3));
            }
        }
        // 视觉 punch 在 CPlayer_CameraServices 上，需要两级跳取服务对象指针。
        if (o.m_pCameraServices != 0 && o.m_vecCsViewPunchAngle != 0) {
            const std::uintptr_t camera = game_.read<std::uintptr_t>(
                pawn + static_cast<std::uintptr_t>(o.m_pCameraServices), 0);
            if (camera != 0) {
                commit(camera + static_cast<std::uintptr_t>(o.m_vecCsViewPunchAngle),
                       zero.data(), sizeof(Angle3));
            }
        }
        // 缓存长度必须由配置给出：默认 0 时不动，避免越界覆盖相邻字段。
        if (o.m_aimPunchCache != 0 && o.aimPunchCacheBytes > 0) {
            std::vector<std::uint8_t> zeros(
                static_cast<std::size_t>(std::clamp(o.aimPunchCacheBytes, 0, 256)), 0u);
            if (!zeros.empty()) {
                commit(pawn + static_cast<std::uintptr_t>(o.m_aimPunchCache), zeros.data(),
                       zeros.size());
            }
        }
    }

    // ---- 2. 加速：倍率字段幂等；再按基准值放大 movement services 的真实速度上限 ----
    if (m.speed) {
        const float factor = std::clamp(m.speedFactor, 1.f, 5.f);
        bool wrote = false;
        if (o.m_flVelocityModifier != 0) {
            commit(pawn + static_cast<std::uintptr_t>(o.m_flVelocityModifier), &factor,
                   sizeof(factor));
            wrote = true;
        }
        // 实机：只写 m_flVelocityModifier 时值能保持为 1.25，但移动速度由
        // CPlayer_MovementServices.m_flMaxspeed 决定，必须一并放大。
        if (o.m_pMovementServices != 0 && o.m_flMaxspeed != 0) {
            const std::uintptr_t movement = game_.read<std::uintptr_t>(
                pawn + static_cast<std::uintptr_t>(o.m_pMovementServices), 0);
            if (movement != 0) {
                const std::uintptr_t address =
                    movement + static_cast<std::uintptr_t>(o.m_flMaxspeed);
                if (!maxspeedSaved_) {
                    const float base = game_.read<float>(address, 0.f);
                    // 只在读到合理上限时缓存基准，避免把被自己写过的放大值当基准。
                    if (base > 0.f && base < 2000.f) {
                        savedMaxspeed_ = base;
                        maxspeedSaved_ = true;
                    }
                }
                if (maxspeedSaved_) {
                    const float boosted = savedMaxspeed_ * factor;
                    commit(address, &boosted, sizeof(boosted));
                    wrote = true;
                }
            }
        }
        if (!wrote) ++missing;
    } else if (maxspeedSaved_ && o.m_pMovementServices != 0 && o.m_flMaxspeed != 0) {
        const std::uintptr_t movement = game_.read<std::uintptr_t>(
            pawn + static_cast<std::uintptr_t>(o.m_pMovementServices), 0);
        if (movement != 0) {
            commit(movement + static_cast<std::uintptr_t>(o.m_flMaxspeed), &savedMaxspeed_,
                   sizeof(savedMaxspeed_));
        }
        maxspeedSaved_ = false;
    }

    // ---- 3. 飞天：按住上升/下降键写绝对垂直速度，松开立即停手 ----
    // flyUpKey 配成 0 时 keyDown 恒真，等于常驻上升。
    const bool flyUp = m.fly && (m.flyUpKey <= 0 ||
                                 Features::keyDown(display, m.flyUpKey));
    const bool flyDown = m.fly && m.flyDownKey > 0 &&
                         Features::keyDown(display, m.flyDownKey);
    if (flyUp || flyDown) {
        if (o.m_MoveType != 0 && m.flyMoveType > 0) {
            if (!moveTypeSaved_) {
                savedMoveType_ = game_.read<std::uint8_t>(
                    pawn + static_cast<std::uintptr_t>(o.m_MoveType), 0);
                moveTypeSaved_ = true;
            }
            const std::uint8_t moveType = static_cast<std::uint8_t>(m.flyMoveType);
            commit(pawn + static_cast<std::uintptr_t>(o.m_MoveType), &moveType,
                   sizeof(moveType));
        }
        if (o.m_vecVelocity == 0 && o.m_vecAbsVelocity == 0) {
            ++missing;
        } else {
            const float vertical = flyUp ? m.flySpeed : -m.flySpeed;
            // 两个速度字段都写：m_vecVelocity 是网络字段，m_vecAbsVelocity
            // 是渲染/预测读的绝对速度，只写其一会被引擎拆开覆盖。
            for (const int field : {o.m_vecVelocity, o.m_vecAbsVelocity}) {
                if (field == 0) continue;
                const std::uintptr_t address = pawn + static_cast<std::uintptr_t>(field);
                Vec3 velocity = game_.read<Vec3>(address);
                velocity.z = vertical;
                commit(address, &velocity, sizeof(velocity));
            }
        }
    } else if (moveTypeSaved_ && o.m_MoveType != 0) {
        commit(pawn + static_cast<std::uintptr_t>(o.m_MoveType), &savedMoveType_,
               sizeof(savedMoveType_));
        moveTypeSaved_ = false;
    }

    // ---- 4. 身体旋转：只写模型 yaw，相机角与射击角都不动 ----
    if (m.spin) {
        // 实机取证（阶段四十九）：写 m_angAbsRotation(0xd4) 会带动渲染世界旋转，
        // 画面持续转动 —— 用户报的「视角一直在动」。spinKeepView 默认真时只写
        // m_angRotation(0xb8)，模型本地旋转，画面静止；置假才追求可见旋转。
        const bool writeAbs = !m.spinKeepView && o.m_angAbsRotation != 0;
        const bool any = o.m_angRotation != 0 || writeAbs;
        if (!any || snapshot.localScene == 0) {
            ++missing;
        } else {
            const std::uintptr_t scene = snapshot.localScene;
            if (o.m_angRotation != 0 && !rotationSaved_) {
                savedRotation_ = game_.read<Angle3>(
                    scene + static_cast<std::uintptr_t>(o.m_angRotation));
                rotationSaved_ = true;
            }
            if (writeAbs && !absRotationSaved_) {
                savedAbsRotation_ = game_.read<Angle3>(
                    scene + static_cast<std::uintptr_t>(o.m_angAbsRotation));
                absRotationSaved_ = true;
            }
            spinAngle_ += m.spinDegreesPerSec * dt;
            if (spinAngle_ > 360.f) spinAngle_ -= 360.f;
            const Angle3 rotation{0.f, spinAngle_, 0.f};
            if (o.m_angRotation != 0) {
                commit(scene + static_cast<std::uintptr_t>(o.m_angRotation), rotation.data(),
                       sizeof(Angle3));
            }
            if (writeAbs) {
                commit(scene + static_cast<std::uintptr_t>(o.m_angAbsRotation), rotation.data(),
                       sizeof(Angle3));
            }
        }
    } else {
        if (rotationSaved_ && o.m_angRotation != 0 && snapshot.localScene != 0) {
            commit(snapshot.localScene + static_cast<std::uintptr_t>(o.m_angRotation),
                   savedRotation_.data(), sizeof(Angle3));
            rotationSaved_ = false;
        }
        if (absRotationSaved_ && o.m_angAbsRotation != 0 && snapshot.localScene != 0) {
            commit(snapshot.localScene + static_cast<std::uintptr_t>(o.m_angAbsRotation),
                   savedAbsRotation_.data(), sizeof(Angle3));
            absRotationSaved_ = false;
        }
    }

    // ---- 5. 子弹追踪内存锁：不看准星、不看 FOV、不看按键，每帧锁射击角 ----
    // v_angle（本地相机角）全程不写，准星与视角纹丝不动；射击结算角被强制
    // 锁到最近敌人头部。同时清掉角速度与历史角数组，防止游戏下一 tick 回滚。
    const bool anglesReady = o.m_angEyeAngles != 0 || o.m_angStashedShootAngles != 0;
    const bool lockHeld =
        m.bulletTrack && anglesReady &&
        (m.bulletTrackKey <= 0 || Features::keyDown(display, m.bulletTrackKey));
    lastTargetDistance_ = -1;
    if (lockHeld) {
        if (const PlayerInfo* target = pickTarget(config, snapshot)) {
            lastTargetDistance_ = static_cast<int>(target->distance);
            Vec3 eye = snapshot.localOrigin;
            eye.z += o.eyeHeight;
            const Vec2 angles = aimAngles(eye, target->head);
            lastLockAngles_ = Angle3{angles.x, angles.y, 0.f};
            lockAnglesValid_ = true;
            lastTargetSeen_ = now;
        }
        // 目标暂时消失（转角 / 遮挡 / 死亡）时继续锁上一帧角度，避免每帧在
        // 「目标角 / 原视角」之间来回跳；超过 holdMs 才停止写入。
        const auto holdWindow =
            std::chrono::milliseconds(std::max(0, m.bulletLockHoldMs));
        const bool holdValid =
            lockAnglesValid_ && (now - lastTargetSeen_) <= holdWindow;
        if (holdValid) {
            // 视角保护：写 eye angles 会联动 v_angle（实机同源）。锁角前先存下
            // 玩家本帧真实相机角，本帧所有射击角写完后再写回它 —— 玩家自己转
            // 视角照常生效，功能不额外带动一帧，视角静止但弹道仍锁在目标上。
            const bool protectView = m.bulletLockProtectView && o.m_angViewAngles != 0;
            const Angle3 viewNow =
                protectView
                    ? game_.read<Angle3>(pawn + static_cast<std::uintptr_t>(o.m_angViewAngles))
                    : Angle3{};
            if (o.m_angEyeAngles != 0 && !eyeSaved_) {
                savedEye_ = game_.read<Angle3>(
                    pawn + static_cast<std::uintptr_t>(o.m_angEyeAngles));
                eyeSaved_ = true;
            }
            if (o.m_angStashedShootAngles != 0 && !shootSaved_) {
                savedShoot_ = game_.read<Angle3>(
                    pawn + static_cast<std::uintptr_t>(o.m_angStashedShootAngles));
                shootSaved_ = true;
            }
            if (o.m_angEyeAngles != 0) {
                commit(pawn + static_cast<std::uintptr_t>(o.m_angEyeAngles),
                       lastLockAngles_.data(), sizeof(Angle3));
            }
            if (o.m_angStashedShootAngles != 0) {
                commit(pawn + static_cast<std::uintptr_t>(o.m_angStashedShootAngles),
                       lastLockAngles_.data(), sizeof(Angle3));
            }
            // 防回滚：游戏用角速度与历史角数组重建 eye angles，清掉它们。
            const Angle3 zero{0.f, 0.f, 0.f};
            if (o.m_angEyeAnglesVelocity != 0) {
                commit(pawn + static_cast<std::uintptr_t>(o.m_angEyeAnglesVelocity),
                       zero.data(), sizeof(Angle3));
            }
            if (o.m_arrOldEyeAngles != 0) {
                commit(pawn + static_cast<std::uintptr_t>(o.m_arrOldEyeAngles),
                       zero.data(), sizeof(Angle3));
            }
            // 视角处理二选一：
            // - 命中优先（bulletLockSyncView）：把 v_angle 也写成锁定角，
            //   下一条 usercmd 的视角与射击角一致，服务器才按锁定方向结算；
            //   代价是相机跟着目标转。
            // - 默认（protectView）：写回玩家本帧相机角，画面不动，
            //   但服务器可能按真实朝向判 miss ——「打中没伤害」的来源。
            if (m.bulletLockSyncView && o.m_angViewAngles != 0) {
                commit(pawn + static_cast<std::uintptr_t>(o.m_angViewAngles),
                       lastLockAngles_.data(), sizeof(Angle3));
            } else if (protectView) {
                commit(pawn + static_cast<std::uintptr_t>(o.m_angViewAngles), viewNow.data(),
                       sizeof(Angle3));
            }
        }
    } else {
        if (m.bulletTrack && !anglesReady) ++missing;
        // 关锁 / Panic：恢复进入锁之前的原值，不留下被改写的射击角。
        if (eyeSaved_ && o.m_angEyeAngles != 0) {
            commit(pawn + static_cast<std::uintptr_t>(o.m_angEyeAngles), savedEye_.data(),
                   sizeof(Angle3));
            eyeSaved_ = false;
        }
        if (shootSaved_ && o.m_angStashedShootAngles != 0) {
            commit(pawn + static_cast<std::uintptr_t>(o.m_angStashedShootAngles),
                   savedShoot_.data(), sizeof(Angle3));
            shootSaved_ = false;
        }
        lockAnglesValid_ = false;
    }

    // ---- 6. 弹着点收成一点 ----
    // 严格分界：只有「纯视模动画」能安全清零。凡是喂给客户端弹道预测的
    // 散布/不准度字段，清掉都会让本地预测以为必中、服务器按真实散布判 miss
    // —— 表现就是「打中却没伤害」。安全模式下这些字段一律不碰。
    if (m.accuracy) {
        const float zeroF = 0.f;
        const Angle3 zero3{0.f, 0.f, 0.f};
        int ready = 0;

        // 安全组：受击抖动 + 枪口抖动时间戳，只驱动视模动画，不进弹道计算。
        if (o.m_flFlinchStack != 0) {
            commit(pawn + static_cast<std::uintptr_t>(o.m_flFlinchStack), &zeroF, sizeof(zeroF));
            ++ready;
        }
        const std::uintptr_t weapon = resolveActiveWeapon(game_, pawn, o);
        if (weapon != 0 && o.m_flLastShakeTime != 0) {
            commit(weapon + static_cast<std::uintptr_t>(o.m_flLastShakeTime), &zeroF,
                   sizeof(zeroF));
            ++ready;
        }

        // 预测组：全部是客户端算散布的输入。关掉安全模式才清，代价是本地
        // 预测与服务器分叉（准星看着收拢，服务器仍按自己的散布结算）。
        if (!m.accuracySafeMode) {
            const int zeroI = 0;
            if (o.m_flInterpolatedInaccuracy != 0) {
                commit(pawn + static_cast<std::uintptr_t>(o.m_flInterpolatedInaccuracy), &zeroF,
                       sizeof(zeroF));
                ++ready;
            }
            if (o.m_pCameraServices != 0 && o.m_vClientScopeInaccuracy != 0) {
                const std::uintptr_t camera = game_.read<std::uintptr_t>(
                    pawn + static_cast<std::uintptr_t>(o.m_pCameraServices), 0);
                if (camera != 0) {
                    commit(camera + static_cast<std::uintptr_t>(o.m_vClientScopeInaccuracy),
                           zero3.data(), sizeof(Angle3));
                    ++ready;
                }
            }
            if (o.m_iShotsFired != 0) {
                commit(pawn + static_cast<std::uintptr_t>(o.m_iShotsFired), &zeroI,
                       sizeof(zeroI));
                ++ready;
            }
            if (weapon != 0) {
                for (const int field : {o.m_fAccuracyPenalty, o.m_fAccuracySmoothedForZoom,
                                        o.m_flTurningInaccuracy,
                                        o.m_flTurningInaccuracyDelta, o.m_flRecoilIndex}) {
                    if (field == 0) continue;
                    commit(weapon + static_cast<std::uintptr_t>(field), &zeroF, sizeof(zeroF));
                    ++ready;
                }
                if (o.m_iRecoilIndex != 0) {
                    commit(weapon + static_cast<std::uintptr_t>(o.m_iRecoilIndex), &zeroI,
                           sizeof(zeroI));
                    ++ready;
                }
            }
        }
        if (ready == 0) ++missing;
    }

    state_.memoryMissing.store(missing);
    if (writes > 0) state_.memoryWrites.fetch_add(writes);
}

void MemoryEngine::restore(const Config& config, const Snapshot& snapshot) {
    const Offsets& o = config.offsets;
    const std::uintptr_t pawn = snapshot.localPawn;
    if (pawn == 0 || !game_.writable()) return;

    if (eyeSaved_ && o.m_angEyeAngles != 0) {
        game_.write(pawn + static_cast<std::uintptr_t>(o.m_angEyeAngles), savedEye_);
        eyeSaved_ = false;
    }
    if (shootSaved_ && o.m_angStashedShootAngles != 0) {
        game_.write(pawn + static_cast<std::uintptr_t>(o.m_angStashedShootAngles), savedShoot_);
        shootSaved_ = false;
    }
    if (moveTypeSaved_ && o.m_MoveType != 0) {
        game_.write(pawn + static_cast<std::uintptr_t>(o.m_MoveType), savedMoveType_);
        moveTypeSaved_ = false;
    }
    if (rotationSaved_ && o.m_angRotation != 0 && snapshot.localScene != 0) {
        game_.write(snapshot.localScene + static_cast<std::uintptr_t>(o.m_angRotation),
                    savedRotation_);
        rotationSaved_ = false;
    }
    if (absRotationSaved_ && o.m_angAbsRotation != 0 && snapshot.localScene != 0) {
        game_.write(snapshot.localScene + static_cast<std::uintptr_t>(o.m_angAbsRotation),
                    savedAbsRotation_);
        absRotationSaved_ = false;
    }
    if (maxspeedSaved_ && o.m_pMovementServices != 0 && o.m_flMaxspeed != 0) {
        const std::uintptr_t movement = game_.read<std::uintptr_t>(
            pawn + static_cast<std::uintptr_t>(o.m_pMovementServices), 0);
        if (movement != 0) {
            game_.write(movement + static_cast<std::uintptr_t>(o.m_flMaxspeed),
                        savedMaxspeed_);
        }
        maxspeedSaved_ = false;
    }
    lastTargetDistance_ = -1;
    lockAnglesValid_ = false;
    spinAngle_ = 0.f;
}

}  // namespace wv
