// ---------------------------------------------------------------------------
// inking_showcase —— **控件总览**（一堆预制件摆在一页里，给人看）
//
// 与 `inking_demo` 的分工：
//   `inking_demo`   最小可运行示例 + 动态档 / 帧动画 / 菜单栏那几件事；
//   本程序          把 `prefab/` 的控件一次摆齐，**看外观、点两下**。
//
// 页面上有：按钮（三态）· 文本输入 · 复选 / 单选 / 开关 · 滑块 · 进度条 ·
//           标签页（三页各有内容）· 悬浮提示 · 通知条 · 模态对话框。
//
// 三条先例留在代码里：
//   1. **几何全在数据里**（`selfAnchor` / `traceAnchor` 取 `LeftTop` + `offset`），
//      父卡片不负责排版——组件不猜业务布局（`AGENTS.md` §3 第 10 条）；
//   2. **浮层挂在场景上，不挂在卡片里**：通知 / 提示 / 对话框要压住所有内容，
//      位置由它们自己算（右下角 / 锚点旁 / 居中）；
//   3. **每个交互都把结果写回右上那句状态**——点下去有没有反应，一眼看得见。
// ---------------------------------------------------------------------------

#include <inking.h>

#include <button/InkingStaticButton.h>
#include <config/window_config.h>
#include <panel/InkingStaticPanel.h>
#include <panel/PanelData.h>
#include <prefab/Dialog.h>
#include <prefab/PrefabTheme.h>
#include <prefab/ProgressBar.h>
#include <prefab/Slider.h>
#include <prefab/TabBar.h>
#include <prefab/ToastHost.h>
#include <prefab/ToggleLike.h>
#include <prefab/Tooltip.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>
#include <text/InkingStaticLabel.h>
#include <text/InkingTextInput.h>
#include <text/LabelData.h>
#include <theme/InkingTheme.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace {

namespace theme = inking_prefab::theme;

// ---------------- 配色（取自 PrefabTheme，换主题一处生效） ----------------
constexpr std::uint32_t kBackdrop = 0xFFF4F1F9u;
constexpr std::uint32_t kCard     = theme::kSurface;
constexpr std::uint32_t kAccent   = theme::kAccentStrong;
constexpr std::uint32_t kAccentHi = theme::kAccent;
constexpr std::uint32_t kAccentPr = 0xFF5A4890u;
constexpr std::uint32_t kGhost    = theme::kSurfaceSunken;
constexpr std::uint32_t kGhostHi  = theme::kHoverNeutral;
constexpr std::uint32_t kDanger   = theme::kDanger;
constexpr std::uint32_t kDangerHi = 0xFFE05555u;
constexpr std::uint32_t kDangerPr = 0xFFB02F2Fu;
/// 次按钮 / 浅底按钮的文字色：**中间调**。
///
/// 为什么不是 `kTextPrimary`：那个是深色（#33303C），跟着主题变浅的是**标签**，
/// 而 `InkingButtonData::textColor` 是纯颜色、**没有 token 通道** ⇒ 深色主题下
/// 深字压深底直接看不见。中间调（#7A7390）在浅灰底和深灰底上对比度都够。
constexpr std::uint32_t kGhostText = theme::kTextMuted;

/**
 * 颜色 → 主题 token。
 *
 * 这是本示例的"换肤接线处"：组件只在**配置里带 token** 时才会在运行期重查
 * （`appearance.themeColor` / `label.colorToken`）。全部写死字面量的话，
 * `InkingSetActiveTheme()` 会成功、版本也会 +1，但画面上一个像素都不变——
 * 那正是"点了一下没反应"最像 bug 的形态。
 *
 * 只在**造数据的那几个助手**里调用一次，调用点就不用逐处写 token 了。
 */
const char* ThemeToken(std::uint32_t color) {
    switch (color) {
        case kBackdrop: return "sceneBg";
        case kCard:     return "card";
        case kGhost:    return "cardAlt";
        case kGhostHi:  return "cardAltHover";
        case kAccent:   return "accent";
        case kAccentHi: return "accentHover";
        case kAccentPr: return "accentPressed";
        case kDanger:   return "danger";
        case kDangerHi: return "dangerHover";
        case kDangerPr: return "dangerPressed";
        case theme::kTextPrimary: return "textPrimary";
        case theme::kTextMuted:   return "textMuted";
        default: return nullptr;
    }
}

/// 浅色表：**值必须与上面那些字面量逐位相同**，否则切到 light 也会"变一下"。
InkingThemeTable MakeLightTheme() {
    InkingThemeTable t;
    t.name = "light";
    t.colors = {
        {"sceneBg", kBackdrop},   {"card", kCard},
        {"cardAlt", kGhost},      {"cardAltHover", kGhostHi},
        {"textPrimary", theme::kTextPrimary},
        {"textMuted", theme::kTextMuted},
        {"accent", kAccent},      {"accentHover", kAccentHi},
        {"accentPressed", kAccentPr},
        {"danger", kDanger},      {"dangerHover", kDangerHi},
        {"dangerPressed", kDangerPr},
        {"thumb", 0xFFFFFFFFu},   // 滑块圆盘：浅色下纯白

    };
    return t;
}

/// 深色表：同一批 token 换一套值。少一个 token 的症状是"那一处还是浅色"，
/// 不会崩也不会空——所以这张表要跟着上面那张**成对维护**。
InkingThemeTable MakeDarkTheme() {
    InkingThemeTable t;
    t.name = "dark";
    t.colors = {
        {"sceneBg", 0xFF14131Au},        {"card", 0xFF1F1D27u},
        {"cardAlt", 0xFF2A2735u},        {"cardAltHover", 0xFF343042u},
        {"textPrimary", 0xFFECE9F4u},    {"textMuted", 0xFFA7A1B8u},
        {"accent", 0xFF9A86D6u},         {"accentHover", 0xFFB2A0E4u},
        {"accentPressed", 0xFF7E6ABEu},
        {"danger", 0xFFD0504Cu},         {"dangerHover", 0xFFDE6B67u},
        {"dangerPressed", 0xFFA83C39u},
        {"thumb", 0xFFE8E5F0u},          // 深色下不用纯白，免得刺眼

    };
    return t;
}

