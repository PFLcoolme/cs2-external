#include "core/Math.h"
#include "test_util.h"

#include <cmath>

namespace {

wv::Mat4 identityView() {
    wv::Mat4 view{};
    for (int i = 0; i < 4; ++i) view.m[i][i] = 1.f;
    return view;
}

void testWorldToScreen() {
    const wv::Mat4 view = identityView();
    wv::Vec2 screen{};

    // 原点正前方：w = 1、NDC = 0，落在屏幕中心。
    EXPECT(wv::worldToScreen({0.f, 0.f, 0.f}, view, {0.f, 0.f}, 200, 100, screen));
    EXPECT_NEAR(screen.x, 100.f, 0.01f);
    EXPECT_NEAR(screen.y, 50.f, 0.01f);

    // 世界 +X → 屏幕最右侧；世界 +Y → 屏幕最上方（NDC 的 +y 向上）。
    EXPECT(wv::worldToScreen({1.f, 0.f, 0.f}, view, {0.f, 0.f}, 200, 100, screen));
    EXPECT_NEAR(screen.x, 200.f, 0.01f);
    EXPECT_NEAR(screen.y, 50.f, 0.01f);

    EXPECT(wv::worldToScreen({0.f, 1.f, 0.f}, view, {0.f, 0.f}, 200, 100, screen));
    EXPECT_NEAR(screen.x, 100.f, 0.01f);
    EXPECT_NEAR(screen.y, 0.f, 0.01f);

    // 客户区原点偏移应整体平移结果。
    EXPECT(wv::worldToScreen({0.f, 0.f, 0.f}, view, {10.f, 20.f}, 200, 100, screen));
    EXPECT_NEAR(screen.x, 110.f, 0.01f);
    EXPECT_NEAR(screen.y, 70.f, 0.01f);

    // w <= 0 表示目标在相机背后，必须拒绝。
    wv::Mat4 behind = identityView();
    behind.m[3][3] = -1.f;
    EXPECT(!wv::worldToScreen({0.f, 0.f, 0.f}, behind, {0.f, 0.f}, 200, 100, screen));
}

void testViewYaw() {
    // 相机朝 +X：第三行 = (-1, 0, 0)，forward = (1, 0, 0)，yaw = 0。
    wv::Mat4 view = identityView();
    view.m[2][0] = -1.f;
    view.m[2][2] = 0.f;
    EXPECT_NEAR(wv::viewYaw(view), 0.f, 0.001f);

    // 相机朝 +Y：第三行 = (0, -1, 0)，forward = (0, 1, 0)，yaw = pi/2。
    view = identityView();
    view.m[2][1] = -1.f;
    view.m[2][2] = 0.f;
    EXPECT_NEAR(wv::viewYaw(view), 1.5707963f, 0.001f);

    // 第三行长度为 0：矩阵布局不符，必须返回 NaN 让调用方回退固定北向。
    view = identityView();
    view.m[2][0] = 0.f;
    view.m[2][1] = 0.f;
    view.m[2][2] = 0.f;
    EXPECT(std::isnan(wv::viewYaw(view)));
}

void testScreenDirection() {
    const wv::Mat4 view = identityView();
    float dx = 0.f;
    float dy = 0.f;

    // 屏幕右侧目标：方向应为 +X。
    EXPECT(wv::screenDirection({1.f, 0.f, 0.f}, view, dx, dy));
    EXPECT_NEAR(dx, 1.f, 0.001f);
    EXPECT_NEAR(dy, 0.f, 0.001f);

    // 屏幕上方目标：NDC 的 +y 向上，屏幕 +y 向下，方向应为 -Y。
    EXPECT(wv::screenDirection({0.f, 1.f, 0.f}, view, dx, dy));
    EXPECT_NEAR(dx, 0.f, 0.001f);
    EXPECT_NEAR(dy, -1.f, 0.001f);

    // 屏幕左下目标：方向应为 (-x, +y)，且归一化到单位长度。
    EXPECT(wv::screenDirection({-1.f, -1.f, 0.f}, view, dx, dy));
    EXPECT_NEAR(dx, -0.7071068f, 0.001f);
    EXPECT_NEAR(dy, 0.7071068f, 0.001f);

    // w 退化到 0：方向无定义，必须拒绝。
    wv::Mat4 flat = identityView();
    flat.m[3][3] = 0.f;
    EXPECT(!wv::screenDirection({0.f, 0.f, 0.f}, flat, dx, dy));
}

}  // namespace

void runMathTests() {
    testWorldToScreen();
    testViewYaw();
    testScreenDirection();
}
