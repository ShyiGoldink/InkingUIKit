// ---------------------------------------------------------------------------
// 最小可运行示例：**只依赖预编译 SDK**（不碰 src/，不需要源码树）
//
// 它对应 README「路线 A：手写组件」——一个场景、一个标签、一个按钮。
// 按钮点下去调 `RequestClose(0)`，`Show()` 原样返回 0。
//
// 构建见同目录 CMakeLists.txt；运行前不需要把任何目录加进 PATH：
// 构建脚本会把 SDK 的 bin/*.dll 拷到 exe 旁边。
// ---------------------------------------------------------------------------

#include <inking.h>

#include <button/InkingStaticButton.h>
#include <scene/InkingScene.h>
#include <text/InkingStaticLabel.h>
#include <text/LabelData.h>
#include <window/InkingWindow.h>

#include <cstdio>
#include <memory>

namespace {

/// 场景「构造即登记」：名字被占时 `IsRegistered()` 为 false。
class HelloScene : public inking::InkingScene {
public:
    HelloScene() : inking::InkingScene("hello") {
        AddChild(std::make_unique<inking::InkingStaticLabel>(this, MakeTitle()));
        auto* button = static_cast<inking::InkingStaticButton*>(
            AddChild(std::make_unique<inking::InkingStaticButton>(this,
                                                                  MakeButton())));
        button->SetOnClicked([] {
            inking::InkingWindow::Instance().RequestClose(0);
        });
    }

private:
    static inking::LabelData MakeTitle() {
        inking::LabelData data;
        data.name = "title";
        data.width = 720;
        data.height = 90;
        data.text = "Inking 0.4.0";
        data.style.fontSize = 44.0f;
        data.style.align = inking::TextAlignH::Center;
        data.style.alignV = inking::TextAlignV::Middle;
        data.color = 0xFF33303Cu;
        // 父级中心对自身中心 ⇒ 水平居中；offsetY 只负责纵向排布。
        data.selfAnchor = inking::InkingChangeAnchor::Center;
        data.traceAnchor = inking::InkingChangeAnchor::Center;
        data.offsetY = -90.0f;
        return data;
    }

    static inking::ButtonData MakeButton() {
        inking::ButtonData data;
        data.name = "quit";
        data.width = 240;
        data.height = 72;
        data.normal =
            inking::ButtonAppearance(inking::AppearanceType::Color, 0xFF6E5AAEu);
        data.hover =
            inking::ButtonAppearance(inking::AppearanceType::Color, 0xFF9380C2u);
        data.onclicked =
            inking::ButtonAppearance(inking::AppearanceType::Color, 0xFF5A4A8Eu);
        // 这两条默认是 true：关掉才能让 hover / pressed 用自己那一份颜色，
        // 否则 Normalize() 会把它们拉回 normal。
        data.hoverInheritsNormal = false;
        data.onclickedInheritsNormal = false;
        data.label = "退出";
        data.text.fontSize = 26.0f;
        data.selfAnchor = inking::InkingChangeAnchor::Center;
        data.traceAnchor = inking::InkingChangeAnchor::Center;
        data.shape = inking::ShapeSpec::RoundedRect(16.0f);
        data.offsetY = 40.0f;
        return data;
    }
};

}  // namespace

int main() {
    std::printf("[minimal] Inking %s-%s / SDL3 %s\n", inking::kVersion,
                inking::kVersionStage, inking::sdl3_version().c_str());

    HelloScene scene;
    if (!scene.IsRegistered()) {
        std::printf("[minimal] 场景 \"hello\" 登记被驳回（名字被占）。\n");
        return 1;
    }

    // 无边框必须在 Show() **之前**设：建窗口时直接带上标志。
    inking::InkingWindow& window = inking::InkingWindow::Instance();
    window.SetBorderless(true);
    return window.Show("hello");
}
