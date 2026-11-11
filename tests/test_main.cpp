#include "test_util.h"

#include <cstdio>

void runMathTests();
void runConfigTests();

int main() {
    runMathTests();
    runConfigTests();

    if (test::failures() == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", test::failures());
    return 1;
}
