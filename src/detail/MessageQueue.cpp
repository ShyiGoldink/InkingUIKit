#include <detail/MessageQueue.h>

#include <detail/InkLog.h>
#include <thread/TaskQueue.h>

#include <any>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

// 编译期闸门：与头文件里的那一个对应。关闭时本实现文件整体不进二进制
// （源 CMake 是"条件排除该 .cpp"，这里用 `#if` 兜一道：即使构建脚本漏配、
// 把本 TU 编进来了，也只剩一个空翻译单元，而不是"与空实现重定义"撞车）。
#if defined(INK_ISMESSAGE)

namespace {

constexpr const char* kModuleName = "MessageQueue";

}  // namespace

namespace inking {

std::mutex MessageQueue::_mutex;
std::condition_variable MessageQueue::_condition;
std::queue<Message> MessageQueue::_messageQueue = {};

void MessageQueue::addMessage(MessageType messageType,
                              float delayTime,
                              const std::string& message) {
    if (message.empty()) {
        return;
    }

    // 延迟消息不在这里等，而是交给任务队列：
    // 排一个 delay 之后才执行的任务，任务到点做的唯一一件事就是按"立即"重新入队。
    // 这样本类保持成一条笨队列，"等"的能力只需要任务队列有。
    if (delayTime > 0.0f) {
        const auto delay = std::chrono::milliseconds(
            static_cast<long long>(static_cast<double>(delayTime) * 1000.0));

        Task<std::any> task;
        task.action = [messageType, message](const std::vector<std::any>&) -> std::any {
            addMessage(messageType, 0.0f, message);
            return {};
        };
        TaskQueue::instance().addTask(std::move(task), delay);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _messageQueue.push(Message{messageType, delayTime, message});
    }
    _condition.notify_one();

    INK_LOG_DEBUG(kModuleName, "消息入队：" + message);
}

void MessageQueue::quickMessage(bool success,
                                float delayTime,
                                const std::string& message) {
    addMessage(success ? MessageType::Pass : MessageType::Error, delayTime, message);
}

std::vector<Message> MessageQueue::drainMessages() {
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Message> messages;
    while (!_messageQueue.empty()) {
        messages.push_back(_messageQueue.front());
        _messageQueue.pop();
    }
    return messages;
}

void MessageQueue::waitForMessage(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(_mutex);
    _condition.wait_for(lock, timeout, [] { return !_messageQueue.empty(); });
}

std::size_t MessageQueue::pendingCount() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _messageQueue.size();
}

}  // namespace inking

#endif  // INK_ISMESSAGE
