#pragma once

// 窗口的**启动配置**：一次把"长什么样"交给窗口，并且**回读校验**。
//
// 为什么不是"业务自己连调四个 setter"：那四对 setter / getter 谁都能调，但
// "调了没生效"是一个**静默**失败——`SetBorderless` 的返回值是"真的变了没有"，
// 幂等调用返回 false，跟失败长得一模一样；`SetBackgroundColor` 干脆是 void。
// 于是"配置写了、窗口没变"只能靠人眼看边框。这里把 set + 回读校验收在一处，
// 让它在**启动第一秒**变成一条 ERROR 日志 + 一个非 0 返回值。
//
// 用法（生成的 `ApplyConfig()` 就是这么写的）：
//
//   inking::WindowConfig window;
//   window.borderless = true;          // 只写 json 里真的有的那几项
//   window.background = 0xFF1B1B24u;
//   if (inking::ApplyWindowConfig(window) != 0) { /* 有字段没生效 */ }
//
// **时机**：必须在 `Show()` 之前。设计尺寸另有一条入口
// （`config/window_config.h` 的 `SetDesignSize`），且设计尺寸要**更早**——
// 窗口第一次构造时就按它定型。生成的 `ApplyConfig()` 里顺序是写死的。

#include <cstdint>
#include <optional>

namespace inking {

/**
 * @brief 启动时要应用的窗口设置。**没写的字段不动**（`std::optional` 空着即跳过）。
 *
 * 为什么用 optional 而不是"给每个字段一个和库一致的默认值"：那等于把库的默认值
 * 抄第二遍到生成器里，库哪天改默认值，生成器抄的那份就成了错的那份。
 * 空着 = 不碰 = 永远是库的默认值，只有一份真相。
 */
struct WindowConfig {
    /// 无边框（自绘标题栏）。`Show()` 之前设 → 建窗直接带 `SDL_WINDOW_BORDERLESS`。
    std::optional<bool> borderless;

    /// 请求系统圆角（Win11 DWM；其它系统静默无效）。注意这是**请求值**，
    /// 真正的圆角由系统画，读回用 `IsRoundedCornersApplied()`。
    std::optional<bool> roundedCorners;

    /// 清屏色（`0xAARRGGBB`）。场景渲染之前那一笔窗底。
    std::optional<std::uint32_t> background;

    /// 启动即全屏。`Show()` 之前只记账，建完窗口补应用一次。
    std::optional<bool> fullscreen;
};

/**
 * @brief 应用上面的设置，并**逐项回读校验**。
 *
 * @return 没生效的字段数（0 = 全部生效）。每一项失败都会打一条 ERROR 日志，
 *         把"json 写的"和"窗口实际的"一起说出来。
 *
 * 会**构造窗口单例**（`InkingWindow::Instance()`）——所以必须排在
 * `SetDesignSize()` 之后：窗口根锚点按设计尺寸定型，顺序反了设计尺寸会被拒。
 */
int ApplyWindowConfig(const WindowConfig& config);

}  // namespace inking
