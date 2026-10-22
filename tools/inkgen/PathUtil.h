#pragma once

// 窄字符串 → `std::filesystem::path` 的**唯一入口**（0.2.0 T-09）。
//
// 为什么不能直接写 `std::filesystem::path(s)`：Windows + libstdc++ 下那个
// 构造函数用 "C" locale 的 `codecvt` 解码窄字符串，**ASCII 以外的字节直接抛
// `Illegal byte sequence`**——工程放在中文目录里，inkgen 连启动都过不去
// （实测：`--dir D:/项目/界面/CXXCSS` 直接 terminate）。
//
// 正确做法是按**系统 ANSI 代码页**（argv 与 Win32 文件 API 的编码）转成宽字符，
// 再用宽字符串建 `path`：路径在内部就是 UTF-16，之后 `exists` / 迭代 /
// `ifstream(fs::path)` 全部正常（实测过）。
//
// 非 Windows 平台就是一个普通构造（那边的窄字符串本来就是字节序列）。

#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace inkgen {

/// 把 argv / 配置里写的路径（窄字节）转成 `std::filesystem::path`。
inline std::filesystem::path ToPath(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) {
        return {};
    }
    const int length = static_cast<int>(text.size());
    const int count
        = MultiByteToWideChar(CP_ACP, 0, text.c_str(), length, nullptr, 0);
    if (count <= 0) {
        return {};  // 转不出来（非法字节）：交给调用方按"路径不存在"报错
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), length, wide.data(), count);
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(text);
#endif
}

/**
 * `fs::path` → 窄字符串（**系统 ANSI 代码页**，不是 UTF-8）。
 *
 * 为什么不能用 `path.string()`：libstdc++ 在 Windows 上按 **UTF-8** 返回，
 * 而 `std::ifstream(窄串)` / `ToPath` 走的是 ANSI——两者混用就会"扫描找得到、
 * 打开打不开"（T-09 实测）。所以凡是"路径变成 string"的地方都走这里，
 * 凡是"string 变成路径"的地方都走 `ToPath`，中间不再有第三种编码。
 */
inline std::string ToNarrow(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }
    const int length = static_cast<int>(wide.size());
    const int count = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), length,
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return {};
    }
    std::string narrow(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), length, narrow.data(), count,
                        nullptr, nullptr);
    return narrow;
#else
    return path.string();
#endif
}

/// `fs::path` → **UTF-8** 串。给"写进产物 / 给人看的文字"用，**不给文件 I/O**。
///
/// 产物（生成的头 / 源）是 UTF-8 带 BOM：注释里写 ANSI 路径会得到非法字节
/// （实测："新建文件夹" 变成 `\xD0\xC2\xBD\xA8...`，编辑器里一片问号）。
inline std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()),
                       text.size());
}

/// 内部路径串（系统 ANSI）→ UTF-8。产物注释里写路径时用它。
inline std::string PathToUtf8(const std::string& text) {
    return ToUtf8(ToPath(text));
}

}  // namespace inkgen
