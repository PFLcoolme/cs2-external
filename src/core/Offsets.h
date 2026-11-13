#pragma once
#include <cstdint>
#include <string>

namespace wv {

// 全部数值与游戏版本强相关，默认值仅作为「结构默认」，必须在 config.json
// 中按当前版本覆盖。为 0 表示未配置。
struct Offsets {
    // 特征码：命中位置假定为 `mov/lea reg, [rip+disp32]`，
    // 解析规则 target = 命中地址 + 7 + disp32。
    // 特征码非空时优先于下面的静态偏移。
    std::string sigEntityList;
    std::string sigLocalPlayerPawn;
    std::string sigViewMatrix;

    // client.dll 相对静态偏移
    std::uintptr_t dwEntityList{0};
    std::uintptr_t dwLocalPlayerPawn{0};
    std::uintptr_t dwViewMatrix{0};

    // 实体列表分块结构（CGameEntitySystem）
    std::uintptr_t entityListChunkBase{0x10};
    std::uintptr_t entityChunkPtrStride{0x8};
    int entityChunkShift{9};              // index >> 9 定位块
    int entityChunkMask{0x1FF};           // index & 0x1FF 定位块内槽位
    // 与 config/default.json 的 112 保持一致：实测 0x78(120) 会扫不到任何健康实体。
    std::uintptr_t entityChunkStride{112};  // 槽位步长（0x70）

    // 网络字段
    int m_iHealth{0};
    int m_ArmorValue{0};  // C_CSPlayerPawn 护甲值；0 = 未配置（不显示护甲）
    int m_iTeamNum{0};
    int m_lifeState{0};
    int m_pGameSceneNode{0};
    int m_vecAbsOrigin{0};
    int m_modelState{0};
    int m_boneArray{0};
    int m_iIDEntIndex{0};
    int m_bSpotted{0};  // 可见性：CS2 中为 1 字节 bool
    // 后坐力角度需要两级跳：C_CSPlayerPawn.m_pAimPunchServices（实测 0x1520）
    // 指向 CCSPlayer_AimPunchServices，其 m_predictableBaseAngle（实测 0x50）是 QAngle。
    // 本版本 schema 里没有 m_aimPunchAngle / m_aimPunchCache 这两个旧字段。
    int m_pAimPunchServices{0};
    int m_aimPunchAngle{0};
    int entityHandleMask{0x7FFF};

    // 骨骼布局：默认按 CTransform（四元数 16 字节 + 坐标 12 字节）取值；
    // 若为 3x4 矩阵布局则改用 boneStride=0x30、bonePosX=0x0C/Y=0x1C/Z=0x2C。
    int boneStride{0x20};
    int bonePosX{0x10};
    int bonePosY{0x14};
    int bonePosZ{0x18};
    int boneHead{6};
    int boneNeck{5};
    int boneChest{4};
    int bonePelvis{0};

    // 直接内存写入使用的网络字段。默认全 0 = 未配置：引擎逐项跳过，
    // 绝不猜测当前版本的 schema 布局。偏移由 tools/cs2_linux_offsets.py
    // 或 cs2-dumper 输出导入，字段名与 OffsetsImporter 的绑定表一致。
    int m_angEyeAngles{0};        // QAngle(3 float)：射击结算角度，写它不动本地相机
    // 实机发现 v_angle 与 m_angEyeAngles 读数几乎一致，单写 eye 可能被每 tick
    // 同步覆盖；m_angStashedShootAngles 是射击结算暂存角，一并写入更稳。
    int m_angStashedShootAngles{0};
    // 锁角时一并清掉：游戏用角速度与历史角数组重建 / 插值 eye angles，
    // 不清会让刚写进去的目标角在下一 tick 被回滚。
    int m_angEyeAnglesVelocity{0};  // C_CSPlayerPawn 0x45a8
    int m_arrOldEyeAngles{0};       // C_CSPlayerPawn 0x4578，只清前 12 字节
    int m_angViewAngles{0};       // C_BasePlayerPawn.v_angle：本地相机角，仅作恢复参考
    // 实机 schema：m_angRotation 在 CGameSceneNode 上（本版本 0xb8），基址是
    // Snapshot::localScene，不是 pawn。写错基址会改到无关内存。
    int m_angRotation{0};         // CGameSceneNode 的 QAngle：模型旋转，视角不变
    int m_vecVelocity{0};         // Vec3：速度，飞天与加速写这里
    int m_flVelocityModifier{0};  // float：速度倍率，加速优先写这个
    int m_MoveType{0};            // uint8：移动类型，飞天可切 noclip
    int m_aimPunchCache{0};       // 后坐力缓存，真无后坐力清零
    int aimPunchCacheBytes{0};    // 缓存字节数，0 = 不清缓存（避免越界覆盖邻字段）
    float eyeHeight{64.f};        // 本地眼睛相对 m_vecAbsOrigin 的高度（Source 单位）

    // 阶段四十二实机取证：只写上面几个「表面」字段不生效，游戏消费的是这些。
    int m_angAbsRotation{0};       // CGameSceneNode 世界旋转（渲染用），scene + 偏移
    int m_pCameraServices{0};      // C_BasePlayerPawn 上的服务对象指针
    int m_pMovementServices{0};    // C_BasePlayerPawn 上的服务对象指针
    int m_flMaxspeed{0};           // CPlayer_MovementServices 内，相对服务对象
    int m_vecCsViewPunchAngle{0};  // CPlayer_CameraServices 内，相对服务对象
    int m_aimPunchVel{0};          // CCSPlayer_AimPunchServices.m_predictableBaseAngleVel
    int m_unpredictableBaseAngle{0};  // CCSPlayer_AimPunchServices 内
    int m_vecAbsVelocity{0};       // C_BaseEntity 绝对速度，飞天与加速的第二落点

    // 阶段四十七实机取证：散布 / 枪口抖动链路。
    int m_pWeaponServices{0};      // C_BasePlayerPawn 服务对象指针
    int m_hActiveWeapon{0};        // CPlayer_WeaponServices 内的当前武器句柄
    int m_iShotsFired{0};          // C_CSPlayerPawn 连发计数，驱动散布增长
    int m_flFlinchStack{0};        // C_CSPlayerPawn 受击抖动
    int m_flInterpolatedInaccuracy{0};  // C_CSPlayerPawn 准星不准度插值（视觉）
    int m_vClientScopeInaccuracy{0};    // CCSPlayer_CameraServices 开镜不准度
    int m_fAccuracyPenalty{0};          // C_CSWeaponBase 精度惩罚
    int m_fAccuracySmoothedForZoom{0};
    int m_flTurningInaccuracy{0};
    int m_flTurningInaccuracyDelta{0};
    int m_iRecoilIndex{0};
    int m_flRecoilIndex{0};
    int m_flLastShakeTime{0};      // C_CSWeaponBase 枪口抖动时间戳
    // CCSWeaponBaseVData.m_flSpread：静态基础散布。偏移已定位（本版本 0x750），
    // 但 VData 指针不在 schema 里，暂不写入；保留字段供后续取证接入。
    int m_flSpreadVData{0};

    bool entityListReady() const {
        return dwEntityList != 0 || !sigEntityList.empty();
    }
    bool localPawnReady() const {
        return dwLocalPlayerPawn != 0 || !sigLocalPlayerPawn.empty();
    }
    bool viewMatrixReady() const {
        return dwViewMatrix != 0 || !sigViewMatrix.empty();
    }
};

}  // namespace wv
