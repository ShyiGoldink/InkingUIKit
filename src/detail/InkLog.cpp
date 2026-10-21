#include <detail/InkLog.h>

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#if defined(INK_ISLOG)

// 下面整段只在日志打开时参与编译。关掉时 `InkLog.h` 给的是**空实现类**
// （没有 `Impl`、没有缓冲），这个文件必须整体消失——否则 `INK_ISLOG` 关掉
// 的结果是"编译不过"，而不是"日志从二进制里彻底消失"，那个开关也就没意义了。

namespace inking {
namespace {

/** 内存环形缓冲容量：tail/grep 直接读这里，不碰磁盘。 */
constexpr std::size_t kRingCapacity = 2000;

/** 同一个键每秒最多落盘多少条；超出的只计数，由 FOLD 行汇总。 */
constexpr int kMaxRecordsPerSecondPerKey = 5;

/** 日志根目录名：可执行文件同级的 LOG/。 */
constexpr const wchar_t* kLogRootFolder = L"LOG";

// ---- 时间 ----

std::tm localTimeOf(const std::time_t value) {
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &value);
#else
    localtime_r(&value, &parts);
#endif
    return parts;
}

std::string formatTime(const std::chrono::system_clock::time_point& point, const char* format) {
    const std::time_t value = std::chrono::system_clock::to_time_t(point);
    const std::tm parts = localTimeOf(value);
    std::ostringstream output;
    output << std::put_time(&parts, format);
    return output.str();
}

long long millisecondsOf(const std::chrono::system_clock::time_point& point) {
    return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      point.time_since_epoch())
                                      .count()) %
           1000;
}

std::string timeOfDay(const std::chrono::system_clock::time_point& point) {
    std::ostringstream output;
    output << formatTime(point, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
           << millisecondsOf(point);
    return output.str();
}

std::string dateOf(const std::chrono::system_clock::time_point& point) {
    return formatTime(point, "%Y-%m-%d");
}

std::string fullStamp(const std::chrono::system_clock::time_point& point) {
    std::ostringstream output;
    output << formatTime(point, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
           << millisecondsOf(point);
    return output.str();
}

// ---- 路径 ----

long processId() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

/**
 * 可执行文件路径。
 *
 * Windows 必须走 W 版：A 版返回系统 ANSI 代码页的窄字符串，装在中文目录里
 * 会拿到一串 GBK 字节，而 `std::filesystem::path` 拿到窄字符串会按 locale 做
 * 字符集转换，转不动就抛 filesystem_error——程序一启动就 terminate。
 */
std::filesystem::path executablePath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (size == buffer.size()) {
        // 返回值等于容量说明可能被截断，扩容重来一次
        buffer.resize(buffer.size() * 2);
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (size > 0) {
        return std::filesystem::path(std::wstring(buffer.data(), size));
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return std::filesystem::path(buffer.data());
    }
#else
    std::vector<char> buffer(4096);
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size > 0) {
        buffer[static_cast<std::size_t>(size)] = '\0';
        return std::filesystem::path(buffer.data());
    }
#endif
    return std::filesystem::path{};
}

std::filesystem::path logRootPath() {
    const std::filesystem::path executable = executablePath();
    const std::filesystem::path parent =
        executable.empty() ? std::filesystem::path(L".") : executable.parent_path();
    return parent.empty() ? std::filesystem::path(kLogRootFolder) : parent / kLogRootFolder;
}

// ---- 线程与文本 ----

/** 线程短号：按创建顺序编号，显示为 T-01、T-02，比裸 id 好读也稳定。 */
int shortThreadId() {
    thread_local const int assigned = []() {
        static std::atomic<int> next{1};
        return next.fetch_add(1);
    }();
    return assigned;
}

const char* levelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Pass:  return "PASS ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO ";
}

constexpr const char* kTagMark = "MARK ";
constexpr const char* kTagFold = "FOLD ";

std::string twoDigits(int value) {
    std::ostringstream output;
    output << std::setw(2) << std::setfill('0') << value;
    return output.str();
}

std::string sixDigits(std::uint64_t value) {
    std::ostringstream output;
    output << std::setw(6) << std::setfill('0') << value;
    return output.str();
}

/** 把多行内容压成单行：日志的行结构不能被消息里的换行破坏。 */
std::string flatten(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (const char item : text) {
        if (item == '\r') {
            continue;
        }
        result += (item == '\n') ? ' ' : item;
    }
    return result;
}

