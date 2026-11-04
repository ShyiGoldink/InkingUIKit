# ---------------------------------------------------------------------------
# 控件总览（给人看，不是自检）
#
# 与 `demo/` 并列：那边是最小可运行示例，这边是"把预制件一次摆齐看外观"。
# 不注册 `add_test`——它跑起来是主循环，没有断言，退出码也不代表任何事。
# ---------------------------------------------------------------------------

add_executable(inking_showcase main.cpp)
target_link_libraries(inking_showcase PRIVATE inking::inking)

set_target_properties(inking_showcase PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
