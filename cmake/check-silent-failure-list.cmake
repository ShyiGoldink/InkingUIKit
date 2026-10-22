# ctest `silent_failure_list_in_sync`：`docs/AGENTS.md` §7 的清单必须与自检源码对齐。
#
# 用法（由 CMakeLists.txt 注册）：
#   cmake -DDOC=<docs/AGENTS.md> -DSELFTEST=<examples/demo/selftest.cpp> \
#         -P check-silent-failure-list.cmake
#
# 为什么需要这条测试：清单的价值全在"**每条都真的有一个自检**"。只在文档里加一行
# 太容易了——写完就以为做完了，而实际断言压根没写。所以判据是**双向的**：
#
#   1. 文档表里的每个自检名，源码里必须有 `NoteSilentFailure("<名字>")`
#      （登记就发生在真正的断言旁边，改断言时很难绕过它）；
#   2. 两边的条数必须相等（源码多登记一条 = 文档漏写；文档多一行 = 断言没写）。
#
# 与 `field_table_in_sync` 的区别：那张表是**生成的**，逐字比对即可；这张表是
# **手写的**（现象 / 触发条件没法从代码生成），所以只对账"名字 + 条数"。

if(NOT DEFINED DOC)
    message(FATAL_ERROR "缺少 -DDOC=<docs/AGENTS.md>")
endif()
if(NOT DEFINED SELFTEST)
    message(FATAL_ERROR "缺少 -DSELFTEST=<examples/demo/selftest.cpp>")
endif()

# 走文件而不是变量：Windows 上 CMake 按控制台代码页解码子进程输出，中文会乱码。
file(READ "${DOC}" doc)

set(beginMarker "<!-- BEGIN SILENT FAILURE LIST -->")
set(endMarker "<!-- END SILENT FAILURE LIST -->")
string(FIND "${doc}" "${beginMarker}" beginPos)
string(FIND "${doc}" "${endMarker}" endPos)
if(beginPos EQUAL -1 OR endPos EQUAL -1 OR endPos LESS beginPos)
    message(FATAL_ERROR
        "${DOC} 里找不到静默失效清单的锚点块（${beginMarker} … ${endMarker}）。"
        "这两个注释是 ctest 的定位依据，删了就等于把这条测试静默关掉。")
endif()

string(LENGTH "${beginMarker}" beginLength)
math(EXPR blockStart "${beginPos} + ${beginLength}")
string(SUBSTRING "${doc}" ${blockStart} -1 afterBegin)
string(FIND "${afterBegin}" "${endMarker}" blockEnd)
string(SUBSTRING "${afterBegin}" 0 ${blockEnd} block)

# 逐行取出最后一列的 `反引号名字`。表格行的形状是固定的（5 列，名字在第 5 列），
# 但这里不硬编列号：**取该行最后一个反引号对**，这样将来加一列也不会假红。
string(REPLACE "\n" ";" lines "${block}")
set(docNames "")
set(docRows 0)
foreach(line IN LISTS lines)
    string(STRIP "${line}" line)
    if(NOT line MATCHES "^\\|")
        continue()
    endif()
    if(line MATCHES "^\\| *-")
        continue()  # 表头分隔行
    endif()
    # 抓最后一个 `...`
    string(REGEX MATCHALL "`[^`]+`" ticks "${line}")
    if(NOT ticks)
        message(FATAL_ERROR
            "清单里有一行没有反引号包住的自检名：\n  ${line}\n"
            "自检名必须写成 `name`（ctest 靠它定位）。")
    endif()
    list(LENGTH ticks tickCount)
    math(EXPR lastIndex "${tickCount} - 1")
    list(GET ticks ${lastIndex} tick)
    string(REPLACE "`" "" name "${tick}")
    list(APPEND docNames "${name}")
    math(EXPR docRows "${docRows} + 1")
endforeach()

if(docRows EQUAL 0)
    message(FATAL_ERROR "静默失效清单是空的（锚点块里没有任何表格行）。")
endif()

file(READ "${SELFTEST}" selftest)
string(REGEX MATCHALL "NoteSilentFailure\\(\"[^\"]+\"\\)" calls "${selftest}")
list(LENGTH calls codeRows)

set(missing "")
foreach(name IN LISTS docNames)
    string(FIND "${selftest}" "NoteSilentFailure(\"${name}\")" found)
    if(found EQUAL -1)
        string(APPEND missing
            "  - 文档里有 `${name}`，但 ${SELFTEST} 里没有对应的 "
            "NoteSilentFailure(\"${name}\") 登记\n")
    endif()
endforeach()

if(NOT missing STREQUAL "")
    message(FATAL_ERROR
        "静默失效清单与自检源码不一致（silent_failure_list_in_sync）：\n"
        "${missing}"
        "修法：要么把那条自检真的写出来并在旁边登记，要么把它从清单里删掉"
        "（清单只收有断言撑腰的条目）。")
endif()

if(NOT docRows EQUAL codeRows)
    message(FATAL_ERROR
        "静默失效清单的条数与自检源码里的登记数不一致（silent_failure_list_in_sync）：\n"
        "  文档表：${docRows} 条\n"
        "  源码登记：${codeRows} 处\n"
        "多出来的那一侧要么是文档漏写、要么是断言没写。")
endif()

message(STATUS "静默失效清单一致（${docRows} 条，每条都有登记）")
