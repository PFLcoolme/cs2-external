#pragma once
#include <cmath>
#include <cstdio>

namespace test {

inline int& failures() {
    static int count = 0;
    return count;
}

inline void expect(bool condition, const char* expression, const char* file, int line) {
    if (condition) return;
    std::fprintf(stderr, "FAIL %s:%d  %s\n", file, line, expression);
    ++failures();
}

inline void expectNear(float actual, float expected, float tolerance, const char* expression,
                       const char* file, int line) {
    if (std::fabs(actual - expected) <= tolerance) return;
    std::fprintf(stderr, "FAIL %s:%d  %s  (got %.6f, want %.6f +/- %.6f)\n", file, line, expression,
                 static_cast<double>(actual), static_cast<double>(expected),
                 static_cast<double>(tolerance));
    ++failures();
}

}  // namespace test

#define EXPECT(expr) ::test::expect((expr), #expr, __FILE__, __LINE__)
#define EXPECT_NEAR(actual, expected, tolerance) \
    ::test::expectNear((actual), (expected), (tolerance), #actual, __FILE__, __LINE__)