/// 装两张表并切到指定的那张（默认 light）。
void InstallDemoThemes(const std::string& active) {
    InkingInstallThemeTable(MakeLightTheme());
    InkingInstallThemeTable(MakeDarkTheme());
    InkingSetActiveTheme(active);
}

// ---------------- 造数据的三个助手 ----------------
//
// 锚点显式给：`traceAnchor` = "落在父级的哪个点"，`selfAnchor` = "自己的哪个点对上它"。
// 两个都取 `LeftTop` 就是"父级左上角 + offset"，最接近手写布局的直觉。

InkingLabelData MakeLabel(const std::string& name, int width, int height,
                          const std::string& text, float fontSize,
                          std::uint32_t color,
                          InkingTextAlignH align = InkingTextAlignH::Left) {
    InkingLabelData d;
    d.name = name;
    d.width = width;
    d.height = height;
    d.text = text;
    d.style.fontSize = fontSize;
    d.style.align = align;
    // 同一件事：`Middle` 按**文字块**（含行距那圈空白）算，字形在行内偏上。
    // 单行标签（高度不到字号的两倍）用"顶留白 = (高 − 字号) / 2"让**字形**居中；
    // 段落标签（hint 那种多行）仍从顶部排，否则会从中间开始。
    d.style.alignV = InkingTextAlignV::Top;
    if (static_cast<float>(height) <= fontSize * 2.0f) {
        d.style.padding.top = (static_cast<float>(height) - fontSize) * 0.5f;
    }
    d.color = color;
    d.colorToken = ThemeToken(color);  // 有 token 才会跟着主题重查
    d.selfAnchor = InkingChangeAnchor::LeftTop;
    d.traceAnchor = InkingChangeAnchor::LeftTop;
    return d;
}

/// 三态显式写、两条 `*InheritsNormal` 关掉（否则 `Normalize()` 会把 hover /
/// onclicked 又拉回 normal 那一份）。
InkingButtonData MakeButton(const std::string& name, int width, int height,
                            const std::string& label, std::uint32_t normal,
                            std::uint32_t hover, std::uint32_t pressed,
                            std::uint32_t textColor = 0xFFFFFFFFu,
                            float fontSize = 16.0f, float radius = 10.0f) {
    InkingButtonData d;
    d.name = name;
    d.width = width;
    d.height = height;
    d.normal = InkingButtonAppearance(InkingAppearanceType::Color, normal);
    d.hover = InkingButtonAppearance(InkingAppearanceType::Color, hover);
    d.onclicked = InkingButtonAppearance(InkingAppearanceType::Color, pressed);
    d.normal.themeColor = ThemeToken(normal);
    d.hover.themeColor = ThemeToken(hover);
    d.onclicked.themeColor = ThemeToken(pressed);
    d.hoverInheritsNormal = false;
    d.onclickedInheritsNormal = false;
    d.normal.transitionSeconds = 0.12f;
    d.hover.transitionSeconds = 0.12f;
    d.onclicked.transitionSeconds = 0.05f;
    d.label = label;
    d.text.fontSize = fontSize;
    d.text.align = InkingTextAlignH::Center;
    // 垂直居中不用 `Middle`：它按**文字块**（含行距那一圈空白）算，
    // 字形在行内天然偏上，42 高的按钮上肉眼能看出 5~6px 的偏上。
    // 这里直接给"顶偏移 = (高 − 字号) / 2"，让字形本身居中。
    d.text.alignV = InkingTextAlignV::Top;
    d.text.topSpace = (static_cast<float>(height) - fontSize) * 0.5f;
    d.textColor = textColor;
    // 浅底按钮的文字色跟着主题走（深色主题下变浅）；主按钮是白字，不用 token。
    if (textColor == kGhostText) {
        d.textColorToken = "textMuted";
    }
    d.selfAnchor = InkingChangeAnchor::LeftTop;
    d.traceAnchor = InkingChangeAnchor::LeftTop;
    d.shape = InkingShapeSpec::RoundedRect(radius);
    return d;
}

InkingPanelData MakeCard(const std::string& name, int width, int height,
                         std::uint32_t color, float radius) {
    InkingPanelData d;
    d.name = name;
    d.width = width;
    d.height = height;
    d.closed.color = color;
    d.closed.themeColor = ThemeToken(color);  // 卡片底也会跟着换
    d.startOpen = true;  // 不写这个，面板起步是"关着"的
    d.shape = InkingShapeSpec::RoundedRect(radius);
    return d;
}

class ShowcaseScene : public InkingScene {
public:
    ShowcaseScene() : InkingScene("showcase") { Build(); }

    /// 截图用：把通知与对话框都摆出来（正常运行时由按钮触发）。
    void PresentOverlaysForSnapshot() {
        if (_toasts != nullptr) {
            _toasts->Show("已保存到本地", 30.0f);
            _toasts->Show("网络断了，正在重试", 30.0f);
        }
        if (_dialog != nullptr) {
            _dialog->Open();
        }
    }

private:
    // ---------------- 通用小助手 ----------------
    /// 在**任意父级**下摆一块面板（卡片 / 输入框的底 / 色块都用它）。
    InkingStaticPanel* AddPad(InkingWidget* parent, const std::string& name, int x,
                              int y, int w, int h,
                              std::uint32_t color = kCard, float radius = 16.0f) {
        InkingPanelData data = MakeCard(name, w, h, color, radius);
        data.offsetX = static_cast<float>(x);
        data.offsetY = static_cast<float>(y);
        return static_cast<InkingStaticPanel*>(
            parent->AddChild(std::make_unique<InkingStaticPanel>(parent, data)));
    }