std::string fileNameOf(const std::string& filePath) {
    const std::size_t lastSlash = filePath.find_last_of("/\\");
    return lastSlash == std::string::npos ? filePath : filePath.substr(lastSlash + 1);
}

/**
 * 把路径转成 UTF-8，用于打印。
 *
 * 不能直接用 `path::string()`：Windows 上它按当前 locale 转换，遇到中文目录
 * 可能抛异常；`u8string()` 返回的是 `char8_t`，又不能直接塞进 ostringstream。
 * 这里显式走一次 WideCharToMultiByte，全程不依赖 locale。
 */
std::string utf8Of(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), size,
                        nullptr, nullptr);
    return utf8;
#else
    return path.string();
#endif
}

struct Impl {
    std::mutex mutex;
    std::ofstream file;
    std::filesystem::path path;
    std::string runId;
    std::string currentDay;
    LogLevel threshold = kDefaultLogLevel;
    bool initialized = false;
    std::uint64_t sequence = 0;
    std::uint64_t records = 0;
    std::uint64_t folded = 0;
    std::uint64_t suppressed = 0;

    std::deque<std::string> ring;

    // 折叠状态：只在"连续相同"时生效，被别的记录打断就结算
    std::string pendingKey;
    std::string pendingModule;
    std::uint64_t pendingCount = 0;
    std::uint64_t pendingSuppressed = 0;
    std::string pendingFirst;
    std::string pendingLast;

    // 限流窗口
    long long rateSecond = -1;
    std::unordered_map<std::string, int> rate;

    // 统计
    std::unordered_map<std::string, std::uint64_t> levelCounts;
    std::unordered_map<std::string, std::uint64_t> moduleCounts;
};

/**
 * 日志状态故意不析构。
 *
 * 这个库是共享库，静态对象析构顺序不可控：别的静态对象的析构函数里若还有
 * 一次日志调用，撞上"日志对象已经析构"就是 use-after-free。这里放一份永不
 * 释放的状态，代价只有几十字节，换来的是退出阶段怎么打日志都不会炸。
 */
Impl& state() {
    static Impl* instance = new Impl();
    return *instance;
}

void pushRing(Impl& st, const std::string& line) {
    st.ring.push_back(line);
    if (st.ring.size() > kRingCapacity) {
        st.ring.pop_front();
    }
}

void writeLineLocked(Impl& st, const std::string& line) {
    pushRing(st, line);
    if (st.file) {
        st.file << line << '\n';
        st.file.flush();
    }
    ++st.records;
}

std::string composeLine(const char* tag, const std::string& module, std::uint64_t sequence,
                        const std::string& site, const std::string& message) {
    std::ostringstream output;
    // 第 3 段固定写成 "模块:--"：这个库没有阶段编号，但保留这一列，
    // 两个工程的日志因此可以用同一套解析与报告工具。
    output << timeOfDay(std::chrono::system_clock::now()) << " | " << tag << " | " << module
           << ":-- | #" << sixDigits(sequence) << " | T-" << twoDigits(shortThreadId()) << " | "
           << site << " | " << message;
    return output.str();
}

/** 结算上一段折叠：只有真的重复过才补一行 FOLD，避免多写一行废话。 */
void flushPendingLocked(Impl& st) {
    if (st.pendingCount > 1 || st.pendingSuppressed > 0) {
        st.folded += st.pendingCount > 1 ? (st.pendingCount - 1) : 0;
        std::ostringstream message;
        message << "上一条消息重复 ×" << st.pendingCount;
        if (st.pendingSuppressed > 0) {
            message << "，另有 " << st.pendingSuppressed << " 条被限流丢弃";
        }
        message << "，" << st.pendingFirst << " ~ " << st.pendingLast;
        writeLineLocked(st, composeLine(kTagFold, st.pendingModule, ++st.sequence, "InkLog.cpp:0",
                                        message.str()));
    }
    st.pendingKey.clear();
    st.pendingCount = 0;
    st.pendingSuppressed = 0;
}

