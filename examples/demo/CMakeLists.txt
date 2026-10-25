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
foreach(target inking_demo inking_demo_selftest)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:${target}>/assets"
        COMMENT "拷贝示例资源（assets/）到 $<TARGET_FILE_DIR:${target}>")
endforeach()