    InkingStaticPanel* AddCard(const std::string& name, int x, int y, int w, int h,
                               std::uint32_t color = kCard, float radius = 16.0f) {
        return AddPad(this, name, x, y, w, h, color, radius);
    }

    InkingStaticLabel* AddLabel(InkingWidget* parent, const std::string& name,
                                int x, int y, int w, int h,
                                const std::string& text, float fontSize,
                                std::uint32_t color,
                                InkingTextAlignH align = InkingTextAlignH::Left) {
        InkingLabelData data = MakeLabel(name, w, h, text, fontSize, color, align);
        data.offsetX = static_cast<float>(x);
        data.offsetY = static_cast<float>(y);
        return static_cast<InkingStaticLabel*>(
            parent->AddChild(std::make_unique<InkingStaticLabel>(parent, data)));
    }

    InkingStaticButton* AddButton(InkingWidget* parent, const std::string& name,
                                  int x, int y, int w, int h,
                                  const std::string& text, std::uint32_t normal,
                                  std::uint32_t hover, std::uint32_t pressed,
                                  std::uint32_t textColor = 0xFFFFFFFFu,
                                  float fontSize = 16.0f) {
        InkingButtonData data = MakeButton(name, w, h, text, normal, hover, pressed,
                                           textColor, fontSize);
        data.offsetX = static_cast<float>(x);
        data.offsetY = static_cast<float>(y);
        return static_cast<InkingStaticButton*>(
            parent->AddChild(std::make_unique<InkingStaticButton>(parent, data)));
    }

    /// 主题标签跟着当前表走（第一次也是它写的）。
    void UpdateThemeLabel() {
        if (_themeLabel != nullptr) {
            const std::string& name = InkingActiveThemeName();
            _themeLabel->SetText("当前：" + (name.empty() ? "（未装表）" : name));
        }
    }

    void Say(const std::string& text) {
        if (_status != nullptr) {
            _status->SetText(text);
        }
    }

    // ---------------- 搭界面 ----------------
    void Build() {
        // 0) 铺满的底色：第一个子节点 ⇒ 画在最下、也最先被命中。
        InkingPanelData backdrop =
            MakeCard("backdrop", InkingDesignWidth(), InkingDesignHeight(),
                     kBackdrop, 0.0f);
        AddChild(std::make_unique<InkingStaticPanel>(this, backdrop));

        // 1) 页眉
        InkingLabelData title =
            MakeLabel("title", 700, 40, "UIKit 控件总览", 30.0f, theme::kTextPrimary);
        title.offsetX = 48.0f;
        title.offsetY = 22.0f;
        AddChild(std::make_unique<InkingStaticLabel>(this, title));

        InkingLabelData sub =
            MakeLabel("subtitle", 700, 22, "按钮 · 输入 · 选择 · 计量 · 页签 · 浮层",
                      15.0f, theme::kTextMuted);
        sub.offsetX = 50.0f;
        sub.offsetY = 66.0f;
        AddChild(std::make_unique<InkingStaticLabel>(this, sub));

        // 2) 右上角状态行：每次交互写一句。
        _status = AddLabel(this, "status_line", 560, 24, 640, 34,
                           "点一点下面的控件，这里会显示最近一次操作", 14.0f,
                           theme::kTextMuted, InkingTextAlignH::Right);

        BuildButtonsCard();
        BuildMetersCard();
        BuildChoicesCard();
        BuildOverlaysCard();
        BuildTabsCard();

        // 3) 浮层最后加：兄弟内后加的在上（顺序 = 谁压谁）。
        BuildFloatingLayers();
    }

    // ---- 卡片一：按钮 + 文本输入 ----
    void BuildButtonsCard() {
        InkingStaticPanel* card = AddCard("card_buttons", 48, 104, 380, 252);
        AddLabel(card, "cap_buttons", 20, 14, 220, 22, "按钮 / 文本", 15.0f,
                 theme::kTextMuted);

        AddButton(card, "btn_primary", 20, 50, 150, 42, "保存", kAccent, kAccentHi,
                  kAccentPr)
            ->clicked.Subscribe([this] { Say("按钮：保存（主按钮）"); });
        AddButton(card, "btn_ghost", 190, 50, 150, 42, "取消", kGhost, kGhostHi,
                  theme::kSurfaceSunken, kGhostText)
            ->clicked.Subscribe([this] { Say("按钮：取消（次按钮）"); });
        AddButton(card, "btn_danger", 20, 104, 320, 42, "删除这条记录", kDanger,
                  0xFFE05555u, 0xFFB02F2Fu)
            ->clicked.Subscribe([this] { Say("按钮：删除（危险色）"); });

        // 输入框本体只有文字 + 光标（它继承标签，没有背景那一层），
        // 所以先垫一块浅色圆角底——否则它看起来就是"一行字"，不像能点进去。
        AddPad(card, "input_note_bg", 20, 160, 320, 42, theme::kSurfaceSunken, 10.0f);

        InkingTextInputData input;
        input.name = "input_note";
        input.width = 320;
        input.height = 42;
        input.offsetX = 20.0f;
        input.offsetY = 160.0f;
        input.text = "点进来，能打字、能选中";
        input.style.fontSize = 15.0f;
        input.style.align = InkingTextAlignH::Left;
        input.style.alignV = InkingTextAlignV::Top;
        input.style.padding.top = (42.0f - 15.0f) * 0.5f;
        input.style.padding.left = 12.0f;
        input.color = theme::kTextPrimary;
        input.colorToken = "textPrimary";  // 手工构造不走 MakeLabel，token 要自己给
        input.selfAnchor = InkingChangeAnchor::LeftTop;
        input.traceAnchor = InkingChangeAnchor::LeftTop;
        auto* field = static_cast<InkingTextInput*>(
            card->AddChild(std::make_unique<InkingTextInput>(card, input)));
        field->submitted.Subscribe(
            [this](const std::string& text) { Say("输入框提交：" + text); });

        AddLabel(card, "hint_input", 20, 210, 320, 20,
                 "回车提交；点进来能编辑、能选中", 12.0f, theme::kTextMuted);

    }

