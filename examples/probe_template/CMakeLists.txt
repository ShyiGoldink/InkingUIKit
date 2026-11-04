# ---------------------------------------------------------------------------
# 探针模板（不是"示例"）
#
# 它**故意只有一个源文件**：复制 `main.cpp` 到你的新目录、改五处，就是一个新探针。
# 链接方式与其它示例一致（`inking::inking`），所以 include 路径、SDL3、编译宏
# 都由库 PUBLIC 传播过来——模板里不需要写一行依赖。
#
# 输出到统一的 `bin/`：运行期 DLL 闭包由库那条 POST_BUILD 拷到那里，
# 直接双击或 `ctest` 都不用设 PATH。
# ---------------------------------------------------------------------------

add_executable(inking_probe_template main.cpp)
target_link_libraries(inking_probe_template PRIVATE inking::inking)

set_target_properties(inking_probe_template PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")

# 退出码 = 失败项数 ⇒ 不需要 FAIL_REGULAR_EXPRESSION（改文案会误红）。
# `SKIP_RETURN_CODE 77`：没有可用 GPU 时不把"环境不具备"算成失败。
#
# 真后端那一档不在这里注册：模板换了控件之后，它验的是你的控件，
# 想跑真后端就 `INKING_PROBE_DRIVER=windows` 手动来一次。
if(INK_BUILD_TESTS)
    add_test(NAME probe_template COMMAND $<TARGET_FILE:inking_probe_template>)
    set_tests_properties(probe_template PROPERTIES
        TIMEOUT 60
        SKIP_RETURN_CODE 77)
endif()
