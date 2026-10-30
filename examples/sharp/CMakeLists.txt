# ---------------------------------------------------------------------------
# 清晰度验收示例（0.7.0 T-05）：720p 设计尺寸 + 一张真实素材 + 全屏逐档看
#
# 与 `examples/pet` 不同，这里**把素材放进仓库**（`assets/nailong-art-01.png`，
# 1536×2752，5.97 MB）：验收的判据是"这张素材在该屏幕上是什么样"，
# 换一张就换了一个问题——所以素材本身就是验收用例的一部分。
#
# 为什么独立成示例而不是塞进 `inking_demo`：demo 的设计尺寸由 CXXCSS 配置烘死，
# 改它要动生成器；而这条验收的全部意义就在"设计 1280×720"这个前提上。
# ---------------------------------------------------------------------------

add_executable(inking_sharp main.cpp)
target_link_libraries(inking_sharp PRIVATE inking::inking)

set_target_properties(inking_sharp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")

# 素材拷到 exe 旁（示例默认从 `assets/` 读；也支持命令行传路径）。
add_custom_command(TARGET inking_sharp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:inking_sharp>/assets"
    COMMENT "拷贝验收素材（assets/）到 $<TARGET_FILE_DIR:inking_sharp>")
