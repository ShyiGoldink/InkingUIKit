# ctest `field_pure_headers_have_no_sdl`：`field/` 的**纯函数层**不许拖进 SDL。
#
# 用法（由 CMakeLists.txt 注册）：
#   cmake -DHEADER=<include/field/InkingFieldProjection.h> \
#         -DSOURCE=<src/field/InkingFieldProjection.cpp> \
#         -P check-field-pure-headers.cmake
#
# 名字里的 `field` 是**场地**（`include/field/`），与 `field_table_in_sync`
# 那条（CXXCSS **配置字段**表）无关——两条测试名字撞车，别改错文件。
#
# 为什么要一条**机械**检查：
#
#   1. "场地能被无窗口测试"这条承诺的全部支撑就是这一层零 SDL 依赖。它一旦
#      被某个 `#include <SDL3/SDL.h>` 破掉，没有任何编译错误——只是从此不能
#      在 headless 进程里用（而那正是引擎 core 进程唯一的用法）；
#   2. 本库的公开面规矩本来就是"新公开头零 SDL 依赖"（`docs/AGENTS.md` §6
#      第 8 条），但那条规矩一直靠人眼，靠人眼就会漏。
#
# 判据分两层，都只看 **`#include` 行**（注释里提到 SDL 是应该的——那正是
# "本层不依赖它"这句话本身）：
#   - 头文件里**只许** `<cstdint>` / `<cstddef>` / `<cmath>`；
#   - 实现文件同样不许 include 任何 SDL 头（它要能单独 `-fsyntax-only` 编过）。

if(NOT DEFINED HEADER)
    message(FATAL_ERROR "缺少 -DHEADER=<include/field/InkingFieldProjection.h>")
endif()
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "缺少 -DSOURCE=<src/field/InkingFieldProjection.cpp>")
endif()

# 走文件而不是命令管道：Windows 上按控制台代码页解码，中文会乱。
file(READ "${HEADER}" header)

# 只许这几个标准头（任务书 §1 第 2 条）。任何 SDL 头都会撞在这条上，
# 不需要再单独查一遍。
string(REGEX MATCHALL "#include[ \t]*<[^>]+>" includes "${header}")
foreach(include IN LISTS includes)
    string(REGEX REPLACE "#include[ \t]*<([^>]+)>" "\\1" name "${include}")
    if(NOT name MATCHES "^(cstdint|cstddef|cmath)$")
        message(FATAL_ERROR
            "${HEADER} 包含了 <${name}>。\n"
            "纯函数层只许 <cstdint> / <cstddef> / <cmath>；要多一个类型，"
            "就说明它已经不是纯函数层了。")
    endif()
endforeach()

file(READ "${SOURCE}" source)
string(REGEX MATCHALL "#include[ \t]*[<\"]SDL[^>\"]*[>\"]" sdlIncludes "${source}")
if(sdlIncludes)
    message(FATAL_ERROR
        "${SOURCE} 包含了 SDL 头：${sdlIncludes}\n"
        "实现文件同样不许（它要能单独 `-fsyntax-only` 编过）。")
endif()

message(STATUS "场地纯函数层不依赖 SDL（${HEADER}）")
