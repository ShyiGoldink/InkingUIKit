#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <dataStruct/MessageStruct.h>

// 编译期开关（CMake 选项 ISMESSAGE → 宏 INK_ISMESSAGE）：
// 关闭时 MessageQueue 退化成空实现，实现文件不参与编译，队列本体
// 和它依赖的存储都不进二进制；调用侧一律走文件末尾的 INK_MESSAGE_* 宏。
#if defined(INK_ISMESSAGE)

namespace inking {

/**
 * @brief UI 消息队列，结构移植自后端的 UIMessageLibrary。
 *
 * UI 线程专门负责画，那么这个静态类专门负责攒消息。它是纯队列，
 * 不认识时间：延迟消息交给 TaskQueue 排一个延迟任务，到点再按"立即"入队。
 */
class MessageQueue {
public:
    /**
     * 添加一条消息。
     * @param messageType 消息类型。
     * @param delayTime   延迟多少秒之后才输出，0 表示立即。
     * @param message     消息内容；空字符串会被忽略。
     */
    static void addMessage(MessageType messageType,
                           float delayTime,
                           const std::string& message);

    /** 快捷添加：按 bool 决定消息是通过还是错误。 */
    static void quickMessage(bool success,
                             float delayTime,
                             const std::string& message);

    /** 取出当前所有待处理消息，供 UI 线程统一重绘/输出。 */
    static std::vector<Message> drainMessages();

    /** 等待消息到达，供 UI 线程阻塞等待新事件。 */
    static void waitForMessage(std::chrono::milliseconds timeout);

    /** 当前队列里有多少条待处理消息，主要给自检用。 */
    static std::size_t pendingCount();

private:
    MessageQueue() = delete;
    ~MessageQueue() = delete;

    static std::mutex _mutex;                  /** 保护消息队列 */
    static std::condition_variable _condition; /** 通知 UI 线程有新消息 */
    static std::queue<Message> _messageQueue;  /** 待处理消息 */
};

}  // namespace inking

#else  // !INK_ISMESSAGE

/// 消息队列关闭时的空实现：即使有人绕过宏直接调用，也不会出现链接错误。
namespace inking {

class MessageQueue {
public:
    static void addMessage(MessageType, float, const std::string&) noexcept {}
    static void quickMessage(bool, float, const std::string&) noexcept {}
    static std::vector<Message> drainMessages() { return {}; }
    static void waitForMessage(std::chrono::milliseconds) noexcept {}
    static std::size_t pendingCount() noexcept { return 0; }
};

}  // namespace inking

#endif  // INK_ISMESSAGE

namespace inking {

/** 消息队列是否在编译期启用。 */
#if defined(INK_ISMESSAGE)
inline constexpr bool kMessageEnabled = true;
#else
inline constexpr bool kMessageEnabled = false;
#endif

}  // namespace inking

// ---------------------------------------------------------------------------
// 调用宏：消息队列关闭时整条语句消失，参数不会求值。
// 直接调类方法（MessageQueue::addMessage）在关闭时参数照样会求值，
// 字符串拼接和 to_string 的开销一点都省不下来。
// ---------------------------------------------------------------------------
#if defined(INK_ISMESSAGE)
#define INK_MESSAGE(messageType, message) \
    ::inking::MessageQueue::addMessage((messageType), 0.0f, (message))
#define INK_MESSAGE_AFTER(messageType, seconds, message) \
    ::inking::MessageQueue::addMessage((messageType), (seconds), (message))
#define INK_MESSAGE_PASS(message) \
    ::inking::MessageQueue::addMessage(::inking::MessageType::Pass, 0.0f, (message))
#define INK_MESSAGE_ERROR(message) \
    ::inking::MessageQueue::addMessage(::inking::MessageType::Error, 0.0f, (message))
#else
#define INK_MESSAGE(messageType, message)          ((void)0)
#define INK_MESSAGE_AFTER(messageType, seconds, message) ((void)0)
#define INK_MESSAGE_PASS(message)                  ((void)0)
#define INK_MESSAGE_ERROR(message)                 ((void)0)
#endif
