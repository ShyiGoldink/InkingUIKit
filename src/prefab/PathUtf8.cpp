// Windows 的系统头要最先包含（`WIN32_LEAN_AND_MEAN` / `NOMINMAX` 必须在它之前定义，
// 否则会被别的头抢先拉进来——那之后这两个宏就无效了）。
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "PathUtf8.h"

namespace inking::prefab {

std::string PathToUtf8(const std::filesystem::path& path) {
#if defined(_WIN32)
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }
    const int size = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
        nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                          static_cast<int>(wide.size()), out.data(), size,
                          nullptr, nullptr);
    return out;
#else
    // POSIX：`path::string()` 本来就是 UTF-8 字节。
    return path.string();
#endif
}

std::filesystem::path Utf8ToPath(const std::string& text) {
#if defined(_WIN32)
    if (text.empty()) {
        return std::filesystem::path{};
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    if (size <= 0) {
        return std::filesystem::path{};
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                          static_cast<int>(text.size()), wide.data(), size);
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(text);
#endif
}

}  // namespace inking::prefab
