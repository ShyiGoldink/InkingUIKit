// `InkingApplyWindowConfig`：set + **回读校验**。
//
// 为什么不把校验留给调用方：调用方拿到的返回值本来就不可靠——
// `SetBorderless` / `SetRoundedCorners` / `SetFullscreen` / `SetTransparent`
// 返回的是"真的变了没有"，幂等调用返回 false，和失败无法区分；
// `SetBackgroundColor` 是 void。
// 唯一的可靠判据是**把值读回来比**，而这件事只有在这里做才只需要写一遍。

#include <window/WindowConfig.h>

#include <detail/InkLog.h>
#include <window/InkingWindow.h>

#include <string>


namespace {

constexpr const char* kModuleName = "InkingWindowConfig";

}  // namespace

int InkingApplyWindowConfig(const InkingWindowConfig& config) {
    InkingWindow& window = InkingWindow::Instance();
    int failed = 0;

    if (config.borderless.has_value()) {
        const bool wanted = *config.borderless;
        window.SetBorderless(wanted);
        if (window.IsBorderless() != wanted) {
            INK_LOG_ERROR(kModuleName,
                          std::string("window.borderless 要 ") + (wanted ? "true" : "false")
                              + "，窗口回读 "
                              + (window.IsBorderless() ? "true" : "false"));
            ++failed;
        }
    }

    if (config.roundedCorners.has_value()) {
        const bool wanted = *config.roundedCorners;
        window.SetRoundedCorners(wanted);
        // 比的是**请求值**：系统支不支持圆角是另一件事（Win10 上
        // `IsRoundedCornersApplied` 恒 false，拿它比会把"系统不支持"误报成"配置没生效"）。
        if (window.IsRoundedCornersRequested() != wanted) {
            INK_LOG_ERROR(kModuleName,
                          std::string("window.roundedCorners 要 ") + (wanted ? "true" : "false")
                              + "，窗口回读 "
                              + (window.IsRoundedCornersRequested() ? "true" : "false"));
            ++failed;
        } else if (wanted && !window.IsRoundedCornersApplied()) {
            // 这一条**不是失败**：请求记下了，只是系统画不了（Win10 / 非 Windows）。
            // 说清楚，免得用户以为配置漏了。
            INK_LOG_DEBUG(kModuleName,
                          "window.roundedCorners 已请求，但系统没有按圆角处理"
                          "（Win11 之前 / 非 Windows 属正常）");
        }
    }

    // 透明**必须排在 background 前面**：背景色的 alpha 判定看的就是这个值，
    // 顺序反了会把"透明窗口 + 半透明底"误报成"alpha 无效"。
    if (config.transparent.has_value()) {
        const bool wanted = *config.transparent;
        window.SetTransparent(wanted);
        // 比的是**请求值**：`IsTransparentApplied()` 说的是"标志进没进窗口"，
        // 而"桌面到底透没透"根本没有回读口；拿它比会把后端不支持误报成配置没生效。
        if (window.IsTransparentRequested() != wanted) {
            INK_LOG_ERROR(kModuleName,
                          std::string("window.transparent 要 ")
                              + (wanted ? "true" : "false")
                              + "，窗口回读 "
                              + (window.IsTransparentRequested() ? "true" : "false")
                              + "（窗口已打开时无法切换：透明是建窗标志）");
            ++failed;
        }
    }

    if (config.background.has_value()) {
        const std::uint32_t wanted = *config.background;
        window.SetBackgroundColor(wanted);
        if (window.GetBackgroundColor() != wanted) {
            INK_LOG_ERROR(kModuleName,
                          "window.background 没有生效（setter 是 noexcept，"
                          "不一致说明窗口层状态被别处覆盖了）");
            ++failed;
        }
    }

    // 背景板（letterbox 留白 / 窗口底图）：
    //   ① 颜色回读比；② 语义名回读比（`$token` 拼错时"颜色还是对的"这件事
    //   不会报错，只有名字对不上才说明主题这条路没接上）；③ 底图路径回读比。
    //   底图能不能解码**在这里不判**——那时可能还没有渲染器，而且"文件不在"
    //   属于运行期环境问题（打包漏了），绘制期会打 ERROR 并退回颜色。
    if (config.backdrop.has_value()) {
        const std::uint32_t wanted = *config.backdrop;
        const char* wantedToken =
            config.backdropToken.empty() ? nullptr : config.backdropToken.c_str();
        window.SetBackdrop(wanted, wantedToken);
        const char* readBack = window.GetBackdropToken();
        const bool tokenMatches =
            (wantedToken == nullptr && readBack == nullptr)
            || (wantedToken != nullptr && readBack != nullptr
                && config.backdropToken == readBack);
        if (window.GetBackdropColor() != wanted || !tokenMatches) {
            INK_LOG_ERROR(kModuleName,
                          "window.backdrop 没有生效（回读颜色 "
                              + std::to_string(window.GetBackdropColor())
                              + "，语义名 \""
                              + (readBack != nullptr ? readBack : "")
                              + "\"）");
            ++failed;
        }
    }

    if (config.backdropImage.has_value()) {
        const std::string& wanted = *config.backdropImage;
        if (wanted.empty()) {
            INK_LOG_ERROR(kModuleName,
                          "window.backdropImage 是空串：要关掉底图就别写这个字段"
                          "（空串和不写看起来一样、语义不同）");
            ++failed;
        } else {
            // setter 返回的是"值真的变了"，幂等调用为 false —— 所以判据只能是回读。
            window.SetBackdropImage(wanted);
            if (window.GetBackdropImage() != wanted) {
                INK_LOG_ERROR(kModuleName,
                              "window.backdropImage 没有生效：要 \"" + wanted
                                  + "\"，窗口回读 \""
                                  + window.GetBackdropImage() + "\"");
                ++failed;
            }
        }
    }

    if (config.fullscreen.has_value()) {
        const bool wanted = *config.fullscreen;
        window.SetFullscreen(wanted);
        if (window.IsFullscreen() != wanted) {
            INK_LOG_ERROR(kModuleName,
                          std::string("window.fullscreen 要 ") + (wanted ? "true" : "false")
                              + "，窗口回读 "
                              + (window.IsFullscreen() ? "true" : "false"));
            ++failed;
        }
    }

    return failed;
}

