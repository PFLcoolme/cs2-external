#pragma once
#include <cstdint>
#include <string>

#include "Offsets.h"

namespace wv {

struct ImportResult {
    bool loaded{false};
    int applied{0};
    std::string message;
};

// 导入 cs2-dumper（a2x/cs2-dumper）生成的 offsets.json。
//
// 兼容两种形态：
//   1. 平铺：{"client.dll": {"dwEntityList": 0x1A1B4C0, ...}}
//   2. 含 schema：{"schemas": {"client.dll": {"C_CSPlayerPawn": {"m_iHealth": {"value": 848}}}}}
//
// 解析策略：递归收集所有命名字段，按所属类优先级择优
// （C_CSPlayerPawn > C_BaseEntity / CGameSceneNode / CSkeletonInstance > 其他），
// 平铺静态偏移作为最低优先级兜底。
ImportResult importOffsetsFile(const std::string& path, Offsets& offsets);

}  // namespace wv
