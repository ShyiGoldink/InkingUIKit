cmake_minimum_required(VERSION 3.20)

project(Inking
    VERSION 0.5.0
    DESCRIPTION "Inking 前端（分层重构版）—— 动态链接库"
    LANGUAGES CXX)

# enable_testing() 必须在**任何 add_subdirectory 之前**调：它只对"调用之后
# 处理到的目录"生效，晚一步就会让子目录里的 add_test 静默失效——测试照样
# 编出来、手动跑也过，就是 ctest 里看不见（踩过：示例里的集成自检一直不在
# ctest 列表里，docs/AGENTS.md §6 第 29 条）。
enable_testing()

# ---------------------------------------------------------------------------
# 产物目录：exe 与 DLL 统一落 build/bin，静态库落 build/lib
#
# Windows 下 exe 按"自身所在目录 → PATH"找依赖 DLL。早先 libinking.dll 落在
# build 根、而 exe 落在 build/bin，示例一启动就因找不到 libinking.dll 静默退出
# （无输出、读不到退出码），看着像"程序根本没跑"。同目录即可，不必每次拷 dll。
# 源码树保持干净（docs/AGENTS.md §3 第 9 条）。
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------------------------
# 语言与默认构建类型
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

# ---------------------------------------------------------------------------
# 选项
#
# ISMESSAGE 与源项目同名同义（CMake 选项 → 宏 INK_ISMESSAGE）：关闭时消息队列
# 退化成空实现、其实现 TU 变成空翻译单元（见 include/detail/MessageQueue.h）。
#
# INK_BUILD_TESTS 控制 ctest 里注册什么（目前是 inkgen 的配置校验 +
# 生成链路的编译自检）；被别的工程当 third_party 引入时不该注册本仓库的测试。
# ---------------------------------------------------------------------------
option(ISMESSAGE "消息队列参与编译（定义 INK_ISMESSAGE）" ON)
option(INK_BUILD_EXAMPLES "构建 examples/ 下的示例工程" ${PROJECT_IS_TOP_LEVEL})
option(INK_BUILD_TESTS "注册自检（ctest）" ${PROJECT_IS_TOP_LEVEL})

# ---------------------------------------------------------------------------
# 目标：动态链接库
#
# 终局形态是 DLL（2026-10-15 拍板）。源项目是 STATIC 库、**零导出宏**，
# 所以这里对四件事逐条交代，免得后来者以为漏了：
#
#   1. **导出方式**：用 `WINDOWS_EXPORT_ALL_SYMBOLS`（CMake 生成 .def，
#      MinGW 走 dlltool、MSVC 走 dumpbin），**不逐类标注 `__declspec(dllexport)`**。
#      理由：本项目 73 个公开头、几十个类，手工标注漏一个不会编译报错——
#      只在链接期（少标）或运行期（把"纯头 inline 值类型"错标成 dllimport）
#      暴露；而 .def 方案一次覆盖全部有定义的符号。
#      代价：导出表比手工精选大（连 detail:: 的实现一起导出），当前可接受。
#
#   2. **不加 `-fvisibility=hidden`（`CXX_VISIBILITY_PRESET hidden`）**。
#      那是"显式导出宏"方案的配套：在 MinGW 上它把符号标成 local，
#      反而可能让 .def 里一个符号都收不到——两套机制混用是自相矛盾的。
#      选了一整套（.def 全导出），就把另一套去掉。
#
#   3. **模板与纯头值类型不进导出表**（`InkingEvent<Args...>` / `Task<T>` /
#      `Affine` / `TransformSpec` / `ShapeSpec` / `ButtonLook` …）：它们没有
#      .cpp、全是头内 inline，.def 里压根没有它们的符号，于是每个翻译单元
#      各自内联展开——这正是想要的（不引入 dllimport 的链接期陷阱）。
#
#   4. **"整个进程只有一份"的槽位已提前搬进 .cpp**（头内 inline 函数的函数内
#      静态量在动态库下会 exe / DLL 各一份，而且**不报错**）：
#        - `include/render/InkingDraw.h` 的 currentRendererSlot /
#          currentAncestorAffineSlot  → src/render/InkingDraw.cpp
#        - `include/thread/TaskQueue.h` 的 TaskQueue::instance
#                                     → src/thread/TaskQueue.cpp
#      （`InkingWindow::Instance()` 本来就是 .cpp 定义，不受影响。）
# ---------------------------------------------------------------------------

