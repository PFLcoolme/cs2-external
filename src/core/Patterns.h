#pragma once

namespace wv::patterns {

// 特征码随代码进版本库（对应 Osiris 的 Source/MemoryPatterns 思路）：
// - 命中位置必须是 `mov/lea reg, [rip+disp32]` 形式
// - 解析规则：目标地址 = 命中地址 + 7 + disp32
// - 空字符串表示该版本尚未提供签名，此时依赖 config.json 的静态偏移
//
// 每次游戏更新后只需替换这里的字符串，配置与代码不需要重新组织。
inline constexpr const char* kEntityList = "";
inline constexpr const char* kLocalPlayerPawn = "";
inline constexpr const char* kViewMatrix = "";

}  // namespace wv::patterns
