#pragma once

// InkingFrontendDeveloper 日志系统。
//
// 与 InkingBackendFramework 的 ShineLog 保持同一套设计（2026-10 重构后）：
//
//   1. 纯文本是唯一事实源。文件是可被 grep / tail / less 直接消费的 .log，
//      运行期**不写任何 HTML**；浏览器视图改由 report() 从 .log 按需生成。
//   2. 每行自带定位：级别、模块、序号、线程号、源文件:行。
//   3. 连续相同的记录只落一条，后续合并成 FOLD 行并带重复次数；同一个键
//      每秒最多 5 条，其余只计数。GUI 库的告警很容易每帧刷，这层是刚需。
//   4. 一次运行一个文件：LOG/<日期>/run-<时-分-秒>-<pid>.log，
//      另有 LOG/latest.txt 指向最近一次运行。
//
// 编译期开关（见根目录 CMakeLists.txt 的 target_compile_definitions）：
//   INK_ISLOG    日志系统参与编译；关掉时 INK_LOG_* 展开成 ((void)0)，
//                参数不求值、字符串不拼接，日志代码完全不进二进制。
//   INK_ISDEBUG  调试级日志与调试断言生效。
//
// 注意：关掉 ISLOG 时本类的空实现必须与真实现**签名一致**，
// 否则宏以外的直接调用会编译不过（.cpp 在关闭时是整段不参与的）。

#include <cstdint>
#include <filesystem>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace inking {

/**
 * @brief 日志级别。
 *
 * Pass 是前端特有的"成功流水"级别，排在 Info 之后：把阈值调到 warn，
 * 它和 Info 一起被过滤；调到 info，它照常保留。
 */
enum class LogLevel : std::uint8_t {
    Debug = 0,
    Info,
    Pass,
    Warn,
    Error
};

#if defined(INK_ISLOG)

/** 默认阈值：开了 INK_ISDEBUG 记 Debug，否则从 Info 起。 */
#if defined(INK_ISDEBUG)
inline constexpr LogLevel kDefaultLogLevel = LogLevel::Debug;
#else
inline constexpr LogLevel kDefaultLogLevel = LogLevel::Info;
#endif

class InkLog {
public:
    /** 普通日志。 */
    static void info(const std::string& moduleName, const std::string& message,
                     const std::source_location& site = std::source_location::current());
    /** 自检通过日志。 */
    static void pass(const std::string& moduleName, const std::string& message,
                     const std::source_location& site = std::source_location::current());
    /** 警告日志。 */
    static void warn(const std::string& moduleName, const std::string& message,
                     const std::source_location& site = std::source_location::current());
    /** 错误日志；不参与折叠，每一条都会落盘。 */
    static void error(const std::string& moduleName, const std::string& message,
                      const std::source_location& site = std::source_location::current());
    /** 调试日志；只有 ISDEBUG 打开时宏才会调用到这里。 */
    static void debug(const std::string& moduleName, const std::string& message,
                      const std::source_location& site = std::source_location::current());

    /**
     * @brief 写一行人工打点（伪级别 MARK）。
     *
     * 不受级别阈值影响：把日志调安静以后，"我从这里开始复现"依然是刚需。
     */
    static void mark(std::string_view text,
                     const std::source_location& site = std::source_location::current());

    /**
     * @brief 当前日志文件路径（首次调用会创建文件与目录）。
     *
     * 返回 `std::filesystem::path` 而不是 `std::string`，是为了**不丢编码**：
     * Windows 上路径要先经过宽字符才能正确处理非 ASCII 目录名，而
     * "窄字符串 → path" 那一步会按当前 locale 做字符集转换，失败就**抛异常**
     * （实测：程序装在中文目录里，一启动就 terminate）。
     */
    static std::filesystem::path logFilePath();

    /** 日志根目录（可执行文件同级的 LOG/）。 */
    static std::filesystem::path logDirectory();

    /** 运行时调整阈值。 */
    static void setLevel(LogLevel level);
    /** 读取当前阈值。 */
    static LogLevel level();
    /** 解析级别名字；无法识别时返回 false。 */
    static bool parseLevel(std::string_view text, LogLevel& result);

