#pragma once

// 生成器侧的**薄壳**（0.6.5 T-15）。
//
// 解析器本体已搬进库：`include/util/InkingJson.h` + `src/util/InkingJson.cpp`，
// 命名空间 `inking::json`。这个头只做两件事：
//   1. 把旧名字（`JsonValue` / `ParseResult` / `SourceLocation` / `ParseJson`）留成
//      别名 —— 于是 `Cxxcss.cpp` / `Emit.cpp` **一行都不用改**，搬迁的那次风险被压到
//      "只可能搬错实现"，不会同时改调用点；
//   2. 保留 `LoadJsonFile`：它的路径语义是**相对当前目录**（工具的输入是命令行给的
//      路径），而库里的 `ParseAsset` 是**相对资源根**。两件事，不合并。

#include <util/InkingJson.h>

#include <string>

namespace inkgen {

using JsonValue = inking::json::Value;
using ParseResult = inking::json::Result;
using SourceLocation = inking::json::SourceLocation;

/// 解析（旧名字）。新代码直接用 `inking::json::Parse`。
inline ParseResult ParseJson(const std::string& text) {
    return inking::json::Parse(text);
}

/// 读文件 + 解析。读不开时 `ok == false`，错误里带文件名。
ParseResult LoadJsonFile(const std::string& path);

}  // namespace inkgen
