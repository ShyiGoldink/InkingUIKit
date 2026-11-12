# ---------------------------------------------------------------------------
# SBOX-01 · 2.5D 小沙盒（静态骨架）
#
# 形态：跟 examples/demo 一样**挂在根构建里**（由 examples/CMakeLists.txt 的
# add_subdirectory(sandbox) 引入），而不是 examples/minimal 那种独立 SDK 消费工程。
# 理由：本示例要与库同一次 cmake --build build 一起编出来、一起落 build/bin，
# 便于用同一份运行期 DLL 闭包与 ctest 环境做离屏自检。
#
# 依赖一行都不用自己写：`inking::inking` 这个别名把公开头目录、SDL3 / SDL3_image /
# SDL3_ttf / SDL3_mixer 的链接与编译期宏都 PUBLIC 传播过来了。main.cpp 直接
# #include <SDL3/SDL.h> 与 <SDL3_image/SDL_image.h> 就是靠这条传播。
# ---------------------------------------------------------------------------

add_executable(inking_sandbox main.cpp)
target_link_libraries(inking_sandbox PRIVATE inking::inking)

# Windows 下 exe 按"自身所在目录 → PATH"找 DLL。根 CMakeLists 已经把
# CMAKE_RUNTIME_OUTPUT_DIRECTORY 设成 build/bin，这里再显式钉一次三档配置，
# 与 demo 保持一致（Debug / Release / 默认都落同一个目录，和 libinking.dll 同处）。
set_target_properties(inking_sandbox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
