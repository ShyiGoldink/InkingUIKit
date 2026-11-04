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

inline constexpr const char* InkingProjectName = "InkingFrontendDeveloper";
inline constexpr const char* InkingVersion     = "1.0.0";

/**
 * 版本阶段（**当前为预览版 `preview`**）。
 *
 * `1.0.0-preview` 与 `1.0.0` 的区别不在功能多少，而在**承诺**：
 * 预览版把 1.0 的功能面收齐、可以整包交付评估，但**公开面盘点还没做完**
 * （`docs/zh/plans/2026-10-29-evidence-and-delivery.md`「1.0.0 的 ABI 冻结」），
 * 所以接口 / ABI 仍可能在正式版前变，**不承诺**跨版本兼容；正式 `1.0.0`
 * 之后才冻结。要"稳"请对照 `docs/zh/CHANGELOG.md` 逐版升级，而不是假定
 * 二进制兼容。
 *
 * 它单独成一个常量而不是拼进 `InkingVersion`，是为了让日志 / 关于框 / 自检
 * 既能报"1.0.0"（版本号），又能在同一行标出"preview"（阶段）——
 * 拼起来之后想再拆开就得解析字符串。
 *
 * 打包脚本按"`release` 阶段省略后缀"生成目录名（`inking-1.0.0-win64`），
 * 其它阶段照旧拼进去（本版是 `inking-1.0.0-preview-win64`）。
 */
inline constexpr const char* InkingVersionStage = "preview";

/// 返回当前构建链接到的 SDL3 版本字符串，例如 "3.4.16"。
std::string InkingSdl3_version();


