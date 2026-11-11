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
