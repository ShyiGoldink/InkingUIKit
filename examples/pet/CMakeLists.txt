# ---------------------------------------------------------------------------
# 桌宠示例：无边框 + 透明 + 置顶 + 序列帧动画
#
# 素材**不放进仓库**（73 张 321×900 的 PNG ≈ 18MB，且属于美术资产）：
# 运行时把帧目录作为第一个参数传进来，不传就用 `main.cpp` 里的 kDefaultFrameDir。
# ---------------------------------------------------------------------------

add_executable(inking_pet main.cpp)
target_link_libraries(inking_pet PRIVATE inking::inking)

set_target_properties(inking_pet PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
