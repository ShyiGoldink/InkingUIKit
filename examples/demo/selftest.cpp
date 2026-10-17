// ---------------------------------------------------------------------------
// inking_demo_selftest —— **无头**端到端自检
//
// 与 `inking_demo` 的分工：
//   - `inking_demo`       开真窗口、跑主循环，给人看（需要显示器）；
//   - 本程序              **不开窗口**（SDL dummy 视频驱动 + 软件渲染器），
//                         把「库到底能不能用」变成一串可断言的结论，给机器看。
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
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTextRequest.h>
#include <dataStruct/InkingTransform.h>
#include <detail/MessageQueue.h>
#include <input/KeyboardInput.h>
#include <input/Shortcut.h>
#include <input/ShortcutRegistry.h>
#include <prefab/MenuBar.h>
#include <render/InkingBatch.h>
#include <render/InkingGlyphAtlas.h>
#include <render/InkingMesh.h>
#include <render/InkingText.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>
#include <thread/TaskQueue.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>

#include <any>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

/// 动态按钮：每逻辑步把自己往右推 10 像素——用来验"Tick 真的派发到组件"。
class Mover : public inking::InkingDynamicButton {
public:
    using inking::InkingDynamicButton::InkingDynamicButton;

protected:
    void onTick(float deltaSeconds) override {
        inking::InkingDynamicButton::onTick(deltaSeconds);  // 先收输入，再动几何
        ChangeOffset(10.0f, 0.0f);
    }
};

struct TestScene : public inking::InkingScene {
    TestScene() : inking::InkingScene("selftest") {
        inking::ButtonData bd;
        bd.name = "probe_button";
        bd.width = 200;
        bd.height = 60;
        bd.normal = inking::ButtonAppearance(inking::AppearanceType::Color,
                                             0xFF3F7FD6u);
        bd.label = "probe";
        bd.text.fontSize = 20.0f;
        bd.selfAnchor = inking::InkingChangeAnchor::Center;
        bd.traceAnchor = inking::InkingChangeAnchor::Center;

        button = static_cast<inking::InkingStaticButton*>(
            AddChild(std::make_unique<inking::InkingStaticButton>(this, bd)));
        button->SetOnClicked([this] { clicked = true; });

        // 动态按钮挪开摆：两个组件重叠时，**后注册的会正确地挡在上面**，
        // 那时静态按钮收不到 hover 是正确行为，不是故障。
        inking::ButtonData dd = bd;
        dd.name = "probe_mover";
        dd.offsetY = 200.0f;
        mover =
            static_cast<Mover*>(AddChild(std::make_unique<Mover>(this, dd)));
    }

    inking::InkingStaticButton* button = nullptr;
    Mover* mover = nullptr;
    bool clicked = false;
};

// ---------------------------------------------------------------------------
// InkingMesh × ShapeContains 对账
//
// 这两者是**同一个几何问题的两条实现**：`AppendContour` 决定"画出来的轮廓长
// 什么样"（填充与描边都用它），`SignedDistance` / `ShapeContains` 决定"点在不在
// 形状里"（命中、裁剪、包围盒都用它）。两边一旦分叉，症状就是"画的是圆角、
// 点到的是直角"——只在边角露头，人眼极难发现。
//
// 做法：把轮廓采样成一圈多边形，用**射线法**独立判一次内外，再和 SDF 的答案
// 逐点比。靠近边界的点跳过（|sdf| < 1.5px），那里两种判法本来就都有理；
// 只有"离边界很远还判反了"才计为分歧——那才是真的分叉。
// ---------------------------------------------------------------------------

