#pragma once

// 路径与 UTF-8 之间的转换（预制件 C5 内部用）。
//
// 为什么单独一份、而且只走 `std::filesystem::path`：
// **Windows 上"窄字符串 → `path`"在中文目录里会抛异常**（`AGENTS.md` §6 第 53 条：
// `GetModuleFileNameA` 给的是 ANSI/GBK，而我们的字符串是 UTF-8）。
// 异常发生在**构造 `path` 那一步**，所以 `error_code` 重载救不了——
// 唯一的解法是全程宽字符：`path` ↔ `wstring` ↔（显式 UTF-8 编解码）↔ `string`。
//
// 这个头是**内部件**（在 `src/` 下，不进 `include/`）：它不是框架的公开词汇，
// 只是预制件实现里的一步转换。

#include <filesystem>
#include <string>

namespace inking::prefab {

/// `path` → UTF-8 窄串（Windows 上走 `WideCharToMultiByte(CP_UTF8)`）。
std::string PathToUtf8(const std::filesystem::path& path);

/// UTF-8 窄串 → `path`（Windows 上先转宽字符再构造——**绝不**用窄串直接构造）。
std::filesystem::path Utf8ToPath(const std::string& text);

}  // namespace inking::prefab
