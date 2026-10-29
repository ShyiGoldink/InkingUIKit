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
inline constexpr const char* kVersion     = "0.6.6";

/**
 * 版本阶段（**当前为发布版**）。
 *
 * "0.5.2-release" 与 "1.0.0" 的区别不在功能多少，而在**承诺**：
 * 0.x 发布版可以拿来用、拿来对齐，但接口 / ABI / 行为仍可能在次版本里变，
 * 不承诺跨版本兼容；1.0.0 之后才冻结。要"稳"请对照 `docs/zh/CHANGELOG.md`
 * 逐版升级，而不是假定二进制兼容。
 *
 * 它单独成一个常量而不是拼进 `kVersion`，是为了让日志 / 关于框 / 自检
 * 既能报"0.5.2"（版本号），又能在同一行标出"release"（阶段）——
 * 拼起来之后想再拆开就得解析字符串。
 *
 * 打包脚本按"`release` 阶段省略后缀"生成目录名（`inking-0.5.2-win64`），
 * 其它阶段照旧拼进去（如 `inking-0.1.0-test-win64`）。
 */
inline constexpr const char* kVersionStage = "release";

/// 返回当前构建链接到的 SDL3 版本字符串，例如 "3.4.16"。
std::string sdl3_version();

}  // namespace inking

