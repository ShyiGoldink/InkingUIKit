# ---------------------------------------------------------------------------
# 图集帧动画测试：透明无边框窗口 + 一张图集（6×5 = 30 帧，15fps）
#
# 素材**不进仓库**（atlas.png 6.6MB，属于美术资产）：运行期从 exe 同级读，
# 或把路径作为第一个参数传进来。
# ---------------------------------------------------------------------------

# `app.rc` 把 `app.manifest` 编进资源段：没有它 Windows 按"DPI 不感知"
# 处理进程，125% 缩放的屏幕上会把 512 的窗口拉成 410 个物理像素（图先被系统
# 缩一遍，看起来偏小偏糊）。图集测试要看像素，所以必须声明 per-monitor v2。
add_executable(inking_atlas_frames main.cpp app.rc)
target_link_libraries(inking_atlas_frames PRIVATE inking::inking)

set_target_properties(inking_atlas_frames PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
