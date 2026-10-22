# ctest `field_table_in_sync`：文档里的字段表必须与 `inkgen --dump-fields` 一致。
#
# 用法（由 cmake/inkgen.cmake 注册）：
#   cmake -DINKGEN=<inkgen 可执行> -DDOC=<CXXCSS.md> -P check-field-table.cmake
#
# 为什么比文本：表是**生成的**（`tools/inkgen/Fields.h`），文档是锚点块里的
# **一份拷贝**。防漂移唯一可靠的判据就是"两边逐字相同"——解析 Markdown 再比
# 结构，等于把生成器再实现一遍，错法只会更多。
#
# 修复办法只有一条：重跑 `inkgen --dump-fields`，把它贴回 §9 的两个锚点之间。

if(NOT DEFINED INKGEN)
    message(FATAL_ERROR "缺少 -DINKGEN=<inkgen 可执行>")
endif()
if(NOT DEFINED DOC)
    message(FATAL_ERROR "缺少 -DDOC=<CXXCSS.md>")
endif()
if(NOT DEFINED OUT)
    message(FATAL_ERROR "缺少 -DOUT=<dump 临时文件>")
endif()

# 必须走**文件**而不是 OUTPUT_VARIABLE：Windows 上 CMake 按控制台代码页
# （cp936）解码子进程的 stdout，UTF-8 的中文列会被解成乱码；落文件再
# `file(READ)` 拿到的才是原始字节（实测：同一个 dump，变量方式 8751 字符、
# 文件方式 7512 字符，且内容不同）。
execute_process(
    COMMAND "${INKGEN}" --dump-fields
    OUTPUT_FILE "${OUT}"
    RESULT_VARIABLE dumpResult)
if(NOT dumpResult EQUAL 0)
    message(FATAL_ERROR "inkgen --dump-fields 失败（退出码 ${dumpResult}）")
endif()
file(READ "${OUT}" dumped)
string(STRIP "${dumped}" dumped)

file(READ "${DOC}" doc)

set(beginMarker "<!-- BEGIN FIELD TABLE -->")
set(endMarker "<!-- END FIELD TABLE -->")
string(FIND "${doc}" "${beginMarker}" beginPos)
string(FIND "${doc}" "${endMarker}" endPos)
if(beginPos EQUAL -1 OR endPos EQUAL -1 OR endPos LESS beginPos)
    message(FATAL_ERROR
        "${DOC} 里找不到字段表的锚点块（${beginMarker} … ${endMarker}）。"
        "这两个注释是 ctest 的定位依据，删了就等于把这条测试静默关掉。")
endif()

# 取两个锚点之间的内容：起点要跳过"开始标记 + 它后面的一个换行"。
string(LENGTH "${beginMarker}" beginLength)
math(EXPR blockStart "${beginPos} + ${beginLength}")
string(SUBSTRING "${doc}" ${blockStart} -1 afterBegin)
# 去掉开始标记后面的第一个换行（文档里的排版要求：标记独占一行）。
string(FIND "${afterBegin}" "\n" firstNewline)
if(firstNewline GREATER -1)
    math(EXPR blockStart2 "${firstNewline} + 1")
    string(SUBSTRING "${afterBegin}" ${blockStart2} -1 afterBegin)
endif()
string(FIND "${afterBegin}" "${endMarker}" blockEnd)
if(blockEnd EQUAL -1)
    message(FATAL_ERROR "字段表锚点块没有闭合（缺 ${endMarker}）")
endif()
string(SUBSTRING "${afterBegin}" 0 ${blockEnd} block)
string(STRIP "${block}" block)

if(NOT block STREQUAL dumped)
    # 报"差在哪儿"：长度 + 第一处不同。逐行 diff 在 CMake 里不划算，
    # 这点信息足够定位（表是生成的，重贴就好）。
    string(LENGTH "${block}" blockLength)
    string(LENGTH "${dumped}" dumpedLength)
    set(hint "")
    if(NOT blockLength EQUAL dumpedLength)
        string(APPEND hint
            "长度不同：文档 ${blockLength} 字符，dump ${dumpedLength} 字符。\n")
    endif()
    # 找最长公共前缀，定位第一处不同。
    set(limit ${blockLength})
    if(dumpedLength LESS limit)
        set(limit ${dumpedLength})
    endif()
    set(mismatch -1)
    set(i 0)
    while(i LESS limit)
        string(SUBSTRING "${block}" ${i} 1 a)
        string(SUBSTRING "${dumped}" ${i} 1 b)
        if(NOT a STREQUAL b)
            set(mismatch ${i})
            break()
        endif()
        math(EXPR i "${i} + 1")
    endwhile()
    if(mismatch GREATER -1)
        math(EXPR contextStart "${mismatch} - 40")
        if(contextStart LESS 0)
            set(contextStart 0)
        endif()
        string(SUBSTRING "${block}" ${contextStart} 80 docSnippet)
        string(SUBSTRING "${dumped}" ${contextStart} 80 dumpSnippet)
        string(APPEND hint
            "第 ${mismatch} 个字符起不同：\n  文档：${docSnippet}\n  dump：${dumpSnippet}\n")
    endif()
    message(FATAL_ERROR
        "CXXCSS.md §9 的字段表与生成器不一致（field_table_in_sync）。\n"
        "${hint}"
        "修法：跑一次 `inkgen --dump-fields`，把输出贴回两个锚点之间。"
        "表是生成的，**不要手改**。")
endif()

message(STATUS "字段表一致（${dumpedLength} 字符）")
