// ---------------------------------------------------------------------------
// Inking 示例程序：最小可运行的「窗口 + 场景 + 组件」
//
// 它**只链接 inking 动态库**（见 CMakeLists.txt），用到的全部是公开头：
//   <inking.h>                     版本 / 日志 / 消息队列 / 任务队列
//   <window/InkingWindow.h>        窗口（Show() 跑主循环）
//   <scene/InkingScene.h>          场景基类（构造即登记）
//   <scene/SceneLibrary.h>         场景库
//   <button/InkingStaticButton.h>  静态按钮（三态只换颜色 → 进命中表）
//   <button/InkingDynamicButton.h> 动态按钮（几何自己动 → 不进表，每帧 Tick）
//   <text/InkingStaticLabel.h>     只读文字
//
// 三件事在这份代码里各演示一条：
//   1. **两种按钮的分工**：静态按钮走点击回调；动态按钮在 `onTick` 里自己
//      挪位置（`ChangeOffset` 是动态档独有的几何写入口）；
//   2. **场景「构造即登记」**：`DemoScene` 一构造就进了 `SceneLibrary`，
//      `Show("demo")` 按名字找到它、激活它、跑主循环；
//   3. **链接库边界**：示例不包含任何 `src/` 下的实现，全部经 DLL 调用。
//
// 注意：文字由纹理层画。机器默认字体若没有对应字形，会**退化成占位方块**
// （那是纹理层有意的兜底，不是本示例的问题）。
// ---------------------------------------------------------------------------

#include <inking.h>

#include <button/InkingDynamicButton.h>
#include <button/InkingStaticButton.h>
#include <input/Shortcut.h>
#include <prefab/MenuBar.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>
#include <text/InkingStaticLabel.h>
#include <text/LabelData.h>
#include <window/InkingWindow.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace {

// ---------------- 一组配色（0xAARRGGBB） ----------------
constexpr std::uint32_t kBg        = 0xFF1B1B24u;  // 窗口底色
constexpr std::uint32_t kAccent    = 0xFF3F7FD6u;  // 按钮通常态
constexpr std::uint32_t kAccentHi  = 0xFF5A9BF0u;  // 悬停
constexpr std::uint32_t kAccentPr  = 0xFF2F63AAu;  // 按下
constexpr std::uint32_t kGhost     = 0xFF32323Fu;  // 会飘的按钮
constexpr std::uint32_t kGhostHi   = 0xFF3F3F52u;
constexpr std::uint32_t kGhostPr   = 0xFF26262Fu;
constexpr std::uint32_t kText      = 0xFFE8E8F2u;
constexpr std::uint32_t kTextDim   = 0xFF9A9AB0u;

/// 造一份按钮配置。三态都显式写，并把两条 `*InheritsNormal` 关掉
/// ——否则 `Normalize()` 会把 hover / onclicked 又拉回 normal 那一份。
inking::ButtonData MakeButton(const std::string& name, int width, int height,
                              const std::string& label, std::uint32_t normal,
                              std::uint32_t hover, std::uint32_t pressed,
                              float radius) {
    inking::ButtonData d;
    d.name = name;
    d.width = width;
    d.height = height;

    d.normal = inking::ButtonAppearance(inking::AppearanceType::Color, normal);
    d.hover = inking::ButtonAppearance(inking::AppearanceType::Color, hover);
    d.onclicked = inking::ButtonAppearance(inking::AppearanceType::Color, pressed);
    d.hoverInheritsNormal = false;
    d.onclickedInheritsNormal = false;

    // 一段很短的过渡：让"三态之间是插值过去的"这件事看得见。
    d.normal.transitionSeconds = 0.14f;
    d.hover.transitionSeconds = 0.14f;
    d.onclicked.transitionSeconds = 0.06f;

    d.label = label;
    d.text.fontSize = 26.0f;
    d.text.align = inking::TextAlignH::Center;
    d.text.alignV = inking::TextAlignV::Middle;

    // 锚点都由父级中心算：`traceAnchor` 是"落在父级的哪个百分比点"，
    // `selfAnchor` 是"自己的哪个点对上它"。两个都取 Center 就是正中对齐。
    d.selfAnchor = inking::InkingChangeAnchor::Center;
    d.traceAnchor = inking::InkingChangeAnchor::Center;

    d.shape = inking::ShapeSpec::RoundedRect(radius);
    return d;
}

