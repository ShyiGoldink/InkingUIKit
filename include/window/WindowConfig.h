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
//   InkingWindowConfig window;
//   window.borderless = true;          // 只写 json 里真的有的那几项
//   window.background = 0xFF1B1B24u;
//   if (InkingApplyWindowConfig(window) != 0) { /* 有字段没生效 */ }
//
// **时机**：必须在 `Show()` 之前。设计尺寸另有一条入口
// （`config/window_config.h` 的 `InkingSetDesignSize`），且设计尺寸要**更早**——
// 窗口第一次构造时就按它定型。生成的 `ApplyConfig()` 里顺序是写死的。

#include <cstdint>
#include <optional>
#include <string>


/**
 * @brief 启动时要应用的窗口设置。**没写的字段不动**（`std::optional` 空着即跳过）。
 *
 * 为什么用 optional 而不是"给每个字段一个和库一致的默认值"：那等于把库的默认值
 * 抄第二遍到生成器里，库哪天改默认值，生成器抄的那份就成了错的那份。
 * 空着 = 不碰 = 永远是库的默认值，只有一份真相。
 */
struct InkingWindowConfig {
    /// 无边框（自绘标题栏）。`Show()` 之前设 → 建窗直接带 `SDL_WINDOW_BORDERLESS`。
    std::optional<bool> borderless;

    /// 请求系统圆角（Win11 DWM；其它系统静默无效）。注意这是**请求值**，
    /// 真正的圆角由系统画，读回用 `IsRoundedCornersApplied()`。
    std::optional<bool> roundedCorners;

    /// 透明窗口：清屏色的 alpha 真的会透出桌面。**必须在 Show() 之前**——
    /// SDL 的透明是建窗标志，窗口开出来之后就切不了。
    std::optional<bool> transparent;

    /// 清屏色（`0xAARRGGBB`）。场景渲染之前那一笔窗底。
    /// alpha 只在 `transparent = true` 时有意义，否则会有一条 WARN。
    std::optional<std::uint32_t> background;

    /// 背景板颜色（`0xAARRGGBB`）：盖在清屏之上、场景之下，覆盖整幅窗口像素，
    /// **letterbox 留白填的就是它**。0 = 不画。`$token` 写法与 `background` 一致。
    std::optional<std::uint32_t> backdrop;

    /// 背景板颜色的**语义名**（`"$letterbox"` 去掉 `$` 之后那一串）。
    /// 空 = 这个颜色不是语义色；**只有写了 `backdrop` 且是 `$token` 时才有值**。
    std::string backdropToken;

    /// 背景板底图路径（cover 铺满整窗）。空 = 不画底图。
    std::optional<std::string> backdropImage;

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
 * `InkingSetDesignSize()` 之后：窗口根锚点按设计尺寸定型，顺序反了设计尺寸会被拒。
 */
int InkingApplyWindowConfig(const InkingWindowConfig& config);

