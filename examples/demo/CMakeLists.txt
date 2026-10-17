# ---------------------------------------------------------------------------
# 示例工程：两个目标
#
#   inking_demo           真窗口 + 主循环，给人看
#   inking_demo_selftest  无头自检（dummy 视频驱动），给机器看；退出码 = 失败项数
#
# 这里**故意不直接写 -l… 与 include 路径**，只用 `inking::inking` 这个别名目标
# ——公开头目录、SDL3 / FFmpeg 的链接、编译期开关（INK_ISLOG 等）都由库那边
# PUBLIC 传播过来。示例工程本身一行依赖都不用知道，这正是"打包成链接库"要
# 交付的东西。
# ---------------------------------------------------------------------------

add_executable(inking_demo main.cpp)
target_link_libraries(inking_demo PRIVATE inking::inking)

add_executable(inking_demo_selftest selftest.cpp)
target_link_libraries(inking_demo_selftest PRIVATE inking::inking)

# 运行时把 exe 都放到同一个目录，免得每次拷 dll（Windows 下）。
foreach(target inking_demo inking_demo_selftest)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
endforeach()
