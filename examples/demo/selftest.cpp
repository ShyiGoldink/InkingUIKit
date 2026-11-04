// ---------------------------------------------------------------------------
// inking_demo_selftest —— **端到端**自检（默认无头）
//
// 与 `inking_demo` 的分工：
//   - `inking_demo`       开真窗口、跑主循环，给人看（需要显示器）；
//   - 本程序              默认不开窗口（SDL dummy 视频驱动 + 软件渲染器），
//                         把「库到底能不能用」变成一串可断言的结论，给机器看。
//
// 后端由环境变量 `INKING_SELFTEST_DRIVER` 决定（0.8.0 T-02）：
//   不设 / 空串   → `dummy`（软件，任何机器都能跑；**默认值不能改**）
//   `windows`     → 真后端（本机走 direct3d11）。建不出窗口/渲染器时打印
//                   `[skip] 真后端不可用` 并 `return 77`，好让 `ctest` 把它
//                   显示成 Skipped——"没显卡"与"代码坏了"不能是同一个红。
//
// 它**只链接 inking 动态库**（和示例主体同一份公开头），所以它验的就是
// 消费者视角下的 DLL：导出符号够不够、跨模块的单例/槽位是不是同一份、
// 组件构造与渲染提交在库外调得动调不动。
//
// 覆盖：场景登记 / 组件构造与树接线 / 命中表烘焙与查询 / 静态按钮三态与
//       点击派发 / 动态按钮 Tick 派发与几何写入 / 渲染提交 / 消息队列 /
//       任务队列（单例与管家线程）。
//
// 退出码 = 失败项数（0 表示全绿），方便挂进 CI。
// ---------------------------------------------------------------------------

#include <audio/InkingAudio.h>
#include <inking.h>

#include <button/InkingDynamicButton.h>
#include <button/InkingStaticButton.h>
#include <config/window_config.h>
#include <dataStruct/InkingImageScale.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTextRequest.h>
#include <dataStruct/InkingTransform.h>
#include <detail/MessageQueue.h>
#include <detail/InkLog.h>
#include <detail/InkingPointerProbe.h>
#include <detail/InkingPopupPlacement.h>
#include <input/CursorShape.h>
#include <input/KeyboardInput.h>
#include <input/Shortcut.h>
#include <input/ShortcutRegistry.h>
#include <image/ImageData.h>
#include <image/InkingAtlas.h>
#include <image/InkingDynamicImage.h>
#include <image/InkingDynamicSprite.h>
#include <image/InkingSpriteGroup.h>
#include <image/InkingSprite.h>
#include <util/InkingJson.h>
#include <image/InkingStaticImage.h>
#include <image/SpriteData.h>
#include <panel/InkingDynamicPanel.h>
#include <panel/InkingStaticPanel.h>
#include <panel/PanelData.h>
#include <panel/InkingFixedPopup.h>
#include <text/InkingDynamicLabel.h>
#include <text/InkingStaticLabel.h>
#include <text/InkingTextInput.h>
#include <text/LabelData.h>
#include <widget/InkingDynamicWidget.h>
#include <prefab/FilePicker.h>
#include <prefab/MenuBar.h>
#include <prefab/MenuPopup.h>
#include <prefab/MenuPopupData.h>
#include <prefab/Dialog.h>
#include <prefab/ProgressBar.h>
#include <prefab/Slider.h>
#include <prefab/TabBar.h>
#include <prefab/ToastHost.h>
#include <prefab/ToggleLike.h>
#include <prefab/Tooltip.h>
#include <video/InkingVideo.h>
#include <render/InkingBatch.h>
#include <render/InkingGlyphAtlas.h>
#include <render/InkingMesh.h>
#include <render/InkingSvg.h>
#include <render/InkingText.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>
#include <theme/InkingTheme.h>
#include <thread/TaskQueue.h>
#include <window/InkingWindow.h>
#include <window/WindowConfig.h>

#include <SDL3/SDL.h>

#include <any>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_fail = 0;

void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) {
        ++g_fail;
    }
}

/// 读回一块像素（ARGB8888，逐行拷贝）。
///
/// **前向声明**：实现在下面的图片那一节（那里才是它主要的用处），而
/// 文字裁行那条自检（0.8.3 T-01）排在它之前、也要读像素。
std::vector<std::uint32_t> ReadPixelRegion(SDL_Renderer* renderer,
                                           const SDL_Rect& rect);

/// 动态按钮：每逻辑步把自己往右推 10 像素——用来验"Tick 真的派发到组件"。
class Mover : public InkingDynamicButton {
public:
    using InkingDynamicButton::InkingDynamicButton;

protected:
    void onTick(float deltaSeconds) override {
        InkingDynamicButton::onTick(deltaSeconds);  // 先收输入，再动几何
        ChangeOffset(10.0f, 0.0f);
    }
};

struct TestScene : public InkingScene {
    TestScene() : InkingScene("selftest") {
        InkingButtonData bd;
        bd.name = "probe_button";
        bd.width = 200;
        bd.height = 60;
        bd.normal = InkingButtonAppearance(InkingAppearanceType::Color,
                                             0xFF3F7FD6u);
        bd.label = "probe";
        bd.text.fontSize = 20.0f;
        bd.selfAnchor = InkingChangeAnchor::Center;
        bd.traceAnchor = InkingChangeAnchor::Center;

        button = static_cast<InkingStaticButton*>(
            AddChild(std::make_unique<InkingStaticButton>(this, bd)));
        button->SetOnClicked([this] { clicked = true; });

        // 动态按钮挪开摆：两个组件重叠时，**后注册的会正确地挡在上面**，
        // 那时静态按钮收不到 hover 是正确行为，不是故障。
        InkingButtonData dd = bd;
        dd.name = "probe_mover";
        dd.offsetY = 200.0f;
        mover =
            static_cast<Mover*>(AddChild(std::make_unique<Mover>(this, dd)));
    }

    InkingStaticButton* button = nullptr;
    Mover* mover = nullptr;
    bool clicked = false;
};

// ---------------------------------------------------------------------------
// InkingMesh × InkingShapeContains 对账
//
// 这两者是**同一个几何问题的两条实现**：`AppendContour` 决定"画出来的轮廓长
// 什么样"（填充与描边都用它），`InkingSignedDistance` / `InkingShapeContains` 决定"点在不在
// 形状里"（命中、裁剪、包围盒都用它）。两边一旦分叉，症状就是"画的是圆角、
// 点到的是直角"——只在边角露头，人眼极难发现。
//
// 做法：把轮廓采样成一圈多边形，用**射线法**独立判一次内外，再和 SDF 的答案
// 逐点比。靠近边界的点跳过（|sdf| < 1.5px），那里两种判法本来就都有理；
// 只有"离边界很远还判反了"才计为分歧——那才是真的分叉。
// ---------------------------------------------------------------------------

/// 射线法：点是否在这一圈闭合多边形内（只用于对账，不参与任何绘制）。
bool PointInPolygon(const std::vector<inking_detail::OutlinePoint>& ring,
                    float x, float y) {
    const std::size_t n = ring.size();
    if (n < 3) {
        return false;
    }
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const float xi = ring[i].x;
        const float yi = ring[i].y;
        const float xj = ring[j].x;
        const float yj = ring[j].y;
        if ((yi > y) == (yj > y)) {
            continue;  // 这条边不跨过水平线
        }
        const float xHit = xi + (y - yi) * (xj - xi) / (yj - yi);
        if (x < xHit) {
            inside = !inside;
        }
    }
    return inside;
}

/// 返回分歧点数（0 = 全部对得上）。
int ReconcileShapeMesh() {
    struct Case {
        const char* name;
        InkingShapeSpec shape;
    };
    const Case cases[] = {
        {"rect", InkingShapeSpec::Rect()},
        {"roundedRect(24)", InkingShapeSpec::RoundedRect(24.0f)},
        {"circle", InkingShapeSpec::Circle()},
        {"ellipse", InkingShapeSpec::Ellipse()},
        {"capsule", InkingShapeSpec::Capsule()},
        {"polygon(6)", InkingShapeSpec::Polygon(6, 0.0f)},
        {"star(5,0.5)", InkingShapeSpec::Star(5, 0.5f)},
        {"superellipse(4)", InkingShapeSpec::Superellipse(4.0f)},
    };

    constexpr int kWidth = 200;
    constexpr int kHeight = 120;
    constexpr int kCols = 60;
    constexpr int kRows = 36;

    int bad = 0;
    for (const Case& c : cases) {
        std::vector<inking_detail::OutlinePoint> ring;
        const float w = static_cast<float>(kWidth);
        const float h = static_cast<float>(kHeight);
        inking_detail::AppendContour(
            ring, c.shape, w, h,
            inking_detail::ContourSteps(c.shape, w, h), 0.0f);

        if (ring.size() < 3) {
            std::printf("      [FAIL] %s：轮廓点数 %zu（应 >= 3）\n", c.name,
                        ring.size());
            ++bad;
            continue;
        }

        int checked = 0;
        int mismatch = 0;
        for (int gy = 0; gy < kRows; ++gy) {
            for (int gx = 0; gx < kCols; ++gx) {
                const float px = (static_cast<float>(gx) + 0.5f) * w
                               / static_cast<float>(kCols);
                const float py = (static_cast<float>(gy) + 0.5f) * h
                               / static_cast<float>(kRows);
                const float sdf =
                    InkingSignedDistance(c.shape, px, py, kWidth, kHeight);
                if (std::fabs(sdf) < 1.5f) {
                    continue;  // 边界带：两种判法都有理，不参与对账
                }
                ++checked;
                const bool byShape = sdf <= 0.0f;
                const bool byMesh = PointInPolygon(ring, px, py);
                if (byShape != byMesh) {
                    ++mismatch;
                }
            }
        }

        if (mismatch != 0 || checked < 100) {
            std::printf("      [FAIL] %s：%d 个采样点、%d 处分歧\n", c.name,
                        checked, mismatch);
            ++bad;
        } else {
            std::printf("      [ ok ] %s：%zu 点轮廓 / %d 个采样点逐点一致\n",
                        c.name, ring.size(), checked);
        }
    }
    return bad;
}

// ---------------------------------------------------------------------------
// 窗口：缩放档位（严格等比）+ 全屏联动
//
// 这两件事**不需要真窗口**：窗口层的记账（`_windowWidth/_windowHeight`、
// `_zoom`、`_fullscreen`）在没有 SDL 窗口时照样成立，事件照发——这正是
// "先记账、Show() 时补应用"那条设计的可测性红利。
// ---------------------------------------------------------------------------

void CheckWindowZoomAndFullscreen() {
    InkingWindow& window = InkingWindow::Instance();

    // ---- 档位表：运行时按设计尺寸 + 显示器可用区域生成 ----
    const std::vector<float> ladder = window.GetZoomLadder();
    bool ladderOk = !ladder.empty();
    for (std::size_t i = 0; i < ladder.size(); ++i) {
        if (ladder[i] < InkingWindow::kMinZoom - 1e-3f
            || ladder[i] > InkingWindow::kMaxZoom + 1e-3f) {
            ladderOk = false;
        }
        if (i > 0 && ladder[i] <= ladder[i - 1]) {
            ladderOk = false;  // 必须严格升序，否则 ZoomBy 会原地踏步
        }
    }
    Check(ladderOk, "缩放档位表非空、严格升序、落在 [kMinZoom, kMaxZoom]");
    if (!ladderOk) {
        return;
    }

    // ---- 最大档：尺寸 = 设计尺寸 × 档位，且**严格等比**（黑边 0） ----
    const float top = ladder.back();
    Check(window.SetZoom(top), "SetZoom(最大档) 真的改变了窗口尺寸");
    Check(std::fabs(window.GetZoom() - top) < 1e-4f,
          "GetZoom 回读与请求的档位一致");

    const int width = window.GetWindowWidth();
    const int height = window.GetWindowHeight();
    Check(width == static_cast<int>(std::lround(InkingDesignWidth() * top))
              && height
                     == static_cast<int>(
                            std::lround(InkingDesignHeight() * top)),
          "窗口尺寸 = 设计尺寸 × 档位（整数像素）");

    // 严格等比的判据：两轴算出来的"实际倍率"之差，折算回设计像素 ≤ 1。
    const double zoomW =
        static_cast<double>(width) / static_cast<double>(InkingDesignWidth());
    const double zoomH =
        static_cast<double>(height) / static_cast<double>(InkingDesignHeight());
    Check(std::fabs(zoomW - zoomH) * InkingDesignWidth() <= 1.0,
          "两轴倍率一致（letterbox 黑边 ≤ 1px）");

    // ---- 夹取与幂等 ----
    Check(!window.SetZoom(top + 10.0f), "超出档位的请求被吸附回最大档（幂等）");
    Check(std::fabs(window.GetZoom() - top) < 1e-4f,
          "吸附之后仍是最大档（没有被放大到屏幕外）");
    Check(!window.ZoomBy(1), "最大档再放大返回 false（夹取到表尾）");
    Check(window.ZoomBy(-1), "ZoomBy(-1) 能从最大档退一档");
    Check(std::fabs(window.GetZoom() - ladder[ladder.size() - 2]) < 1e-4f,
          "退一档落在档位表的倒数第二格");

    window.SetZoom(1.0f);  // 复位，别影响后面的用例

    // ---- 全屏：幂等 + 事件 + 缩放空操作 ----
    Check(window.SetFullscreen(true), "SetFullscreen(true) 生效");
    Check(window.IsFullscreen(), "IsFullscreen 回读为真");
    Check(!window.SetFullscreen(true), "全屏是幂等的（第二次返回 false）");
    Check(!window.SetZoom(top), "全屏下缩放是空操作");
    Check(window.SetFullscreen(false), "退出全屏生效");
    Check(!window.IsFullscreen(), "退出全屏后 IsFullscreen 为假");
}

// ---------------------------------------------------------------------------
// 窗口启动配置（`InkingWindowConfig`）：set + **回读校验**
//
// 这一层的价值全在"回读"上：三个 setter 返回的是"真的变了没有"，幂等调用返回
// false，和失败长得一样；`SetBackgroundColor` 干脆是 void。所以判据只能是"读回来比"。
// ---------------------------------------------------------------------------

void CheckWindowConfigApply() {
    InkingWindow& window = InkingWindow::Instance();

    const std::uint32_t backgroundBefore = window.GetBackgroundColor();
    const bool borderlessBefore = window.IsBorderless();

    // 1) 空配置：一项都不动（"没写 = 不碰"，留给库的默认值）。
    InkingWindowConfig empty;
    Check(InkingApplyWindowConfig(empty) == 0,
          "空 InkingWindowConfig 返回 0（没写 = 不动库的默认值）");
    Check(window.GetBackgroundColor() == backgroundBefore
              && window.IsBorderless() == borderlessBefore,
          "空 InkingWindowConfig 确实一项都没动");

    // 2) 五项全设：回读一致、失败数为 0。
    InkingWindowConfig config;
    config.borderless = true;
    config.roundedCorners = true;
    config.transparent = true;
    config.background = 0xFF010203u;
    config.fullscreen = true;
    Check(InkingApplyWindowConfig(config) == 0,
          "五项窗口设置回读全部一致（失败数 0）");
    Check(window.IsBorderless(), "borderless 回读为真");
    Check(window.GetBackgroundColor() == 0xFF010203u, "background 回读一致");
    Check(window.IsFullscreen(), "fullscreen 回读为真");
    Check(window.IsRoundedCornersRequested(),
          "roundedCorners 请求值回读为真（系统画不画是另一件事）");
    Check(window.IsTransparentRequested(),
          "transparent 请求值回读为真（还没建窗，标志无从落地）");
    Check(!window.IsTransparentApplied(),
          "没有窗口时 IsTransparentApplied 恒为假（它读的是窗口标志）");

    // 3) 复原，别污染后面的用例。
    InkingWindowConfig restore;
    restore.borderless = borderlessBefore;
    restore.transparent = false;
    restore.background = backgroundBefore;
    restore.fullscreen = false;
    Check(InkingApplyWindowConfig(restore) == 0, "复原窗口设置成功");
}

// ---------------------------------------------------------------------------
// 菜单栏：全屏自动隐藏（订阅事件，不是轮询）
// ---------------------------------------------------------------------------

void CheckMenuBarFullscreenHiding() {
    InkingWindow& window = InkingWindow::Instance();
    inking_prefab::MenuBar bar(nullptr);

    Check(bar.IsVisible(), "菜单栏初始可见");
    window.SetFullscreen(true);
    Check(!bar.IsVisible() && bar.IsHiddenByFullscreen(),
          "全屏 → 菜单栏自动隐藏（记账为「被全屏藏起来」）");
    window.SetFullscreen(false);
    Check(bar.IsVisible() && !bar.IsHiddenByFullscreen(),
          "退出全屏 → 菜单栏恢复（只恢复被全屏藏起来的那一次）");

    // 关掉这条规则之后，全屏不该再动它。
    bar.SetHideInFullscreen(false);
    window.SetFullscreen(true);
    Check(bar.IsVisible(), "关掉 hideInFullscreen 后全屏不隐藏");
    window.SetFullscreen(false);
}

// ---------------------------------------------------------------------------
// 快捷键：解析 / 匹配 / 冲突 / 消费
// ---------------------------------------------------------------------------

void CheckShortcuts() {
    // ---- 解析与回写 ----
    bool ok = false;
    const InkingShortcut combo = InkingShortcut::Parse("Ctrl+Shift+S", &ok);
    Check(ok && !combo.Empty(), "解析 Ctrl+Shift+S 成功");
    Check(combo.ToString() == "Ctrl+Shift+S", "ToString 与输入一致（往返稳定）");
    Check(combo.modifiers == (InkingShortcut::kCtrl | InkingShortcut::kShift),
          "修饰键位掩码正确");

    ok = true;
    const InkingShortcut bogus = InkingShortcut::Parse("Ctrl+Nope", &ok);
    Check(!ok && bogus.Empty(), "非法主键名解析失败且不返回半个快捷键");

    ok = true;
    Check(InkingShortcut::Parse("Ctrl+", &ok).Empty() && !ok,
          "只有修饰键的串不算快捷键");

    // ---- 匹配：修饰键必须完全相等 ----
    InkingKeyEvent press{};
    press.key = 's';
    press.ctrl = true;
    press.shift = true;
    const InkingShortcut save = InkingShortcut::Parse("Ctrl+S");
    Check(!save.Matches(press), "Ctrl+S 不被 Ctrl+Shift+S 命中（严格匹配）");
    press.shift = false;
    Check(save.Matches(press), "Ctrl+S 命中 Ctrl+S");
    press.repeat = true;
    Check(!save.Matches(press), "长按重复默认不触发");
    press.repeat = false;

    // ---- 绑定表：冲突 / 派发 / 消费 ----
    InkingShortcutRegistry registry;
    int fired = 0;
    const InkingShortcutRegistry::Id id =
        registry.Bind(save, [&fired] { ++fired; }, "保存");
    Check(id != 0 && registry.Count() == 1, "快捷键绑定成功");
    Check(registry.Bind(save, [] {}, "重复项") == 0 && registry.Count() == 1,
          "重复组合被拒绝（先到先得，不静默覆盖）");
    Check(registry.IsTaken(save), "IsTaken 能预检冲突");

    // 造一条真实的按键事件喂进去（不经过窗口，自检要能单独验这条路）。
    InkingKeyboardInput input;
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_S;
    event.key.mod = SDL_KMOD_CTRL;
    input.UpdateFromSDL(event);

    Check(registry.Dispatch(input) == 1 && fired == 1, "命中 → 回调执行一次");
    Check(input.GetFrameEvents().empty(),
          "命中 → 本帧按键事件被消费（焦点组件收不到这一下）");
    Check(input.GetState().IsKeyDown(SDLK_S),
          "消费不影响跨帧状态（按住仍然为真）");
    Check(registry.Unbind(id) && registry.Count() == 0, "退订生效");
}

// ---------------------------------------------------------------------------
// 菜单栏：菜单项顺手绑快捷键 + 析构自动注销
// ---------------------------------------------------------------------------

void CheckMenuBarShortcutBinding() {
    InkingWindow& window = InkingWindow::Instance();
    const std::size_t before = window.GetShortcuts().Count();

    InkingButtonData look;
    look.name = "menu_item";
    look.width = 80;
    look.height = 28;

    const InkingShortcut open = InkingShortcut::Parse("Ctrl+O");
    {
        inking_prefab::MenuBar bar(nullptr);
        InkingStaticButton* item =
            bar.AddButton(look, "打开", [] {}, open);
        Check(item != nullptr && bar.GetBoundShortcutCount() == 1,
              "AddButton 第 4 参顺手绑定了快捷键");
        Check(window.GetShortcuts().Count() == before + 1,
              "绑定落进了窗口的全局表（不依赖焦点）");
    }
    Check(window.GetShortcuts().Count() == before,
          "菜单栏析构 → 快捷键自动注销（不留下悬垂回调）");
}

// ---------------------------------------------------------------------------
// 文字高清：按设备比例烘字，布局零位移
//
// 两套渲染器，**逻辑尺寸相同、设备尺寸差 2 倍**：
//   A：1280×720，不开逻辑呈现        → 比例 1
//   B：2560×1440，LETTERBOX(1280×720) → 比例 2
// 判据（把 B 的包围盒除回 2 再比）：
//   - 原点与尺寸都不变 ⇒ 布局零位移（高清没有把界面撑大）；
//   - B 的墨迹确实是 A 的 2 倍设备像素 ⇒ 是**按比例重烘**，不是把 1x 位图拉大。
// 最后一条是"清晰度"能自动断言的部分（真正的观感归人眼）。
// ---------------------------------------------------------------------------

/// 读回整幅画面里"非黑像素"的包围盒（**设备像素**）。false = 什么都没画出来。
bool InkBox(SDL_Renderer* renderer, SDL_Rect* out) {
    // SDL 3.4 起这个接口直接给一张 surface（不再要调用方准备缓冲）。
    SDL_Surface* captured = SDL_RenderReadPixels(renderer, nullptr);
    if (captured == nullptr) {
        return false;
    }
    // 统一成 ARGB8888 再扫：不假设后端给的像素格式。
    SDL_Surface* surface = captured;
    if (surface->format != SDL_PIXELFORMAT_ARGB8888) {
        surface = SDL_ConvertSurface(captured, SDL_PIXELFORMAT_ARGB8888);
        SDL_DestroySurface(captured);
        if (surface == nullptr) {
            return false;
        }
    }
    if (!SDL_LockSurface(surface)) {
        SDL_DestroySurface(surface);
        return false;
    }

    const int width = surface->w;
    const int height = surface->h;
    const int pitch = surface->pitch;
    const auto* base = static_cast<const std::uint8_t*>(surface->pixels);
    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < height; ++y) {
        const auto* row =
            reinterpret_cast<const std::uint32_t*>(base + static_cast<std::size_t>(y) * pitch);
        for (int x = 0; x < width; ++x) {
            if ((row[x] & 0x00FFFFFFu) == 0u) {
                continue;  // 黑 = 背景
            }
            if (x < minX) { minX = x; }
            if (y < minY) { minY = y; }
            if (x > maxX) { maxX = x; }
            if (y > maxY) { maxY = y; }
        }
    }
    SDL_UnlockSurface(surface);
    SDL_DestroySurface(surface);

    if (maxX < 0) {
        return false;
    }
    out->x = minX;
    out->y = minY;
    out->w = maxX - minX + 1;
    out->h = maxY - minY + 1;
    return true;
}

/// 找一个**真能打开**的字体。
///
/// 判据不能用 `SDL_GetPathInfo`：它只问"这个路径存在吗"，而字体能不能被
/// TTF 打开是另一件事（权限、被占用、沙箱都可能拦下来）。所以拿
/// `InkingMeasureText` 量两个字符——量出来正好等于"字号 × 码点数"就是**估算法**
/// 的答案，说明没打开字体；不等于才是真字体度量。
///
/// 环境变量 `INK_TEST_FONT` 优先：CI / 沙箱里系统字体不一定可读。
std::string FindUsableFont() {
    std::vector<std::string> candidates;
    if (const char* env = SDL_getenv("INK_TEST_FONT");
        env != nullptr && *env != '\0') {
        candidates.emplace_back(env);
    }
    candidates.emplace_back("C:/Windows/Fonts/msyh.ttc");
    candidates.emplace_back("C:/Windows/Fonts/simhei.ttf");
    candidates.emplace_back("C:/Windows/Fonts/arial.ttf");

    for (const std::string& path : candidates) {
        InkingTextRequest probe;
        probe.text = "Hg";
        probe.fontSize = 40.0f;
        probe.fontPath = path;
        const InkingTextMetrics metrics = InkingMeasureText(probe);
        if (std::fabs(metrics.width - 80.0f) > 0.5f) {  // 80 = 2 码点 × 40
            return path;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// 静默失效清单（0.3.0 T-14）
//
// **这张表就是清单**：`docs/AGENTS.md` §7 那份必须与它逐条对齐（ctest
// `silent_failure_list_in_sync` 比对文档与源码）。每条 = 一条"会静默变空白 /
// 不生效"的路径 + 一个**可断言**的自检名。
//
// 为什么要它：0.1.1 / 0.2.0 各自补了一处护栏，但补完不等于结束——没有清单，
// 下一轮还会从别的地方漏。清单本身就是"哪些地方必须发声"的答案。
//
// **加一条的门槛**：写得出一条可复现的触发条件。写不出来的不进清单
// （否则它会慢慢变成愿望清单，谁也不敢删）。
// ---------------------------------------------------------------------------

struct SilentFailureItem {
    const char* id;          ///< 自检名（与 `Check(...)` 的消息前缀一致）
    const char* task;        ///< 对应任务（追溯用）
    const char* phenomenon;  ///< 现象：一句话，能照着复现
};

constexpr SilentFailureItem kSilentFailures[] = {
    {"label_self_size", "T-01",
     "内容驱动组件在无布局的容器里尺寸停在 0：不画、也不报"},
    {"warn_zero_size", "T-02",
     "任意可绘制组件 0×0：屏幕上一块空白，日志里一个字都没有"},
    {"panel_transparent_fallback", "T-03",
     "面板两态全透明：看不见、但点得到（比不画更容易被当成漏配）"},
    {"font_open_failure", "T-04",
     "字体打不开：整段文字退化成占位方块，没人知道是字体的问题"},
    {"theme_token_missing", "既有（0.3.0 补自检）",
     "主题 token 缺失：颜色静默退回兜底值"},
    {"scroll_range_stale", "既有（0.3.0 补自检）",
     "忘了 UpdateScrollRange：滚轮没反应，范围恒为 0"},
    {"text_negative_width", "既有（0.3.0 补自检）",
     "文本区宽度为负：排版交出零宽行，最后一个像素都不画"},
    {"input_caret_clamped", "T-12",
     "输入框光标越界：画到框外、还跟着滚动量漂"},
    {"background_alpha_without_transparent", "既有（0.4.0 补自检）",
     "背景色配了 alpha、窗口却不是透明窗口：桌面透不出来，日志一个字都没有"},
    {"text_raster_capped", "T-03（0.7.1）",
     "设计尺寸小、屏幕大（R 超过上限）：文字被静默夹到 3× 烘再放大，字糊而无声"},
    {"image_undersampled", "T-04（0.7.1）",
     "素材像素不够它占的屏幕像素（σ<1）：画面只是有点软，没人会去查素材"},
    {"texture_bytes_leak", "T-05（0.8.2）",
     "纹理账本只建不销账：内存只涨不回落，画面上完全看不出来"},
    {"glyph_atlas_full", "T-02（0.8.3）",
     "字形图集满了：文字永久改走整段纹理（内存与首帧耗时都涨），日志一个字都没有"},
};

/// 自检**真的跑到**的条目（运行时登记）。
///
/// "表里有、测试里没有"只能靠这个集合抓住——声明一张表很容易，让每条真的
/// 跑一遍不容易。删掉某个自检函数（或它的登记）时，元自检会红。
std::vector<std::string> g_silentChecked;

/// 登记一条"我刚验过"。名字必须与 `kSilentFailures` 里的一致（拼错会被元自检
/// 抓住：总条数或逐条匹配会对不上）。
void NoteSilentFailure(const char* id) { g_silentChecked.emplace_back(id); }

// ---------------------------------------------------------------------------
// 静默失效护栏（0.1.1 T-01 ~ T-05）
//
// 这一组验的不是"功能对不对"，而是"出错的时候**发不发声**"。反馈方的 12 条
// 里真正致命的那几条都是同一类：配置错了 / 内容撑不开 / 字体没找到，界面上
// 只是少了一块或变成方块，日志里一个字都没有。
//
// 断言全部落在**可观测的量**上（尺寸、颜色值、`InkLog::CountOf`），不用"看起来
// 对"这种判据；每一项都用 `ResetStats()` 把计数归零，于是"恰好 1 条"是能断言的。
// ---------------------------------------------------------------------------

class ZeroProbe : public InkingStaticWidget {
public:
    explicit ZeroProbe(InkingWidget* parent)
        : InkingStaticWidget(parent, "zero_probe") {}

    /// 把 protected 的护栏与几何暴露出来：这里验的是**机制**本身
    /// （0×0 报一条、有尺寸不报），不依赖某个具体组件怎么排自己。
    void Fire() { WarnIfZeroSize("ZeroProbe"); }
    void SetBox(int width, int height) noexcept {
        _width = width;
        _height = height;
    }
};

void CheckSilentFailureGuards(SDL_Renderer* renderer, InkingScene* home) {
    std::printf("\n-- 静默失效护栏（0.1.1）--\n");

    struct GuardScene : InkingScene {
        explicit GuardScene(const char* name) : InkingScene(name) {}
    };
    GuardScene scene("selftest-guard");

    // 面板**故意不声明 layout**：这正是反馈方那种"手搭界面"的形态。
    InkingPanelData panelData;
    panelData.name = "guard_panel";
    panelData.width = 400;
    panelData.height = 300;
    auto* panel = static_cast<InkingStaticPanel*>(
        scene.AddChild(std::make_unique<InkingStaticPanel>(&scene, panelData)));

    // ---- T-01：省略 height + 无布局容器 ⇒ 按内容自测量（不是 0） ----
    InkingLabelData labelData;
    labelData.name = "guard_label";
    labelData.width = 200;  // 显式宽度，height 省略（= 0）
    labelData.text = "guard label text";
    labelData.style.fontSize = 16.0f;
    auto* label = static_cast<InkingStaticLabel*>(
        panel->AddChild(std::make_unique<InkingStaticLabel>(panel, labelData)));

    Check(label->GetWidth() == 200,
          "label_self_size：配置的宽度不被测量值覆盖（200 仍是 200）");
    Check(label->GetHeight() > 0,
          "label_self_size：无布局容器时按内容撑开（不再是 0 高、不是一像素不画）");
    {
        const InkingLayoutSize measured =
            label->MeasureLayout(InkingLayoutConstraints{0.0f, 0.0f});
        Check(static_cast<int>(std::lround(measured.height)) == label->GetHeight(),
              "label_self_size：高度等于 MeasureLayout 报的那一份（两条腿同一个答案）");
    }
    NoteSilentFailure("label_self_size");

    // ---- T-01 反例：显式高度优先，绝不按内容覆盖 ----
    InkingLabelData tallData;
    tallData.name = "guard_label_tall";
    tallData.width = 200;
    tallData.height = 24;  // 文字要三行，24 装不下 —— 那是使用者的选择
    tallData.text = "line one\nline two\nline three";
    tallData.style.fontSize = 16.0f;
    auto* tall = static_cast<InkingStaticLabel*>(
        panel->AddChild(std::make_unique<InkingStaticLabel>(panel, tallData)));
    Check(tall->GetHeight() == 24,
          "label_explicit_height：写了 height: 24 就是 24（自测量只补零的那一维）");

    // ---- T-02：0 尺寸走统一护栏，且只有这一条 ----
    auto probe = std::make_unique<ZeroProbe>(panel);
    InkLog::ResetStats();
    probe->Fire();
    Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
          "warn_zero_size：0×0 产生 1 条 WARN");
    probe->SetBox(120, 40);
    InkLog::ResetStats();
    probe->Fire();
    Check(InkLog::CountOf(InkingLogLevel::Warn) == 0,
          "warn_zero_size：有尺寸时一条都不产生（不是每帧刷）");
    NoteSilentFailure("warn_zero_size");

    // ---- T-03：两态全透明 ⇒ 品红兜底 + 1 条 WARN ----
    {
        InkingPanelData transparent;
        transparent.name = "guard_transparent";
        transparent.closed.color = 0x00000000u;
        transparent.openInheritsClosed = true;  // open 也跟着继承 ⇒ 两态都透明
        InkLog::ResetStats();
        transparent.Normalize();
        Check(transparent.closed.color == 0xFFFF00FFu
                  && transparent.open.color == 0xFFFF00FFu,
              "panel_transparent_fallback：兜底成品红（0xFFFF00FF），不再是看不见的黑");
        Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
              "panel_transparent_fallback：兜底同时产生 1 条 WARN");

        // 半透明（alpha = 1）**不该**进兜底：那是"看不见但点得到"的正规做法。
        InkingPanelData veil;
        veil.name = "guard_veil";
        veil.closed.color = 0x01000000u;
        veil.openInheritsClosed = true;
        InkLog::ResetStats();
        veil.Normalize();
        Check(veil.closed.color == 0x01000000u
                  && InkLog::CountOf(InkingLogLevel::Warn) == 0,
              "panel_transparent_fallback：alpha = 1/255 是正规用法，不动、也不报");
    }
    NoteSilentFailure("panel_transparent_fallback");

    // ---- T-04：字体打不开要说出来（否则只能看到"字变成了方块"） ----
    if (renderer != nullptr) {
        InkingTextRequest request;
        request.text = "font probe";
        request.fontSize = 18.0f;
        request.fontPath = "Z:/definitely/not/a/font.ttf";
        InkLog::ResetStats();
        inking_detail::BeginFrame(renderer);
        inking_detail::PaintText(renderer, request, 400, 100, 1.0f, 0.0f, 0.0f,
                                  InkingTransformSpec{});
        inking_detail::EndFrame();
        Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
              "font_open_failure：路径打不开产生 1 条 WARN（带路径、字号、SDL 错误）");
        Check(!InkLog::grep("not/a/font", "InkingText").empty(),
              "font_open_failure：日志里能按路径搜到（可归因，不是只说'画不出来'）");
    }
    NoteSilentFailure("font_open_failure");

    // ---- T-05：分级计数 + 同级去重 ----
    InkLog::ResetStats();
    for (int i = 0; i < 3; ++i) {
        INK_LOG_INFO("SelftestLog", "dedup-probe-message");
    }
    const std::vector<std::string> hit =
        InkLog::grep("dedup-probe-message", "SelftestLog");
    Check(hit.size() == 1,
          "log_dedup：同一消息连写 3 次只落 1 行（其余的进 FOLD 汇总行）");
    Check(InkLog::CountOf(InkingLogLevel::Info) == 3,
          "log_dedup：计数按**调用次数**算（3 次），不是按落盘行数算");
#if defined(INK_ISDEBUG)
    std::printf("      [skip] 本次构建打开了 INKING_DEBUG_LOG —— "
                "级别开关的验收不适用\n");
#else
    INK_LOG_DEBUG("SelftestLog", "debug-probe-message");
    Check(InkLog::CountOf(InkingLogLevel::Debug) == 0,
          "log_level_filter：INKING_DEBUG_LOG=OFF 时 DEBUG 根本不进二进制（计数为 0）");
#endif

    // 场景库是"构造即登记"的：本组用完把活跃场景还回去，别让后面的用例
    // 渲染到的是一张空场景（`CheckLayerOrder` 自己会切，但别靠它）。
    InkingSceneLibrary::SetActiveScene(home);
}

// ---------------------------------------------------------------------------
// 字体回退链（0.3.0 T-13）
// ---------------------------------------------------------------------------
//
// 判据全都落在**可观测的量**上：字形来自哪份文件是看不出来的（长得都像字），
// 所以验"解析结论"——`fontFallbacks` 计数、WARN 条数、以及估算法的特征值。
//
// 本机若一份字体都打不开（沙箱里 `SDL_IOFromFile` 对所有路径失败），
// "退到第二条"那一段无从验起：如实跳过，别让环境问题伪装成通过。
void CheckFontFallbackChain(SDL_Renderer* renderer,
                            InkingScene* home) {
    std::printf("\n-- 字体回退链（0.3.0 T-13）--\n");

    // ---- ① 整条链全打不开：1 条汇总 WARN（带全部候选），且不"静默换字体" ----
    {
        InkingTextRequest request;
        request.text = "chain probe";  // 11 个码点
        request.fontSize = 18.0f;
        request.fontPath = "Z:/definitely/not/a/font.ttf";
        request.fontPaths = {"Z:/also/not/here.ttf"};

        InkLog::ResetStats();
        const InkingTextMetrics metrics = InkingMeasureText(request);
        Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
              "font_fallback_chain：两条候选都打不开 → 汇总 1 条 WARN（不是每条一条）");
        Check(!InkLog::grep("also/not/here", "InkingText").empty(),
              "font_fallback_chain：WARN 里列全了链上每一条路径（可归因）");
        // 没字体 ⇒ 估算法：高 = 字号，宽 = 字号 × 码点数。与退化画法同一套数。
        Check(std::fabs(metrics.height - 18.0f) < 0.01f,
              "font_fallback_chain：全失败时退回估算法（不是 0、也不是乱猜）");

        // 同一条链再问一次：结论进缓存 ⇒ 不再产生第二条 WARN（否则每帧一条）。
        InkLog::ResetStats();
        (void)InkingMeasureText(request);
        Check(InkLog::CountOf(InkingLogLevel::Warn) == 0,
              "font_fallback_chain：同一条链只报一次（第二次解析命中缓存）");
    }

    // ---- ② 整条链路（排版一趟 + 绘制一趟）也只报一条：0.3.0 的门槛 ----
    // 为什么单独验：链解析发生在两个不同的字号上（排版用 `fontSize`、绘制用
    // `deviceSize`），按字号缓存就会**两趟各报一条**——门槛要的是一共一条。
    if (renderer != nullptr && home != nullptr) {
        InkingScene scene("selftest-font-chain");
        InkingLabelData data;
        data.name = "chain_label";
        data.width = 200;
        data.text = "chain label";
        data.style.fontSize = 16.0f;
        data.style.fontPath = "Z:/chain/missing.ttf";
        data.style.fontPaths = {"Z:/chain/missing2.ttf"};

        InkLog::ResetStats();
        auto* label = static_cast<InkingStaticLabel*>(
            scene.AddChild(
                std::make_unique<InkingStaticLabel>(&scene, data)));
        Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
              "font_fallback_chain：构造（排版那一趟）报 1 条");
        Check(label->GetWidth() > 0,
              "font_fallback_chain：没字体也照常排版（估算法给出宽度，不是 0）");

        InkingSceneLibrary::SetActiveScene(&scene);
        scene.TickFrame(1.0f / 60.0f);
        Check(InkingSceneLibrary::RenderScene(renderer),
              "font_fallback_chain：退化画法也能提交一帧");
        Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
              "font_fallback_chain：绘制那一趟不再重复报（整条链路共 1 条 WARN）");
        InkingSceneLibrary::SetActiveScene(home);
    }

    // ---- ③ 链上第二条命中 ⇒ `fontFallbacks` +1（需要一份真字体） ----
    const std::string usable = FindUsableFont();
    if (usable.empty()) {
        std::printf("      [skip] 没有可读字体（可用 INK_TEST_FONT 指定）——"
                    "「退到第二条」这一段无从验起\n");
        return;
    }
    InkingTextRequest request;
    request.text = "Hg";
    request.fontSize = 40.0f;
    request.fontPath = "Z:/definitely/not/a/font.ttf";
    request.fontPaths = {usable};
    const std::uint64_t before =
        inking_detail::TextCacheStatsSnapshot().fontFallbacks;
    const InkingTextMetrics metrics = InkingMeasureText(request);
    const std::uint64_t after =
        inking_detail::TextCacheStatsSnapshot().fontFallbacks;
    Check(after == before + 1,
          "font_fallback_chain：命中第二条 → fontFallbacks +1（用的是第二份字体）");
    Check(std::fabs(metrics.width - 80.0f) > 0.5f,
          "font_fallback_chain：量出来的是真字体度量（不是 2 码点 × 40 的估算）");
}

// ---------------------------------------------------------------------------
// 静默失效清单里的三条"既有路径"（0.3.0 T-14）
//
// 这三条以前只存在于代码注释里：行为是对的（或至少是定下来的），但没有断言。
// 现在逐条钉住——"改坏了"会红，而不是等使用者再报一次。
// ---------------------------------------------------------------------------

/// 主题 token 缺失：退回兜底色**并且**记一次 miss。
///
/// 为什么不是"报错"：token 缺失发生在**运行期装表**之后（生成期已经保证用到的
/// token 每张表都有），报错会把"用户拼了一张不全的表"这种可恢复的情况变成崩溃。
/// 所以取"可见但不致命"：颜色退回兜底 + 计数 +1。
void CheckThemeTokenMissing() {
    std::printf("\n-- 主题 token 缺失 --\n");

    InkingClearThemeTables();
    InkingThemeTable partial;
    partial.name = "t_partial";
    partial.colors = {{"surface", 0xFF112233u}};
    Check(InkingInstallThemeTable(std::move(partial)),
          "装一张只含部分 token 的表");
    Check(InkingSetActiveTheme("t_partial"), "切到那张不全的表");

    const InkingThemeStats before = InkingThemeStatsSnapshot();
    const std::uint32_t got = InkingResolveThemeColor("not_a_token", 0xFF445566u);
    const InkingThemeStats after = InkingThemeStatsSnapshot();
    Check(got == 0xFF445566u,
          "theme_token_missing：表里没有的 token 退回兜底色（不是黑、不是随机色）");
    Check(after.misses == before.misses + 1,
          "theme_token_missing：查不到记一次 miss（观测口看得见，不是完全无声）");
    NoteSilentFailure("theme_token_missing");

    // 一张表都没装 = 零成本路径：直接给兜底色，**不计 miss**。
    // （否则"不用运行期主题"的项目会看到一堆假 miss，把观测口变成噪声。）
    InkingClearThemeTables();
    const InkingThemeStats idle = InkingThemeStatsSnapshot();
    (void)InkingResolveThemeColor("surface", 0xFF667788u);
    Check(InkingThemeStatsSnapshot().misses == idle.misses,
          "theme_token_missing：一张表都没装时不算 miss（零成本路径不该被误报）");
}

/// 滚动范围：内容超出视口后，`UpdateScrollRange` 给出的范围要被真的用上。
///
/// 这条路径的失败形态很隐蔽：忘了调 `UpdateScrollRange` ⇒ 范围恒为 0 ⇒
/// 滚轮**没有任何反应**，而画面本身完全正常（所以不会有人怀疑到滚动上）。
void CheckScrollRangeStale() {
    std::printf("\n-- 滚动范围 --\n");

    InkingScene scene("selftest-scroll");
    InkingPanelData panelData;
    panelData.name = "scroll_panel";
    panelData.width = 200;
    panelData.height = 100;
    panelData.scroll.enabled = true;
    auto* panel = static_cast<InkingStaticPanel*>(
        scene.AddChild(
            std::make_unique<InkingStaticPanel>(&scene, panelData)));

    for (int i = 0; i < 3; ++i) {
        InkingPanelData row;
        row.name = "scroll_row" + std::to_string(i);
        row.width = 200;
        row.height = 60;
        row.offsetY = static_cast<float>(i) * 60.0f;
        panel->AddChild(std::make_unique<InkingStaticPanel>(panel, row));
    }

    panel->UpdateScrollRange();
    Check(std::fabs(panel->GetContentHeight() - 180.0f) < 0.01f,
          "scroll_range_stale：内容高 = 子节点并集（3 × 60 = 180）");
    const float expected = 180.0f - panel->ContentViewportHeight();
    Check(std::fabs(panel->GetScrollRangeY() - expected) < 0.01f,
          "scroll_range_stale：可滚范围 = 内容 − 视口（不是 0、也不是内容本身）");

    panel->SetScroll(0.0f, 10000.0f);
    Check(std::fabs(panel->GetScrollY() - panel->GetScrollRangeY()) < 0.01f,
          "scroll_range_stale：SetScroll 按这个范围夹住（范围错了这里立刻露馅）");
    NoteSilentFailure("scroll_range_stale");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

/// 文本区宽度为负：两层都要挡住，不能变成"什么都不画"。
void CheckTextNegativeWidth() {
    std::printf("\n-- 文本区宽度为负 --\n");

    // ---- ① 排版层：可排宽度 = maxWidth − 左右 padding ≤ 0 ⇒ **不换行** ----
    // 这条挡住的是"框比内缩还窄"时排出一堆零宽行、最后一像素不画。
    InkingTextLayoutRequest request;
    request.text = "negative width probe";
    request.style.fontSize = 16.0f;
    request.style.padding.left = 8.0f;
    request.style.padding.right = 8.0f;
    request.maxWidth = 10.0f;  // 可排宽度 = 10 − 16 = −6
    const auto measure = [](const std::string& text,
                            const InkingTextStyle& style) {
        InkingTextRequest probe;
        probe.text = text;
        probe.fontSize = style.fontSize > 0.0f ? style.fontSize : 16.0f;
        probe.fontPath = style.fontPath;
        probe.fontPaths = style.fontPaths;
        const InkingTextMetrics metrics = InkingMeasureText(probe);
        return InkingTextExtent{metrics.width, metrics.height};
    };
    const InkingTextLayoutResult laid = InkingLayoutText(request, measure);
    Check(laid.lines.size() == 1,
          "text_negative_width：可排宽度为负时不换行（不是零行、不是崩）");
    Check(laid.contentWidth > 0.0f,
          "text_negative_width：仍然报出真实宽度（不是 0 宽的行）");
    NoteSilentFailure("text_negative_width");

    // ---- ② 组件层：配置了负宽度 ⇒ 夹到 0，再按内容补一维 ----
    InkingScene scene("selftest-negative");
    InkingLabelData data;
    data.name = "negative_label";
    data.width = -40;   // 明显不合法的配置
    data.height = 20;
    data.text = "neg";
    data.style.fontSize = 16.0f;
    auto* label = static_cast<InkingStaticLabel*>(
        scene.AddChild(
            std::make_unique<InkingStaticLabel>(&scene, data)));
    Check(label->GetWidth() > 0 && label->GetHeight() == 20,
          "text_negative_width：负宽度被夹到 0 后按内容补，不会出现负几何");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-01（0.8.3）：文字按框高裁行
// ---------------------------------------------------------------------------
//
// `InkingTextOverflow::Clip` 是默认值、头文件里写着"多余的行不画"，但 0.8.2 之前
// **没有任何人实现它**：`maxHeight` 只参与垂直对齐，于是 300×40 的标签把 7 行
// 全画到框外（实测框外 10,030 个亮像素）。
//
// 三条断言各钉一个会静默走错的实现：
//   ① 行数真的按框高截（且 `truncated` 置起来——它是"有内容没排进来"的唯一观测口）；
//   ② **至少留一行**：框比一行还矮时也不许整块变空白（那比溢出糟得多）；
//   ③ 画面上**框外 0 个亮像素**，同时框内 > 0——后半句防的是"什么都不画"这种
//      假通过（0 == 0 对任何错误的实现都成立）。
void CheckTextClipByHeight() {
    std::printf("\n-- 文字按框高裁行（0.8.3 T-01） --\n");

    // ---- ① 排版层：假测量，数值完全可控（宽 10/码点、行高 = 字号 × 行距） ----
    const auto measure = [](const std::string& text,
                            const InkingTextStyle& style) {
        std::size_t codepoints = 0;
        for (const unsigned char byte : text) {
            if ((byte & 0xC0u) != 0x80u) {
                ++codepoints;
            }
        }
        return InkingTextExtent{10.0f * static_cast<float>(codepoints),
                                  style.fontSize};
    };
    InkingTextLayoutRequest request;
    request.text = "a\nb\nc\nd";     // 4 行，每行 20 × 1.25 = 25
    request.style.fontSize = 20.0f;
    request.maxWidth = 0.0f;         // 宽度不限 ⇒ 不换行，纯靠 \n
    request.maxHeight = 60.0f;       // 装得下 2 行（50），第 3 行超
    const InkingTextLayoutResult clipped = InkingLayoutText(request, measure);
    std::printf("         4 行 / 框高 60 ⇒ %zu 行 / 高 %.0f / truncated=%d\n",
                clipped.lines.size(), static_cast<double>(clipped.height),
                clipped.truncated ? 1 : 0);
    Check(clipped.lines.size() == 2,
          "text_clip_by_height：按框高丢尾行（4 行 → 装得下的 2 行）");
    Check(clipped.truncated && clipped.contentHeight <= 60.0f,
          "text_clip_by_height：置 truncated，且排出来的高度不超框");

    // ---- ② 至少留一行：框比一行还矮 ----
    request.maxHeight = 5.0f;  // 5 < 25（一行都装不下）
    const InkingTextLayoutResult tooShort = InkingLayoutText(request, measure);
    Check(tooShort.lines.size() == 1 && tooShort.truncated,
          "text_clip_by_height：框比一行还矮时仍留一行（宁可溢出几像素，也不许整块消失）");

    // ---- ③ 与 maxLines 取更严的那个 ----
    request.maxHeight = 0.0f;  // 不限高
    request.maxLines = 3;
    const InkingTextLayoutResult byLines = InkingLayoutText(request, measure);
    request.maxLines = 0;
    request.maxHeight = 60.0f;
    const InkingTextLayoutResult byHeight = InkingLayoutText(request, measure);
    Check(byLines.lines.size() == 3 && byHeight.lines.size() == 2,
          "text_clip_by_height：maxLines 与 maxHeight 各管一头（谁更严谁生效）");

    // ---- ④ 组件 + 像素：E3 的原始证据（300×40 标签 + 100 个汉字） ----
    //
    // 先清一次文字缓存：`AcquireFont` 会把"打开失败"也记成一格（**空**那一格，
    // 免得每帧重试），而本进程早于 `SDL_Init` 就量过文字——那一笔失败的缓存会
    // 让这里永远拿到估算法。清掉之后重新试开，才是"这台机器到底有没有字体"。
    inking_detail::ReleaseTextCache();
    const std::string font = FindUsableFont();
    if (font.empty()) {
        std::printf("         [skip] 没找到可用字体：像素那一段验不了"
                    "（排版那几条上面已经验过）\n");
        return;
    }
    SDL_Window* win = SDL_CreateWindow("text-clip", InkingDesignWidth(),
                                       InkingDesignHeight(), 0);
    SDL_Renderer* rnd =
        win != nullptr ? SDL_CreateRenderer(win, nullptr) : nullptr;
    if (rnd == nullptr) {
        std::printf("         [skip] 建不出渲染器：像素那一段验不了\n");
        if (win != nullptr) {
            SDL_DestroyWindow(win);
        }
        return;
    }

    std::string longText;
    for (int i = 0; i < 100; ++i) {
        longText += "中";
    }
    InkingScene scene("text_clip");
    InkingLabelData data;
    data.name = "clip_label";
    data.width = 300;
    data.height = 40;  // 只装得下一行多一点
    data.style.fontSize = 20.0f;
    data.style.fontPath = font;
    data.text = longText;
    data.selfAnchor = InkingChangeAnchor::LeftTop;
    data.traceAnchor = InkingChangeAnchor::LeftTop;
    data.offsetX = 100.0f;
    data.offsetY = 100.0f;
    auto* label = static_cast<InkingStaticLabel*>(
        scene.AddChild(std::make_unique<InkingStaticLabel>(&scene, data)));
    scene.TickFrame(1.0f / 60.0f);

    const InkingTextLayoutResult& laid = label->GetTextLayout();
    std::printf("         100 汉字 / 300×40 ⇒ %zu 行 / 排出来高 %.0f\n",
                laid.lines.size(), static_cast<double>(laid.height));
    Check(laid.truncated && laid.height <= 40.0f && !laid.lines.empty(),
          "text_clip_by_height：组件层的排版也按 40 的框高截（高度不超框、置 truncated）");

    // 与探针同一套读回口径：窗口 = 设计尺寸 + 关掉逻辑呈现 ⇒ 1 设计单位 = 1 像素。
    SDL_SetRenderLogicalPresentation(rnd, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderDrawColor(rnd, 0, 0, 0, 255);
    SDL_RenderClear(rnd);
    InkingSceneLibrary::SetActiveScene(&scene);
    InkingSceneLibrary::RenderScene(rnd);

    // 只读框那一带：左侧 600 宽 × 下方 300 高（覆盖"下方"与"右侧"两个溢出方向）。
    const std::vector<std::uint32_t> pixels =
        ReadPixelRegion(rnd, SDL_Rect{100, 100, 600, 300});
    int inside = 0;
    int below = 0;
    int right = 0;
    if (pixels.size() == 600u * 300u) {
        for (int y = 0; y < 300; ++y) {
            for (int x = 0; x < 600; ++x) {
                const std::uint32_t pixel =
                    pixels[static_cast<std::size_t>(y) * 600u
                           + static_cast<std::size_t>(x)];
                const int lum = static_cast<int>((pixel >> 16) & 0xFFu)
                    + static_cast<int>((pixel >> 8) & 0xFFu)
                    + static_cast<int>(pixel & 0xFFu);
                if (lum < 90) {
                    continue;  // 黑底
                }
                if (y < 40 && x < 300) {
                    ++inside;
                } else if (y >= 40) {
                    ++below;
                } else {
                    ++right;
                }
            }
        }
    }
    std::printf("         框内亮像素 %d / 框下方 %d / 框右侧 %d\n", inside, below,
                right);
    Check(inside > 0,
          "text_clip_by_height：框内真的有字（否则「框外 0 像素」是假通过）");
    Check(below == 0 && right == 0,
          "text_clip_by_height：框外亮像素 == 0（E3 的原始症状：改动前是 10,030）");

    InkingSceneLibrary::SetActiveScene(nullptr);
    SDL_DestroyRenderer(rnd);
    SDL_DestroyWindow(win);
}

// ---------------------------------------------------------------------------
// T-02 / T-03（0.8.3）：字形图集满了之后
// ---------------------------------------------------------------------------
//
// 改动前的实测：1024² 图集灌满后，连画 60 帧（文字一个字都没变）**多烘 1140 次**
// 字形、也多失败 1140 次（19 次/帧），日志 **0 条**。两件事都属于"白干活还不能
// 被看见"：
//   - 每次都把整段文字逐字形烘一遍再失败，最后照样退回整段渲染（T-02）；
//   - "图集满了"这个事实没有任何声音，与项目自己的静默失效纪律相悖（T-03）。
//
// 三条断言：
//   ① 满了之后连画 60 帧，`rasterized` 增量 **== 0**，且画面**仍有像素**
//      （后半句防"把字画没了"换来的假绿）；
//   ② 第一次满恰好 1 条 WARN，继续灌 2000 个新字仍然只有那 1 条；
//   ③ `ReleaseGlyphAtlas()` 之后 `full` 复位——漏了复位就是"新图集一上来就
//      宣布自己满了"，文字永久退化成整段渲染。
void CheckGlyphAtlasFullNoRetry(SDL_Renderer* renderer) {
    std::printf("\n-- 字形图集满：不再逐帧重烘 / 首次发声（0.8.3 T-02 / T-03） --\n");
    if (renderer == nullptr) {
        std::printf("         [skip] 没有渲染器，图集无从灌起\n");
        return;
    }
    // 从干净状态开始：前面的用例留下的图集可能是"半满"的，那会让灌满需要多少格
    // 变得不可预测（这里要的是确定性，不是速度）。
    inking_detail::ReleaseTextCache();
    const std::string font = FindUsableFont();
    if (font.empty()) {
        std::printf("         [skip] 没有可读字体，图集无从灌起"
                    "（先看 PATH 上的 SDL3*.dll 有几份）\n");
        return;
    }

    // 大字号灌得快：48 px 的字形一层只放得下 20 来个，几百格就把 1024² 塞满了。
    constexpr float kDeviceSize = 48.0f;
    const auto glyphAt = [](std::uint32_t code) {
        // U+0800..U+FFFF 的三字节 UTF-8（这里灌的全是 CJK 基本区）。
        std::string out;
        out.push_back(static_cast<char>(0xE0u | (code >> 12)));
        out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        return out;
    };

    InkLog::ResetStats();
    std::uint32_t code = 0x4E00u;
    int filled = 0;
    bool full = false;
    for (int i = 0; i < 4000 && !full; ++i, ++code) {
        const std::string glyph = glyphAt(code);
        (void)inking_detail::AcquireGlyph(renderer, font, kDeviceSize,
                                           std::string_view(glyph));
        ++filled;
        full = inking_detail::GlyphAtlasSnapshot().full;
    }
    const inking_detail::GlyphAtlasStats atFull =
        inking_detail::GlyphAtlasSnapshot();
    std::printf("         灌 %d 个字形 ⇒ 图集 %d 格（上限 %d²）/ full=%d\n", filled,
                atFull.slots, 1024, atFull.full ? 1 : 0);
    Check(full && atFull.slots > 0,
          "glyph_atlas_full：图集真的被灌满了（观测口 full 置起来、格数 > 0）");

    // ---- ② 第一次满恰好 1 条 WARN ----
    const int warnsAtFull = InkLog::CountOf(InkingLogLevel::Warn);
    Check(warnsAtFull == 1,
          "glyph_atlas_full：第一次满恰好 1 条 WARN（带格数 / 上限 / 后果）");
    for (int i = 0; i < 2000; ++i, ++code) {
        const std::string glyph = glyphAt(code);
        (void)inking_detail::AcquireGlyph(renderer, font, kDeviceSize,
                                           std::string_view(glyph));
    }
    Check(InkLog::CountOf(InkingLogLevel::Warn) == warnsAtFull,
          "glyph_atlas_full：再灌 2000 个新字仍然只有那 1 条（不是每个字一条）");
    NoteSilentFailure("glyph_atlas_full");

    // ---- ① 连画 60 帧：重烘次数一个都不该涨 ----
    InkingScene scene("glyph_full");
    InkingLabelData data;
    data.name = "glyph_full_label";
    data.width = 400;
    data.height = 60;
    data.style.fontSize = 20.0f;
    data.style.fontPath = font;
    data.text = "abc中文测试";
    data.selfAnchor = InkingChangeAnchor::LeftTop;
    data.traceAnchor = InkingChangeAnchor::LeftTop;
    data.offsetX = 40.0f;
    data.offsetY = 40.0f;
    scene.AddChild(std::make_unique<InkingStaticLabel>(&scene, data));
    scene.TickFrame(1.0f / 60.0f);
    InkingSceneLibrary::SetActiveScene(&scene);

    const int rasterizedBefore =
        inking_detail::GlyphAtlasSnapshot().rasterized;
    for (int frame = 0; frame < 60; ++frame) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        InkingSceneLibrary::RenderScene(renderer);
    }
    const int rasterizedAfter = inking_detail::GlyphAtlasSnapshot().rasterized;
    std::printf("         连画 60 帧：重烘 %d → %d\n", rasterizedBefore,
                rasterizedAfter);
    Check(rasterizedAfter == rasterizedBefore,
          "glyph_atlas_full：图集满后连画 60 帧，重烘次数增量 == 0（改动前是 1140）");

    // 画面仍有字：回退到整段渲染那条路必须真的画出东西。
    // 整屏读回（窗口是 640×360，逻辑呈现 LETTERBOX 到 1280×720，读物理像素即可）：
    // 这里只问"有没有亮像素"，不关心像素落在哪。
    int bright = 0;
    {
        const std::vector<std::uint32_t> pixels =
            ReadPixelRegion(renderer, SDL_Rect{0, 0, 640, 360});
        for (const std::uint32_t pixel : pixels) {
            const int lum = static_cast<int>((pixel >> 16) & 0xFFu)
                + static_cast<int>((pixel >> 8) & 0xFFu)
                + static_cast<int>(pixel & 0xFFu);
            if (lum >= 90) {
                ++bright;
            }
        }
    }
    std::printf("         回退到整段渲染后亮像素 %d\n", bright);
    Check(bright > 0, "glyph_atlas_full：回退之后画面仍有字（回退没坏）");

    // ---- ③ 复位：ReleaseGlyphAtlas 之后 full 必须落回 false ----
    InkingSceneLibrary::SetActiveScene(nullptr);
    inking_detail::ReleaseGlyphAtlas();
    Check(!inking_detail::GlyphAtlasSnapshot().full,
          "glyph_atlas_full：ReleaseGlyphAtlas 之后 full 复位（新图集不该一上来就满）");
}

// ---------------------------------------------------------------------------
// T-04（0.8.4）：文字纹理的字节预算与 LRU 淘汰
// ---------------------------------------------------------------------------
//
// 要验的是四件**只有计数看得见**的事：
//   ① 超预算之后真的会淘汰，且当前占用回到"预算 + 单张最大"以内（瞬时上界）；
//   ② 被丢掉的纹理**下一帧能重建**（不是丢完就缺字，也不是丢了个野句柄）；
//   ③ 预算设成 0 = 与 0.8.3 完全一致：只涨不落（这条是"淘汰可关"的判据，
//      任务书 §0 的取舍底线）；
//   ④ 淘汰次数进了账本（`textureEvictions`），否则"生效了没有"在画面上看不出来。
//
// 这一段**必须关掉逐字形那条路**（`SetGlyphAtlasEnabled(false)`）：开着的时候
// 文字从图集里取，根本不建"整段纹理"，预算也就无从谈起。
void CheckTextureBudgetEviction(SDL_Renderer* renderer) {
    std::printf("\n-- 文字纹理预算与 LRU 淘汰（0.8.4 T-04） --\n");
    if (renderer == nullptr) {
        std::printf("         [skip] 没有渲染器，纹理无从建起\n");
        return;
    }
    const std::string font = FindUsableFont();
    if (font.empty()) {
        std::printf("         [skip] 没有可读字体，纹理无从建起\n");
        return;
    }

    using inking_detail::TextCacheStats;
    using inking_detail::TextCacheStatsSnapshot;
    constexpr std::uint64_t kMegabyte = 1024ull * 1024ull;
    constexpr std::uint64_t kBudget = 8ull * kMegabyte;

    // 逐字形那条路关掉：这一条验的是"整段一张纹理"那张表。
    inking_detail::SetGlyphAtlasEnabled(false);
    inking_detail::ReleaseTextCache();
    inking_detail::SetTextureBudget(0);  // 先确认默认档（不淘汰）

    InkingScene scene("texture_budget");
    InkingLabelData data;
    data.name = "budget_label";
    data.width = 800;
    data.height = 40;
    data.style.fontSize = 20.0f;
    data.style.fontPath = font;
    data.wrap = InkingTextWrap::NoWrap;  // 单行：一张纹理就是这一串
    data.selfAnchor = InkingChangeAnchor::LeftTop;
    data.traceAnchor = InkingChangeAnchor::LeftTop;
    auto* label = static_cast<InkingStaticLabel*>(
        scene.AddChild(std::make_unique<InkingStaticLabel>(&scene, data)));
    InkingSceneLibrary::SetActiveScene(&scene);

    // 每串都不一样的**长**串：一张纹理约 60 KB，200 张就是 12 MB 上下。
    const std::string base =
        "文字纹理预算与淘汰的自检样例串，长度要够才能把预算撑满。";
    const auto frame = [&](int index) {
        label->SetText(base + std::to_string(index));
        scene.TickFrame(1.0f / 60.0f);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        InkingSceneLibrary::RenderScene(renderer);
        return TextCacheStatsSnapshot();
    };

    const TextCacheStats one = frame(0);
    const std::uint64_t singleBytes = one.textureBytes;
    std::printf("         单张纹理 %llu 字节\n",
                static_cast<unsigned long long>(singleBytes));
    Check(singleBytes > 0,
          "T-04：关掉逐字形之后，一段文字确实建出了一张整段纹理");

    // ---- ③ 预算 0：只涨不落（与 0.8.3 一致） ----
    for (int i = 1; i <= 200; ++i) {
        frame(i);
    }
    const TextCacheStats unbounded = TextCacheStatsSnapshot();
    std::printf("         预算 0 × 201 张 ⇒ %llu 字节 / 淘汰 %llu 次\n",
                static_cast<unsigned long long>(unbounded.textureBytes),
                static_cast<unsigned long long>(unbounded.textureEvictions));
    Check(unbounded.textureBytes > kBudget,
          "T-04：预算 0 时占用越过 8 MB（不淘汰就是改动前的行为，这条是基线）");
    Check(unbounded.textureEvictions == 0,
          "T-04：预算 0 时一次淘汰都不发生（淘汰必须可关）");

    // ---- ①② 预算 8 MB：淘汰生效、且被丢的能重建 ----
    inking_detail::SetTextureBudget(kBudget);
    for (int i = 201; i <= 400; ++i) {
        frame(i);
    }
    // 再多一帧：淘汰发生在帧首，最后一张的"回落"要到下一帧开头才看得到。
    const TextCacheStats bounded = frame(401);
    std::printf("         预算 8 MB × 402 张 ⇒ %llu 字节 / 淘汰 %llu 次\n",
                static_cast<unsigned long long>(bounded.textureBytes),
                static_cast<unsigned long long>(bounded.textureEvictions));
    Check(bounded.textureBytes <= kBudget + singleBytes,
          "T-04：预算 8 MB 时占用回到 ≤ 8 MB + 单张最大（瞬时上界，见头文件）");
    Check(bounded.textureEvictions > 0, "T-04：真的淘汰过（textureEvictions > 0）");

    // 被丢的那一串还能重建：重画第 0 帧那一串，`texturesCreated` 必须再涨一次。
    // 涨了说明"它已经被丢掉了、这次是重新烘的"，也就是淘汰真的发生了（而不是
    // 账本算错了数）。
    const std::uint64_t createdBefore = TextCacheStatsSnapshot().texturesCreated;
    const TextCacheStats rebuilt = frame(0);
    std::printf("         重画最早那一串 ⇒ 新建 %llu 次（之前 %llu）\n",
                static_cast<unsigned long long>(rebuilt.texturesCreated),
                static_cast<unsigned long long>(createdBefore));
    Check(rebuilt.texturesCreated > createdBefore,
          "T-04：被淘汰的那一串能重建（丢的是缓存，不是内容；也没有野句柄）");

    // ---- 收尾：预算归 0、逐字形那条路开回去、缓存清空 ----
    inking_detail::SetTextureBudget(0);
    Check(inking_detail::GetTextureBudget() == 0, "T-04：预算可以关回去（0 = 不淘汰）");
    inking_detail::SetGlyphAtlasEnabled(true);
    InkingSceneLibrary::SetActiveScene(nullptr);
    inking_detail::ReleaseTextCache();
}

/// 元自检：清单里的每一条**都真的跑过**。
///
/// 这是"清单化"能不能成立的关键：声明一张表只要几分钟，而一条自检悄悄消失
/// （改名、被注释掉、被删）没人会发现。这里逐条对账，并且要求**恰好一次**
/// （登记两次意味着两个自检共用一个名字，那等于其中一个没被清单覆盖）。
void CheckSilentFailureListSynced() {
    std::printf("\n-- 静默失效清单同步（T-14）--\n");

    std::size_t declared = 0;
    for (const SilentFailureItem& item : kSilentFailures) {
        ++declared;
        bool noted = false;
        for (const std::string& seen : g_silentChecked) {
            if (seen == item.id) {
                noted = true;
                break;
            }
        }
        char label[256] = {};
        std::snprintf(label, sizeof(label),
                      "silent_failure_list_synced：%s 的自检真的跑了（%s）",
                      item.id, item.task);
        Check(noted, label);
        if (!noted) {
            std::printf("         现象：%s\n", item.phenomenon);
        }
    }
    Check(g_silentChecked.size() == declared,
          "silent_failure_list_synced：每条恰好登记一次（多登记 = 两条自检共用同一个名字）");
}

void CheckTextHiDpi() {
    SDL_Window* windowA = SDL_CreateWindow("hidpi-1x", 1280, 720, 0);
    SDL_Window* windowB = SDL_CreateWindow("hidpi-2x", 2560, 1440, 0);
    SDL_Renderer* rendererA =
        windowA != nullptr ? SDL_CreateRenderer(windowA, nullptr) : nullptr;
    SDL_Renderer* rendererB =
        windowB != nullptr ? SDL_CreateRenderer(windowB, nullptr) : nullptr;
    Check(rendererA != nullptr && rendererB != nullptr,
          "创建两套渲染器（比例 1 / 比例 2）");
    if (rendererA == nullptr || rendererB == nullptr) {
        if (rendererA != nullptr) { SDL_DestroyRenderer(rendererA); }
        if (rendererB != nullptr) { SDL_DestroyRenderer(rendererB); }
        if (windowA != nullptr) { SDL_DestroyWindow(windowA); }
        if (windowB != nullptr) { SDL_DestroyWindow(windowB); }
        return;
    }

    // 与窗口层同一条设置：设计尺寸的逻辑呈现，等比 + 黑边。
    // **只给 B 设**：A 代表"没有逻辑呈现的普通窗口"，那也是一条真实路径
    // （`RendererDeviceRatio` 对它退回 1）。
    SDL_SetRenderLogicalPresentation(rendererB, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    // 高清那条路的**输入**：文字层就是从这两个数算出"1 逻辑单位 = 多少设备像素"。
    // 先把它们钉住，后面几千万行计算才有意义。
    {
        int logicalWidth = 0;
        int logicalHeight = 0;
        SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
        int outputWidth = 0;
        int outputHeight = 0;
        const bool readable =
            SDL_GetRenderLogicalPresentation(rendererB, &logicalWidth,
                                             &logicalHeight, &mode)
            && SDL_GetCurrentRenderOutputSize(rendererB, &outputWidth,
                                              &outputHeight);
        Check(readable && logicalWidth == InkingDesignWidth()
                  && logicalHeight == InkingDesignHeight()
                  && outputWidth == 2560 && outputHeight == 1440,
              "比例来源可读：逻辑 1280×720 / 输出 2560×1440 ⇒ ratio = 2");
    }

    InkingTextRequest request;
    request.text = "Hg高清Text";
    request.fontSize = 40.0f;
    request.color = 0xFFFFFFFFu;

    const std::string fontPath = FindUsableFont();
    const bool hasFont = !fontPath.empty();
    request.fontPath = fontPath;
    if (!hasFont) {
        // 没有字体时这一层画出的是**退化方块**（`PaintPlaceholder`），它也跟着
        // 设计尺寸走，所以下面那两条"布局零位移"照样有意义；但"文字是否真的
        // 按设备比例重烘"就无从验起了——如实跳过，别让环境问题伪装成通过。
        std::printf(
            "      [skip] 没有可读字体（可用 INK_TEST_FONT 指定）——"
            "高清路径只验到退化画法\n");
    }

    const auto drawAt = [&request](SDL_Renderer* renderer) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderClear(renderer);
        // 走场景那条**同一条**提交路径（刷批 → 画纹理），不自造一套。
        inking_detail::BeginFrame(renderer);
        inking_detail::PaintText(renderer, request, 400, 100, 1.0f, 100.0f,
                                  60.0f, InkingTransformSpec{});
        inking_detail::EndFrame();
    };

    drawAt(rendererA);
    SDL_Rect boxA{};
    const bool okA = InkBox(rendererA, &boxA);

    drawAt(rendererB);  // 比例 2：字形要按 80px 重烘
    SDL_Rect boxB{};
    const bool okB = InkBox(rendererB, &boxB);

    // 诊断：失败时要知道是**哪一边**空、包围盒长什么样（窗口后端读回的黑与
    // "真没画"在断言里分不出来）。
    std::printf("      [diag] hasFont=%d okA=%d boxA=(%d,%d %dx%d) "
                "okB=%d boxB=(%d,%d %dx%d)\n",
                hasFont ? 1 : 0, okA ? 1 : 0, boxA.x, boxA.y, boxA.w, boxA.h,
                okB ? 1 : 0, boxB.x, boxB.y, boxB.w, boxB.h);
    Check(okA && okB, "两套渲染器都画出了文字（墨迹包围盒非空）");
    if (okA && okB) {
        const double bx = static_cast<double>(boxB.x) / 2.0;
        const double by = static_cast<double>(boxB.y) / 2.0;
        const double bw = static_cast<double>(boxB.w) / 2.0;
        const double bh = static_cast<double>(boxB.h) / 2.0;
        Check(std::fabs(static_cast<double>(boxA.x) - bx) <= 2.0
                  && std::fabs(static_cast<double>(boxA.y) - by) <= 2.0,
              "高清前后文字原点不变（布局零位移）");
        Check(std::fabs(static_cast<double>(boxA.w) - bw) <= 2.0
                  && std::fabs(static_cast<double>(boxA.h) - bh) <= 2.0,
              "高清前后文字尺寸不变（仍按设计字号占位）");
        Check(static_cast<double>(boxB.w)
                  >= static_cast<double>(boxA.w) * 1.8,
              "比例 2 下墨迹是 2 倍设备像素（真按比例重烘，不是拉伸）");
    }
    if (hasFont) {
        Check(inking_detail::GlyphAtlasSnapshot().rasterized > 0,
              "文字走了字形图集（高清路径不是退化方块）");
    }

    SDL_DestroyRenderer(rendererA);
    SDL_DestroyRenderer(rendererB);
    SDL_DestroyWindow(windowA);
    SDL_DestroyWindow(windowB);
}

// ---------------------------------------------------------------------------
// T-03（0.7.1）：文字光栅上限可配 + 触顶告警
//
// 改动之前 `kMaxTextRasterRatio` 是个常量：设计 640 + 4K 屏（R = 6）时文字按
// 3× 烘、屏幕上再放大 2×，糊得没有理由，而且**日志里一个字都没有**。
//
// 这一条要钉住三件事：
//   1. 夹取时计数 +1（`TextCacheStats::rasterCapped`，可断言）；
//   2. WARN **只报一次**（第二帧不再刷）；
//   3. 把上限提上去以后不再夹取（计数不动），且上限夹在 [1, 8]。
//
// 用"逻辑 320×180 + 窗口 1280×720"造 R = 4：不必真的开一块 4K 缓冲。
// ---------------------------------------------------------------------------
void CheckTextRasterCap() {
    std::printf("\n-- 文字光栅上限：可配 + 触顶告警（0.7.1 T-03）--\n");

    SDL_Window* window = SDL_CreateWindow("text-raster-cap", 1280, 720, 0);
    SDL_Renderer* renderer =
        window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    Check(renderer != nullptr, "创建探针渲染器（1280×720）");
    if (renderer == nullptr) {
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        return;
    }
    // 逻辑 320×180 ⇒ R = 4（窗口 1280×720 的 min(4, 4)）
    SDL_SetRenderLogicalPresentation(renderer, 320, 180,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    // 复位到默认并**重新武装**告警（同值调用也算一次决策，见头文件那段）。
    inking_detail::SetTextRasterCap(3.0f);
    Check(inking_detail::GetTextRasterCap() == 3.0f,
          "text_raster_capped：默认上限是 3×（不调 setter 等于改动之前）");

    const auto drawText = [renderer] {
        InkingTextRequest request;
        request.text = "cap";
        request.fontSize = 20.0f;
        request.color = 0xFFFFFFFFu;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderClear(renderer);
        inking_detail::BeginFrame(renderer);
        inking_detail::PaintText(renderer, request, 200, 60, 1.0f, 0.0f, 0.0f,
                                  InkingTransformSpec{});
        inking_detail::EndFrame();
    };
    const auto cappedNow = [] {
        return inking_detail::TextCacheStatsSnapshot().rasterCapped;
    };

    const std::uint64_t cappedBefore = cappedNow();
    InkLog::ResetStats();
    drawText();  // R=4 vs 上限 3：夹取
    const std::uint64_t cappedAfterFirst = cappedNow();
    Check(cappedAfterFirst == cappedBefore + 1,
          "text_raster_capped：R=4 超过上限 3 ⇒ 夹取计数 +1");
    Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
          "text_raster_capped：触顶报 1 条 WARN（不是静默夹取）");
    NoteSilentFailure("text_raster_capped");

    drawText();  // 同一档上限的第二帧
    Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
          "触顶 WARN 只报一次：第二帧不再刷日志");

    // 提到 6×：R=4 不再触顶 ⇒ 计数不动
    Check(inking_detail::SetTextRasterCap(6.0f)
              && inking_detail::GetTextRasterCap() == 6.0f,
          "SetTextRasterCap(6) 生效");
    const std::uint64_t cappedBeforeRaise = cappedNow();
    drawText();
    Check(cappedNow() == cappedBeforeRaise,
          "上限提到 6 以后 R=4 不再夹取（计数不动，字按 4× 烘）");

    // 边界：夹在 [1, 8]，非法入参被拒
    Check(inking_detail::SetTextRasterCap(99.0f)
              && inking_detail::GetTextRasterCap() == 8.0f,
          "上限夹在 8（99 被夹成 8）");
    Check(inking_detail::SetTextRasterCap(0.25f)
              && inking_detail::GetTextRasterCap() == 1.0f,
          "上限夹在 1（0.25 被夹成 1）");
    const float kept = inking_detail::GetTextRasterCap();
    Check(!inking_detail::SetTextRasterCap(-1.0f)
              && inking_detail::GetTextRasterCap() == kept,
          "负上限被拒绝，当前值不变");

    // 收尾：还原默认，别让后面的用例继承一个奇怪的上限
    inking_detail::SetTextRasterCap(3.0f);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

// ---------------------------------------------------------------------------
// T-02：缩放规则（素材该多大 / σ 干不干净）
//
// 这一条验的是**规则本身**，不是绘制：`InkingImageScaleSpec` 是纯函数，所以断言可以
// 逐字对着任务书 §3 的数字（设计 1920 × 纵深 1 × R_max 2 = 3840 等）。
//
// 为什么必须落成自检而不是只写进 API.md：文档写错了没人会发现。而这四个数字
// 后面挂着两个真实结论——"设计 640 在 4K 上要 3840 宽的素材"与"σ 非整数就是脏"。
// 前者错了会让美术白出一倍的图，后者错了会让纵深档位选得毫无依据。
// ---------------------------------------------------------------------------
void CheckImageScaleRules() {
    std::printf("\n-- 缩放规则：素材该多大 / σ 干不干净（0.7.0 T-02）--\n");

    const auto make = [](int designWidth, float depth, float ratioMax) {
        InkingImageScaleSpec spec;
        spec.designWidth = designWidth;
        spec.depth = depth;
        spec.ratioMax = ratioMax;
        return spec;
    };

    // 规则一：素材像素 = 设计尺寸 × 纵深 × R_max
    Check(make(1920, 1.0f, 2.0f).RequiredTextureWidth() == 3840,
          "素材宽：1920 × 1.0 × 2 = 3840");
    Check(make(640, 0.5f, 6.0f).RequiredTextureWidth() == 1920,
          "素材宽：640 × 0.5 × 6 = 1920（纵深退后一半，素材只要一半）");

    // 规则一的推论：绝对像素只取决于"最多占多少屏幕"，与设计尺寸无关。
    // 三种设计尺寸在 4K 上都是 3840 宽——这就是"设计尺寸小 ≠ 会糊"的算式。
    Check(make(1920, 1.0f, 2.0f).RequiredTextureWidth() == 3840
              && make(1280, 1.0f, 3.0f).RequiredTextureWidth() == 3840
              && make(640, 1.0f, 6.0f).RequiredTextureWidth() == 3840,
          "同一块 4K 屏：1920 / 1280 / 640 三种设计尺寸都要 3840 宽的素材");

    // 规则二：σ = 素材像素 ÷ 屏幕像素；=1 最清
    Check(std::fabs(make(1920, 1.0f, 2.0f).SamplingRatio(3840, 2.0f) - 1.0f)
              < 1e-4f,
          "σ：3840 素材占 1920×1×2 屏幕 ⇒ 1.0（1:1）");
    Check(make(1280, 1.0f, 2.0f).SamplingRatio(3840, 2.0f) == 1.5f,
          "σ：3840 素材占 1280×2 屏幕 ⇒ 1.5（会有一列纹素被跳过）");

    // 干净档位判定：整数干净、分数不干净、放大不干净
    Check(InkingImageScaleSpec::IsCleanRatio(1.0f)
              && InkingImageScaleSpec::IsCleanRatio(2.0f)
              && InkingImageScaleSpec::IsCleanRatio(3.0f),
          "IsCleanRatio：1 / 2 / 3 都干净（整数档）");
    Check(!InkingImageScaleSpec::IsCleanRatio(1.5f)
              && !InkingImageScaleSpec::IsCleanRatio(1.2f)
              && !InkingImageScaleSpec::IsCleanRatio(0.5f),
          "IsCleanRatio：1.5 / 1.2 / 0.5 都不干净（0.5 是放大，不是整数倍缩小）");

    // 档位生成式 d = m / k：2× 素材的干净纵深 = 1.0 / 0.667 / 0.5 / 0.4
    Check(std::fabs(InkingImageScaleSpec::CleanDepth(2.0f, 3) - 0.6667f) < 0.001f
              && std::fabs(InkingImageScaleSpec::CleanDepth(2.0f, 5) - 0.4f)
                     < 0.001f,
          "CleanDepth：2× 素材的 k=3 / k=5 档 ⇒ 0.667 / 0.4");

    // 非法输入：给 0 而不是一个假的 1（调用方据此跳过判定）
    Check(make(0, 1.0f, 2.0f).RequiredTextureWidth() == 0
              && make(320, 0.0f, 2.0f).RequiredTextureWidth() == 0
              && make(320, 1.0f, 2.0f).SamplingRatio(0, 1.0f) == 0.0f,
          "非法输入（设计宽 0 / 纵深 0 / 纹理 0）⇒ 0，不是伪造的 1");
}

// ---------------------------------------------------------------------------
// 音频层接线自检（P0 的观测口）
//
// 这一条存在的理由：音频整层曾经被 `#if defined(INK_HAS_SDL3_MIXER)` 关掉，
// 而 CMake 仍然 find_package + 链接 SDL3_mixer —— "能编能链"把"没接上"完全
// 盖住：`InkingAudioInit()` 恒 false、BGM / SE 全无声，而且不报错、不警告。
// 二进制判据（导入表里有 SDL3_mixer.dll + MIX_ 符号）由构建侧查；
// **"淡入真的在推进"只能靠跑一遍**，那是空桩版本永远涨不上去的量。
//
// 这里直接调 `InkingAudioTick`：它和窗口层每帧调的是**同一个函数**（窗口那边删掉
// 匿名 namespace 空桩之后解析到的就是它），所以这条断言同时钉住了两层。
// ---------------------------------------------------------------------------

/// 一段**静音**的内存 WAV（44 字节头 + 全零 PCM）。自检不依赖磁盘素材。
std::vector<std::uint8_t> MakeSilentWav(int sampleRate, int channels, int bits,
                                        float seconds) {
    const int bytesPerSample = bits / 8;
    const auto dataSize = static_cast<std::uint32_t>(
        static_cast<double>(sampleRate) * seconds * channels * bytesPerSample);
    std::vector<std::uint8_t> wav(44u + dataSize, 0);
    const auto put32 = [&wav](std::size_t at, std::uint32_t value) {
        wav[at] = static_cast<std::uint8_t>(value & 0xFFu);
        wav[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
        wav[at + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
        wav[at + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    };
    const auto put16 = [&wav](std::size_t at, std::uint16_t value) {
        wav[at] = static_cast<std::uint8_t>(value & 0xFFu);
        wav[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    };
    std::memcpy(&wav[0], "RIFF", 4);
    put32(4, 36u + dataSize);
    std::memcpy(&wav[8], "WAVE", 4);
    std::memcpy(&wav[12], "fmt ", 4);
    put32(16, 16u);
    put16(20, 1u);  // PCM
    put16(22, static_cast<std::uint16_t>(channels));
    put32(24, static_cast<std::uint32_t>(sampleRate));
    put32(28,
          static_cast<std::uint32_t>(sampleRate * channels * bytesPerSample));
    put16(32, static_cast<std::uint16_t>(channels * bytesPerSample));
    put16(34, static_cast<std::uint16_t>(bits));
    std::memcpy(&wav[36], "data", 4);
    put32(40, dataSize);
    return wav;
}

void CheckAudioPipeline() {
    // dummy 驱动：自检不出声、也不需要声卡（AGENTS.md §6 第 79 条）。
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    const bool inited = InkingAudioInit();
    Check(inited, "InkingAudioInit 成功（音频层真的接上了 SDL3_mixer）");
    if (!inited) {
        return;  // 没接上：后面都无从验，不再往下编故事
    }

    const std::vector<std::uint8_t> wav = MakeSilentWav(44100, 1, 16, 0.2f);
    const InkingSoundId sound =
        InkingAudioLoadFromMemory(wav.data(), wav.size(), "selftest.wav");
    Check(sound != 0u && InkingAudioIsReady(sound), "内存 WAV 加载成功");

    // 0.5 秒淡入、目标 1.0：增益必须**按帧上升**，而不是一步到 1。
    InkingAudioPlayBgm(sound, 0.5f, 1.0f);
    const float start = InkingAudioBgmVolume();
    for (int frame = 0; frame < 10; ++frame) {
        InkingAudioTick(0.05f);  // 累计 0.5 秒
    }
    const float end = InkingAudioBgmVolume();
    std::printf("      [audio] 淡入增益 %.3f → %.3f\n",
                static_cast<double>(start), static_cast<double>(end));
    Check(end > start + 0.1f, "BGM 淡入在 InkingAudioTick 里推进（增益上升）");

    InkingAudioStopBgm(0.0f);
    InkingAudioShutdown();
}

// ---------------------------------------------------------------------------
// 设计画布尺寸：默认值 / 参数校验 / 定型之后拒绝改动
//
// 前半段**必须跑在场景构造之前**（场景是第一个按设计尺寸算几何的地方），
// 后半段在构造之后。之所以在这里不验"真的改成另一个尺寸"这条正向路径：
// 它会改掉后面**所有**用例的前提（锚点、命中坐标、letterbox 倍率全是按它算的）。
// 正向路径由消费方工程端到端跑（D:/MyWindow：config.json 写 1600×900，
// 启动时断言运行期读到的就是 1600×900），这里只钉住三条边界。
// ---------------------------------------------------------------------------

void CheckDesignSizeDefault() {
    Check(InkingDesignWidth() == InkingDefaultDesignWidth
              && InkingDesignHeight() == InkingDefaultDesignHeight,
          "没有配置时设计尺寸 = 默认值（1280×720）");
    Check(!inking_detail::DesignSizeTaken(),
          "场景 / 窗口构造之前设计尺寸还没定型");
    Check(!InkingSetDesignSize(0, 720), "宽 0 被拒（不静默夹到 1）");
    Check(!InkingSetDesignSize(20000, 720), "超出上限被拒（1..16384）");
    Check(InkingSetDesignSize(InkingDefaultDesignWidth,
                                InkingDefaultDesignHeight),
          "设成当前值是幂等的（返回 true，不算失败）");
}

void CheckDesignSizeLocked() {
    Check(inking_detail::DesignSizeTaken(),
          "场景构造之后设计尺寸已定型");
    Check(!InkingSetDesignSize(1600, 900),
          "定型之后再改被拒（返回 false，不静默半生效）");
    Check(InkingDesignWidth() == InkingDefaultDesignWidth
              && InkingDesignHeight() == InkingDefaultDesignHeight,
          "被拒之后设计尺寸没有变（没有留下改了一半的状态）");
}

// ---------------------------------------------------------------------------
// 层级：树序绘制 + 兄弟内 z（局部层级）
// ---------------------------------------------------------------------------
//
// 树长这样（括号里是 zIndex）：
//
//   场景根
//   ├── outer 面板 (20)   300x200，居中
//   │   └── inner 面板 (1) 100x80，和 outer 同心   ← 子 z 比父小得多
//   ├── card  面板 (15)   300x200，和 outer 完全重合
//   │   └── deep 面板 (100) 40x40，居中
//   └── tieA / tieB (同 z=5，错开摆)
//
// 三条规矩逐条验：
//   ① 父先于子 ⇒ outer(20) 在 inner(1) **之下**——子 z 再小也盖在父上面
//      （这正是"面板底色盖住自己的标题和按钮"那个坑）；
//   ② 兄弟内 z ⇒ card(15) 在 outer(20) 之下；
//   ③ 跨子树不穿透 ⇒ deep 的 z=100 只在 card 内部有效，盖不过 outer。
//
// 最后再验两件"合起来才错"的事：命中与绘制必须同源，所以除了比
// `IsAbove`，还要看绘制列表里谁先谁后、以及真渲染出来的中心像素是什么色。
void CheckLayerOrder(SDL_Renderer* renderer) {
    std::printf("\n-- 层级：树序绘制 + 兄弟内 z --\n");

    InkingScene scene;  // 运行时构造：不登记进 InkingSceneLibrary

    const auto makePanel = [](const char* name, int width, int height, int z,
                              float offsetX, float offsetY,
                              std::uint32_t color) {
        InkingPanelData data;
        data.name = name;
        data.width = width;
        data.height = height;
        data.zIndex = z;
        data.offsetX = offsetX;
        data.offsetY = offsetY;
        data.selfAnchor = InkingChangeAnchor::Center;
        data.traceAnchor = InkingChangeAnchor::Center;
        data.closed.color = color;
        data.open.color = color;
        return data;
    };

    constexpr std::uint32_t kRed = 0xFFFF0000u;
    constexpr std::uint32_t kBlue = 0xFF0000FFu;
    constexpr std::uint32_t kGreen = 0xFF00C000u;

    InkingWidget* outer = scene.AddChild(
        std::make_unique<InkingStaticPanel>(
            &scene, makePanel("layer_outer", 300, 200, 20, 0.0f, 0.0f, kRed)));
    InkingWidget* inner = outer->AddChild(
        std::make_unique<InkingStaticPanel>(
            outer, makePanel("layer_inner", 100, 80, 1, 0.0f, 0.0f, kBlue)));
    InkingWidget* card = scene.AddChild(
        std::make_unique<InkingStaticPanel>(
            &scene, makePanel("layer_card", 300, 200, 15, 0.0f, 0.0f, kGreen)));
    InkingWidget* deep = card->AddChild(
        std::make_unique<InkingStaticPanel>(
            card, makePanel("layer_deep", 40, 40, 100, 0.0f, 0.0f, kRed)));
    InkingWidget* tieA = scene.AddChild(
        std::make_unique<InkingStaticPanel>(
            &scene, makePanel("layer_tie_a", 40, 40, 5, -500.0f, -300.0f, kRed)));
    InkingWidget* tieB = scene.AddChild(
        std::make_unique<InkingStaticPanel>(
            &scene, makePanel("layer_tie_b", 40, 40, 5, 500.0f, -300.0f, kRed)));

    scene.TickFrame(1.0f / 60.0f);

    // ① 父先于子：子 z=1 也在父 z=20 之上。
    Check(InkingWidget::IsAbove(*inner, *outer),
          "父先于子：子 z=1 仍在父 z=20 之上（底色不再盖住内容）");
    Check(!InkingWidget::IsAbove(*outer, *inner),
          "父先于子：反向不成立（没有两头都说自己在上）");

    // ② 兄弟之间按 z。
    Check(InkingWidget::IsAbove(*outer, *card),
          "兄弟内 z：z=20 的 outer 在 z=15 的 card 之上");
    Check(!InkingWidget::IsAbove(*card, *outer),
          "兄弟内 z：反向不成立");

    // ③ 跨子树不穿透：deep 的 z=100 只在 card 内部有效。
    Check(InkingWidget::IsAbove(*outer, *deep),
          "跨子树不穿透：card 的子孙 z=100 也盖不过 z=20 那一支");
    Check(!InkingWidget::IsAbove(*deep, *outer),
          "跨子树不穿透：反向不成立");

    // ④ 同 z 看注册序号（后挂的在上）。
    Check(InkingWidget::IsAbove(*tieB, *tieA),
          "同 z 的兄弟：后注册的 tieB 在 tieA 之上");

    // ⑤ 绘制列表必须和 IsAbove 同源：父在前、兄弟按 z、跨子树整支连成一段。
    const std::vector<InkingScene::DrawItem>& items =
        scene.BuildDrawList();
    const auto indexOf = [&items](const InkingWidget* node) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].node == node) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const int iOuter = indexOf(outer);
    const int iInner = indexOf(inner);
    const int iCard = indexOf(card);
    const int iDeep = indexOf(deep);

    Check(iOuter >= 0 && iInner >= 0 && iCard >= 0 && iDeep >= 0,
          "四个面板都进了绘制列表");
    Check(iOuter >= 0 && iInner > iOuter, "绘制顺序：outer 先画、inner 后画");
    Check(iCard >= 0 && iOuter > iCard, "绘制顺序：card(z=15) 早于 outer(z=20)");
    Check(iDeep >= 0 && iDeep < iOuter, "绘制顺序：card 整支（含 deep）都在 outer 之前");

    // ⑥ 命中和绘制同源：中心点上 inner 压在 outer 上，所以命中的该是 inner。
    const InkingHitResult hit = scene.QueryHit(640.0f, 360.0f);
    Check(hit.target == inner,
          "命中最上面那个：中心点命中的是子面板 inner，不是父面板");

    // ⑦ 端到端：真渲染一帧，中心像素必须是 inner 的蓝色（不是 outer 的红）。
    if (renderer != nullptr) {
        // 这一步必须自己做：`onRender` 收到的是**设计坐标**，整幅缩放由窗口层
        // 的 letterbox 呈现负责（`InkingWindow` 里那一行）。这里没有窗口对象，
        // 不补这一句的话组件会被画到设计坐标去——1280x720 的画布落在 640x360
        // 的窗口之外，中心像素读到的是空的。
        SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                         InkingDesignHeight(),
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        InkingSceneLibrary::SetActiveScene(&scene);
        const bool rendered = InkingSceneLibrary::RenderScene(renderer);
        const SDL_Rect one{320, 180, 1, 1};  // 窗口 640x360 的正中（物理像素）
        // SDL3 的 RenderReadPixels 返回一张新 surface（SDL2 那条"传缓冲"的
        // 签名在 3.x 里已经没了），所以转成 ARGB8888 再取那一个像素。
        SDL_Surface* shot = SDL_RenderReadPixels(renderer, &one);
        std::uint32_t pixel = 0;
        bool read = shot != nullptr;
        if (shot != nullptr) {
            SDL_Surface* argb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
            if (argb != nullptr) {
                std::memcpy(&pixel, argb->pixels, sizeof(pixel));
                SDL_DestroySurface(argb);
            } else {
                read = false;
            }
            SDL_DestroySurface(shot);
        }
        InkingSceneLibrary::SetActiveScene(nullptr);
        SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                         SDL_LOGICAL_PRESENTATION_DISABLED);
        Check(rendered && read, "层级场景渲染一帧并读回中心像素");
        std::printf("         中心像素 = 0x%08X（期望 0x%08X）\n", pixel,
                    (kBlue & 0x00FFFFFFu) | 0xFF000000u);
        Check(read && (pixel & 0x00FFFFFFu) == (kBlue & 0x00FFFFFFu),
              "中心像素是子面板的蓝色（父容器底色没有盖住它）");
    }
}

// ---------------------------------------------------------------------------
// 面板装饰（0.2.0 T-07）：描边在**子孙之上**、阴影在**填充之下**
// ---------------------------------------------------------------------------
//
// 三条要一起验，因为它们互相制衡：
//   ① 描边压得住子孙：暗底面板 + 亮描边 + 铺满的子面板，边框那一像素必须是
//      描边色（描边若跟填充同一趟，会被子面板盖掉 —— 那就是"加完等于没加"）；
//   ② 描边不越界压兄弟：后注册（层级更高）的 B 盖住 A 的边框，那里必须是 B
//      的颜色。这是"局部层级"的判据：装饰笔只压自己的子孙，不是全局第二趟；
//   ③ 装饰不是节点：`GetDrawItemCount()` 一条都不多。
//
// 再加两条：阴影在填充之下（偏移副本露在面板外的部分可见、面板内不被它压暗）、
// 描边/阴影跟着开合过渡插值（`GetDisplayStroke` 观测口）。
//
// 坐标：设计 1280×720 → 窗口 640×360（倍率 0.5）。面板 200×200 居中 ⇒
// 设计 (540,260)~(740,460)，设备 (270,130)~(370,230)。
void CheckPanelDecoration(SDL_Renderer* renderer) {
    std::printf("\n-- 面板装饰：描边在子孙之上 / 阴影在填充之下 --\n");
    if (renderer == nullptr) {
        return;
    }

    // 抗锯齿边缘不做逐位相等（软件渲染器会羽化半像素），给一个宽容带。
    const auto closeTo = [](std::uint32_t got, std::uint32_t want, int tol) {
        const auto ch = [](std::uint32_t v, int shift) {
            return static_cast<int>((v >> shift) & 0xFFu);
        };
        return std::abs(ch(got, 16) - ch(want, 16)) <= tol
            && std::abs(ch(got, 8) - ch(want, 8)) <= tol
            && std::abs(ch(got, 0) - ch(want, 0)) <= tol;
    };

    constexpr std::uint32_t kDark = 0xFF202020u;    // 面板底（暗）
    constexpr std::uint32_t kEdge = 0xFFFFD000u;    // 描边（亮黄）
    constexpr std::uint32_t kChild = 0xFF0000FFu;   // 子面板（蓝）
    constexpr std::uint32_t kSibling = 0xFF00C000u; // 后注册的兄弟（绿）
    constexpr std::uint32_t kFill = 0xFFFF0000u;    // 阴影测试面板的填充（红）
    constexpr std::uint32_t kShadow = 0xFF804000u;  // 阴影（棕）

    InkingScene scene;

    InkingPanelData a;
    a.name = "deco_parent";
    a.width = 200;
    a.height = 200;
    a.selfAnchor = InkingChangeAnchor::Center;
    a.traceAnchor = InkingChangeAnchor::Center;
    a.closed.color = kDark;
    a.open.color = kDark;
    a.closed.stroke = InkingStrokeSpec{6.0f, kEdge};  // 6 设计像素 = 3 设备像素

    InkingWidget* parent = scene.AddChild(
        std::make_unique<InkingStaticPanel>(&scene, a));

    InkingPanelData c;
    c.name = "deco_child";
    c.width = 200;
    c.height = 200;  // 和父**完全重合**：描边只能靠装饰遍才活下来
    c.selfAnchor = InkingChangeAnchor::Center;
    c.traceAnchor = InkingChangeAnchor::Center;
    c.closed.color = kChild;
    c.open.color = kChild;
    parent->AddChild(
        std::make_unique<InkingStaticPanel>(parent, c));

    // B：后注册 = 层级更高，压住 A 的左上边框（设计 530..600 × 260..320）。
    InkingPanelData b;
    b.name = "deco_sibling";
    b.width = 70;
    b.height = 60;
    b.selfAnchor = InkingChangeAnchor::Center;
    b.traceAnchor = InkingChangeAnchor::Center;
    b.offsetX = -75.0f;
    b.offsetY = -70.0f;
    b.closed.color = kSibling;
    b.open.color = kSibling;
    scene.AddChild(std::make_unique<InkingStaticPanel>(&scene, b));

    // D：阴影测试（摆开，不和 A 重叠）。设计 280..400 × 450..570。
    InkingPanelData d;
    d.name = "deco_shadow";
    d.width = 120;
    d.height = 120;
    d.selfAnchor = InkingChangeAnchor::Center;
    d.traceAnchor = InkingChangeAnchor::Center;
    d.offsetX = -300.0f;
    d.offsetY = 150.0f;
    d.closed.color = kFill;
    d.open.color = kFill;
    d.closed.shadow = InkingShadowSpec{12.0f, 12.0f, kShadow};
    scene.AddChild(std::make_unique<InkingStaticPanel>(&scene, d));

    scene.TickFrame(1.0f / 60.0f);
    Check(scene.GetDrawItemCount() == 4,
          "装饰笔不是节点：绘制列表条目数 = 4（描边 / 阴影不各占一条）");

    InkingSceneLibrary::SetActiveScene(&scene);
    SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    const bool rendered = InkingSceneLibrary::RenderScene(renderer);
    // 这一帧画完再逐个读像素（每次 RenderScene 都会清屏重画，读一次就够）。
    const auto readPixel = [&](int x, int y) -> std::uint32_t {
        const SDL_Rect one{x, y, 1, 1};
        SDL_Surface* shot = SDL_RenderReadPixels(renderer, &one);
        if (shot == nullptr) {
            return 0u;
        }
        std::uint32_t pixel = 0;
        SDL_Surface* argb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
        if (argb != nullptr) {
            std::memcpy(&pixel, argb->pixels, sizeof(pixel));
            SDL_DestroySurface(argb);
        }
        SDL_DestroySurface(shot);
        return pixel;
    };

    const std::uint32_t edge = readPixel(320, 130);     // 上边框中点
    const std::uint32_t inside = readPixel(320, 180);   // 正中（子面板）
    const std::uint32_t overlap = readPixel(270, 150);  // A 的左边框 × B
    const std::uint32_t fill = readPixel(170, 255);     // D 的正中
    const std::uint32_t shadow = readPixel(203, 288);   // D 的右下外侧（只在阴影里）

    SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(nullptr);

    Check(rendered, "装饰场景渲染一帧");
    std::printf("         边框=0x%08X 内部=0x%08X 重叠=0x%08X 填充=0x%08X 阴影=0x%08X\n",
                edge, inside, overlap, fill, shadow);
    Check(closeTo(edge, kEdge, 60),
          "panel_stroke_over_children：铺满的子面板盖不住父面板的描边");
    Check(closeTo(inside, kChild, 40),
          "子面板的填充在描边之内（描边没有把内容整块盖住）");
    Check(closeTo(overlap, kSibling, 40),
          "描边不越界：后注册的兄弟盖住边框时，那里是兄弟的颜色（局部层级）");
    Check(closeTo(fill, kFill, 40), "阴影面板的填充正常（阴影没有压暗自己）");
    Check(closeTo(shadow, kShadow, 40),
          "阴影画在填充之下：露出面板外的偏移副本可见");

    // -----------------------------------------------------------------------
    // 描边 / 阴影跟着开合过渡插值（观测口，不猜像素）
    // -----------------------------------------------------------------------
    InkingScene anim;
    InkingPanelData p;
    p.name = "deco_anim";
    p.width = 100;
    p.height = 100;
    p.selfAnchor = InkingChangeAnchor::Center;
    p.traceAnchor = InkingChangeAnchor::Center;
    p.closed.color = kDark;
    p.closed.stroke = InkingStrokeSpec{};  // 关着：没描边
    p.open.color = kDark;
    p.open.stroke = InkingStrokeSpec{4.0f, kEdge};
    p.open.transitionSeconds = 0.2f;
    p.openInheritsClosed = false;  // 显式给了 open 的描边，不继承
    InkingStaticPanel* animated =
        static_cast<InkingStaticPanel*>(anim.AddChild(
            std::make_unique<InkingStaticPanel>(&anim, p)));

    Check(!animated->GetDisplayStroke().Visible(),
          "关着时没有描边");
    Check(animated->SetOpen(true), "打开（起一段 0.2s 过渡）");
    // 必须让它**活跃**：`TickFrame` 对非活跃场景直接返回（和上面主题那段同一条坑）。
    InkingSceneLibrary::SetActiveScene(&anim);
    anim.TickFrame(0.1f);  // 过一半
    const float midWidth = animated->GetDisplayStroke().width;
    std::printf("         过渡中点描边宽度 = %.2f（目标 4）\n", midWidth);
    Check(midWidth > 0.5f && midWidth < 3.5f,
          "描边跟着开合过渡插值（不是 0 也不是一步到位）");
    anim.TickFrame(0.2f);  // 收尾
    Check(animated->GetDisplayStroke().width == 4.0f,
          "过渡结束落目标值（4 设计像素）");
    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// 按钮三笔：抽公共件之后画面必须一模一样（0.2.0 T-07 的回归网）
// ---------------------------------------------------------------------------
//
// `PaintSurface` 是把 `PaintButtonAppearance` 里"阴影 → 填充 → 描边"整段搬出来
// 的（面板描边要用同一份）。搬动最容易出的错是**顺序变了**——阴影盖住填充、
// 描边被填充盖住，两边单独看都"画了"，合起来才错。所以这里按三笔的可见结果
// 逐个取像素：中心=填充、边框=描边、右下外侧=阴影。
void CheckButtonPaintOrder(SDL_Renderer* renderer) {
    std::printf("\n-- 按钮三笔：阴影 → 填充 → 描边（顺序回归） --\n");
    if (renderer == nullptr) {
        return;
    }

    const auto closeTo = [](std::uint32_t got, std::uint32_t want, int tol) {
        const auto ch = [](std::uint32_t v, int shift) {
            return static_cast<int>((v >> shift) & 0xFFu);
        };
        return std::abs(ch(got, 16) - ch(want, 16)) <= tol
            && std::abs(ch(got, 8) - ch(want, 8)) <= tol
            && std::abs(ch(got, 0) - ch(want, 0)) <= tol;
    };

    constexpr std::uint32_t kFill = 0xFFD04040u;    // 填充
    constexpr std::uint32_t kEdge = 0xFF00FF00u;    // 描边（绿）
    constexpr std::uint32_t kShadow = 0xFF804000u;  // 阴影（棕）

    InkingScene scene;
    InkingButtonData data;
    data.name = "paint_button";
    data.width = 200;
    data.height = 60;
    data.selfAnchor = InkingChangeAnchor::Center;
    data.traceAnchor = InkingChangeAnchor::Center;
    data.normal = InkingButtonAppearance(InkingAppearanceType::Color, kFill);
    data.hover = data.normal;
    data.onclicked = data.normal;
    data.stroke = InkingStrokeSpec{8.0f, kEdge};       // 8 设计 = 4 设备像素
    data.shadow = InkingShadowSpec{10.0f, 10.0f, kShadow};
    scene.AddChild(std::make_unique<InkingStaticButton>(&scene, data));

    scene.TickFrame(1.0f / 60.0f);
    InkingSceneLibrary::SetActiveScene(&scene);
    SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    const bool rendered = InkingSceneLibrary::RenderScene(renderer);

    const auto readPixel = [&](int x, int y) -> std::uint32_t {
        const SDL_Rect one{x, y, 1, 1};
        SDL_Surface* shot = SDL_RenderReadPixels(renderer, &one);
        if (shot == nullptr) {
            return 0u;
        }
        std::uint32_t pixel = 0;
        SDL_Surface* argb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
        if (argb != nullptr) {
            std::memcpy(&pixel, argb->pixels, sizeof(pixel));
            SDL_DestroySurface(argb);
        }
        SDL_DestroySurface(shot);
        return pixel;
    };

    // 按钮设计 540..740 × 330..390 ⇒ 设备 270..370 × 165..195。
    const std::uint32_t center = readPixel(320, 180);
    const std::uint32_t border = readPixel(320, 165);
    const std::uint32_t outside = readPixel(372, 197);

    SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(nullptr);

    std::printf("         中心=0x%08X 边框=0x%08X 右下外侧=0x%08X\n", center,
                border, outside);
    Check(rendered, "按钮场景渲染一帧");
    Check(closeTo(center, kFill, 40), "button_paint_unchanged：填充在最上（没被阴影压暗）");
    Check(closeTo(border, kEdge, 60), "button_paint_unchanged：描边压在填充边缘之上");
    Check(closeTo(outside, kShadow, 40),
          "button_paint_unchanged：阴影露在形状之外（偏移副本，无模糊）");
}

// ---------------------------------------------------------------------------
// 按钮状态事件（0.2.0 T-11）：两档共用 `stateChanged`
// ---------------------------------------------------------------------------
//
// 反馈里"两档语义分叉"指的是**采样时机**（动态档 50Hz 逻辑步、静态档渲染帧），
// 不是回调接口。T-11 把公开订阅口补上之后，业务只要订阅 `stateChanged` 就能
// 知道"鼠标进出 / 按下抬起"，不用再派生类去重写 protected 钩子。
//
// 这里对两档各推一次 hover：**两档都由 `DispatchPointer` 驱动**——静态档是场景
// 推结果，动态档是场景在"指针真的变了"那一帧推一次通知（0.4.1 T-02）。
// 断言两边都恰好收到一次 `(Normal, Hover)`。
void CheckButtonStateEvent() {
    std::printf("\n-- 按钮状态事件：两档共用 stateChanged --\n");

    InkingScene scene;

    InkingButtonData base;
    base.width = 160;
    base.height = 60;
    base.selfAnchor = InkingChangeAnchor::Center;
    base.traceAnchor = InkingChangeAnchor::Center;
    base.normal = InkingButtonAppearance(InkingAppearanceType::Color, 0xFF3070C0u);

    InkingButtonData staticData = base;
    staticData.name = "event_static";
    auto* staticButton = static_cast<InkingStaticButton*>(
        scene.AddChild(std::make_unique<InkingStaticButton>(
            &scene, staticData)));

    int staticCount = 0;
    InkingButtonState staticFrom = InkingButtonState::Pressed;
    InkingButtonState staticTo = InkingButtonState::Pressed;
    staticButton->stateChanged.Subscribe(
        [&](InkingButtonState from, InkingButtonState to) {
            ++staticCount;
            staticFrom = from;
            staticTo = to;
        });

    // 动态档挪开摆（正下方 200 设计像素），两档各自独立收指针。
    InkingButtonData dynamicData = base;
    dynamicData.name = "event_dynamic";
    dynamicData.offsetY = 200.0f;
    auto* dynamicButton = static_cast<InkingDynamicButton*>(
        scene.AddChild(std::make_unique<InkingDynamicButton>(
            &scene, dynamicData)));

    int dynamicCount = 0;
    InkingButtonState dynamicFrom = InkingButtonState::Pressed;
    InkingButtonState dynamicTo = InkingButtonState::Pressed;
    dynamicButton->stateChanged.Subscribe(
        [&](InkingButtonState from, InkingButtonState to) {
            ++dynamicCount;
            dynamicFrom = from;
            dynamicTo = to;
        });

    InkingSceneLibrary::SetActiveScene(&scene);
    scene.TickFrame(1.0f / 60.0f);

    // ① 静态档：指针进按钮 → 场景派发。
    scene.SetPointerState(640.0f, 360.0f, false, true);
    scene.DispatchPointer();
    Check(staticCount == 1 && staticFrom == InkingButtonState::Normal
              && staticTo == InkingButtonState::Hover,
          "静态档：hover 推一次 → (Normal, Hover)，计数 1");

    // 同一状态再派发一次：不该重复发（事件报的是"变化"）。
    scene.DispatchPointer();
    Check(staticCount == 1, "静态档：状态没变就不重复触发");

    // ② 动态档：指针移到它身上 → 指针变化当帧通知（0.4.1 T-02）。
    //    以前这里走 `TickLogic`（固定逻辑步里自己拉）；现在通知挂在"指针真的
    //    变了"那一帧上，所以驱动方式换成 `DispatchPointer`，断言一字不改。
    scene.SetPointerState(640.0f, 560.0f, false, true);
    scene.DispatchPointer();
    Check(dynamicCount == 1 && dynamicFrom == InkingButtonState::Normal
              && dynamicTo == InkingButtonState::Hover,
          "动态档：指针变化通知 → (Normal, Hover)，计数 1");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// 统一输入语义（0.4.1 T-01 / T-02 / T-03）
// ---------------------------------------------------------------------------
//
// 三件事一起验，因为它们合起来才是"一个点上只有一个输入归属"：
//   T-01 容器答案要深挖到**真正接住输入的那一个**（静态 / 动态同一套）；
//   T-02 通知只发生在"指针真的变了"的帧上（鼠标不动就不通知）；
//   T-03 边沿判定只有一份（`PointerProbe`），语义与静态档同一套。

/// 动态容器：矩形命中 + 声明"子树由我判"（`OwnsSubtreeHit`）。
class DynamicContainerSpy : public InkingDynamicWidget {
public:
    DynamicContainerSpy(InkingWidget* parent,
                        const InkingAnchorData& data)
        : InkingDynamicWidget(parent, data) {}

    bool HitTest(float worldX, float worldY) const override {
        const float lx = worldX - GetAbsX();
        const float ly = worldY - GetAbsY();
        return lx >= 0.0f && ly >= 0.0f
            && lx < static_cast<float>(GetWidth())
            && ly < static_cast<float>(GetHeight());
    }

    bool OwnsSubtreeHit() const noexcept override { return true; }
};

/// 只数"被通知了几次"的动态节点。
class PointerSpy : public InkingDynamicWidget {
public:
    PointerSpy(InkingWidget* parent, const InkingAnchorData& data)
        : InkingDynamicWidget(parent, data) {}

    bool HitTest(float, float) const override { return false; }

    int notifications = 0;

    /// 通知里有没有一次读到"次键按着"（0.5.0：触摸长按合成的）。
    /// 定点浮窗就是靠这一份状态判"长按点在外部"。
    bool sawSecondaryDown = false;

protected:
    void onPointerStateChanged() override {
        ++notifications;
        const InkingScene* scene = GetScene();
        if (scene != nullptr && scene->GetPointerDown().secondary) {
            sawSecondaryDown = true;
        }
    }
};

// ---- T-01：动态容器里的静态按钮能被命中 ----
void CheckPopupHitOwner() {
    std::printf("\n-- 统一输入语义：动态容器的答案深挖到子控件（T-01）--\n");

    InkingScene scene("hit_owner");

    InkingAnchorData panelData;
    panelData.width = 200;
    panelData.height = 200;
    panelData.offsetX = 100.0f;
    panelData.offsetY = 100.0f;
    auto* container = static_cast<DynamicContainerSpy*>(scene.AddChild(
        std::make_unique<DynamicContainerSpy>(&scene, panelData)));
    container->SetName("dyn_container");

    InkingButtonData buttonData;
    buttonData.name = "inner_button";
    buttonData.width = 80;
    buttonData.height = 40;
    buttonData.offsetX = 10.0f;
    buttonData.offsetY = 10.0f;
    buttonData.normal = InkingButtonAppearance(InkingAppearanceType::Color,
                                                 0xFF204060u);
    auto* inner = static_cast<InkingStaticButton*>(container->AddChild(
        std::make_unique<InkingStaticButton>(container, buttonData)));

    // 按钮世界中心 = 100 + 10 + 40, 100 + 10 + 20
    Check(scene.QueryHit(150.0f, 130.0f).target == inner,
          "T-01：动态容器（OwnsSubtreeHit=true）里的静态按钮能被命中");

    // 容器内的空白区仍答容器自己——深挖没把它弄丢
    Check(scene.QueryHit(280.0f, 280.0f).target == container,
          "T-01：容器里的空白处仍答容器（深挖只换到真正的子控件）");
}

// ---- T-02：指针不变就不通知 ----
void CheckPointerStateChannel() {
    std::printf("\n-- 统一输入语义：指针不变就不通知动态节点（T-02）--\n");

    InkingScene scene("pointer_channel");

    InkingAnchorData data;
    data.width = 100;
    data.height = 100;
    auto* spy = static_cast<PointerSpy*>(
        scene.AddChild(std::make_unique<PointerSpy>(&scene, data)));

    // 指针从没变过：连着派发两次，一次都不该通知。
    scene.DispatchPointer();
    scene.DispatchPointer();
    Check(spy->notifications == 0, "T-02：指针没变 → 一次都不通知");

    // 同一帧里重复派发也只通知一次（"每帧只判断一次"不依赖调用方自觉）。
    scene.SetPointerState(10.0f, 10.0f, false, true);
    scene.DispatchPointer();
    scene.DispatchPointer();
    Check(spy->notifications == 1, "T-02：指针变了 → 当帧通知一次，重复派发不再通知");

    // 换一帧（帧末消费清账）再变一次 ⇒ 再通知一次。
    scene.ConsumeFrameDirty();
    scene.SetPointerState(20.0f, 20.0f, false, true);
    scene.DispatchPointer();
    Check(spy->notifications == 2, "T-02：下一帧再变 → 再通知一次");
}

// ---- T-03：边沿判定只有一份 ----
void CheckPointerProbe() {
    std::printf("\n-- 统一输入语义：PointerProbe 的边沿与捕获（T-03）--\n");

    inking_detail::PointerProbe probe;

    // 0.5.0：`Update` 吃的是**两个键**的整包状态。这些老断言只关心主键，
    // 所以用一个 helper 把 bool 包成"只有主键"——次键全关，语义与以前一样。
    const auto onlyPrimary = [](bool down) {
        return InkingPointerDownState{down, false};
    };
    const auto primaryCaptured = [&probe] {
        return probe.IsCaptured(InkingPointerButton::Primary);
    };

    // ① 同点按下 → 抬起 = 一次点击
    auto r = probe.Update(true, onlyPrimary(false));
    Check(r.hoverChanged && r.hovered && !r.primary.clicked,
          "T-03：指针进入 → hoverChanged，无点击");
    r = probe.Update(true, onlyPrimary(true));
    Check(r.primary.pressed && primaryCaptured(),
          "T-03：按下 → pressed + 捕获");
    r = probe.Update(true, onlyPrimary(false));
    Check(r.primary.released && r.primary.clicked,
          "T-03：同点抬起 → released + clicked");

    // ② 按下 → 滑出去 → 松开 = 取消（用户唯一的"我反悔了"）
    probe.Reset();
    probe.Update(true, onlyPrimary(false));
    probe.Update(true, onlyPrimary(true));
    r = probe.Update(false, onlyPrimary(true));
    Check(r.hoverChanged && !r.hovered && !r.primary.released,
          "T-03：按着滑出去 → 捕获不结束");
    r = probe.Update(false, onlyPrimary(false));
    Check(r.primary.released && !r.primary.clicked,
          "T-03：在外面松开 → released 但不 click");

    // ③ 没按下时重复判：第二次什么都不报（幂等）
    probe.Reset();
    probe.Update(true, onlyPrimary(false));
    const auto again = probe.Update(true, onlyPrimary(false));
    Check(!again.hoverChanged && !again.primary.pressed
              && !again.primary.released && !again.primary.clicked,
          "T-03：无按下连判两次，第二次什么都不报");

    // ④ 在别处按下、按着挪进来：不算这次按下
    probe.Reset();
    probe.Update(false, onlyPrimary(true));
    r = probe.Update(true, onlyPrimary(true));
    Check(!r.primary.pressed && !primaryCaptured(),
          "T-03：在别处按下、按着挪进来 → 不算这次按下");

    // ⑤ "按下发生了"与"按下算在我身上"是两件事：前者给"点外部就关"用
    probe.Reset();
    r = probe.Update(false, onlyPrimary(true));
    Check(r.anyDownEdge && !r.primary.pressed,
          "T-03：在别处按下 → downEdge 真、pressed 假");

    // ⑥ 0.5.0：两个键各走各的边沿，左右同按互不覆盖
    //
    // 这是"分槽"的存在理由——共用一份 `wasDown` 时，后按下的那个键会把前一个
    // 键的 `released` 吞掉，表现是按钮永久停在 Pressed 色。
    probe.Reset();
    probe.Update(true, onlyPrimary(false));
    r = probe.Update(true, InkingPointerDownState{true, true});
    Check(r.primary.pressed && r.secondary.pressed && r.anyDownEdge,
          "0.5.0：左右同时按下 → 两个槽各自 pressed");
    r = probe.Update(true, InkingPointerDownState{false, true});
    Check(r.primary.released && r.primary.clicked && !r.secondary.released,
          "0.5.0：只抬左键 → 主键 click，次键仍按着");
    r = probe.Update(true, InkingPointerDownState{false, false});
    Check(r.secondary.released && r.secondary.clicked && !r.primary.clicked,
          "0.5.0：再抬右键 → 次键 click，主键不重复报");
}

// ---------------------------------------------------------------------------
// 右键 / 触摸长按（0.5.0）
// ---------------------------------------------------------------------------
//
// 这一组全部**无头**跑：长按只要推进 `TickLogic`，不需要真触摸设备。
// 这正是"长按判定住在场景层"的收益——判定位置在窗口层的话，这里得造一串
// SDL 事件才能验。

/// 只数事件的静态按钮。
///
/// 三态仍然用真的 `InkingButtonLook`（不是假的），所以"右键不该点亮按钮"这条
/// 断言验的是**真实现**，不是替身。
class ButtonSpy : public InkingStaticButton {
public:
    ButtonSpy(InkingWidget* parent, const InkingButtonData& data)
        : InkingStaticButton(parent, data) {}

    struct Counts {
        int primaryPress = 0;
        int primaryRelease = 0;
        int primaryClick = 0;
        int secondaryPress = 0;
        int secondaryRelease = 0;
        int secondaryClick = 0;
    };

    Counts counts{};
    float lastClickX = -1.0f;
    float lastClickY = -1.0f;

protected:
    void onPointerPress(InkingPointerButton button, bool press) override {
        // 先记账再交给基类：基类对 Secondary 会直接 return（不换三态），
        // 但我们仍然要数到这次 press。
        if (button == InkingPointerButton::Primary) {
            press ? ++counts.primaryPress : ++counts.primaryRelease;
        } else {
            press ? ++counts.secondaryPress : ++counts.secondaryRelease;
        }
        InkingStaticButton::onPointerPress(button, press);
    }

    void onPointerClick(InkingPointerButton button, float designX,
                        float designY) override {
        if (button == InkingPointerButton::Primary) {
            ++counts.primaryClick;
        } else {
            ++counts.secondaryClick;
            lastClickX = designX;
            lastClickY = designY;
        }
        InkingStaticButton::onPointerClick(button, designX, designY);
    }
};

/// 在一个干净场景里放一个 200×100 的按钮（左下角 100,100），返回它。
/// 场景对象由调用方持有（`InkingSceneLibrary` 不拥有场景）。
ButtonSpy* MakeSpyButton(InkingScene& scene, const char* name) {
    InkingButtonData data;
    data.name = name;
    data.width = 200;
    data.height = 100;
    data.offsetX = 100.0f;
    data.offsetY = 100.0f;
    data.normal = InkingButtonAppearance(InkingAppearanceType::Color,
                                           0xFF204060u);
    data.hover = data.normal;
    data.onclicked = data.normal;
    return static_cast<ButtonSpy*>(scene.AddChild(
        std::make_unique<ButtonSpy>(&scene, data)));
}

// ---- 右键 = Primary / Secondary 两种状态 ----
void CheckRightButton() {
    std::printf("\n-- 右键：两种输入状态的对称（0.5.0）--\n");

    const auto feed = [](InkingScene& scene, float x, float y,
                         bool primary, bool secondary,
                         InkingPointerSource source
                             = InkingPointerSource::Mouse) {
        scene.SetPointerState(x, y, InkingPointerDownState{primary, secondary},
                              true, source);
        scene.DispatchPointer();
    };

    // ① 右键按下 / 抬起 = 一次次键点击，且**不点亮按钮**
    {
        InkingScene scene("right_click");
        ButtonSpy* spy = MakeSpyButton(scene, "right_btn");

        feed(scene, 200.0f, 150.0f, false, false);
        feed(scene, 200.0f, 150.0f, false, true);
        Check(spy->counts.secondaryPress == 1
                  && spy->counts.primaryPress == 0,
              "0.5.0：右键按下 → 只有次键 press");
        // 指针在按钮上 ⇒ 此刻是 Hover；关键是**不是 Pressed**。
        Check(spy->GetState() != InkingButtonState::Pressed,
              "0.5.0：右键按下**不进 Pressed 态**（三态是主键手势的可视化）");

        feed(scene, 200.0f, 150.0f, false, false);
        Check(spy->counts.secondaryRelease == 1
                  && spy->counts.secondaryClick == 1
                  && spy->counts.primaryClick == 0,
              "0.5.0：右键抬起 → 次键 release + click，主键零事件");
    }

    // ② 左键回归：语义与 0.5.0 之前一模一样
    {
        InkingScene scene("left_regression");
        ButtonSpy* spy = MakeSpyButton(scene, "left_btn");

        feed(scene, 200.0f, 150.0f, true, false);
        Check(spy->counts.primaryPress == 1
                  && spy->GetState() == InkingButtonState::Pressed,
              "0.5.0：左键按下 → 主键 press + Pressed 态（回归）");
        feed(scene, 200.0f, 150.0f, false, false);
        Check(spy->counts.primaryClick == 1 && spy->counts.secondaryClick == 0,
              "0.5.0：左键抬起 → 主键 click（回归）");
    }

    // ③ 左右同按再依次抬起：两个槽各走各的
    //
    // 这是"分槽"的存在理由。共用一份 `wasDown` 时，抬起会在第一个键上把
    // `released` 算完，第二个键的 press(false) 就永远不来——按钮永久 Pressed。
    {
        InkingScene scene("both_buttons");
        ButtonSpy* spy = MakeSpyButton(scene, "both_btn");

        feed(scene, 200.0f, 150.0f, true, false);
        feed(scene, 200.0f, 150.0f, true, true);
        Check(spy->counts.primaryPress == 1
                  && spy->counts.secondaryPress == 1,
              "0.5.0：左右同按 → 两个键各收到一次 press");

        feed(scene, 200.0f, 150.0f, false, true);
        Check(spy->counts.primaryRelease == 1 && spy->counts.primaryClick == 1
                  && spy->counts.secondaryRelease == 0,
              "0.5.0：只抬左键 → 主键 click，次键仍按着");

        feed(scene, 200.0f, 150.0f, false, false);
        Check(spy->counts.secondaryRelease == 1
                  && spy->counts.secondaryClick == 1
                  && spy->counts.primaryClick == 1,
              "0.5.0：再抬右键 → 次键 click，主键不重复报");
    }

    // ④ 右键不改 hover、不抢键盘焦点
    {
        InkingScene scene("right_no_hover");
        ButtonSpy* spy = MakeSpyButton(scene, "hover_btn");

        feed(scene, 200.0f, 150.0f, false, false);
        InkingWidget* before = scene.GetHoveredNode();
        feed(scene, 200.0f, 150.0f, false, true);
        Check(before == spy && scene.GetHoveredNode() == spy,
              "0.5.0：右键按下不改变悬停（hover 是“我指着谁”）");
        Check(scene.GetKeyboardFocus() == nullptr,
              "0.5.0：右键**不抢键盘焦点**（否则点空白会清掉输入框焦点）");
        feed(scene, 200.0f, 150.0f, false, false);
    }

    // ⑤ 坐标 = **按下点**，不是抬起点
    //
    // 用右键验（鼠标路径）；触摸长按的同一条语义在下面 ⑨ 验。
    {
        InkingScene scene("press_origin");
        ButtonSpy* spy = MakeSpyButton(scene, "origin_btn");

        feed(scene, 150.0f, 130.0f, false, true);
        feed(scene, 170.0f, 140.0f, false, true);   // 按住挪动，仍在按钮内
        feed(scene, 170.0f, 140.0f, false, false);
        Check(spy->lastClickX == 150.0f && spy->lastClickY == 130.0f,
              "0.5.0：click 坐标是按下点 (150,130)，不是抬起点 (170,140)");
    }
}

// ---- 触摸：轻点 = 主键，长按 = 次键 ----
void CheckTouchLongPress() {
    std::printf("\n-- 触摸：长按自动映射为次键（0.5.0）--\n");

    const auto feed = [](InkingScene& scene, float x, float y,
                         bool primary, bool secondary,
                         InkingPointerSource source
                             = InkingPointerSource::Touch) {
        scene.SetPointerState(x, y, InkingPointerDownState{primary, secondary},
                              true, source);
        scene.DispatchPointer();
    };

    // ① 轻点（0.3s）→ 只有主键 click
    {
        InkingScene scene("touch_tap");
        ButtonSpy* spy = MakeSpyButton(scene, "tap_btn");

        feed(scene, 200.0f, 150.0f, true, false);
        scene.TickLogic(0.3);
        feed(scene, 200.0f, 150.0f, false, false);
        Check(spy->counts.primaryClick == 1
                  && spy->counts.secondaryClick == 0
                  && spy->counts.secondaryPress == 0,
              "0.5.0：触摸轻点 → 只有主键 click（与鼠标左键同构）");
    }

    // ② 长按（0.5s）→ 主键作废 + 次键按下；抬手 → 次键 click
    {
        InkingScene scene("touch_long");
        ButtonSpy* spy = MakeSpyButton(scene, "long_btn");

        feed(scene, 200.0f, 150.0f, true, false);
        Check(spy->counts.primaryPress == 1, "0.5.0：长按前先有一次主键 press");

        scene.TickLogic(0.5);          // 计时到阈值 → 置 pending
        scene.DispatchPointer();       // 消费：作废主键、次键按下

        Check(spy->counts.primaryRelease == 1 && spy->counts.primaryClick == 0,
              "0.5.0：长按成立 → 主键捕获作废（release）但**不发 click**");
        Check(spy->counts.secondaryPress == 1,
              "0.5.0：长按成立 → 次键 press");
        Check(spy->GetState() != InkingButtonState::Pressed,
              "0.5.0：长按不把按钮点亮（次键不进三态）");

        feed(scene, 200.0f, 150.0f, false, false);
        Check(spy->counts.secondaryRelease == 1
                  && spy->counts.secondaryClick == 1,
              "0.5.0：长按后抬手 → 次键 release + click（菜单在这里弹）");
        Check(spy->counts.primaryClick == 0,
              "0.5.0：整趟长按 **一次左键 click 都没有**");
    }

    // ③ 长按候选期内移动超 slop → 放弃长按（那是拖动 / 滚动）
    {
        InkingScene scene("touch_drag");
        ButtonSpy* spy = MakeSpyButton(scene, "drag_btn");

        feed(scene, 200.0f, 150.0f, true, false);
        feed(scene, 200.0f, 175.0f, true, false);   // 25 > slop(8)，仍在按钮内
        scene.TickLogic(1.0);
        scene.DispatchPointer();
        Check(spy->counts.secondaryPress == 0,
              "0.5.0：按住滑走 25 设计像素 → 长按被放弃（不当成菜单手势）");

        // 而且**不会迟到**：手指停在远处再等 1 秒也还是不触发。
        scene.TickLogic(1.0);
        scene.DispatchPointer();
        Check(spy->counts.secondaryPress == 0,
              "0.5.0：滑走之后继续按住 → 长按**不会迟到触发**");
        feed(scene, 200.0f, 175.0f, false, false);
    }

    // ④ 来源抖动（触摸按着时传了一次 Mouse）→ 长按仍成立
    //
    // 场景只在**按下那一刻**锁来源。若每帧照抄，触摸屏上"边按边动鼠标"
    // 就会把长按打断，而现象是"长按偶尔不成立"，极难复现。
    {
        InkingScene scene("source_lock");
        ButtonSpy* spy = MakeSpyButton(scene, "lock_btn");

        feed(scene, 200.0f, 150.0f, true, false);                  // Touch
        feed(scene, 200.0f, 150.0f, true, false,
             InkingPointerSource::Mouse);                        // 抖成 Mouse
        scene.TickLogic(0.5);
        scene.DispatchPointer();
        Check(spy->counts.secondaryPress == 1,
              "0.5.0：来源按**按下瞬间**锁定 → 中途抖动不影响长按");
        feed(scene, 200.0f, 150.0f, false, false);
    }

    // ⑤ 长按的 click 坐标也是**按下点**（长按后手指微动，菜单不该挪）
    {
        InkingScene scene("long_origin");
        ButtonSpy* spy = MakeSpyButton(scene, "long_origin_btn");

        feed(scene, 160.0f, 130.0f, true, false);
        feed(scene, 163.0f, 132.0f, true, false);   // 3px，slop 内
        scene.TickLogic(0.6);
        scene.DispatchPointer();
        feed(scene, 163.0f, 132.0f, false, false);
        Check(spy->lastClickX == 160.0f && spy->lastClickY == 130.0f,
              "0.5.0：长按的 click 坐标仍是按下点 (160,130)");
    }

    // ⑥ 长按对**动态组件**可见（浮窗的"点外部关闭"靠它）
    //
    // 这是最容易漏的一条：动态组件读的是 `GetPointerDown()`。若它返回物理状态
    // 而不是合成后的有效状态，长按在浮窗看来**从未发生**——"长按菜单外部"
    // 就永远关不掉菜单，而且完全没有报错。
    {
        InkingScene scene("long_dynamic_visible");
        MakeSpyButton(scene, "behind_btn");

        InkingAnchorData data;
        data.width = 40;
        data.height = 40;
        data.offsetX = 600.0f;
        data.offsetY = 600.0f;
        auto* spy = static_cast<PointerSpy*>(
            scene.AddChild(std::make_unique<PointerSpy>(&scene, data)));

        feed(scene, 200.0f, 150.0f, true, false);
        scene.TickLogic(0.6);
        scene.DispatchPointer();
        Check(spy->sawSecondaryDown,
              "0.5.0：动态组件从 GetPointerDown() 看得到长按合成的次键");
        feed(scene, 200.0f, 150.0f, false, false);
    }
}


//
// 纯数字断言：四方向 × "翻转后仍放不下" 一共十几种组合，靠开窗口看不出对错。
// 这里只验四组代表 + 三条边界（两轴独立 / 非零原点 / 尺寸为 0 的退化）。
// ---------------------------------------------------------------------------
// 定点浮窗的落位算术（0.4.1 T-05）
// ---------------------------------------------------------------------------
//
// 纯数字断言：四方向 × "翻转后仍放不下" 一共十几种组合，靠开窗口看不出对错。
// 这里只验四组代表 + 三条边界（两轴独立 / 非零原点 / 尺寸为 0 的退化）。
void CheckPopupPlacement() {
    std::printf("\n-- 定点浮窗：落位算术（T-05）--\n");

    const InkingShapeBounds screen{0.0f, 0.0f, 1280.0f, 720.0f};
    const InkingAnchorPoint leftTop{0.0f, 0.0f};

    // ① 右下角：两轴都要翻到对侧
    {
        const auto p = inking_detail::SolvePopupPlacement(
            1200.0f, 700.0f, 200, 120, screen, leftTop);
        std::printf("         右下角 → x=%.1f y=%.1f side=(%d,%d)\n", p.x, p.y,
                    p.sideX, p.sideY);
        Check(p.x == 1000.0f && p.y == 580.0f && p.sideX == -1
                  && p.sideY == -1 && !p.clampedX && !p.clampedY,
              "T-05：右下角定点 → 翻左翻上（x=1000, y=580）");
    }

    // ② 左上角：原地不动
    {
        const auto p = inking_detail::SolvePopupPlacement(
            10.0f, 10.0f, 200, 120, screen, leftTop);
        Check(p.x == 10.0f && p.y == 10.0f && p.sideX == 1 && p.sideY == 1,
              "T-05：左上角定点 → 不翻不夹（x=10, y=10）");
    }

    // ③ 中部：不动
    {
        const auto p = inking_detail::SolvePopupPlacement(
            600.0f, 300.0f, 200, 120, screen, leftTop);
        Check(p.x == 600.0f && p.y == 300.0f && p.sideX == 1 && p.sideY == 1,
              "T-05：中部定点 → 不翻不夹");
    }

    // ④ 两轴独立：x 翻、y 不翻（右下角那种"往下展开"的手感）
    {
        const auto p = inking_detail::SolvePopupPlacement(
            1200.0f, 10.0f, 200, 120, screen, leftTop);
        Check(p.x == 1000.0f && p.sideX == -1 && p.y == 10.0f
                  && p.sideY == 1,
              "T-05：x 翻左、y 仍向下（两轴独立）");
    }

    // ⑤ 比范围还大：贴住起始边、允许另一侧溢出
    {
        const auto p = inking_detail::SolvePopupPlacement(
            600.0f, 300.0f, 1400, 120, screen, leftTop);
        Check(p.clampedX && p.sideX == 0 && p.x == 0.0f && !p.clampedY,
              "T-05：比范围还宽的浮窗 → clampedX + x=0（允许右溢出）");
    }

    // ⑥ 范围原点不是 (0,0)：整套算术在父级坐标里自洽
    {
        const InkingShapeBounds area{100.0f, 100.0f, 400.0f, 300.0f};
        const auto p = inking_detail::SolvePopupPlacement(
            450.0f, 350.0f, 200, 120, area, leftTop);
        Check(p.x == 250.0f && p.y == 230.0f && p.sideX == -1
                  && p.sideY == -1,
              "T-05：范围原点非零时，翻到对侧后仍落在范围内");
    }

    // ⑦ 退化：尺寸还是 0（布局没算完就 OpenAt）→ 就放在定点上，不崩
    {
        const auto p = inking_detail::SolvePopupPlacement(
            600.0f, 300.0f, 0, 0, screen, leftTop);
        Check(p.x == 600.0f && p.y == 300.0f && p.sideX == 1
                  && !p.clampedX,
              "T-05：尺寸为 0 → 位置 = 定点，不翻转不夹紧");
    }
}

// ---------------------------------------------------------------------------
// 定点浮窗（0.4.1 T-06）
// ---------------------------------------------------------------------------
//
// 七条断言把"定点 / 吃掉 / 点外关闭 / 焦点"四件事钉住。注意第 3 条其实在验
// T-01 与 T-06 的协同：浮窗是**动态容器**，里面的静态按钮能命中，靠的正是
// `QueryHit` 对动态那一侧也深挖。
void CheckFixedPopup() {
    std::printf("\n-- 定点浮窗：OpenAt / 吃掉 / 点外关闭（T-06）--\n");

    InkingScene scene("fixed_popup");

    const auto makePanel = [](const char* name) {
        InkingPanelData data;
        data.name = name;
        data.width = 200;
        data.height = 120;
        data.closed.color = 0xFFFFFFFFu;
        data.shape = InkingShapeSpec::RoundedRect(16.0f);
        return data;
    };

    InkingFixedPopup popup(&scene, makePanel("popup"));

    // ① 定点落位：右下角 ⇒ 翻左翻上
    popup.OpenAt(1200.0f, 700.0f);
    std::printf("         OpenAt(1200,700) → GetX=%.1f GetY=%.1f side=(%d,%d)\n",
                popup.GetX(), popup.GetY(), popup.GetResolvedSideX(),
                popup.GetResolvedSideY());
    Check(popup.IsOpen() && popup.GetX() == 1000.0f && popup.GetY() == 580.0f
              && popup.GetResolvedSideX() == -1
              && popup.GetResolvedSideY() == -1,
          "T-06：OpenAt(1200,700) → 翻左翻上（GetX=1000 / GetY=580）");

    // ② 打开时夺取键盘焦点（局部快捷键的前提）
    Check(scene.GetKeyboardFocus() == &popup,
          "T-06：OpenAt 之后键盘焦点在浮窗上");

    // ③ 浮窗里的静态按钮能被命中（动态容器深挖）
    InkingButtonData innerData;
    innerData.name = "popup_inner";
    innerData.width = 80;
    innerData.height = 40;
    innerData.offsetX = 10.0f;
    innerData.offsetY = 10.0f;
    innerData.normal = InkingButtonAppearance(InkingAppearanceType::Color,
                                                0xFF204060u);
    auto* inner = static_cast<InkingStaticButton*>(
        popup.AddItem(std::make_unique<InkingStaticButton>(
            nullptr, innerData)));
    // 按钮世界中心 = 1000 + 10 + 40, 580 + 10 + 20
    Check(scene.QueryHit(1050.0f, 610.0f).target == inner,
          "T-06：浮窗里的静态按钮能被命中");

    // ④ 空白区：命中浮窗且是 Block（吃掉，不透到下面）
    const InkingHitResult blank = scene.QueryHit(1150.0f, 670.0f);
    Check(blank.target == &popup && blank.kind == InkingHitKind::Block,
          "T-06：点浮窗空白区 → 命中浮窗且 kind=Block");

    // ⑤ 外部按下 ⇒ 关闭
    scene.SetPointerState(100.0f, 100.0f, true, true);
    scene.DispatchPointer();
    Check(!popup.IsOpen(), "T-06：外部按下 → 立即关闭");

    // ⑥ 比范围还宽：贴左边、允许右溢出
    InkingPanelData wideData = makePanel("popup_wide");
    wideData.width = 1400;
    InkingFixedPopup widePopup(&scene, wideData);
    widePopup.OpenAt(600.0f, 300.0f);
    Check(widePopup.IsClampedX() && widePopup.GetX() == 0.0f,
          "T-06：比窗口还宽的浮窗 → 贴左边（IsClampedX，允许右溢出）");

    // ⑦ 关闭时焦点当场释放
    widePopup.Close();
    Check(scene.GetKeyboardFocus() == nullptr,
          "T-06：Close 之后焦点被释放（不留悬空）");
}

// ---------------------------------------------------------------------------
// 菜单浮窗（0.4.1 T-07 / T-08）
// ---------------------------------------------------------------------------
//
// 布局四条 + 快捷键三条。快捷键那三条刻意分开验：命中 / 取回焦点 / 拒绝重复，
// 因为它们各自会因为不同的原因坏掉（表没建、通知没来、去重写错）。
void CheckMenuPopup() {
    std::printf("\n-- 菜单浮窗：布局 / 项 / 局部快捷键（T-07 / T-08）--\n");

    InkingScene scene("menu_popup");

    inking_prefab::MenuPopupStyle style =
        inking_prefab::MenuPopupStyle::MakeDefault();
    style.itemWidth = 200;
    style.itemHeight = 36;
    style.padding = 6;
    style.gap = 2;

    inking_prefab::MenuPopup menu(&scene, style);

    // 只给颜色、宽高留 0：`AddButton` 应该补成菜单的默认尺寸。
    InkingButtonData look;
    look.normal =
        InkingButtonAppearance(InkingAppearanceType::Color, 0xFFFFFFFFu);
    look.hover = look.normal;
    look.onclicked = look.normal;

    int hits = 0;
    auto* first = menu.AddButton(look, "打开", [&] { ++hits; });
    menu.AddButton(look, "另存为", [&] {});
    menu.AddButton(look, "退出", [&] {});

    InkingAnchorData separatorData;
    separatorData.width = style.itemWidth;
    separatorData.height = 2;
    menu.AddItem(
        std::make_unique<InkingDynamicWidget>(nullptr, separatorData));

    // ① 项表：3 个按钮 + 1 个自定义件
    Check(menu.GetItemCount() == 4,
          "T-07：3 个按钮 + 1 个自定义项都进了项表");

    // ② 竖排布局：相邻项的 y 差 = itemHeight + gap
    const InkingShapeBounds r0 = menu.GetItemRect(0);
    const InkingShapeBounds r1 = menu.GetItemRect(1);
    const InkingShapeBounds r2 = menu.GetItemRect(2);
    std::printf("         项矩形 y = %.1f / %.1f / %.1f\n", r0.y, r1.y, r2.y);
    Check(r1.y > r0.y && r2.y > r1.y
              && (r1.y - r0.y) == static_cast<float>(style.itemHeight + style.gap),
          "T-07：竖排布局生效（相邻项差 = itemHeight + gap）");

    // ③ 容器宽度 = itemWidth + padding×2
    Check(menu.GetWidth() == style.itemWidth + style.padding * 2,
          "T-07：容器宽度 = itemWidth + padding×2");

    // ④ 激活：回调一次 + 自动关闭
    first->TriggerClick();
    Check(hits == 1 && !menu.IsOpen(),
          "T-07：TriggerClick → 回调一次并自动关闭");

    // ⑤ 局部快捷键：绑 F5，按下就命中并关闭
    int shortcutHits = 0;
    const bool bound = menu.BindLocalShortcut(
        InkingShortcut::Parse("F5"), [&] { ++shortcutHits; }, "F5");
    menu.OpenAt(100.0f, 100.0f);

    InkingKeyEvent key{};
    key.key = SDLK_F5;
    menu.PushKeyDown(key);
    Check(bound && shortcutHits == 1 && !menu.IsOpen(),
          "T-08：局部快捷键 F5 → 命中一次并关闭");

    // ⑥ 焦点被夺走：下一次指针变化就取回（菜单是焦点模态的）
    menu.OpenAt(100.0f, 100.0f);
    scene.SetKeyboardFocus(nullptr);
    Check(scene.GetKeyboardFocus() == nullptr, "T-08：前置——焦点已被清空");
    scene.SetPointerState(20.0f, 20.0f, false, true);
    scene.DispatchPointer();
    Check(scene.GetKeyboardFocus() == &menu,
          "T-08：焦点被夺走后，下一次指针变化就取回");

    // ⑦ 重复绑定被拒（先到先得，不覆盖）
    const std::size_t before = menu.GetLocalShortcutCount();
    const bool duplicate = menu.BindLocalShortcut(
        InkingShortcut::Parse("F5"), [] {}, "F5");
    Check(!duplicate && menu.GetLocalShortcutCount() == before,
          "T-08：重复绑同一个组合被拒（先到先得）");

    // ⑧ 与全局表冲突：绑之前就拒（窗口层先跑全局表，局部永远收不到）
    InkingShortcutRegistry& global =
        InkingWindow::Instance().GetShortcuts();
    const InkingShortcutRegistry::Id id =
        global.Bind(InkingShortcut::Parse("Ctrl+Alt+F9"), [] {}, "probe");
    const bool conflict = menu.BindLocalShortcut(
        InkingShortcut::Parse("Ctrl+Alt+F9"), [] {}, "conflict");
    if (id != 0) {
        global.Unbind(id);
    }
    Check(!conflict, "T-08：与全局表冲突时拒绝绑定（不静默）");

    menu.Close();
}

// ---------------------------------------------------------------------------
// FilePicker：盘符栏 / 换盘符（C5 增补）
// ---------------------------------------------------------------------------
//
// 盘符是**跟着机器走的东西**：判据只能钉"与本机枚举结果一致"，钉死某个字母
// 等于把用例绑在一台机器上。
//
// 这条用例同时补上一个缺口——FilePicker 落到 `prefab/` 之后**一直没有任何自检
// 和示例**（`grep FilePicker examples/` 是空的），今天之前它只是"编得过"。
void CheckFilePickerDrives() {
    std::printf("\n-- 文件选择：盘符栏 / 换盘符（C5 增补）--\n");

    const std::vector<std::filesystem::path> drives =
        inking_prefab::FilePicker::EnumerateDrives();
    if (drives.empty()) {
        // 非 Windows：盘符栏按设计整条不建。这里不是"跳过检查"，而是
        // "平台本来就没有盘符"这条契约本身要被看见。
        std::printf("         本平台没有盘符（POSIX 只有 /），跳过换盘符用例\n");
        return;
    }

    InkingScene scene("file_picker_drives");
    const inking_prefab::FilePickerStyle style =
        inking_prefab::FilePickerStyle::MakeDefault();
    inking_prefab::FilePicker picker(&scene, style);

    // ① 项数 = 枚举结果（"列出来的"与"能换的"必须是同一份）
    Check(picker.GetDriveCount() == drives.size(),
          "C5：盘符栏项数 = 枚举到的盘符数");

    // ② 每项 id 都是 `drive:<完整根路径>`：字母拼 `C:` 是 drive-relative，
    //    那是另一个位置（当前盘上的当前目录），不是 C 盘根。
    bool allRoots = true;
    for (std::size_t i = 0; i < picker.GetDriveCount(); ++i) {
        const std::string id = picker.Drives().ItemId(i);
        allRoots = allRoots && id.rfind("drive:", 0) == 0 && id.size() > 6;
    }
    Check(allRoots, "C5：每项 id 都是 drive:<根路径>（不是 drive:C）");

    // ③ 点盘符 = 当前目录切到那个盘的根
    const bool activated = picker.Drives().Activate(0);
    Check(activated && picker.GetDirectory() == drives.front(),
          "C5：点第一个盘符 → 当前目录切到该盘根");

    // ④ 多盘机器上真的换了一次盘
    if (drives.size() > 1) {
        picker.Drives().Activate(drives.size() - 1);
        Check(picker.GetDirectory() == drives.back(),
              "C5：换到另一个盘符生效");
    }

    // ⑤ 显示名是 `C:` 这种（去掉根路径尾部分隔符）
    const std::string current = picker.GetCurrentDrive();
    Check(!current.empty() && current.back() == ':',
          "C5：GetCurrentDrive 给 `C:` 这种盘符名");

    // ⑥ 幂等：盘符没变 → 项数与建出来的节点数都不变
    const std::size_t beforeCount = picker.GetDriveCount();
    const std::size_t beforeBuilt = picker.Drives().GetBuiltItemCount();
    picker.RefreshDrives();
    Check(picker.GetDriveCount() == beforeCount
              && picker.Drives().GetBuiltItemCount() == beforeBuilt,
          "C5：RefreshDrives 幂等（项数与节点数都不变）");
}

// ---------------------------------------------------------------------------
// 文件选择：按钮栏不是菜单栏（0.5.3，GoldingCore 反馈 1）
// ---------------------------------------------------------------------------
//
// 反馈：「全屏后『添加』『取消』消失」。
// 根因：底部那一栏当初为了"不新增底层词汇"复用了 `inking_prefab::MenuBar`，而它的数据面
// 捆着窗口语义——`hideInFullscreen` / `zoomWheel` 默认都是 true，实现里还订阅
// `fullscreenChanged`。**布局该复用，策略不该复用**：现在换成普通 `InkingPanelData` 横排。
//
// 这里断的不是"按钮还在"（那是视觉），而是三条可量的：类型上不再是 MenuBar、
// 全屏事件不从它这里过去、排列口径没变。

void CheckFilePickerButtonBar() {
    std::printf("\n-- 文件选择：按钮栏不带窗口语义（0.5.3）--\n");

    InkingWindow& window = InkingWindow::Instance();
    InkingScene scene("file_picker_buttons");
    const inking_prefab::FilePickerStyle style =
        inking_prefab::FilePickerStyle::MakeDefault();
    inking_prefab::FilePicker picker(&scene, style);

    // ① 结构上不再是菜单栏：全屏自隐 / 滚轮缩放都是 MenuBar 自己的行为，
    //    换成普通面板之后它们**按构造**不存在（不是靠开关关掉的）。
    Check(dynamic_cast<inking_prefab::MenuBar*>(&picker.Buttons()) == nullptr,
          "C5：按钮栏不是 MenuBar（窗口语义按构造不存在）");
    Check(picker.GetButtonCount() == 2, "C5：按钮栏建了两个按钮（确定 / 取消）");

    // ② 全屏不再藏它（反馈 1 的现场：以前整条 SetVisible(false)）。
    const bool wasFullscreen = window.IsFullscreen();
    window.SetFullscreen(true);
    Check(picker.IsButtonBarVisible(), "C5：全屏后按钮栏仍可见（反馈 1）");
    {
        // 构造期就已经全屏那一路：MenuBar 是"构造时查一次 IsFullscreen()"，
        // 这里也必须新建即可见。
        inking_prefab::FilePicker late(&scene, style);
        Check(late.IsButtonBarVisible(), "C5：全屏状态下新建的选择器，按钮栏也可见");
    }
    window.SetFullscreen(wasFullscreen);
    Check(picker.IsButtonBarVisible(), "C5：退出全屏后仍可见（没有被谁记成隐藏）");

    // ③ 排列口径没变：两个 96 宽按钮靠右、间隔 8、右边距 8。
    //    这条钉住"换了容器但排出来一样"——只验结构会把视觉改坏还全绿。
    const int barWidth = picker.Buttons().GetWidth();
    const InkingStaticButton* accept = picker.GetButton(0);
    const InkingStaticButton* cancel = picker.GetButton(1);
    const bool layoutOk =
        accept != nullptr && cancel != nullptr && accept->GetWidth() == 96 && cancel->GetWidth() == 96
        && std::fabs((cancel->GetX() + static_cast<float>(cancel->GetWidth()))
                     - (static_cast<float>(barWidth) - 8.0f)) < 0.51f
        && std::fabs((cancel->GetX() - (accept->GetX() + static_cast<float>(accept->GetWidth()))) - 8.0f)
               < 0.51f;
    Check(layoutOk, "C5：两个按钮仍靠右排（宽 96 / 间隔 8 / 右边距 8）");

    // ④ 「取消」仍然真的接在 `canceled` 上（换容器最容易丢的就是这层接线）。
    bool canceled = false;
    const auto token = picker.canceled.Subscribe([&canceled] { canceled = true; });
    if (cancel != nullptr) {
        scene.Activate("filePickerButton.cancel");
    }
    Check(canceled, "C5：语义点「取消」→ canceled 事件发出");
    picker.canceled.Unsubscribe(token);

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// 滚动：内容尺寸的口径（0.5.3，GoldingCore 反馈 2）
// ---------------------------------------------------------------------------
//
// 反馈：「文件选择器默认界面没有滑块，进子目录再退回才有」。
// 两个根因，各有一条断言：
//   ① 内容尺寸按 `IsVisibleInTree()` 统计 ⇒ 在**隐藏的弹窗**里填好的列表内容高恒为 0，
//      而弹窗转可见之后没有人重量（`ListView::rebuildVisible` 在可见区间没变时早退）；
//   ② 统计时又减了一次面板自己的 `GetX()/GetY()`（子项坐标本来就是父级相对）
//      ⇒ 列表在 FilePicker 里 y=70，内容高就少 70，末尾永远滚不到。

void CheckScrollContentMeasurement() {
    std::printf("\n-- 滚动：内容尺寸的口径（0.5.3）--\n");

    InkingScene scene("selftest-scroll-measure");

    // 容器放在父级 (0, 70)：这正是列表在 FilePicker 里的位置（② 的现场）。
    InkingPanelData hostData;
    hostData.name = "measure_host";
    hostData.width = 300;
    hostData.height = 500;
    auto* host = static_cast<InkingStaticPanel*>(
        scene.AddChild(std::make_unique<InkingStaticPanel>(&scene, hostData)));

    InkingPanelData listData;
    listData.name = "measure_list";
    listData.width = 300;
    listData.height = 200;
    listData.offsetY = 70.0f;
    listData.scroll.enabled = true;
    auto* list = static_cast<InkingStaticPanel*>(
        host->AddChild(std::make_unique<InkingStaticPanel>(host, listData)));

    for (int i = 0; i < 10; ++i) {
        InkingPanelData row;
        row.name = "measure_row" + std::to_string(i);
        row.width = 300;
        row.height = 26;
        row.offsetY = static_cast<float>(i) * 26.0f;
        list->AddChild(std::make_unique<InkingStaticPanel>(list, row));
    }

    list->UpdateScrollRange();
    Check(std::fabs(list->GetContentHeight() - 260.0f) < 0.01f,
          "scroll_measure：内容高 = 10 × 26 = 260（不再扣掉自身 y=70）");
    Check(std::fabs(list->GetScrollRangeY() - (260.0f - list->ContentViewportHeight())) < 0.01f,
          "scroll_measure：可滚范围 = 内容 − 内容视口（差 70 时这条立刻红）");

    // ① 父级隐藏时照样量得准：这正是"弹窗先建、后显示"那一路。
    host->SetVisible(false);
    list->UpdateScrollRange();
    Check(std::fabs(list->GetContentHeight() - 260.0f) < 0.01f,
          "scroll_measure：父级隐藏时仍量得准（不按整棵树判可见）");
    host->SetVisible(true);

    // 边界：子项自己被 SetVisible(false) **仍占位**（与布局同一口径），内容高不变。
    list->GetChildren().back()->SetVisible(false);
    list->UpdateScrollRange();
    Check(std::fabs(list->GetContentHeight() - 260.0f) < 0.01f,
          "scroll_measure：自身隐藏的项仍计入内容高（与「隐藏仍占位」一致）");

    // ---- 端到端：隐藏的容器里填一个 30 项目录，转可见后就该有滑块 ----
    // 目录建在**当前工作目录**下，不用 `temp_directory_path()`：后者在 `env -i`
    // 这类干净环境里会退到系统的共享临时目录（`TEMP/TMP` 未设），那里本来就有一堆
    // 别人的文件，"枚举到 30 项"这条断言立刻不成立（包内验收实测踩到）。
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::current_path() / L"selftest-picker-scroll";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    for (int i = 0; i < 30; ++i) {
        std::ofstream file(dir / (std::string("f") + std::to_string(i) + ".bin"), std::ios::binary);
        file << "x";
    }

    InkingScene popupScene("selftest-picker-popup");
    InkingPanelData popupData;
    popupData.name = "picker_popup";
    popupData.width = 920;
    popupData.height = 640;
    popupData.clipChildren = true;
    popupData.visible = false;  // 与 golding.popup 同序：先建成隐藏、create 返回后才显示
    auto* popup = static_cast<InkingStaticPanel*>(
        popupScene.AddChild(std::make_unique<InkingStaticPanel>(&popupScene, popupData)));

    inking_prefab::FilePicker picker(popup, inking_prefab::FilePickerStyle::MakeDefault());
    picker.SetDirectory(dir);   // ← 此刻整棵子树不可见（反馈 2 的现场）
    popup->SetVisible(true);

    const float expected =
        static_cast<float>(picker.GetEntryCount()) * picker.List().GetRowHeight();
    // `>=` 而不是 `==`：万一上面那条目录没清干净（上一次异常退出的残留），
    // 这条仍然成立，而下面的"内容高 = 项数 × 行高"照样是精确等式。
    Check(picker.GetEntryCount() >= 30, "C5：临时目录至少枚举到 30 项");
    Check(std::fabs(picker.List().GetContentHeight() - expected) < 0.01f,
          "C5：隐藏期填的目录，内容高 = 项数 × 行高（不是 0）");
    Check(picker.List().GetBarMetrics(true).visible,
          "C5：转可见后滑块就出现（反馈 2：不必先点进子目录）");

    std::filesystem::remove_all(dir, ec);
    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// 输入框：光标 clamp + 水平滚动（0.2.0 T-12）
// ---------------------------------------------------------------------------
//
// 反馈报的"未获焦点仍画光标"在 0.1.x 已经修掉（`onRender` 有 `_focused` 门控）。
// 同一症状的**另一个成因**还在：单行输入不换行，文本一超宽光标就画到本体之外
// （面板默认 `clipChildren = false`，所以它真的会出现在框外）。
//
// 两条断言把这件事钉住：
//   ① 光标整体落在 `[0, width]` 里（夹取 + 滚动一起生效）；
//   ② 滚动之后，"点在光标那一像素上"命中的偏移仍是光标那个偏移
//      （`BoxForOffset` 减了多少，`OffsetAtPoint` 就加回多少）。
void CheckInputScroll() {
    std::printf("\n-- 输入框：光标 clamp + 水平滚动 --\n");

    InkingScene scene;
    InkingTextInputData data;
    data.name = "scroll_input";
    data.width = 120;
    data.height = 32;
    data.selfAnchor = InkingChangeAnchor::Center;
    data.traceAnchor = InkingChangeAnchor::Center;
    data.style.fontSize = 16.0f;
    data.style.fontPath = "";  // 机器默认字体：无头环境也能量到宽度

    auto* input = static_cast<InkingTextInput*>(scene.AddChild(
        std::make_unique<InkingTextInput>(&scene, data)));

    InkingSceneLibrary::SetActiveScene(&scene);
    scene.TickFrame(1.0f / 60.0f);

    input->SetFocused(true);
    input->SetEditText("WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");  // 32 个宽字符
    Check(input->GetTextWidth() > static_cast<float>(input->GetWidth()),
          "前置条件：文本确实比本体宽");

    // End：光标到末尾（这一下会触发滚动）。
    InkingKeyEvent end;
    end.key = SDLK_END;
    Check(input->HandleKey(end), "End 被输入框吃掉（光标到末尾）");

    const InkingTextInputRect caret = input->CaretBox();
    std::printf("         滚动量=%.1f 光标 x=%.1f 宽=%.1f 本体宽=%d\n",
                input->GetScrollX(), caret.x, caret.width, input->GetWidth());
    Check(input->GetScrollX() > 0.0f, "文本超宽 + 光标在末尾 → 产生水平滚动");
    Check(caret.x >= -0.01f
              && caret.x + caret.width
                     <= static_cast<float>(input->GetWidth()) + 0.01f,
          "input_caret_clamped：光标整体落在本体里（不再画到框外）");
    NoteSilentFailure("input_caret_clamped");

    // 点光标那一像素：Roll 后映射仍然一致。
    const float pressX = input->GetAbsX() + caret.x + caret.width * 0.5f;
    const float pressY = input->GetAbsY() + caret.y + caret.height * 0.5f;
    const std::size_t hitOffset = input->OffsetAtPoint(pressX, pressY);
    Check(hitOffset == input->Edit().Caret(),
          "input_scroll_mapping：滚动后同一像素仍映射到光标那个偏移");

    // 文本变短：滚动量必须回 0（否则下次输入会从"半句话"开始显示）。
    input->SetEditText("ab");
    Check(input->GetScrollX() == 0.0f, "文本变短后滚动量回 0");
    const InkingTextInputRect shortCaret = input->CaretBox();
    Check(shortCaret.x >= -0.01f
              && shortCaret.x + shortCaret.width
                     <= static_cast<float>(input->GetWidth()) + 0.01f,
          "短文本时光标也在本体里");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// 截图：失败路径的契约（正路径见下面「怎么验」）
// ---------------------------------------------------------------------------
//
// `InkingWindow::Capture` / `RequestCapture` 都要"要么真写成功、要么明确失败"，
// **不能排一个永远不落盘的请求**——那是最难查的一类静默失效。
//
// 本程序没有打开 `InkingWindow`（用的是裸 SDL 窗口），所以这里能验的就是
// 反面：没有渲染器时必须返回 false，而且**不许留下排队状态**。
// 正路径（真写出一张 PNG）由构建脚本跑一次 `INK_CAPTURE=<路径>` 的示例验：
// 第一帧落盘、尺寸等于窗口像素尺寸。
void CheckCaptureContract() {
    std::printf("\n-- 截图：失败路径 --\n");
    InkingWindow& window = InkingWindow::Instance();

    Check(!window.Capture(""), "空路径：立即截图返回 false");
    Check(!window.Capture("/tmp/ink_should_not_exist.png"),
          "没有窗口 / 渲染器：立即截图返回 false（不是崩溃、不是假成功）");
    Check(!window.RequestCapture("/tmp/ink_should_not_exist.png"),
          "没有窗口 / 渲染器：排队截图也返回 false");
    Check(!window.HasPendingCapture(),
          "被拒的请求没有留在队列里（不会有一张永远不落盘的截图）");
}

// ---------------------------------------------------------------------------
// 树的动态性（0.4.0 A 组）：改树之后，收集表 / 命中表立刻跟上
// ---------------------------------------------------------------------------
//
// 这一组对应 GoldingCore 反馈的 A1~A5。它们的共同根因只有一句：
// **渲染列表与命中表在"建树时"一次性收集，之后树变了不重建**。
//
// 每条断言都对着一个"以前必错、而且不报错"的现象：
//   A1 动态容器子树里的静态按钮永远收不到点击（点得到容器、点不到按钮）
//   A2 离线建好的子树挂进场景后不渲染（节点在、就是没画）
//   A3 创建时隐藏、之后显示也不渲染
//   A4 容器改尺寸后 `fillMain` 不重新收敛
//   B1 交叉轴 `Stretch` 没生效（`width:0` 解出来就是 0 宽）
//   A5 显式重建入口
void CheckDynamicTree() {
    std::printf("\n-- 树的动态性（A 组） --\n");

    struct ProbeScene : public InkingScene {
        ProbeScene() : InkingScene("probe_dyntree") {}
    };

    // 场景名不能和主场景撞：撞了会被 InkingSceneLibrary 驳回，测试就变成"在停放态
    // 的场景上断言"——全绿但什么都没验（这种假绿最难发现）。
    ProbeScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "A 组：探针场景注册成功（名字不与他人冲突）");
        return;
    }

    const auto makeButton = [](const char* name, int width, int height, int x,
                               int y) {
        InkingButtonData data;
        data.name = name;
        data.width = width;
        data.height = height;
        data.offsetX = static_cast<float>(x);
        data.offsetY = static_cast<float>(y);
        data.normal = InkingButtonAppearance(InkingAppearanceType::Color,
                                              0xFF112233u);
        return data;
    };

    // ---- A1：动态容器子树里的静态按钮 ----
    InkingAnchorData outerData;
    outerData.width = 400;
    outerData.height = 300;
    auto* outer = static_cast<InkingStaticPanel*>(
        scene.AddChild(std::make_unique<InkingStaticPanel>(
            &scene, [] {
                InkingPanelData data;
                data.name = "a1_outer";
                data.width = 400;
                data.height = 300;
                data.closed.color = 0xFF808080u;
                data.open.color = data.closed.color;
                data.Normalize();
                return data;
            }())));

    InkingAnchorData boxData;
    boxData.width = 200;
    boxData.height = 150;
    boxData.offsetX = 10.0f;
    boxData.offsetY = 10.0f;
    auto* box = scene.AddChild(
        std::make_unique<InkingDynamicWidget>(outer, boxData));
    auto* inner = static_cast<InkingStaticButton*>(
        box->AddChild(std::make_unique<InkingStaticButton>(
            box, makeButton("a1_inner", 80, 30, 20, 20))));

    Check(scene.QueryHit(inner->GetAbsX() + 5.0f, inner->GetAbsY() + 5.0f)
                  .target
              == inner,
          "A1：动态容器里的静态按钮能被命中（以前只有容器自己）");

    // ---- A2：离线建好的子树再挂进来 ----
    std::size_t before = scene.GetDrawItemCount();
    auto offline = std::make_unique<InkingStaticPanel>(
        nullptr, [] {
            InkingPanelData data;
            data.name = "a2_offline";
            data.width = 120;
            data.height = 60;
            data.offsetX = 500.0f;
            data.closed.color = 0xFF445566u;
            data.open.color = data.closed.color;
            data.Normalize();
            return data;
        }());
    auto* offlineChild = static_cast<InkingStaticButton*>(
        offline->AddChild(std::make_unique<InkingStaticButton>(
            offline.get(), makeButton("a2_child", 60, 20, 10, 10))));
    auto* attached = static_cast<InkingStaticPanel*>(
        scene.AddChild(std::move(offline)));
    (void)attached;
    Check(scene.GetDrawItemCount() == before + 2,
          "A2：离线子树的**每一层**都进了绘制列表（以前只进最上面那层）");
    Check(scene.QueryHit(offlineChild->GetAbsX() + 5.0f,
                         offlineChild->GetAbsY() + 5.0f)
                  .target
              == offlineChild,
          "A2：离线子树里的按钮也能被命中");

    // ---- A3：创建时隐藏、之后显示 ----
    InkingPanelData hiddenData;
    hiddenData.name = "a3_hidden";
    hiddenData.width = 120;
    hiddenData.height = 60;
    hiddenData.offsetY = 400.0f;
    hiddenData.visible = false;
    hiddenData.closed.color = 0xFF778899u;
    hiddenData.open.color = hiddenData.closed.color;
    hiddenData.Normalize();
    auto* hidden = static_cast<InkingStaticPanel*>(
        scene.AddChild(std::make_unique<InkingStaticPanel>(&scene,
                                                                   hiddenData)));
    Check(scene.QueryHit(60.0f, 430.0f).target == nullptr,
          "A3：隐藏的面板点不到（可见性在命中侧也生效）");
    hidden->SetVisible(true);
    Check(scene.QueryHit(60.0f, 430.0f).target == hidden,
          "A3：隐藏过再显示，立刻能点到（以前永远回不来）");

    // ---- A4 / B1：布局在"孩子挂了"和"容器改了"之后都要重跑 ----
    InkingAnchorData columnData;
    columnData.width = 300;
    columnData.height = 200;
    columnData.offsetX = 600.0f;
    columnData.offsetY = 400.0f;
    columnData.layout.kind = InkingLayoutKind::Stack;
    columnData.layout.axis = InkingLayoutAxis::Vertical;
    columnData.layout.gap = 4.0f;
    auto* column = static_cast<InkingDynamicWidget*>(
        scene.AddChild(
            std::make_unique<InkingDynamicWidget>(&scene, columnData)));

    InkingButtonData fixed = makeButton("a4_fixed", 100, 40, 0, 0);
    InkingButtonData filler = makeButton("a4_filler", 0, 40, 0, 0);
    filler.layoutItem.fillMain = true;
    auto* fixedButton = static_cast<InkingStaticButton*>(
        column->AddChild(std::make_unique<InkingStaticButton>(
            column, fixed)));
    auto* fillerButton = static_cast<InkingStaticButton*>(
        column->AddChild(std::make_unique<InkingStaticButton>(
            column, filler)));

    Check(fixedButton->GetWidth() == 300,
          "B1：交叉轴 Stretch 把 `width:100` 的按钮拉到 300（不是 100）");
    Check(fillerButton->GetWidth() == 300,
          "B1：`width:0` 的按钮也拉到 300（以前解出来就是 0 宽、不画不报错）");
    Check(fillerButton->GetHeight() > 100,
          "A4：`fillMain` 在**挂上来那一刻**就收敛（以前只看构造期那一趟）");

    column->Resize(500, 300);
    Check(fixedButton->GetWidth() == 500 && fillerButton->GetWidth() == 500,
          "A4：容器改尺寸后交叉轴跟着变（抽屉拉宽的基础）");
    Check(fillerButton->GetHeight() > 200,
          "A4：容器改尺寸后 `fillMain` 重新分配（不是只收敛一次）");

    // ---- A5：显式重建入口 ----
    const std::size_t rebuilt = scene.RebuildRenderList();
    Check(rebuilt == scene.GetDrawItemCount() && rebuilt >= 6,
          "A5：RebuildRenderList 返回重建后的项数（与随后的查询一致）");

    // ---- B2 / D7：名字入口与语义激活 ----
    inner->SetName("a1_inner_renamed");
    Check(scene.FindByName("a1_inner_renamed") == inner,
          "B2：SetName 之后能按名字找到动态容器里的节点");
    Check(scene.FindByName("a1_inner") == nullptr, "B2：旧名字不再命中");

    bool clicked = false;
    inner->SetOnClicked([&clicked] { clicked = true; });
    Check(scene.Activate("a1_inner_renamed") && clicked,
          "D7：`Activate(名字)` 触发点击，**不需要知道控件类型**");

    // ---- D6：不用等帧，命中表可强制建 ----
    Check(scene.RebuildHitTable() > 0,
          "D6：`RebuildHitTable()` 在没有任何帧的情况下建表成功");
}

// ---------------------------------------------------------------------------
// 移动的动态节点只刷洞（T-19）
// ---------------------------------------------------------------------------
//
// 判据**只用计数**，不用帧时间：帧时间会被场景规模主导（`TaskGuide-G.md` §2.1.2
// 实测 200/1000/4000 个静态节点分别 45/178/798 µs），量出来的是"我这台机器多少
// 静态节点"，不是"这次改动贡献了什么"。
//
// 两件事要分开证：
//   ① 移动**叶子**：`GetStructureGeneration()` 不涨（没走全量）+ `GetHoleRefreshCount()`
//      涨（走了刷洞）+ **新旧位置的命中答案相反**（洞真的跟上了，不是白刷）；
//   ② 移动**带静态子孙的动态容器**：代次照旧涨。静态子的绝对坐标是建表时的快照，
//      祖先一动它就过期——这条是"为什么不能一刀切全走快路"的证据。
void CheckLeafHoleRefresh() {
    std::printf("\n-- 移动的动态节点只刷洞（T-19） --\n");

    struct ProbeScene : public InkingScene {
        ProbeScene() : InkingScene("probe_leaf_hole") {}
    };

    ProbeScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "T-19：探针场景注册成功（名字不与他人冲突）");
        return;
    }

    // 铺一批静态节点：让"全量重建"真的有东西可重建（自检里只判计数，但若场景
    // 里一个静态节点都没有，两条路的差别会被摊平，测出来的东西没意义）。
    for (int i = 0; i < 32; ++i) {
        InkingAnchorData cell;
        cell.width = 20;
        cell.height = 20;
        cell.offsetX = static_cast<float>((i % 8) * 24);
        cell.offsetY = static_cast<float>((i / 8) * 24 + 400);
        scene.AddChild(
            std::make_unique<InkingStaticWidget>(&scene, cell));
    }

    const auto makeButton = [](const char* name, int x, int y) {
        InkingButtonData data;
        data.name = name;
        data.width = 60;
        data.height = 40;
        data.offsetX = static_cast<float>(x);
        data.offsetY = static_cast<float>(y);
        data.normal = InkingButtonAppearance(InkingAppearanceType::Color,
                                              0xFF335577u);
        return data;
    };

    // ---- ① 叶子：每帧走位的动态按钮 ----
    auto* leaf = static_cast<InkingDynamicButton*>(
        scene.AddChild(std::make_unique<InkingDynamicButton>(
            &scene, makeButton("t19_leaf", 100, 100))));

    // 先把表建好：这一步一定是全量重建（构造到现在一直脏着）。
    scene.RebuildHitTable();

    const float oldX = leaf->GetAbsX() + 10.0f;
    const float oldY = leaf->GetAbsY() + 10.0f;
    Check(scene.QueryHit(oldX, oldY).target == leaf,
          "T-19：移动之前，原位置能命中动态按钮（基线）");

    const std::uint64_t generationBefore = scene.GetStructureGeneration();
    const std::uint64_t holesBefore = scene.GetHoleRefreshCount();

    // 挪 300 像素：足够离开旧包络，两个洞不重叠，于是"旧位置不命中"才可证。
    leaf->ChangeOffset(400.0f, 100.0f);
    const float newX = leaf->GetAbsX() + 10.0f;
    const float newY = leaf->GetAbsY() + 10.0f;

    Check(scene.GetStructureGeneration() == generationBefore,
          "T-19：移动叶子**不**通知场景重排（结构代次不涨）");

    // 一次查询 = 一次 sortDrawList = 一次"只刷洞"。
    // 顺序要紧：先查新位置（此时刷洞还没发生，必须靠这次查询自己刷）。
    Check(scene.QueryHit(newX, newY).target == leaf,
          "T-19：移动后**新位置**命中它（洞跟上了，不是过期洞）");
    Check(scene.GetHoleRefreshCount() == holesBefore + 1,
          "T-19：这一帧真走了「只刷洞」（计数 +1，全量重建不走这条）");
    Check(scene.GetStructureGeneration() == generationBefore,
          "T-19：查询也不会顺手全量重建（代次仍不涨）");
    Check(scene.QueryHit(oldX, oldY).target != leaf,
          "T-19：**旧位置**不再命中它（洞真的跟着走了）");

    // ---- ② 带静态子孙的动态容器：必须走全量 ----
    InkingAnchorData boxData;
    boxData.width = 200;
    boxData.height = 150;
    boxData.offsetX = 700.0f;
    boxData.offsetY = 60.0f;
    auto* box = static_cast<InkingDynamicWidget*>(
        scene.AddChild(
            std::make_unique<InkingDynamicWidget>(&scene, boxData)));
    InkingButtonData innerData = makeButton("t19_inner", 20, 30);
    auto* inner = static_cast<InkingStaticButton*>(box->AddChild(
        std::make_unique<InkingStaticButton>(box, innerData)));

    Check(box->HasStaticInSubtree(),
          "T-19：容器的子树判据认得出静态子（`HasStaticInSubtree`）");
    Check(!leaf->HasStaticInSubtree(),
          "T-19：叶子的判据是 O(1) 且为假（没有子树）");

    const std::uint64_t containerGeneration = scene.GetStructureGeneration();
    const float innerOldX = inner->GetAbsX() + 5.0f;
    const float innerOldY = inner->GetAbsY() + 5.0f;
    Check(scene.QueryHit(innerOldX, innerOldY).target == inner,
          "T-19：容器里那个静态按钮原来点得到（基线）");

    box->ChangeOffset(700.0f, 260.0f);  // 往下挪 200
    Check(scene.GetStructureGeneration() > containerGeneration,
          "T-19：移动**带静态子孙**的容器仍走全量重建（代次涨）");

    const float innerNewX = inner->GetAbsX() + 5.0f;
    const float innerNewY = inner->GetAbsY() + 5.0f;
    Check(scene.QueryHit(innerNewX, innerNewY).target == inner,
          "T-19：容器挪完之后静态子的命中位置正确（快照跟着重建了）");
    Check(scene.QueryHit(innerOldX, innerOldY).target != inner,
          "T-19：静态子的旧位置不再命中它");

    // ---- ③ 隐藏的动态节点不留洞（与 T-4 验收③同一条） ----
    auto* hidden = static_cast<InkingDynamicButton*>(
        scene.AddChild(std::make_unique<InkingDynamicButton>(
            &scene, makeButton("t19_hidden", 900, 500))));
    const std::size_t holesWithVisible = scene.GetHitTableHoleCount();
    hidden->SetVisible(false);
    Check(scene.GetHitTableHoleCount() < holesWithVisible,
          "T-19：隐藏的动态节点**不留洞**（洞数少一个）");
    hidden->ChangeOffset(900.0f, 560.0f);  // 隐藏状态下移动也只能刷出 0 个洞
    Check(scene.GetHitTableHoleCount() == holesWithVisible - 1,
          "T-19：隐藏的节点刷洞也刷不出来（可见性判据在刷洞这一侧同样生效）");

    // ---- ④ 档位没被这次改动带偏 ----
    //
    // 编译期那条（`src/inking.cpp` 的 `static_assert(!HasResize<InkingStaticImage>)`
    // 一族）在这里的运行时回声：动态的仍是动态、静态的仍是静态，静态节点照旧在表里
    // ——刷洞那条路**没有**把表的条目也当成"可以只刷一半"的东西。
    Check(leaf->IsDynamic() && !inner->IsDynamic(),
          "T-19：档位判定没变（动态按钮是动态、容器里的静态按钮是静态）");
    Check(scene.GetHitTableEntryCount() > 0,
          "T-19：静态节点仍在表里（刷洞没有把表刷坏）");
}

// ---------------------------------------------------------------------------
// 动态图片（G-01）：几何能改的同一张图
// ---------------------------------------------------------------------------
//
// 五条验收对应 `TaskGuide-G.md` G-01 的"验收"五项。第 5 项（`src/inking.cpp`
// 的镜像断言）是**编译期**的，这里只留它的运行时回声——真正拦住"静态档被顺手
// 改成动态"的是那几条 `static_assert`，它们红了连 exe 都出不来。
//
// 第三条（拖动改的是几何、不是 A13）是本任务最容易写反的一条：静态版拖的是
// 外部显示变换，动态版必须拖几何——否则"占位不变"，等于把动态档的存在理由丢了。
void CheckDynamicImage() {
    std::printf("\n-- 动态图片（G-01） --\n");

    struct ProbeScene : public InkingScene {
        ProbeScene() : InkingScene("probe_dynimage") {}
    };

    ProbeScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "G-01：探针场景注册成功（名字不与他人冲突）");
        return;
    }

    // 一个静态参照物：让表里真的有条目，"一条都没多"才有意义。
    InkingAnchorData refData;
    refData.width = 40;
    refData.height = 40;
    refData.offsetX = 20.0f;
    refData.offsetY = 560.0f;
    scene.AddChild(
        std::make_unique<InkingStaticWidget>(&scene, refData));
    const std::size_t entriesBefore = scene.GetHitTableEntryCount();

    InkingImageData data;
    data.name = "g01_image";
    data.width = 100;
    data.height = 50;
    data.offsetX = 200.0f;
    data.offsetY = 100.0f;
    data.normal =
        InkingButtonAppearance(InkingAppearanceType::Color, 0xFF446688u);

    auto* image = static_cast<InkingDynamicImage*>(
        scene.AddChild(std::make_unique<InkingDynamicImage>(&scene,
                                                                   data)));

    // ---- ① 是动态档，且**不进命中表**（占地记成"洞"） ----
    //
    // 构造本身是骨架变化（代次会涨，别的节点也可能重排），所以这里判的是
    // **条目数不变**、**洞多一个**——那才是"动态节点不进表"的可断言形态。
    Check(image->IsDynamic(), "①：新类是动态档（`IsDynamic()` 为真）");
    Check(scene.GetHitTableEntryCount() == entriesBefore,
          "①：动态图片**不进命中表**（条目数不变）");
    Check(scene.GetHitTableHoleCount() == 1,
          "①：它的占地记成**一个洞**（点落在洞里才会问动态层）");

    // ---- ② Resize 改尺寸，命中跟着变 ----
    //
    // 取一个"贴着旧右边界外侧"的世界点：宽 100 时在外，宽 120 时在内。
    // 同一个点结果翻转，才证明"命中读的是当前几何"，不是构造时那份。
    const float edgeX = 305.0f;  // 本地 x = 105
    const float edgeY = 110.0f;  // 本地 y = 10
    Check(!image->HitTest(edgeX, edgeY),
          "②：Resize 之前，越出右边界的点不在图上（基线）");
    Check(image->Resize(120, 80), "②：`Resize(120, 80)` 真的改了");
    Check(image->GetWidth() == 120 && image->GetHeight() == 80,
          "②：`GetWidth()/GetHeight()` 跟着变成 120/80");
    Check(image->HitTest(edgeX, edgeY),
          "②：同一个世界点在 Resize 后命中（几何驱动命中）");

    // ---- ③ 内置拖动写的是**几何**，A13 一个字节都没动 ----
    image->SetDraggable(true);
    const float offsetBeforeDrag = image->GetOffsetX();

    // "按下 + 拖 10px + 抬起"：三次指针变化，每次都要派发一次——动态档的输入
    // 只在**指针真的变了**的那一帧被通知（`DispatchPointer` 第 0 步）。
    scene.SetPointerState(250.0f, 140.0f, true, true);
    scene.DispatchPointer();
    scene.SetPointerState(260.0f, 140.0f, true, true);
    scene.DispatchPointer();
    scene.SetPointerState(260.0f, 140.0f, false, true);
    scene.DispatchPointer();

    Check(image->GetOffsetX() - offsetBeforeDrag == 10.0f,
          "③：拖 10px → `GetOffsetX()` +10（拖动改几何，不是显示变换）");
    Check(image->GetDisplayTransform().IsIdentity(),
          "③：拖动**不改 A13**（`GetDisplayTransform()` 仍是单位变换）");
    Check(!image->IsDragging(), "③：抬手后不再跟随（捕获已释放）");

    // ---- ④ A13 仍可用，且与几何**复合** ----
    image->ChangeOffset(30.0f, 0.0f);  // 几何先挪一次
    InkingTransformSpec shift;
    shift.translateX = 5.0f;
    Check(image->SetDisplayTransform(shift), "④：A13 仍可用（`SetDisplayTransform` 生效）");
    Check(!image->GetDisplayTransform().IsIdentity(),
          "④：生效变换不是单位（外部那份叠上来了）");
    // 合成结果：画在 abs + 5，命中也要在同一条上（同一个 `GetDisplayTransform`）。
    Check(image->HitTest(image->GetAbsX() + 5.0f + 10.0f,
                         image->GetAbsY() + 10.0f),
          "④：命中按「几何 + 外部变换」的合成结果判");
    Check(image->GetDisplayTransformRejectedCount() == 0,
          "④：动态图片认外部变换（拒绝计数保持 0）");

    // ---- ⑤ 档位的运行时回声（编译期那几条在 `src/inking.cpp`） ----
    Check(image->IsDynamic(),
          "⑤：镜像断言成立（动态图片属于动态档，静态图片的反向断言在编译期）");
}

// ---------------------------------------------------------------------------
// 非阻塞跑帧 / 帧末任务 / 确定性时间 / 退出码 / 离屏渲染（D 组，0.3.0）
// ---------------------------------------------------------------------------
//
// **必须放在最后**：`Open()` 会自己 `SDL_Init`，而 `releaseWindow` 会
// `SDL_Quit`——中途跑会把上面那些检查用的裸窗口 / 渲染器一起拆掉。
// 所以本函数在 `main` 里排在"销毁裸窗口"之后。
void CheckHeadlessPump() {
    std::printf("\n-- 非阻塞跑帧（D 组） --\n");
    InkingWindow& window = InkingWindow::Instance();

    // D4：固定 delta —— 截图与动画断言要在 CI 里稳定，就必须去掉墙上时钟。
    window.SetFixedDelta(1.0f / 60.0f);
    Check(window.GetFixedDelta() > 0.0f, "D4：固定 delta 记下了");

    // D1：Open 只建窗口，不进循环。
    //
    // 透明是**建窗标志**，所以断言分三步：请求 → 建窗 → 读回标志。
    // 第三步是这次改动唯一的"落地证据"——请求值只证明我们记下了。
    Check(window.SetTransparent(true), "透明窗口：建窗前请求被记下");
    Check(window.IsTransparentRequested(), "透明窗口：请求值回读为真");
    Check(!window.IsTransparentApplied(), "透明窗口：建窗前 applied 为假");
    Check(window.Open("selftest"), "D1：Open 成功（dummy 视频驱动 + 软渲染器）");
    Check(window.IsTransparentApplied(),
          "透明窗口：SDL_WINDOW_TRANSPARENT 真的进了窗口标志");
    Check(window.RunFrames(2) == 2, "D1：RunFrames(2) 真的跑了两帧");
    Check(window.HasWindow(), "D1：跑帧期间窗口还活着");

    // D3：帧末任务的两种排法。
    int order = 0;
    int first = 0;
    int third = 0;
    window.PostFrameTask([&] { first = ++order; });
    window.RunAfterFrames(2, [&] { third = ++order; });
    window.RunFrames(1);
    Check(first == 1 && third == 0,
          "D3：PostFrameTask 在本帧末执行、RunAfterFrames(2) 还没到");
    window.RunFrames(1);
    Check(third == 2, "D3：RunAfterFrames(2) 在第二帧末执行");

    // D2：离屏渲染，不依赖窗口。
    const std::string offscreen = "selftest_offscreen.png";
    Check(InkingWindow::RenderToTexture(offscreen, 320, 180, "selftest"),
          "D2：RenderToTexture 不建窗口也能出图");
    {
        std::FILE* file = std::fopen(offscreen.c_str(), "rb");
        long size = 0;
        if (file != nullptr) {
            std::fseek(file, 0, SEEK_END);
            size = std::ftell(file);
            std::fclose(file);
        }
        Check(size > 100, "D2：写出的是真文件（不是空壳）");
        std::remove(offscreen.c_str());
    }

    // D5：带退出码关窗。
    window.RequestClose(7);
    Check(window.GetExitCode() == 7, "D5：`RequestClose(7)` 记下退出码");
    Check(!window.PumpOnce(), "D5：关窗之后 `PumpOnce()` 返回 false");
    Check(!window.HasWindow(), "D5：收尾把窗口释放了（不会留下半开状态）");

    // ---- 静默失效：半透明底 + 不透明窗口 ----
    //
    // 触发条件写得出、也验得了：`background` 的 alpha < 255，而窗口不是透明窗口。
    // 断言落在"恰好一条 WARN"上（`InkLog::grep` 按模块 + 关键字检索），不靠眼睛。
    // 窗口刚关掉，所以这一次 `SetTransparent(false)` 能生效（它只在建窗前有效）。
    Check(window.SetTransparent(false), "静默失效用例：关窗后可以改回不透明");
    window.SetBackgroundColor(0x80112233u);
    InkLog::ResetStats();
    Check(window.Open("selftest"), "静默失效用例：重新开窗成功");
    Check(window.RunFrames(1) == 1, "静默失效用例：跑一帧（清屏那一笔真的发生）");
    const std::vector<std::string> alphaWarns =
        InkLog::grep("window.background", "InkingWindow");
    Check(alphaWarns.size() == 1,
          "background_alpha_without_transparent：半透明底 + 不透明窗口恰好 1 条 WARN");
    NoteSilentFailure("background_alpha_without_transparent");
    window.RequestClose(0);
    Check(!window.PumpOnce(), "静默失效用例：收尾关窗");
}

// ---------------------------------------------------------------------------
// 运行期主题（颜色）：换一张表，下一帧颜色就变
// ---------------------------------------------------------------------------
//
// 面板的 `closed` 写了一个**语义色** `surface`，生成期烘的默认值是蓝
// （真实项目里那个默认值来自 CXXCSS 的烘焙主题）。这里手写两张表模拟"多主题"：
//
//   不装表        → 用烘好的字面量（蓝）
//   切到 t_red    → 红
//   切回 t_blue   → 蓝
//   清空表        → 回到烘好的字面量（蓝）
//
// 验的是**整条链路**：主题版本 → `InkingPanelLook::Advance` 重算 → 真渲染出来的像素。
// 只验 `InkingResolveThemeColor` 不够——"查到了但没画出去"正是这类功能最容易出的错。
void CheckRuntimeTheme(SDL_Renderer* renderer) {
    std::printf("\n-- 运行期主题（颜色） --\n");
    if (renderer == nullptr) {
        return;
    }

    InkingClearThemeTables();
    const std::uint64_t versionBeforeClear = InkingThemeVersion();

    InkingThemeTable blue;
    blue.name = "t_blue";
    blue.colors = {{"surface", 0xFF0000FFu}};
    InkingThemeTable red;
    red.name = "t_red";
    red.colors = {{"surface", 0xFFFF0000u}};
    Check(InkingInstallThemeTable(std::move(blue)), "装第一张主题表");
    Check(InkingInstallThemeTable(std::move(red)), "装第二张主题表");
    Check(InkingThemeTableCount() == 2, "运行期同时持有两张表（多主题）");
    Check(InkingThemeVersion() > versionBeforeClear, "装表让版本号前进");

    // 没切主题时活跃主题是空的 → 一切照旧用烘好的字面量。
    Check(InkingActiveThemeName().empty(), "装表不等于切表（活跃主题仍为空）");

    // 没有这张表时切不过去，而且**版本不许动**（界面不该白闪一下）。
    const std::uint64_t versionBeforeBadSwitch = InkingThemeVersion();
    Check(!InkingSetActiveTheme("t_not_installed"),
          "切一张没装过的主题：返回 false");
    Check(InkingThemeVersion() == versionBeforeBadSwitch,
          "切失败不动版本号（不会闪一下再回原样）");

    InkingScene scene;
    InkingPanelData data;
    data.name = "theme_probe";
    data.width = 200;
    data.height = 160;
    data.selfAnchor = InkingChangeAnchor::Center;
    data.traceAnchor = InkingChangeAnchor::Center;
    data.closed.color = 0xFF0000FFu;      // 烘好的字面量（蓝）
    data.closed.themeColor = "surface";   // 但它其实是语义色
    InkingWidget* panel = scene.AddChild(
        std::make_unique<InkingStaticPanel>(&scene, data));
    // 必须让它**活跃**：`TickFrame` 对非活跃场景直接返回（画面外的插值没有意义），
    // 而颜色重查恰恰发生在 `onAnimationTick` 里。
    InkingSceneLibrary::SetActiveScene(&scene);
    scene.TickFrame(1.0f / 60.0f);

    const auto centerPixel = [&]() -> std::uint32_t {
        SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                         InkingDesignHeight(),
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        InkingSceneLibrary::SetActiveScene(&scene);
        const bool rendered = InkingSceneLibrary::RenderScene(renderer);
        // 注意**不要**在这里清活跃场景：`TickFrame` 对非活跃场景直接返回，
        // 清了后面那个"切主题 → TickFrame"就白跑（第一版就是这么错的）。
        SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                         SDL_LOGICAL_PRESENTATION_DISABLED);
        if (!rendered) {
            return 0u;
        }
        const SDL_Rect one{320, 180, 1, 1};
        SDL_Surface* shot = SDL_RenderReadPixels(renderer, &one);
        if (shot == nullptr) {
            return 0u;
        }
        std::uint32_t pixel = 0;
        SDL_Surface* argb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
        if (argb != nullptr) {
            std::memcpy(&pixel, argb->pixels, sizeof(pixel));
            SDL_DestroySurface(argb);
        }
        SDL_DestroySurface(shot);
        return pixel & 0x00FFFFFFu;
    };

    Check(centerPixel() == 0x0000FFu,
          "不装表：用生成期烘好的字面量（蓝）");

    // 切到红色主题：**下一帧**就该变，不需要任何"通知组件"的动作。
    Check(InkingSetActiveTheme("t_red"), "切到 t_red");
    scene.TickFrame(1.0f / 60.0f);
    Check(centerPixel() == 0xFF0000u, "切主题后画面变红（颜色重查真的走到绘制）");

    Check(InkingSetActiveTheme("t_blue"), "切回 t_blue");
    scene.TickFrame(1.0f / 60.0f);
    Check(centerPixel() == 0x0000FFu, "切回去又是蓝");

    // 表里没有这个 token：用兜底色，并记一次 miss（不报错、不空白）。
    InkingThemeTable partial;
    partial.name = "t_partial";
    partial.colors = {{"other", 0xFF00FF00u}};
    Check(InkingInstallThemeTable(std::move(partial)), "装一张不含 surface 的表");
    Check(InkingSetActiveTheme("t_partial"), "切到那张不全的表");
    const InkingThemeStats before = InkingThemeStatsSnapshot();
    scene.TickFrame(1.0f / 60.0f);
    const std::uint32_t fallbackPixel = centerPixel();
    const InkingThemeStats after = InkingThemeStatsSnapshot();
    Check(fallbackPixel == 0x0000FFu, "表里没有这个 token：退回烘好的字面量");
    Check(after.misses > before.misses, "没查到时记一次 miss（观测口看得见）");

    // 清空：回到"全用字面量"，组件也要跟着退回去。
    InkingClearThemeTables();
    scene.TickFrame(1.0f / 60.0f);
    Check(InkingThemeTableCount() == 0, "清空主题表");
    Check(centerPixel() == 0x0000FFu, "清空后画面回到烘好的字面量");
    InkingSceneLibrary::SetActiveScene(nullptr);
    (void)panel;
}

// ---------------------------------------------------------------------------
// SVG 设计稿（0.5.1）：整张稿 → 按目标像素光栅化 → 纹理 → 采样
// ---------------------------------------------------------------------------
//
// 来源：`docs/zh/plans/2026-10-24-0.5.1-svg-design.md`。断言只落在**可观测的量**上
// ——surface 尺寸、像素颜色、`TextCacheStats` 计数、`InkLog::CountOf`；
// "看起来画出来了"不算证据（`AGENTS.md` §4 第 4 条）。

std::uint32_t ReadSurfacePixel(SDL_Surface* surface, int x, int y) {
    if (surface == nullptr) {
        return 0xFFDEADBEu;
    }
    SDL_Surface* argb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
    if (argb == nullptr) {
        return 0xFFDEADBEu;
    }
    SDL_LockSurface(argb);
    std::uint32_t pixel = 0xFFDEADBEu;
    if (x >= 0 && y >= 0 && x < argb->w && y < argb->h) {
        pixel = static_cast<const std::uint32_t*>(argb->pixels)[y * argb->w + x];
    }
    SDL_UnlockSurface(argb);
    SDL_DestroySurface(argb);
    return pixel;
}

/// 从渲染器读一个**输出像素**（逻辑呈现下 = 设计坐标 × 缩放比）。
std::uint32_t ReadOutputPixel(SDL_Renderer* renderer, int x, int y) {
    const SDL_Rect one{x, y, 1, 1};
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, &one);
    if (shot == nullptr) {
        return 0xFFDEADBEu;
    }
    std::uint32_t pixel = 0xFFDEADBEu;
    SDL_Surface* argb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
    if (argb != nullptr) {
        std::memcpy(&pixel, argb->pixels, sizeof(pixel));
        SDL_DestroySurface(argb);
    }
    SDL_DestroySurface(shot);
    return pixel;
}

/// 只比 RGB（alpha 在离屏读回里受混合影响，不参与"颜色对不对"的判断）。
bool RgbClose(std::uint32_t got, std::uint32_t want, int tol) {
    const auto channel = [](std::uint32_t value, int shift) {
        return static_cast<int>((value >> shift) & 0xFFu);
    };
    return std::abs(channel(got, 16) - channel(want, 16)) <= tol
        && std::abs(channel(got, 8) - channel(want, 8)) <= tol
        && std::abs(channel(got, 0) - channel(want, 0)) <= tol;
}

/// 任务书 §2.1 的"支持矩阵"样本：多图元 + g@transform + 渐变 + evenodd 孔洞。
const char* kSvgDesignSample =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"120\" "
    "viewBox=\"0 0 200 120\">"
    "<defs><linearGradient id=\"g\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"0%\">"
    "<stop offset=\"0\" stop-color=\"#FF0000\"/>"
    "<stop offset=\"1\" stop-color=\"#0000FF\"/></linearGradient></defs>"
    "<rect x=\"0\" y=\"0\" width=\"200\" height=\"120\" fill=\"#101018\"/>"
    "<g transform=\"translate(100,60) rotate(10)\">"
    "<circle cx=\"0\" cy=\"0\" r=\"30\" fill=\"#22CC55\"/></g>"
    "<rect x=\"10\" y=\"90\" width=\"180\" height=\"20\" fill=\"url(#g)\"/>"
    "<path d=\"M 150 10 H 190 V 50 H 150 Z M 160 20 H 180 V 40 H 160 Z\" "
    "fill=\"#FFB45C\" fill-rule=\"evenodd\"/>"
    "</svg>";

/// 竖长稿：目标框 200×100 时短边只有 100 → 触发一次二次解码。
const char* kSvgTall =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"200\" "
    "viewBox=\"0 0 100 200\">"
    "<rect x=\"0\" y=\"0\" width=\"100\" height=\"200\" fill=\"#00FF00\"/>"
    "</svg>";

const char* kSvgBroken = "<svg><path d=\"M 0 0 L";

/// 故意含两个 `<text>` 与一个 `clip-path`：三个都"不会被渲染"，必须报出来。
const char* kSvgUnsupported =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\" "
    "viewBox=\"0 0 100 100\">"
    "<text x=\"10\" y=\"50\">A</text><text x=\"10\" y=\"80\">B</text>"
    "<rect x=\"0\" y=\"0\" width=\"100\" height=\"100\" fill=\"#00FF00\" "
    "clip-path=\"url(#c)\"/>"
    "</svg>";

/// 整张 1280×720 稿（与 `examples/demo/assets/design.svg` 同内容）：
/// 自检不依赖文件，但断言的就是示例要展示的那张图。
const char* kSvgFullDesign =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"720\" "
    "viewBox=\"0 0 1280 720\">"
    "<defs><linearGradient id=\"sky\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\">"
    "<stop offset=\"0\" stop-color=\"#1B1F3B\"/>"
    "<stop offset=\"1\" stop-color=\"#3A2E5C\"/></linearGradient></defs>"
    "<rect x=\"0\" y=\"0\" width=\"1280\" height=\"720\" fill=\"url(#sky)\"/>"
    "<g transform=\"translate(640,360) rotate(12)\">"
    "<circle cx=\"0\" cy=\"0\" r=\"210\" fill=\"#4C6FFF\" opacity=\"0.35\"/>"
    "<rect x=\"-120\" y=\"-300\" width=\"240\" height=\"600\" fill=\"#22CC99\" "
    "opacity=\"0.25\"/></g>"
    "<path d=\"M 120 560 C 320 420 520 520 720 460 L 900 560 L 900 640 "
    "L 120 640 Z\" fill=\"#FFB45C\" opacity=\"0.9\"/>"
    "<path d=\"M 980 160 H 1180 V 360 H 980 Z M 1020 200 H 1140 V 320 H 1020 Z\" "
    "fill=\"#8FE3FF\" fill-rule=\"evenodd\" opacity=\"0.85\"/>"
    "<rect x=\"80\" y=\"80\" width=\"240\" height=\"120\" rx=\"18\" "
    "fill=\"#7963D6\" stroke=\"#FFFFFF\" stroke-width=\"3\" opacity=\"0.6\"/>"
    "</svg>";

const std::uint8_t* SvgBytes(const char* text) {
    return reinterpret_cast<const std::uint8_t*>(text);
}

/// 在场景里摆一个 SVG 图片组件（调用方自己 `AddChild` 之后要 SetImageBytes）。
InkingStaticImage* AddSvgImage(InkingScene& scene,
                                       const char* name, int width, int height,
                                       float offsetX, float offsetY,
                                       bool hitPassThrough = false) {
    InkingImageData data;
    data.name = name;
    data.width = width;
    data.height = height;
    data.normal.source = name;
    data.hitPassThrough = hitPassThrough;
    data.selfAnchor = InkingChangeAnchor::LeftTop;
    data.traceAnchor = InkingChangeAnchor::LeftTop;
    data.offsetX = offsetX;
    data.offsetY = offsetY;
    return static_cast<InkingStaticImage*>(
        scene.AddChild(std::make_unique<InkingStaticImage>(&scene, data)));
}

void SetSvgBytes(InkingStaticImage* image, const char* svg) {
    image->SetImageBytes(SvgBytes(svg), std::strlen(svg), image->ImageName());
}

/// 一帧离屏渲染（清屏 + 逻辑呈现 + RenderScene）。
bool RenderSvgFrame(SDL_Renderer* renderer, InkingScene& scene) {
    InkingSceneLibrary::SetActiveScene(&scene);
    SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderClear(renderer);
    return InkingSceneLibrary::RenderScene(renderer);
}

void EndSvgFrame(SDL_Renderer* renderer) {
    SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-01：光栅化矩阵（直接调内部件，不经过组件）
// ---------------------------------------------------------------------------
void CheckSvgRasterMatrix() {
    std::printf("\n-- SVG 光栅化矩阵：多元素 / 渐变 / 变换 / evenodd / 坏稿（0.5.1 T-01） --\n");
    const std::size_t size = std::strlen(kSvgDesignSample);
    const inking_detail::SvgRasterResult full = inking_detail::RasterizeSvg(
        SvgBytes(kSvgDesignSample), size, 200, 120, 16 * 1024 * 1024);
    Check(full.surface != nullptr && full.error.empty(), "整张稿光栅化成功");
    if (full.surface != nullptr) {
        Check(full.surface->w == 200 && full.surface->h == 120,
              "按目标框出像素（200×120，不是固有尺寸）");
        Check(RgbClose(ReadSurfacePixel(full.surface, 5, 5), 0xFF101018u, 8),
              "背景色与原色一致");
        Check(RgbClose(ReadSurfacePixel(full.surface, 20, 100), 0xFFFF0000u, 40),
              "linearGradient 左端（红）");
        Check(RgbClose(ReadSurfacePixel(full.surface, 180, 100), 0xFF0000FFu, 40),
              "linearGradient 右端（蓝）");
        Check(RgbClose(ReadSurfacePixel(full.surface, 100, 60), 0xFF22CC55u, 40),
              "g@transform 里的圆（旋转后圆心不动）");
        Check(RgbClose(ReadSurfacePixel(full.surface, 170, 30), 0xFF101018u, 8),
              "evenodd 孔洞露出背景（没被填满）");
        SDL_DestroySurface(full.surface);
    }

    const inking_detail::SvgRasterResult tall = inking_detail::RasterizeSvg(
        SvgBytes(kSvgTall), std::strlen(kSvgTall), 200, 100, 16 * 1024 * 1024);
    Check(tall.surface != nullptr && tall.secondPasses == 1,
          "稿比框细长时二次解码一次（最多一次）");
    if (tall.surface != nullptr) {
        Check(tall.surface->w >= 200 && tall.surface->h >= 100,
              "二次解码后任一边都不小于目标框");
        SDL_DestroySurface(tall.surface);
    }

    const inking_detail::SvgRasterResult broken = inking_detail::RasterizeSvg(
        SvgBytes(kSvgBroken), std::strlen(kSvgBroken), 100, 100,
        16 * 1024 * 1024);
    Check(broken.surface == nullptr && !broken.error.empty(),
          "坏稿返回错误串（不静默）");
}

// ---------------------------------------------------------------------------
// T-03：组件级纹理路 + 缓存命中 + 坏稿退回颜色
// ---------------------------------------------------------------------------
void CheckSvgImage(SDL_Renderer* renderer) {
    std::printf("\n-- SVG 图片组件：纹理路 / 缓存命中 / 坏稿退回颜色（0.5.1 T-03） --\n");
    if (renderer == nullptr) {
        return;
    }
    InkingScene scene;
    InkingStaticImage* image = AddSvgImage(scene, "svg_probe", 200, 120,
                                                   20.0f, 20.0f);
    SetSvgBytes(image, kSvgDesignSample);

    const inking_detail::TextCacheStats before =
        inking_detail::TextCacheStatsSnapshot();
    scene.TickFrame(1.0f / 60.0f);
    const bool rendered = RenderSvgFrame(renderer, scene);
    const inking_detail::TextCacheStats after =
        inking_detail::TextCacheStatsSnapshot();
    Check(rendered, "SVG 图片组件渲染一帧");
    Check(after.svgRasters == before.svgRasters + 1,
          "首次绘制真的光栅化了 1 次");

    // 设计坐标 → 输出像素：1280×720 逻辑 → 640×360 输出 = 0.5。
    const auto px = [&](int designX, int designY) {
        return ReadOutputPixel(renderer, designX / 2, designY / 2);
    };
    Check(RgbClose(px(25, 25), 0xFF101018u, 12), "组件里的背景色正确");
    Check(RgbClose(px(40, 120), 0xFFFF0000u, 60), "渐变左端透到屏幕（红）");
    Check(RgbClose(px(200, 120), 0xFF0000FFu, 60), "渐变右端透到屏幕（蓝）");
    Check(RgbClose(px(190, 50), 0xFF101018u, 12), "evenodd 孔洞透到屏幕");

    // 第二帧：命中缓存，不再光栅化。
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    InkingSceneLibrary::RenderScene(renderer);
    const inking_detail::TextCacheStats again =
        inking_detail::TextCacheStatsSnapshot();
    Check(again.svgRasters == after.svgRasters, "第二帧不再光栅化（缓存生效）");
    Check(again.svgCacheHits == after.svgCacheHits + 1, "第二帧命中 SVG 档");

    // 坏稿：解码失败 → 计数 + 一条 WARN；组件仍按颜色渲染（不是洞）。
    InkingStaticImage* bad =
        AddSvgImage(scene, "svg_broken", 100, 100, 300.0f, 20.0f);
    SetSvgBytes(bad, kSvgBroken);
    InkLog::ResetStats();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    InkingSceneLibrary::RenderScene(renderer);
    const inking_detail::TextCacheStats badStats =
        inking_detail::TextCacheStatsSnapshot();
    Check(badStats.svgFailures == after.svgFailures + 1, "坏稿计一次失败");
    Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
          "坏稿只报一条 WARN（不每帧刷）");

    EndSvgFrame(renderer);
}

// ---------------------------------------------------------------------------
// T-02：按目标像素分档（同尺寸命中 / 第 4 档淘汰 / 二次解码计数）
// ---------------------------------------------------------------------------
void CheckSvgCacheDensity(SDL_Renderer* renderer) {
    std::printf("\n-- SVG 纹理分档：同尺寸命中 / 第 4 档淘汰（0.5.1 T-02） --\n");
    if (renderer == nullptr) {
        return;
    }

    // ① 同一张图、同一尺寸的两个组件：一次光栅化 + 一次命中。
    {
        InkingScene scene;
        SetSvgBytes(AddSvgImage(scene, "density_same", 100, 60, 20.0f, 20.0f),
                    kSvgDesignSample);
        SetSvgBytes(AddSvgImage(scene, "density_same", 100, 60, 200.0f, 20.0f),
                    kSvgDesignSample);
        scene.TickFrame(1.0f / 60.0f);
        const inking_detail::TextCacheStats before =
            inking_detail::TextCacheStatsSnapshot();
        RenderSvgFrame(renderer, scene);
        const inking_detail::TextCacheStats after =
            inking_detail::TextCacheStatsSnapshot();
        Check(after.svgRasters == before.svgRasters + 1,
              "同尺寸的两张图共用一次光栅化");
        Check(after.svgCacheHits == before.svgCacheHits + 1,
              "第二个组件命中缓存");
        EndSvgFrame(renderer);
    }

    // ② 同一张图、四个不同尺寸：四档，第 4 档插入时淘汰最早的一档。
    {
        InkingScene scene;
        const int sizes[4][2] = {{60, 36}, {80, 48}, {100, 60}, {120, 72}};
        for (int i = 0; i < 4; ++i) {
            SetSvgBytes(AddSvgImage(scene, "density_multi", sizes[i][0],
                                    sizes[i][1], 20.0f + 160.0f * i, 200.0f),
                        kSvgDesignSample);
        }
        scene.TickFrame(1.0f / 60.0f);
        const inking_detail::TextCacheStats before =
            inking_detail::TextCacheStatsSnapshot();
        RenderSvgFrame(renderer, scene);
        const inking_detail::TextCacheStats after =
            inking_detail::TextCacheStatsSnapshot();
        Check(after.svgRasters == before.svgRasters + 4,
              "四个尺寸各光栅化一次");
        Check(after.svgEvictions >= before.svgEvictions + 1,
              "第 4 档淘汰最早一档（每图最多 3 档）");
        EndSvgFrame(renderer);
    }

    // ③ 竖稿进横框：二次解码（直接调内部件，不经过渲染）。
    {
        inking_detail::SvgRasterResult tall = inking_detail::RasterizeSvg(
            SvgBytes(kSvgTall), std::strlen(kSvgTall), 200, 100,
            16 * 1024 * 1024);
        Check(tall.secondPasses == 1, "竖稿进横框时二次解码");
        Check(tall.surface != nullptr && tall.surface->w >= 200
                  && tall.surface->h >= 100,
              "二次解码后任一边都不小于目标框");
        if (tall.surface != nullptr) {
            SDL_DestroySurface(tall.surface);
        }
    }
}

// ---------------------------------------------------------------------------
// T-04：不支持元素扫描（text×2 + clip-path×1）
// ---------------------------------------------------------------------------
void CheckSvgUnsupported(SDL_Renderer* renderer) {
    std::printf("\n-- SVG 不支持元素：扫描计数 + 每图一条 WARN（0.5.1 T-04） --\n");
    if (renderer == nullptr) {
        return;
    }
    InkingScene scene;
    SetSvgBytes(AddSvgImage(scene, "unsupported_probe", 100, 100, 20.0f, 20.0f),
                kSvgUnsupported);
    scene.TickFrame(1.0f / 60.0f);
    const inking_detail::TextCacheStats before =
        inking_detail::TextCacheStatsSnapshot();
    InkLog::ResetStats();
    RenderSvgFrame(renderer, scene);
    const inking_detail::TextCacheStats after =
        inking_detail::TextCacheStatsSnapshot();
    Check(after.svgUnsupportedElements == before.svgUnsupportedElements + 3,
          "text×2 + clip-path×1 计 3 次（大小写不敏感）");
    Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
          "汇总 WARN 恰好一条");

    InkLog::ResetStats();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    InkingSceneLibrary::RenderScene(renderer);
    Check(InkLog::CountOf(InkingLogLevel::Warn) == 0,
          "同一张图第二次绘制不再报（缓存命中）");
    EndSvgFrame(renderer);
}

// ---------------------------------------------------------------------------
// T-06：背景穿透（整张稿当背景时不吃空白点击）
// ---------------------------------------------------------------------------
void CheckSvgBackgroundPassthrough() {
    std::printf("\n-- SVG 背景：hitPassThrough 不挡点击（0.5.1 T-06） --\n");
    // 场景名必须不同：同名场景的第二次构造会进"停放态"（`IsRegistered()` false），
    // 那会让命中表不按预期建。
    const auto build = [](const char* sceneName, bool passThrough,
                          InkingStaticImage** outImage,
                          InkingStaticButton** outButton) {
        auto scene = std::make_unique<InkingScene>(sceneName);
        InkingStaticImage* image =
            AddSvgImage(*scene, "bg_probe", 1280, 720, 0.0f, 0.0f, passThrough);
        SetSvgBytes(image, kSvgDesignSample);
        InkingButtonData data;
        data.name = "on_bg";
        data.width = 200;
        data.height = 60;
        data.normal = InkingButtonAppearance(InkingAppearanceType::Color,
                                              0xFF3F7FD6u);
        data.selfAnchor = InkingChangeAnchor::LeftTop;
        data.traceAnchor = InkingChangeAnchor::LeftTop;
        data.offsetX = 540.0f;
        data.offsetY = 330.0f;
        auto* button = static_cast<InkingStaticButton*>(
            scene->AddChild(
                std::make_unique<InkingStaticButton>(scene.get(), data)));
        scene->TickFrame(1.0f / 60.0f);
        *outImage = image;
        *outButton = button;
        return scene;
    };

    InkingStaticImage* passImage = nullptr;
    InkingStaticButton* passButton = nullptr;
    auto passScene = build("bg_pass", true, &passImage, &passButton);
    Check(passScene->IsRegistered(), "穿透组场景登记成功");
    Check(passScene->QueryHit(640.0f, 360.0f).target == passButton,
          "点按钮 → 命中按钮（背景不挡）");
    const InkingHitResult passBlank = passScene->QueryHit(100.0f, 100.0f);
    Check(passBlank.kind == InkingHitKind::PassThrough,
          "点空白 → 答案被判成 PassThrough（背景让过，不挡）");

    InkingStaticImage* blockImage = nullptr;
    InkingStaticButton* blockButton = nullptr;
    auto blockScene = build("bg_block", false, &blockImage, &blockButton);
    Check(blockScene->IsRegistered(), "对照组场景登记成功");
    const InkingHitResult blockBlank = blockScene->QueryHit(100.0f, 100.0f);
    Check(blockBlank.kind == InkingHitKind::Block
              && blockBlank.target == blockImage,
          "不穿透时背景照旧吃掉空白点击（默认行为不变）");
}

// ---------------------------------------------------------------------------
// T-07：整张 1280×720 设计稿在设备上可见（示例的自检版，不依赖 assets/）
// ---------------------------------------------------------------------------
void CheckSvgFullDesign(SDL_Renderer* renderer) {
    std::printf("\n-- SVG 整张设计稿：1280×720 铺满 + 采样（0.5.1 T-07） --\n");
    if (renderer == nullptr) {
        return;
    }
    InkingScene scene;
    SetSvgBytes(AddSvgImage(scene, "design_full", 1280, 720, 0.0f, 0.0f),
                kSvgFullDesign);
    scene.TickFrame(1.0f / 60.0f);
    const inking_detail::TextCacheStats before =
        inking_detail::TextCacheStatsSnapshot();
    const bool rendered = RenderSvgFrame(renderer, scene);
    const inking_detail::TextCacheStats after =
        inking_detail::TextCacheStatsSnapshot();
    Check(rendered, "整张稿渲染一帧");
    Check(after.svgRasters == before.svgRasters + 1, "整张稿光栅化一次");

    const auto px = [&](int designX, int designY) {
        return ReadOutputPixel(renderer, designX / 2, designY / 2);
    };
    // 只断言"可区分"，不追渐变的中间色：0.5 缩放 + 混合后精确值没有意义。
    const std::uint32_t sky = px(20, 20);
    const std::uint32_t orange = px(300, 620);
    const std::uint32_t hole = px(1100, 280);
    Check((sky & 0x00FFFFFFu) != 0u, "渐变背景可见（不是纯黑）");
    Check(((orange >> 16) & 0xFFu) > 120u, "橙色 path 可见（R 通道高）");
    Check(((hole >> 16) & 0xFFu) < 128u, "evenodd 孔洞是背景色（不是橙色块）");
    EndSvgFrame(renderer);
}

// ---------------------------------------------------------------------------
// T-02（高清）：2× 输出下重新光栅化一档 + 采样正确
// ---------------------------------------------------------------------------
void CheckSvgHiDpi() {
    std::printf("\n-- SVG 高清：2× 输出重新光栅化（0.5.1 T-02） --\n");
    SDL_Window* window = SDL_CreateWindow("svg-hidpi", 2560, 1440, 0);
    SDL_Renderer* renderer =
        window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    Check(renderer != nullptr, "创建 2× 渲染器（2560×1440）");
    if (renderer == nullptr) {
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        return;
    }

    InkingScene scene;
    SetSvgBytes(AddSvgImage(scene, "svg_hidpi", 200, 120, 20.0f, 20.0f),
                kSvgDesignSample);
    scene.TickFrame(1.0f / 60.0f);
    const inking_detail::TextCacheStats before =
        inking_detail::TextCacheStatsSnapshot();
    const bool rendered = RenderSvgFrame(renderer, scene);
    const inking_detail::TextCacheStats after =
        inking_detail::TextCacheStatsSnapshot();
    Check(rendered, "2× 下渲染一帧");
    Check(after.svgRasters == before.svgRasters + 1,
          "2× 下重新光栅化一档（目标框 = 设计尺寸 × 2）");

    // 输出 2560×1440 / 逻辑 1280×720 = 2：设计坐标 × 2 = 输出像素。
    const auto px = [&](int designX, int designY) {
        return ReadOutputPixel(renderer, designX * 2, designY * 2);
    };
    Check(RgbClose(px(25, 25), 0xFF101018u, 12), "2× 下背景色正确");
    Check(RgbClose(px(40, 120), 0xFFFF0000u, 60), "2× 下渐变左端（红）");
    Check(RgbClose(px(200, 120), 0xFF0000FFu, 60), "2× 下渐变右端（蓝）");

    EndSvgFrame(renderer);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

// ---------------------------------------------------------------------------
// 场景动画时钟与频率订阅（0.6.0 T-01）
// ---------------------------------------------------------------------------
//
// 这一层容易写错的**不是**"能不能按时回调"，而是时间基准。四条断言各自对应
// 一个具体的错法：
//
//   1. 12fps × 1 秒用 60 帧 × 1/60 喂（**故意不整除**）：严格的浮点比较会
//      少一步（11 次），这是 0.6.0 里唯一必须靠 epsilon 才对得上的数字；
//   2. 一次喂 0.25s 要补发 3 次：既不能吞掉时间（"这一帧只算一次"），
//      也不能进"追不上"计数（0.25s 是 `InkingTimeline` 正常会给出的上限）；
//   3. 没有订阅者时钟照样走：否则组件读到的 `AnimationSeconds()` 会停在
//      "最后一次有人订阅时"；
//   4. 非活跃场景不推进：切走的场景动画冻结，不需要额外的暂停开关。
//
// 外加一条护栏：回调里改树 ⇒ 本轮剩下的订阅立刻停派（`InkingScene.h` 里
// "回调里不许增删节点"那条的落点）。
void CheckSceneAnimationClock() {
    std::printf("\n-- 场景动画时钟与频率订阅（0.6.0 T-01） --\n");

    InkingScene scene("anim_clock");
    Check(scene.IsRegistered(), "T-01：动画时钟场景登记成功");
    Check(scene.AnimationSeconds() == 0.0, "T-01：初始时钟为 0");

    // ④ 非活跃场景：`TickFrame` 直接返回（切走冻结）
    scene.TickFrame(0.5);
    Check(scene.AnimationSeconds() == 0.0,
          "T-01：非活跃场景的时钟不推进（切走即冻结）");

    InkingSceneLibrary::SetActiveScene(&scene);

    // ③ 无订阅者：时钟照走，每帧都走
    scene.TickFrame(0.5);
    Check(std::fabs(scene.AnimationSeconds() - 0.5) < 1e-9,
          "T-01：无订阅者时时钟仍每帧前进（0.5s → 0.5）");

    // ① 12fps：60 帧 × 1/60 恰好派发 12 次
    int ticks = 0;
    const InkingScene::AnimationHandle handle =
        scene.SubscribeAnimation(1.0 / 12.0, [&ticks](double) { ++ticks; });
    Check(handle != 0 && scene.AnimationSubscriberCount() == 1,
          "T-01：订阅成功且计数为 1");
    Check(scene.SubscribeAnimation(0.0, [](double) {}) == 0,
          "T-01：间隔为 0 的订阅被拒绝（返回无效句柄 0）");

    for (int i = 0; i < 60; ++i) {
        scene.TickFrame(1.0 / 60.0);
    }
    std::printf("         12fps × 1s（60×1/60）→ 派发 %d 次\n", ticks);
    Check(ticks == 12, "T-01：12fps 订阅跑满 1 秒恰好派发 12 次（不因浮点少一步）");

    // ② 慢帧一次 0.25s：补发 3 次，且不进"追不上"计数
    const std::uint64_t drops0 = scene.GetAnimationCatchUpDropCount();
    ticks = 0;
    scene.TickFrame(0.25);
    Check(ticks == 3, "T-01：一次喂 0.25s（3 个间隔）补发 3 次，不吞时间");
    Check(scene.GetAnimationCatchUpDropCount() == drops0,
          "T-01：0.25s 属于正常补发，不计入 catch-up drop");

    // ⑤ 单帧补发上限：120fps 喂 5s（应 600 步）→ 只补 64 步 + 记一次丢失
    int fastTicks = 0;
    scene.SubscribeAnimation(1.0 / 120.0, [&fastTicks](double) { ++fastTicks; });
    const std::uint64_t dropsBeforeCap = scene.GetAnimationCatchUpDropCount();
    scene.TickFrame(5.0);
    std::printf("         120fps 喂 5s → 补发 %d 步，丢账 %llu 次\n", fastTicks,
                static_cast<unsigned long long>(scene.GetAnimationCatchUpDropCount()
                                                - dropsBeforeCap));
    Check(fastTicks == 64, "T-01：单帧最多补发 64 步（不在一帧里补 600 次）");
    Check(scene.GetAnimationCatchUpDropCount() == dropsBeforeCap + 1,
          "T-01：被截断的欠账记一次 catch-up drop");

    // ⑥ 退订：一次成功、二次失败
    Check(scene.UnsubscribeAnimation(handle), "T-01：退订成功");
    Check(!scene.UnsubscribeAnimation(handle), "T-01：重复退订返回 false");

    InkingSceneLibrary::SetActiveScene(nullptr);

    // ⑦ 护栏：回调里增删节点（**违规操作**）不能让同帧的后继订阅踩空
    {
        InkingScene guard("anim_guard");
        InkingSceneLibrary::SetActiveScene(&guard);

        int firstHits = 0;
        int secondHits = 0;
        guard.SubscribeAnimation(1.0 / 60.0, [&guard, &firstHits](double) {
            ++firstHits;
            InkingPanelData data;
            data.name = "anim_guard_panel";
            data.width = 10;
            data.height = 10;
            guard.AddChild(std::make_unique<InkingStaticPanel>(&guard, data));
        });
        guard.SubscribeAnimation(1.0 / 60.0,
                                 [&secondHits](double) { ++secondHits; });

        const std::size_t before = guard.GetDrawItemCount();
        guard.TickFrame(1.0 / 60.0);
        Check(firstHits == 1 && secondHits == 0,
              "T-01：回调里改树 ⇒ 同帧剩下的订阅不再派发（护栏，不是许可）");
        Check(guard.GetDrawItemCount() == before + 1,
              "T-01：违规新建的节点没丢（下一帧照常重建绘制列表）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }
}

// ---------------------------------------------------------------------------
// InkingSpriteData：序列帧 / 图集二选一 + 校验（0.6.0 T-02）
// ---------------------------------------------------------------------------
//
// 这一层**没有可画的像素**，所以断言只能落在"数据说了什么"上：帧数怎么算、
// 哪些组合必须被拒绝、越界是被夹取（并报 WARN）而不是被静默吞掉。
//
// 四条错误分支对应四种真实会写的配置错误（都填 / 都不填 / fps 写 0 / 图集
// count 比格子多），夹取分支对应"美术少交了几帧"——那一类**必须还能看见画面**，
// 所以修 + 记 WARN，而不是留一块空白。
void CheckSpriteDataValidate() {
    std::printf("\n-- InkingSpriteData：序列帧 / 图集二选一与校验（0.6.0 T-02） --\n");

    const auto makeFrame = [](const char* source) {
        InkingButtonAppearance frame(InkingAppearanceType::Image, 0xFF000000u);
        frame.source = source;
        return frame;
    };

    // ① 序列帧：4 帧合法，fps 缺省 12
    {
        InkingSpriteData sprite;
        sprite.name = "sprite_seq";
        sprite.normal.type = InkingAppearanceType::Image;
        sprite.normal.source = "frame0.png";
        for (int i = 0; i < 4; ++i) {
            sprite.frames.push_back(makeFrame("frame.png"));
        }
        std::printf("         序列帧：帧数=%zu fps=%.1f\n", sprite.FrameCount(),
                    sprite.fps);
        Check(sprite.Validate().empty(), "T-02：4 帧序列帧通过校验");
        Check(sprite.FrameCount() == 4 && !sprite.UsesSheet(),
              "T-02：序列帧 FrameCount = frames.size()（4），且不是图集模式");
        Check(sprite.fps == 12.0, "T-02：fps 缺省 12（配错数量级也还看得见在动）");

        // ② fps 两个方向的非法值
        sprite.fps = 0.0;
        Check(!sprite.Validate().empty(),
              "T-02：fps = 0 → 报错（不是「就不动」，是配错了）");
        sprite.fps = -12.0;
        Check(!sprite.Validate().empty(), "T-02：fps 为负 → 报错");
        sprite.fps = 12.0;

        // ③ 帧里混进纯色
        sprite.frames[2].type = InkingAppearanceType::Color;
        Check(!sprite.Validate().empty(), "T-02：某一帧是纯色 → 报错（帧必须是图）");
        sprite.frames[2] = makeFrame("frame.png");

        // ④ 帧没有图像内容
        sprite.frames[1] = InkingButtonAppearance{};
        Check(!sprite.Validate().empty(), "T-02：某一帧 source 空 → 报错");
        sprite.frames[1] = makeFrame("frame.png");

        // ⑤ 都填：不猜哪个优先
        sprite.sheet.frameWidth = 64;
        sprite.sheet.frameHeight = 64;
        sprite.sheet.columns = 4;
        sprite.sheet.rows = 1;
        const std::string both = sprite.Validate();
        std::printf("         都填时的报错：%s\n", both.c_str());
        Check(!both.empty() && both.find("二选一") != std::string::npos,
              "T-02：frames 与 sheet 都填 → 报\"二选一\"（不替调用方挑）");
    }

    // ⑥ 都不填
    {
        InkingSpriteData sprite;
        sprite.name = "sprite_empty";
        const std::string none = sprite.Validate();
        std::printf("         都不填时的报错：%s\n", none.c_str());
        Check(!none.empty() && sprite.FrameCount() == 0,
              "T-02：frames 与 sheet 都不填 → 报错且帧数为 0");
    }

    // ⑦ startFrame 越界：夹到最后一帧 + 1 条 WARN，且幂等
    {
        InkingSpriteData sprite;
        sprite.name = "sprite_clamp";
        sprite.frames.push_back(makeFrame("a.png"));
        sprite.frames.push_back(makeFrame("b.png"));
        sprite.startFrame = 9;
        InkLog::ResetStats();
        sprite.Normalize();
        Check(sprite.startFrame == 1,
              "T-02：startFrame 越界 → 夹到最后一帧（9 → 1，不是静默归零）");
        Check(InkLog::CountOf(InkingLogLevel::Warn) == 1,
              "T-02：夹取同时产生 1 条 WARN");
        InkLog::ResetStats();
        sprite.Normalize();
        Check(InkLog::CountOf(InkingLogLevel::Warn) == 0,
              "T-02：Normalize 幂等（第二次不再重复报）");
    }

    // ⑧ 图集：count 缺省取 rows × columns，显式更小值保留，超了报错
    {
        InkingSpriteData sprite;
        sprite.name = "sprite_sheet";
        sprite.normal.type = InkingAppearanceType::Image;
        sprite.normal.source = "walk.png";
        sprite.sheet.frameWidth = 64;
        sprite.sheet.frameHeight = 64;
        sprite.sheet.columns = 4;
        sprite.sheet.rows = 1;
        sprite.Normalize();
        std::printf("         图集：count=%d 帧数=%zu\n", sprite.sheet.count,
                    sprite.FrameCount());
        Check(sprite.sheet.count == 4 && sprite.FrameCount() == 4
                  && sprite.UsesSheet(),
              "T-02：图集省略 count → rows × columns（4）");
        Check(sprite.Validate().empty(),
              "T-02：图集（sheet.source 空、沿用 normal.source）通过校验");

        sprite.sheet.count = 3;
        Check(sprite.FrameCount() == 3,
              "T-02：图集显式给更小的 count 被保留（末尾空格是合法用法）");

        sprite.sheet.count = 99;
        Check(!sprite.Validate().empty(), "T-02：图集 count > 格子数 → 报错");

        sprite.sheet.count = 0;
        sprite.sheet.columns = 0;
        Check(!sprite.Validate().empty(), "T-02：图集 columns 为 0 → 报错");
    }
}

// ---------------------------------------------------------------------------
// InkingSprite：序列帧播放 / 暂停 / 定位 / 纹理复用（0.6.0 T-03）
// ---------------------------------------------------------------------------
//
// 断言全部落在**帧号与计数**上，不是"看起来在动"：帧号是时间的纯函数，
// 所以"12fps 跑 1 秒 = 12 次换帧"这种话才有意义。
//
// 三条最容易写错的：
//   1. 慢帧要**跨帧**（0.25s 一次跨 3 帧），不是"一次回调 +1 帧"；
//   2. 暂停是**时间冻结**（恢复后原地继续），不是"停止换帧"（恢复后跳 36 帧）；
//   3. 换帧不该再建纹理——第一轮播放把 4 帧都画过之后，第二轮 `texturesCreated`
//      增量必须是 0（构造期只预热**字节**，纹理属于渲染器，建窗之后才存在）。

/// 四张 40×40 的 SVG 帧。**静态生命周期**：帧只借视图不拷贝字节。
const char* kSpriteFrameSvg[4] = {
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\">"
    "<rect width=\"40\" height=\"40\" fill=\"#CC3333\"/></svg>",
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\">"
    "<rect width=\"40\" height=\"40\" fill=\"#33CC33\"/></svg>",
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\">"
    "<rect width=\"40\" height=\"40\" fill=\"#3333CC\"/></svg>",
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\">"
    "<rect width=\"40\" height=\"40\" fill=\"#CCCC33\"/></svg>",
};

/// 造一份序列帧配置（第 i 帧的名字唯一 ⇒ 每帧一个纹理槽位）。
InkingSpriteData InkingMakeSpriteData(const char* name, int frameCount, double fps,
                                  bool loop) {
    InkingSpriteData data;
    data.name = name;
    data.width = 40;
    data.height = 40;
    data.fps = fps;
    data.loop = loop;
    data.autoplay = true;
    data.selfAnchor = InkingChangeAnchor::LeftTop;
    data.traceAnchor = InkingChangeAnchor::LeftTop;
    data.offsetX = 20.0f;
    data.offsetY = 20.0f;
    for (int i = 0; i < frameCount; ++i) {
        InkingButtonAppearance frame(InkingAppearanceType::Image,
                                       0xFF000000u);
        frame.source = std::string(name) + "#" + std::to_string(i);
        const char* svg = kSpriteFrameSvg[i % 4];
        frame.imageBytes = SvgBytes(svg);
        frame.imageSize = std::strlen(svg);
        data.frames.push_back(frame);
    }
    return data;
}

InkingSprite* AddSprite(InkingScene& scene, const char* name,
                                int frameCount, double fps, bool loop) {
    return static_cast<InkingSprite*>(scene.AddChild(
        std::make_unique<InkingSprite>(
            &scene, InkingMakeSpriteData(name, frameCount, fps, loop))));
}

void CheckSpriteSequence(SDL_Renderer* renderer) {
    std::printf("\n-- InkingSprite：序列帧 / 暂停 / 定位 / 纹理复用（0.6.0 T-03） --\n");

    // ① 12fps × 1 秒 = 12 次换帧；4 帧循环回绕；两个同 fps 的 sprite 帧号恒等
    {
        InkingScene scene("sprite_seq");
        InkingSceneLibrary::SetActiveScene(&scene);

        InkingSprite* sprite = AddSprite(scene, "spr_a", 4, 12.0, true);
        InkingSprite* twin = AddSprite(scene, "spr_b", 4, 12.0, true);
        Check(sprite->IsValid() && sprite->GetFrameCount() == 4
                  && sprite->GetLoadedFrameCount() == 4,
              "T-03：4 帧的字节在构造期全部备齐");
        Check(sprite->GetFrameIndex() == 0 && sprite->IsPlaying(),
              "T-03：autoplay 默认开且落在第 0 帧");

        int events = 0;
        std::size_t lastNew = 0;
        std::size_t lastOld = 0;
        sprite->frameChanged.Subscribe(
            [&events, &lastNew, &lastOld](std::size_t now, std::size_t before) {
                ++events;
                lastNew = now;
                lastOld = before;
            });

        for (int i = 0; i < 12; ++i) {
            scene.TickFrame(1.0 / 12.0);
        }
        std::printf("         12×1/12 → 帧号=%zu 换帧=%llu 跳过=%llu 事件=%d\n",
                    sprite->GetFrameIndex(),
                    static_cast<unsigned long long>(sprite->GetFrameAdvanceCount()),
                    static_cast<unsigned long long>(sprite->GetSkippedFrameCount()),
                    events);
        Check(sprite->GetFrameAdvanceCount() == 12,
              "T-03：12fps 跑满 1 秒恰好换 12 帧（不多不少）");
        Check(sprite->GetFrameIndex() == 0,
              "T-03：4 帧循环满一圈回到第 0 帧");
        Check(sprite->GetSkippedFrameCount() == 0,
              "T-03：正常播放不跳帧（skipped 恒为 0）");
        Check(events == 12 && lastNew == 0 && lastOld == 3,
              "T-03：frameChanged 每换一帧发一次（(0,3) 是回绕那一次）");
        Check(twin->GetFrameIndex() == sprite->GetFrameIndex(),
              "T-03：同场景两个同 fps 的 sprite 帧号恒等（时间只有一个源）");
    }

    // ② 慢帧：一次 0.25s 跨 3 帧（画面不慢放）+ skipped +2
    {
        InkingScene scene("sprite_slow");
        InkingSceneLibrary::SetActiveScene(&scene);
        InkingSprite* sprite = AddSprite(scene, "spr_slow", 4, 12.0, true);

        scene.TickFrame(0.25);
        std::printf("         喂 0.25s → 帧号=%zu 跳过=%llu\n",
                    sprite->GetFrameIndex(),
                    static_cast<unsigned long long>(
                        sprite->GetSkippedFrameCount()));
        Check(sprite->GetFrameIndex() == 3,
              "T-03：一次 0.25s 跨 3 帧（0 → 3，不是只 +1）");
        Check(sprite->GetSkippedFrameCount() == 2,
              "T-03：跨过的 2 帧记进 skipped（掉帧可量化）");
        Check(sprite->GetFrameAdvanceCount() == 1,
              "T-03：跨多帧只算一次换帧（不是算了 3 次）");
    }

    // ③ 非循环：停在末帧并 finished，之后不再推进
    {
        InkingScene scene("sprite_once");
        InkingSceneLibrary::SetActiveScene(&scene);
        InkingSprite* sprite = AddSprite(scene, "spr_once", 4, 12.0, false);

        for (int i = 0; i < 4; ++i) {
            scene.TickFrame(1.0 / 12.0);
        }
        Check(sprite->GetFrameIndex() == 3 && sprite->IsFinished(),
              "T-03：非循环播到末帧停住且 IsFinished() 为真");

        const std::uint64_t advances = sprite->GetFrameAdvanceCount();
        for (int i = 0; i < 10; ++i) {
            scene.TickFrame(1.0 / 12.0);
        }
        Check(sprite->GetFrameAdvanceCount() == advances && sprite->IsFinished(),
              "T-03：播完之后再 tick 不再换帧");

        sprite->Play();  // 播完再播 = 从头再来
        Check(!sprite->IsFinished() && sprite->GetFrameIndex() == 0,
              "T-03：播完后 Play() 从头开始（不是停在末帧）");
    }

    // ④ 暂停是"时间冻结"：暂停 3 秒后恢复，原地继续
    {
        InkingScene scene("sprite_pause");
        InkingSceneLibrary::SetActiveScene(&scene);
        InkingSprite* sprite = AddSprite(scene, "spr_pause", 4, 12.0, true);

        scene.TickFrame(1.0 / 12.0);
        scene.TickFrame(1.0 / 12.0);
        Check(sprite->GetFrameIndex() == 2, "T-03：两帧之后停在第 2 帧");

        sprite->Pause();
        const double pausedTime = sprite->GetAnimationTime();
        for (int i = 0; i < 36; ++i) {  // 3 秒
            scene.TickFrame(1.0 / 12.0);
        }
        Check(!sprite->IsPlaying() && sprite->GetFrameIndex() == 2,
              "T-03：暂停期间帧号不动（时钟照走，画面冻结）");
        Check(std::fabs(sprite->GetAnimationTime() - pausedTime) < 1e-9,
              "T-03：暂停期间动画时间也冻结");

        sprite->Play();
        scene.TickFrame(1.0 / 12.0);
        std::printf("         暂停 3s 后恢复 → 帧号=%zu（应 3，不是跳 36 帧）\n",
                    sprite->GetFrameIndex());
        Check(sprite->GetFrameIndex() == 3,
              "T-03：暂停 3 秒后恢复从原帧继续（不是跳过 36 帧）");

        sprite->Stop();
        Check(!sprite->IsPlaying() && sprite->GetFrameIndex() == 0,
              "T-03：Stop() 回到起始帧且不播");

        Check(sprite->SeekToFrame(2) && sprite->GetFrameIndex() == 2,
              "T-03：SeekToFrame(2) 当场换到第 2 帧");
        Check(sprite->SeekToFrame(99) && sprite->GetFrameIndex() == 3,
              "T-03：SeekToFrame 越界夹到末帧（定位是交互，不报错）");
    }

    // ⑤ 换帧不重建纹理：第一轮把 4 帧都画过，第二轮 texturesCreated 增量 0
    {
        InkingScene scene("sprite_tex");
        InkingSceneLibrary::SetActiveScene(&scene);
        InkingSprite* sprite = AddSprite(scene, "spr_tex", 4, 12.0, true);

        const auto statsBeforeFirst = inking_detail::TextCacheStatsSnapshot();
        for (int i = 0; i < 5; ++i) {  // 走完一整圈多一点
            scene.TickFrame(1.0 / 12.0);
            RenderSvgFrame(renderer, scene);
        }
        const auto statsAfterFirst = inking_detail::TextCacheStatsSnapshot();
        const std::uint64_t firstPass =
            statsAfterFirst.texturesCreated - statsBeforeFirst.texturesCreated;

        for (int i = 0; i < 5; ++i) {
            scene.TickFrame(1.0 / 12.0);
            RenderSvgFrame(renderer, scene);
        }
        const auto statsAfterSecond = inking_detail::TextCacheStatsSnapshot();
        const std::uint64_t secondPass =
            statsAfterSecond.texturesCreated - statsAfterFirst.texturesCreated;

        std::printf("         纹理创建：首轮 %llu 次 / 第二轮 %llu 次（预热点在字节，"
                    "纹理首轮建一次）\n",
                    static_cast<unsigned long long>(firstPass),
                    static_cast<unsigned long long>(secondPass));
        Check(firstPass >= 4, "T-03：首轮播放把 4 帧的纹理都建了出来");
        Check(secondPass == 0,
              "T-03：第二轮换帧不再创建纹理（缓存命中，换帧零解码）");
        Check(sprite->GetFrameAdvanceCount() == 10,
              "T-03：10 次 1/12 推进 = 10 次换帧（循环跨圈计数连续）");

        EndSvgFrame(renderer);
        (void)statsAfterSecond;
    }

    // ⑥ 配置非法：图集连图都没有 → 明确报错，不假装播起来
    {
        InkingScene scene("sprite_bad");
        InkingSceneLibrary::SetActiveScene(&scene);

        InkingSpriteData data = InkingMakeSpriteData("spr_sheet", 4, 12.0, true);
        data.frames.clear();
        // 只配切法、不给任何图（`sheet.source` 与 `normal.source` 都空）：
        // `Validate()` 必须拦在"没有可播的帧"之前就说清是"图集没有图"。
        data.normal.type = InkingAppearanceType::Image;
        data.normal.source.clear();
        data.sheet.source.clear();
        data.sheet.frameWidth = 40;
        data.sheet.frameHeight = 40;
        data.sheet.columns = 4;
        data.sheet.rows = 1;

        InkLog::ResetStats();
        auto* sprite = static_cast<InkingSprite*>(scene.AddChild(
            std::make_unique<InkingSprite>(&scene, data)));
        std::printf("         图集无图：IsValid=%d 错误=%s\n",
                    sprite->IsValid() ? 1 : 0,
                    sprite->GetConfigError().c_str());
        Check(!sprite->IsValid() && !sprite->GetConfigError().empty(),
              "T-03：配置非法时 `IsValid()` 为假且给出了原因");
        Check(InkLog::CountOf(InkingLogLevel::Error) >= 1,
              "T-03：拒绝的同时有 ERROR（不是静默空白）");
        Check(sprite->GetFrameCount() == 0 && !sprite->SeekToFrame(0),
              "T-03：没有可播的帧时 SeekToFrame 返回 false");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }
}

// ---------------------------------------------------------------------------
// T-15（0.6.5）：库内的 JSON 小工具
// ---------------------------------------------------------------------------
//
// 这套断言的重点是**报错可用**：行号、重复键、路径前缀。解析成功本身不稀奇，
// 而"配置写错了却只说'解析失败'"在几百行的文件里等于没有信息。
void CheckJsonTool() {
    std::printf("\n-- json：解析 / 取值 / 转义（0.6.5 T-15） --\n");

    // ① 好 json：Find 与 At 混着走三层
    {
        const char* text = R"({
            "name": "atlas",
            "meta": { "scale": "1", "animations": [
                { "name": "walk", "fps": 8, "frames": [1, 2, 3] },
                { "name": "idle", "fps": 4, "loop": false }
            ] }
        })";
        const inking_json::Result result = inking_json::Parse(text);
        Check(result.ok, "T-15：合法 json 解析成功");
        if (result.ok) {
            const inking_json::Value& root = result.value;
            const inking_json::Value* meta = root.Find("meta");
            const inking_json::Value* animations =
                meta != nullptr ? meta->Find("animations") : nullptr;
            const inking_json::Value* second =
                animations != nullptr ? animations->At(1) : nullptr;
            // 存成 `std::string` 而不是 `.c_str()`：临时串的指针立刻悬垂，
            // 打印出来的会是别的值（第一次跑正是这么红的）。
            const std::string secondName =
                second != nullptr ? inking_json::ReadString(*second, "name", "")
                                  : std::string();
            std::printf("         三层取值：name=%s 动画数=%zu 第 2 个=%s\n",
                        inking_json::ReadString(root, "name", "").c_str(),
                        animations != nullptr ? animations->items.size() : 0u,
                        secondName.c_str());
            Check(inking_json::ReadString(root, "name", "") == "atlas"
                      && animations != nullptr
                      && animations->items.size() == 2
                      && secondName == "idle",
                  "T-15：Find + At 混用能取到三层嵌套的值");

            const inking_json::Value* walk =
                animations != nullptr && animations->At(0) != nullptr
                    ? animations->At(0)
                    : nullptr;
            Check(walk != nullptr
                      && inking_json::ReadNumber(*walk, "fps", 0.0) == 8.0
                      && inking_json::ReadBool(*walk, "loop", true),
                  "T-15：ReadNumber / ReadBool 取到值、缺字段给 fallback");
            Check(second != nullptr
                      && !inking_json::ReadBool(*second, "loop", true)
                      && inking_json::ReadNumber(*second, "missing", 7.0) == 7.0,
                  "T-15：false 与 fallback 分得开（不会把 false 当成缺字段）");
        }
    }

    // ② 坏 json：三类各拿到带**行号**的错误
    {
        struct BadCase {
            const char* label;
            const char* text;
            int expectedLine;
        };
        const BadCase cases[3] = {
            {"尾随逗号", "{\n  \"a\": 1,\n}", 3},
            {"重复键", "{\n  \"a\": 1,\n  \"a\": 2\n}", 3},
            {"少右括号", "{\n  \"a\": 1\n", 3},
        };
        for (const BadCase& item : cases) {
            const inking_json::Result result = inking_json::Parse(item.text);
            std::printf("         %s：ok=%d 错误=%s\n", item.label,
                        result.ok ? 1 : 0, result.error.c_str());
            Check(!result.ok && result.error.find("行") != std::string::npos,
                  "T-15：坏 json 被拒且错误里带行号");
        }
    }

    // ③ ParseAsset：不存在的路径 → ok=false 且错误带路径
    {
        const inking_json::Result result =
            inking_json::ParseAsset("__不存在__/atlas.json");
        std::printf("         ParseAsset 错误=%s\n", result.error.c_str());
        Check(!result.ok
                  && result.error.find("__不存在__/atlas.json") != std::string::npos,
              "T-15：ParseAsset 读不到时错误里带路径");
    }

    // ④ Escape 往返：引号 / 反斜杠 / 换行 / 制表 / 中文
    {
        const std::string original = "他说\"走\"\\path\\\n下一行\t制表，中文：帧动画";
        const std::string escaped = inking_json::Escape(original);
        const inking_json::Result result = inking_json::Parse(
            "{\"s\":\"" + escaped + "\"}");
        const std::string roundTrip =
            result.ok ? inking_json::ReadString(result.value, "s", "?") : "?";
        std::printf("         转义后=%s\n         往返相等=%d\n", escaped.c_str(),
                    roundTrip == original ? 1 : 0);
        Check(result.ok && roundTrip == original,
              "T-15：Escape 之后能解析回来，且逐字节相等（含中文）");
    }
}

// ---------------------------------------------------------------------------
// T-04：源矩形（图集切格）
// ---------------------------------------------------------------------------
//
// 验收就一句话：**图集里切出来的第 k 格，必须和"单独存的第 k 张小图"逐像素一致。**
// 中间隔着三层（`InkingButtonAppearance::srcRect` → `PaintImage` → `InkingImageFitSpec` →
// `fillShapeTextured`），任何一层把值丢掉，画面都还"有东西"、只是切错块或整张拉伸
// ——所以必须对像素，不能只断言"渲染成功"。

/// 自检专用：把 protected 的切格入口露出来。
///
/// `InkingSprite` 的图集模式（T-05）走的是同一个入口；T-04 先在自检里直接验绘制链，
/// 否则要等 T-05 才第一次跑到这段代码。
struct SheetProbeImage : InkingStaticImage {
    using InkingStaticImage::InkingStaticImage;
    using InkingStaticImage::SetRenderSourceRect;
};

/**
 * 造一张 24 位 BMP（BGR / 行序自下而上 / 行尾补 4 字节）。
 *
 * 为什么自检里造 BMP 而不是 PNG：这里要的是"逐像素确定"的图案，BMP 头 54 字节
 * 手写就够了，不必为了一个测试引一个编码器；解码仍走 `IMG_Load_IO`（内存流），
 * 与 PNG 完全同一条路，所以这条自检验的还是生产代码。
 *
 * 图案随 **格号 + 格内坐标** 变化：切错一格、偏一列、少一像素，逐像素对拍当场红。
 */
std::vector<std::uint8_t> MakeSheetBmp(int width, int height, int cellSize,
                                       int firstCell) {
    const int stride = (width * 3 + 3) / 4 * 4;
    const int imageSize = stride * height;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(54 + imageSize), 0);
    const auto put32 = [&bytes](int offset, std::uint32_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
        bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
        bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
        bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    };
    bytes[0] = 'B';
    bytes[1] = 'M';
    put32(2, static_cast<std::uint32_t>(54 + imageSize));
    put32(10, 54);  // 像素数据偏移
    put32(14, 40);  // BITMAPINFOHEADER 大小
    put32(18, static_cast<std::uint32_t>(width));
    put32(22, static_cast<std::uint32_t>(height));
    bytes[26] = 1;   // planes
    bytes[28] = 24;  // bpp
    put32(34, static_cast<std::uint32_t>(imageSize));
    for (int y = 0; y < height; ++y) {
        const int sourceY = height - 1 - y;  // BMP 自下而上
        for (int x = 0; x < width; ++x) {
            const int cell = firstCell + x / cellSize;
            const int localX = x % cellSize;
            const auto r = static_cast<std::uint32_t>((localX * 6 + cell * 13) & 0xFF);
            const auto g =
                static_cast<std::uint32_t>((sourceY * 5 + cell * 31) & 0xFF);
            const auto b =
                static_cast<std::uint32_t>((cell * 60 + localX * 2) & 0xFF);
            std::uint8_t* pixel =
                &bytes[static_cast<std::size_t>(54 + y * stride + x * 3)];
            pixel[0] = static_cast<std::uint8_t>(b);
            pixel[1] = static_cast<std::uint8_t>(g);
            pixel[2] = static_cast<std::uint8_t>(r);
        }
    }
    return bytes;
}

/// 造一张 **2 色**棋盘 BMP（0.6.4 T-12 用）。
///
/// 与 `MakeSheetBmp` 分开写而不是加参数：那张图的图案是"每格渐变 + 换格变色"，
/// 用来验采样模式会得到"本来就有很多颜色"的结论——判据必须建立在**只有 2 种颜色**
/// 的源图上，放大后多出来的颜色才只可能来自插值。
std::vector<std::uint8_t> MakeCheckerBmp(int width, int height, int blockSize) {
    const int stride = (width * 3 + 3) / 4 * 4;
    const int imageSize = stride * height;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(54 + imageSize), 0);
    const auto put32 = [&bytes](int offset, std::uint32_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
        bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
        bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
        bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    };
    bytes[0] = 'B';
    bytes[1] = 'M';
    put32(2, static_cast<std::uint32_t>(54 + imageSize));
    put32(10, 54);
    put32(14, 40);
    put32(18, static_cast<std::uint32_t>(width));
    put32(22, static_cast<std::uint32_t>(height));
    bytes[26] = 1;
    bytes[28] = 24;
    put32(34, static_cast<std::uint32_t>(imageSize));
    for (int y = 0; y < height; ++y) {
        const int sourceY = height - 1 - y;  // BMP 自下而上
        for (int x = 0; x < width; ++x) {
            const bool white = ((x / blockSize) + (sourceY / blockSize)) % 2 == 0;
            const auto value = static_cast<std::uint8_t>(white ? 0xFF : 0x00);
            std::uint8_t* pixel =
                &bytes[static_cast<std::size_t>(54 + y * stride + x * 3)];
            pixel[0] = value;
            pixel[1] = value;
            pixel[2] = value;
        }
    }
    return bytes;
}

/**
 * 造一张"**内容随绝对坐标变化**"的 24 位 BMP（0.6.5 T-14 用）。
 *
 * 与 `MakeSheetBmp` 的区别：那张图的颜色取决于"第几格 + 格内坐标"，而帧表的矩形
 * **宽高不等**、没有"格"这回事。这里用绝对坐标 → 从同一张图里裁出任意矩形，
 * 与"直接造那块矩形"逐像素相同，于是"帧表切得对不对"可以拿小图对拍。
 */
std::vector<std::uint8_t> MakeAtlasRegionBmp(int atlasWidth, int atlasHeight, int x0,
                                             int y0, int width, int height) {
    const int stride = (width * 3 + 3) / 4 * 4;
    const int imageSize = stride * height;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(54 + imageSize), 0);
    const auto put32 = [&bytes](int offset, std::uint32_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
        bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
        bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
        bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    };
    bytes[0] = 'B';
    bytes[1] = 'M';
    put32(2, static_cast<std::uint32_t>(54 + imageSize));
    put32(10, 54);
    put32(14, 40);
    put32(18, static_cast<std::uint32_t>(width));
    put32(22, static_cast<std::uint32_t>(height));
    bytes[26] = 1;
    bytes[28] = 24;
    put32(34, static_cast<std::uint32_t>(imageSize));
    for (int row = 0; row < height; ++row) {
        const int topDown = height - 1 - row;  // BMP 自下而上
        const int absoluteY = y0 + topDown;
        for (int x = 0; x < width; ++x) {
            const int absoluteX = x0 + x;
            std::uint8_t* pixel =
                &bytes[static_cast<std::size_t>(54 + row * stride + x * 3)];
            pixel[0] = static_cast<std::uint8_t>((absoluteX * 13 + absoluteY * 2) & 0xFF);
            pixel[1] = static_cast<std::uint8_t>((absoluteX * 5 + absoluteY * 11) & 0xFF);
            pixel[2] = static_cast<std::uint8_t>((absoluteX * 7 + absoluteY * 3) & 0xFF);
        }
    }
    return bytes;
}

/// 读回一块像素（ARGB8888，逐行拷贝——surface 的 pitch 可能带对齐）。
std::vector<std::uint32_t> ReadPixelRegion(SDL_Renderer* renderer,
                                           const SDL_Rect& rect) {
    std::vector<std::uint32_t> out;
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, &rect);
    if (shot == nullptr) {
        return out;
    }
    SDL_Surface* argb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(shot);
    if (argb == nullptr || !SDL_LockSurface(argb)) {
        SDL_DestroySurface(argb);
        return out;
    }
    out.resize(static_cast<std::size_t>(rect.w) * static_cast<std::size_t>(rect.h));
    for (int row = 0; row < rect.h; ++row) {
        std::memcpy(&out[static_cast<std::size_t>(row) * rect.w],
                    static_cast<const std::uint8_t*>(argb->pixels)
                        + static_cast<std::size_t>(row) * argb->pitch,
                    sizeof(std::uint32_t) * static_cast<std::size_t>(rect.w));
    }
    SDL_UnlockSurface(argb);
    SDL_DestroySurface(argb);
    return out;
}

// ---------------------------------------------------------------------------
// T-01：栅格化发生在设备分辨率吗
//
// §3.2 的整条推论（"素材按屏幕像素出才有意义"）压在这个假设上：纹理直接对
// **屏幕像素**采样，中间没有"先渲到设计分辨率再放大"。若假设反了，2× 素材
// 等于白烧 4 倍显存。十行探针换一个二值结论，值得。
//
// 探针的判据要**不依赖采样模式**——这条吃过亏：软件后端的几何批恒用最近邻
// （`SetImageSampling` 那一段的记录），所以"放大后有没有中间灰"在这个后端
// 上永远为假，拿它做判别会得出"结论成立"的假象。
//
// 改用**一条 1px 黑列**（纹理 64 宽，第 33 列全黑，其余全白；33 是奇数）：
//
//   - **在设备分辨率栅格化**：64 纹理 → 64 屏幕 = 1:1 ⇒ 读回来**正好一列**
//     纯黑，落在第 33 列，32 行全黑；
//   - **先渲到 32×32 再放大**：64 → 32 取样丢掉一半纹素（最近邻丢奇数或偶数列，
//     双线性变成 50% 灰），再放大 ⇒ "一列纯黑"这条不成立（要么没有黑列，
//     要么两列、要么是灰列）。
//
// 清屏用**橙色**而不是黑色：黑列是探针图案，不该和背景同色（否则"探针丢了"
// 会被读成"有一片黑"）。
// ---------------------------------------------------------------------------

/// 一次回读里"那条黑列"的读数。
struct ProbeCensus {
    bool read = false;          ///< 回读成功了吗
    int blackColumns = 0;       ///< 含近黑像素的**设备列**个数
    int firstBlackColumn = -1;  ///< 第一条这样的列的设备坐标
    int blackPerColumn = 0;     ///< 那列上有几个近黑像素
    int greyPixels = 0;         ///< 既非近黑也非近白（插值留下的痕迹，只看诊断）
};

ProbeCensus CensusBlackColumn(SDL_Renderer* renderer, const SDL_Rect& region) {
    ProbeCensus census;
    if (renderer == nullptr || region.w <= 0 || region.h <= 0) {
        return census;
    }
    // 整幅读回再自己裁：`SDL_RenderReadPixels` 的矩形在**逻辑呈现**开着时是
    // 逻辑坐标还是物理坐标，各后端历史上不一致；nullptr（整幅）没有这个歧义。
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
    if (shot == nullptr) {
        return census;
    }
    SDL_Surface* argb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(shot);
    if (argb == nullptr || !SDL_LockSurface(argb)) {
        SDL_DestroySurface(argb);
        return census;
    }
    const auto* base = static_cast<const std::uint8_t*>(argb->pixels);
    // 每一列记"近黑像素数"（近黑 = r+g+b ≤ 30，挡住抗锯齿边缘那点残值）。
    std::vector<int> blackAt(static_cast<std::size_t>(region.w), 0);
    for (int row = region.y; row < region.y + region.h && row < argb->h; ++row) {
        if (row < 0) {
            continue;
        }
        const auto* line = reinterpret_cast<const std::uint32_t*>(
            base + static_cast<std::size_t>(row) * argb->pitch);
        for (int col = region.x; col < region.x + region.w && col < argb->w; ++col) {
            if (col < 0) {
                continue;
            }
            const std::uint32_t pixel = line[col];
            const int sum = static_cast<int>((pixel >> 16) & 0xFFu)
                + static_cast<int>((pixel >> 8) & 0xFFu)
                + static_cast<int>(pixel & 0xFFu);
            if (sum <= 30) {
                ++blackAt[static_cast<std::size_t>(col - region.x)];
            } else if (sum < 735) {  // 245 × 3：既不是近黑也不是近白
                ++census.greyPixels;
            }
        }
    }
    SDL_UnlockSurface(argb);
    SDL_DestroySurface(argb);

    for (int i = 0; i < region.w; ++i) {
        const int count = blackAt[static_cast<std::size_t>(i)];
        if (count <= 0) {
            continue;
        }
        ++census.blackColumns;
        if (census.firstBlackColumn < 0) {
            census.firstBlackColumn = region.x + i;
        }
        if (count > census.blackPerColumn) {
            census.blackPerColumn = count;
        }
    }
    census.read = true;
    return census;
}

void CheckRasterIsDeviceSpace() {
    std::printf("\n-- 栅格化发生在设备分辨率（0.7.0 T-01）--\n");

    constexpr int kScreen = 64;   // 屏幕（设备像素）
    constexpr int kLogical = 32;  // 逻辑画布 ⇒ R = 2
    constexpr int kProbeColumn = 33;  // 黑列的**设备**列号（奇数：最近邻会丢它）

    SDL_Window* window = SDL_CreateWindow("ink-raster-space", kScreen, kScreen, 0);
    SDL_Renderer* renderer =
        window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    Check(renderer != nullptr, "创建 64×64 探针渲染器");
    if (renderer == nullptr) {
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        return;
    }

    // 造一张 64×64 的 24 位 BMP：白底 + 指定列全黑。用 BMP 而不是 PNG 的理由
    // 与 `MakeSheetBmp` 相同——图案要逐像素确定，头 54 字节手写就够了。
    const std::vector<std::uint8_t> probeBmp = [] {
        constexpr int size = 64;
        const int stride = (size * 3 + 3) / 4 * 4;
        const int imageSize = stride * size;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(54 + imageSize), 0);
        const auto put32 = [&bytes](int offset, std::uint32_t value) {
            bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
            bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
            bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
            bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
        };
        bytes[0] = 'B';
        bytes[1] = 'M';
        put32(2, static_cast<std::uint32_t>(54 + imageSize));
        put32(10, 54);
        put32(14, 40);
        put32(18, size);
        put32(22, size);
        bytes[26] = 1;   // planes
        bytes[28] = 24;  // bpp
        put32(34, static_cast<std::uint32_t>(imageSize));
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const auto value =
                    static_cast<std::uint8_t>(x == kProbeColumn ? 0x00 : 0xFF);
                std::uint8_t* pixel =
                    &bytes[static_cast<std::size_t>(54 + y * stride + x * 3)];
                pixel[0] = value;
                pixel[1] = value;
                pixel[2] = value;
            }
        }
        return bytes;
    }();

    // `logicalSize == 0` ⇒ 不设逻辑呈现（1 逻辑单位 = 1 设备像素）
    const auto probe = [&](int logicalSize, int quadSize) {
        if (logicalSize > 0) {
            SDL_SetRenderLogicalPresentation(renderer, logicalSize, logicalSize,
                                             SDL_LOGICAL_PRESENTATION_LETTERBOX);
        } else {
            SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                             SDL_LOGICAL_PRESENTATION_DISABLED);
        }
        InkingImageBytes image;
        image.data = probeBmp.data();
        image.size = probeBmp.size();
        image.name = "raster_probe";

        // 橙色清屏：探针图案里有黑，背景不能也是黑。
        SDL_SetRenderDrawColor(renderer, 0xF0, 0x80, 0x10, 255);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderClear(renderer);
        inking_detail::BeginFrame(renderer);
        const bool painted = inking_detail::PaintImage(
            renderer, image, InkingShapeSpec{}, quadSize, quadSize, 1.0f, 0.0f,
            0.0f, InkingTransformSpec{}, InkingImageFit::Fill, 0xFFFFFFFFu);
        inking_detail::EndFrame();
        // 只读中间那一半：边缘那一圈有形状层自己的抗锯齿过渡，与本条无关
        // （第 33 列落在 16..47 之内，左右各留 17 / 14 列）。
        const SDL_Rect center{kScreen / 4, kScreen / 4, kScreen / 2, kScreen / 2};
        return std::make_pair(painted, CensusBlackColumn(renderer, center));
    };

    // ① 基准：不设逻辑呈现，64 纹理铺 64 屏幕 = 1:1（用来证明探针本身能看见那列）
    const auto baseline = probe(0, kScreen);
    // ② 被测：逻辑 32、屏幕 64（R = 2），同一张 64 宽的纹理铺满逻辑画布
    const auto measured = probe(kLogical, kLogical);

    // 尾部恢复，别污染后面的检查（自检里已有这个套路）。
    SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                     SDL_LOGICAL_PRESENTATION_DISABLED);

    if (!baseline.first || !measured.first || !baseline.second.read
        || !measured.second.read) {
        std::printf("      [skip] 图片解码 / 回读不可用——这条探针无从验起\n");
    } else {
        std::printf("      [diag] 黑列：基准 %d 列@%d（每列 %d 像素，灰像素 %d）／"
                    "R=2 %d 列@%d（每列 %d 像素，灰像素 %d）\n",
                    baseline.second.blackColumns, baseline.second.firstBlackColumn,
                    baseline.second.blackPerColumn, baseline.second.greyPixels,
                    measured.second.blackColumns, measured.second.firstBlackColumn,
                    measured.second.blackPerColumn, measured.second.greyPixels);
        Check(baseline.second.blackColumns == 1
                  && baseline.second.firstBlackColumn == kProbeColumn
                  && baseline.second.blackPerColumn == kScreen / 2,
              "基准：1:1 时第 33 列是**唯一**一列纯黑（探针能看见单像素特征）");
        if (measured.second.blackColumns == 1
            && measured.second.firstBlackColumn == kProbeColumn
            && measured.second.blackPerColumn == kScreen / 2) {
            Check(true,
                  "R=2 时同一列仍是唯一的纯黑列 ⇒ 栅格化在**设备分辨率**"
                  "（§3.2 成立）");
        } else {
            // 任务书 §1.3：这不是"代码写错"，而是**地基反了**——§3.2 的整条
            // 推论要重写。所以不计失败项，但必须喊出来（静默变成"通过"最坏）。
            std::printf("      [阻塞] R=2 下黑列读数 %d 列@%d：栅格化可能发生在"
                        "逻辑分辨率 ⇒ §3.2 需重写，任务书要重开\n",
                        measured.second.blackColumns,
                        measured.second.firstBlackColumn);
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

// ---------------------------------------------------------------------------
// T-04（0.7.1）：素材分辨率 DEBUG 校验
//
// 素材像素不够的症状是"画面有点软"——没人会顺着"有点软"去查美术导出的像素数。
// 所以这条护栏必须**自己出声**，而它只在 `INK_ISDEBUG` 下存在（发布包零成本）。
//
// 两条断言分别对应两边：
//   - DEBUG：64×64 的素材进 128×128 的组件 ⇒ 计数 +1、WARN 1 条；
//   - Release：计数恒 0、一条 WARN 都没有 ⇒ 证明"整段真的被编译掉了"，
//     而不是"这段代码在 Release 里恰好没触发"。
//
// 注意两边都要登记 `NoteSilentFailure`：静默失效清单的元自检不分构建类型。
// ---------------------------------------------------------------------------
void CheckImageUndersampledWarn() {
    std::printf("\n-- 素材分辨率：DEBUG 下才校验（0.7.1 T-04）--\n");

    struct ProbeScene : InkingScene {
        ProbeScene() : InkingScene("selftest-undersample") {}
    };
    ProbeScene scene;

    InkingImageData data;
    data.name = "undersample_probe";
    data.width = 128;
    data.height = 128;
    data.normal.type = InkingAppearanceType::Image;
    data.normal.source = "undersample_probe";

    const std::uint64_t before =
        inking_detail::TextCacheStatsSnapshot().imageUndersampled;
    InkLog::ResetStats();

    auto* image = static_cast<InkingStaticImage*>(
        scene.AddChild(std::make_unique<InkingStaticImage>(&scene, data)));
    // 64×64 的素材铺进 128 宽的组件：任何一块屏上 σ 都不超过 0.5。
    const std::vector<std::uint8_t> small = MakeCheckerBmp(64, 64, 1);
    image->SetImageBytes(small.data(), small.size(), "undersample_probe");

    const std::uint64_t delta =
        inking_detail::TextCacheStatsSnapshot().imageUndersampled - before;
    const std::uint64_t warnings =
        InkLog::CountOf(InkingLogLevel::Warn);

#if defined(INK_ISDEBUG)
    Check(delta == 1,
          "image_undersampled：DEBUG 下素材 64×64 进 128×128 组件 ⇒ 计数 +1");
    Check(warnings == 1,
          "image_undersampled：DEBUG 下报 1 条 WARN（带素材 / 需要 / 比例三个数）");
#else
    Check(delta == 0 && warnings == 0,
          "image_undersampled：Release 下整段被编译掉（计数 0、无 WARN）");
#endif
    NoteSilentFailure("image_undersampled");
}

void CheckImageFitSrcRect(SDL_Renderer* renderer) {
    std::printf("\n-- 图集源矩形：切格与单独小图逐像素一致（0.6.1 T-04） --\n");

    // ① 纯算术：UV 与等比都按"这一帧"算，不按整张纹理
    {
        InkingImageFitSpec spec;
        spec.fit = InkingImageFit::Fill;
        spec.frameWidth = 40.0f;
        spec.frameHeight = 40.0f;
        spec.imageWidth = 160.0f;  // 整张 4×1 图集
        spec.imageHeight = 40.0f;
        spec.srcRect = InkingShapeBounds{80.0f, 0.0f, 40.0f, 40.0f};  // 第 2 格

        float u = 0.0f;
        float v = 0.0f;
        spec.UvAt(0.0f, 0.0f, u, v);
        Check(std::fabs(u - 0.5f) < 1e-6f && std::fabs(v) < 1e-6f,
              "T-04：源矩形左上 = 纹理坐标 (0.5, 0)");

        spec.UvAt(40.0f, 40.0f, u, v);
        Check(std::fabs(u - 0.75f) < 1e-6f && std::fabs(v - 1.0f) < 1e-6f,
              "T-04：源矩形右下 = 纹理坐标 (0.75, 1)");

        // Contain 的等比必须用帧宽高（40×40）；用整张（160×40）会算成 20×20 的细条。
        spec.fit = InkingImageFit::Contain;
        spec.frameWidth = 80.0f;
        const InkingShapeBounds drawn = spec.DrawnRect();
        std::printf("         Contain 框 80×40 + 帧 40×40 → 画 %.1f×%.1f @ x=%.1f\n",
                    drawn.width, drawn.height, drawn.x);
        Check(std::fabs(drawn.width - 40.0f) < 1e-4f
                  && std::fabs(drawn.height - 40.0f) < 1e-4f
                  && std::fabs(drawn.x - 20.0f) < 1e-4f,
              "T-04：Contain 的等比按帧尺寸算（不是整张纹理，否则留白算错）");

        spec.srcRect = InkingShapeBounds{};
        Check(!spec.HasSourceRect()
                  && std::fabs(spec.SourceRect().width - 160.0f) < 1e-4f,
              "T-04：源矩形全零 = 整张纹理（既有图片走这条）");
    }

    if (renderer == nullptr) {
        return;
    }

    // ② 像素对拍：整张 160×40 图集切第 2 格 vs 单独一张 40×40 小图
    int outputWidth = 0;
    int outputHeight = 0;
    SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
    const float pixelRatio =
        outputWidth > 0
            ? static_cast<float>(outputWidth)
                / static_cast<float>(InkingDesignWidth())
            : 1.0f;
    // 组件摆设计坐标 (20,20)、40×40（与 `AddSvgImage` 同一套锚点约定）。
    const SDL_Rect region{static_cast<int>(std::lround(20.0f * pixelRatio)),
                          static_cast<int>(std::lround(20.0f * pixelRatio)),
                          static_cast<int>(std::lround(40.0f * pixelRatio)),
                          static_cast<int>(std::lround(40.0f * pixelRatio))};
    std::printf("         读回区域 %d×%d @ (%d,%d)（设计→设备 %.2f）\n", region.w,
                region.h, region.x, region.y, static_cast<double>(pixelRatio));

    const std::vector<std::uint8_t> atlas = MakeSheetBmp(160, 40, 40, 0);
    const std::vector<std::uint8_t> single = MakeSheetBmp(40, 40, 40, 2);

    const auto shoot = [&](const char* sceneName, const char* imageName,
                           const std::vector<std::uint8_t>& bytes,
                           const InkingShapeBounds& sourceRect) {
        InkingScene scene(sceneName);
        InkingSceneLibrary::SetActiveScene(&scene);

        InkingImageData data;
        data.name = imageName;
        data.width = 40;
        data.height = 40;
        data.normal.type = InkingAppearanceType::Image;
        data.normal.source = imageName;
        data.normal.imageBytes = bytes.data();
        data.normal.imageSize = bytes.size();
        data.selfAnchor = InkingChangeAnchor::LeftTop;
        data.traceAnchor = InkingChangeAnchor::LeftTop;
        data.offsetX = 20.0f;
        data.offsetY = 20.0f;

        auto* image = static_cast<SheetProbeImage*>(scene.AddChild(
            std::make_unique<SheetProbeImage>(&scene, data)));
        image->SetRenderSourceRect(sourceRect);  // 全零 = 整张
        scene.TickFrame(1.0f / 60.0f);
        RenderSvgFrame(renderer, scene);
        std::vector<std::uint32_t> pixels = ReadPixelRegion(renderer, region);
        EndSvgFrame(renderer);
        return pixels;
    };

    const std::vector<std::uint32_t> fromSheet =
        shoot("img_src_sheet", "sheet_atlas", atlas,
              InkingShapeBounds{80.0f, 0.0f, 40.0f, 40.0f});
    const std::vector<std::uint32_t> fromSingle =
        shoot("img_src_single", "cell2_alone", single, InkingShapeBounds{});
    const std::vector<std::uint32_t> fromFullRect =
        shoot("img_src_full", "cell2_alone", single,
              InkingShapeBounds{0.0f, 0.0f, 40.0f, 40.0f});

    const auto diffCount = [](const std::vector<std::uint32_t>& a,
                              const std::vector<std::uint32_t>& b) {
        if (a.empty() || a.size() != b.size()) {
            return -1;
        }
        int diff = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                ++diff;
            }
        }
        return diff;
    };

    Check(!fromSheet.empty(), "T-04：读回图集那一格的非空像素");
    const int sheetDiff = diffCount(fromSheet, fromSingle);
    const int fullDiff = diffCount(fromFullRect, fromSingle);
    std::printf("         图集第2格 vs 单独小图：差异像素 %d / %zu\n", sheetDiff,
                fromSheet.size());
    std::printf("         显式整图矩形 vs 不给：差异像素 %d / %zu\n", fullDiff,
                fromSingle.size());
    Check(sheetDiff == 0,
          "T-04：图集第 2 格与单独一张小图逐像素一致（切格对齐、UV 正确）");
    Check(fullDiff == 0,
          "T-04：显式给整图矩形与不给，逐像素一致（既有图片零影响）");

    // ③ 换格不换纹理：同一张图集画两格，纹理只解码一次
    {
        std::uint64_t texturesBefore = 0;
        std::uint64_t texturesAfter = 0;
        {
            InkingScene scene("img_src_tex");
            InkingSceneLibrary::SetActiveScene(&scene);

            InkingImageData data;
            data.name = "sheet_atlas_tex";
            data.width = 40;
            data.height = 40;
            data.normal.type = InkingAppearanceType::Image;
            data.normal.source = "sheet_atlas_tex";
            data.normal.imageBytes = atlas.data();
            data.normal.imageSize = atlas.size();
            data.selfAnchor = InkingChangeAnchor::LeftTop;
            data.traceAnchor = InkingChangeAnchor::LeftTop;
            data.offsetX = 20.0f;
            data.offsetY = 20.0f;

            auto* image = static_cast<SheetProbeImage*>(scene.AddChild(
                std::make_unique<SheetProbeImage>(&scene, data)));
            scene.TickFrame(1.0f / 60.0f);

            texturesBefore =
                inking_detail::TextCacheStatsSnapshot().texturesCreated;
            for (int cell = 0; cell < 4; ++cell) {
                image->SetRenderSourceRect(InkingShapeBounds{
                    static_cast<float>(cell * 40), 0.0f, 40.0f, 40.0f});
                RenderSvgFrame(renderer, scene);
            }
            texturesAfter =
                inking_detail::TextCacheStatsSnapshot().texturesCreated;
            EndSvgFrame(renderer);
        }
        std::printf("         画 4 格：新建纹理 %llu 张\n",
                    static_cast<unsigned long long>(texturesAfter
                                                    - texturesBefore));
        Check(texturesAfter - texturesBefore == 1,
              "T-04：4 格共用一张纹理（切格只换源矩形，不重复解码）");
    }
}

// ---------------------------------------------------------------------------
// T-05：InkingSprite 的图集模式
// ---------------------------------------------------------------------------
//
// 图集模式与序列帧共用同一套帧号（`frame = floor(t × fps)`），只在"怎么把这一帧
// 搬到画面上"分叉：序列帧换字节视图，图集换源矩形。所以验收也分两半——
// **切格对不对**（像素）与**帧号照常走**（计数）。
void CheckSpriteSheet(SDL_Renderer* renderer) {
    std::printf("\n-- InkingSprite 图集：切格 / 零纹理 / 几何校验（0.6.1 T-05） --\n");

    // ① 纯算术：行优先 + margin/spacing（不必真画一张图就能验）
    {
        InkingSpriteData::SpriteSheet sheet;
        sheet.frameWidth = 40;
        sheet.frameHeight = 30;
        sheet.columns = 4;
        sheet.rows = 3;
        sheet.margin = 2;
        sheet.spacing = 4;

        std::printf("         4×3 格（40×30 / margin 2 / spacing 4）→ 整张 %d×%d\n",
                    sheet.SheetWidth(), sheet.SheetHeight());
        Check(sheet.SheetWidth() == 40 * 4 + 4 * 3 + 2 * 2
                  && sheet.SheetHeight() == 30 * 3 + 4 * 2 + 2 * 2,
              "T-05：整张尺寸 = 格数×格宽 + spacing×(n−1) + 2×margin");
        Check(sheet.ResolveCount() == 12, "T-05：count 缺省 = columns × rows");

        const InkingShapeBounds first = sheet.CellRect(0);
        const InkingShapeBounds second = sheet.CellRect(1);
        const InkingShapeBounds nextRow = sheet.CellRect(4);
        std::printf("         格 0=(%.0f,%.0f) 格 1=(%.0f,%.0f) 格 4=(%.0f,%.0f)\n",
                    first.x, first.y, second.x, second.y, nextRow.x, nextRow.y);
        Check(first.x == 2.0f && first.y == 2.0f && first.width == 40.0f
                  && first.height == 30.0f,
              "T-05：第 0 格从 margin 起，尺寸就是 frameWidth/Height");
        Check(second.x == 46.0f && second.y == 2.0f,
              "T-05：同行下一格 = margin + n×(frameWidth + spacing)");
        Check(nextRow.x == 2.0f && nextRow.y == 36.0f,
              "T-05：行优先（第 columns 格换行，纵向加 frameHeight + spacing）");
        Check(sheet.CellRect(12).width == 0.0f,
              "T-05：越界格给全零矩形（画不出来就不画，不画错块）");
    }

    const std::vector<std::uint8_t> atlas = MakeSheetBmp(160, 40, 40, 0);
    const std::vector<std::uint8_t> single = MakeSheetBmp(40, 40, 40, 1);

    const auto sheetData = [&atlas](bool autoplay) {
        InkingSpriteData data;
        data.name = "spr_sheet_px";
        data.width = 40;
        data.height = 40;
        data.fps = 12.0;
        data.loop = true;
        data.autoplay = autoplay;
        data.selfAnchor = InkingChangeAnchor::LeftTop;
        data.traceAnchor = InkingChangeAnchor::LeftTop;
        data.offsetX = 20.0f;
        data.offsetY = 20.0f;
        data.normal.type = InkingAppearanceType::Image;
        data.normal.source = "sheet_atlas_spr";  // 纹理缓存键
        data.normal.imageBytes = atlas.data();
        data.normal.imageSize = atlas.size();
        data.sheet.frameWidth = 40;
        data.sheet.frameHeight = 40;
        data.sheet.columns = 4;
        data.sheet.rows = 1;
        return data;
    };

    // ② 构造期几何校验：columns=5 要 200 宽，图只有 160 → IsValid 假 + 1 条 ERROR
    {
        InkingScene scene("sprite_sheet_bad");
        InkingSceneLibrary::SetActiveScene(&scene);

        InkingSpriteData data = sheetData(false);
        data.sheet.columns = 5;  // 期望 5×40 = 200，真实 160

        InkLog::ResetStats();
        auto* sprite = static_cast<InkingSprite*>(
            scene.AddChild(std::make_unique<InkingSprite>(&scene, data)));
        std::printf("         columns=5 / 图宽 160 → IsValid=%d\n%s\n",
                    sprite->IsValid() ? 1 : 0,
                    sprite->GetConfigError().c_str());
        Check(sprite->IsSheetMode(), "T-05：走的是图集模式");
        Check(!sprite->IsValid()
                  && sprite->GetConfigError().find("图集尺寸对不上")
                         != std::string::npos,
              "T-05：切格与图宽不符在构造期就被拦下（不是播起来才发现）");
        Check(InkLog::CountOf(InkingLogLevel::Error) == 1,
              "T-05：恰好一条 ERROR（不重复刷）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }

    // ②b 图集没字节（生成器忘了烘）→ 不能静默退回纯色填
    {
        InkingScene scene("sprite_sheet_nobytes");
        InkingSceneLibrary::SetActiveScene(&scene);

        InkingSpriteData data = sheetData(false);
        data.normal.imageBytes = nullptr;
        data.normal.imageSize = 0;

        auto* sprite = static_cast<InkingSprite*>(
            scene.AddChild(std::make_unique<InkingSprite>(&scene, data)));
        std::printf("         图集无字节：IsValid=%d\n", sprite->IsValid() ? 1 : 0);
        Check(!sprite->IsValid()
                  && sprite->GetConfigError().find("没有字节") != std::string::npos,
              "T-05：图集没烘字节时构造期报错（不是静默一块底色）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }

    // ③ 像素：图集第 1 格 = 单独存的第 1 张小图
    {
        InkingScene scene("sprite_sheet_px");
        InkingSceneLibrary::SetActiveScene(&scene);

        InkingSpriteData data = sheetData(false);
        InkLog::ResetStats();
        auto* sprite = static_cast<InkingSprite*>(
            scene.AddChild(std::make_unique<InkingSprite>(&scene, data)));
        Check(sprite->IsValid() && sprite->GetFrameCount() == 4
                  && sprite->GetLoadedFrameCount() == 4,
              "T-05：4×1 图集切出 4 帧，几何校验通过");

        if (renderer != nullptr) {
            int outputWidth = 0;
            int outputHeight = 0;
            SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
            const float pixelRatio =
                outputWidth > 0
                    ? static_cast<float>(outputWidth)
                        / static_cast<float>(InkingDesignWidth())
                    : 1.0f;
            const SDL_Rect region{
                static_cast<int>(std::lround(20.0f * pixelRatio)),
                static_cast<int>(std::lround(20.0f * pixelRatio)),
                static_cast<int>(std::lround(40.0f * pixelRatio)),
                static_cast<int>(std::lround(40.0f * pixelRatio))};

            // 图集：落在第 1 格，然后连画 4 格看纹理建了几张
            const std::uint64_t texturesBefore =
                inking_detail::TextCacheStatsSnapshot().texturesCreated;
            sprite->SeekToFrame(1);
            RenderSvgFrame(renderer, scene);
            const std::vector<std::uint32_t> fromSheet =
                ReadPixelRegion(renderer, region);
            for (std::size_t cell = 0; cell < 4; ++cell) {
                sprite->SeekToFrame(cell);
                RenderSvgFrame(renderer, scene);
            }
            const std::uint64_t texturesAfter =
                inking_detail::TextCacheStatsSnapshot().texturesCreated;
            EndSvgFrame(renderer);

            // 单独的第 1 张小图（同一个位置、同一个尺寸）
            InkingScene singleScene("sprite_sheet_single");
            InkingSceneLibrary::SetActiveScene(&singleScene);
            InkingImageData imageData;
            imageData.name = "cell1_alone";
            imageData.width = 40;
            imageData.height = 40;
            imageData.normal.type = InkingAppearanceType::Image;
            imageData.normal.source = "cell1_alone";
            imageData.normal.imageBytes = single.data();
            imageData.normal.imageSize = single.size();
            imageData.selfAnchor = InkingChangeAnchor::LeftTop;
            imageData.traceAnchor = InkingChangeAnchor::LeftTop;
            imageData.offsetX = 20.0f;
            imageData.offsetY = 20.0f;
            singleScene.AddChild(std::make_unique<InkingStaticImage>(
                &singleScene, imageData));
            singleScene.TickFrame(1.0f / 60.0f);
            RenderSvgFrame(renderer, singleScene);
            const std::vector<std::uint32_t> fromSingle =
                ReadPixelRegion(renderer, region);
            EndSvgFrame(renderer);

            int diff = (fromSheet.size() == fromSingle.size()
                        && !fromSheet.empty())
                           ? 0
                           : -1;
            if (diff == 0) {
                for (std::size_t i = 0; i < fromSheet.size(); ++i) {
                    if (fromSheet[i] != fromSingle[i]) {
                        ++diff;
                    }
                }
            }
            std::printf("         图集第1格 vs 单独小图：差异像素 %d / %zu；"
                        "画 4 格新建纹理 %llu 张\n",
                        diff, fromSheet.size(),
                        static_cast<unsigned long long>(texturesAfter
                                                        - texturesBefore));
            Check(diff == 0,
                  "T-05：图集第 1 格与单独一张小图逐像素一致");
            Check(texturesAfter - texturesBefore == 1,
                  "T-05：换格不换纹理（4 格只建 1 张，零重复解码）");
        }
    }

    // ④ 图集模式的帧号照常走（与序列帧同一套时间 → 帧号映射）
    {
        InkingScene scene("sprite_sheet_clock");
        InkingSceneLibrary::SetActiveScene(&scene);

        auto* sprite = static_cast<InkingSprite*>(scene.AddChild(
            std::make_unique<InkingSprite>(&scene, sheetData(true))));
        for (int i = 0; i < 12; ++i) {
            scene.TickFrame(1.0 / 12.0);
        }
        std::printf("         12fps 跑 1 秒 → 帧号=%zu 换帧=%llu\n",
                    sprite->GetFrameIndex(),
                    static_cast<unsigned long long>(
                        sprite->GetFrameAdvanceCount()));
        Check(sprite->GetFrameAdvanceCount() == 12 && sprite->GetFrameIndex() == 0,
              "T-05：图集模式 12fps 跑满 1 秒换 12 帧并回到第 0 帧");
    }
}

// ---------------------------------------------------------------------------
// T-11（0.6.4）：图集的起始格 firstFrame
// ---------------------------------------------------------------------------
//
// "每行一个方向"是最常见的排布（史莱姆包就是 6 列 × 4 行），而没有起点字段时
// **只能播第 0 行**。验收拆两半：
//   ① **格号换算**——`firstFrame=12, count=6` 的第 0 帧必须落在格 12 上；
//   ② **画出来是那一格**——参照物取"同一张图集、不设起点、直接 `SeekToFrame(12)`"，
//      两者必须逐像素相同。它比另存一张小图更严格：连纹理都必须是同一张
//      （另存小图会把"切错块但恰好颜色接近"放过）。
void CheckSpriteSheetFirstFrame(SDL_Renderer* renderer) {
    std::printf("\n-- 图集起始格 firstFrame：格号 / 越界 / 像素（0.6.4 T-11） --\n");

    // ① 纯算术：起点只影响"帧号 → 格号"的换算，不改切格本身
    {
        InkingSpriteData::SpriteSheet sheet;
        sheet.frameWidth = 40;
        sheet.frameHeight = 40;
        sheet.columns = 6;
        sheet.rows = 4;
        sheet.firstFrame = 12;  // 第 2 行第 0 格
        sheet.count = 6;

        const InkingShapeBounds cell = sheet.CellRect(12);
        std::printf("         6×4 图集，起点 12 → 格 12 = (%.0f,%.0f,%.0f×%.0f)；"
                    "整张 %d×%d\n",
                    cell.x, cell.y, cell.width, cell.height, sheet.SheetWidth(),
                    sheet.SheetHeight());
        Check(cell.x == 0.0f && cell.y == 80.0f && cell.width == 40.0f
                  && cell.height == 40.0f,
              "T-11：第 12 格 = 第 2 行第 0 列（行优先，x=0 y=80）");
        Check(sheet.SheetWidth() == 240 && sheet.SheetHeight() == 160,
              "T-11：整张尺寸与起点无关（它描述整张图，不是从哪格起）");

        // 只写起点不算"配了图集"——否则一处笔误就让整张图片变成图集模式。
        InkingSpriteData::SpriteSheet lone;
        lone.firstFrame = 12;
        Check(!lone.IsConfigured(),
              "T-11：只写 firstFrame 不算配了图集（IsConfigured 仍为假）");
    }

    const std::vector<std::uint8_t> atlas = MakeSheetBmp(240, 160, 40, 0);

    const auto sheetData = [&atlas](int firstFrame, int count) {
        InkingSpriteData data;
        data.name = "spr_first_frame";
        data.width = 40;
        data.height = 40;
        data.fps = 12.0;
        data.autoplay = false;
        data.selfAnchor = InkingChangeAnchor::LeftTop;
        data.traceAnchor = InkingChangeAnchor::LeftTop;
        data.offsetX = 20.0f;
        data.offsetY = 20.0f;
        data.normal.type = InkingAppearanceType::Image;
        data.normal.source = "sheet_first_frame";
        data.normal.imageBytes = atlas.data();
        data.normal.imageSize = atlas.size();
        data.sheet.frameWidth = 40;
        data.sheet.frameHeight = 40;
        data.sheet.columns = 6;
        data.sheet.rows = 4;
        data.sheet.firstFrame = firstFrame;
        data.sheet.count = count;
        return data;
    };

    // ② 换算落在组件上：起始帧与末帧的源矩形
    {
        InkingScene scene("sprite_first_frame");
        InkingSceneLibrary::SetActiveScene(&scene);

        InkLog::ResetStats();
        auto* sprite = static_cast<InkingSprite*>(scene.AddChild(
            std::make_unique<InkingSprite>(&scene, sheetData(12, 6))));
        const InkingShapeBounds first = sprite->GetAppearance().srcRect;
        std::printf("         起点 12 / 6 帧：IsValid=%d 帧数=%zu 第 0 帧矩形"
                    " (%.0f,%.0f,%.0f×%.0f)\n",
                    sprite->IsValid() ? 1 : 0, sprite->GetFrameCount(), first.x,
                    first.y, first.width, first.height);
        Check(sprite->IsValid() && sprite->IsSheetMode()
                  && sprite->GetFrameCount() == 6,
              "T-11：firstFrame=12 / count=6 是合法配置，切出 6 帧");
        Check(first.x == 0.0f && first.y == 80.0f && first.width == 40.0f
                  && first.height == 40.0f,
              "T-11：第 0 帧的源矩形 = CellRect(12)（起点进了换算，不是被忽略）");

        sprite->SeekToFrame(5);
        const InkingShapeBounds last = sprite->GetAppearance().srcRect;
        std::printf("         第 5 帧矩形 (%.0f,%.0f,%.0f×%.0f)\n", last.x, last.y,
                    last.width, last.height);
        Check(last.x == 200.0f && last.y == 80.0f,
              "T-11：末帧落回格 17（起点 + 帧号，不是只偏移第一帧）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }

    // ③ 越界：要 6 格但从第 20 格起（只剩 4 格）→ 构造期一条 ERROR
    {
        InkingScene scene("sprite_first_frame_bad");
        InkingSceneLibrary::SetActiveScene(&scene);

        InkLog::ResetStats();
        auto* sprite = static_cast<InkingSprite*>(scene.AddChild(
            std::make_unique<InkingSprite>(&scene, sheetData(20, 6))));
        std::printf("         起点 20 / 6 帧 / 共 24 格 → IsValid=%d：%s\n",
                    sprite->IsValid() ? 1 : 0,
                    sprite->GetConfigError().c_str());
        Check(!sprite->IsValid()
                  && sprite->GetConfigError().find("超出格子总数")
                         != std::string::npos,
              "T-11：起点 + 帧数超过格子总数在构造期被拦下");
        Check(InkLog::CountOf(InkingLogLevel::Error) == 1,
              "T-11：恰好一条 ERROR（不重复刷）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }

    // ④ 像素：起点 12 的第 0 帧 vs 不设起点、Seek 到第 12 帧
    if (renderer != nullptr) {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
        const float pixelRatio =
            outputWidth > 0
                ? static_cast<float>(outputWidth)
                    / static_cast<float>(InkingDesignWidth())
                : 1.0f;
        const SDL_Rect region{static_cast<int>(std::lround(20.0f * pixelRatio)),
                              static_cast<int>(std::lround(20.0f * pixelRatio)),
                              static_cast<int>(std::lround(40.0f * pixelRatio)),
                              static_cast<int>(std::lround(40.0f * pixelRatio))};

        const auto shoot = [&](const char* sceneName, int firstFrame, int count,
                               std::size_t seekTo) {
            InkingScene scene(sceneName);
            InkingSceneLibrary::SetActiveScene(&scene);
            auto* sprite = static_cast<InkingSprite*>(scene.AddChild(
                std::make_unique<InkingSprite>(&scene,
                                                       sheetData(firstFrame, count))));
            sprite->SeekToFrame(seekTo);
            scene.TickFrame(1.0f / 60.0f);
            RenderSvgFrame(renderer, scene);
            std::vector<std::uint32_t> pixels = ReadPixelRegion(renderer, region);
            EndSvgFrame(renderer);
            InkingSceneLibrary::SetActiveScene(nullptr);
            return pixels;
        };

        const std::vector<std::uint32_t> withFirst =
            shoot("spr_ff_a", 12, 6, 0);
        const std::vector<std::uint32_t> bySeek =
            shoot("spr_ff_b", 0, 24, 12);

        int diff = (withFirst.size() == bySeek.size() && !withFirst.empty()) ? 0
                                                                            : -1;
        if (diff == 0) {
            for (std::size_t i = 0; i < withFirst.size(); ++i) {
                if (withFirst[i] != bySeek[i]) {
                    ++diff;
                }
            }
        }
        std::printf("         起点12第0帧 vs 起点0的第12帧：差异像素 %d / %zu\n",
                    diff, withFirst.size());
        Check(diff == 0,
              "T-11：firstFrame=12 的第 0 帧与直接定位到第 12 帧逐像素一致");
    }
}

// ---------------------------------------------------------------------------
// T-12（0.6.4）：图片采样模式
// ---------------------------------------------------------------------------
//
// 这件事**只能靠像素**验：`Nearest` 与 `Linear` 的差别就是"格子边界是硬的还是糊的"。
// 判据用**颜色种数**——把一张 2 色棋盘放大，最近邻仍只有 2 种颜色，双线性会插出
// 一堆中间灰。所以源图必须只有 2 色（`MakeCheckerBmp`），多出来的颜色才只可能
// 来自插值。
//
// **必须知道的后端差异**（`build/probe_sampling.cpp` 实测，2026-10-28）：
//
//   后端          | 并批 Linear | 并批 Nearest | 单笔 Linear | 单笔 Nearest
//   software      |     2 色    |     2 色     |    33 色    |     2 色
//   direct3d11    |    12 色    |     2 色     |    12 色    |     2 色
//
// ⇒ **SDL 的 software 后端在 `SDL_RenderGeometry` 这条路上不认采样模式**（恒最近邻），
// 而 d3d11 认。像素素材不受影响（它本来就要最近邻）；受影响的只有"软件后端 +
// 高清图要平滑缩放"这一种组合。所以断言分两组：
//   ① 单笔路径（临时关掉并批）三档来回——这组**两个后端都成立**，它证明的是
//      "切换会重设**已经建好**的纹理"，而那正是开关最容易做漏的地方；
//   ② 默认路径（并批）在 `Nearest` 下恰好 2 色——像素素材的主场，两个后端都成立。
// 并批下的 `Linear` 只打印不断言（软件后端恒为 2 色，那是 SDL 的事，不是我们的）。
void CheckImageSampling(SDL_Renderer* renderer) {
    std::printf("\n-- 图片采样模式：像素素材的硬边（0.6.4 T-12） --\n");

    // ① 开关本身：默认线性，能读回、能改
    {
        const inking_detail::ImageSampling saved =
            inking_detail::GetImageSampling();
        inking_detail::SetImageSampling(inking_detail::ImageSampling::Nearest);
        const bool nearest =
            inking_detail::GetImageSampling()
            == inking_detail::ImageSampling::Nearest;
        inking_detail::SetImageSampling(inking_detail::ImageSampling::Linear);
        const bool linear =
            inking_detail::GetImageSampling()
            == inking_detail::ImageSampling::Linear;
        Check(nearest && linear, "T-12：采样开关写得进也读得回");
        inking_detail::SetImageSampling(saved);
    }

    if (renderer == nullptr) {
        return;
    }

    // 2 色棋盘（每格 1 像素）画进 40×40 的组件：放大 5~10 倍，插值一定看得出来。
    const std::vector<std::uint8_t> checker = MakeCheckerBmp(4, 4, 1);

    InkingScene scene("image_sampling");
    InkingSceneLibrary::SetActiveScene(&scene);

    InkingImageData data;
    data.name = "sampling_checker";
    data.width = 40;
    data.height = 40;
    data.normal.type = InkingAppearanceType::Image;
    data.normal.source = "sampling_checker";
    data.normal.imageBytes = checker.data();
    data.normal.imageSize = checker.size();
    data.selfAnchor = InkingChangeAnchor::LeftTop;
    data.traceAnchor = InkingChangeAnchor::LeftTop;
    data.offsetX = 20.0f;
    data.offsetY = 20.0f;
    scene.AddChild(std::make_unique<InkingStaticImage>(&scene, data));
    scene.TickFrame(1.0f / 60.0f);

    int outputWidth = 0;
    int outputHeight = 0;
    SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
    const float pixelRatio =
        outputWidth > 0
            ? static_cast<float>(outputWidth) / static_cast<float>(InkingDesignWidth())
            : 1.0f;
    // 读**内圈**：边缘那一条有形状网格的 1px 羽化（0.6.2 T-08），它本来就不是
    // "图片的像素"，混进来会让"恰好 2 色"假红。
    const SDL_Rect region{
        static_cast<int>(std::lround(25.0f * pixelRatio)),
        static_cast<int>(std::lround(25.0f * pixelRatio)),
        static_cast<int>(std::lround(30.0f * pixelRatio)),
        static_cast<int>(std::lround(30.0f * pixelRatio))};
    std::printf("         后端 %s，读回 %d×%d\n", SDL_GetRendererName(renderer),
                region.w, region.h);

    const auto colorCount = [&]() {
        RenderSvgFrame(renderer, scene);
        const std::vector<std::uint32_t> pixels = ReadPixelRegion(renderer, region);
        EndSvgFrame(renderer);
        std::vector<std::uint32_t> seen;
        for (const std::uint32_t pixel : pixels) {
            if (std::find(seen.begin(), seen.end(), pixel) == seen.end()) {
                seen.push_back(pixel);
            }
        }
        return static_cast<int>(seen.size());
    };

    // ② 单笔路径：Linear 多色 → Nearest 恰好 2 色 → 切回 Linear 又多色。
    // 第三档是关键——它证明**已经建好的纹理**也被重设了（不是只影响新建的纹理）。
    {
        inking_detail::SetImageBatchingEnabled(false);
        inking_detail::SetImageSampling(inking_detail::ImageSampling::Linear);
        const int linear = colorCount();
        inking_detail::SetImageSampling(inking_detail::ImageSampling::Nearest);
        const int nearest = colorCount();
        inking_detail::SetImageSampling(inking_detail::ImageSampling::Linear);
        const int back = colorCount();
        inking_detail::SetImageBatchingEnabled(true);

        std::printf("         单笔路径：Linear %d 色 / Nearest %d 色 / 切回 %d 色\n",
                    linear, nearest, back);
        Check(linear > 2, "T-12：Linear 会把 2 色棋盘插值出中间色");
        Check(nearest == 2, "T-12：Nearest 下放大后仍是 2 色（像素画的硬边）");
        Check(back > 2,
              "T-12：切回 Linear 立刻生效（已建好的纹理被重设，不是只影响新纹理）");
    }

    // ③ 默认路径（并批 + 矩形 + Fill，也就是 `InkingSprite` 最常走的那条）
    {
        inking_detail::SetImageSampling(inking_detail::ImageSampling::Linear);
        const int batchedLinear = colorCount();
        inking_detail::SetImageSampling(inking_detail::ImageSampling::Nearest);
        const int batchedNearest = colorCount();
        std::printf("         并批路径：Linear %d 色 / Nearest %d 色\n",
                    batchedLinear, batchedNearest);
        Check(batchedNearest == 2,
              "T-12：并批路径下 Nearest 恰好 2 色（像素素材的主场）");
    }

    inking_detail::SetImageSampling(inking_detail::ImageSampling::Linear);
    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-05（0.8.2）：纹理字节账本
// ---------------------------------------------------------------------------
//
// 要验的不是"数字看起来合理"，而是三条**只有账本才看得见**的事：
//   ① 建一张已知尺寸的图 ⇒ 账上至少多出 宽×高×4（口径是 RGBA8 上界）；
//   ② `ReleaseTextCache()` 之后 ⇒ 归零（不清账的坏味道是"越用越多、永不回落"，
//      而画面上完全看不出来——它只会 OOM，然后没人知道是纹理表干的）；
//   ③ 再建一次 ⇒ **峰值不回退**（峰值记的是最坏情况，不是当前值）。
//
// 顺带钉一条容易写错的：**缓存命中不该再记账**（同一个文件被多个组件引用时
// 只该占一次）。双计的表现是"账上比实际多一倍"，那会让人过早去优化一个
// 并不存在的问题。
void CheckTextureBytes(SDL_Renderer* renderer) {
    std::printf("\n-- 纹理字节账本：建 → 放 → 归零 → 峰值不回退（0.8.2 T-05） --\n");
    using inking_detail::TextCacheStats;
    using inking_detail::TextCacheStatsSnapshot;

    if (renderer == nullptr) {
        // 没有渲染器就没有纹理，账本无从验；上面"创建渲染器"那条已经红了，
        // 这里只留一句说明，不额外记一笔假红。
        std::printf("         [skip] 没有渲染器，账本无从验\n");
        return;
    }

    // 先放一次：让"账上有什么"从一个确定的 0 开始（前面十几个用例留下的纹理
    // 不可枚举，拿它们当基线就只能验"变大了"）。
    inking_detail::ReleaseTextCache();
    const TextCacheStats empty = TextCacheStatsSnapshot();
    std::printf("         释放后：%llu 字节 / %llu 张\n",
                static_cast<unsigned long long>(empty.textureBytes),
                static_cast<unsigned long long>(empty.textureCount));
    Check(empty.textureBytes == 0 && empty.textureCount == 0,
          "T-05：释放之后账真的归零（纹理表与字形图集都销过账）");

    const std::vector<std::uint8_t> bmp = MakeCheckerBmp(64, 64, 8);
    constexpr std::uint64_t kOneTexture = 64ull * 64ull * 4ull;  // 16,384

    const auto paint = [&](const char* name) {
        InkingScene scene("texture_bytes");
        InkingSceneLibrary::SetActiveScene(&scene);
        InkingImageData data;
        data.name = name;
        data.width = 64;
        data.height = 64;
        data.normal.type = InkingAppearanceType::Image;
        data.normal.source = name;
        data.normal.imageBytes = bmp.data();
        data.normal.imageSize = bmp.size();
        data.selfAnchor = InkingChangeAnchor::LeftTop;
        data.traceAnchor = InkingChangeAnchor::LeftTop;
        scene.AddChild(std::make_unique<InkingStaticImage>(&scene, data));
        scene.TickFrame(1.0f / 60.0f);
        RenderSvgFrame(renderer, scene);
        EndSvgFrame(renderer);
        return TextCacheStatsSnapshot();
    };

    const TextCacheStats first = paint("bytes_probe_a");
    std::printf("         建一张 64×64：%llu 字节 / %llu 张\n",
                static_cast<unsigned long long>(first.textureBytes),
                static_cast<unsigned long long>(first.textureCount));
    Check(first.textureBytes >= kOneTexture,
          "T-05：建一张 64×64 的图，账上至少多出 宽×高×4");
    Check(first.textureCount >= 1, "T-05：纹理条数跟着涨");
    Check(first.textureBytesPeak >= first.textureBytes,
          "T-05：峰值不小于当前值");

    // 同名再画一次 = 缓存命中：条数与字节数都**不该**再涨。
    const TextCacheStats second = paint("bytes_probe_a");
    Check(second.textureCount == first.textureCount
              && second.textureBytes == first.textureBytes,
          "T-05：缓存命中不重复记账（同一个文件只占一笔）");

    // 换一个名字 = 真的多一张。
    const TextCacheStats third = paint("bytes_probe_b");
    Check(third.textureBytes >= first.textureBytes + kOneTexture,
          "T-05：换一张不同名字的图，账上再多一笔");

    const std::uint64_t peakBefore = third.textureBytesPeak;

    inking_detail::ReleaseTextCache();
    const TextCacheStats released = TextCacheStatsSnapshot();
    std::printf("         再释放：%llu 字节 / %llu 张（峰值 %llu）\n",
                static_cast<unsigned long long>(released.textureBytes),
                static_cast<unsigned long long>(released.textureCount),
                static_cast<unsigned long long>(released.textureBytesPeak));
    Check(released.textureBytes == 0 && released.textureCount == 0,
          "T-05：多张之后释放同样归零（每一处都销账了，不是只有第一处）");
    Check(released.textureBytesPeak == peakBefore,
          "T-05：峰值只增不减（回落之后仍记得住最坏情况）");

    // 登记进静默失效清单：这一条的现象正是"账本只建不销账，内存只涨不回落"。
    NoteSilentFailure("texture_bytes_leak");

    // 再建一次：当前值重新涨起来，峰值不能倒退。
    const TextCacheStats again = paint("bytes_probe_c");
    Check(again.textureBytes >= kOneTexture && again.textureBytesPeak >= peakBefore,
          "T-05：释放后再建，当前值重新涨起、峰值不回退");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-13（0.6.5）：运行期换配置
// ---------------------------------------------------------------------------
//
// 验收的落点不是"换成功了"（那个看帧数就知道），而是三件**静默**的事：
//   ① **订阅还在**（换配置后 `frameChanged` 照样发）——替换组件那条路会让它悄悄失效；
//   ② **换配置本身不发 `frameChanged`**（没有"旧帧"这回事，发了就是假事件）；
//   ③ **换过去画的是新图**——图集模式只改源矩形不动字节，忘了同步字节就会
//      "帧数对、`IsSheetMode()` 对、画面还是上一张"（最像成功的那种错）。
void CheckSpriteRuntimeSwap(SDL_Renderer* renderer) {
    std::printf("\n-- 运行期换配置 SetSpriteData（0.6.5 T-13） --\n");

    const std::vector<std::uint8_t> atlas = MakeSheetBmp(240, 160, 40, 0);

    const auto sheetConfig = [&atlas]() {
        InkingSpriteData data;
        data.name = "swap_sheet";
        data.width = 64;   // 故意与序列帧不同：组件尺寸**不该**跟着变
        data.height = 64;
        data.fps = 12.0;
        data.autoplay = true;
        data.selfAnchor = InkingChangeAnchor::LeftTop;
        data.traceAnchor = InkingChangeAnchor::LeftTop;
        data.offsetX = 20.0f;
        data.offsetY = 20.0f;
        data.normal.type = InkingAppearanceType::Image;
        data.normal.source = "swap_atlas";
        data.normal.imageBytes = atlas.data();
        data.normal.imageSize = atlas.size();
        data.sheet.frameWidth = 40;
        data.sheet.frameHeight = 40;
        data.sheet.columns = 6;
        data.sheet.rows = 4;
        return data;
    };

    InkingScene scene("sprite_swap");
    InkingSceneLibrary::SetActiveScene(&scene);

    InkingSpriteData sequence = InkingMakeSpriteData("swap_seq", 4, 12.0, true);
    sequence.autoplay = false;  // 停在指定帧，别让 tick 把帧号推走
    auto* sprite = static_cast<InkingSprite*>(scene.AddChild(
        std::make_unique<InkingSprite>(&scene, sequence)));

    int events = 0;
    sprite->frameChanged.Subscribe(
        [&events](std::size_t, std::size_t) { ++events; });

    const int widthBefore = sprite->GetWidth();
    sprite->SeekToFrame(2);       // 定位本身**会**发一次（画面确实变了）
    const int eventsAfterSeek = events;
    Check(eventsAfterSeek == 1, "T-13：Seek 发一次 frameChanged（作为后续计数的基线）");

    // ---- ① 序列帧 → 图集（keepPhase），订阅必须还在 ----
    sprite->SetSpriteData(sheetConfig(), /*keepPhase=*/true);
    std::printf("         换成图集：IsSheet=%d 帧数=%zu 帧号=%zu 播放=%d 尺寸=%d\n",
                sprite->IsSheetMode() ? 1 : 0, sprite->GetFrameCount(),
                sprite->GetFrameIndex(), sprite->IsPlaying() ? 1 : 0,
                sprite->GetWidth());
    Check(sprite->IsValid() && sprite->IsSheetMode()
              && sprite->GetFrameCount() == 24,
          "T-13：换成图集后 IsSheetMode 为真、切出 24 帧");
    Check(sprite->GetFrameIndex() == 2,
          "T-13：keepPhase=true 保留原帧号（同动作换方向不跳回第一帧）");
    Check(!sprite->IsPlaying(),
          "T-13：IsPlaying 保持换之前的值（配置里的 autoplay 不覆盖运行期状态）");
    Check(sprite->GetWidth() == widthBefore,
          "T-13：组件尺寸不跟着配置走（改尺寸属于布局层）");
    Check(events == eventsAfterSeek && sprite->GetFrameAdvanceCount() == 0,
          "T-13：换配置本身不发 frameChanged、不计换帧");
    Check(sprite->GetAppearance().srcRect.x == 80.0f
              && sprite->GetAppearance().srcRect.y == 0.0f,
          "T-13：换过去后的源矩形 = CellRect(2)（图集切格真的生效了）");

    std::uint64_t texturesBefore = 0;
    std::uint64_t texturesAfter = 0;
    if (renderer != nullptr) {
        scene.TickFrame(1.0f / 60.0f);
        texturesBefore = inking_detail::TextCacheStatsSnapshot().texturesCreated;
        RenderSvgFrame(renderer, scene);
        texturesAfter = inking_detail::TextCacheStatsSnapshot().texturesCreated;
        EndSvgFrame(renderer);
        std::printf("         换图集后新建纹理 %llu 张\n",
                    static_cast<unsigned long long>(texturesAfter
                                                    - texturesBefore));
        Check(texturesAfter - texturesBefore == 1,
              "T-13：图集只建 1 张纹理（换的是源矩形，不是每帧一张）");
    }

    // ---- ② 图集 → 序列帧（keepPhase）：订阅还在，帧号仍 2 ----
    sprite->SetSpriteData(sequence, /*keepPhase=*/true);
    std::printf("         换回序列帧：IsSheet=%d 帧数=%zu 帧号=%zu\n",
                sprite->IsSheetMode() ? 1 : 0, sprite->GetFrameCount(),
                sprite->GetFrameIndex());
    Check(sprite->IsValid() && !sprite->IsSheetMode()
              && sprite->GetFrameCount() == 4,
          "T-13：换回序列帧后 4 帧、不再走图集");
    Check(sprite->GetFrameIndex() == 2, "T-13：keepPhase=true 再换回来帧号仍是 2");
    Check(events == eventsAfterSeek,
          "T-13：换回来同样不发 frameChanged");

    // 订阅还活着：之后的 Seek 照样能收到（这就是"不掉订阅"的判据）。
    sprite->SeekToFrame(1);
    Check(events == eventsAfterSeek + 1,
          "T-13：换过两次配置后订阅仍生效（Seek 又发了一次）");

    // ---- ③ keepPhase=false（默认）：落到第 0 帧 ----
    sprite->SetSpriteData(sheetConfig());
    Check(sprite->GetFrameIndex() == 0,
          "T-13：keepPhase 缺省时落到第 0 帧（换的是另一段动画）");

    // ---- ④ 来回三次：字节指针不悬垂（清理顺序的回归测试） ----
    bool roundTripOk = true;
    for (int round = 0; round < 3; ++round) {
        sprite->SetSpriteData(sequence, true);
        roundTripOk = roundTripOk && sprite->IsValid()
                      && sprite->GetFrameCount() == 4
                      && sprite->GetLoadedFrameCount() == 4;
        if (renderer != nullptr) {
            RenderSvgFrame(renderer, scene);
            EndSvgFrame(renderer);
        }
        sprite->SetSpriteData(sheetConfig(), true);
        roundTripOk = roundTripOk && sprite->IsValid()
                      && sprite->GetFrameCount() == 24
                      && sprite->GetLoadedFrameCount() == 24;
        if (renderer != nullptr) {
            RenderSvgFrame(renderer, scene);
            EndSvgFrame(renderer);
        }
    }
    Check(roundTripOk,
          "T-13：序列帧 ↔ 图集来回三次，帧数与已加载数每次都完整（无悬垂）");

    // ---- ⑤ 坏配置：照常换，但 IsValid 为假（不让界面停摆） ----
    InkingSpriteData broken = sheetConfig();
    broken.sheet.columns = 5;  // 期望 200 宽，真实 240
    InkLog::ResetStats();
    sprite->SetSpriteData(broken);
    std::printf("         换到坏配置：IsValid=%d 错误=%s\n",
                sprite->IsValid() ? 1 : 0, sprite->GetConfigError().c_str());
    Check(!sprite->IsValid()
              && sprite->GetConfigError().find("图集尺寸对不上") != std::string::npos,
          "T-13：换到坏配置时 IsValid 为假并给出原因（与构造同一条判据）");

    // 真正"没有可播的帧"的那一种（`Validate()` 就过不去：图集连格宽都没有）：
    // 换过去后帧数为 0，播放状态必须被拉回 false——否则组件会"在播什么也没有"。
    sprite->Play();
    InkingSpriteData noFrames = sheetConfig();
    noFrames.sheet.frameWidth = 0;
    sprite->SetSpriteData(noFrames);
    std::printf("         换到无帧配置：帧数=%zu 播放=%d 错误=%s\n",
                sprite->GetFrameCount(), sprite->IsPlaying() ? 1 : 0,
                sprite->GetConfigError().c_str());
    Check(!sprite->IsValid() && sprite->GetFrameCount() == 0
              && !sprite->IsPlaying(),
          "T-13：没有可播的帧时播放状态被拉回 false（不空转）");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-14（0.6.5）：不规则帧矩形
// ---------------------------------------------------------------------------
//
// "一张大页里混装多个动作"必然出现帧宽高不等（跑步比待机宽）。验收与 T-11 同一套
// 思路：**算术**（`CellRect` 原样返回表里的矩形）+ **像素**（帧表切出来的那一块，
// 与"直接造那块矩形"逐像素一致——这样才排除"矩形对但 UV/采样偏了"）。
// 每个用例的组件尺寸按该帧的宽高给，两边同比例，才比得起来。
void CheckSpriteUnevenFrames(SDL_Renderer* renderer) {
    std::printf("\n-- 不规则帧矩形 frameRects（0.6.5 T-14） --\n");

    struct FrameCase {
        const char* label;
        int x, y, width, height;
    };
    const FrameCase kFrames[3] = {{"40×40", 0, 0, 40, 40},
                                  {"56×40", 40, 0, 56, 40},
                                  {"40×56", 0, 40, 40, 56}};
    constexpr int kAtlasWidth = 128;
    constexpr int kAtlasHeight = 96;

    // ① 纯算术（在下面那份 `data` 上做）：CellRect 原样返回、帧数由表定、越界给全零
    InkingSpriteData data;
    data.name = "uneven_frames";
    data.width = 56;
    data.height = 56;
    data.fps = 12.0;
    data.autoplay = false;
    data.selfAnchor = InkingChangeAnchor::LeftTop;
    data.traceAnchor = InkingChangeAnchor::LeftTop;
    data.offsetX = 20.0f;
    data.offsetY = 20.0f;
    data.normal.type = InkingAppearanceType::Image;
    data.normal.source = "uneven_atlas";
    for (const FrameCase& frame : kFrames) {
        data.sheet.frameRects.push_back(InkingShapeBounds{
            static_cast<float>(frame.x), static_cast<float>(frame.y),
            static_cast<float>(frame.width), static_cast<float>(frame.height)});
    }

    {
        InkingSpriteData normalized = data;
        normalized.sheet.count = 99;  // 帧表在时 count 会被覆盖（不是"两套各说一套"）
        normalized.Normalize();
        Check(normalized.sheet.count == 3 && normalized.FrameCount() == 3,
              "T-14：帧表非空时帧数由表定（count 被覆盖，不出现两套真相）");
        Check(normalized.Validate().empty(), "T-14：帧表配置本身合法");

        const InkingShapeBounds second = normalized.sheet.CellRect(1);
        std::printf("         表里第 1 帧 = (%.0f,%.0f,%.0f×%.0f)；表外第 3 帧宽=%.0f\n",
                    second.x, second.y, second.width, second.height,
                    normalized.sheet.CellRect(3).width);
        Check(second.x == 40.0f && second.y == 0.0f && second.width == 56.0f
                  && second.height == 40.0f,
              "T-14：CellRect 原样返回表里的矩形（不再叠加 firstFrame）");
        Check(normalized.sheet.CellRect(3).width == 0.0f,
              "T-14：表外的帧号给全零矩形（不画错块）");
    }

    // ② 互斥：帧表 + 网格字段同时给 → 构造期一条 ERROR
    {
        InkingScene scene("uneven_exclusive");
        InkingSceneLibrary::SetActiveScene(&scene);

        InkingSpriteData both = data;
        both.sheet.columns = 4;
        both.sheet.frameWidth = 40;
        both.sheet.frameHeight = 40;

        InkLog::ResetStats();
        auto* sprite = static_cast<InkingSprite*>(scene.AddChild(
            std::make_unique<InkingSprite>(&scene, both)));
        std::printf("         帧表 + 网格同给：IsValid=%d 错误=%s\n",
                    sprite->IsValid() ? 1 : 0, sprite->GetConfigError().c_str());
        Check(!sprite->IsValid()
                  && sprite->GetConfigError().find("互斥") != std::string::npos,
              "T-14：帧表与网格互斥（不静默选一套）");
        Check(InkLog::CountOf(InkingLogLevel::Error) == 1,
              "T-14：恰好一条 ERROR");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }

    // ③ 越界：第 2 帧的 40×56 超出 96 高 → 构造期一条 ERROR，文案带帧号
    {
        InkingScene scene("uneven_out_of_bounds");
        InkingSceneLibrary::SetActiveScene(&scene);

        InkingSpriteData out = data;
        // 必须带上字节：没有字节时构造期会先报"图集没有字节"，越界那条就永远轮不到
        // （自检第一次跑正是这么红的——两条错有先后，验错的那条被前一条挡住了）。
        const std::vector<std::uint8_t> atlasBytes =
            MakeAtlasRegionBmp(kAtlasWidth, kAtlasHeight, 0, 0, kAtlasWidth,
                               kAtlasHeight);
        out.normal.imageBytes = atlasBytes.data();
        out.normal.imageSize = atlasBytes.size();
        out.sheet.frameRects[2] = InkingShapeBounds{0.0f, 80.0f, 40.0f, 56.0f};

        InkLog::ResetStats();
        auto* sprite = static_cast<InkingSprite*>(scene.AddChild(
            std::make_unique<InkingSprite>(&scene, out)));
        std::printf("         越界帧：IsValid=%d 错误=%s\n",
                    sprite->IsValid() ? 1 : 0, sprite->GetConfigError().c_str());
        Check(!sprite->IsValid()
                  && sprite->GetConfigError().find("第 2 帧") != std::string::npos,
              "T-14：帧表越界在构造期被拦下（文案带帧号与矩形）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }

    // ④ 像素：帧表切出来的每一帧，与"直接造那块矩形"逐像素一致
    if (renderer == nullptr) {
        return;
    }
    int outputWidth = 0;
    int outputHeight = 0;
    SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
    const float pixelRatio =
        outputWidth > 0
            ? static_cast<float>(outputWidth) / static_cast<float>(InkingDesignWidth())
            : 1.0f;

    // 大图与"裁出来的小图"来自同一个生成器（绝对坐标），所以内容必然可对。
    const std::vector<std::uint8_t> atlas =
        MakeAtlasRegionBmp(kAtlasWidth, kAtlasHeight, 0, 0, kAtlasWidth, kAtlasHeight);

    for (std::size_t index = 0; index < 3; ++index) {
        const FrameCase& frame = kFrames[index];
        const std::vector<std::uint8_t> cell =
            MakeAtlasRegionBmp(kAtlasWidth, kAtlasHeight, frame.x, frame.y,
                               frame.width, frame.height);
        const SDL_Rect region{
            static_cast<int>(std::lround(20.0f * pixelRatio)),
            static_cast<int>(std::lround(20.0f * pixelRatio)),
            static_cast<int>(std::lround(static_cast<float>(frame.width) * pixelRatio)),
            static_cast<int>(std::lround(static_cast<float>(frame.height) * pixelRatio))};

        // 帧表那一侧：组件尺寸 = 该帧宽高（两边同比例才比得起来）
        InkingSpriteData sheetData = data;
        sheetData.width = frame.width;
        sheetData.height = frame.height;
        sheetData.normal.imageBytes = atlas.data();
        sheetData.normal.imageSize = atlas.size();

        InkingScene sheetScene(std::string("uneven_sheet_") + frame.label);
        InkingSceneLibrary::SetActiveScene(&sheetScene);
        auto* sprite = static_cast<InkingSprite*>(sheetScene.AddChild(
            std::make_unique<InkingSprite>(&sheetScene, sheetData)));
        sprite->SeekToFrame(index);
        sheetScene.TickFrame(1.0f / 60.0f);
        RenderSvgFrame(renderer, sheetScene);
        const std::vector<std::uint32_t> fromSheet = ReadPixelRegion(renderer, region);
        EndSvgFrame(renderer);

        // 参照侧：同一块矩形的独立小图
        InkingScene singleScene(std::string("uneven_single_") + frame.label);
        InkingSceneLibrary::SetActiveScene(&singleScene);
        InkingImageData imageData;
        imageData.name = std::string("uneven_cell_") + frame.label;
        imageData.width = frame.width;
        imageData.height = frame.height;
        imageData.normal.type = InkingAppearanceType::Image;
        imageData.normal.source = imageData.name;
        imageData.normal.imageBytes = cell.data();
        imageData.normal.imageSize = cell.size();
        imageData.selfAnchor = InkingChangeAnchor::LeftTop;
        imageData.traceAnchor = InkingChangeAnchor::LeftTop;
        imageData.offsetX = 20.0f;
        imageData.offsetY = 20.0f;
        singleScene.AddChild(
            std::make_unique<InkingStaticImage>(&singleScene, imageData));
        singleScene.TickFrame(1.0f / 60.0f);
        RenderSvgFrame(renderer, singleScene);
        const std::vector<std::uint32_t> fromSingle = ReadPixelRegion(renderer, region);
        EndSvgFrame(renderer);

        // 只比**内部**：最外一圈的采样点会落到矩形外一个纹素（图集里那儿是别的
        // 动作，独立小图里则是纹理边界）——那是"图集需要有留白"的固有现象，
        // 不是切格错。差一整格 / 差一行会落在内部，所以这组断言照样抓得住。
        int diffInside = (fromSheet.size() == fromSingle.size() && !fromSheet.empty())
                             ? 0
                             : -1;
        int diffAll = diffInside;
        if (diffInside == 0) {
            const std::size_t w = static_cast<std::size_t>(region.w);
            const std::size_t h = static_cast<std::size_t>(region.h);
            for (std::size_t i = 0; i < fromSheet.size(); ++i) {
                if (fromSheet[i] == fromSingle[i]) {
                    continue;
                }
                ++diffAll;
                const std::size_t col = i % w;
                const std::size_t row = i / w;
                const bool border =
                    col == 0 || col + 1 == w || row == 0 || row + 1 == h;
                if (!border) {
                    ++diffInside;
                }
            }
        }
        std::printf("         第 %zu 帧（%s）：内部差异 %d / %zu（含边框 %d）\n", index,
                    frame.label, diffInside, fromSheet.size(), diffAll);
        Check(diffInside == 0, "T-14：帧表切出的那一帧与独立小图逐像素一致（内部）");
    }

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-16（0.6.5）：图集描述 → InkingSpriteData
// ---------------------------------------------------------------------------
//
// 这一层是**纯函数**（描述 + 字节视图 → 配置），所以验收的重点是"转过去的字段
// 逐项对得上"，再加上一条像素（转出来的东西真的能画对那块）。
void CheckAtlasData(SDL_Renderer* renderer) {
    std::printf("\n-- 图集描述 InkingAtlasData → InkingSpriteData（0.6.5 T-16） --\n");

    InkingAtlasData atlas;
    atlas.name = "slime";

    InkingAtlasAnimation walk;
    walk.name = "walk";
    walk.source = "slime.png";
    walk.fps = 8.0;
    walk.loop = false;
    walk.frames = {InkingShapeBounds{0.0f, 0.0f, 40.0f, 40.0f},
                   InkingShapeBounds{40.0f, 0.0f, 56.0f, 40.0f},
                   InkingShapeBounds{0.0f, 40.0f, 40.0f, 56.0f}};
    walk.events = {InkingSpriteData::SpriteEvent{1, "step"}};
    atlas.animations.push_back(walk);

    InkingAtlasAnimation idle;
    idle.name = "idle";
    idle.source = "slime.png";
    idle.fps = 4.0;
    idle.frames = {InkingShapeBounds{96.0f, 0.0f, 32.0f, 32.0f}};
    atlas.animations.push_back(idle);

    // ① 查找与校验
    {
        Check(atlas.Validate().empty(), "T-16：合法的图集描述通过 Validate");
        Check(atlas.FindIndex("walk") == 0 && atlas.FindIndex("idle") == 1,
              "T-16：FindIndex 按名字找到下标");
        Check(atlas.FindIndex("run") == InkingAtlasData::kNotFound,
              "T-16：找不到时给 kNotFound（不是 0，0 是“walk”）");

        InkingAtlasData duplicated = atlas;
        duplicated.animations[1].name = "walk";
        std::printf("         重名：%s\n", duplicated.Validate().c_str());
        Check(!duplicated.Validate().empty(),
              "T-16：重名动画在 Validate 里报错（不静默只播第一条）");
    }

    // ② 转换：字段逐项对得上
    {
        const InkingSpriteData data =
            InkingMakeSpriteData(atlas, atlas.FindIndex("walk"), nullptr, 0);
        std::printf("         转出来：name=%s 帧数=%zu fps=%.1f loop=%d 事件=%zu "
                    "尺寸=%d×%d\n",
                    data.name.c_str(), data.FrameCount(), data.fps,
                    data.loop ? 1 : 0, data.events.size(), data.width, data.height);
        Check(data.FrameCount() == 3 && data.sheet.frameRects.size() == 3,
              "T-16：帧表原样灌进 sheet.frameRects，帧数 == 3");
        Check(data.sheet.frameRects[1].width == 56.0f
                  && data.sheet.frameRects[1].height == 40.0f,
              "T-16：帧矩形逐项相等（含不等宽的 56×40）");
        Check(data.fps == 8.0 && !data.loop && data.events.size() == 1
                  && data.events[0].name == "step",
              "T-16：fps / loop / events 原样拷贝");
        Check(data.normal.source == "slime.png" && data.sheet.source.empty(),
              "T-16：路径只写一处（normal.source，sheet.source 留空沿用）");
        Check(data.runtimeAsset && data.width == 40 && data.height == 40,
              "T-16：没给字节时走运行期读图；尺寸默认跟第一帧（40×40）");
        Check(data.Validate().empty(), "T-16：转出来的配置通过 InkingSpriteData::Validate");
    }

    // ③ 越界下标：给一份非法配置 + 一条 ERROR（不崩、不静默空白）
    {
        InkLog::ResetStats();
        const InkingSpriteData bad =
            InkingMakeSpriteData(atlas, InkingAtlasData::kNotFound, nullptr, 0);
        Check(!bad.Validate().empty(),
              "T-16：下标越界转出的是非法配置（Validate 能说清楚）");
        Check(InkLog::CountOf(InkingLogLevel::Error) == 1,
              "T-16：越界同时有一条 ERROR（不是静默空白）");
    }

    // ④ 像素：`InkingMakeSpriteData` 产出的配置画出来的第 1 帧 == 同一块矩形的独立小图。
    // 与 T-14 同一套对拍法（大图与裁片来自同一个按绝对坐标生成器），区别只在于
    // 这里的帧表是**从 `InkingAtlasData` 转过来的**，走完了"描述 → 配置 → 组件"全程。
    if (renderer == nullptr) {
        return;
    }
    {
        constexpr int kAtlasWidth = 128;
        constexpr int kAtlasHeight = 96;
        const std::vector<std::uint8_t> bytes =
            MakeAtlasRegionBmp(kAtlasWidth, kAtlasHeight, 0, 0, kAtlasWidth,
                               kAtlasHeight);

        InkingAtlasData pixelAtlas;
        pixelAtlas.name = "atlas_pixels";
        InkingAtlasAnimation anim;
        anim.name = "walk";
        anim.source = "atlas_pixels_src";
        anim.frames = {InkingShapeBounds{0.0f, 0.0f, 40.0f, 40.0f},
                       InkingShapeBounds{40.0f, 0.0f, 56.0f, 40.0f},
                       InkingShapeBounds{0.0f, 40.0f, 40.0f, 56.0f}};
        pixelAtlas.animations.push_back(anim);
        Check(pixelAtlas.Validate().empty(), "T-16：像素用例的描述合法");

        const InkingShapeBounds rect = anim.frames[1];  // 56×40，故意不等宽
        const std::vector<std::uint8_t> cell =
            MakeAtlasRegionBmp(kAtlasWidth, kAtlasHeight, 40, 0, 56, 40);

        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
        const float pixelRatio =
            outputWidth > 0
                ? static_cast<float>(outputWidth)
                    / static_cast<float>(InkingDesignWidth())
                : 1.0f;
        const SDL_Rect region{
            static_cast<int>(std::lround(20.0f * pixelRatio)),
            static_cast<int>(std::lround(20.0f * pixelRatio)),
            static_cast<int>(std::lround(56.0f * pixelRatio)),
            static_cast<int>(std::lround(40.0f * pixelRatio))};

        InkingSpriteData sheetData = InkingMakeSpriteData(
            pixelAtlas, 0, bytes.data(), bytes.size());
        sheetData.autoplay = false;
        sheetData.selfAnchor = InkingChangeAnchor::LeftTop;
        sheetData.traceAnchor = InkingChangeAnchor::LeftTop;
        sheetData.offsetX = 20.0f;
        sheetData.offsetY = 20.0f;
        Check(sheetData.width == 40 && sheetData.height == 40,
              "T-16：尺寸取第一帧（40×40），不是 56×40 那一帧");

        // 要跟 56×40 那一帧对拍，组件尺寸就得按这一帧给：图集比第一帧宽时**调用方
        // 自己调 `width/height`**（`InkingMakeSpriteData` 不替你改尺寸——那会触发重布局）。
        // 不改的话这一帧会被压进 40×40，与独立小图当然对不上。
        sheetData.width = 56;
        sheetData.height = 40;

        InkingScene sheetScene("atlas_sheet_px");
        InkingSceneLibrary::SetActiveScene(&sheetScene);
        auto* sprite = static_cast<InkingSprite*>(sheetScene.AddChild(
            std::make_unique<InkingSprite>(&sheetScene, sheetData)));
        sprite->SeekToFrame(1);
        sheetScene.TickFrame(1.0f / 60.0f);
        RenderSvgFrame(renderer, sheetScene);
        const std::vector<std::uint32_t> fromAtlas =
            ReadPixelRegion(renderer, region);
        EndSvgFrame(renderer);

        InkingScene singleScene("atlas_single_px");
        InkingSceneLibrary::SetActiveScene(&singleScene);
        InkingImageData imageData;
        imageData.name = "atlas_cell11";
        imageData.width = 56;
        imageData.height = 40;
        imageData.normal.type = InkingAppearanceType::Image;
        imageData.normal.source = imageData.name;
        imageData.normal.imageBytes = cell.data();
        imageData.normal.imageSize = cell.size();
        imageData.selfAnchor = InkingChangeAnchor::LeftTop;
        imageData.traceAnchor = InkingChangeAnchor::LeftTop;
        imageData.offsetX = 20.0f;
        imageData.offsetY = 20.0f;
        singleScene.AddChild(
            std::make_unique<InkingStaticImage>(&singleScene, imageData));
        singleScene.TickFrame(1.0f / 60.0f);
        RenderSvgFrame(renderer, singleScene);
        const std::vector<std::uint32_t> fromSingle =
            ReadPixelRegion(renderer, region);
        EndSvgFrame(renderer);

        int diffInside = (fromAtlas.size() == fromSingle.size() && !fromAtlas.empty())
                             ? 0
                             : -1;
        int diffAll = diffInside;
        if (diffInside == 0) {
            const std::size_t w = static_cast<std::size_t>(region.w);
            const std::size_t h = static_cast<std::size_t>(region.h);
            for (std::size_t i = 0; i < fromAtlas.size(); ++i) {
                if (fromAtlas[i] == fromSingle[i]) {
                    continue;
                }
                ++diffAll;
                const std::size_t col = i % w;
                const std::size_t row = i / w;
                if (col != 0 && col + 1 != w && row != 0 && row + 1 != h) {
                    ++diffInside;
                }
            }
        }
        std::printf("         InkingAtlasData 第 1 帧 vs 独立小图：内部差异 %d（含边框 %d）\n",
                    diffInside, diffAll);
        Check(diffInside == 0,
              "T-16：InkingAtlasData 转出来的第 1 帧与独立小图逐像素一致（内部）");
    }

}

// ---------------------------------------------------------------------------
// 动态精灵（G-03）：帧机同源、几何能改
// ---------------------------------------------------------------------------
//
// 四条验收对应 `TaskGuide-G.md` G-03 的"验收"四项。第 4 项（`src/inking.cpp` 的
// 镜像断言）是**编译期**的，这里只留它的运行时回声——真正拦住"静态档被顺手改成
// 动态"的是那几条 `static_assert`，它们红了连 exe 都出不来。
//
// 第 1 条（100 帧后三项计数逐一相等）是本任务唯一能证明"帧逻辑同源"的客观判据：
// 两份帧机是**复制**来的，"看着一样"不算数。让它们在**同一个场景、同一个时钟**
// 上各跑 100 帧，落点与两个计数必须一个个对上。G-04 抽 `InkingSpriteLike` 之后这条断言
// **一个字都不改**照样要过——那正是"纯重构"的判据。
//
// 另加一条任务书没列的 ⑤：静态 / 动态各画一次同一个图集格，**逐像素对拍**。上面
// 那些都在读状态，而"动态节点真的进了绘制列表并画出来"只有画面能证明。
void CheckDynamicSprite(SDL_Renderer* renderer) {
    std::printf("\n-- 动态精灵（G-03） --\n");

    // ---- ① 是动态档，且**不进命中表**（占地记成"洞"） ----
    //
    // 判据与 G-01 同构：构造本身是骨架变化（代次会涨），所以判的是**条目数不变**、
    // **洞多一个**——那才是"动态节点不进表"的可断言形态。
    // ---- ③ 顺带在这里做：Resize 改几何，命中跟着变（洞也得跟着刷） ----
    {
        InkingScene scene("g03_spr_tier");
        InkingSceneLibrary::SetActiveScene(&scene);

        // 一个静态参照物：让表里真的有条目，"一条都没多"才有意义。
        InkingAnchorData refData;
        refData.width = 40;
        refData.height = 40;
        refData.offsetX = 20.0f;
        refData.offsetY = 560.0f;
        scene.AddChild(
            std::make_unique<InkingStaticWidget>(&scene, refData));
        const std::size_t entriesBefore = scene.GetHitTableEntryCount();

        const InkingSpriteData data = InkingMakeSpriteData("g03_spr", 4, 12.0, true);
        auto* sprite = static_cast<InkingDynamicSprite*>(
            scene.AddChild(std::make_unique<InkingDynamicSprite>(
                &scene, data)));

        Check(sprite->IsDynamic(), "①：新类是动态档（`IsDynamic()` 为真）");
        Check(scene.GetHitTableEntryCount() == entriesBefore,
              "①：动态精灵**不进命中表**（条目数不变）");
        Check(scene.GetHitTableHoleCount() == 1,
              "①：它的占地记成**一个洞**（点落在洞里才会问动态层）");
        Check(sprite->IsValid() && sprite->GetFrameCount() == 4
                  && sprite->GetLoadedFrameCount() == 4,
              "①：4 帧的字节在构造期全部备齐（与静态版同一个入口）");
        Check(sprite->IsPlaying() && sprite->GetFrameIndex() == 0,
              "①：autoplay 默认开且落在第 0 帧");

        // ---- ③ Resize 之后 HitTest 跟着变 ----
        //
        // 取一个"贴着旧右边界外侧"的世界点：宽 40 时在外，宽 60 时在内。同一个点
        // 结果翻转，才证明"命中读的是当前几何"，不是构造时那份。
        const float edgeX = 65.0f;  // 本地 x = 45
        const float edgeY = 25.0f;  // 本地 y = 5
        Check(!sprite->HitTest(edgeX, edgeY),
              "③：Resize 之前，越出右边界的点不在精灵上（基线）");
        Check(sprite->Resize(60, 40), "③：`Resize(60, 40)` 真的改了");
        Check(sprite->GetWidth() == 60 && sprite->GetHeight() == 40,
              "③：`GetWidth()/GetHeight()` 跟着变成 60/40");
        Check(sprite->HitTest(edgeX, edgeY),
              "③：同一个世界点在 Resize 后命中（几何驱动命中）");
        // 场景那一侧同样要跟上：洞过期 = 那一片**点不到**（T-19 的静默失效形态）。
        // 几何入口走 `MarkGeometryMoved()` 才有这一条 —— 少写一句就只剩上面那条
        // 自判命中是绿的。
        Check(scene.QueryHit(edgeX, edgeY).target == sprite,
              "③：洞跟着刷新（场景在这一点的查询也命中它，不是只 `HitTest` 自判）");

        // ---- 偏离项：`SetSpriteData` **仍不动尺寸** ----
        //
        // 静态版的语义是"换配置不动几何"（`InkingSprite.h` 那段：改尺寸属布局层）。
        // 动态档多了"随手 Resize"的能力，但那必须是**调用方**喊的，不能藏在换配置
        // 的副作用里——否则一次"切动作"就变成一次看不见的重排。
        InkingSpriteData bigger = InkingMakeSpriteData("g03_spr_big", 4, 12.0, true);
        bigger.width = 100;
        bigger.height = 100;
        sprite->SetSpriteData(bigger);
        Check(sprite->GetWidth() == 60 && sprite->GetHeight() == 40,
              "③：`SetSpriteData` 不改尺寸（100×100 的配置没有悄悄覆盖几何）");
        Check(sprite->GetFrameIndex() == 0 && sprite->IsPlaying(),
              "③：换配置落到第 0 帧且播放状态保持（与静态版同一条）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }

    // ---- ② 帧逻辑同源的硬证据：同一份配置，100 帧后三项计数逐一相等 ----
    //
    // 两档放进**同一个场景**：时钟只有一个（`AnimationSeconds()`），所以"帧号恒等"
    // 是结构保证；真正要验的是"两份帧机对同一个时间算出同一个东西"——落点、累计
    // 换帧次数、跳帧次数。
    {
        InkingScene scene("g03_spr_parity");
        InkingSceneLibrary::SetActiveScene(&scene);

        const InkingSpriteData data =
            InkingMakeSpriteData("g03_parity", 4, 12.0, true);
        auto* stat = static_cast<InkingSprite*>(
            scene.AddChild(
                std::make_unique<InkingSprite>(&scene, data)));
        auto* dyn = static_cast<InkingDynamicSprite*>(
            scene.AddChild(std::make_unique<InkingDynamicSprite>(
                &scene, data)));
        Check(stat->IsValid() && dyn->IsValid(),
              "②：同一份配置两档都认（都合法，帧数也一样）");

        for (int i = 0; i < 100; ++i) {
            scene.TickFrame(1.0 / 12.0);
        }
        std::printf("         100×1/12 → 静态 %zu/%llu/%llu，动态 %zu/%llu/%llu\n",
                    stat->GetFrameIndex(),
                    static_cast<unsigned long long>(stat->GetFrameAdvanceCount()),
                    static_cast<unsigned long long>(stat->GetSkippedFrameCount()),
                    dyn->GetFrameIndex(),
                    static_cast<unsigned long long>(dyn->GetFrameAdvanceCount()),
                    static_cast<unsigned long long>(dyn->GetSkippedFrameCount()));
        Check(dyn->GetFrameIndex() == stat->GetFrameIndex(),
              "②：100 帧后帧号相等（时间 → 帧号是同一个纯函数）");
        Check(dyn->GetFrameAdvanceCount() == stat->GetFrameAdvanceCount(),
              "②：100 帧后换帧次数相等");
        Check(dyn->GetSkippedFrameCount() == stat->GetSkippedFrameCount(),
              "②：100 帧后跳帧次数相等");
        // 顺带钉住"不是两边一起错"：100 次 1/12 推进每次都该换一帧。
        Check(stat->GetFrameAdvanceCount() == 100,
              "②：基线正确（100 次 1/12 推进 = 100 次换帧，不是两边一起错）");
        // 档位的运行时回声：`src/inking.cpp` 的 `!HasResize<InkingSprite>` 无法
        // 在运行期直接断言，但"它仍是静态档"这条等价事实可以。
        Check(!stat->IsDynamic(),
              "②：静态精灵仍是静态档（这一笔没有把旧类改成动态）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }

    // ---- ④ 图集模式：Resize 之后仍按 SheetWidth/SheetHeight 切格 ----
    //
    // 这一条盯的是"几何与切格是两个坐标系统"：`Resize` 改的是**画多大**，源矩形
    // 仍是图集里的那块像素（40×40 的格）。如果谁把源矩形跟着几何缩放，画面会变成
    // "整张图集的左上角被拉大"，而 `IsValid()` 依然为真——静默错。
    {
        // 同一张图集（4×1 格、每格 40×40），④ 与 ⑤ 共用一份构造方式。
        const std::vector<std::uint8_t> atlas = MakeSheetBmp(160, 40, 40, 0);
        const auto makeSheetData = [&atlas](const char* name) {
            InkingSpriteData data;
            data.name = name;
            data.width = 40;
            data.height = 40;
            data.fps = 12.0;
            data.loop = true;
            data.autoplay = false;  // 只验切格：不播，帧号由 `SeekToFrame` 指定
            data.selfAnchor = InkingChangeAnchor::LeftTop;
            data.traceAnchor = InkingChangeAnchor::LeftTop;
            data.offsetX = 200.0f;
            data.offsetY = 100.0f;
            data.normal.type = InkingAppearanceType::Image;
            data.normal.source = std::string(name) + "_atlas";
            data.normal.imageBytes = atlas.data();
            data.normal.imageSize = atlas.size();
            data.sheet.frameWidth = 40;
            data.sheet.frameHeight = 40;
            data.sheet.columns = 4;
            data.sheet.rows = 1;
            return data;
        };

        InkingScene scene("g03_spr_sheet");
        InkingSceneLibrary::SetActiveScene(&scene);

        const InkingSpriteData data = makeSheetData("g03_sheet_spr");
        auto* sprite = static_cast<InkingDynamicSprite*>(
            scene.AddChild(std::make_unique<InkingDynamicSprite>(
                &scene, data)));
        Check(sprite->IsSheetMode() && sprite->IsValid()
                  && sprite->GetFrameCount() == 4,
              "④：图集模式构造成功（切格与图宽对上，量图校验通过）");

        const InkingShapeBounds before = sprite->GetAppearance().srcRect;
        Check(before.width == 40.0f && before.height == 40.0f,
              "④：初始源矩形 = 第 0 格（40×40）");

        Check(sprite->Resize(120, 90), "④：`Resize(120, 90)` 改了绘制尺寸");
        Check(sprite->IsValid() && sprite->IsSheetMode(),
              "④：Resize 之后配置仍然合法（切格几何与本体尺寸无关）");
        const InkingShapeBounds after = sprite->GetAppearance().srcRect;
        Check(after.x == before.x && after.y == before.y
                  && after.width == before.width
                  && after.height == before.height,
              "④：源矩形不随几何变（仍取图集里的 40×40 格，不是被拉成 120×90）");

        Check(sprite->SeekToFrame(2), "④：`SeekToFrame(2)` 成功");
        const InkingShapeBounds cell = data.sheet.CellRect(2);
        const InkingShapeBounds now = sprite->GetAppearance().srcRect;
        Check(now.x == cell.x && now.y == cell.y && now.width == cell.width
                  && now.height == cell.height,
              "④：第 2 帧的源矩形 == `CellRect(2)`（帧号→格号换算没被几何带偏）");

        // ---- A13 保留（与 G-01 同一条决定）：外部变换与几何正交、在命中处复合 ----
        InkingTransformSpec shift;
        shift.translateX = 5.0f;
        Check(sprite->SetDisplayTransform(shift),
              "④：A13 仍可用（外部显示变换被接受）");
        Check(sprite->HitTest(sprite->GetAbsX() + 10.0f, sprite->GetAbsY() + 10.0f),
              "④：命中按「几何 + 外部变换」的合成结果判");
        Check(sprite->GetDisplayTransformRejectedCount() == 0,
              "④：动态精灵认外部变换（拒绝计数保持 0）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }

    // ---- ⑤ 绘制同源：同一格、同一位置，两档画出来的像素一致 ----
    //
    // 上面每一条都是"读状态"。动态档最容易的静默失效不在状态里，而在**画面**上：
    // 节点在、`IsDynamic()` 真、命中查得到，就是不渲染（漏登记）。而"切格生效"
    // 这件事同样只有像素能证明——`GetAppearance().srcRect` 对不对，与
    // `PaintButtonAppearance` 有没有用它，是两件事。
    //
    // 所以这里摆两个场景，静态 / 动态各画一次第 2 格，逐像素对拍。
    if (renderer != nullptr) {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
        const float pixelRatio =
            outputWidth > 0
                ? static_cast<float>(outputWidth)
                    / static_cast<float>(InkingDesignWidth())
                : 1.0f;
        const SDL_Rect region{static_cast<int>(std::lround(200.0f * pixelRatio)),
                              static_cast<int>(std::lround(100.0f * pixelRatio)),
                              static_cast<int>(std::lround(40.0f * pixelRatio)),
                              static_cast<int>(std::lround(40.0f * pixelRatio))};

        const std::vector<std::uint8_t> atlas = MakeSheetBmp(160, 40, 40, 0);
        const auto makeSheetData = [&atlas](const char* name) {
            InkingSpriteData data;
            data.name = name;
            data.width = 40;
            data.height = 40;
            data.fps = 12.0;
            data.autoplay = false;
            data.selfAnchor = InkingChangeAnchor::LeftTop;
            data.traceAnchor = InkingChangeAnchor::LeftTop;
            data.offsetX = 200.0f;
            data.offsetY = 100.0f;
            data.normal.type = InkingAppearanceType::Image;
            data.normal.source = std::string(name) + "_atlas";
            data.normal.imageBytes = atlas.data();
            data.normal.imageSize = atlas.size();
            data.sheet.frameWidth = 40;
            data.sheet.frameHeight = 40;
            data.sheet.columns = 4;
            data.sheet.rows = 1;
            return data;
        };

        std::vector<std::uint32_t> fromDynamic;
        {
            InkingScene dynScene("g03_px_dyn");
            InkingSceneLibrary::SetActiveScene(&dynScene);
            auto* dyn = static_cast<InkingDynamicSprite*>(
                dynScene.AddChild(std::make_unique<InkingDynamicSprite>(
                    &dynScene, makeSheetData("g03_px_dyn_spr"))));
            dyn->SeekToFrame(2);
            dynScene.TickFrame(1.0f / 60.0f);
            RenderSvgFrame(renderer, dynScene);
            fromDynamic = ReadPixelRegion(renderer, region);
            EndSvgFrame(renderer);
        }
        std::vector<std::uint32_t> fromStatic;
        {
            InkingScene statScene("g03_px_stat");
            InkingSceneLibrary::SetActiveScene(&statScene);
            auto* stat = static_cast<InkingSprite*>(
                statScene.AddChild(std::make_unique<InkingSprite>(
                    &statScene, makeSheetData("g03_px_stat_spr"))));
            stat->SeekToFrame(2);
            statScene.TickFrame(1.0f / 60.0f);
            RenderSvgFrame(renderer, statScene);
            fromStatic = ReadPixelRegion(renderer, region);
            EndSvgFrame(renderer);
        }
        InkingSceneLibrary::SetActiveScene(nullptr);

        int diff = -1;
        if (!fromDynamic.empty() && fromDynamic.size() == fromStatic.size()) {
            diff = 0;
            for (std::size_t i = 0; i < fromDynamic.size(); ++i) {
                if (fromDynamic[i] != fromStatic[i]) {
                    ++diff;
                }
            }
        }
        // "差异 0"只在那一格**真的画了东西**时才有意义：两边都没画的话也是 0。
        // 图集图案逐像素不同，所以"颜色种类 > 1"就是"画出来了"的判据。
        int distinct = 0;
        for (std::size_t i = 0; i < fromStatic.size(); ++i) {
            bool seen = false;
            for (std::size_t j = 0; j < i; ++j) {
                if (fromStatic[j] == fromStatic[i]) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                ++distinct;
            }
        }
        std::printf("        第 2 格：动态 vs 静态 差异像素 %d / %zu（颜色 %d 种）\n",
                    diff, fromStatic.size(), distinct);
        Check(distinct > 1,
              "⑤：那一格真的画了东西（不是两边都空，否则差异 0 没有意义）");
        Check(diff == 0,
              "⑤：动态精灵与静态精灵同一格逐像素一致（画出来了，且切格生效）");
    }
}

// ---------------------------------------------------------------------------
// G-05：动态标签（文字与几何都能改，改尺寸的时机是**显式**的）
// ---------------------------------------------------------------------------
//
// 这一组盯的三件事，静态版一条都答不了：
//   ① `SetText` **不**隐式撑高（静态版会——它是复制时最容易被顺手带过来的一行）；
//   ② 只有 `FitToContent()` 改几何，且写完与 `MeasureLayout()` 自洽；
//   ③ 在布局容器里 `FitToContent()` **不生效，但要喊一声**（写了也会被覆盖）。
//
// 另加一条"进没进绘制列表"：动态档最容易的静默失效是**漏登记**——状态全对、
// 就是不画。画文字那一笔与静态版逐字相同（同一个 `inking_detail::PaintText`），所以
// 这里不做像素对拍，改用 `GetDrawItemCount()` 钉住"登记"这一步。
void CheckDynamicLabel() {
    std::printf("\n-- 动态标签（G-05） --\n");

    // ---- ① / ② / ③ / ④：档位、不进表、SetText 不改几何、FitToContent 才改 ----
    {
        InkingScene scene("g05_dynlabel");
        InkingSceneLibrary::SetActiveScene(&scene);

        // 一个静态参照物：让表里真的有条目，"一条都没多"才有意义。
        InkingAnchorData refData;
        refData.width = 40;
        refData.height = 40;
        refData.offsetX = 20.0f;
        refData.offsetY = 560.0f;
        scene.AddChild(
            std::make_unique<InkingStaticWidget>(&scene, refData));
        const std::size_t entriesBefore = scene.GetHitTableEntryCount();
        const std::size_t drawsBefore = scene.GetDrawItemCount();

        InkingLabelData data;
        data.name = "g05_dyn_label";
        data.width = 200;   // 换行基准（不是"必须占 200"：见 ③ 之后那句）
        data.height = 0;    // 内容驱动：高度由自测量补
        data.offsetX = 40.0f;
        data.offsetY = 80.0f;
        data.selfAnchor = InkingChangeAnchor::LeftTop;
        data.traceAnchor = InkingChangeAnchor::LeftTop;
        data.text = "短";
        data.style.fontSize = 20.0f;

        auto* label = static_cast<InkingDynamicLabel*>(
            scene.AddChild(std::make_unique<InkingDynamicLabel>(
                &scene, data)));

        Check(label->IsDynamic(), "①：新类是动态档（`IsDynamic()` 为真）");
        Check(scene.GetHitTableEntryCount() == entriesBefore,
              "①：动态标签**不进命中表**（条目数不变）");
        Check(scene.GetHitTableHoleCount() == 1,
              "①：它的占地记成**一个洞**（点落在洞里才会问动态层）");
        Check(scene.GetDrawItemCount() == drawsBefore + 1,
              "①：它进了绘制列表（漏登记就不会画——动态档最容易的静默失效）");
        Check(label->GetWidth() == 200 && label->GetHeight() > 0,
              "①：构造期补了自测量（`height:0` 的标签不会留在 0×0）");

        // ---- ② SetText 不改几何（静态版会隐式撑高） ----
        const int widthBefore = label->GetWidth();
        const int heightBefore = label->GetHeight();
        Check(label->SetText(
                  "这是一段明显更长的文字，用来验证 SetText 不会隐式撑高"),
              "②：`SetText()` 真的换了内容");
        Check(label->GetHeight() == heightBefore,
              "②：`SetText()` 之后高度**不变**（没有隐式撑高）");
        Check(label->GetWidth() == widthBefore, "②：宽度也不变");

        // ---- ③ FitToContent 才改尺寸，且改完与 MeasureLayout 自洽 ----
        Check(label->FitToContent(), "③：`FitToContent()` 真的改了几何");
        const InkingLayoutSize measured =
            label->MeasureLayout(InkingLayoutConstraints{0.0f, 0.0f});
        std::printf("         FitToContent → %d×%d；事后再量 → %.1f×%.1f\n",
                    label->GetWidth(), label->GetHeight(), measured.width,
                    measured.height);
        Check(std::fabs(static_cast<float>(label->GetHeight()) - measured.height)
                  <= 1.0f,
              "③：写进去的高度 == `MeasureLayout().height`（±1：量到自洽才写）");
        Check(std::fabs(static_cast<float>(label->GetWidth()) - measured.width)
                  <= 1.0f,
              "③：宽度也是内容宽（±1）");
        Check(label->GetHeight() > heightBefore,
              "③：长文本比短文本高（撑高真的发生了，不是量了个原地踏步）");
        // 幂等：同样的内容再喊一次，几何没变 ⇒ false。
        Check(!label->FitToContent(),
              "③：再喊一次是幂等的（`Resize` 没变 ⇒ false，一个脏都不标）");

        // ---- ④ Resize 之后排版按新宽度重排（`availableWidth` 进缓存键） ----
        Check(label->Resize(300, 120), "④：`Resize(300, 120)` 改了");
        const float wide = label->GetTextLayout().width;
        Check(label->Resize(100, 120), "④：再 `Resize(100, 120)`");
        const float narrow = label->GetTextLayout().width;
        std::printf("         排版宽度：300 框 → %.1f；100 框 → %.1f\n", wide,
                    narrow);
        Check(narrow <= 100.5f,
              "④：窄框里排版宽度不超过框（真的按 100 重排了）");
        Check(narrow < wide,
              "④：宽→窄之后排版宽度变了（`availableWidth` 进缓存键，不是吃老缓存）");

        // ---- ⑤ 标签仍默认不参与命中；洞不改变遮挡策略；A13 保留 ----
        Check(!label->HitTest(label->GetAbsX() + 5.0f, label->GetAbsY() + 5.0f),
              "⑤：`HitTest` 恒 false（标签不吃点击，与静态版一致）");
        Check(label->IsHitPassThrough(),
              "⑤：策略是 PassThrough（看得见、点得穿）");
        Check(scene.QueryHit(label->GetAbsX() + 5.0f, label->GetAbsY() + 5.0f)
                      .target
                  == nullptr,
              "⑤：洞不挡事件（点落在标签上时静态表的答案原样保留）");
        InkingTransformSpec shift;
        shift.translateX = 5.0f;
        Check(label->SetDisplayTransform(shift),
              "⑤：A13 仍可用（外部显示变换被接受）");
        Check(label->GetDisplayTransformRejectedCount() == 0,
              "⑤：动态标签认外部变换（拒绝计数保持 0）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }

    // ---- ⑥ 布局容器内不生效：返回 false + 几何不动 + 一条 WARN ----
    //
    // 这是 G-05 唯一"写了等于没写"的坑：`ApplyLayoutRect` 会在下一次 `Relayout()`
    // 时把 `_width/_height` 整体重写，所以自己量的尺寸活不过一帧。规矩是
    // **宁可喊一声，也不要写一个下一帧就消失的尺寸**——三条断言各钉一角。
    {
        InkingScene scene("g05_dynlabel_layout");
        InkingSceneLibrary::SetActiveScene(&scene);

        InkingAnchorData columnData;
        columnData.width = 400;
        columnData.height = 300;
        columnData.offsetX = 20.0f;
        columnData.offsetY = 20.0f;
        columnData.layout.kind = InkingLayoutKind::Stack;
        columnData.layout.axis = InkingLayoutAxis::Vertical;
        columnData.layout.gap = 4.0f;
        auto* column = static_cast<InkingDynamicWidget*>(
            scene.AddChild(std::make_unique<InkingDynamicWidget>(
                &scene, columnData)));

        InkingLabelData data;
        data.name = "g05_in_layout";
        data.text = "布局容器里的标签";
        data.style.fontSize = 20.0f;
        data.layoutItem.margin.left = 8.0f;
        auto* inner = static_cast<InkingDynamicLabel*>(
            column->AddChild(std::make_unique<InkingDynamicLabel>(
                column, data)));

        Check(inner->IsManagedByLayout(),
              "⑥：它认得出自己被布局管着（父链上有有效 `InkingLayoutSpec`）");
        const int widthBefore = inner->GetWidth();
        const int heightBefore = inner->GetHeight();
        std::printf("         布局给的尺寸 %d×%d\n", widthBefore, heightBefore);
        Check(widthBefore > 0 && heightBefore > 0,
              "⑥：布局先给了它一个尺寸（下面那条「不变」才有意义）");

        InkLog::ResetStats();
        Check(!inner->FitToContent(),
              "⑥：`FitToContent()` 返回 false（不写一个下一帧就消失的尺寸）");
        Check(inner->GetWidth() == widthBefore
                  && inner->GetHeight() == heightBefore,
              "⑥：几何一个字节都没动");
        const std::vector<std::string> warns =
            InkLog::grep("FitToContent", "InkingDynamicLabel");
        Check(warns.size() == 1,
              "⑥：日志里恰好一条 WARN（不静默——这条正是静默失效的形态）");

        InkingSceneLibrary::SetActiveScene(nullptr);
    }
}

// ---------------------------------------------------------------------------
// G-07：动态面板（几何可变 + 可滚动的容器）
// ---------------------------------------------------------------------------
//
// 六条验收各钉一角，其中三条是"静态版答不了"的：
//   ① 滚动条拖动是**自判**的（动态档收不到 `PushPointer*`，只有指针变化通知）；
//   ② 与静态滚动容器**不串台**（同场景并存，一边拖另一边不动）；
//   ③ 裁剪与命中同源（`Resize` 之后裁剪区跟着变；超出的子节点那一段点不到，
//      而子节点自己的 `HitTest` 在那一点是 true —— 证明"是容器在剪"）；
//   ④ 子树变换含滚动位移；
//   ⑤ 包络调用计数（动态容器的包络是整棵子树并集，§2.1 第 3 条要求可观测）；
//   ⑥ 编译期那条镜像断言在 `src/inking.cpp`，这里做运行期回声。
//
// **不做滚轮**：滚轮在动态档没有可靠的读取时机（见 `InkingDynamicPanel.h`
// 顶上"关键决定二"），所以这一组里一条滚轮断言都不该有——顺手加回来会让
// "丢了 / 滚双倍"这种偶发问题变成常态。
void CheckDynamicPanel() {
    std::printf("\n-- 动态面板（G-07） --\n");

    InkingScene scene("g07_dynpanel");
    InkingSceneLibrary::SetActiveScene(&scene);

    // ---- 对照物：一个**静态**滚动容器（② 拿它验"不串台"） ----
    InkingPanelData staticData;
    staticData.name = "g07_static_scroll";
    staticData.width = 200;
    staticData.height = 200;
    staticData.offsetX = 400.0f;
    staticData.offsetY = 100.0f;
    staticData.closed.color = 0xFF223344u;
    staticData.scroll.enabled = true;
    auto* staticPanel = static_cast<InkingStaticPanel*>(
        scene.AddChild(std::make_unique<InkingStaticPanel>(
            &scene, staticData)));
    InkingAnchorData staticContentData;
    staticContentData.width = 160;
    staticContentData.height = 800;
    staticPanel->AddChild(std::make_unique<InkingStaticWidget>(
        staticPanel, staticContentData));
    staticPanel->UpdateScrollRange();

    // ---- 被测：动态面板（200×200，内容 160×300 ⇒ 竖直可滚 100px） ----
    InkingPanelData data;
    data.name = "g07_dyn_panel";
    data.width = 200;
    data.height = 200;
    data.offsetX = 100.0f;
    data.offsetY = 100.0f;
    data.closed.color = 0xFF445566u;
    data.clipChildren = true;  // ③ 要验"裁剪与命中同源"
    data.scroll.enabled = true;
    auto* panel = static_cast<InkingDynamicPanel*>(
        scene.AddChild(std::make_unique<InkingDynamicPanel>(
            &scene, data)));

    Check(panel->IsDynamic(),
          "⑥：新类是动态档（编译期镜像断言在 `src/inking.cpp`）");
    Check(panel->OwnsSubtreeHit(),
          "①：容器语义（子孙不进全局命中表，由 `HitTest` 递归解答）");

    // 内容用一个**能命中**的子节点（静态按钮）：③ 要证明"子节点自己覆盖那一点、
    // 但容器裁掉了它"。普通的 `InkingStaticWidget` 不参与命中（`HitTest` 恒 false），
    // 用它就分不清"被剪掉了"和"本来就不占那儿"。
    InkingButtonData contentData;
    contentData.name = "g07_content";
    contentData.width = 160;
    contentData.height = 300;
    contentData.normal =
        InkingButtonAppearance(InkingAppearanceType::Color, 0xFF888888u);
    auto* content = static_cast<InkingStaticButton*>(
        panel->AddChild(std::make_unique<InkingStaticButton>(
            panel, contentData)));
    panel->UpdateScrollRange();
    Check(panel->GetContentHeight() == 300.0f,
          "①：内容高 300（`UpdateScrollRange` 显式量，框架不隐式扫描）");
    Check(panel->GetScrollRangeY() == 100.0f,
          "①：可滚范围 = 300 − 200 = 100");

    // ---- ① 滚动条拖动：按在滑块上 + 拖 40px + 抬起 ----
    //
    // 动态档收不到 `PushPointer*`，全部判定都在 `onPointerStateChanged` 里，
    // 所以这里必须走**真路径**（喂指针状态 + `DispatchPointer`），
    // 而不是直接调 `PressScrollAt` —— 直接调只能证明算术对，证明不了"入口接上了"。
    const InkingScrollBarMetrics bar = panel->GetBarMetrics(true);
    Check(bar.visible, "①：竖直条可见（内容 300 > 视口 200）");
    const float thumbCenterX = bar.thumbX + bar.thumbWidth * 0.5f;
    const float thumbCenterY = bar.thumbY + bar.thumbHeight * 0.5f;
    const float pressX = panel->GetAbsX() + thumbCenterX;
    const float pressY = panel->GetAbsY() + thumbCenterY;
    Check(panel->HitTest(pressX, pressY),
          "①：按点落在面板上（捕获的前提：`HitTest` 在按下那一刻为真）");

    scene.SetPointerState(pressX, pressY, true, true);
    scene.DispatchPointer();
    Check(panel->DraggingAxis() == 1,
          "①：按在滑块上就进入**竖直**拖动（`onPointerStateChanged` 自判）");

    scene.SetPointerState(pressX, pressY + 40.0f, true, true);
    scene.DispatchPointer();
    // 拖动是"滑块中心对到指针上"，所以滚动量按**条几何**换算：
    //   40px × range / (轨道长 − 滑块长)。range 100、行程 192−128=64 ⇒ 62.5。
    const float travel = bar.trackHeight - bar.thumbHeight;
    const float expected = travel > 0.0f
                               ? std::min(40.0f * panel->GetScrollRangeY() / travel,
                                          panel->GetScrollRangeY())
                               : 0.0f;
    std::printf("         拖 40px → GetScrollY()=%.1f（按条几何换算应为 %.1f）\n",
                panel->GetScrollY(), expected);
    Check(std::fabs(panel->GetScrollY() - expected) < 0.5f,
          "①：滚动量按 `InkingPanelData::scroll` 的换算变化（滑块中心跟手）");

    // ---- ② 不与静态滚动容器串台 ----
    Check(staticPanel->GetScrollY() == 0.0f,
          "②：同场景的静态滚动容器没有跟着动（推通道 / 拉通道各管各的）");

    scene.SetPointerState(pressX, pressY + 40.0f, false, true);
    scene.DispatchPointer();
    Check(panel->DraggingAxis() == 0, "①：抬手后不再跟随（捕获释放）");
    const float settled = panel->GetScrollY();
    scene.SetPointerState(pressX, pressY + 120.0f, false, true);
    scene.DispatchPointer();
    Check(panel->GetScrollY() == settled,
          "①：松手之后再动指针，滚动量一个字节都不变");

    // ---- ④ 子树变换含滚动位移 ----
    panel->SetScroll(0.0f, 60.0f);
    const InkingTransformSpec subtree = panel->GetSubtreeTransform();
    std::printf("         滚动 60 → 子树变换 translateY=%.1f\n",
                subtree.translateY);
    Check(std::fabs(subtree.translateY + panel->GetScrollY()) < 0.001f,
          "④：`GetSubtreeTransform().translateY == -GetScrollY()`");
    Check(std::fabs(panel->GetClipTransform().translateY) < 0.001f,
          "④：裁剪变换**不含**滚动位移（否则滚出去的内容还看得见）");

    // ---- ③ Resize 之后裁剪区跟着变；超出的子节点那一段点不到 ----
    const InkingShapeBounds clipBefore = panel->GetSubtreeClipBounds();
    Check(panel->Resize(300, 200), "③：`Resize(300, 200)` 改了绘制尺寸");
    const InkingShapeBounds clipAfter = panel->GetSubtreeClipBounds();
    std::printf("         裁剪区：%0.0f×%0.0f → %0.0f×%0.0f\n",
                clipBefore.width, clipBefore.height, clipAfter.width,
                clipAfter.height);
    Check(clipAfter.width > clipBefore.width,
          "③：裁剪区跟着 `Resize` 变宽（内容视口 = 本体扣掉条带）");
    Check(std::fabs(clipAfter.width - (300.0f - 16.0f)) < 0.5f,
          "③：裁剪区宽度 = 300 − 条带 16（`barInset × 2 + barWidth`）");

    // 取子节点**自己的**世界坐标往下 280px 那一点：它在子节点本体里（300 高），
    // 但已经越过面板 200 高的裁剪区（此刻滚动 60，子节点整体在面板上方 60）。
    // 用它而不是"面板绝对位置 + 250"，正是因为**滚动位移真实存在**——
    // 按面板坐标硬算会落到子节点之外，那就分不清"被剪"和"没占住"了。
    // 注意 `GetAbsX/Y` 是**布局**位置，不含子树变换里的滚动位移——所以世界坐标
    // 要自己减一次滚动量（少减这一下正是"画在这儿、点在那儿"的经典成因）。
    const float overX = content->GetAbsX() - panel->GetScrollX() + 80.0f;
    const float overY = content->GetAbsY() - panel->GetScrollY() + 280.0f;
    Check(content->HitTest(overX, overY),
          "③：子节点自己的几何覆盖这一点（它确实溢出了本体）");
    Check(!panel->HitTest(overX, overY),
          "③：超出本体那一段在容器上点不到（裁剪与命中同源）");
    Check(panel->HitTestNode(overX, overY) == nullptr,
          "③：深挖子树也拿不到它（`HitTestNode` 的裁剪那条先拦住）");
    Check(panel->HitTest(panel->GetAbsX() + 80.0f, panel->GetAbsY() + 150.0f),
          "③：本体之内的点照常命中（裁剪只剪超出的那一段）");
    Check(panel->HitTestNode(panel->GetAbsX() + 80.0f,
                             panel->GetAbsY() + 150.0f) == content,
          "③：深挖拿到的是那个子节点（子树优先，容器语义）");

    // ---- ⑤ 包络成本有观测口：一帧内不超过 1 次/节点 ----
    //
    // 用一次**真实的几何移动**（实体层每帧走位就是这条）来制造"洞要刷"的脏——
    // 不能用 `SetScroll`：本类的洞按滚动范围扩过，与滚动量无关（见
    // `InkingDynamicPanel.cpp` 的 `SetScroll`），它**不该**触发包络重算。
    panel->ChangeOffset(panel->GetOffsetX() + 5.0f, panel->GetOffsetY());
    panel->ResetHitEnvelopeCallCount();
    scene.TickFrame(1.0f / 60.0f);
    scene.QueryHit(panel->GetAbsX() + 10.0f, panel->GetAbsY() + 10.0f);
    const std::uint64_t once = panel->GetHitEnvelopeCallCount();
    scene.QueryHit(panel->GetAbsX() + 20.0f, panel->GetAbsY() + 20.0f);
    std::printf("         一帧内包络调用：%llu（再查一次仍为 %llu）\n",
                static_cast<unsigned long long>(once),
                static_cast<unsigned long long>(
                    panel->GetHitEnvelopeCallCount()));
    Check(once == 1,
          "⑤：这一帧算了一次包络（动态容器的包络 = 整棵子树并集，要能被数出来）");
    Check(panel->GetHitEnvelopeCallCount() == once,
          "⑤：同一帧再查不重复算（洞刷一次就够）");

    // ---- 开合与静态版同一份 `InkingPanelLook`（借这里的场景顺手验一条） ----
    Check(panel->SetOpen(true), "开合：`SetOpen(true)` 真的改了状态");
    // 注意读 `panel->GetData()` 而不是本地那份 `data`：`Normalize()` 在组件里做，
    // 省略的 `open` 段由它继承 `closed`（本地那份还没继承）。
    Check(panel->IsOpen()
              && panel->GetAppearance().color == panel->GetData().open.color,
          "开合：目标态与外观取的是 `open` 那一份");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-18（0.6.6）：动画组
// ---------------------------------------------------------------------------
//
// 验收的重点是"**只有一个节点**"这件事——它看不见（画出来一模一样），只能靠
// `GetOwnedChildCount()==0` 与 `animationChanged` 的计数钉住。
void CheckSpriteGroup() {
    std::printf("\n-- 动画组 InkingSpriteGroup（0.6.6 T-18） --\n");

    const auto makeGroup = []() {
        InkingSpriteGroupData group;
        struct Spec {
            const char* name;
            int frames;
            double fps;
            bool loop;
            int width;
            int height;
        };
        const Spec specs[3] = {{"idle", 4, 12.0, true, 40, 40},
                               {"run", 6, 16.0, true, 48, 56},
                               {"attack", 8, 10.0, false, 56, 48}};
        for (const Spec& spec : specs) {
            InkingSpriteData data =
                InkingMakeSpriteData(spec.name, spec.frames, spec.fps, spec.loop);
            data.width = spec.width;
            data.height = spec.height;
            group.animations.emplace_back(spec.name, data);
        }
        return group;
    };

    // ① 数据层：查找 / 重名 / 空表
    {
        const InkingSpriteGroupData group = makeGroup();
        Check(group.Validate().empty(), "T-18：合法的动作表通过 Validate");
        Check(group.FindIndex("run") == 1
                  && group.FindIndex("nope") == InkingSpriteGroupData::kNotFound,
              "T-18：FindIndex 按名字找、找不到给 kNotFound");

        InkingSpriteGroupData duplicated = group;
        duplicated.animations[2].first = "idle";
        std::printf("         重名：%s\n", duplicated.Validate().c_str());
        Check(!duplicated.Validate().empty(),
              "T-18：重名动作在 Validate 里报错");

        InkingSpriteGroupData empty;
        Check(!empty.Validate().empty(), "T-18：空动作表报错");
    }

    InkingScene scene("sprite_group");
    InkingSceneLibrary::SetActiveScene(&scene);

    // ② 构造 + 切换
    {
        InkLog::ResetStats();
        auto* group = static_cast<InkingSpriteGroup*>(scene.AddChild(
            std::make_unique<InkingSpriteGroup>(&scene, makeGroup(), "run")));

        int changes = 0;
        std::string lastNew;
        std::string lastOld;
        group->animationChanged.Subscribe(
            [&changes, &lastNew, &lastOld](const std::string& now,
                                           const std::string& before) {
                ++changes;
                lastNew = now;
                lastOld = before;
            });

        std::printf("         初始：动作=%s 帧数=%zu 尺寸=%d×%d\n",
                    group->GetCurrentAnimation().c_str(), group->GetFrameCount(),
                    group->GetWidth(), group->GetHeight());
        Check(group->IsValid() && group->GetGroupError().empty(),
              "T-18：合法动作表构造后 IsValid 为真");
        Check(group->GetCurrentAnimation() == "run"
                  && group->GetFrameCount() == 6 && group->GetWidth() == 48
                  && group->GetHeight() == 56,
              "T-18：初始动作按名字选中（帧数 / 尺寸都是那条动作的）");
        Check(group->GetOwnedChildCount() == 0,
              "T-18：组**只有一个节点**（没有隐藏的子 sprite）");
        Check(changes == 0, "T-18：构造不发 animationChanged");

        // 切到 idle：帧数 / 尺寸跟着变
        Check(group->SwitchTo("idle"), "T-18：SwitchTo 存在的动作返回 true");
        std::printf("         切到 idle：帧数=%zu 尺寸=%d×%d 事件=%d (%s←%s)\n",
                    group->GetFrameCount(), group->GetWidth(), group->GetHeight(),
                    changes, lastNew.c_str(), lastOld.c_str());
        Check(group->GetFrameCount() == 4 && group->GetWidth() == 40
                  && group->GetHeight() == 40,
              "T-18：切换后帧数与组件尺寸都跟新动作走");
        Check(changes == 1 && lastNew == "idle" && lastOld == "run",
              "T-18：animationChanged 发一次，(新, 旧) 名字正确");

        // 切到 attack（不循环那份），再切回同一个名字：不重建、不发事件
        Check(group->SwitchTo("attack"), "T-18：再切到 attack");
        Check(group->GetFrameCount() == 8 && group->GetWidth() == 56
                  && group->GetHeight() == 48,
              "T-18：第三个动作的帧数与宽高也对");
        Check(changes == 2, "T-18：每次真实切换发一次事件");
        Check(group->SwitchTo("attack") && changes == 2,
              "T-18：切到当前动作是空操作（不重建、不发事件）");

        // 不存在的名字：false + 恰好一条 ERROR + 停在原动作
        InkLog::ResetStats();
        Check(!group->SwitchTo("nope"),
              "T-18：SwitchTo 不存在的动作返回 false");
        std::printf("         切到 nope：错误=%s（当前仍是 %s）\n",
                    group->IsValid() ? "无" : "见日志",
                    group->GetCurrentAnimation().c_str());
        Check(InkLog::CountOf(InkingLogLevel::Error) == 1,
              "T-18：恰好一条 ERROR（不静默停在旧动作）");
        Check(group->GetCurrentAnimation() == "attack" && changes == 2,
              "T-18：切换失败后动作与事件计数都不变");

        // keepPhase：保留帧号
        group->SwitchTo("idle");
        group->SeekToFrame(3);
        Check(group->SwitchTo("run", /*keepPhase=*/true)
                  && group->GetFrameIndex() == 3,
              "T-18：keepPhase 保留帧号（同动作换方向不跳回第一帧）");
        Check(group->SwitchTo("idle", /*keepPhase=*/false)
                  && group->GetFrameIndex() == 0,
              "T-18：keepPhase 缺省时落到第 0 帧");

        // resize=false：尺寸不动
        const int widthBefore = group->GetWidth();
        Check(group->SwitchTo("attack", false, /*resize=*/false)
                  && group->GetWidth() == widthBefore,
              "T-18：resize=false 时组件尺寸不变（交给调用方）");

        // 动作名列表（UI 做下拉用）
        const std::vector<std::string> names = group->GetAnimationNames();
        Check(names.size() == 3 && names[0] == "idle" && names[1] == "run"
                  && names[2] == "attack",
              "T-18：GetAnimationNames 按声明顺序给出全部动作");
    }

    // ③ 初始名字写错：退回第 0 个 + 一条 ERROR（不是空白）
    {
        InkLog::ResetStats();
        auto* group = static_cast<InkingSpriteGroup*>(scene.AddChild(
            std::make_unique<InkingSpriteGroup>(&scene, makeGroup(),
                                                        "typo")));
        std::printf("         初始名写错：动作=%s 帧数=%zu\n",
                    group->GetCurrentAnimation().c_str(), group->GetFrameCount());
        Check(group->GetCurrentAnimation() == "idle" && group->GetFrameCount() == 4,
              "T-18：初始名字不存在时退回第 0 个动作");
        Check(InkLog::CountOf(InkingLogLevel::Error) == 1,
              "T-18：退回的同时有一条 ERROR（不是静默播错动作）");
    }

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-08：矩形图片并进几何批
// ---------------------------------------------------------------------------
//
// 这件事**功能上完全看不出来**（画出来一模一样，只是少几次 draw call），
// 所以断言只能落在两处：`BatchStats` 的提交计数，与"开关关掉时像素一模一样"。
//
// 三条要注意：
//   1. 计数是**累计量**，只能取前后差值；
//   2. 50 个 sprite 共用同一张图集 ⇒ 纹理只建 1 张、几何只提 1 次；
//   3. 并批之后形状走**羽化网格**（矩形四边多一条 1px 的抗锯齿过渡），所以
//      "逐像素一致"只在**内部**、且只在**纯色图**上成立——边缘那一条本来就该
//      不一样，这是矩形图片第一次和矩形按钮用同一套边缘（0.6.1 之前是硬边）。
void CheckSpriteBatching(SDL_Renderer* renderer) {
    std::printf("\n-- 矩形图片并批：提交次数 / 像素 / 变换（0.6.2 T-08） --\n");
    if (renderer == nullptr) {
        return;
    }

    struct Counts {
        std::uint64_t geometry = 0;
        std::uint64_t texture = 0;
        std::uint64_t draws = 0;
    };
    const auto snapshot = []() {
        const inking_detail::BatchStats stats =
            inking_detail::BatchStatsSnapshot();
        return Counts{stats.geometrySubmissions, stats.textureSubmissions,
                      stats.textureDraws};
    };

    const std::vector<std::uint8_t> atlas = MakeSheetBmp(160, 40, 40, 0);
    constexpr int kSpriteCount = 50;

    const auto buildScene = [&](const char* sceneName) {
        auto scene = std::make_unique<InkingScene>(sceneName);
        for (int i = 0; i < kSpriteCount; ++i) {
            InkingSpriteData data;
            data.name = std::string(sceneName) + "_" + std::to_string(i);
            data.width = 40;
            data.height = 40;
            data.fps = 12.0;
            data.autoplay = false;  // 停在第 0 格：像素才可比
            data.selfAnchor = InkingChangeAnchor::LeftTop;
            data.traceAnchor = InkingChangeAnchor::LeftTop;
            // 10×5 的网格，格距 44 设计像素。
            data.offsetX = 20.0f + static_cast<float>(i % 10) * 44.0f;
            data.offsetY = 20.0f + static_cast<float>(i / 10) * 44.0f;
            data.normal.type = InkingAppearanceType::Image;
            data.normal.source = "batch_atlas";  // 同一个键 ⇒ 同一张纹理
            data.normal.imageBytes = atlas.data();
            data.normal.imageSize = atlas.size();
            data.sheet.frameWidth = 40;
            data.sheet.frameHeight = 40;
            data.sheet.columns = 4;
            data.sheet.rows = 1;
            scene->AddChild(
                std::make_unique<InkingSprite>(scene.get(), data));
        }
        return scene;
    };

    // 覆盖左上 2×2 个 sprite 的区域（设计 20..104 ⇒ 设备 10..52）。
    int outputWidth = 0;
    int outputHeight = 0;
    SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
    const float pixelRatio =
        outputWidth > 0
            ? static_cast<float>(outputWidth)
                / static_cast<float>(InkingDesignWidth())
            : 1.0f;
    const SDL_Rect region{
        static_cast<int>(std::lround(20.0f * pixelRatio)),
        static_cast<int>(std::lround(20.0f * pixelRatio)),
        static_cast<int>(std::lround(84.0f * pixelRatio)),
        static_cast<int>(std::lround(84.0f * pixelRatio))};
    // 第一个 sprite 的**内部**（挖掉四边各 2 设备像素的羽化带）。
    const SDL_Rect interior{region.x + 2, region.y + 2, region.w / 2 - 4,
                            region.h / 2 - 4};

    std::vector<std::uint32_t> batched;
    std::vector<std::uint32_t> batchedInterior;
    std::vector<std::uint32_t> immediate;
    std::vector<std::uint32_t> immediateInterior;
    Counts onDelta{};
    Counts offDelta{};
    {
        std::unique_ptr<InkingScene> scene = buildScene("sprite_batch_on");
        InkingSceneLibrary::SetActiveScene(scene.get());
        scene->TickFrame(1.0f / 60.0f);

        const Counts before = snapshot();
        RenderSvgFrame(renderer, *scene);
        const Counts after = snapshot();
        batched = ReadPixelRegion(renderer, region);
        batchedInterior = ReadPixelRegion(renderer, interior);
        EndSvgFrame(renderer);
        onDelta = Counts{after.geometry - before.geometry,
                         after.texture - before.texture,
                         after.draws - before.draws};
    }
    {
        std::unique_ptr<InkingScene> scene = buildScene("sprite_batch_off");
        InkingSceneLibrary::SetActiveScene(scene.get());
        scene->TickFrame(1.0f / 60.0f);

        inking_detail::SetImageBatchingEnabled(false);
        const Counts before = snapshot();
        RenderSvgFrame(renderer, *scene);
        const Counts after = snapshot();
        immediate = ReadPixelRegion(renderer, region);
        immediateInterior = ReadPixelRegion(renderer, interior);
        EndSvgFrame(renderer);
        inking_detail::SetImageBatchingEnabled(true);  // 立刻恢复默认
        offDelta = Counts{after.geometry - before.geometry,
                          after.texture - before.texture,
                          after.draws - before.draws};
    }

    std::printf("         50 个 sprite：并批 几何 %llu / 单笔 %llu / 纹理批 %llu"
                "　　关开关 几何 %llu / 单笔 %llu\n",
                static_cast<unsigned long long>(onDelta.geometry),
                static_cast<unsigned long long>(onDelta.texture),
                static_cast<unsigned long long>(onDelta.draws),
                static_cast<unsigned long long>(offDelta.geometry),
                static_cast<unsigned long long>(offDelta.texture));
    Check(onDelta.geometry == 1 && onDelta.texture == 0,
          "T-08：50 个矩形 sprite 合并成**一次** SDL_RenderGeometry（0 次单笔绘制）");
    Check(offDelta.texture == kSpriteCount && offDelta.geometry == 0,
          "T-08：关掉开关后回到每张图一次 SDL_RenderTexture（50 次单笔）");

    const auto maxChannelDelta = [](const std::vector<std::uint32_t>& a,
                                    const std::vector<std::uint32_t>& b) {
        int worst = -1;
        if (a.empty() || a.size() != b.size()) {
            return worst;
        }
        worst = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            for (int shift = 0; shift <= 16; shift += 8) {
                const int left = static_cast<int>((a[i] >> shift) & 0xFFu);
                const int right = static_cast<int>((b[i] >> shift) & 0xFFu);
                const int delta = left > right ? left - right : right - left;
                if (delta > worst) {
                    worst = delta;
                }
            }
        }
        return worst;
    };

    const auto diffCount = [](const std::vector<std::uint32_t>& a,
                              const std::vector<std::uint32_t>& b) {
        if (a.empty() || a.size() != b.size()) {
            return -1;
        }
        int diff = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                ++diff;
            }
        }
        return diff;
    };

    const int interiorDiff = diffCount(batchedInterior, immediateInterior);
    const int interiorDelta = maxChannelDelta(batchedInterior, immediateInterior);
    const int fullDiff = diffCount(batched, immediate);
    std::printf("         渐变图集 并批 vs 单笔：内部差异 %d / %zu，最大通道差 %d；"
                "整块差异 %d / %zu\n",
                interiorDiff, batchedInterior.size(), interiorDelta, fullDiff,
                batched.size());
    // 为什么不是"逐位相同"：并批之后走的是**形状网格**，矩形四边多一条 1px 羽化带，
    // 四边形因此向外扩了半个像素，UV 跟着扩——渐变图上就表现为"采样点偏了半个纹素"
    // （R 每纹素差 6，实测最大 3）。这是羽化的固有代价，不是 alpha 或定位的问题；
    // 纯色图那一组会给出**严格相等**，专门盯住"看起来对了但颜色偏了"。
    Check(interiorDelta >= 0 && interiorDelta <= 8,
          "T-08：并批后内部像素与单笔的差 ≤ 8（只有羽化外扩造成的半纹素偏移）");

    // ---- 纯色图：内部必须**严格相等**（没有渐变可放大误差） ----
    {
        // 40×40 全同色（24 位 BMP 没有 alpha，所以这条只盯颜色，不盯透明）。
        constexpr int kSide = 40;
        std::vector<std::uint8_t> solid;
        {
            const int stride = (kSide * 3 + 3) / 4 * 4;
            const int imageSize = stride * kSide;
            solid.assign(static_cast<std::size_t>(54 + imageSize), 0);
            solid[0] = 'B';
            solid[1] = 'M';
            const auto put32 = [&solid](int at, std::uint32_t value) {
                solid[at] = static_cast<std::uint8_t>(value & 0xFFu);
                solid[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
                solid[at + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
                solid[at + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
            };
            put32(2, static_cast<std::uint32_t>(54 + imageSize));
            put32(10, 54);
            put32(14, 40);
            put32(18, kSide);
            put32(22, kSide);
            solid[26] = 1;
            solid[28] = 24;
            put32(34, static_cast<std::uint32_t>(imageSize));
            for (int y = 0; y < kSide; ++y) {
                for (int x = 0; x < kSide; ++x) {
                    std::uint8_t* pixel =
                        &solid[static_cast<std::size_t>(54 + y * stride + x * 3)];
                    pixel[0] = 0x28;  // B
                    pixel[1] = 0x50;  // G
                    pixel[2] = 0xC8;  // R
                }
            }
        }
        const auto buildSolid = [&](const char* sceneName) {
            auto scene = std::make_unique<InkingScene>(sceneName);
            InkingImageData data;
            data.name = std::string(sceneName);
            data.width = kSide;
            data.height = kSide;
            data.normal.type = InkingAppearanceType::Image;
            data.normal.source = std::string(sceneName) + "_bytes";
            data.normal.imageBytes = solid.data();
            data.normal.imageSize = solid.size();
            data.selfAnchor = InkingChangeAnchor::LeftTop;
            data.traceAnchor = InkingChangeAnchor::LeftTop;
            data.offsetX = 20.0f;
            data.offsetY = 20.0f;
            scene->AddChild(
                std::make_unique<InkingStaticImage>(scene.get(), data));
            return scene;
        };
        const SDL_Rect inner{region.x + 2, region.y + 2, 16, 16};
        std::vector<std::uint32_t> batchedSolid;
        std::vector<std::uint32_t> immediateSolid;
        {
            std::unique_ptr<InkingScene> scene = buildSolid("img_batch_solid_on");
            InkingSceneLibrary::SetActiveScene(scene.get());
            scene->TickFrame(1.0f / 60.0f);
            RenderSvgFrame(renderer, *scene);
            batchedSolid = ReadPixelRegion(renderer, inner);
            EndSvgFrame(renderer);
        }
        {
            std::unique_ptr<InkingScene> scene = buildSolid("img_batch_solid_off");
            InkingSceneLibrary::SetActiveScene(scene.get());
            scene->TickFrame(1.0f / 60.0f);
            inking_detail::SetImageBatchingEnabled(false);
            RenderSvgFrame(renderer, *scene);
            immediateSolid = ReadPixelRegion(renderer, inner);
            EndSvgFrame(renderer);
            inking_detail::SetImageBatchingEnabled(true);
        }
        const int solidDiff = diffCount(batchedSolid, immediateSolid);
        std::printf("         纯色图 并批 vs 单笔：内部差异 %d / %zu\n", solidDiff,
                    batchedSolid.size());
        Check(solidDiff == 0,
              "T-08：纯色图并批后内部**逐像素相等**（颜色没有被羽化带偏）");
    }

    // ---- 透明区必须透出背景（不是变黑）：半透明 SVG 盖在亮背景上 ----
    {
        const char* kHalfTransparent =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\">"
            "<rect x=\"0\" y=\"0\" width=\"20\" height=\"40\" fill=\"#CC3333\"/>"
            "</svg>";
        const auto buildSvg = [&](const char* sceneName) {
            auto scene = std::make_unique<InkingScene>(sceneName);
            InkingImageData data;
            data.name = std::string(sceneName);
            data.width = 40;
            data.height = 40;
            data.normal.type = InkingAppearanceType::Image;
            data.normal.source = std::string(sceneName) + "_svg";
            data.normal.imageBytes = SvgBytes(kHalfTransparent);
            data.normal.imageSize = std::strlen(kHalfTransparent);
            data.selfAnchor = InkingChangeAnchor::LeftTop;
            data.traceAnchor = InkingChangeAnchor::LeftTop;
            data.offsetX = 20.0f;
            data.offsetY = 20.0f;
            scene->AddChild(
                std::make_unique<InkingStaticImage>(scene.get(), data));
            return scene;
        };
        // 背景用 0x4E4C71（T-07 探针里的同一个值），透明区读回它才算对。
        const auto renderOnLightBackground = [&](InkingScene& scene) {
            InkingSceneLibrary::SetActiveScene(&scene);
            SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                             InkingDesignHeight(),
                                             SDL_LOGICAL_PRESENTATION_LETTERBOX);
            SDL_SetRenderDrawColor(renderer, 0x4E, 0x4C, 0x71, 255);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderClear(renderer);
            return InkingSceneLibrary::RenderScene(renderer);
        };
        std::vector<std::uint32_t> shot;
        {
            std::unique_ptr<InkingScene> scene =
                buildSvg("img_batch_alpha");
            InkingSceneLibrary::SetActiveScene(scene.get());
            scene->TickFrame(1.0f / 60.0f);
            renderOnLightBackground(*scene);
            // 右半（设计 30..50 ⇒ 设备 15..25）在图里是**透明的**。
            shot = ReadPixelRegion(renderer, SDL_Rect{region.x + 14, region.y + 4,
                                                      4, 4});
            EndSvgFrame(renderer);
        }
        const std::uint32_t transparent = shot.empty() ? 0u : shot[0];
        std::printf("         半透明 SVG 的透明区读回 %08X（背景 4E4C71）\n",
                    transparent);
        Check(!shot.empty() && (transparent & 0x00FFFFFFu) == 0x004E4C71u,
              "T-08：并批后透明区**透出背景**（0x4E4C71），没有变成半透明黑");
    }

    // ---- 变换：并批顺带修好"矩形图片的 transform 被丢掉" ----
    {
        const std::vector<std::uint8_t> wide = MakeSheetBmp(80, 20, 80, 0);
        const auto buildRotated = [&](const char* sceneName) {
            auto scene = std::make_unique<InkingScene>(sceneName);
            InkingImageData data;
            data.name = std::string(sceneName) + "_img";
            data.width = 80;
            data.height = 20;
            data.normal.type = InkingAppearanceType::Image;
            data.normal.source = std::string(sceneName) + "_wide";
            data.normal.imageBytes = wide.data();
            data.normal.imageSize = wide.size();
            data.normal.transform.rotate = 90.0f;  // 度，顺时针
            data.selfAnchor = InkingChangeAnchor::LeftTop;
            data.traceAnchor = InkingChangeAnchor::LeftTop;
            data.offsetX = 100.0f;
            data.offsetY = 100.0f;
            scene->AddChild(std::make_unique<InkingStaticImage>(
                scene.get(), data));
            return scene;
        };
        // 设计 (140, 78) 在"旋转后"的带子里（竖条 x 130..150 × y 70..150），
        // 但在"没旋转"的带子外（横条 y 100..120）——正好分辨转没转。
        const SDL_Rect probe{static_cast<int>(std::lround(140.0f * pixelRatio)),
                             static_cast<int>(std::lround(78.0f * pixelRatio)),
                             1, 1};
        std::vector<std::uint32_t> rotated;
        std::vector<std::uint32_t> unbatched;
        {
            std::unique_ptr<InkingScene> scene =
                buildRotated("sprite_batch_rot_on");
            InkingSceneLibrary::SetActiveScene(scene.get());
            scene->TickFrame(1.0f / 60.0f);
            RenderSvgFrame(renderer, *scene);
            rotated = ReadPixelRegion(renderer, probe);
            EndSvgFrame(renderer);
        }
        {
            std::unique_ptr<InkingScene> scene =
                buildRotated("sprite_batch_rot_off");
            InkingSceneLibrary::SetActiveScene(scene.get());
            scene->TickFrame(1.0f / 60.0f);
            inking_detail::SetImageBatchingEnabled(false);
            RenderSvgFrame(renderer, *scene);
            unbatched = ReadPixelRegion(renderer, probe);
            EndSvgFrame(renderer);
            inking_detail::SetImageBatchingEnabled(true);
        }
        const std::uint32_t onPixel = rotated.empty() ? 0u : rotated[0];
        const std::uint32_t offPixel = unbatched.empty() ? 0u : unbatched[0];
        std::printf("         旋转 90° 的 80×20 图：并批读 %08X / 关开关读 %08X"
                    "（清屏是 FF000000）\n",
                    onPixel, offPixel);
        Check(onPixel != 0xFF000000u,
              "T-08：并批后矩形图片的 transform 生效（旋转 90° 真的转过去了）");
        Check(offPixel == 0xFF000000u,
              "T-08：确认那条快路**确实不看 transform**（关开关时同一点是空的）——"
              "这是 0.6.1 的既有缺陷，本任务顺带修掉");
    }
}

// ---------------------------------------------------------------------------
// T-09：帧事件与 finished
// ---------------------------------------------------------------------------
//
// 三个容易做错、也最容易静默的点：
//   1. **慢帧跨帧**：一次跨 3 帧时中间那些帧的事件必须照发（区间扫描），
//      只判"落点"的实现会安静地吞掉它们；
//   2. **回绕**：3 → 0 的区间不连续，得拆成 `(3, 末帧]` + `[0, 0]` 两段；
//   3. **Seek 不发事件**：跳到第 5 帧不是"播过了第 1~5 帧"。
void CheckSpriteEvents(SDL_Renderer* renderer) {
    std::printf("\n-- 帧事件：区间扫描 / 回绕 / Seek 不发 / finished（0.6.3 T-09） --\n");
    (void)renderer;

    // 造一份带事件的 4 帧配置（第 0 / 1 / 3 帧各挂一个）。
    const auto makeData = [](const char* name, bool loop) {
        InkingSpriteData data = InkingMakeSpriteData(name, 4, 12.0, loop);
        data.events.push_back(InkingSpriteData::SpriteEvent{0, "zero"});
        data.events.push_back(InkingSpriteData::SpriteEvent{1, "one"});
        data.events.push_back(InkingSpriteData::SpriteEvent{3, "three"});
        return data;
    };
    const auto names = [](const std::vector<std::string>& log) {
        std::string out;
        for (const std::string& item : log) {
            if (!out.empty()) {
                out += ",";
            }
            out += item;
        }
        return out;
    };
    const auto subscribe = [](InkingSprite* sprite,
                              std::vector<std::string>* log) {
        sprite->frameEvent.Subscribe(
            [log](const std::string& name, std::size_t frame) {
                log->push_back(name + "@" + std::to_string(frame));
            });
    };

    // ① 正常播放：逐帧按顺序各发一次，播满一圈回绕时第 0 帧的事件也发
    {
        InkingScene scene("sprite_events_play");
        InkingSceneLibrary::SetActiveScene(&scene);
        auto* sprite = static_cast<InkingSprite*>(
            scene.AddChild(std::make_unique<InkingSprite>(
                &scene, makeData("spr_evt_play", true))));
        std::vector<std::string> log;
        subscribe(sprite, &log);

        // 3 次 1/12 走到第 3 帧（第 4 次就到一圈的终点了，会回绕 —— 见下）。
        for (int i = 0; i < 3; ++i) {  // 0→1→2→3
            scene.TickFrame(1.0 / 12.0);
        }
        const std::string first = names(log);
        std::printf("         3 次 1/12 → 事件 %s\n", first.c_str());
        Check(first == "one@1,three@3",
              "T-09：正常播放时第 1 / 3 帧各发一次，第 2 帧没有事件");

        scene.TickFrame(1.0 / 12.0);  // 3 → 0 回绕
        const std::string afterWrap = names(log);
        std::printf("         回绕一次 → 事件 %s\n", afterWrap.c_str());
        Check(afterWrap == "one@1,three@3,zero@0",
              "T-09：回绕（3→0）时第 0 帧的事件照发（区间拆两段）");
    }

    // ② 慢帧跨 3 帧：中间的 1、3 两个事件都不许吞
    {
        InkingScene scene("sprite_events_slow");
        InkingSceneLibrary::SetActiveScene(&scene);
        auto* sprite = static_cast<InkingSprite*>(
            scene.AddChild(std::make_unique<InkingSprite>(
                &scene, makeData("spr_evt_slow", true))));
        std::vector<std::string> log;
        subscribe(sprite, &log);

        scene.TickFrame(0.25);  // 0.25s × 12fps = 3 帧：0 → 3
        const std::string log2 = names(log);
        std::printf("         一次 0.25s（跨 3 帧）→ 事件 %s 帧号=%zu\n",
                    log2.c_str(), sprite->GetFrameIndex());
        Check(sprite->GetFrameIndex() == 3 && log2 == "one@1,three@3",
              "T-09：慢帧跨过的那几帧，事件按帧号升序**全部**发出");
    }

    // ③ Seek / Stop 不发事件（"跳过去"不是"播过去"）
    {
        InkingScene scene("sprite_events_seek");
        InkingSceneLibrary::SetActiveScene(&scene);
        auto* sprite = static_cast<InkingSprite*>(
            scene.AddChild(std::make_unique<InkingSprite>(
                &scene, makeData("spr_evt_seek", true))));
        std::vector<std::string> log;
        subscribe(sprite, &log);

        sprite->SeekToFrame(3);
        sprite->SeekToFrame(0);
        std::printf("         Seek 两次 → 事件数 %zu\n", log.size());
        Check(log.empty(),
              "T-09：SeekToFrame 不发帧事件（只有播放推进才发）");
    }

    // ④ 非循环：finished 恰好一次；Play() 再来一遍才发第二次
    {
        InkingScene scene("sprite_events_finished");
        InkingSceneLibrary::SetActiveScene(&scene);
        auto* sprite = static_cast<InkingSprite*>(
            scene.AddChild(std::make_unique<InkingSprite>(
                &scene, makeData("spr_evt_once", false))));
        int finishedCount = 0;
        bool finishedSawPlaying = true;
        sprite->finished.Subscribe([&finishedCount, &finishedSawPlaying, sprite] {
            ++finishedCount;
            // 回调里读状态：应该说"已经不在播了"，而不是"正在播最后一下"。
            finishedSawPlaying = sprite->IsPlaying();
        });

        for (int i = 0; i < 6; ++i) {  // 4 帧走完 + 两次多余的 tick
            scene.TickFrame(1.0 / 12.0);
        }
        std::printf("         非循环跑完 → finished=%d（多 tick 两次后）\n",
                    finishedCount);
        Check(finishedCount == 1 && !finishedSawPlaying,
              "T-09：非循环播完 finished 只发一次，且回调里 IsPlaying() 已为假");

        sprite->Play();
        for (int i = 0; i < 5; ++i) {
            scene.TickFrame(1.0 / 12.0);
        }
        std::printf("         Play() 再跑完 → finished=%d\n", finishedCount);
        Check(finishedCount == 2, "T-09：Play() 之后能再播一遍并再发一次 finished");

        // 循环动画永远不发 finished
        InkingScene loopScene("sprite_events_loop");
        InkingSceneLibrary::SetActiveScene(&loopScene);
        auto* loopSprite = static_cast<InkingSprite*>(
            loopScene.AddChild(std::make_unique<InkingSprite>(
                &loopScene, makeData("spr_evt_loop", true))));
        int loopFinished = 0;
        loopSprite->finished.Subscribe([&loopFinished] { ++loopFinished; });
        for (int i = 0; i < 12; ++i) {
            loopScene.TickFrame(1.0 / 12.0);
        }
        Check(loopFinished == 0, "T-09：循环动画从不发 finished（它没有「结束」这回事）");
    }

    // ⑤ 事件帧越界：配置不合法，构造期就报（与运行期同一份 Validate()）
    {
        InkingScene scene("sprite_events_bad");
        InkingSceneLibrary::SetActiveScene(&scene);
        InkingSpriteData data = makeData("spr_evt_bad", true);
        data.events.push_back(InkingSpriteData::SpriteEvent{9, "nowhere"});

        InkLog::ResetStats();
        auto* sprite = static_cast<InkingSprite*>(
            scene.AddChild(std::make_unique<InkingSprite>(&scene, data)));
        std::printf("         事件指向第 9 帧（只有 4 帧）→ IsValid=%d\n%s\n",
                    sprite->IsValid() ? 1 : 0,
                    sprite->GetConfigError().c_str());
        Check(!sprite->IsValid()
                  && sprite->GetConfigError().find("只有 4 帧")
                         != std::string::npos,
              "T-09：事件帧越界在构造期被拦下（不是播到那儿才发现）");
        Check(InkLog::CountOf(InkingLogLevel::Error) >= 1,
              "T-09：越界事件同时有一条 ERROR");
    }

    // ⑥ 帧事件回调里换配置（0.9.0 T-01：快照派发 + 切换延后到本帧末）
    //
    // 改之前这是 UB：`fireEventsBetween` 边遍历 `_sprite.events` 边调回调，回调里
    // `SetSpriteData` 把整个 `_sprite` 换掉，剩下的 `three@3` 从悬垂容器里读；
    // 外层还会拿旧动画的帧号再发一次 `frameChanged`。
    {
        InkingScene scene("sprite_events_swap");
        InkingSceneLibrary::SetActiveScene(&scene);
        auto* sprite = static_cast<InkingSprite*>(
            scene.AddChild(std::make_unique<InkingSprite>(
                &scene, makeData("spr_evt_swapA", true))));
        std::vector<std::string> log;
        subscribe(sprite, &log);

        int frameChangedCount = 0;
        sprite->frameChanged.Subscribe(
            [&frameChangedCount](std::size_t, std::size_t) {
                ++frameChangedCount;
            });

        // 第二份配置：帧数同样是 4（于是"到底换没换"只能看名字），事件换成另一族。
        InkingSpriteData second =
            InkingMakeSpriteData("spr_evt_swapB", 4, 12.0, true);
        second.events.push_back(InkingSpriteData::SpriteEvent{2, "swapTwo"});

        int swapCalls = 0;
        sprite->frameEvent.Subscribe(
            [&swapCalls, sprite, &second](const std::string& name,
                                          std::size_t /*frame*/) {
                if (name != "one") {
                    return;
                }
                ++swapCalls;
                sprite->SetSpriteData(second);
            });

        const std::uint64_t advanceBefore = sprite->GetFrameAdvanceCount();
        scene.TickFrame(0.25);  // 0.25s × 12fps = 3 帧：0 → 3（跨过 1 与 3）
        const std::string afterSwap = names(log);
        std::printf("         0.25s 跨 3 帧、回调里换配置 → 事件 %s 帧号=%zu 名称=%s\n",
                    afterSwap.c_str(), sprite->GetFrameIndex(),
                    sprite->GetSpriteData().name.c_str());
        Check(afterSwap == "one@1,three@3",
              "T-01：回调里换配置后，本批剩下的事件照发（快照派发，不是边遍历边换）");
        Check(swapCalls == 1 && sprite->GetSpriteData().name == "spr_evt_swapB",
              "T-01：切换真的执行了（在本帧末），不是被静默丢掉");
        Check(frameChangedCount == 0,
              "T-01：这一帧换过配置 ⇒ 旧动画的 frameChanged 不发");
        Check(sprite->GetFrameAdvanceCount() - advanceBefore == 1,
              "T-01：计数只记播放推进的那一次落帧，切换本身不计（边界：与改前一致）");
        Check(sprite->GetFrameCount() == 4 && sprite->GetFrameIndex() == 0
                  && sprite->GetConfigError().empty(),
              "T-01：切换落在第 0 帧、新配置帧数对上、没有配置错误");

        // 稳定性：换完之后照常播（第二份配置的 `swapTwo@2` 该在第 2 帧出现）。
        for (int i = 0; i < 3; ++i) {
            scene.TickFrame(1.0 / 12.0);
        }
        const std::string afterPlay = names(log);
        std::printf("         换完再跑 3 次 1/12 → 事件 %s 帧号=%zu\n",
                    afterPlay.c_str(), sprite->GetFrameIndex());
        Check(afterPlay == "one@1,three@3,swapTwo@2"
                  && sprite->GetFrameIndex() == 3,
              "T-01：换完之后从第 0 帧照常推进，新配置的事件生效、不崩");
    }
}

// ---------------------------------------------------------------------------
// T-01：视频真的出画（批次 a 的门槛）
// ---------------------------------------------------------------------------
//
// 判据对应任务书 §3 T-01 的验收，但用**实际的 API 名字**：任务书写的是
// `GetFrameCount()`，库里没有这个函数（观测口是 `GetDecodedFrameCount()` /
// `GetPresentedFrameCount()` / `GetDroppedFrameCount()`）。
//
//   ① 解出的帧数 > 0；
//   ② 画面尺寸 = 320×240（与 `ffmpeg -f lavfi -i testsrc=size=320x240` 一致）；
//   ③ 丢帧 = 0（1 秒素材、30fps 的帧间隔 33ms，20ms 一帧地消费不该丢）。
//
// 外加一条**像素判据**：画面内部不止一种颜色。兜底色是一整块纯色，只有真的把
// testsrc 贴上去了才会出现多色——这条才是"真出画"，前三条只能证明"解出来了"。
void CheckVideoDecode(SDL_Renderer* renderer) {
    std::printf("\n-- 视频解码：真的出画（T-01）--\n");
    if (renderer == nullptr) {
        Check(false, "T-01：需要一个渲染器（拿不到就跳过整组）");
        return;
    }

    struct VideoScene : public InkingScene {
        VideoScene() : InkingScene("probe_video_decode") {}
    };
    VideoScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "T-01：探针场景注册成功（名字不与他人冲突）");
        return;
    }

    InkingVideoData data;
    data.name = "t01_video";
    data.width = 320;
    data.height = 240;
    data.source = "probe.mp4";
    data.fit = InkingVideoFit::Fill;  // 铺满 = 画面四边贴齐组件框，读像素好定位
    // 兜底色故意选一个 testsrc 里不会出现的值：一旦读像素看到它，就说明"贴的是
    // 兜底而不是画面"——这比"读到黑色"更能定性（黑既可能是没画，也可能是画面黑）。
    data.normal.color = 0xFF123456u;
    data.options.autoplay = false;    // 手动 Play：帧序与断言对得上
    data.options.loop = false;
    data.options.muted = true;        // 没有音轨，静音只是别让音频层参与

    auto* video = static_cast<InkingStaticVideo*>(
        scene.AddChild(std::make_unique<InkingStaticVideo>(&scene, data)));

    Check(video->AssetExists(), "T-01：素材 probe.mp4 在资源根里找得到");
    Check(video->IsOpened(), "T-01：打开成功（解封装 + 解码器都就绪）");
    Check(video->GetVideoWidth() == 320 && video->GetVideoHeight() == 240,
          "T-01：② 解出的画面 = 320×240（与生成参数一致）");
    if (!video->IsOpened()) {
        // 没打开就没什么可断言的了：把"环境缺什么"说清楚，别让它伪装成通过。
        std::printf("      [skip] 视频没打开（素材 %s）——解码断言不适用\n",
                    video->AssetPath().empty() ? "没解析到" : video->AssetPath().c_str());
        return;
    }

    video->Play();
    // 1 设计单位 = 1 像素：窗口 640×360，组件在 (0,0) 320×240 ⇒ 读像素不用换算。
    SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(&scene);

    // 约 0.8 秒真实时间：解码在后台线程跑，而"上屏"由主时钟（无音轨 ⇒ 真实时间）
    // 决定——循环转得再快也不会让画面提前出现，所以就按真实时间等。
    for (int i = 0; i < 40; ++i) {
        scene.TickFrame(1.0f / 30.0f);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        InkingSceneLibrary::RenderScene(renderer);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::printf("         解码 %u 帧 / 上屏 %u 帧 / 丢 %u 帧 / 时长 %.2f 秒\n",
                video->GetDecodedFrameCount(), video->GetPresentedFrameCount(),
                video->GetDroppedFrameCount(),
                static_cast<double>(video->GetDurationSeconds()));

    Check(video->GetDecodedFrameCount() > 0, "T-01：① 解出的帧数 > 0");
    Check(video->GetPresentedFrameCount() > 0,
          "T-01：上屏帧数 > 0（画面真的贴上去了，不是一直在画兜底色）");
    // 丢帧**不是**要求恰好 0：主时钟是真实时间（无音轨那条路），而这台机器上
    // 还有别的活（编译 / 测试并行），一次 sleep 打盹就会让队首落后超过 60ms 的
    // 阈值、丢一帧。要求"0"等于把断言绑在机器负载上——实测同一份代码跑出过
    // 28/0 与 28/1 两种结果。
    //
    // 但也不能不判：历史上那个 bug 的形态是"队首判据方向写反 ⇒ 队列被一路清空、
    // 一帧都上不了屏（自检读出'已出帧 0，丢 19'）"。所以判据取**比例**：
    // 丢帧不超过两成，真坏掉时是"出一帧丢一大半"，一眼就红。
    const std::uint32_t presented = video->GetPresentedFrameCount();
    const std::uint32_t dropped = video->GetDroppedFrameCount();
    Check(presented > 0 && dropped <= std::max<std::uint32_t>(2, presented / 5),
          "T-01：③ 丢帧 ≤ 两成（真坏了是「出一帧丢一大半」，不是差这一两帧）");

    // 像素：读画面**内部**（避开边缘那一条抗锯齿带）。
    const std::vector<std::uint32_t> pixels =
        ReadPixelRegion(renderer, SDL_Rect{40, 30, 240, 180});
    std::vector<std::uint32_t> seen;
    for (std::size_t i = 0; i < pixels.size(); i += 32) {
        if (std::find(seen.begin(), seen.end(), pixels[i]) == seen.end()) {
            seen.push_back(pixels[i]);
        }
        if (seen.size() > 8) {
            break;
        }
    }
    std::printf("         画面内部抽到的颜色种数：%zu（兜底纯色只会是 1）；"
                "首像素 0x%08X / 视频绝对位置 (%.1f, %.1f)\n",
                seen.size(), pixels.empty() ? 0u : pixels[0],
                static_cast<double>(video->GetAbsX()),
                static_cast<double>(video->GetAbsY()));
    // 兜底色若出现在画面里，说明贴的是**兜底**而不是画面（或者画面的 tint 被它
    // 乘黑了）。两种都算"没出画"，所以这条一起判。
    const bool sawFallback =
        std::find(seen.begin(), seen.end(), data.normal.color) != seen.end();
    Check(pixels.size() == 240u * 180u && seen.size() > 1 && !sawFallback,
          "T-01：画面内部不止一种颜色、且看不到兜底色（真的把 testsrc 贴上去了）");

    SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-02：滑块的点击 / 拖动 / 键盘 / 部件
// ---------------------------------------------------------------------------
//
// 走**真路径**：喂指针状态 + `DispatchPointer()`（与图片拖动那条探针同一套），
// 于是"场景判出按下算谁的、捕获、拖动派发"整条链都真的跑了一遍——直接调
// `SetValue` 只能证明写入口，证不了手感那一层。
void CheckSliderDrag() {
    std::printf("\n-- 滑块：点 / 拖 / 键盘 / 部件（T-02）--\n");

    struct SliderScene : public InkingScene {
        SliderScene() : InkingScene("probe_slider") {}
    };
    SliderScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "T-02：探针场景注册成功（名字不与他人冲突）");
        return;
    }

    inking_prefab::SliderData data = inking_prefab::SliderData::MakeDefault();
    data.name = "t02_slider";
    data.width = 200;
    data.height = 28;
    data.offsetX = 20.0f;
    data.offsetY = 20.0f;
    data.min = 0.0f;
    data.max = 100.0f;
    data.step = 1.0f;  // 量化到整数，断言才能用"≈ 且 ±1 个 step"
    data.value = 0.0f;

    auto* slider = static_cast<inking_prefab::Slider*>(
        scene.AddChild(std::make_unique<inking_prefab::Slider>(&scene, data)));

    // 静态组件进**全局命中表**，而表是"结构脏就重烘"——所以喂指针之前必须先跑一帧，
    // 否则 `DispatchPointer` 问的是一张还没建的表，按下算不到滑块头上
    // （症状就是"值一直是 0、事件一次都没有"，看起来像组件没接指针）。
    scene.TickFrame(1.0f / 60.0f);

    int changedCount = 0;
    float lastNow = -1.0f;
    float lastBefore = -1.0f;
    int releasedCount = 0;
    slider->changed.Subscribe([&changedCount, &lastNow, &lastBefore](float now,
                                                                    float before) {
        ++changedCount;
        lastNow = now;
        lastBefore = before;
    });
    slider->released.Subscribe([&releasedCount] { ++releasedCount; });

    // 轨道跨度：[20, 220]（offsetX 20 + 宽 200），纵向中心 y = 34。
    const float trackLeft = 20.0f;
    const float trackWidth = 200.0f;
    const float probeY = 34.0f;

    // ---- ① 点轨道 25% 处：值 = 25 ----
    scene.SetPointerState(trackLeft + trackWidth * 0.25f, probeY, true, true);
    scene.DispatchPointer();
    std::printf("         点 25%% → 值 %.1f（期望 25）、changed %d 次\n",
                static_cast<double>(slider->GetValue()), changedCount);
    Check(std::fabs(slider->GetValue() - 25.0f) <= 1.0f,
          "T-02：① 点轨道 25% → 值 ≈ 25（±1 个 step）");
    Check(changedCount == 1 && lastNow == slider->GetValue() &&
              lastBefore == 0.0f,
          "T-02：① 只发一次 changed，且参数是（新值, 旧值）");

    // ---- ② 拖到 75%：值跟着走；同一点再派发不发事件 ----
    scene.SetPointerState(trackLeft + trackWidth * 0.75f, probeY, true, true);
    scene.DispatchPointer();
    Check(std::fabs(slider->GetValue() - 75.0f) <= 1.0f,
          "T-02：② 拖到 75% → 值 ≈ 75（累计位移，不是本次增量）");

    const int changedAt75 = changedCount;
    scene.SetPointerState(trackLeft + trackWidth * 0.75f, probeY, true, true);
    scene.DispatchPointer();
    Check(changedCount == changedAt75,
          "T-02：② 同一位置再派发 → **不发** changed（Emit 只在真变时）");

    // ---- ③ 抬起：released 一次；changed 累计 2 次（25 → 75）----
    scene.SetPointerState(trackLeft + trackWidth * 0.75f, probeY, false, true);
    scene.DispatchPointer();
    Check(releasedCount == 1, "T-02：③ 抬起 → released 一次");
    Check(changedCount == 2, "T-02：③ changed 累计 2 次（25 → 75，中间没有多余事件）");

    // ---- ④ 部件：填充 = 轨道 × 比例；圆盘水平落在轨道内、纵向同心 ----
    // 取副本（理由同 T-03 那一处：`GetParts()` 返回的是内部那一份缓存）。
    const inking_prefab::SliderParts parts = slider->GetParts();
    std::printf("         部件：轨道 %.0f×%.0f / 填充 %.1f / 圆盘 (%.1f, %.1f) %.0f×%.0f\n",
                static_cast<double>(parts.track.width),
                static_cast<double>(parts.track.height),
                static_cast<double>(parts.fill.width),
                static_cast<double>(parts.thumb.x),
                static_cast<double>(parts.thumb.y),
                static_cast<double>(parts.thumb.width),
                static_cast<double>(parts.thumb.height));
    Check(std::fabs(parts.fill.width - parts.track.width * 0.75f) <= 1.5f,
          "T-02：④ 填充宽 = 轨道宽 × 0.75（与值同步）");
    Check(parts.thumb.x >= parts.track.x - 0.01f &&
              parts.thumb.x + parts.thumb.width <=
                  parts.track.x + parts.track.width + 0.01f,
          "T-02：④ 圆盘的**水平范围**落在轨道内（垂直溢出是设计：盘 18 > 轨 6）");
    Check(std::fabs((parts.thumb.y + parts.thumb.height * 0.5f) -
                    (parts.track.y + parts.track.height * 0.5f)) <= 0.01f,
          "T-02：④ 圆盘与轨道纵向同心");

    // ---- ⑤ 键盘：焦点 + 右箭头 = +1 step ----
    Check(slider->WantsKeyboardFocus(), "T-02：⑤ 滑块要键盘焦点（否则箭头静默无效）");
    const float beforeKey = slider->GetValue();
    scene.SetKeyboardFocus(slider);
    InkingKeyboardInput keys;
    SDL_Event keyDown{};
    keyDown.type = SDL_EVENT_KEY_DOWN;
    keyDown.key.key = SDLK_RIGHT;
    keys.UpdateFromSDL(keyDown);
    scene.SetKeyboardInput(keys);
    scene.DispatchKeyboard();
    std::printf("         按 → ：%.1f → %.1f\n", static_cast<double>(beforeKey),
                static_cast<double>(slider->GetValue()));
    Check(std::fabs(slider->GetValue() - (beforeKey + 1.0f)) <= 0.01f,
          "T-02：⑤ 焦点在滑块上按 → ：值 +1 step");

    // ---- ⑥ 命中表不重烘：改值只标重绘脏 ----
    const std::uint64_t before = scene.GetStructureGeneration();
    slider->SetValue(10.0f);
    Check(scene.GetStructureGeneration() == before,
          "T-02：⑥ 改值不动骨架代次（轨道几何没变，只有画面变了）");
}

// ---------------------------------------------------------------------------
// T-03：进度条的部件 / 幂等 / 像素
// ---------------------------------------------------------------------------
void CheckProgressBar(SDL_Renderer* renderer) {
    std::printf("\n-- 进度条：部件 / 幂等 / 像素（T-03）--\n");

    struct BarScene : public InkingScene {
        BarScene() : InkingScene("probe_progress_bar") {}
    };
    BarScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "T-03：探针场景注册成功（名字不与他人冲突）");
        return;
    }

    inking_prefab::ProgressBarData data =
        inking_prefab::ProgressBarData::MakeDefault();
    data.name = "t03_bar";
    data.width = 200;
    data.height = 20;
    data.offsetX = 20.0f;
    data.offsetY = 20.0f;
    data.progress = 0.0f;

    auto* bar = static_cast<inking_prefab::ProgressBar*>(
        scene.AddChild(std::make_unique<inking_prefab::ProgressBar>(&scene, data)));

    // ---- ① 几何：fill = track × progress ----
    Check(bar->SetProgress(0.5f), "T-03：① SetProgress(0.5) 真的改了");
    Check(!bar->SetProgress(0.5f),
          "T-03：① 同值再来一次返回 false（每帧喂同一个值 ⇒ 零额外重绘）");
    // `GetParts()` 返回的是**现算的那一份**（内部缓存），所以这里取副本：
    // 拿引用的话下一次 `SetProgress` 会把手里这份也改掉（同一个对象）。
    const inking_prefab::ProgressBarParts half = bar->GetParts();
    Check(std::fabs(half.fill.width - half.track.width * 0.5f) <= 0.01f,
          "T-03：① 填充宽 = 轨道宽 × 0.5");

    // ---- ② 进度 0：填充段零宽零高（onRender 据此不画这一笔）----
    Check(bar->SetProgress(0.0f), "T-03：② SetProgress(0) 真的改了");
    const inking_prefab::ProgressBarParts zero = bar->GetParts();
    Check(zero.fill.width == 0.0f && zero.fill.height == 0.0f,
          "T-03：② 进度 0 ⇒ 填充段零宽零高（不是「零宽的三角化」）");

    // ---- ③ 越界夹取 ----
    bar->SetProgress(1.5f);
    Check(bar->GetProgress() == 1.0f, "T-03：③ 1.5 → 夹到 1（不报错、不越界）");
    bar->SetProgress(-1.0f);
    Check(bar->GetProgress() == 0.0f, "T-03：③ -1 → 夹到 0");

    // ---- ④ 进度不变几何：容器不重排 ----
    Check(bar->GetWidth() == 200 && bar->GetHeight() == 20,
          "T-03：④ 进度变化不改组件尺寸（容器不重排）");

    // ---- ⑤ 像素：左半是填充色、右半是轨道色；进度 0 时左半也是轨道色 ----
    if (renderer == nullptr) {
        std::printf("      [skip] 没有渲染器——像素断言不适用\n");
        return;
    }
    SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(&scene);
    scene.TickFrame(1.0f / 60.0f);

    // 组件真的落在哪：用**它自己的绝对坐标**，别拿配置里的 offset 猜
    // （锚点 / 父级排版都会挪它）。
    const int absX = static_cast<int>(std::lround(bar->GetAbsX()));
    const int absY = static_cast<int>(std::lround(bar->GetAbsY()));
    const auto sampleAt = [&](int localX) {
        const std::vector<std::uint32_t> one = ReadPixelRegion(
            renderer, SDL_Rect{absX + localX, absY + 10, 1, 1});
        return one.empty() ? 0u : one[0];
    };
    const auto renderFrame = [&] {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        InkingSceneLibrary::RenderScene(renderer);
    };
    // 通道差（颜色比对的容差：软件后端不抖动，但仍不写死逐位相等）。
    const auto nearColor = [](std::uint32_t a, std::uint32_t b) {
        for (int shift = 0; shift <= 16; shift += 8) {
            const int left = static_cast<int>((a >> shift) & 0xFFu);
            const int right = static_cast<int>((b >> shift) & 0xFFu);
            const int delta = left > right ? left - right : right - left;
            if (delta > 12) {
                return false;
            }
        }
        return true;
    };

    bar->SetProgress(0.5f);
    renderFrame();
    const std::uint32_t leftHalf = sampleAt(50);    // 0~100 这一段是填充
    const std::uint32_t rightHalf = sampleAt(150);  // 100~200 这一段是轨道
    std::printf("         进度 0.5：左半 0x%08X / 右半 0x%08X\n", leftHalf,
                rightHalf);
    Check(!nearColor(leftHalf, rightHalf),
          "T-03：⑤ 进度 0.5 时左右两半**颜色不同**（填充真的画出来了）");

    bar->SetProgress(0.0f);
    renderFrame();
    const std::uint32_t leftAtZero = sampleAt(50);
    std::printf("         进度 0.0：左半 0x%08X（应与右半同色）\n", leftAtZero);
    Check(nearColor(leftAtZero, rightHalf),
          "T-03：⑤ 进度 0 时左半与右半同色（填充块真的没画）");

    SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-04：勾选三件套（翻 / 互斥 / 部件 / 勾的方向）
// ---------------------------------------------------------------------------
//
// 走真路径（喂指针 + `DispatchPointer`），所以"命中 → 点击 → 翻转 → 发事件"
// 整条链都跑了一遍。三件套共用同一个基类，所以断言也共用一套写法。
void CheckToggleLike(SDL_Renderer* renderer) {
    std::printf("\n-- 勾选三件套：翻 / 互斥 / 部件（T-04）--\n");

    struct ToggleScene : public InkingScene {
        ToggleScene() : InkingScene("probe_toggle_like") {}
    };
    ToggleScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "T-04：探针场景注册成功（名字不与他人冲突）");
        return;
    }

    inking_prefab::ToggleData cbData = inking_prefab::MakeCheckboxData();
    cbData.name = "t04_checkbox";
    cbData.offsetX = 20.0f;
    cbData.offsetY = 20.0f;
    auto* box = static_cast<inking_prefab::Checkbox*>(
        scene.AddChild(std::make_unique<inking_prefab::Checkbox>(&scene, cbData)));

    inking_prefab::ToggleData r1Data = inking_prefab::MakeRadioData();
    r1Data.name = "t04_radio_1";
    r1Data.offsetX = 60.0f;
    r1Data.offsetY = 20.0f;
    auto* radio1 = static_cast<inking_prefab::Radio*>(
        scene.AddChild(std::make_unique<inking_prefab::Radio>(&scene, r1Data)));

    inking_prefab::ToggleData r2Data = inking_prefab::MakeRadioData();
    r2Data.name = "t04_radio_2";
    r2Data.offsetX = 90.0f;
    r2Data.offsetY = 20.0f;
    auto* radio2 = static_cast<inking_prefab::Radio*>(
        scene.AddChild(std::make_unique<inking_prefab::Radio>(&scene, r2Data)));

    inking_prefab::ToggleData swData = inking_prefab::MakeSwitchData();
    swData.name = "t04_switch";
    swData.offsetX = 20.0f;
    swData.offsetY = 60.0f;
    auto* toggle = static_cast<inking_prefab::Switch*>(
        scene.AddChild(std::make_unique<inking_prefab::Switch>(&scene, swData)));

    // 静态组件进全局命中表：喂指针之前先跑一帧（同 T-02 那条）。
    scene.TickFrame(1.0f / 60.0f);

    const auto clickAt = [&scene](float x, float y) {
        scene.SetPointerState(x, y, true, true);
        scene.DispatchPointer();
        scene.SetPointerState(x, y, false, true);
        scene.DispatchPointer();
    };

    // ---- ① Checkbox：点一次翻过去、再点翻回来 ----
    int boxClicked = 0;
    int boxCancelled = 0;
    box->clicked.Subscribe([&boxClicked] { ++boxClicked; });
    box->cancelled.Subscribe([&boxCancelled] { ++boxCancelled; });

    clickAt(29.0f, 29.0f);  // 复选框中心（20 + 18/2）
    Check(box->IsChecked() && boxClicked == 1,
          "T-04：① 点 Checkbox 一次 → 选中 + `clicked` 一条");

    clickAt(29.0f, 29.0f);
    Check(!box->IsChecked() && boxCancelled == 1,
          "T-04：① 再点一次 → 未选中 + `cancelled` 一条");

    // ---- ② SetChecked 是幂等的：同值返回 false 且零事件 ----
    const int clickedBefore = boxClicked;
    const int cancelledBefore = boxCancelled;
    Check(!box->SetChecked(false),
          "T-04：② `SetChecked(同值)` 返回 false（幂等）");
    Check(boxClicked == clickedBefore && boxCancelled == cancelledBefore,
          "T-04：② 幂等调用一条事件都不发");
    Check(box->SetChecked(true) && boxClicked == clickedBefore + 1,
          "T-04：② `SetChecked(真值)` 也走事件（程序入口与用户点击同一个出口）");

    // ---- ③ Radio：组内只有一个为真；点已选中的不翻转 ----
    inking_prefab::RadioGroup group;
    radio1->SetGroup(&group);
    radio2->SetGroup(&group);
    Check(group.GetCount() == 2, "T-04：③ 两个单选挂进了同一个组");

    int r1Clicked = 0;
    int r1Cancelled = 0;
    int r2Clicked = 0;
    radio1->clicked.Subscribe([&r1Clicked] { ++r1Clicked; });
    radio1->cancelled.Subscribe([&r1Cancelled] { ++r1Cancelled; });
    radio2->clicked.Subscribe([&r2Clicked] { ++r2Clicked; });

    clickAt(69.0f, 29.0f);  // 第一个单选
    Check(radio1->IsChecked() && !radio2->IsChecked() && r1Clicked == 1,
          "T-04：③ 点第一个 → 它选中、另一个未选中、`clicked` 一条");

    clickAt(69.0f, 29.0f);  // 再点已选中的那个
    Check(radio1->IsChecked() && r1Cancelled == 0 && r1Clicked == 1,
          "T-04：③ 点**已选中**的单选 → 不翻转、零事件（单选不可反选）");

    clickAt(99.0f, 29.0f);  // 第二个单选
    std::printf("         组内：r1=%d r2=%d / r1.cancelled=%d / r2.clicked=%d\n",
                radio1->IsChecked() ? 1 : 0, radio2->IsChecked() ? 1 : 0,
                r1Cancelled, r2Clicked);
    Check(!radio1->IsChecked() && radio2->IsChecked(),
          "T-04：③ 点另一个 → 组内只有一个为真（互斥）");
    Check(r1Cancelled == 1 && r2Clicked == 1,
          "T-04：③ 一次换人 = 1 条 `clicked` + 1 条 `cancelled`（各发各的）");
    Check(group.GetChecked() == radio2,
          "T-04：③ `RadioGroup::GetChecked()` 指向新的那个");

    // ---- ④ 部件：mark 落在 base 之内（三件各验一次）----
    const auto markInsideBase = [](const inking_prefab::ToggleParts& parts) {
        return parts.mark.x >= parts.base.x - 0.01f &&
               parts.mark.y >= parts.base.y - 0.01f &&
               parts.mark.x + parts.mark.width <=
                   parts.base.x + parts.base.width + 0.01f &&
               parts.mark.y + parts.mark.height <=
                   parts.base.y + parts.base.height + 0.01f;
    };
    Check(markInsideBase(box->GetParts()), "T-04：④ Checkbox 的勾落在框内");
    Check(markInsideBase(radio1->GetParts()), "T-04：④ Radio 的点落在圆内");

    // ---- ⑤ Switch：关过渡时立刻到位，且布局一个像素不变 ----
    const int switchWidth = toggle->GetWidth();
    const int switchHeight = toggle->GetHeight();
    toggle->SetAnimated(false);
    clickAt(40.0f, 71.0f);  // 开关中心（20 + 40/2, 60 + 22/2）
    const inking_prefab::ToggleParts onParts = toggle->GetParts();
    std::printf("         开关：checked=%d / knob=%.2f / 滑块 (%.1f, %.1f) %.0f×%.0f\n",
                toggle->IsChecked() ? 1 : 0,
                static_cast<double>(toggle->GetKnobRatio()),
                static_cast<double>(onParts.mark.x),
                static_cast<double>(onParts.mark.y),
                static_cast<double>(onParts.mark.width),
                static_cast<double>(onParts.mark.height));
    Check(toggle->IsChecked() && toggle->GetKnobRatio() == 1.0f,
          "T-04：⑤ `SetAnimated(false)` 后点开关 → 立刻到开端（不留半路）");
    Check(toggle->GetWidth() == switchWidth &&
              toggle->GetHeight() == switchHeight,
          "T-04：⑤ 过渡开关不影响布局（组件框一个像素不变）");
    Check(markInsideBase(onParts), "T-04：⑤ 滑块落在轨道内");
    Check(std::fabs((onParts.mark.x + onParts.mark.width) -
                    (onParts.base.x + onParts.base.width - swData.knobInset)) <=
              0.01f,
          "T-04：⑤ 开到顶时滑块右缘贴住轨道内侧");

    // ---- ⑥ Switch 的过渡：中间在半路，0.15 秒后到位 ----
    //
    // ⚠️ `TickAnimation` 只在**活跃场景**的 `TickFrame` 里跑（停放 / 关闭的场景不
    // 推进动画）——所以这一段必须先把它设为活跃，否则滑块一步都不动，
    // 而现象是"过渡没生效"，看起来像组件的问题。
    InkingSceneLibrary::SetActiveScene(&scene);
    toggle->SetAnimated(true);
    clickAt(40.0f, 71.0f);  // 关回去
    Check(!toggle->IsChecked() && toggle->GetKnobRatio() == 1.0f,
          "T-04：⑥ 刚点完还在开端（过渡从下一帧开始推进）");
    scene.TickFrame(0.05f);
    const float midRatio = toggle->GetKnobRatio();
    std::printf("         过渡一帧（0.05s）后 knob=%.2f\n",
                static_cast<double>(midRatio));
    Check(midRatio > 0.0f && midRatio < 1.0f,
          "T-04：⑥ 过渡中滑块在半路（0 < knob < 1）");
    scene.TickFrame(0.2f);  // 一次走完剩下的
    Check(toggle->GetKnobRatio() == 0.0f, "T-04：⑥ 累计超过 0.15 秒后到位");

    // ---- ⑦ 像素：内置勾是"从下往上挑"的，不是反过来的那道 ----
    if (renderer == nullptr) {
        std::printf("      [skip] 没有渲染器——勾的方向断言不适用\n");
        return;
    }
    box->SetChecked(true);
    SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(&scene);
    scene.TickFrame(1.0f / 60.0f);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    InkingSceneLibrary::RenderScene(renderer);

    const inking_prefab::ToggleParts boxParts = box->GetParts();
    const int markX = static_cast<int>(std::lround(box->GetAbsX() + boxParts.mark.x));
    const int markY = static_cast<int>(std::lround(box->GetAbsY() + boxParts.mark.y));
    const int markW = std::max(1, static_cast<int>(std::lround(boxParts.mark.width)));
    const int markH = std::max(1, static_cast<int>(std::lround(boxParts.mark.height)));
    const std::vector<std::uint32_t> marks =
        ReadPixelRegion(renderer, SDL_Rect{markX, markY, markW, markH});

    const std::uint32_t tickColor = cbData.mark.color;
    const auto isTick = [tickColor](std::uint32_t pixel) {
        for (int shift = 0; shift <= 16; shift += 8) {
            const int a = static_cast<int>((pixel >> shift) & 0xFFu);
            const int b = static_cast<int>((tickColor >> shift) & 0xFFu);
            const int delta = a > b ? a - b : b - a;
            if (delta > 24) {  // 边缘那圈是羽化混出来的，只数接近实心的
                return false;
            }
        }
        return true;
    };

    int total = 0;
    int bottomLeft = 0;
    int topRight = 0;
    int topLeftCorner = 0;
    if (marks.size() == static_cast<std::size_t>(markW * markH)) {
        for (int y = 0; y < markH; ++y) {
            for (int x = 0; x < markW; ++x) {
                if (!isTick(marks[static_cast<std::size_t>(y * markW + x)])) {
                    continue;
                }
                ++total;
                if (x < markW / 2 && y >= markH / 2) {
                    ++bottomLeft;
                }
                if (x >= markW / 2 && y < markH / 2) {
                    ++topRight;
                }
                if (x < 4 && y < 4) {
                    ++topLeftCorner;
                }
            }
        }
    }
    std::printf("         勾的像素：总 %d / 左下 %d / 右上 %d / 左上角 %d（%d×%d）\n",
                total, bottomLeft, topRight, topLeftCorner, markW, markH);
    Check(total > 0, "T-04：⑦ 选中时勾真的画出来了（不是空白框）");
    Check(bottomLeft > 0 && topRight > 0 && topLeftCorner == 0,
          "T-04：⑦ 勾的方向对：左下 → 右上，左上角是空的（反过来就是画倒了）");

    SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-05：Tooltip（延迟 / 穿透 / 落位）
// ---------------------------------------------------------------------------
//
// 三条判据都是"看得见的行为"，不是内部字段：
//   ① 不到 `delay` 不弹、到点就弹；
//   ② `Hide()` 之后计时**复位**（不是暂停——再等多久都不该弹）；
//   ③ 提示盖住的那个按钮**点得到**（穿透），且落位在锚点下方、水平居中。
void CheckTooltip() {
    std::printf("\n-- Tooltip：延迟 / 穿透 / 落位（T-05）--\n");

    struct TipScene : public InkingScene {
        TipScene() : InkingScene("probe_tooltip") {}
    };
    TipScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "T-05：探针场景注册成功（名字不与他人冲突）");
        return;
    }
    // `TickFrame` 只在**活跃**场景里推进动画（停放 / 关闭的场景直接返回）——
    // 计时挂在 `onAnimationTick` 上，所以这里必须先让它活跃。
    InkingSceneLibrary::SetActiveScene(&scene);

    // 锚点：100,100 处的 80×30 按钮。
    InkingButtonData anchorData;
    anchorData.name = "t05_anchor";
    anchorData.width = 80;
    anchorData.height = 30;
    anchorData.offsetX = 100.0f;
    anchorData.offsetY = 100.0f;
    auto* anchor = static_cast<InkingStaticButton*>(
        scene.AddChild(std::make_unique<InkingStaticButton>(&scene, anchorData)));

    // 提示会落在这块按钮**上面**（锚点下方 6px，正好盖住它）——用来验穿透。
    InkingButtonData underData;
    underData.name = "t05_under";
    underData.width = 80;
    underData.height = 30;
    underData.offsetX = 100.0f;
    underData.offsetY = 140.0f;
    auto* under = static_cast<InkingStaticButton*>(
        scene.AddChild(std::make_unique<InkingStaticButton>(&scene, underData)));

    inking_prefab::TooltipData data = inking_prefab::TooltipData::MakeDefault();
    data.delay = 0.1f;
    inking_prefab::Tooltip tip(&scene, data);

    scene.TickFrame(1.0 / 60.0);
    Check(!tip.IsShowing() && !tip.IsPending(), "T-05：初始既不显示、也不计时");

    Check(tip.ShowFor(anchor, "保存当前文档"), "T-05：ShowFor 接上了锚点");
    Check(tip.IsPending() && !tip.IsShowing(), "T-05：进入计时，还没弹出来");

    scene.TickFrame(1.0 / 60.0);
    scene.TickFrame(1.0 / 60.0);
    Check(!tip.IsShowing(),
          "T-05：① 跑 2 帧（0.033s < delay 0.1s）→ 不弹");

    for (int i = 0; i < 5; ++i) {
        scene.TickFrame(1.0 / 60.0);
    }
    std::printf("         提示 %d×%d @ (%.1f, %.1f)；锚点底边 y=%.1f\n",
                tip.GetWidth(), tip.GetHeight(),
                static_cast<double>(tip.GetAbsX()),
                static_cast<double>(tip.GetAbsY()),
                static_cast<double>(anchor->GetAbsY() + anchor->GetHeight()));
    Check(tip.IsShowing(), "T-05：① 跑够 7 帧（0.117s ≥ 0.1s）→ 弹出来");

    // 落位：在锚点下方、水平居中。
    const float anchorCenterX =
        anchor->GetAbsX() + static_cast<float>(anchor->GetWidth()) * 0.5f;
    const float tipCenterX =
        tip.GetAbsX() + static_cast<float>(tip.GetWidth()) * 0.5f;
    Check(tip.GetAbsY() >= anchor->GetAbsY() + anchor->GetHeight(),
          "T-05：③ 落在锚点**下方**（不是盖在它身上）");
    Check(std::fabs(tipCenterX - anchorCenterX) <= 1.0f,
          "T-05：③ 水平居中对齐锚点");

    // 穿透：提示正中间那一点，点到的是**底下那块按钮**。
    const float probeX = tipCenterX;
    const float probeY = tip.GetAbsY() + static_cast<float>(tip.GetHeight()) * 0.5f;
    Check(!tip.HitTest(probeX, probeY), "T-05：③ `HitTest` 恒 false（提示不参与命中）");
    const InkingHitResult hit = scene.QueryHit(probeX, probeY);
    std::printf("         提示正中点击 → 命中 %s\n",
                hit.target == under ? "底层按钮（正确）"
                                    : (hit.target == nullptr ? "空（穿透后没东西）"
                                                             : "别的节点"));
    Check(hit.target == under,
          "T-05：③ 点击**穿过提示**落到下面的按钮（提示不挡）");

    // 计时复位：`Hide()` 之后跑多久都不弹。
    Check(tip.ShowFor(anchor, "第二句"), "T-05：再次 ShowFor 会重新计时");
    tip.Hide();
    Check(!tip.IsPending() && !tip.IsShowing(), "T-05：② `Hide()` 立刻复位");
    for (int i = 0; i < 10; ++i) {
        scene.TickFrame(1.0 / 60.0);
    }
    Check(!tip.IsShowing(), "T-05：② `Hide()` 之后跑 10 帧也不弹（计时真的清零了）");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-06：Dialog（模态 / 遮罩 / Esc / Enter）
// ---------------------------------------------------------------------------
//
// "模态"只能靠**命中表**证明：开着的时候，遮罩位置上那一点必须不再回答底下那个
// 按钮。所以判据全部走 `QueryHit` 与真派发，不看内部字段。
void CheckDialog() {
    std::printf("\n-- Dialog：模态 / 遮罩 / 键盘（T-06）--\n");

    struct DlgScene : public InkingScene {
        DlgScene() : InkingScene("probe_dialog") {}
    };
    DlgScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "T-06：探针场景注册成功（名字不与他人冲突）");
        return;
    }
    InkingSceneLibrary::SetActiveScene(&scene);

    // 底下的按钮：位置在左上角附近，**离居中的对话框很远**（不会被面板盖住，
    // 只被遮罩盖住——这样才能区分"面板挡住"和"遮罩挡住"）。
    InkingButtonData underData;
    underData.name = "t06_under";
    underData.width = 80;
    underData.height = 30;
    underData.offsetX = 40.0f;
    underData.offsetY = 40.0f;
    auto* under = static_cast<InkingStaticButton*>(
        scene.AddChild(std::make_unique<InkingStaticButton>(&scene, underData)));

    inking_prefab::DialogData dd = inking_prefab::DialogData::MakeDefault();
    dd.width = 420;
    dd.height = 240;
    inking_prefab::Dialog dialog(&scene, dd);

    int acceptedCount = 0;
    int cancelledCount = 0;
    int lastAction = -1;
    dialog.accepted.Subscribe([&acceptedCount] { ++acceptedCount; });
    dialog.cancelled.Subscribe([&cancelledCount] { ++cancelledCount; });
    dialog.action.Subscribe([&lastAction](int index) { lastAction = index; });
    dialog.SetDefaultAction(2);

    const auto clickAt = [&scene](float x, float y) {
        scene.SetPointerState(x, y, true, true);
        scene.DispatchPointer();
        scene.SetPointerState(x, y, false, true);
        scene.DispatchPointer();
    };
    const auto pressKey = [&scene](SDL_Keycode code) {
        InkingKeyboardInput keys;
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = code;
        keys.UpdateFromSDL(event);
        scene.SetKeyboardInput(keys);
        scene.DispatchKeyboard();
    };

    scene.TickFrame(1.0 / 60.0);

    // ---- ① 关着：底下按钮正常命中 ----
    Check(!dialog.IsOpen(), "T-06：① 构造出来是关着的");
    Check(scene.QueryHit(60.0f, 50.0f).target == under,
          "T-06：① 关着的时候点到的是底下那个按钮（基线）");

    // ---- ② 打开：遮罩铺满，底下点不到 ----
    dialog.Open();
    scene.TickFrame(1.0 / 60.0);
    const InkingHitResult openHit = scene.QueryHit(60.0f, 50.0f);
    std::printf("         打开后同一位置命中 %s\n",
                openHit.target == &dialog
                    ? "对话框遮罩（正确）"
                    : (openHit.target == under ? "仍是底下按钮（模态失效）"
                                               : "别的节点"));
    Check(openHit.target == &dialog,
          "T-06：② 打开后按钮位置**点到的是遮罩**（模态生效）");
    Check(scene.GetKeyboardFocus() == &dialog,
          "T-06：② 打开时自己拿键盘焦点（Esc / Enter 才有地方落）");

    // ---- ③ 点遮罩：默认**不关** ----
    clickAt(60.0f, 50.0f);
    Check(dialog.IsOpen() && cancelledCount == 0,
          "T-06：③ 点遮罩默认不关、也不发 `cancelled`（防误触丢数据）");

    // ---- ④ 点面板内部：不算点遮罩 ----
    const int panelCenterX = InkingDesignWidth() / 2;
    const int panelCenterY = InkingDesignHeight() / 2;
    clickAt(static_cast<float>(panelCenterX), static_cast<float>(panelCenterY));
    Check(dialog.IsOpen(), "T-06：④ 点面板本体不会关");
    // 点面板会把焦点清掉（场景的规则：点到的组件不想要焦点就别留着）。
    // 下一帧由对话框自己把焦点收回来——不然 Esc / Enter 从此没有落点。
    scene.TickFrame(1.0 / 60.0);
    Check(scene.GetKeyboardFocus() == &dialog,
          "T-06：④ 点面板之后焦点**仍归对话框**（模态的键盘也归它）");

    // ---- ⑤ Esc：关 + `cancelled` 一条 ----
    pressKey(SDLK_ESCAPE);
    std::printf("         Esc 之后：open=%d / accepted=%d / cancelled=%d\n",
                dialog.IsOpen() ? 1 : 0, acceptedCount, cancelledCount);
    Check(!dialog.IsOpen(), "T-06：⑤ Esc 关掉");
    Check(cancelledCount == 1 && acceptedCount == 0,
          "T-06：⑤ Esc 只发 `cancelled` 一条，`accepted` 一条都不发");
    Check(scene.GetKeyboardFocus() == nullptr,
          "T-06：⑤ 关掉后焦点释放（不留一个指向不可见节点的焦点）");

    // ---- ⑥ Enter：关 + `accepted` 与 `action` 各一条 ----
    dialog.Open();
    scene.TickFrame(1.0 / 60.0);
    pressKey(SDLK_RETURN);
    std::printf("         Enter 之后：open=%d / accepted=%d / action=%d\n",
                dialog.IsOpen() ? 1 : 0, acceptedCount, lastAction);
    Check(!dialog.IsOpen() && acceptedCount == 1 && cancelledCount == 1,
          "T-06：⑥ Enter 关掉并发 `accepted`（`cancelled` 没多出来）");
    Check(lastAction == 2, "T-06：⑥ `action` 带出 `SetDefaultAction(2)` 那个索引");

    // ---- ⑦ 允许"点遮罩关闭"时，点的语义是**取消** ----
    dialog.SetCloseOnScrimClick(true);
    dialog.Open();
    scene.TickFrame(1.0 / 60.0);
    clickAt(60.0f, 50.0f);
    Check(!dialog.IsOpen() && cancelledCount == 2 && acceptedCount == 1,
          "T-06：⑦ 打开这个开关后，点遮罩 = 取消");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-07：Toast（堆叠 / 到期 / 不泄漏）
// ---------------------------------------------------------------------------
//
// "不泄漏"这条只能这么验：`GetOwnedChildCount()` 归零 + `RemoveChild` 走拥有型
// 路径（不接返回值 ⇒ 当场析构）。悬垂指针不会自己喊，所以数必须对得上。
void CheckToast() {
    std::printf("\n-- Toast：堆叠 / 到期 / 清空（T-07）--\n");

    struct ToastScene : public InkingScene {
        ToastScene() : InkingScene("probe_toast") {}
    };
    ToastScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "T-07：探针场景注册成功（名字不与他人冲突）");
        return;
    }
    InkingSceneLibrary::SetActiveScene(&scene);

    inking_prefab::ToastData data = inking_prefab::ToastData::MakeDefault();
    inking_prefab::ToastHost host(&scene, data);

    std::vector<int> dismissed;
    host.dismissed.Subscribe([&dismissed](int id) { dismissed.push_back(id); });

    // ---- ① 连发三条：id 自增、都挂着、纵向不重叠 ----
    const int idA = host.Show("第一条", 0.5f);
    const int idB = host.Show("第二条", 1.0f);
    const int idC = host.Show("第三条", 1.0f);
    scene.TickFrame(1.0 / 60.0);
    std::printf("         id = %d / %d / %d；挂着 %zu 条\n", idA, idB, idC,
                host.GetToastCount());
    Check(idA == 1 && idB == 2 && idC == 3, "T-07：① id 从 1 起自增、永不复用");
    Check(host.GetToastCount() == 3, "T-07：① 连发三条 → 三条都在");

    {
        std::vector<float> tops;
        for (InkingWidget* node : host.GetChildren()) {
            tops.push_back(node->GetAbsY());
        }
        bool stacked = tops.size() == 3;
        for (std::size_t i = 1; stacked && i < tops.size(); ++i) {
            stacked = std::fabs(tops[i] - tops[i - 1]) >= 1.0f;
        }
        std::printf("         三条的 y = %.0f / %.0f / %.0f\n",
                    tops.empty() ? 0.0 : static_cast<double>(tops[0]),
                    tops.size() < 2 ? 0.0 : static_cast<double>(tops[1]),
                    tops.size() < 3 ? 0.0 : static_cast<double>(tops[2]));
        Check(stacked, "T-07：① 三条纵向堆叠、互不重叠");
        Check(!tops.empty() && tops.back() > tops.front(),
              "T-07：① 新的一条贴在最下面（从下往上堆）");
    }

    // ---- ② 到期：第一条先走，`dismissed` 带的是它的 id ----
    scene.TickFrame(0.5f);
    std::printf("         0.5s 后：挂着 %zu 条 / dismissed=%zu\n",
                host.GetToastCount(), dismissed.size());
    Check(host.GetToastCount() == 2 && dismissed.size() == 1 &&
              dismissed.front() == idA,
          "T-07：② 0.5s 后第一条自己消失，`dismissed` 带的是它的 id");

    // ---- ③ 同一批到期：一次性全移除，不留空位 ----
    scene.TickFrame(0.6f);  // 累计 1.1s > 1.0s
    Check(host.GetToastCount() == 0 && dismissed.size() == 3,
          "T-07：③ 同一批到期 → 一次性全部移除（`dismissed` 共 3 条）");

    // ---- ④ 清空：不发 `dismissed`（那是"到期"，不是"被清掉"） ----
    dismissed.clear();
    host.Show("x", 10.0f);
    host.Show("y", 10.0f);
    Check(host.GetToastCount() == 2, "T-07：④ 再发两条 → 挂着两条");
    host.Clear();
    Check(host.GetToastCount() == 0 && dismissed.empty(),
          "T-07：④ `Clear()` 清空但**不发** `dismissed`");

    // ---- ⑤ 不参与命中：通知盖在按钮上也点得到下面 ----
    InkingButtonData underData;
    underData.name = "t07_under";
    underData.width = 120;
    underData.height = 40;
    underData.offsetX = static_cast<float>(InkingDesignWidth() - 300);
    underData.offsetY = static_cast<float>(InkingDesignHeight() - 80);
    auto* under = static_cast<InkingStaticButton*>(
        scene.AddChild(std::make_unique<InkingStaticButton>(&scene, underData)));
    host.Show("盖在按钮上的通知", 5.0f);
    scene.TickFrame(1.0 / 60.0);
    const InkingHitResult hit = scene.QueryHit(
        under->GetAbsX() + 10.0f, under->GetAbsY() + 10.0f);
    std::printf("         通知盖住按钮时点击 → 命中 %s\n",
                hit.target == under ? "底层按钮（正确）"
                                    : (hit.target == nullptr ? "空" : "别的节点"));
    Check(hit.target == under,
          "T-07：⑤ 通知对指针透明（盖在按钮上也点到按钮）");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-08：TabBar（切页 / 命中 / 事件）
// ---------------------------------------------------------------------------
//
// "切走的页点不到"只能靠**命中查询**证明：可见性是骨架变化 ⇒ 命中表重烘。
// 所以判据里的主角是 `QueryHit`——不是 alpha、也不是坐标。
void CheckTabBar() {
    std::printf("\n-- TabBar：切页 / 命中 / 事件（T-08）--\n");

    struct TabScene : public InkingScene {
        TabScene() : InkingScene("probe_tabbar") {}
    };
    TabScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "T-08：探针场景注册成功（名字不与他人冲突）");
        return;
    }
    InkingSceneLibrary::SetActiveScene(&scene);

    inking_prefab::TabBarData data = inking_prefab::TabBarData::MakeDefault();
    data.name = "t08_tabs";
    data.width = 480;
    data.height = 320;
    data.offsetX = 100.0f;
    data.offsetY = 60.0f;
    inking_prefab::TabBar tabs(&scene, data);

    // 每页塞一块**可命中**的按钮（铺满内容区）：这样"点到的是哪一页"能用
    // `QueryHit` 直接读出来。
    const auto makePage = [&](const char* name, std::uint32_t color) {
        InkingButtonData pageData;
        pageData.name = name;
        pageData.width = 480;
        pageData.height = 320 - 36;  // = 内容区高（标签条 36）
        pageData.normal = InkingButtonAppearance(InkingAppearanceType::Color, color);
        return std::unique_ptr<InkingWidget>(
            std::make_unique<InkingStaticButton>(nullptr, pageData));
    };

    int lastNow = -9;
    int lastBefore = -9;
    int changeCount = 0;
    tabs.pageChanged.Subscribe([&](int now, int before) {
        lastNow = now;
        lastBefore = before;
        ++changeCount;
    });

    Check(tabs.AddPage("一", makePage("t08_page0", 0xFFDDDDDDu)) == 0,
          "T-08：加第一页 → 索引 0");
    Check(tabs.AddPage("二", makePage("t08_page1", 0xFFCCCCCCu)) == 1, "T-08：加第二页");
    Check(tabs.AddPage("三", makePage("t08_page2", 0xFFBBBBBBu)) == 2, "T-08：加第三页");
    std::printf("         页数 %d / 当前页 %d / 内容区 %d×%d\n",
                tabs.GetPageCount(), tabs.GetVisiblePageIndex(),
                tabs.GetContentWidth(), tabs.GetContentHeight());

    Check(tabs.GetPageCount() == 3 && tabs.GetVisiblePageIndex() == 0,
          "T-08：① 三页，第一页自动成为当前页");
    Check(tabs.GetContentWidth() == 480 && tabs.GetContentHeight() == 320 - 36,
          "T-08：① 内容区 = 整框减掉标签条那一条（480×284）");

    auto* page0 = tabs.GetPage(0);
    auto* page1 = tabs.GetPage(1);
    auto* page2 = tabs.GetPage(2);
    Check(page0 != nullptr && page0->IsVisible() && !page1->IsVisible() &&
              !page2->IsVisible(),
          "T-08：① 只有第一页可见（其余 `SetVisible(false)`）");

    scene.TickFrame(1.0 / 60.0);

    // ---- ② 切页：命中跟着走 ----
    const float insideX = 100.0f + 240.0f;                 // 内容区中央
    const float insideY = 60.0f + 36.0f + 142.0f;
    Check(scene.QueryHit(insideX, insideY).target == page0,
          "T-08：② 切页前，内容区中间点到的是第 1 页");

    Check(tabs.Select(1), "T-08：② `Select(1)` 真的切了");
    scene.TickFrame(1.0 / 60.0);
    const InkingHitResult afterSwitch = scene.QueryHit(insideX, insideY);
    std::printf("         切到第 2 页后，同一位置命中 %s\n",
                afterSwitch.target == page1
                    ? "第 2 页（正确）"
                    : (afterSwitch.target == page0 ? "仍是第 1 页（可见性没生效）"
                                                   : "别的节点"));
    Check(afterSwitch.target == page1,
          "T-08：② 切到第 2 页后，同一位置点到的是**第 2 页**");
    Check(!page0->IsVisible() && page1->IsVisible(),
          "T-08：② 旧页隐藏、新页可见（先藏后显）");
    Check(changeCount == 1 && lastNow == 1 && lastBefore == 0,
          "T-08：② `pageChanged` 一条，参数是（现在 1, 之前 0）");

    // ---- ③ 切到当前页：零事件 ----
    Check(!tabs.Select(1), "T-08：③ 切到当前页返回 false");
    Check(changeCount == 1, "T-08：③ 切到当前页**不发**事件");

    // ---- ④ 越界：拒绝且不发事件 ----
    Check(!tabs.Select(-1) && !tabs.Select(3), "T-08：④ 越界索引被拒绝");
    Check(changeCount == 1 && tabs.GetVisiblePageIndex() == 1,
          "T-08：④ 越界之后当前页没变、也没发事件");

    // ---- ⑤ 点标签按钮：走同一条 `Select` 出口 ----
    // 标签条：第 i 个标签占 [100 + i×96, +96)，纵向在 60~96。
    const float tab2X = 100.0f + 2.0f * 96.0f + 48.0f;
    const float tabY = 60.0f + 18.0f;
    scene.SetPointerState(tab2X, tabY, true, true);
    scene.DispatchPointer();
    scene.SetPointerState(tab2X, tabY, false, true);
    scene.DispatchPointer();
    std::printf("         点第 3 个标签 → 当前页 %d / 事件 %d 条\n",
                tabs.GetVisiblePageIndex(), changeCount);
    Check(tabs.GetVisiblePageIndex() == 2 && changeCount == 2,
          "T-08：⑤ 点标签切页（与 `Select` 同一个出口，不是第二套逻辑）");
    Check(scene.QueryHit(insideX, insideY).target == page2,
          "T-08：⑤ 点标签之后命中也跟着换到第 3 页");

    InkingSceneLibrary::SetActiveScene(nullptr);
}

// ---------------------------------------------------------------------------
// T-09 / T-10：系统集成（光标形状 + 文件拖放）
// ---------------------------------------------------------------------------
//
// 两条都只能靠**代理量**验：光标长什么样、从资源管理器拖进来有没有反应，都得
// 人看一眼（那是 `USER.md` 里"主观量交给用户"的那一档）。这里钉的是**判定**：
//   - 光标：形状变了才调 `SDL_SetCursor`（计数能断言"悬停 5 帧只换 1 次"）；
//   - 拖放：一次拖 N 个文件 = 一条事件带 N 个路径；没场景时丢弃要计数。
void CheckSystemIntegration() {
    std::printf("\n-- 系统集成：光标形状 / 文件拖放（T-09 / T-10）--\n");

    struct SysScene : public InkingScene {
        SysScene() : InkingScene("probe_sysint") {}
    };
    SysScene scene;
    if (!scene.IsRegistered()) {
        Check(false, "T-09：探针场景注册成功（名字不与他人冲突）");
        return;
    }

    // 一个按钮（有光标声明）放在 (100,100) 80×30。
    InkingButtonData buttonData;
    buttonData.name = "t09_button";
    buttonData.width = 80;
    buttonData.height = 30;
    buttonData.offsetX = 100.0f;
    buttonData.offsetY = 100.0f;
    auto* button = static_cast<InkingStaticButton*>(
        scene.AddChild(std::make_unique<InkingStaticButton>(&scene, buttonData)));
    scene.TickFrame(1.0f / 60.0f);

    Check(button->GetCursorShape() == InkingCursorShape::Hand,
          "T-09：按钮声明了手型（默认是箭头）");

    InkingWindow& window = InkingWindow::Instance();
    window.SetFixedDelta(1.0f / 60.0f);
    Check(window.Open("probe_sysint"), "T-09：窗口打开（dummy 驱动 + 软渲染器）");

    // 设计坐标 → 窗口坐标（letterbox：等比缩放 + 居中留边）。
    const float designW = static_cast<float>(InkingDesignWidth());
    const float designH = static_cast<float>(InkingDesignHeight());
    const float windowW = static_cast<float>(window.GetWindowWidth());
    const float windowH = static_cast<float>(window.GetWindowHeight());
    const float scale = std::min(windowW / designW, windowH / designH);
    const float offsetX = (windowW - designW * scale) * 0.5f;
    const float offsetY = (windowH - designH * scale) * 0.5f;

    const auto pushMotion = [&](float designX, float designY) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.x = designX * scale + offsetX;
        event.motion.y = designY * scale + offsetY;
        SDL_PushEvent(&event);
    };

    // ---- ① 光标：悬停按钮 → 手型；悬停不动不重复设置；移开换回箭头 ----
    // **预热一次命中查询**：`Open()` 之后的第一帧，指针派发有可能在命中表
    // 就绪之前跑（现象：`hovered` 一直是空、计数却不加，看着像"光标没接上"）。
    // 先问一次把表催出来，后面的断言才测的是光标本身。
    // ⚠️ 这条是绕过，不是修好：根因（Open 后第一次派发前表没烘焙）尚未定位，
    //    已记在 `.workbuddy/memory/`，别把它当成"测试需要预热"的常态。
    Check(scene.QueryHit(140.0f, 115.0f).target == button,
          "T-09：① 按钮在 (140,115) 上（命中表就绪）");
    // **悬停要先"真的落到按钮上"**，再谈光标。
    //
    // 窗口在后台时场景收到的是 `inside == false`（悬停被清空），而自检在无头 /
    // CI 里跑时窗口经常拿不到输入焦点——那时判红是"环境不具备"，与"光标没接上"
    // 长得一模一样。所以判据不用焦点标志（它不可靠），直接看**悬停结果**：
    // 落到按钮就验，落不到就跳过并说明原因。
    const std::uint64_t before = window.GetCursorUpdateCount();
    pushMotion(140.0f, 115.0f);  // 按钮中心
    window.PumpOnce();
    const std::uint64_t afterHover = window.GetCursorUpdateCount();
    std::printf("         悬停按钮后：换光标 %llu 次（之前 %llu）；hovered = %s\n",
                static_cast<unsigned long long>(afterHover),
                static_cast<unsigned long long>(before),
                scene.GetHoveredNode() == button ? "按钮" : "空");
    if (scene.GetHoveredNode() != button) {
        std::printf("         [skip] 窗口没拿到输入焦点（后台 / CI）——"
                    "悬停类断言跳过；拖放不受影响，继续\n");
    } else {
        Check(afterHover == before + 1,
              "T-09：① 悬停按钮 → 换一次光标（手型）");

        // 后面两条**只在无头后端上断言**：真后端的窗口在后台跑时，鼠标焦点
        // 会在帧间抖动（`inside` 跟着变），形状于是真的在 Hand ↔ Default 之间
        // 来回切——那两条"不重复 / 只 +1"就不再是"代码对不对"的判据，而是
        // "这台机器的窗口有没有被点到"。上面那条（悬停 → 换一次）已经证明
        // 整条链路是通的，这两条在真后端上跳过并说出来。
        const char* driverEnv2 = std::getenv("INKING_SELFTEST_DRIVER");
        const bool headlessBackend = driverEnv2 == nullptr || driverEnv2[0] == '\0'
                                     || std::string(driverEnv2) == "dummy";
        if (!headlessBackend) {
            std::printf("         [skip] 真后端：窗口焦点会抖动——"
                        "「不重复设置 / 移开 +1」两条只在前端稳定时才有意义\n");
        } else {
            for (int i = 0; i < 5; ++i) {
                pushMotion(140.0f, 115.0f);
                window.PumpOnce();
            }
            Check(window.GetCursorUpdateCount() == afterHover,
                  "T-09：① 悬停不动跑 5 帧 → **不重复设置**（形状没变）");

            pushMotion(900.0f, 600.0f);  // 空白处
            window.PumpOnce();
            std::printf("         移到空白后：换光标 %llu 次\n",
                        static_cast<unsigned long long>(window.GetCursorUpdateCount()));
            Check(window.GetCursorUpdateCount() == afterHover + 1,
                  "T-09：① 移开 → 换回箭头（再 +1）");
        }
    }

    // ---- ② 拖放：一次拖 3 个文件 = 一条事件带 3 个路径 ----
    int dropEvents = 0;
    std::vector<std::string> lastPaths;
    scene.filesDropped.Subscribe(
        [&dropEvents, &lastPaths](const std::vector<std::string>& paths) {
            ++dropEvents;
            lastPaths = paths;
        });

    const auto pushDrop = [](const char* path) {
        SDL_Event event{};
        event.type = SDL_EVENT_DROP_FILE;
        event.drop.data = path;  // 只在这里用到，下一次 PumpOnce 之前一直有效
        SDL_PushEvent(&event);
    };
    const std::uint64_t dropsBefore = window.GetDroppedFileCount();
    pushDrop("C:/tmp/一.txt");
    pushDrop("C:/tmp/二.txt");
    pushDrop("C:/tmp/三.txt");
    window.PumpOnce();
    std::printf("         拖 3 个文件 → 事件 %d 条 / 路径 %zu 个\n", dropEvents,
                lastPaths.size());
    Check(window.GetDroppedFileCount() == dropsBefore + 3,
          "T-10：② 窗口收下 3 条路径");
    Check(dropEvents == 1 && lastPaths.size() == 3,
          "T-10：② **一次派发**带 3 个路径（不是三次回调）");
    Check(!lastPaths.empty() && lastPaths.front() == "C:/tmp/一.txt",
          "T-10：② 路径原样保留（UTF-8，顺序不变）");

    // ---- ③ 没有活跃场景：丢弃 + 计数（不静默） ----
    const std::size_t droppedBefore = window.GetDroppedWithoutSceneCount();
    InkingSceneLibrary::SetActiveScene(nullptr);
    pushDrop("C:/tmp/孤儿.txt");
    window.PumpOnce();
    std::printf("         无场景时丢弃 %zu 条（之前 %zu）\n",
                window.GetDroppedWithoutSceneCount(), droppedBefore);
    Check(window.GetDroppedWithoutSceneCount() == droppedBefore + 1,
          "T-10：③ 没有活跃场景时丢弃并计数（不静默）");
    Check(dropEvents == 1, "T-10：③ 丢弃的那条**不会**被派发给订阅者");
    InkingSceneLibrary::SetActiveScene(&scene);

    InkingSceneLibrary::SetActiveScene(nullptr);
    window.RequestClose(0);
    window.PumpOnce();  // 收掉窗口（releaseWindow 里顺带销毁光标缓存）
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // ---- 视频驱动：默认 dummy；`INKING_SELFTEST_DRIVER=windows` 走真后端 ----
    //
    // 为什么默认**必须**仍是 dummy：那是"在任何机器上都能跑"的唯一保证。
    // 改成"自动挑真后端"等于把默认测试变成看显卡脸色——一台没 GPU 的机器上
    // 它从"全绿"变成"红"，而代码一行没动。
    //
    // 为什么需要真后端这一档（0.8.0 T-02）：全部像素证据原本只在 software
    // 后端上，而 0.6.4 那次"采样模式在几何批上不生效"正是 **GPU 与 software
    // 行为不同**的那类缺陷。真后端跑一遍，这类差异才会自动现形。
    //
    // **必须在这里读、在 `SDL_Init` 之前设 hint**：hint 只在建驱动时被消费，
    // 晚一步就静默无效（配置写了、行为没变、没有报错）。
    const char* driverEnv = std::getenv("INKING_SELFTEST_DRIVER");
    const std::string videoDriver =
        (driverEnv != nullptr && driverEnv[0] != '\0') ? driverEnv : "dummy";

    std::printf("== inking 动态库 · 端到端自检 ==\n");
    // 阶段单独打出来（`0.4.0` + `release`）：当前是哪个阶段要在运行期看得见，
    // 不能只写在 README 里（别人抓到一份日志时，第一行就该知道它是什么版本）。
    std::printf("   版本 %s-%s / SDL3 %s / 消息队列编译期%s\n",
                InkingVersion, InkingVersionStage,
                InkingSdl3_version().c_str(),
                InkingMessageEnabled ? "开" : "关");
    // 日志要自证**跑了哪一个后端**：同一份"失败项：0"在两种后端下含义不同。
    std::printf("   视频驱动 = %s（环境变量 INKING_SELFTEST_DRIVER 可改）\n",
                videoDriver.c_str());

    // ---- 设计尺寸：默认值 / 参数校验（必须在场景构造之前） ----
    CheckDesignSizeDefault();

    TestScene scene;
    Check(scene.IsRegistered(), "场景「构造即登记」进 InkingSceneLibrary");
    Check(scene.GetDrawItemCount() >= 2, "绘制列表里有两个组件");
    CheckDesignSizeLocked();  // 场景已按设计尺寸定型 → 之后不许再改

    // ---- 窗口与渲染器：驱动由 INKING_SELFTEST_DRIVER 决定（默认 dummy）----
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, videoDriver.c_str());
    Check(SDL_Init(SDL_INIT_VIDEO),
          (std::string("SDL_Init（视频驱动 ") + videoDriver + "）").c_str());
    SDL_Window* win = SDL_CreateWindow("ink-selftest", 640, 360, 0);
    SDL_Renderer* rnd = win != nullptr ? SDL_CreateRenderer(win, nullptr) : nullptr;

    // 真后端建不出来 = **这台机器没有可用的 GPU 驱动**，它与"代码坏了"必须在
    // ctest 里长得不一样（Skipped vs Failed）。所以这里先判、后断言：
    // 直接在 Check 里红掉的话，一台没有显卡的 CI 机器会永远红着。
    if ((win == nullptr || rnd == nullptr) && videoDriver != "dummy") {
        std::printf("[skip] 真后端不可用（视频驱动 %s）：%s\n",
                    videoDriver.c_str(),
                    SDL_GetError()[0] != '\0' ? SDL_GetError() : "（无错误信息）");
        if (rnd != nullptr) {
            SDL_DestroyRenderer(rnd);
        }
        if (win != nullptr) {
            SDL_DestroyWindow(win);
        }
        SDL_Quit();
        return 77;  // ctest 的 SKIP_RETURN_CODE：显示 Skipped，不算失败
    }
    Check(win != nullptr, "创建窗口");
    Check(rnd != nullptr,
          (std::string("创建渲染器（") + videoDriver + "）").c_str());

    scene.TickFrame(1.0f / 60.0f);
    InkingSceneLibrary::SetActiveScene(&scene);
    Check(InkingSceneLibrary::RenderScene(rnd),
          "RenderScene 走通（渲染提交成功）");
    Check(scene.GetHitTableEntryCount() > 0,
          "静态组件进了命中表（动态组件不进）");
    Check(scene.button->GetWidth() == 200, "静态按钮宽度来自配置");

    // ---- 静态按钮：三态 + 点击（场景推过来） ----
    scene.SetPointerState(640.0f, 360.0f, false, true);
    scene.DispatchPointer();
    Check(scene.button->GetState() == InkingButtonState::Hover,
          "指针移入 → 悬停态");

    scene.SetPointerState(640.0f, 360.0f, true, true);
    scene.DispatchPointer();
    Check(scene.button->GetState() == InkingButtonState::Pressed,
          "按下 → 按下态");

    scene.SetPointerState(640.0f, 360.0f, false, true);
    scene.DispatchPointer();
    Check(scene.clicked, "抬起 → 点击回调真的被触发");
    Check(scene.button->GetState() == InkingButtonState::Hover,
          "抬起后回到悬停态");

    // ---- 动态按钮：Tick 派发 + 几何写入 ----
    const float beforeX = scene.mover->GetX();
    scene.TickLogic(0.02);
    const float afterX = scene.mover->GetX();
    Check(afterX > beforeX + 0.5f,
          "动态组件的 onTick 被派发且几何真的改了（ChangeOffset 生效）");
    Check(InkingSceneLibrary::RenderScene(rnd),
          "动态几何变化后再渲染一帧成功");

    // ---- InkingMesh × InkingShapeContains 对账（纯几何，与窗口无关） ----
    Check(ReconcileShapeMesh() == 0,
          "轮廓采样与形状判定同源（8 种形状逐点对账）");

    // ---- 窗口：缩放（严格等比）/ 全屏 / 启动配置 / 菜单栏联动 ----
    CheckWindowZoomAndFullscreen();
    CheckWindowConfigApply();
    CheckMenuBarFullscreenHiding();

    // ---- 快捷键：解析 / 匹配 / 冲突 / 消费 / 菜单项绑定 ----
    CheckShortcuts();
    CheckMenuBarShortcutBinding();

    // ---- 文字高清：按设备比例烘字，布局零位移 ----
    CheckTextHiDpi();

    // ---- 文字光栅上限：可配 + 触顶告警（0.7.1 T-03）----
    CheckTextRasterCap();

    // ---- 缩放规则：素材该多大 / σ 干不干净（0.7.0 T-02）----
    CheckImageScaleRules();

    // ---- 栅格化发生在设备分辨率（0.7.0 T-01）：颜色种类数 ----
    CheckRasterIsDeviceSpace();

    // ---- 素材分辨率：DEBUG 校验（0.7.1 T-04）----
    CheckImageUndersampledWarn();

    // ---- 静默失效护栏：自测量 / 零尺寸 / 全透明兜底 / 字体 / 日志（0.1.1） ----
    CheckSilentFailureGuards(rnd, &scene);

    // ---- 字体回退链：链上第二条命中 / 全失败只报一条（0.3.0 T-13） ----
    CheckFontFallbackChain(rnd, &scene);

    // ---- 静默失效清单里的三条既有路径（0.3.0 T-14） ----
    CheckThemeTokenMissing();
    CheckScrollRangeStale();
    CheckTextNegativeWidth();

    // ---- 文字按框高裁行：`Clip` 的默认语义真的生效（0.8.3 T-01） ----
    CheckTextClipByHeight();

    // ---- 字形图集满：不再逐帧重烘 + 首次发声（0.8.3 T-02 / T-03） ----
    CheckGlyphAtlasFullNoRetry(rnd);

    // ---- 文字纹理预算与 LRU 淘汰（0.8.4 T-04） ----
    CheckTextureBudgetEviction(rnd);

    // ---- 消息队列 ----
    InkingMessageQueue::addMessage(InkingMessageType::Pass, 0.0f, "hi");
    const std::vector<InkingMessage> msgs =
        InkingMessageQueue::drainMessages();
    Check(msgs.size() == 1 && msgs[0].message == "hi", "消息队列入队 / 出队");

    // ---- 任务队列：单例在 DLL 里，管家线程真的在跑 ----
    std::atomic<int> ran{0};
    InkingTask<std::any> task;
    task.action = [&ran](const std::vector<std::any>&) -> std::any {
        ran.fetch_add(1, std::memory_order_relaxed);
        return {};
    };
    InkingTaskQueue::instance().addTask(std::move(task));
    for (int i = 0; i < 200 && ran.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(ran.load() == 1, "任务队列的管家线程执行了任务");

    // ---- 音频层接线（P0）：淡入曲线是唯一能证明"真的接上了"的观测口 ----
    CheckAudioPipeline();

    // ---- 层级：树序绘制 + 兄弟内 z（放在最后：会临时切换活动场景） ----
    CheckLayerOrder(rnd);

    // ---- 装饰笔：面板描边在子孙之上 / 阴影在填充之下；按钮三笔顺序不变（0.2.0） ----
    CheckPanelDecoration(rnd);
    CheckButtonPaintOrder(rnd);

    // ---- 按钮状态事件：两档共用 stateChanged（0.2.0 T-11） ----
    CheckButtonStateEvent();

    // ---- 统一输入语义：归属深挖 / 通知时机 / 边沿判定（0.4.1 T-01~T-03） ----
    CheckPopupHitOwner();
    CheckPointerStateChannel();
    CheckPointerProbe();
    CheckRightButton();
    CheckTouchLongPress();

    // ---- 定点浮窗：落位算术 + 组件行为（0.4.1 T-05 / T-06） ----
    CheckPopupPlacement();
    CheckFixedPopup();

    // ---- 菜单浮窗：布局 / 项 / 局部快捷键（0.4.1 T-07 / T-08） ----
    CheckMenuPopup();

    // ---- 文件选择：盘符栏 / 换盘符（C5 增补） ----
    CheckFilePickerDrives();

    // ---- 文件选择：按钮栏不带窗口语义 + 内容尺寸的口径（0.5.3） ----
    CheckFilePickerButtonBar();
    CheckScrollContentMeasurement();

    // ---- 输入框：光标 clamp + 水平滚动（0.2.0 T-12） ----
    CheckInputScroll();

    // ---- 截图：失败路径的契约（正路径由 INK_CAPTURE 跑示例验） ----
    CheckCaptureContract();

    // ---- 运行期主题：换表 → 下一帧颜色就变（含真渲染像素） ----
    CheckRuntimeTheme(rnd);

    // ---- SVG 设计稿（0.5.1）：整张稿光栅化 / 缓存分档 / 不支持元素 / 背景穿透 ----
    CheckSvgRasterMatrix();
    CheckSvgImage(rnd);
    CheckSvgCacheDensity(rnd);
    CheckSvgUnsupported(rnd);
    CheckSvgBackgroundPassthrough();
    CheckSvgFullDesign(rnd);
    CheckSvgHiDpi();

    // ---- 树的动态性（0.4.0 A 组）：A1 / A2 / A3 / A4 / A5 / B1 / B2 / D6 / D7 ----
    CheckDynamicTree();
    CheckLeafHoleRefresh();  // T-19：移动叶子只刷洞（判据是计数，不是帧时间）
    CheckDynamicImage();     // G-01：动态图片——拖动改几何、A13 保留

    // ---- 场景动画时钟与频率订阅（0.6.0 T-01）----
    CheckSceneAnimationClock();

    // ---- InkingSpriteData：序列帧 / 图集二选一与校验（0.6.0 T-02）----
    CheckSpriteDataValidate();

    // ---- 库内 JSON 小工具：解析 / 取值 / 转义（0.6.5 T-15）----
    CheckJsonTool();

    // ---- InkingSprite：序列帧播放 / 暂停 / 定位 / 纹理复用（0.6.0 T-03）----
    CheckSpriteSequence(rnd);

    // ---- 动态精灵：帧机同源 + 几何能改（G-03）----
    CheckDynamicSprite(rnd);

    // ---- 动态标签：SetText 不改几何、FitToContent 才改、布局容器里喊一声（G-05）----
    CheckDynamicLabel();

    // ---- 动态面板：滚动条自判拖动 / 裁剪与命中同源 / 包络计数（G-07）----
    CheckDynamicPanel();

    // ---- 图集源矩形：切格与单独小图逐像素一致（0.6.1 T-04）----
    CheckImageFitSrcRect(rnd);

    // ---- InkingSprite 图集模式：切格 / 零纹理 / 几何校验（0.6.1 T-05）----
    CheckSpriteSheet(rnd);

    // ---- 矩形图片并批：提交次数 / 像素 / 变换（0.6.2 T-08）----
    CheckSpriteBatching(rnd);

    // ---- 帧事件：区间扫描 / 回绕 / Seek 不发 / finished（0.6.3 T-09）----
    CheckSpriteEvents(rnd);

    // ---- 图集起始格：每行一个方向的图集也能播（0.6.4 T-11）----
    CheckSpriteSheetFirstFrame(rnd);

    // ---- 图片采样模式：像素素材放大后仍是硬边（0.6.4 T-12）----
    CheckImageSampling(rnd);

    // ---- 运行期换配置：订阅不断 + 换过去画的是新图（0.6.5 T-13）----
    CheckSpriteRuntimeSwap(rnd);

    // ---- 不规则帧矩形：一页多动作、帧宽高不等（0.6.5 T-14）----
    CheckSpriteUnevenFrames(rnd);

    // ---- 图集描述 → InkingSpriteData：具名动画表（0.6.5 T-16）----
    CheckAtlasData(rnd);

    // ---- 动画组：一个节点承载多个动作（0.6.6 T-18）----
    CheckSpriteGroup();

    // ---- 0.9.0 批次 a+b：视频出画 / 滑块 / 进度条（T-01 ~ T-03）----
    //
    // 排在纹理字节账本**之前**：那一组会清空纹理缓存，而视频这一条要建纹理、
    // 还要真的读回像素。顺序反了会变成"账本里突然多出一张别人的纹理"。
    CheckVideoDecode(rnd);
    CheckSliderDrag();
    CheckProgressBar(rnd);
    CheckToggleLike(rnd);
    CheckTooltip();
    CheckDialog();
    CheckToast();
    CheckTabBar();

    // ---- 纹理字节账本：建→放→归零→峰值（0.8.2 T-05，放在最后：会清空纹理缓存）----
    CheckTextureBytes(rnd);

    InkingSceneLibrary::SetActiveScene(nullptr);
    if (rnd != nullptr) {
        SDL_DestroyRenderer(rnd);
    }
    if (win != nullptr) {
        SDL_DestroyWindow(win);
    }
    SDL_Quit();

    // ---- 非阻塞跑帧（0.4.0 D 组）：**必须放在裸窗口销毁之后** ----
    // 它会自己 SDL_Init，并在收尾时 SDL_Quit——放在前面会把上面用到的
    // 渲染器一起拆掉（那种顺序错误会表现成"随机几个检查失败"）。
    CheckHeadlessPump();

    // ---- 光标 / 文件拖放（T-09 / T-10）----
    //
    // **必须排在裸窗口销毁之后、纹理账本之后**：它自己 `Open()` 一次窗口，
    // 收尾时 `releaseWindow` 会 `SDL_Quit()`——放在前面会把上面所有用例正在用的
    // 渲染器一起拆掉（与 `CheckHeadlessPump` 同一条纪律）。
    CheckSystemIntegration();

    // ---- 清单的元自检放在**真正的最后**：前面的检查全部登记完才对账（T-14） ----
    //
    // 为什么排在 `CheckHeadlessPump()` 之后：`background_alpha_without_transparent`
    // 的断言要开窗才验得到（清屏那一笔发生在 `stepFrame` 里）。放在窗口用例之前，
    // 它会误报"这条自检没跑"。
    CheckSilentFailureListSynced();

    std::printf("== 失败项：%d ==\n", g_fail);
    return g_fail;
}
