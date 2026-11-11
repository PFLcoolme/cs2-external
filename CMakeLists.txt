cmake_minimum_required(VERSION 3.21)
project(cs2_lite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(X11 REQUIRED)

add_executable(cs2_lite
    src/Main.cpp
    src/core/Memory.cpp
    src/core/Config.cpp
    src/core/OffsetsImporter.cpp
    src/game/Game.cpp
    src/render/Overlay.cpp
    src/features/Features.cpp
    src/ui/Menu.cpp
)

target_include_directories(cs2_lite PRIVATE src)

target_link_libraries(cs2_lite PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    X11::X11
    X11::Xext
    X11::Xtst
)

target_compile_options(cs2_lite PRIVATE -Wall -Wextra -Wpedantic)

# 首次构建时把默认配置放到可执行文件旁边
add_custom_command(TARGET cs2_lite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/config/default.json
            $<TARGET_FILE_DIR:cs2_lite>/config.json
)

# 生成器产出的偏移文件跟随可执行文件，使 config.json 里 offsetsFile 的相对路径
# （config/offsets.json）无论从仓库根目录还是 build/ 启动都能解析到。
add_custom_command(TARGET cs2_lite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:cs2_lite>/config
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/config/offsets.json
            $<TARGET_FILE_DIR:cs2_lite>/config/offsets.json
)

# ---- 单元测试：不引入第三方框架，直接 ctest 跑 ----
option(CS2_LITE_BUILD_TESTS "构建单元测试" ON)
if(CS2_LITE_BUILD_TESTS)
    enable_testing()

    add_executable(cs2_tests
        tests/test_main.cpp
        tests/test_math.cpp
        tests/test_config.cpp
        src/core/Config.cpp
        src/core/OffsetsImporter.cpp
    )
    target_include_directories(cs2_tests PRIVATE src)
    target_link_libraries(cs2_tests PRIVATE Qt6::Core)
    target_compile_options(cs2_tests PRIVATE -Wall -Wextra -Wpedantic)

    add_test(NAME cs2_tests COMMAND cs2_tests)
endif()