    // ---- 卡片二：滑块 + 进度条 ----
    void BuildMetersCard() {
        InkingStaticPanel* card = AddCard("card_meters", 48, 372, 380, 320);
        AddLabel(card, "cap_meters", 20, 14, 220, 22, "计量", 15.0f,
                 theme::kTextMuted);

        // 音量：0~100，初始 70
        AddLabel(card, "lb_vol", 20, 46, 120, 24, "音量", 14.0f, theme::kTextPrimary);
        InkingStaticLabel* volValue =
            AddLabel(card, "lb_vol_value", 260, 46, 80, 24, "70", 14.0f,
                     theme::kAccentStrong, InkingTextAlignH::Right);

        inking_prefab::SliderData vol = inking_prefab::SliderData::MakeDefault();
        vol.name = "slider_volume";
        vol.width = 320;
        vol.height = 28;
        vol.offsetX = 20.0f;
        vol.offsetY = 74.0f;
        vol.min = 0.0f;
        vol.max = 100.0f;
        vol.value = 70.0f;
        vol.step = 1.0f;
        vol.track.themeColor = "cardAlt";
        vol.fill.themeColor = "accent";
        vol.thumb.themeColor = "thumb";
        auto* volSlider = static_cast<inking_prefab::Slider*>(
            card->AddChild(std::make_unique<inking_prefab::Slider>(card, vol)));
        volSlider->changed.Subscribe([this, volValue](float now, float) {
            volValue->SetText(std::to_string(static_cast<int>(std::lround(now))));
            Say("滑块：音量 → " + std::to_string(static_cast<int>(std::lround(now))));
        });

        // 亮度：不量化，0~1
        AddLabel(card, "lb_bright", 20, 116, 120, 24, "亮度", 14.0f,
                 theme::kTextPrimary);
        InkingStaticLabel* brightValue =
            AddLabel(card, "lb_bright_value", 260, 116, 80, 24, "45%", 14.0f,
                     theme::kAccentStrong, InkingTextAlignH::Right);

        inking_prefab::SliderData bright = inking_prefab::SliderData::MakeDefault();
        bright.name = "slider_brightness";
        bright.width = 320;
        bright.height = 28;
        bright.offsetX = 20.0f;
        bright.offsetY = 144.0f;
        bright.min = 0.0f;
        bright.max = 1.0f;
        bright.value = 0.45f;
        bright.track.themeColor = "cardAlt";
        bright.fill.themeColor = "accent";
        bright.thumb.themeColor = "thumb";
        auto* brightSlider = static_cast<inking_prefab::Slider*>(
            card->AddChild(std::make_unique<inking_prefab::Slider>(card, bright)));
        brightSlider->changed.Subscribe([this, brightValue](float now, float) {
            const int percent = static_cast<int>(std::lround(now * 100.0f));
            brightValue->SetText(std::to_string(percent) + "%");
            Say("滑块：亮度 → " + std::to_string(percent) + "%");
        });

        // 进度条：只读
        AddLabel(card, "lb_progress", 20, 196, 160, 24, "下载进度", 14.0f,
                 theme::kTextPrimary);
        AddLabel(card, "lb_progress_value", 260, 196, 80, 24, "35%", 14.0f,
                 theme::kTextMuted, InkingTextAlignH::Right);

        inking_prefab::ProgressBarData bar =
            inking_prefab::ProgressBarData::MakeDefault();
        bar.name = "progress_download";
        bar.width = 320;
        bar.height = 14;
        bar.offsetX = 20.0f;
        bar.offsetY = 226.0f;
        bar.progress = 0.35f;
        bar.track.themeColor = "cardAlt";
        bar.fill.themeColor = "accent";
        card->AddChild(std::make_unique<inking_prefab::ProgressBar>(card, bar));

        AddLabel(card, "hint_meters_1", 20, 258, 340, 20,
                 "滑块能拖、能按方向键", 12.0f, theme::kTextMuted);
        AddLabel(card, "hint_meters_2", 20, 280, 340, 20,
                 "进度条只读：SetProgress 夹取 0~1", 12.0f, theme::kTextMuted);
    }