/// 造一份标签配置（居中、给定字号与颜色）。
inking::LabelData MakeLabel(const std::string& name, int width, int height,
                            const std::string& text, float fontSize,
                            std::uint32_t color) {
    inking::LabelData d;
    d.name = name;
    d.width = width;
    d.height = height;
    d.text = text;
    d.style.fontSize = fontSize;
    d.style.align = inking::TextAlignH::Center;
    d.style.alignV = inking::TextAlignV::Middle;
    d.color = color;
    d.selfAnchor = inking::InkingChangeAnchor::Center;
    d.traceAnchor = inking::InkingChangeAnchor::Center;
    return d;
}

/**
 * 会飘的动态按钮：**演示动态档与静态档的唯一差别——几何能自己改**。
 *
 * 它只做一件事：每逻辑步把自己的 `offsetX` 按正弦挪一点。`ChangeOffset` 是
 * `InkingDynamicWidget` 的几何写入口，它自己会标脏、自己会通知场景
 * （静态档没有这个入口——那是有意的类型保证，`src/inking.cpp` 里有断言钉着）。
 *
 * 输入照常：`InkingDynamicButton::onTick` 里那一句 `JudgePointer()` 是"自己拉
 * 指针"的实现，所以这里必须先调基类，再动几何。
 */
class FloatingButton : public inking::InkingDynamicButton {
public:
    FloatingButton(inking::InkingWidget* parent, const inking::ButtonData& data,
                   float amplitude)
        : inking::InkingDynamicButton(parent, data),
          _baseX(data.offsetX),
          _baseY(data.offsetY),
          _amplitude(amplitude) {}

protected:
    void onTick(float deltaSeconds) override {
        inking::InkingDynamicButton::onTick(deltaSeconds);  // 先收输入

        _phase += deltaSeconds;
        const float dx = _amplitude * std::sin(_phase * 1.7f);
        ChangeOffset(_baseX + dx, _baseY);
    }

private:
    float _phase = 0.0f;
    float _baseX = 0.0f;
    float _baseY = 0.0f;
    float _amplitude = 0.0f;
};

/**
 * 示例场景。
 *
 * **构造即登记**：`InkingScene("demo")` 这一步就把自己挂进 `SceneLibrary`
 * ——名字被占时会进入"停放态"（`IsRegistered()` 为 false），所以下面 `main`
 * 里那份断言不是多余的。
 *
 * 组件的挂法用 `AddChild`（**拥有型**）：场景持有 `unique_ptr`，场景销毁时
 * 子树跟着销毁。构造参数里那个 `this` 是"父级"（非拥有、可后续换父），
 * 传它只是让组件在构造期就能沿父链找到场景与自己的绝对坐标。
 */