    /** 读取内存环形缓冲里最近 count 行。 */
    static std::vector<std::string> tail(std::size_t count);
    /** 在缓冲里按子串检索；module 为空表示不过滤模块。 */
    static std::vector<std::string> grep(std::string_view pattern, std::string_view module);
    /** 统计摘要：总条数、折叠数、限流数、按级别与模块的计数。 */
    static std::string statistics();

    /**
     * @brief 从当前 .log 生成一份自包含 HTML 报告。
     * @return 报告路径；失败时返回空 path。
     *
     * 日志原文内联在页面里，双击即可打开，不依赖本地服务器，
     * 也不会被浏览器的 file:// 跨域策略拦住。
     */
    static std::filesystem::path report();

private:
    /** 记录种类：普通记录、人工打点。折叠行由写盘层自己产生。 */
    enum class Kind {
        Record,
        Mark
    };

    /** 统一的落盘入口：级别过滤、折叠、限流、写文件。 */
    static void write(LogLevel level, const std::string& moduleName,
                      const std::string& message, Kind kind,
                      const std::source_location& site);
};

#else  // !INK_ISLOG

/// 日志关闭时的空实现：签名与真实现一致，绕过宏直接调用也能编译。
class InkLog {
public:
    static void info(const std::string&, const std::string&,
                     const std::source_location& = std::source_location::current()) noexcept {}
    static void pass(const std::string&, const std::string&,
                     const std::source_location& = std::source_location::current()) noexcept {}
    static void warn(const std::string&, const std::string&,
                     const std::source_location& = std::source_location::current()) noexcept {}
    static void error(const std::string&, const std::string&,
                      const std::source_location& = std::source_location::current()) noexcept {}
    static void debug(const std::string&, const std::string&,
                      const std::source_location& = std::source_location::current()) noexcept {}
    static void mark(std::string_view,
                     const std::source_location& = std::source_location::current()) noexcept {}
    static std::filesystem::path logFilePath() { return {}; }
    static std::filesystem::path logDirectory() { return {}; }
    static void setLevel(LogLevel) noexcept {}
    static LogLevel level() noexcept { return LogLevel::Info; }
    static bool parseLevel(std::string_view, LogLevel&) noexcept { return false; }
    static std::vector<std::string> tail(std::size_t) { return {}; }
    static std::vector<std::string> grep(std::string_view, std::string_view) { return {}; }
    static std::string statistics() { return {}; }
    static std::filesystem::path report() { return {}; }
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
// 每次调用都把 std::source_location::current() 传下去，日志里的"文件:行"
// 才会指向业务代码，而不是 InkLog.cpp 这个转发点。
// ---------------------------------------------------------------------------
#if defined(INK_ISLOG)
#define INK_LOG_INFO(moduleName, message) \
    ::inking::InkLog::info((moduleName), (message), ::std::source_location::current())
#define INK_LOG_PASS(moduleName, message) \
    ::inking::InkLog::pass((moduleName), (message), ::std::source_location::current())
#define INK_LOG_WARN(moduleName, message) \
    ::inking::InkLog::warn((moduleName), (message), ::std::source_location::current())
#define INK_LOG_ERROR(moduleName, message) \
    ::inking::InkLog::error((moduleName), (message), ::std::source_location::current())
#else
#define INK_LOG_INFO(moduleName, message)  ((void)0)
#define INK_LOG_PASS(moduleName, message)  ((void)0)
#define INK_LOG_WARN(moduleName, message)  ((void)0)
#define INK_LOG_ERROR(moduleName, message) ((void)0)
#endif

/** 调试日志：ISLOG 与 ISDEBUG 同时打开才会真正写入。 */
#if defined(INK_ISLOG) && defined(INK_ISDEBUG)
#define INK_LOG_DEBUG(moduleName, message) \
    ::inking::InkLog::debug((moduleName), (message), ::std::source_location::current())
#else
#define INK_LOG_DEBUG(moduleName, message) ((void)0)
#endif

/** 人工打点：不受运行期阈值影响，用于标记复现起点。 */
#if defined(INK_ISLOG)
#define INK_LOG_MARK(text) ::inking::InkLog::mark((text), ::std::source_location::current())
#else
#define INK_LOG_MARK(text) ((void)0)
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