    // ---- 卡片三：勾选三件套 ----
    void BuildChoicesCard() {
        InkingStaticPanel* card = AddCard("card_choices", 452, 104, 380, 252);
        AddLabel(card, "cap_choices", 20, 14, 220, 22, "选择", 15.0f,
                 theme::kTextMuted);

        // 复选框
        inking_prefab::ToggleData check = inking_prefab::MakeCheckboxData();
        check.name = "check_remember";
        check.checked = true;
        check.offsetX = 20.0f;
        check.offsetY = 52.0f;
        auto* checkbox = static_cast<inking_prefab::Checkbox*>(
            card->AddChild(std::make_unique<inking_prefab::Checkbox>(card, check)));
        AddLabel(card, "lb_remember", 52, 48, 200, 26, "记住我", 15.0f,
                 theme::kTextPrimary);
        checkbox->clicked.Subscribe([this] { Say("复选框：记住我 = 开"); });
        checkbox->cancelled.Subscribe([this] { Say("复选框：记住我 = 关"); });

        // 开关
        inking_prefab::ToggleData sw = inking_prefab::MakeSwitchData();
        sw.name = "switch_autosave";
        sw.checked = true;
        sw.offsetX = 20.0f;
        sw.offsetY = 94.0f;
        auto* toggle = static_cast<inking_prefab::Switch*>(
            card->AddChild(std::make_unique<inking_prefab::Switch>(card, sw)));
        AddLabel(card, "lb_autosave", 76, 90, 220, 30, "自动保存", 15.0f,
                 theme::kTextPrimary);
        toggle->clicked.Subscribe([this] { Say("开关：自动保存 = 开"); });
        toggle->cancelled.Subscribe([this] { Say("开关：自动保存 = 关"); });

        // 单选组（互斥由组负责）。
        // 行距给足 36：三个圈 + 文字本身纵向占 18，留 18 的呼吸；上面不再压一个
        // "尺寸"小标题——那个标题的 24 高会和第一行的圈**重叠**（踩过）。
        const char* names[3] = {"小", "中", "大"};
        const int rows[3] = {154, 190, 226};
        for (int i = 0; i < 3; ++i) {
            inking_prefab::ToggleData radio = inking_prefab::MakeRadioData();
            radio.name = std::string("radio_size_") + std::to_string(i);
            radio.checked = (i == 1);
            radio.offsetX = 20.0f;
            radio.offsetY = static_cast<float>(rows[i] - 18);
            auto* node = static_cast<inking_prefab::Radio*>(
                card->AddChild(std::make_unique<inking_prefab::Radio>(card, radio)));
            _radios.Add(node);
            AddLabel(card, std::string("lb_size_") + std::to_string(i), 56,
                     rows[i] - 23, 200, 28, names[i], 15.0f, theme::kTextPrimary);
            node->clicked.Subscribe([this, i, names] { Say(std::string("单选：尺寸 = ") + names[i]); });
        }
    }

    // ---- 卡片四：浮层入口 ----
    void BuildOverlaysCard() {
        InkingStaticPanel* card = AddCard("card_overlays", 452, 372, 380, 320);
        AddLabel(card, "cap_overlays", 20, 14, 240, 22, "浮层与反馈", 15.0f,
                 theme::kTextMuted);

        AddButton(card, "btn_toast", 20, 50, 320, 42, "弹一条通知", kAccent,
                  kAccentHi, kAccentPr)
            ->clicked.Subscribe([this] {
                if (_toasts != nullptr) {
                    _toasts->Show("已保存到本地", 2.5f);
                }
                Say("通知：右下角弹出一条（2.5 秒后自己消失）");
            });

        AddButton(card, "btn_dialog", 20, 100, 320, 42, "打开对话框", kGhost,
                  kGhostHi, theme::kSurfaceSunken, kGhostText)
            ->clicked.Subscribe([this] {
                if (_dialog != nullptr) {
                    _dialog->Open();
                }
                Say("对话框：打开（Esc 取消 / Enter 确认）");
            });

        auto* hintButton = AddButton(card, "btn_tooltip", 20, 150, 320, 42,
                                     "鼠标停在这里看提示", kGhost, kGhostHi,
                                     theme::kSurfaceSunken, kGhostText);
        hintButton->stateChanged.Subscribe(
            [this, hintButton](InkingButtonState from, InkingButtonState to) {
                if (_tip == nullptr) {
                    return;
                }
                if (to == InkingButtonState::Hover) {
                    _tip->ShowFor(hintButton, "0.4 秒后出现，点一下会穿到按钮上");
                } else if (from == InkingButtonState::Hover) {
                    _tip->Hide();
                }
            });

        AddButton(card, "btn_toast_clear", 20, 208, 320, 36, "清空通知", kGhost,
                  kGhostHi, theme::kSurfaceSunken, kGhostText, 14.0f)
            ->clicked.Subscribe([this] {
                if (_toasts != nullptr) {
                    _toasts->Clear();
                }
                Say("通知：已清空（不发 dismissed）");
            });

        AddLabel(card, "hint_overlays_1", 20, 256, 340, 20,
                 "通知条与提示都不参与命中：", 12.0f, theme::kTextMuted);
        AddLabel(card, "hint_overlays_2", 20, 278, 340, 20,
                 "提示盖在按钮上，点下去照样落到按钮。", 12.0f, theme::kTextMuted);
    }

