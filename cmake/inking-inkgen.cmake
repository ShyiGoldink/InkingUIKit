# ---------------------------------------------------------------------------
# 面向**预编译 SDK** 的 CXXCSS 生成规则（不依赖源码）
#
# 和仓库里的 `cmake/inkgen.cmake` 的区别：
#   - 那个要先把 `tools/inkgen/*.cpp` 编成 inkgen 本体（需要源码）；
#   - 这个直接调发布包里现成的 `bin/inkgen.exe`（只有二进制）。
#
# 怎么用（必须在 `find_package(inking CONFIG)` 之后）：
#
#   include("${INKING_INKGEN_CMAKE}")
#
#   file(GLOB MY_CSS CONFIGURE_DEPENDS
#       "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/Button/*.json"
#       "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/Label/*.json")
#
#   ink_add_generated(my_app SOURCES ${MY_CSS})
#
# 或者把整个 CXXCSS 根交给它（**推荐**：组件多时命令行只有 1 个参数，
# 工程放在中文路径下也不会撞上 Windows 的 8191 字符上限）：
#
#   ink_add_generated(my_app DIR "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS")
#
# 然后代码里就能：
#
#   #include <inkgen/button_service.h>
#   ...
#   inking::cxxcss::NormalButton btn{this};   // 外观烘在类里，构造不查表
#
# 产物只落构建目录（默认 `<build>/inkgen/`），源码树保持干净。
# ---------------------------------------------------------------------------
function(ink_add_generated target)
    cmake_parse_arguments(GEN "" "OUT_DIR;NAMESPACE;HEADER;DIR" "SOURCES" ${ARGN})

    if(NOT INKING_INKGEN OR NOT EXISTS "${INKING_INKGEN}")
        message(FATAL_ERROR
            "ink_add_generated(${target})：找不到 inkgen.exe。"
            "确认你已经 `find_package(inking CONFIG REQUIRED)`（它会导出 "
            "INKING_INKGEN），并且发布包的 bin/ 目录完整。")
    endif()
    if(GEN_DIR AND GEN_SOURCES)
        message(FATAL_ERROR
            "ink_add_generated(${target})：SOURCES 与 DIR 只能给一个。")
    endif()
    if(NOT GEN_DIR AND NOT GEN_SOURCES)
        message(FATAL_ERROR
            "ink_add_generated(${target})：SOURCES / DIR 至少要给一个。"
            "先 file(GLOB ...) 出 CXXCSS/<类型>/*.json 再传进来，或者把 "
            "CXXCSS 根交给 DIR。")
    endif()
    if(GEN_DIR)
        if(NOT IS_DIRECTORY "${GEN_DIR}")
            message(FATAL_ERROR
                "ink_add_generated(${target})：DIR 不是目录（${GEN_DIR}）。")
        endif()
        # `--dir` 由 inkgen 自己扫，CMake 看不见扫了哪些文件；这里 glob 一份
        # **只用于 DEPENDS**（新增 / 删除 json 时 CONFIGURE_DEPENDS 会重跑配置）。
        file(GLOB_RECURSE _gen_dir_json CONFIGURE_DEPENDS "${GEN_DIR}/*.json")
        set(GEN_SOURCES ${_gen_dir_json})
        set(_gen_input --dir "${GEN_DIR}")
    else()
        set(_gen_input ${GEN_SOURCES})
    endif()

    if(NOT GEN_OUT_DIR)
        # **构建目录根**（不是 inkgen/ 子目录）：因为默认 HEADER 是
        # `inkgen/button_service.h`，代码里写 `#include <inkgen/button_service.h>`。
        # 产物落在 <OUT_DIR>/inkgen/，而 include 路径是 <OUT_DIR>。
        set(GEN_OUT_DIR "${CMAKE_BINARY_DIR}")
    endif()
    if(NOT GEN_NAMESPACE)
        set(GEN_NAMESPACE "inking::cxxcss")
    endif()
    if(NOT GEN_HEADER)
        set(GEN_HEADER "inkgen/button_service.h")
    endif()

    # 产物目录 = include 根 + HEADER 的目录部分；include 根进 include 路径。
    get_filename_component(_gen_subdir "${GEN_HEADER}" DIRECTORY)
    if(_gen_subdir)
        set(_gen_dir "${GEN_OUT_DIR}/${_gen_subdir}")
    else()
        set(_gen_dir "${GEN_OUT_DIR}")
    endif()

    set(_header "${GEN_OUT_DIR}/${GEN_HEADER}")
    get_filename_component(_gen_stem "${GEN_HEADER}" NAME_WE)
    set(_source "${_gen_dir}/${_gen_stem}_register.cpp")

    add_custom_command(
        OUTPUT "${_header}" "${_source}"
        COMMAND "${INKING_INKGEN}"
                --out-dir "${GEN_OUT_DIR}"
                --namespace "${GEN_NAMESPACE}"
                --header "${GEN_HEADER}"
                ${_gen_input}
        DEPENDS "${INKING_INKGEN}" ${GEN_SOURCES}
        COMMENT "inkgen：生成 ${GEN_NAMESPACE} 的配置（${target}）"
        VERBATIM)

    add_custom_target(${target}_inkgen DEPENDS "${_header}" "${_source}")

    add_library(${target}_inkgen_generated OBJECT "${_source}")
    add_dependencies(${target}_inkgen_generated ${target}_inkgen)
    # 生成的代码 include 的是公开头，所以要链接库（顺带继承 include 路径与宏）
    target_link_libraries(${target}_inkgen_generated PRIVATE inking::inking)
    target_include_directories(${target}_inkgen_generated PRIVATE "${GEN_OUT_DIR}")
    set_target_properties(${target}_inkgen_generated PROPERTIES
        POSITION_INDEPENDENT_CODE ON)

    target_sources(${target} PRIVATE $<TARGET_OBJECTS:${target}_inkgen_generated>)
    # 产物目录进 include 路径：消费方写 #include <inkgen/button_service.h>
    target_include_directories(${target} PRIVATE "${GEN_OUT_DIR}")

    # 把产物路径往外传一层，省得调用方再抄一遍（抄错就是两个不同的产物）。
    set(${target}_GENERATED_HEADER "${GEN_HEADER}" PARENT_SCOPE)
    set(${target}_GENERATED_INCLUDE_DIR "${GEN_OUT_DIR}" PARENT_SCOPE)
endfunction()