bool openFileLocked(Impl& st, const std::chrono::system_clock::time_point& now) {
    const std::string day = dateOf(now);
    const std::filesystem::path directory = logRootPath() / day;

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }

    const std::string base = "run-" + formatTime(now, "%H%M%S") + "-" + std::to_string(processId());
    std::filesystem::path path = directory / (base + ".log");
    for (int suffix = 1; suffix < 1000; ++suffix) {
        if (!std::filesystem::exists(path, error)) {
            break;
        }
        path = directory / (base + "-" + std::to_string(suffix) + ".log");
    }

    st.file.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!st.file) {
        return false;
    }
    st.file.setf(std::ios::unitbuf);
    st.path = path;
    st.currentDay = day;
    st.runId = "r-" + formatTime(now, "%Y%m%d-%H%M%S") + "-" + std::to_string(processId());

    std::ostringstream header;
    header << "# InkingFrontendDeveloper log\n"
           << "# run        " << st.runId << '\n'
           << "# started    " << fullStamp(now) << '\n'
           << "# level      " << levelTag(st.threshold) << '\n'
           << "# note       第 3 段为模块:阶段，前端无阶段概念，固定为 --\n";
    st.file << header.str();
    st.file.flush();

    // latest.txt：给"最近一次运行在哪"一个稳定入口。
    // 只写 ASCII 相对路径，避免非 ASCII 目录在 UTF-8 文本里被转坏。
    std::ofstream latest(logRootPath() / "latest.txt",
                         std::ios::out | std::ios::binary | std::ios::trunc);
    if (latest) {
        latest << "run=" << st.runId << '\n'
               << "path=" << day << "/" << (base + ".log") << '\n'
               << "started=" << fullStamp(now) << '\n'
               << "pid=" << processId() << '\n';
    }
    return true;
}

}  // namespace

void InkLog::write(LogLevel level, const std::string& moduleName, const std::string& message,
                   Kind kind, const std::source_location& site) {
    if (message.empty()) {
        return;
    }

    Impl& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);

    const auto now = std::chrono::system_clock::now();
    if (!st.initialized) {
        if (const char* levelText = std::getenv("INK_LOG_LEVEL")) {
            LogLevel parsed = st.threshold;
            if (parseLevel(levelText, parsed)) {
                st.threshold = parsed;
            }
        }
        if (!openFileLocked(st, now)) {
            return;
        }
        st.initialized = true;
    }

    // MARK 是定位锚点，不受阈值影响：把日志调安静以后也要能看到"我从这里开始"
    const bool forced = (kind == Kind::Mark);
    if (!forced && level < st.threshold) {
        return;
    }

    const bool foldable = !forced && level < LogLevel::Error;
    const std::string flat = flatten(message);
    const char* tag = forced ? kTagMark : levelTag(level);

    const long long second =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    if (second != st.rateSecond) {
        st.rateSecond = second;
        st.rate.clear();
    }

    const std::string key = std::string(tag) + '|' + moduleName + '|' +
                            std::to_string(std::hash<std::string>{}(flat));

    // 折叠：连续相同就不再落盘，只在结算时补一行 FOLD
    if (foldable && !st.pendingKey.empty() && key == st.pendingKey) {
        ++st.pendingCount;
        st.pendingLast = timeOfDay(now);
        return;
    }

    // 限流：同一个键每秒超过上限就只计数
    if (foldable) {
        int& bucket = st.rate[key];
        if (bucket >= kMaxRecordsPerSecondPerKey) {
            ++st.suppressed;
            if (key == st.pendingKey) {
                ++st.pendingSuppressed;
            }
            return;
        }
        ++bucket;
    }

    flushPendingLocked(st);

    if (st.currentDay != dateOf(now)) {
        // 跨零点继续写同一个文件：一次运行一个文件才好复盘
        st.currentDay = dateOf(now);
        writeLineLocked(st, "# ---- 跨日 " + st.currentDay + "，本次运行继续写入同一文件 ----");
    }

    const std::string location =
        fileNameOf(site.file_name()) + ':' + std::to_string(site.line());
    writeLineLocked(st, composeLine(tag, moduleName, ++st.sequence, location, flat));

    ++st.levelCounts[tag];
    ++st.moduleCounts[moduleName];

    if (foldable) {
        st.pendingKey = key;
        st.pendingModule = moduleName;
        st.pendingCount = 1;
        st.pendingSuppressed = 0;
        st.pendingFirst = timeOfDay(now);
        st.pendingLast = st.pendingFirst;
    }
}

void InkLog::info(const std::string& moduleName, const std::string& message,
                  const std::source_location& site) {
    write(LogLevel::Info, moduleName, message, Kind::Record, site);
}

