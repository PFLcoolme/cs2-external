#pragma once
#include <cmath>
#include <limits>

namespace wv {

struct Vec2 {
    float x{0.f};
    float y{0.f};
};

struct Vec3 {
    float x{0.f};
    float y{0.f};
    float z{0.f};

    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const { return std::sqrt(dot(*this)); }
};

// Source 2 的视图矩阵以 4x4 float 行主序存放，worldToScreen 采用
// 行向量右乘形式（与社区通用实现一致）：
//   clip = world(1x4) * view(4x4)
struct Mat4 {
    float m[4][4]{};
};

// origin 为游戏客户区左上角在屏幕上的坐标，width/height 为客户区尺寸。
inline bool worldToScreen(const Vec3& world, const Mat4& view, const Vec2& origin,
                          int width, int height, Vec2& out) {
    const float w = world.x * view.m[3][0] + world.y * view.m[3][1] +
                    world.z * view.m[3][2] + view.m[3][3];
    if (w < 0.001f) return false;  // 目标在相机背后

    const float x = world.x * view.m[0][0] + world.y * view.m[0][1] +
                    world.z * view.m[0][2] + view.m[0][3];
    const float y = world.x * view.m[1][0] + world.y * view.m[1][1] +
                    world.z * view.m[1][2] + view.m[1][3];

    out.x = origin.x + (static_cast<float>(width) * 0.5f) * (1.f + x / w);
    out.y = origin.y + (static_cast<float>(height) * 0.5f) * (1.f - y / w);
    // w 接近 0 时 x/w 会溢出成 NaN/inf，防御性拒绝，别把垃圾坐标带进快照。
    if (!std::isfinite(out.x) || !std::isfinite(out.y)) return false;
    return true;
}

// 相对屏幕中心的像素偏移。实机日志出现过 headScreen 投影退化到 (1938,-4260)，
// XTest 按它移动会把准星甩飞再归位，表现为视角剧烈抖动、打不中。
// 只接受落在屏幕矩形内的目标，非有限值直接拒绝。
inline bool screenDelta(const Vec2& head, int width, int height, Vec2& delta) {
    const float halfW = static_cast<float>(width) * 0.5f;
    const float halfH = static_cast<float>(height) * 0.5f;
    delta.x = head.x - halfW;
    delta.y = head.y - halfH;
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) return false;
    return std::fabs(delta.x) <= halfW && std::fabs(delta.y) <= halfH;
}

// 从视图矩阵左上 3x3 推导相机朝向角（世界系 yaw，弧度）。
// 行主序 + 行向量右乘下，相机 forward 等于第三行取反（相机看向 -Z）。
// 旋转行长度应接近 1；不接近说明矩阵布局假设不成立，返回 NaN 让调用方回退。
inline float viewYaw(const Mat4& view) {
    const float r0 = view.m[2][0];
    const float r1 = view.m[2][1];
    const float r2 = view.m[2][2];
    const float rowLength = std::sqrt(r0 * r0 + r1 * r1 + r2 * r2);
    if (!std::isfinite(rowLength) || std::fabs(rowLength - 1.f) > 0.05f) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const float fx = -r0;
    const float fy = -r1;
    if (std::fabs(fx) < 1e-6f && std::fabs(fy) < 1e-6f) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return std::atan2(fy, fx);
}

// from → to 的 Source 角度（度）：x = pitch，y = yaw，与 Snapshot::aimPunch 同序。
// Source 约定：yaw = atan2(dy, dx)；向上看为负 pitch，故对 dz 取负。
// 水平距离退化（正上/正下方）时保留 yaw = 0，不产生 NaN。
inline Vec2 aimAngles(const Vec3& from, const Vec3& to) {
    constexpr float kRadToDeg = 57.29577951308232f;
    const Vec3 d = to - from;
    const float horizontal = std::sqrt(d.x * d.x + d.y * d.y);
    Vec2 angles{};
    angles.y = std::atan2(d.y, d.x) * kRadToDeg;
    angles.x = -std::atan2(d.z, horizontal) * kRadToDeg;
    return angles;
}

// 目标相对屏幕中心的方向（单位向量），用于屏幕外指示。
// w < 0 表示目标在相机背后，此时 NDC 会翻转，取反才能得到真实方位。
inline bool screenDirection(const Vec3& world, const Mat4& view, float& dx, float& dy) {
    const float w = world.x * view.m[3][0] + world.y * view.m[3][1] +
                    world.z * view.m[3][2] + view.m[3][3];
    if (!std::isfinite(w) || std::fabs(w) < 1e-4f) return false;

    const float x = world.x * view.m[0][0] + world.y * view.m[0][1] +
                    world.z * view.m[0][2] + view.m[0][3];
    const float y = world.x * view.m[1][0] + world.y * view.m[1][1] +
                    world.z * view.m[1][2] + view.m[1][3];

    dx = x / w;
    dy = -y / w;  // NDC 的 +y 向上，屏幕 +y 向下
    if (w < 0.f) {
        dx = -dx;
        dy = -dy;
    }
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-5f) return false;
    dx /= length;
    dy /= length;
    return true;
}

}  // namespace wv