    // ---- 卡片五：标签页 ----
    void BuildTabsCard() {
        inking_prefab::TabBarData data = inking_prefab::TabBarData::MakeDefault();
        data.name = "tabs";
        // 内缩 16：TabBar 自己的底是直角矩形，比卡片小一圈之后
        // 露出来的就是卡片的圆角——两件事都不必给 TabBar 加形状字段。
        data.width = 344;
        data.height = 556;
        data.offsetX = 16.0f;
        data.offsetY = 16.0f;
        data.selfAnchor = InkingChangeAnchor::LeftTop;
        data.traceAnchor = InkingChangeAnchor::LeftTop;
        data.tabLength = 108.0f;
        data.background.themeColor = "card";
        // 标签**按钮**也铺在标签条上（长度 108×3 几乎盖满），它们的底色不挂
        // token 的话，标签条看着还是白的——这正是"只给容器挂 token 不够"的那类。
        data.tab.normal.themeColor = "card";
        data.tab.hover.themeColor = "cardAltHover";
        data.tab.onclicked.themeColor = "cardAlt";
        data.textColor = kGhostText;
        data.selectedTextColor = 0xFF9A86D6u;
        data.content.themeColor = "card";
        data.stripThickness = 38.0f;
        InkingStaticPanel* host = AddCard("card_tabs", 856, 104, 376, 588);
        _tabs = static_cast<inking_prefab::TabBar*>(
            host->AddChild(std::make_unique<inking_prefab::TabBar>(host, data)));
        _tabs->pageChanged.Subscribe([this](int now, int) {
            Say("标签页：切到第 " + std::to_string(now + 1) + " 页");
        });

        // 页容器与内容区同尺寸（376 × (588-38)）。
        const int pageW = _tabs->GetContentWidth();
        const int pageH = _tabs->GetContentHeight();

        // --- 第一页：常规 ---
        {
            InkingPanelData pageData = MakeCard("page_general", pageW, pageH,
                                                theme::kSurface, 0.0f);
            auto page = std::make_unique<InkingStaticPanel>(nullptr, pageData);
            // **先挂进 TabBar、再加子控件**：页里的控件要沿父链认识场景，
            // 否则收不到每帧钩子——换主题时页内控件颜色不跟着变，且不报错。
            const int pageIndex = _tabs->AddPage("常规", std::move(page));
            InkingStaticPanel* p =
                static_cast<InkingStaticPanel*>(_tabs->GetPage(pageIndex));

            AddLabel(p, "cap_general", 24, 20, 240, 26, "常规设置", 19.0f,
                     theme::kTextPrimary);

            AddPad(p, "input_search_bg", 24, 58, 296, 40, theme::kSurfaceSunken, 10.0f);
            InkingTextInputData input;
            input.name = "input_search";
            input.width = 296;
            input.height = 40;
            input.offsetX = 24.0f;
            input.offsetY = 60.0f;
            input.text = "搜索关键字";
            input.style.fontSize = 14.0f;
            input.style.align = InkingTextAlignH::Left;
            input.style.alignV = InkingTextAlignV::Top;
            input.style.padding.top = (40.0f - 14.0f) * 0.5f;
            input.style.padding.left = 12.0f;
            input.color = theme::kTextPrimary;
            input.colorToken = "textPrimary";
            input.selfAnchor = InkingChangeAnchor::LeftTop;
            input.traceAnchor = InkingChangeAnchor::LeftTop;
            p->AddChild(std::make_unique<InkingTextInput>(p, input));

            AddButton(p, "btn_save_page", 24, 116, 140, 40, "保存", kAccent,
                      kAccentHi, kAccentPr)
                ->clicked.Subscribe([this] { Say("页内按钮：保存"); });
            AddButton(p, "btn_reset_page", 176, 116, 140, 40, "重置", kGhost,
                      kGhostHi, theme::kSurfaceSunken, kGhostText)
                ->clicked.Subscribe([this] { Say("页内按钮：重置"); });

            inking_prefab::ToggleData sw = inking_prefab::MakeSwitchData();
            sw.name = "switch_boot";
            sw.checked = true;
            sw.offsetX = 24.0f;
            sw.offsetY = 186.0f;
            p->AddChild(std::make_unique<inking_prefab::Switch>(p, sw));
            AddLabel(p, "lb_boot", 80, 182, 220, 30, "开机启动", 15.0f,
                     theme::kTextPrimary);

            inking_prefab::ToggleData check = inking_prefab::MakeCheckboxData();
            check.name = "check_telemetry";
            check.offsetX = 24.0f;
            check.offsetY = 232.0f;
            p->AddChild(std::make_unique<inking_prefab::Checkbox>(p, check));
            AddLabel(p, "lb_telemetry", 56, 228, 260, 26, "发送匿名统计", 15.0f,
                     theme::kTextPrimary);

            AddLabel(p, "lb_page_progress", 24, 300, 160, 24, "同步进度", 14.0f,
                     theme::kTextPrimary);
            AddLabel(p, "lb_page_progress_value", 256, 300, 64, 24, "62%", 14.0f,
                     theme::kTextMuted, InkingTextAlignH::Right);
            inking_prefab::ProgressBarData pageBar =
                inking_prefab::ProgressBarData::MakeDefault();
            pageBar.name = "progress_sync";
            pageBar.width = 296;
            pageBar.height = 12;
            pageBar.offsetX = 24.0f;
            pageBar.offsetY = 330.0f;
            pageBar.progress = 0.62f;
            pageBar.track.themeColor = "cardAlt";
            pageBar.fill.themeColor = "accent";
            p->AddChild(std::make_unique<inking_prefab::ProgressBar>(p, pageBar));

            AddLabel(p, "hint_general", 24, 370, 296, 40,
                     "页内容自己摆，TabBar 只切可见性。", 12.0f, theme::kTextMuted);

        }

        // --- 第二页：外观 ---
        {
            InkingPanelData pageData = MakeCard("page_appearance", pageW, pageH,
                                                theme::kSurface, 0.0f);
            auto page = std::make_unique<InkingStaticPanel>(nullptr, pageData);
            // **先挂进 TabBar、再加子控件**：页里的控件要沿父链认识场景，
            // 否则收不到每帧钩子——换主题时页内控件颜色不跟着变，且不报错。
            const int pageIndex = _tabs->AddPage("外观", std::move(page));
            InkingStaticPanel* p =
                static_cast<InkingStaticPanel*>(_tabs->GetPage(pageIndex));

            AddLabel(p, "cap_appearance", 24, 20, 240, 26, "外观", 19.0f,
                     theme::kTextPrimary);
            AddLabel(p, "lb_theme", 24, 66, 200, 24, "主题色", 14.0f,
                     theme::kTextMuted);

            // ---- 运行期换肤：两块**可点**的色板 ----
            //
            // 这里手写 `InkingButtonData` 而不是用 `MakeButton`：色板代表"主题长
            // 什么样"，它自己**不能**跟着主题变（`MakeButton` 会自动挂 token）。
            struct ChipSpec {
                const char* name;
                const char* theme;
                std::uint32_t fill;
                std::uint32_t border;
            };
            const ChipSpec chips[2] = {
                {"chip_light", "light", 0xFFF7F4FBu, 0xFFD9D3E6u},
                {"chip_dark", "dark", 0xFF1F1D27u, 0xFF4A4558u},
            };
            for (int i = 0; i < 2; ++i) {
                InkingButtonData chip;
                chip.name = chips[i].name;
                chip.width = 64;
                chip.height = 64;
                chip.offsetX = static_cast<float>(24 + i * 76);
                chip.offsetY = 96.0f;
                chip.selfAnchor = InkingChangeAnchor::LeftTop;
                chip.traceAnchor = InkingChangeAnchor::LeftTop;
                chip.shape = InkingShapeSpec::RoundedRect(14.0f);
                chip.normal =
                    InkingButtonAppearance(InkingAppearanceType::Color, chips[i].fill);
                chip.hover = chip.normal;
                chip.onclicked =
                    InkingButtonAppearance(InkingAppearanceType::Color, chips[i].border);
                chip.hoverInheritsNormal = false;
                chip.onclickedInheritsNormal = false;
                chip.stroke.width = 2.0f;
                chip.stroke.color = chips[i].border;

                auto* node = static_cast<InkingStaticButton*>(
                    p->AddChild(std::make_unique<InkingStaticButton>(p, chip)));
                const std::string themeName = chips[i].theme;
                node->clicked.Subscribe([this, themeName] {
                    if (InkingSetActiveTheme(themeName)) {
                        Say("主题：" + themeName + "（版本 " +
                            std::to_string(InkingThemeVersion()) + "）");
                    } else {
                        Say("主题：" + themeName + " 切不过去（表没装？）");
                    }
                    UpdateThemeLabel();
                });
            }

            _themeLabel = AddLabel(p, "lb_theme_now", 168, 100, 160, 24,
                                   "当前：light", 13.0f, theme::kTextMuted);
            AddLabel(p, "hint_theme", 168, 126, 160, 60,
                     "点左边的色板切换；组件要带 token 才会变。", 12.0f,
                     theme::kTextMuted);

            AddLabel(p, "lb_radius", 24, 182, 200, 24, "圆角", 14.0f,
                     theme::kTextPrimary);
            inking_prefab::SliderData radius =
                inking_prefab::SliderData::MakeDefault();
            radius.name = "slider_radius";
            radius.width = 296;
            radius.height = 28;
            radius.offsetX = 24.0f;
            radius.offsetY = 210.0f;
            radius.min = 0.0f;
            radius.max = 32.0f;
            radius.value = 16.0f;
            radius.step = 2.0f;
            radius.track.themeColor = "cardAlt";
            radius.fill.themeColor = "accent";
            radius.thumb.themeColor = "thumb";
            p->AddChild(std::make_unique<inking_prefab::Slider>(p, radius));

            AddLabel(p, "hint_appearance", 24, 260, 296, 60,
                     "这一页放什么都不影响页签本身——切页只改可见性。", 12.0f,
                     theme::kTextMuted);

        }

        // --- 第三页：关于 ---
        {
            InkingPanelData pageData = MakeCard("page_about", pageW, pageH,
                                                theme::kSurface, 0.0f);
            auto page = std::make_unique<InkingStaticPanel>(nullptr, pageData);
            // **先挂进 TabBar、再加子控件**：页里的控件要沿父链认识场景，
            // 否则收不到每帧钩子——换主题时页内控件颜色不跟着变，且不报错。
            const int pageIndex = _tabs->AddPage("关于", std::move(page));
            InkingStaticPanel* p =
                static_cast<InkingStaticPanel*>(_tabs->GetPage(pageIndex));

            AddLabel(p, "cap_about", 24, 20, 240, 26, "关于", 19.0f,
                     theme::kTextPrimary);
            AddLabel(p, "about_1", 24, 64, 296, 26,
                     std::string("Inking ") + InkingVersion + "-" + InkingVersionStage,
                     15.0f, theme::kTextPrimary);
            AddLabel(p, "about_2", 24, 96, 296, 24, "SDL3 " + InkingSdl3_version(),
                     13.0f, theme::kTextMuted);
            AddLabel(p, "about_3", 24, 128, 296, 24,
                     "预制件 10 个 · 基础组件若干", 13.0f, theme::kTextMuted);
            AddLabel(p, "hint_about", 24, 180, 296, 120,
                     "本页是 inking_showcase：一个控件总览，改它比改示例主体安全。",
                     12.0f, theme::kTextMuted);

        }
    }