void InkLog::pass(const std::string& moduleName, const std::string& message,
                  const std::source_location& site) {
    write(LogLevel::Pass, moduleName, message, Kind::Record, site);
}

void InkLog::warn(const std::string& moduleName, const std::string& message,
                  const std::source_location& site) {
    write(LogLevel::Warn, moduleName, message, Kind::Record, site);
}

void InkLog::error(const std::string& moduleName, const std::string& message,
                   const std::source_location& site) {
    write(LogLevel::Error, moduleName, message, Kind::Record, site);
}

void InkLog::debug(const std::string& moduleName, const std::string& message,
                   const std::source_location& site) {
    write(LogLevel::Debug, moduleName, message, Kind::Record, site);
}

void InkLog::mark(std::string_view text, const std::source_location& site) {
    write(LogLevel::Info, "InkLog", std::string(text), Kind::Mark, site);
}

std::filesystem::path InkLog::logFilePath() {
    Impl& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (!st.initialized) {
        const auto now = std::chrono::system_clock::now();
        if (openFileLocked(st, now)) {
            st.initialized = true;
        }
    }
    return st.path;
}

std::filesystem::path InkLog::logDirectory() {
    return logRootPath();
}

void InkLog::setLevel(LogLevel newLevel) {
    Impl& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.threshold = newLevel;
}

LogLevel InkLog::level() {
    Impl& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    return st.threshold;
}

bool InkLog::parseLevel(std::string_view text, LogLevel& result) {
    std::string lower;
    lower.reserve(text.size());
    for (const char item : text) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(item)));
    }

    if (lower == "debug") {
        result = LogLevel::Debug;
    } else if (lower == "info") {
        result = LogLevel::Info;
    } else if (lower == "pass") {
        result = LogLevel::Pass;
    } else if (lower == "warn" || lower == "warning") {
        result = LogLevel::Warn;
    } else if (lower == "error") {
        result = LogLevel::Error;
    } else {
        return false;
    }
    return true;
}

std::vector<std::string> InkLog::tail(std::size_t count) {
    Impl& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    flushPendingLocked(st);

    std::vector<std::string> result;
    const std::size_t begin = st.ring.size() > count ? st.ring.size() - count : 0;
    result.reserve(st.ring.size() - begin);
    for (std::size_t index = begin; index < st.ring.size(); ++index) {
        result.push_back(st.ring[index]);
    }
    return result;
}

std::vector<std::string> InkLog::grep(std::string_view pattern, std::string_view module) {
    Impl& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    flushPendingLocked(st);

    std::vector<std::string> result;
    const std::string needle(pattern);
    const std::string moduleNeedle =
        module.empty() ? std::string() : ("| " + std::string(module) + ':');
    for (const std::string& line : st.ring) {
        if (!moduleNeedle.empty() && line.find(moduleNeedle) == std::string::npos) {
            continue;
        }
        if (!needle.empty() && line.find(needle) == std::string::npos) {
            continue;
        }
        result.push_back(line);
    }
    return result;
}

std::string InkLog::statistics() {
    Impl& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    flushPendingLocked(st);

    std::ostringstream output;
    output << "日志统计\n"
           << "  文件      " << (st.path.empty() ? std::string("(未初始化)") : utf8Of(st.path))
           << '\n'
           << "  运行标识  " << (st.runId.empty() ? "-" : st.runId) << '\n'
           << "  阈值      " << levelTag(st.threshold) << '\n'
           << "  落盘条数  " << st.records << '\n'
           << "  折叠条数  " << st.folded << '\n'
           << "  限流丢弃  " << st.suppressed << '\n';

    output << "  按级别    ";
    bool firstLevel = true;
    for (const char* tag : {"DEBUG", "INFO ", "PASS ", "WARN ", "ERROR"}) {
        const auto iter = st.levelCounts.find(tag);
        if (iter == st.levelCounts.end()) {
            continue;
        }
        if (!firstLevel) {
            output << " / ";
        }
        output << tag << ' ' << iter->second;
        firstLevel = false;
    }
    if (firstLevel) {
        output << "无";
    }
    output << '\n';

    output << "  按模块    ";
    bool firstModule = true;
    for (const auto& entry : st.moduleCounts) {
        if (!firstModule) {
            output << " / ";
        }
        output << entry.first << ' ' << entry.second;
        firstModule = false;
    }
    if (firstModule) {
        output << "无";
    }
    return output.str();
}

}  // namespace inking

#endif  // INK_ISLOG
