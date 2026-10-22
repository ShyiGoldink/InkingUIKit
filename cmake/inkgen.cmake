# inkgen：CXXCSS 配置 → C++ 代码（构建期工具 + 生成规则的自检）。
# 只依赖标准库，不碰 SDL。

add_library(inkgen_lib STATIC
    tools/inkgen/Json.cpp
    tools/inkgen/Cxxcss.cpp
    tools/inkgen/Emit.cpp)

# 公开 include 目录给自检用：自检要直接调这张库，而不是 fork 一个进程。
target_include_directories(inkgen_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tools/inkgen>)
# 生成器要产出运行期那份数据结构（ink::ButtonData），所以需要 include/ 根——
# 但**不链接 ink_core**：那是"SDF 与 SDL"的世界，构建期工具不该拖进来。
# ButtonData.h 只依赖标准库，所以这里只要一个 include 路径就够。
target_include_directories(inkgen_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(inkgen_lib PUBLIC cxx_std_20)

# 生成器本体。
add_executable(inkgen tools/inkgen/main.cpp)
target_link_libraries(inkgen PRIVATE inkgen_lib)
target_compile_features(inkgen PRIVATE cxx_std_20)

# ---------------------------------------------------------------------------
# 真实配置在 ctest 里过一遍（只校验、不写文件）。
#
# `--validate` 让"配置写错了"这件事在**构建 + 测试**两条路上都会红，而不是等
# 某个示例跑起来才发现。它也是唯一一条能单独跑的配置校验入口。
#
# 目录名决定组件类型（CXXCSS.md §1 第 1 条），所以这里按类型 glob；
# 新增一个组件类型时记得把它的目录也加进来。
#
# 路径用 `${CMAKE_CURRENT_LIST_DIR}/..`（= **本框架**的根），不用
# `CMAKE_SOURCE_DIR`：把本仓库当 third_party 引入时，后者是消费方的目录——
# 实测过一次，那样 glob 会空、配置直接失败。
#
# 整段包在 `INK_BUILD_TESTS` 里：这条本来就是一条测试，消费方工程不该注册它。
# ---------------------------------------------------------------------------
if(INK_BUILD_TESTS)
    file(GLOB INK_CXXCSS_CONFIGS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../CXXCSS/Button/*.json"
        "${CMAKE_CURRENT_LIST_DIR}/../CXXCSS/Panel/*.json"
        "${CMAKE_CURRENT_LIST_DIR}/../CXXCSS/Label/*.json"
        "${CMAKE_CURRENT_LIST_DIR}/../CXXCSS/Image/*.json"
        "${CMAKE_CURRENT_LIST_DIR}/../CXXCSS/Text/*.json"
        "${CMAKE_CURRENT_LIST_DIR}/../CXXCSS/Theme/*.json")
    if(INK_CXXCSS_CONFIGS)
        add_test(NAME inkgen_validate_cxxcss
                 COMMAND inkgen --validate ${INK_CXXCSS_CONFIGS})
    endif()

    # 字段表防漂移（0.2.0 T-08）：`CXXCSS.md` §9 的锚点块必须与
    # `inkgen --dump-fields` 逐字一致——新增字段忘记同步文档，这条测试就红。
    add_test(NAME field_table_in_sync
             COMMAND ${CMAKE_COMMAND}
                     -DINKGEN=$<TARGET_FILE:inkgen>
                     -DDOC=${CMAKE_CURRENT_LIST_DIR}/../CXXCSS/CXXCSS.md
                     -DOUT=${CMAKE_CURRENT_BINARY_DIR}/field_table_dump.md
                     -P ${CMAKE_CURRENT_LIST_DIR}/check-field-table.cmake)
endif()

# 生成器**自己的**自检（目录名判定 / 跨类型重名 / 每条报错 / 产物内容）
# 在 `tests/test_inkgen.cpp`，由顶层 CMakeLists 的 `add_subdirectory(tests)` 挂上。
