#pragma once
#include <cmath>

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
    return true;
}

}  // namespace wv
