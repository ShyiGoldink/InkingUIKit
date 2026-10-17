#pragma once

#include <string>

namespace inking {

/**
 * @brief 消息类型，决定消息在 UI 上的呈现方式。
 *
 * normal/error/pass 与 InkingBackendFramework 保持一致，
 * warn 是前端额外补的一档，用于"能跑但不该这样"的提示。
 */
enum class MessageType {
    Normal = 0,
    Error  = 1,
    Pass   = 2,
    Warn   = 3
};

/**
 * @brief 一条待 UI 处理的消息。
 *
 * delayTime 表示"延迟多少秒之后才输出"，0 表示立即。
 * 延迟本身不在这里等：延迟消息会变成任务队列里的一个延迟任务，
 * 到点后把它按"立即"重新入队，详见 MessageQueue::addMessage。
 */
struct Message {
    MessageType type = MessageType::Normal;
    float       delayTime = 0.0f;
    std::string message;
};

}  // namespace inking