file(GLOB_RECURSE INK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(inking SHARED ${INK_SOURCES})
add_library(inking::inking ALIAS inking)

target_include_directories(inking
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

# 编译期开关。PUBLIC：这些宏会影响公开头里 inline 函数的**内容**
# （例如 MessageQueue 的空实现与真实现是两份不同的类定义），消费者必须
# 与库看到同一个取值，否则就是 ODR 违例。
#
# `INK_ISDEBUG` 从 0.1.1 起改成**默认关**的选项（`INKING_DEBUG_LOG`）。
# 它长期被无条件写成 PUBLIC，于是发布包的消费方**关不掉**调试流：实测 948 条
# 日志里有一条重复 528 次，而那一条本来就是 DEBUG 级——噪声的根子是"这个开关
# 永远为真"，不是哪一句日志写错了级别。
#
# 为什么仍然是 PUBLIC 而不是 PRIVATE：`include/detail/InkLog.h` 里的
# `kDefaultLogLevel` / `kDebugEnabled` 是 `inline constexpr`，库与调用方取值
# 不同就是同一个头两套定义（ODR 违例，编译器不报错、行为错乱）——正是下面
# 那段"一旦公开头出现这种分支，就必须改成 PUBLIC"说的情形。所以正确做法是
# **让它默认 OFF、需要的人显式打开**，而不是把它藏进库自己的编译选项里：
# 藏起来的话库打出调试流、调用方的宏却是空的，两边看到的不是同一个日志世界。
option(INKING_DEBUG_LOG "调试级日志与调试断言参与编译（定义 INK_ISDEBUG）" OFF)

target_compile_definitions(inking PUBLIC INK_ISLOG=1)
if(INKING_DEBUG_LOG)
    target_compile_definitions(inking PUBLIC INK_ISDEBUG=1)
endif()
if(ISMESSAGE)
    target_compile_definitions(inking PUBLIC INK_ISMESSAGE=1)
endif()

set_target_properties(inking PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    OUTPUT_NAME "inking")

# ---------------------------------------------------------------------------
# 依赖
#
#   SDL3 / SDL3_image / SDL3_ttf / SDL3_mixer  → msys64 带了 CMake config
#   FFmpeg 五库                                → 只有 pkg-config（libavcodec.pc …）
#
# 全部 PUBLIC：公开头虽然**只前向声明** SDL_Renderer（不拖 SDL 进来），但
# `render/InkingBatch.h` 等确实用了 `SDL_Vertex` 完整类型，消费者需要同一套
# 头与库版本 —— 所以传播给它。
# ---------------------------------------------------------------------------
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)
find_package(SDL3_mixer CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec libavformat libavutil libswresample libswscale)

target_link_libraries(inking
    PUBLIC
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        SDL3_mixer::SDL3_mixer
        PkgConfig::FFMPEG)

# ---------------------------------------------------------------------------
# 可选能力的编译开关（文字 / 图片）
#
# `src/render/InkingText.cpp` 用 `#if defined(INK_HAS_SDL3_TTF)` /
# `INK_HAS_SDL3_IMAGE` 分支：没定义时那一层**静默退化**成"占位方块 / 图片
# 解不出来"——不报错、不警告，界面上看起来"有字"，只是每段文字都是方块。
#
# 四个包在上面都是 REQUIRED 且已经链接上了，所以能力就该打开。之前这里漏了
# 定义，于是文字高清（按设备比例烘字）这条路上一直没有真的栅格化过——
# 高清无从谈起，任何关于"文字清晰度"的验收也都会是假通过。
# ---------------------------------------------------------------------------
# 注意：这三个能力宏是 **PRIVATE**，因为 `include/` 下的公开头**没有**
# 用 `#if defined(INK_HAS_SDL3_TTF)` 这类做分支（只有 `src/render/*` 与
# `src/audio/*` 的内部实现用）。消费者看不到它们，也不会与库产生两套类定义，
# 所以没有 ODR 风险。
#
# **一旦公开头出现这种分支，就必须改成 PUBLIC**（和 INK_ISLOG / INK_ISDEBUG
# 同一个理由）：库与调用方看到不同的宏 = 同一个头里的 inline 代码一边真实现、
# 一边空实现，编译器不报错、行为错乱。
#
# `INK_HAS_SDL3_MIXER` 曾经**漏在这里**：`src/audio/InkingAudio.cpp` 整份被
# `#if defined(INK_HAS_SDL3_MIXER)` 包着，宏没有 ⇒ 编进去的是 `#else` 那个空
# 实现分支，于是 `AudioInit()` 恒 false、BGM / SE / 语音全部静默无声，而
# CMake 仍然 find_package + 链接 SDL3_mixer —— "能编能链"把这件事完全盖住了。
# 判据只能看二进制：`objdump -p libinking.dll` 的依赖里得有 `SDL3_mixer.dll`。
target_compile_definitions(inking PRIVATE
    INK_HAS_SDL3_TTF
    INK_HAS_SDL3_IMAGE
    INK_HAS_SDL3_MIXER)

# ---------------------------------------------------------------------------
# inkgen：CXXCSS → C++ 的构建期生成器
#
# 生成器**只依赖标准库**（连 SDL 都不碰），所以它能在任何环境下先编出来，
# 再拿它去生成组件代码。`cmake/inkgen.cmake` 负责：
#   1. 建 `inkgen_lib`（Json / Cxxcss / Emit 三份实现）与 `inkgen` 可执行；
#   2. 在 INK_BUILD_TESTS 下把 CXXCSS/ 里的全部真实配置过一遍
#      `inkgen --validate`——"配置写错了"于是会在构建与 ctest 两条路上都红。
# ---------------------------------------------------------------------------
include(cmake/inkgen.cmake)

# ---------------------------------------------------------------------------
# 把 CXXCSS 配置生成成 C++，并挂到一个目标上。
#
#   ink_add_generated(<目标>
#       OUT_DIR   <include 根，约定在 build/ 下>
#       (SOURCES  <json 文件列表> | DIR <CXXCSS 根>)
#       [NAMESPACE inking::cxxcss] [HEADER inkgen/button_service.h])
#
# SOURCES 与 DIR **二选一**：DIR 让 inkgen 自己扫目录（`--dir`），组件多的时候
# 命令行只有 1 个参数——工程放在中文路径下时，CMake 不会再因为命令行太长把它
# 包进 .bat（那个 .bat 走系统代码页，非 ASCII 路径必然废）。
#
# HEADER 可以带一层子目录（默认 `inkgen/button_service.h`）：产物写成
# `#include <inkgen/button_service.h>`，和其他头文件放在一起也不会撞名。
# OUT_DIR 是**加进 include 路径的那个根**，产物落在 OUT_DIR/HEADER 的目录里。
#
# 做完三件事：
#   1. 建自定义命令，配置变了就重新生成（依赖挂在 inkgen 可执行上，
#      所以第一次构建会先把生成器编出来）；
#   2. 把生成出来的 .cpp 编进一个 OBJECT 库；
#   3. 把它的对象文件与 include 目录加到 <目标> 上——调用方只要
#      `#include <inkgen/button_service.h>` 就能用，不需要手写任何构建规则。
#
# 产物只落 build 目录，源码树保持干净（docs/AGENTS.md §3 第 9 条）。
# ---------------------------------------------------------------------------
function(ink_add_generated target)
    cmake_parse_arguments(GEN "" "OUT_DIR;NAMESPACE;HEADER;DIR" "SOURCES" ${ARGN})

    if(NOT GEN_OUT_DIR)
        message(FATAL_ERROR "ink_add_generated(${target})：缺少 OUT_DIR")
    endif()
    if(GEN_DIR AND GEN_SOURCES)
        message(FATAL_ERROR
            "ink_add_generated(${target})：SOURCES 与 DIR 只能给一个。")
    endif()
    if(NOT GEN_DIR AND NOT GEN_SOURCES)
        message(FATAL_ERROR
            "ink_add_generated(${target})：SOURCES / DIR 至少要给一个"
            "（先 glob 出 CXXCSS/Button/*.json，或者把 CXXCSS 根交给 DIR）。")
    endif()
    if(GEN_DIR)
        if(NOT IS_DIRECTORY "${GEN_DIR}")
            message(FATAL_ERROR
                "ink_add_generated(${target})：DIR 不是目录（${GEN_DIR}）。")
        endif()
        # 依赖跟踪：`--dir` 让 inkgen 自己扫，CMake 看不见它扫了哪些文件，
        # 所以这里自己 glob 一份**只用于 DEPENDS**（CONFIGURE_DEPENDS 保证
        # 新增 / 删除 json 时自动重跑 configure）。
        file(GLOB_RECURSE _gen_dir_json CONFIGURE_DEPENDS "${GEN_DIR}/*.json")
        set(GEN_SOURCES ${_gen_dir_json})
        set(_gen_input --dir "${GEN_DIR}")
    else()
        set(_gen_input ${GEN_SOURCES})
    endif()
    if(NOT GEN_NAMESPACE)
        set(GEN_NAMESPACE "inking::cxxcss")
    endif()
    if(NOT GEN_HEADER)
        set(GEN_HEADER "inkgen/button_service.h")
    endif()

    # 产物目录 = include 根 + HEADER 里的子目录部分；include 根进 include 路径。
    get_filename_component(_gen_subdir "${GEN_HEADER}" DIRECTORY)
    if(_gen_subdir)
        set(_gen_dir "${GEN_OUT_DIR}/${_gen_subdir}")
    else()
        set(_gen_dir "${GEN_OUT_DIR}")
    endif()

    set(_header "${GEN_OUT_DIR}/${GEN_HEADER}")
    # 产物源文件名跟着头文件主体走（和 tools/inkgen/Emit.cpp 里那条一致）：
    # `button_service.h` → `button_service_register.cpp`。固定叫 button_register.cpp
    # 的话，产物里混了面板之后名不副实，同一个目录放两套生成结果也会撞名。
    get_filename_component(_gen_stem "${GEN_HEADER}" NAME_WE)
    set(_source "${_gen_dir}/${_gen_stem}_register.cpp")

    add_custom_command(
        OUTPUT "${_header}" "${_source}"
        COMMAND $<TARGET_FILE:inkgen>
                --out-dir "${GEN_OUT_DIR}"
                --namespace "${GEN_NAMESPACE}"
                --header "${GEN_HEADER}"
                ${_gen_input}
        DEPENDS inkgen ${GEN_SOURCES}
        COMMENT "inkgen：生成 ${GEN_NAMESPACE} 的配置（${target}）"
        VERBATIM)

    # 独立的生成目标：构建它（或被别的目标依赖）就会触发上面那条命令。
    add_custom_target(${target}_generate DEPENDS "${_header}" "${_source}")

    add_library(${target}_generated OBJECT "${_source}")
    add_dependencies(${target}_generated ${target}_generate)
    # 生成代码 include 的是公开头，所以要知道 include/ 在哪、宏开了哪些。
    target_link_libraries(${target}_generated PRIVATE inking::inking)
    # 生成的头文件自己要能找到（`#include <inkgen/all/button_service.h>`
    # 里的那一段，是相对 include 根的）。
    target_include_directories(${target}_generated PRIVATE "${GEN_OUT_DIR}")
    set_target_properties(${target}_generated PROPERTIES
        POSITION_INDEPENDENT_CODE ON)

    target_sources(${target} PRIVATE $<TARGET_OBJECTS:${target}_generated>)
    # 产物目录进 include 路径：调用方写 `#include <inkgen/button_service.h>`。
    target_include_directories(${target} PRIVATE "${GEN_OUT_DIR}")

    # 把生成出来的头文件路径往外传一层：
    #   1. 调用方不用把 HEADER 再抄一遍（抄错就是两个不同的产物）；
    #   2. 别的目标（例如生成代码的自检）可以把它当 include 目录用——
    #      生成源文件里写的是 `#include <inkgen/xxx/button_service.h>`，
    #      编译器得从 HEADER 所在目录往上找，所以那个目录也要进 include 路径。
    set(${target}_GENERATED_HEADER "${GEN_HEADER}" PARENT_SCOPE)
    set(${target}_GENERATED_INCLUDE_DIR "${GEN_OUT_DIR}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# 生成链路的**编译自检**：把 CXXCSS/ 下全部真实配置生成一份并编进一个最小 exe。
#
# 这条目标的存在理由：`inkgen --validate` 只证明"配置读得懂"，
# 而"生成出来的 .h/.cpp 能不能编过"是另一件事——两份产物里只要有任何一个
# 字段对不上运行期结构，红的就该是构建而不是 runtime。所以每次构建都真编一遍，
# 代价只是一个空 main。
#
# 想看生成结果：build/inkgen/all/button_service.h（+ *_register.cpp）。
# ---------------------------------------------------------------------------
file(GLOB INK_ALL_CSS_CONFIGS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/*.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/Button/*.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/Panel/*.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/Label/*.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/Image/*.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/Text/*.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/Theme/*.json")

if(INK_ALL_CSS_CONFIGS)
    add_executable(inkgen_compile_check tools/inkgen/compile_check.cpp)
    target_link_libraries(inkgen_compile_check PRIVATE inking::inking)
    ink_add_generated(inkgen_compile_check
        OUT_DIR   "${CMAKE_BINARY_DIR}"
        NAMESPACE "inking::cxxcss"
        HEADER    "inkgen/all/button_service.h"
        DIR       "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS")

    # 把"生成 + 编译 + 链接"整体挂成一条测试：`ctest -R inkgen` 就能单独跑。
    # 用 `--build` 而不是直接跑 exe —— 要验的正是**构建**这一步。
    if(INK_BUILD_TESTS)
        add_test(NAME inkgen_compile_check
                 COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}"
                         --target inkgen_compile_check)
    endif()
else()
    message(STATUS "Inking: CXXCSS/ 下没有配置，跳过生成链路的编译自检")
endif()

# ---------------------------------------------------------------------------
# 静默失效清单（`docs/AGENTS.md` §7）与自检源码的对账（0.3.0 T-14）。
#
# 清单是**手写**的（现象 / 触发条件没法从代码生成），所以只能对账"名字 + 条数"：
# 文档里的每条自检名，自检源码里必须真有 `NoteSilentFailure("<名字>")`。
# 这条测试挡的是"在文档里加一行、以为就做完了"。
# ---------------------------------------------------------------------------
if(INK_BUILD_TESTS)
    add_test(NAME silent_failure_list_in_sync
             COMMAND ${CMAKE_COMMAND}
                     -DDOC=${CMAKE_CURRENT_SOURCE_DIR}/docs/AGENTS.md
                     -DSELFTEST=${CMAKE_CURRENT_SOURCE_DIR}/examples/demo/selftest.cpp
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-silent-failure-list.cmake)
endif()

# ---------------------------------------------------------------------------
# 示例工程
# ---------------------------------------------------------------------------
if(INK_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# 安装（可选；只为把"库 + 头"搬到别处用）
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS inking
    EXPORT inkingTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# ---------------------------------------------------------------------------
# 导出：让消费方 `find_package(inking CONFIG)` 就能用
#
# 之前只有 `install(TARGETS)` 而没有 `EXPORT`，于是装完之后：
#   - 头与库都在磁盘上，但**没有任何 `*Targets.cmake`**；
#   - `find_package(inking)` 找不到包，消费方只能手写 include 路径和一堆
#     SDL3 / FFmpeg 链接项 —— 等于"装了但用不了"。
# 下面三件套补齐：导出 target、写版本文件、生成包配置。依赖在
# `cmake/inkingConfig.cmake.in` 里重新 find（见那份头部的注释）。
# ---------------------------------------------------------------------------
install(EXPORT inkingTargets
    FILE inkingTargets.cmake
    NAMESPACE inking::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/inking)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/inkingConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/inkingConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/inkingConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/inking)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/inkingConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/inkingConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/inking)

# CXXCSS 生成规则：**面向预编译 SDK**（只调 bin/inkgen.exe，不需要源码）。
# 随包装出去，消费方 include 一下就能 `ink_add_generated(...)` —— 没有它，
# 包里那个 inkgen.exe 只是躺在磁盘上的一个 exe，谁也接不上。
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/inking-inkgen.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/inking)

# **生成器本体也要装**：走 CXXCSS 路线的消费方靠 bin/inkgen.exe 干活。
# 之前漏了这条，于是 `cmake --install` 永远不会更新包里的 inkgen.exe ——
# 打包只能靠"手工从 build 目录复制"，而手工复制正是之前漏掉 cmake 配置的根源。
# 让 install 一并管掉，打包就只剩一条命令。
install(TARGETS inkgen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
