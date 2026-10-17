#pragma once

// InkingFrontendDeveloper 日志系统。
// 结构移植自 InkingBackendFramework 的 ShineLog：静态类 + 可执行文件同级
// 目录下的 Log.html，页面按「天」和「程序启动会话」分组，写入时加锁。
//
// 编译期开关（CMake 选项 → 宏，见根目录 CMakeLists.txt）：
//   -DISLOG=ON    定义 INK_ISLOG   日志系统参与编译，输出 Log.html
//   -DISDEBUG=ON  定义 INK_ISDEBUG 调试级日志与调试断言生效
//
// 关闭时 INK_LOG_* 宏展开成 ((void)0)：参数不求值、字符串不拼接、日志代码
// 完全不进二进制。所以"关掉日志"是真的把代码从程序里去掉，而不是运行时
// 判断一下再跳过。

#include <filesystem>
#include <mutex>
#include <string>

namespace inking {

#if defined(INK_ISLOG)

class InkLog {
public:
    /** 普通日志。 */
    static void info(const std::string& moduleName, const std::string& message);
    /** 自检通过日志（绿色）。 */
    static void pass(const std::string& moduleName, const std::string& message);
    /** 警告日志（琥珀色）。 */
    static void warn(const std::string& moduleName, const std::string& message);
    /** 错误日志（红色）。 */
    static void error(const std::string& moduleName, const std::string& message);
    /** 调试日志（紫色）；只有 ISDEBUG 打开时宏才会调用到这里。 */
    static void debug(const std::string& moduleName, const std::string& message);

    /**
     * Log.html 的完整路径（可执行文件同级目录；取不到时退回当前目录）。
     *
     * 返回 `std::filesystem::path` 而不是 `std::string`，是为了**不丢编码**：
     * Windows 上路径要先经过宽字符才能正确处理非 ASCII 目录名，而
     * "窄字符串 → path" 那一步会按当前 locale 做字符集转换，失败就**抛异常**
     * （实测：程序装在中文目录里，一启动就 terminate）。
     */
    static std::filesystem::path logFilePath();

private:
    /** 日志颜色，决定写入 HTML 时使用的 class。 */
    enum class Color {
        Info,
        Pass,
        Warn,
        Error,
        Debug
    };

    /** 首次写入时确认日志文件存在，必要时创建页面骨架或备份旧格式文件。 */
    static void ensureLogFile();
    /** 追加一行带颜色的日志。 */
    static void writeLine(const std::string& moduleName,
                          const std::string& message,
                          Color color);
    /** 颜色枚举 → CSS class 名称。 */
    static std::string colorName(Color color);
    /** 当前本地时间，格式 YYYY-MM-DD HH:MM:SS。 */
    static std::string nowTime();
    /** 转义 HTML 特殊字符，避免日志内容破坏页面结构。 */
    static std::string escapeHtml(const std::string& text);
    /** 判断已有日志文件是否已经是当前网页格式。 */
    static bool fileUsesNewFormat(const std::filesystem::path& path);
    /** 判断文件末尾是否已经存在某一天的分组标题。 */
    static bool dayHeaderExists(const std::filesystem::path& path,
                                const std::string& day);
    /** 生成旧版日志的备份文件名。 */
    static std::filesystem::path legacyPathFor(const std::filesystem::path& path);

    static bool _checked;         /** 是否已经检查过日志文件 */
    static bool _sessionStarted;  /** 本次进程是否已经写入过会话标题 */
    static std::mutex _mutex;     /** 串行化写入，避免多线程写坏文件 */
};

#else  // !INK_ISLOG

/// 日志关闭时的空实现：即使有人绕过宏直接调用，也不会出现链接错误。
class InkLog {
public:
    static void info(const std::string&, const std::string&) noexcept {}
    static void pass(const std::string&, const std::string&) noexcept {}
    static void warn(const std::string&, const std::string&) noexcept {}
    static void error(const std::string&, const std::string&) noexcept {}
    static void debug(const std::string&, const std::string&) noexcept {}
    static std::filesystem::path logFilePath() { return {}; }
};

#endif  // INK_ISLOG

/** 日志系统是否在编译期启用。 */
#if defined(INK_ISLOG)
inline constexpr bool kLogEnabled = true;
#else
inline constexpr bool kLogEnabled = false;
#endif

/** 调试输出与调试断言是否在编译期启用。 */
#if defined(INK_ISDEBUG)
inline constexpr bool kDebugEnabled = true;
#else
inline constexpr bool kDebugEnabled = false;
#endif

}  // namespace inking

// ---------------------------------------------------------------------------
// 调用宏：日志关闭时整条语句消失，参数不会求值。
// ---------------------------------------------------------------------------
#if defined(INK_ISLOG)
#define INK_LOG_INFO(moduleName, message)  ::inking::InkLog::info((moduleName), (message))
#define INK_LOG_PASS(moduleName, message)  ::inking::InkLog::pass((moduleName), (message))
#define INK_LOG_WARN(moduleName, message)  ::inking::InkLog::warn((moduleName), (message))
#define INK_LOG_ERROR(moduleName, message) ::inking::InkLog::error((moduleName), (message))
#else
#define INK_LOG_INFO(moduleName, message)  ((void)0)
#define INK_LOG_PASS(moduleName, message)  ((void)0)
#define INK_LOG_WARN(moduleName, message)  ((void)0)
#define INK_LOG_ERROR(moduleName, message) ((void)0)
#endif

/** 调试日志：ISLOG 与 ISDEBUG 同时打开才会真正写入。 */
#if defined(INK_ISLOG) && defined(INK_ISDEBUG)
#define INK_LOG_DEBUG(moduleName, message) ::inking::InkLog::debug((moduleName), (message))
#else
#define INK_LOG_DEBUG(moduleName, message) ((void)0)
#endif

/** 调试断言：条件不成立时写一条错误日志；ISDEBUG 关闭时整段消失。 */
#if defined(INK_ISDEBUG)
#define INK_DEBUG_CHECK(condition, message)    \
    do {                                       \
        if (!(condition)) {                    \
            INK_LOG_ERROR("Debug", (message)); \
        }                                      \
    } while (false)
#else
#define INK_DEBUG_CHECK(condition, message) ((void)0)
#endif
