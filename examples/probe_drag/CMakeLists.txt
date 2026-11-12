# ---------------------------------------------------------------------------
# 拖拽原语的探针（1.4）
#
# 与 `probe_template` 同一套约定：**一个源文件**、退出码 = 失败项数、
# 真后端建不出窗口时 `return 77`（ctest 显示 Skipped，与"代码坏了"区分开）。
# 链接方式照抄其它示例：include 路径 / SDL3 / 编译宏都由 `inking::inking`
# PUBLIC 传播过来。
# ---------------------------------------------------------------------------

add_executable(inking_probe_drag main.cpp)
target_link_libraries(inking_probe_drag PRIVATE inking::inking)

set_target_properties(inking_probe_drag PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")

if(INK_BUILD_TESTS)
    add_test(NAME probe_drag COMMAND $<TARGET_FILE:inking_probe_drag>)
    set_tests_properties(probe_drag PROPERTIES
        TIMEOUT 60
        SKIP_RETURN_CODE 77)
endif()
