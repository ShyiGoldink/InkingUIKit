#pragma once

// InkingFrontendDeveloper 日志系统。
//
// 与 InkingBackendFramework 的 `ShineLog`（`basic/ShineLog.h`）是**同一套设计取向，
// 不是逐字同构**——写清楚这层关系，是为了让"改哪一边"有判据，而不是靠记忆：
//
//   **一样**（这两条一旦改，两边都得改，否则解析/报告工具会错位）
//     a. 纯文本唯一事实源、运行期不写 HTML；
//     b. 每行 7 段自带定位，且第 3 段固定写成 `模块:--`。
//
//   **前端没搬**（仍只在框架端，别在这里找）
//     `init(LogOptions)` / `shutdown()` / 差分快照 `snapshot()` /
//     控制台镜像 `setMirrorSink` + `INKING_LOG_CONSOLE` / 交互命令 `command()`。
//     级别集合也不同：框架端是 `Trace…Fatal` 六档，前端是
//     `Debug / Info / **Pass** / Warn / Error` 五档——`Pass` 是 GUI 库特有的
//     "成功流水"级别（调到 warn 时和 Info 一起被过滤）。
//
//   **前端特有**（`0.1.1` 加的，框架端没有；不需要反向同步）
//     DEBUG 独立折叠槽、WARN/ERROR 不折叠、`CountOf` / `ResetStats` 观测口。
//
// 下面四条是本层自己的设计原则（其中第 3 条里"前端特有"的那半段已就地标注）：
//
//   1. 纯文本是唯一事实源。文件是可被 grep / tail / less 直接消费的 .log，
//      运行期**不写任何 HTML**；浏览器视图改由 report() 从 .log 按需生成。
//   2. 每行自带定位：级别、模块、序号、线程号、源文件:行。
//   3. 连续相同的记录只落一条，后续合并成 FOLD 行并带重复次数；同一个键
//      每秒最多 5 条，其余只计数。GUI 库的告警很容易每帧刷，这层是刚需。
//      **DEBUG 另有一条独立的折叠槽**（`0.1.1` 加的）：别的级别的记录插进来
//      不会打断它的累计——实测反馈方那条噪声正是"每帧一条 DEBUG，中间夹着
//      别的日志"，共用的折叠槽对这种交错毫无作用。
//   4. 一次运行一个文件：LOG/<日期>/run-<时-分-秒>-<pid>.log，
//      另有 LOG/latest.txt 指向最近一次运行。
//
// 编译期开关（见根目录 CMakeLists.txt 的 target_compile_definitions）：
//   INK_ISLOG         日志系统参与编译；关掉时 INK_LOG_* 展开成 ((void)0)，
//                     参数不求值、字符串不拼接，日志代码完全不进二进制。
//   INK_ISDEBUG       调试级日志与调试断言生效。**由 CMake 选项
//                     `INKING_DEBUG_LOG` 控制，默认关**（`0.1.1` 之前它被
//                     无条件写成 PUBLIC，于是发布包的消费方**关不掉**调试流，
//                     948 条里有一条重复 528 次——噪声的根子是这条，不是
//                     某一句日志的级别写错了）。
//                     它仍然必须是 **PUBLIC** 而不是 PRIVATE：本头里
//                     `kDefaultLogLevel` / `kDebugEnabled` 是 `inline constexpr`，
//                     库与调用方取值不同就是 ODR 违例（见 CMakeLists.txt 里
//                     那段"一旦公开头出现这种分支，就必须改成 PUBLIC"）。
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
     * @brief 某个级别在**本次进程内发生了多少次**（编译期关闭时恒为 0）。
     *
     * 与 `statistics()` 的"落盘条数"不是一回事：被折叠、被限流、被当前阈值
     * 挡住的不算——这里数的是"真的调用了"，因为它要回答的问题是
     * **"这段代码到底发声了没有"**。
     *
     * 存在的理由很具体：从 `0.1.1` 起，好几条护栏的验收是"这里应当产生
     * 恰好 1 条 WARN"（零尺寸、全透明兜底、字体打不开）。没有这个观测口，
     * 那些断言只能靠人翻 `LOG/` 目录——而"只能靠眼看"的验收等于没有验收。
     */
    static std::uint64_t CountOf(LogLevel level);

    /**
     * @brief 清空分级计数、折叠/去重槽。
     *
     * **只给自检用，不是业务 API**：自检一个进程里会跑很多组断言，不归零就
     * 没法断言"这一组恰好产生了 1 条"。业务代码没有任何理由调它（调了会把
     * 已经记下的运行期统计抹掉）。
     */
    static void ResetStats();

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
    static std::uint64_t CountOf(LogLevel) noexcept { return 0; }
    static void ResetStats() noexcept {}
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