    // ---- 浮层三件套 ----
    void BuildFloatingLayers() {
        _tip = static_cast<inking_prefab::Tooltip*>(
            AddChild(std::make_unique<inking_prefab::Tooltip>(this)));

        inking_prefab::DialogData dialogData = inking_prefab::DialogData::MakeDefault();
        dialogData.name = "confirm_dialog";
        dialogData.width = 480;
        dialogData.height = 260;
        _dialog = static_cast<inking_prefab::Dialog*>(
            AddChild(std::make_unique<inking_prefab::Dialog>(this, dialogData)));

        AddLabel(_dialog->GetPanelNode(), "dlg_title", 24, 24, 432, 32,
                 "确认删除？", 20.0f, theme::kTextPrimary);
        AddLabel(_dialog->GetPanelNode(), "dlg_body", 24, 68, 432, 60,
                 "这条记录会从本地移除，且不可撤销。", 14.0f, theme::kTextMuted);

        AddButton(_dialog->GetPanelNode(), "dlg_cancel", 24, 180, 140, 42, "取消",
                  kGhost, kGhostHi, theme::kSurfaceSunken, kGhostText)
            ->clicked.Subscribe([this] {
                if (_dialog != nullptr) {
                    _dialog->Cancel();
                }
            });
        AddButton(_dialog->GetPanelNode(), "dlg_ok", 316, 180, 140, 42, "删除",
                  kDanger, 0xFFE05555u, 0xFFB02F2Fu)
            ->clicked.Subscribe([this] {
                if (_dialog != nullptr) {
                    _dialog->Accept();
                }
            });

        // 通知放在最后：它是"最上层"的东西（弹窗开着时也看得见）。
        _toasts = static_cast<inking_prefab::ToastHost*>(
            AddChild(std::make_unique<inking_prefab::ToastHost>(this)));

        _dialog->accepted.Subscribe([this] { Say("对话框：已确认"); });
        _dialog->cancelled.Subscribe([this] { Say("对话框：已取消"); });
    }

