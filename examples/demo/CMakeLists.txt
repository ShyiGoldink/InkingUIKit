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

# 设计稿背景（0.5.1）：`assets/design.svg` 由 `InkingStaticImage` 在**运行期**
# 从资源根读（默认 exe 旁的 `assets/`）。拷到 exe 旁边，示例才能直接跑。
# 自检不依赖这个文件（它用内联 SVG），但保持一致：缺文件时只该看到一条 ERROR，
# 而不是"背景图没支持"。
#
# 视频样片 `probe.mp4`（0.9.0 T-01）也走这一份：自检打开它、断言解出的帧数与
# 画面尺寸。它由 `ffmpeg -f lavfi -i testsrc=size=320x240:rate=30 -t 1` 生成，
# 命令写在 `docs/zh/api/` 与任务书里（**数字写死**，断言才有基准）。
#
# **只拷一次**：两个 exe 的输出目录是同一个（上面那三条），早先是每个 target
# 各挂一条 POST_BUILD，于是并行链接时两个 `copy_directory` 同时写
# `bin/assets/`——Windows 上偶发 `Error copying directory ... Permission denied`
# （目录正被另一个进程占着），`cmake --build` 就这么随机失败。改成"一个 target
# 拷一次、两个 exe 都依赖它"。
add_custom_target(inking_demo_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "${CMAKE_BINARY_DIR}/bin/assets"
    COMMENT "拷贝示例资源（assets/）到 bin/assets")
add_dependencies(inking_demo inking_demo_assets)
add_dependencies(inking_demo_selftest inking_demo_assets)