class DemoScene : public inking::InkingScene {
public:
    DemoScene() : inking::InkingScene("demo") {
        BuildUi();
    }

private:
    void BuildUi() {
        // 0) 菜单栏：无边框窗口的**自绘标题栏**（拖动 + 最小化 / 关闭），
        //    同时承担两件事——
        //      · 滚轮缩放：指针在菜单栏上滚一下就按档位缩放窗口（无边框没有系统
        //        边缘可拖，这是改尺寸的唯一入口）；
        //      · 全局快捷键：菜单项绑的组合不依赖焦点，全屏隐藏后照样能用。
        //    全屏由 `InkingWindow` 内置的 F11 切换；菜单栏在全屏时会自动隐藏
        //    （`MenuBarStyle::hideInFullscreen` 默认开）。
        inking::prefab::MenuBarStyle barStyle =
            inking::prefab::MenuBarStyle::MakeDefault();
        barStyle.draggable = true;       // 按住空白处拖窗口
        barStyle.captionButtons = true;  // 右上角最小化 / 关闭
        inking::prefab::MenuBar* bar = static_cast<inking::prefab::MenuBar*>(
            AddChild(std::make_unique<inking::prefab::MenuBar>(this, barStyle)));
        _menuBar = bar;

        inking::ButtonData item = MakeButton("menu_item", 92, 28, "", kGhost,
                                             kGhostHi, kGhostPr, 8.0f);
        item.text.fontSize = 20.0f;
        bar->AddButton(item, "放大",
                       [] { inking::InkingWindow::Instance().ZoomBy(1); },
                       inking::Shortcut::Parse("Ctrl+="));
        bar->AddButton(item, "缩小",
                       [] { inking::InkingWindow::Instance().ZoomBy(-1); },
                       inking::Shortcut::Parse("Ctrl+-"));
        bar->AddButton(item, "全屏",
                       [] { inking::InkingWindow::Instance().ToggleFullscreen(); },
                       inking::Shortcut::Parse("Ctrl+Enter"));
        bar->AddButton(item, "退出",
                       [] { inking::InkingWindow::Instance().RequestExit(); },
                       inking::Shortcut::Parse("Ctrl+Q"));

        // 1) 标题
        _title = AddLabel("title", "Inking 动态库示例", 900, 70, 44.0f, kText,
                          -260.0f);
        // 2) 一行说明（点会飘的按钮也能点）
        _hint = AddLabel("hint", "下面那个按钮每帧都在自己改变位置（动态档）",
                         900, 50, 22.0f, kTextDim, 10.0f);

        // 3) 静态按钮：点击计数
        inking::ButtonData clickData = MakeButton("click_me", 260, 84, "点我",
                                                 kAccent, kAccentHi, kAccentPr,
                                                 14.0f);
        clickData.offsetY = -120.0f;
        inking::InkingStaticButton* button =
            static_cast<inking::InkingStaticButton*>(
                AddChild(std::make_unique<inking::InkingStaticButton>(
                    this, clickData)));
        // 语义是"替换"：再设一次就是换掉上一个回调（要挂多个用 clicked.Subscribe）。
        button->SetOnClicked([this] {
            ++_clicks;
            _status->SetText("点击次数：" + std::to_string(_clicks));
        });
        _button = button;

        // 4) 状态标签
        _status = AddLabel("status", "点击次数：0", 700, 60, 28.0f, kText,
                           90.0f);

        // 5) 动态按钮：自己飘
        inking::ButtonData floatData = MakeButton("floating", 240, 76, "我会飘",
                                                 kGhost, kGhostHi, kGhostPr,
                                                 38.0f);
        floatData.shape = inking::ShapeSpec::Capsule();
        floatData.offsetY = 220.0f;
        AddChild(std::make_unique<FloatingButton>(this, floatData, 220.0f));
    }

    inking::InkingStaticLabel* AddLabel(const std::string& name,
                                        const std::string& text, int width,
                                        int height, float fontSize,
                                        std::uint32_t color, float offsetY) {
        inking::LabelData data =
            MakeLabel(name, width, height, text, fontSize, color);
        data.offsetY = offsetY;
        return static_cast<inking::InkingStaticLabel*>(
            AddChild(std::make_unique<inking::InkingStaticLabel>(this, data)));
    }

    inking::InkingStaticLabel* _title = nullptr;
    inking::InkingStaticLabel* _hint = nullptr;
    inking::InkingStaticLabel* _status = nullptr;
    inking::InkingStaticButton* _button = nullptr;
    inking::prefab::MenuBar* _menuBar = nullptr;
    int _clicks = 0;
};

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    std::printf("[demo] Inking %s-%s（测试版本）/ 链接到的 SDL3 %s\n",
                inking::kVersion, inking::kVersionStage,
                inking::sdl3_version().c_str());
    std::printf("[demo] 消息队列编译期启用：%s\n",
                inking::kMessageEnabled ? "是" : "否");

    DemoScene scene;
    if (!scene.IsRegistered()) {
        // 名字被占（例如同一进程里已经建过一个 "demo"）。
        std::printf("[demo] 场景 \"demo\" 登记被驳回了——名字被占，退出。\n");
        return 1;
    }

    // Show() 是**同步**的：里面建窗口、跑主循环，直到关窗 / ESC 才返回。
    // 它按名字从 SceneLibrary 找场景并设为活跃（不需要自己 SetActiveScene）。
    //
    // 无边框：必须在 `Show()` **之前**设——那时它还只是记账，建窗口时直接带上
    // `SDL_WINDOW_BORDERLESS`，不会有一次"先带边框再拆掉"的窗口重建。
    inking::InkingWindow& window = inking::InkingWindow::Instance();
    window.SetBorderless(true);
    // ESC 的语义是"退一层"：全屏时先退全屏，再按一次才请求退出。退出前会过
    // 这道闸门——返回 false 就是"先别退"（业务可以在这里弹确认框）。
    window.SetExitConfirmHandler([] {
        std::printf("[demo] 退出确认闸门：直接放行（真实业务可在这里弹确认框）\n");
        return true;
    });

    window.Show("demo");

    std::printf("[demo] 主循环结束，退出。\n");
    return 0;
}