/// 射线法：点是否在这一圈闭合多边形内（只用于对账，不参与任何绘制）。
bool PointInPolygon(const std::vector<inking::detail::OutlinePoint>& ring,
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
        inking::ShapeSpec shape;
    };
    const Case cases[] = {
        {"rect", inking::ShapeSpec::Rect()},
        {"roundedRect(24)", inking::ShapeSpec::RoundedRect(24.0f)},
        {"circle", inking::ShapeSpec::Circle()},
        {"ellipse", inking::ShapeSpec::Ellipse()},
        {"capsule", inking::ShapeSpec::Capsule()},
        {"polygon(6)", inking::ShapeSpec::Polygon(6, 0.0f)},
        {"star(5,0.5)", inking::ShapeSpec::Star(5, 0.5f)},
        {"superellipse(4)", inking::ShapeSpec::Superellipse(4.0f)},
    };

    constexpr int kWidth = 200;
    constexpr int kHeight = 120;
    constexpr int kCols = 60;
    constexpr int kRows = 36;

    int bad = 0;
    for (const Case& c : cases) {
        std::vector<inking::detail::OutlinePoint> ring;
        const float w = static_cast<float>(kWidth);
        const float h = static_cast<float>(kHeight);
        inking::detail::AppendContour(
            ring, c.shape, w, h,
            inking::detail::ContourSteps(c.shape, w, h), 0.0f);

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
                    inking::SignedDistance(c.shape, px, py, kWidth, kHeight);
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
    inking::InkingWindow& window = inking::InkingWindow::Instance();

    // ---- 档位表：运行时按设计尺寸 + 显示器可用区域生成 ----
    const std::vector<float> ladder = window.GetZoomLadder();
    bool ladderOk = !ladder.empty();
    for (std::size_t i = 0; i < ladder.size(); ++i) {
        if (ladder[i] < inking::InkingWindow::kMinZoom - 1e-3f
            || ladder[i] > inking::InkingWindow::kMaxZoom + 1e-3f) {
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
    Check(width == static_cast<int>(std::lround(inking::kDesignWidth * top))
              && height
                     == static_cast<int>(
                            std::lround(inking::kDesignHeight * top)),
          "窗口尺寸 = 设计尺寸 × 档位（整数像素）");

    // 严格等比的判据：两轴算出来的"实际倍率"之差，折算回设计像素 ≤ 1。
    const double zoomW =
        static_cast<double>(width) / static_cast<double>(inking::kDesignWidth);
    const double zoomH =
        static_cast<double>(height) / static_cast<double>(inking::kDesignHeight);
    Check(std::fabs(zoomW - zoomH) * inking::kDesignWidth <= 1.0,
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
// 菜单栏：全屏自动隐藏（订阅事件，不是轮询）
// ---------------------------------------------------------------------------

void CheckMenuBarFullscreenHiding() {
    inking::InkingWindow& window = inking::InkingWindow::Instance();
    inking::prefab::MenuBar bar(nullptr);

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
    const inking::Shortcut combo = inking::Shortcut::Parse("Ctrl+Shift+S", &ok);
    Check(ok && !combo.Empty(), "解析 Ctrl+Shift+S 成功");
    Check(combo.ToString() == "Ctrl+Shift+S", "ToString 与输入一致（往返稳定）");
    Check(combo.modifiers == (inking::Shortcut::kCtrl | inking::Shortcut::kShift),
          "修饰键位掩码正确");

    ok = true;
    const inking::Shortcut bogus = inking::Shortcut::Parse("Ctrl+Nope", &ok);
    Check(!ok && bogus.Empty(), "非法主键名解析失败且不返回半个快捷键");

    ok = true;
    Check(inking::Shortcut::Parse("Ctrl+", &ok).Empty() && !ok,
          "只有修饰键的串不算快捷键");

    // ---- 匹配：修饰键必须完全相等 ----
    inking::KeyEvent press{};
    press.key = 's';
    press.ctrl = true;
    press.shift = true;
    const inking::Shortcut save = inking::Shortcut::Parse("Ctrl+S");
    Check(!save.Matches(press), "Ctrl+S 不被 Ctrl+Shift+S 命中（严格匹配）");
    press.shift = false;
    Check(save.Matches(press), "Ctrl+S 命中 Ctrl+S");
    press.repeat = true;
    Check(!save.Matches(press), "长按重复默认不触发");
    press.repeat = false;

    // ---- 绑定表：冲突 / 派发 / 消费 ----
    inking::ShortcutRegistry registry;
    int fired = 0;
    const inking::ShortcutRegistry::Id id =
        registry.Bind(save, [&fired] { ++fired; }, "保存");
    Check(id != 0 && registry.Count() == 1, "快捷键绑定成功");
    Check(registry.Bind(save, [] {}, "重复项") == 0 && registry.Count() == 1,
          "重复组合被拒绝（先到先得，不静默覆盖）");
    Check(registry.IsTaken(save), "IsTaken 能预检冲突");

    // 造一条真实的按键事件喂进去（不经过窗口，自检要能单独验这条路）。
    inking::KeyboardInput input;
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
    inking::InkingWindow& window = inking::InkingWindow::Instance();
    const std::size_t before = window.GetShortcuts().Count();

    inking::ButtonData look;
    look.name = "menu_item";
    look.width = 80;
    look.height = 28;

    const inking::Shortcut open = inking::Shortcut::Parse("Ctrl+O");
    {
        inking::prefab::MenuBar bar(nullptr);
        inking::InkingStaticButton* item =
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
/// `MeasureText` 量两个字符——量出来正好等于"字号 × 码点数"就是**估算法**
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
        inking::TextRequest probe;
        probe.text = "Hg";
        probe.fontSize = 40.0f;
        probe.fontPath = path;
        const inking::TextMetrics metrics = inking::MeasureText(probe);
        if (std::fabs(metrics.width - 80.0f) > 0.5f) {  // 80 = 2 码点 × 40
            return path;
        }
    }
    return {};
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
    SDL_SetRenderLogicalPresentation(rendererB, inking::kDesignWidth,
                                     inking::kDesignHeight,
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
        Check(readable && logicalWidth == inking::kDesignWidth
                  && logicalHeight == inking::kDesignHeight
                  && outputWidth == 2560 && outputHeight == 1440,
              "比例来源可读：逻辑 1280×720 / 输出 2560×1440 ⇒ ratio = 2");
    }

    inking::TextRequest request;
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
        inking::detail::BeginFrame(renderer);
        inking::detail::PaintText(renderer, request, 400, 100, 1.0f, 100.0f,
                                  60.0f, inking::TransformSpec{});
        inking::detail::EndFrame();
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
        Check(inking::detail::GlyphAtlasSnapshot().rasterized > 0,
              "文字走了字形图集（高清路径不是退化方块）");
    }

    SDL_DestroyRenderer(rendererA);
    SDL_DestroyRenderer(rendererB);
    SDL_DestroyWindow(windowA);
    SDL_DestroyWindow(windowB);
}

// ---------------------------------------------------------------------------
// 音频层接线自检（P0 的观测口）
//
// 这一条存在的理由：音频整层曾经被 `#if defined(INK_HAS_SDL3_MIXER)` 关掉，
// 而 CMake 仍然 find_package + 链接 SDL3_mixer —— "能编能链"把"没接上"完全
// 盖住：`AudioInit()` 恒 false、BGM / SE 全无声，而且不报错、不警告。
// 二进制判据（导入表里有 SDL3_mixer.dll + MIX_ 符号）由构建侧查；
// **"淡入真的在推进"只能靠跑一遍**，那是空桩版本永远涨不上去的量。
//
// 这里直接调 `AudioTick`：它和窗口层每帧调的是**同一个函数**（窗口那边删掉
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
    const bool inited = inking::AudioInit();
    Check(inited, "AudioInit 成功（音频层真的接上了 SDL3_mixer）");
    if (!inited) {
        return;  // 没接上：后面都无从验，不再往下编故事
    }

    const std::vector<std::uint8_t> wav = MakeSilentWav(44100, 1, 16, 0.2f);
    const inking::SoundId sound =
        inking::AudioLoadFromMemory(wav.data(), wav.size(), "selftest.wav");
    Check(sound != 0u && inking::AudioIsReady(sound), "内存 WAV 加载成功");

    // 0.5 秒淡入、目标 1.0：增益必须**按帧上升**，而不是一步到 1。
    inking::AudioPlayBgm(sound, 0.5f, 1.0f);
    const float start = inking::AudioBgmVolume();
    for (int frame = 0; frame < 10; ++frame) {
        inking::AudioTick(0.05f);  // 累计 0.5 秒
    }
    const float end = inking::AudioBgmVolume();
    std::printf("      [audio] 淡入增益 %.3f → %.3f\n",
                static_cast<double>(start), static_cast<double>(end));
    Check(end > start + 0.1f, "BGM 淡入在 AudioTick 里推进（增益上升）");

    inking::AudioStopBgm(0.0f);
    inking::AudioShutdown();
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("== inking 动态库 · 无头端到端自检 ==\n");
    // 阶段单独打出来（`0.1.0` + `test`）：测试版本这件事要在运行期看得见，
    // 不能只写在 README 里（别人抓到一份日志时，第一行就该知道它不是正式版）。
    std::printf("   版本 %s-%s（测试版本）/ SDL3 %s / 消息队列编译期%s\n",
                inking::kVersion, inking::kVersionStage,
                inking::sdl3_version().c_str(),
                inking::kMessageEnabled ? "开" : "关");

    TestScene scene;
    Check(scene.IsRegistered(), "场景「构造即登记」进 SceneLibrary");
    Check(scene.GetDrawItemCount() >= 2, "绘制列表里有两个组件");

    // ---- 无头环境：dummy 视频驱动 + 软件渲染器 ----
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    Check(SDL_Init(SDL_INIT_VIDEO), "SDL_Init（dummy 视频驱动）");
    SDL_Window* win = SDL_CreateWindow("ink-selftest", 640, 360, 0);
    Check(win != nullptr, "创建窗口");
    SDL_Renderer* rnd = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
    Check(rnd != nullptr, "创建渲染器（软件）");

    scene.TickFrame(1.0f / 60.0f);
    inking::SceneLibrary::SetActiveScene(&scene);
    Check(inking::SceneLibrary::RenderScene(rnd),
          "RenderScene 走通（渲染提交成功）");
    Check(scene.GetHitTableEntryCount() > 0,
          "静态组件进了命中表（动态组件不进）");
    Check(scene.button->GetWidth() == 200, "静态按钮宽度来自配置");

    // ---- 静态按钮：三态 + 点击（场景推过来） ----
    scene.SetPointerState(640.0f, 360.0f, false, true);
    scene.DispatchPointer();
    Check(scene.button->GetState() == inking::ButtonState::Hover,
          "指针移入 → 悬停态");

    scene.SetPointerState(640.0f, 360.0f, true, true);
    scene.DispatchPointer();
    Check(scene.button->GetState() == inking::ButtonState::Pressed,
          "按下 → 按下态");

    scene.SetPointerState(640.0f, 360.0f, false, true);
    scene.DispatchPointer();
    Check(scene.clicked, "抬起 → 点击回调真的被触发");
    Check(scene.button->GetState() == inking::ButtonState::Hover,
          "抬起后回到悬停态");

    // ---- 动态按钮：Tick 派发 + 几何写入 ----
    const float beforeX = scene.mover->GetX();
    scene.TickLogic(0.02);
    const float afterX = scene.mover->GetX();
    Check(afterX > beforeX + 0.5f,
          "动态组件的 onTick 被派发且几何真的改了（ChangeOffset 生效）");
    Check(inking::SceneLibrary::RenderScene(rnd),
          "动态几何变化后再渲染一帧成功");

    // ---- InkingMesh × ShapeContains 对账（纯几何，与窗口无关） ----
    Check(ReconcileShapeMesh() == 0,
          "轮廓采样与形状判定同源（8 种形状逐点对账）");

    // ---- 窗口：缩放（严格等比）/ 全屏 / 菜单栏联动 ----
    CheckWindowZoomAndFullscreen();
    CheckMenuBarFullscreenHiding();

    // ---- 快捷键：解析 / 匹配 / 冲突 / 消费 / 菜单项绑定 ----
    CheckShortcuts();
    CheckMenuBarShortcutBinding();

    // ---- 文字高清：按设备比例烘字，布局零位移 ----
    CheckTextHiDpi();

    // ---- 消息队列 ----
    inking::MessageQueue::addMessage(inking::MessageType::Pass, 0.0f, "hi");
    const std::vector<inking::Message> msgs =
        inking::MessageQueue::drainMessages();
    Check(msgs.size() == 1 && msgs[0].message == "hi", "消息队列入队 / 出队");

    // ---- 任务队列：单例在 DLL 里，管家线程真的在跑 ----
    std::atomic<int> ran{0};
    inking::Task<std::any> task;
    task.action = [&ran](const std::vector<std::any>&) -> std::any {
        ran.fetch_add(1, std::memory_order_relaxed);
        return {};
    };
    inking::TaskQueue::instance().addTask(std::move(task));
    for (int i = 0; i < 200 && ran.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(ran.load() == 1, "任务队列的管家线程执行了任务");

    // ---- 音频层接线（P0）：淡入曲线是唯一能证明"真的接上了"的观测口 ----
    CheckAudioPipeline();

    inking::SceneLibrary::SetActiveScene(nullptr);
    if (rnd != nullptr) {
        SDL_DestroyRenderer(rnd);
    }
    if (win != nullptr) {
        SDL_DestroyWindow(win);
    }
    SDL_Quit();

    std::printf("== 失败项：%d ==\n", g_fail);
    return g_fail;
}
