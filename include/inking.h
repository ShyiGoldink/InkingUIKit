#pragma once

// InkingFrontendDeveloper 的公共入口头文件。
// 引入这一个头文件即可拿到版本信息、日志、消息队列与任务队列。
//
// 注意它**不含组件**（Button / Scene / Window …）：组件头各自带自己的依赖，
// 全都塞进来会让"只是想用日志"的翻译单元被迫拖上一整套锚点树。
// 组件的入口分别是：
//   <button/InkingStaticButton.h>  按钮
//   <scene/InkingScene.h>          场景
//   <window/InkingWindow.h>        窗口

#include <string>

#include <detail/InkLog.h>
#include <detail/MessageQueue.h>
#include <thread/TaskQueue.h>

namespace inking {

inline constexpr const char* kProjectName = "InkingFrontendDeveloper";
inline constexpr const char* kVersion     = "0.1.0";

/**
 * 版本阶段（**当前为测试版本**）。
 *
 * "0.1.0-test" 与 "0.1.0" 的区别不在功能多少，而在**承诺**：
 * 测试版本期间接口、ABI、行为都可能在下一个版本里变，不保证兼容；
 * 消费方应当把它当"能跑、能对齐、但别上生产"的东西。
 *
 * 它单独成一个常量而不是拼进 `kVersion`，是为了让日志 / 关于框 / 自检
 * 既能报"0.1.0"（版本号），又能在同一行标出"test"（阶段）——
 * 拼起来之后想再拆开就得解析字符串。
 */
inline constexpr const char* kVersionStage = "test";

/// 返回当前构建链接到的 SDL3 版本字符串，例如 "3.4.16"。
std::string sdl3_version();

}  // namespace inking