    // ---------------- 成员 ----------------
    InkingStaticLabel* _status = nullptr;
    InkingStaticLabel* _themeLabel = nullptr;
    inking_prefab::RadioGroup _radios;
    inking_prefab::TabBar* _tabs = nullptr;
    inking_prefab::ToastHost* _toasts = nullptr;
    inking_prefab::Tooltip* _tip = nullptr;
    inking_prefab::Dialog* _dialog = nullptr;
};

}  // namespace

int main() {
    std::printf("[showcase] Inking %s-%s / SDL3 %s\n", InkingVersion,
                InkingVersionStage, InkingSdl3_version().c_str());

    // 装两张表再建场景：**构造时**的颜色解析就会按当前主题走。
    // 光调 `InkingSetActiveTheme` 不装表是切不动的（返回 false、版本不动）。
    const char* themeEnv = std::getenv("INKING_SHOWCASE_THEME");
    InstallDemoThemes((themeEnv != nullptr && themeEnv[0] != '\0') ? themeEnv
                                                                  : "light");

    ShowcaseScene scene;
    if (!scene.IsRegistered()) {
        std::printf("[showcase] 场景 \"showcase\" 登记被驳回（名字被占）。\n");
        return 1;
    }

    // ---- 截图模式：`INKING_SHOWCASE_SNAPSHOT=<png>` ----
    //
    // 走**离屏**读回（dummy 软件后端 + `SDL_RenderReadPixels`），不是抓屏：
    // 不依赖窗口是否真的显示出来、也不受 DWM / 远程桌面影响。
    // 用途：改完布局先自己看一眼，别让人肉当第一道检查。
    if (const char* shot = std::getenv("INKING_SHOWCASE_SNAPSHOT");
        shot != nullptr && shot[0] != '\0') {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::printf("[showcase] SDL_Init 失败：%s\n", SDL_GetError());
            return 1;
        }
        SDL_Window* window = SDL_CreateWindow("shot", InkingDesignWidth(),
                                              InkingDesignHeight(), 0);
        SDL_Renderer* renderer =
            window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
        if (renderer == nullptr) {
            std::printf("[showcase] 建不出软件渲染器：%s\n", SDL_GetError());
            return 1;
        }
        SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                         InkingDesignHeight(),
                                         SDL_LOGICAL_PRESENTATION_DISABLED);
        // **必须自己开混合**：`InkingWindow` 建好渲染器后会设一次
        // `SDL_BLENDMODE_BLEND`，而这条截图路径绕过了它。不开的话半透明的东西
        // （对话框遮罩最明显）会画成**不透明黑**——看着像"模态把界面整个盖住了"。
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        if (std::getenv("INKING_SHOWCASE_OVERLAY") != nullptr) {
            scene.PresentOverlaysForSnapshot();
        }
        InkingSceneLibrary::SetActiveScene(&scene);

        // `INKING_SHOWCASE_SWITCH_TO=dark`：**先按当前主题跑一帧，再切**，
        // 然后接着跑两帧再截图。这是"运行中换肤"那条路径（版本 +1 → 各组件
        // 在下一帧重查），与"启动时就按新表构造"完全是两回事——后者会掩盖
        // "Advance 没被调用、版本比较没发生"这类问题。
        InkingSceneLibrary::SetActiveScene(&scene);
        scene.TickFrame(1.0f / 60.0f);
        if (const char* switchTo = std::getenv("INKING_SHOWCASE_SWITCH_TO");
            switchTo != nullptr && switchTo[0] != '\0') {
            const bool ok = InkingSetActiveTheme(switchTo);
            std::printf("[showcase] 运行中切主题 -> %s：%s（版本 %llu）\n", switchTo,
                        ok ? "成功" : "失败（表没装？）",
                        static_cast<unsigned long long>(InkingThemeVersion()));
        }
        // 多跑两帧：通知条的尺寸与堆叠位置在每帧钩子里算（只跑一帧会看到
        // "文字被压扁"的中间态）。
        scene.TickFrame(1.0f / 60.0f);
        scene.TickFrame(1.0f / 60.0f);

        SDL_SetRenderDrawColor(renderer, 244, 241, 249, 255);
        SDL_RenderClear(renderer);
        InkingSceneLibrary::RenderScene(renderer);

        SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
        if (surface != nullptr && SDL_SavePNG(surface, shot)) {
            std::printf("[showcase] 截图已写入 %s（%d×%d）\n", shot,
                        surface->w, surface->h);
        } else {
            std::printf("[showcase] 截图失败：%s\n", SDL_GetError());
        }
        if (surface != nullptr) {
            SDL_DestroySurface(surface);
        }
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    // 带系统标题栏：本页只是"看控件"，不需要无边框自绘标题栏那一套。
    InkingWindow::Instance().Show("showcase");
    std::printf("[showcase] 主循环结束。\n");
    return 0;
}
