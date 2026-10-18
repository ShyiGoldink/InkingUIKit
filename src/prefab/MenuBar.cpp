#include <prefab/MenuBar.h>

#include <button/InkingStaticButton.h>
#include <detail/InkLog.h>
#include <prefab/PrefabTheme.h>
#include <window/InkingWindow.h>

#include <cmath>
#include <cstdint>
#include <utility>

namespace inking::prefab {

namespace {

/// 内容容器 / 弹簧那种"只占位不露面"的东西用的颜色：alpha = 2/255。
///
/// **不能写全透明**：`PanelData::Normalize()`（和 `ButtonData::Normalize()` 同一条
/// 规矩）会把"三态 alpha 全 0"兜底成不透明黑——于是弹簧会变成一块黑方块盖住
/// 菜单栏背景（`AGENTS.md` §6 第 52 条记的正是这个坑）。
constexpr std::uint32_t kInvisibleColor = 0x02000000u;

/**
 * 菜单栏自己那份面板数据：把风格里的排列规则、尺寸、背景搬过来。
 */
PanelData MakePanelData(const MenuBarStyle& style) {
    PanelData panel = style.panel;
    if (panel.name.empty()) {
        panel.name = "menuBar";
    }
    if (!panel.layout.Valid()) {
        // 没声明布局：所有项会叠在 (0,0)。给一份横向兜底 + 记一条 WARN
        // （不静默——静默兜底正是本项目最怕的那类）。
        INK_LOG_WARN(std::string(kMenuBarModuleName),
                     "菜单栏的 panel.layout 没声明（kind = None）：项会叠在左上角。"
                     "这里兜底成横向 Stack + 偏左 + 交叉轴居中。名字："
                         + panel.name);
        panel.layout.kind = LayoutKind::Stack;
        panel.layout.axis = LayoutAxis::Horizontal;
        panel.layout.justify = LayoutJustify::Start;
        panel.layout.align = LayoutAlign::Center;
        panel.layout.gap = 6.0f;
        panel.layout.padding = LayoutSpacing{6.0f, 2.0f, 6.0f, 2.0f};
    }
    return panel;
}

}  // namespace

ButtonData MakeDefaultCaptionLook() {
    ButtonData look;
    look.name = "menuBarCaption";
    look.width = 40;
    look.height = 28;
    look.shape = ShapeSpec::RoundedRect(theme::kRadiusCaption);
    // **常态是不透明白**（用户点名要的）：标题栏按钮是"白按钮"，悬停 / 按下才铺浅紫。
    look.normal = ButtonAppearance(AppearanceType::Color, theme::kSurface);
    look.hover = ButtonAppearance(AppearanceType::Color, theme::kAccentSoft);
    look.onclicked = ButtonAppearance(AppearanceType::Color,
                                      theme::kAccentSoftPressed);
    look.hoverInheritsNormal = false;
    look.onclickedInheritsNormal = false;
    look.text.fontSize = 15.0f;
    // 标题栏按钮的文字（`—` / `×`）**居中**：以前只能靠 leftSpace 手填估算值，
    // 比例字体下必然歪（这是"按钮文字补上对齐能力"那条的直接受益者）。
    look.text.align = TextAlignH::Center;
    look.text.alignV = TextAlignV::Middle;
    look.text.leftSpace = 0.0f;  // 居中时不用它
    look.text.topSpace = 0.0f;
    look.text.path = theme::DefaultFontPath();
    // 深紫的两个符号（用户点名要的）——它们是标题栏上仅有的"图形"，用主色系最合。
    look.textColor = theme::kAccentStrong;
    return look;
}

MenuBarStyle MenuBarStyle::MakeDefault() {
    MenuBarStyle style;
    style.panel.name = "menuBar";
    style.panel.width = 1280;  // 设计画布宽（窗口顶部一条）
    style.panel.height = 36;
    // 菜单栏 / 工具栏该在内容之上：默认给一层 100（普通内容是 0）。
    // 下拉展开那一档用 200（`SelectStyle::expandedZIndex`），所以下拉能盖住
    // 内容、又盖不住菜单栏——正好是桌面应用该有的层次。
    //
    // A8b（局部层级）之后补一句：z 只在**兄弟之间**生效，所以这里的 100 仍然是
    // "我这层压住内容"，而下拉是菜单栏的子孙、天然在它之上；200 那档自此只是
    // "在菜单栏内部排最后"，不会再有跨级效果（无害，别再指望它压住别的兄弟）。
    style.panel.zIndex = 100;
    style.panel.shape = ShapeSpec::Rect();  // 顶部通栏，不做圆角
    style.panel.closed.color = theme::kSurface;
    style.panel.open.color = theme::kSurface;
    style.panel.openInheritsClosed = false;
    style.panel.traceAnchor = InkingChangeAnchor::LeftTop;
    style.panel.selfAnchor = InkingChangeAnchor::LeftTop;
    style.panel.offsetX = 0.0f;
    style.panel.offsetY = 0.0f;
    style.panel.layout.kind = LayoutKind::Stack;
    style.panel.layout.axis = LayoutAxis::Horizontal;
    style.panel.layout.justify = LayoutJustify::Start;  // 偏左（默认）
    style.panel.layout.align = LayoutAlign::Center;     // 交叉轴居中
    style.panel.layout.gap = 6.0f;
    style.panel.layout.padding = LayoutSpacing{6.0f, 2.0f, 6.0f, 2.0f};
    style.captionLook = MakeDefaultCaptionLook();
    style.textColor = theme::kTextPrimary;  // 菜单项文字（用自己那份 json 外观时靠它补）
    return style;
}

MenuBar::MenuBar(InkingWidget* parent, const MenuBarStyle& style)
    : InkingStaticPanel(parent, MakePanelData(style)), _style(style) {
    if (_style.captionButtons) {
        AddCaptionButtons();
    }

    // 全屏联动（C 需求）：**订阅事件**，不是每帧轮询。
    // 在 `onRender` 里改可见性属于"提交期改结构"——那时场景正在遍历绘制列表，
    // 改骨架就是悬垂的温床（`InkingScene` 把销毁攒到帧末正是这个理由）。
    _fullscreenToken = InkingWindow::Instance().fullscreenChanged.Subscribe(
        [this](bool fullscreen) {
            if (!_style.hideInFullscreen) {
                return;
            }
            if (fullscreen) {
                // 先记账再隐藏：退出全屏只恢复"被这条规则藏起来的那一次"，
                // 不覆盖业务自己调过的 `SetVisible(false)`。
                _hiddenByFullscreen = IsVisible();
                SetVisible(false);
            } else if (_hiddenByFullscreen) {
                SetVisible(true);
                _hiddenByFullscreen = false;
            }
        });

    // 构造时就可能已经是全屏（先 `SetFullscreen(true)` 再建场景），补一次。
    if (_style.hideInFullscreen && InkingWindow::Instance().IsFullscreen()) {
        _hiddenByFullscreen = IsVisible();
        SetVisible(false);
    }
}

MenuBar::~MenuBar() {
    // 两样都住在**进程级**对象里，比菜单栏活得久，必须逐个摘掉：
    //   ① 窗口的全屏事件（`InkingWindow` 是单例，事件槽跟着它走）；
    //   ② 窗口的快捷键表（回调里 capture 了 `this`）。
    // 漏任何一条，症状都是"关掉场景之后按快捷键 / 切全屏就崩"，且极难复现。
    InkingWindow& window = InkingWindow::Instance();
    if (_fullscreenToken != 0) {
        window.fullscreenChanged.Unsubscribe(_fullscreenToken);
        _fullscreenToken = 0;
    }
    for (const std::uint64_t id : _shortcutIds) {
        window.GetShortcuts().Unbind(id);
    }
    _shortcutIds.clear();
}

// ---------------------------------------------------------------------------
// 内容项
// ---------------------------------------------------------------------------

InkingWidget* MenuBar::AddItem(std::unique_ptr<InkingWidget> item) {
    if (item == nullptr) {
        return nullptr;
    }
    InkingWidget* raw = AddChild(std::move(item));
    if (raw != nullptr) {
        // 项跟着容器走一层之上：菜单栏的背景与它的按钮/图片作为一个整体叠放
        // （同 `OptionBar::SetLayerZ` 那条——只给容器高 z 会"背景在上、内容被盖"）。
        raw->ChangeZIndex(GetZIndex() + 1);
        _items.push_back(raw);
    }
    // 窗口按钮（弹簧 + 最小化 + 关闭）必须排在**最后**：新加的内容项会插到
    // 它们前面，所以每次加完都重建一次那三个节点顺序（见头文件顶上那段）。
    if (_style.captionButtons) {
        RebuildCaptions();
    }
    Relayout();
    return raw;
}

InkingWidget* MenuBar::AddItem(const ItemFactory& factory) {
    if (!factory) {
        return nullptr;
    }
    return AddItem(factory());
}

InkingStaticButton* MenuBar::AddButton(const ButtonData& look,
                                       const std::string& text,
                                       ButtonCallback onActivate,
                                       const Shortcut& shortcut) {
    auto node = std::make_unique<InkingStaticButton>(nullptr, look);
    InkingStaticButton* button = node.get();
    button->SetText(text);
    if (_style.textColor != 0u) {
        // 统一文字色（用自己那份 json 外观时特别有用：json 里没有文字色，见
        // `CXXCSS.md` §5）。
        button->SetTextColor(_style.textColor);
    }
    InkingWidget* raw = AddItem(std::move(node));
    if (raw == nullptr) {
        return nullptr;
    }
    if (onActivate) {
        button->clicked.Subscribe(
            [callback = onActivate] { callback(); });
        // 快捷键与点击是**同一个动作**（用户按 Ctrl+S 和点"保存"不该有差别），
        // 所以两处挂的是同一份回调的副本。
        if (!shortcut.Empty()) {
            BindShortcut(shortcut, onActivate, text);
        }
    }
    return static_cast<InkingStaticButton*>(raw);
}

// ---------------------------------------------------------------------------
// 全局快捷键（B 需求）
// ---------------------------------------------------------------------------

std::uint64_t MenuBar::BindShortcut(const Shortcut& shortcut,
                                    ButtonCallback onActivate,
                                    std::string label) {
    if (shortcut.Empty() || !onActivate) {
        return 0;
    }
    if (label.empty()) {
        label = shortcut.ToString();
    }
    const std::uint64_t id = InkingWindow::Instance().GetShortcuts().Bind(
        shortcut, std::move(onActivate), std::move(label));
    if (id != 0) {
        // 记下来：析构时要还回去（表比本对象活得久）。
        _shortcutIds.push_back(id);
    }
    return id;
}

std::size_t MenuBar::GetBoundShortcutCount() const noexcept {
    return _shortcutIds.size();
}

std::size_t MenuBar::GetItemCount() const noexcept { return _items.size(); }

ShapeBounds MenuBar::GetItemRect(std::size_t index) const {
    if (index >= _items.size() || _items[index] == nullptr) {
        return ShapeBounds{};
    }
    const InkingWidget* node = _items[index];
    return ShapeBounds{node->GetX(), node->GetY(),
                       static_cast<float>(node->GetWidth()),
                       static_cast<float>(node->GetHeight())};
}

InkingWidget* MenuBar::GetItemNode(std::size_t index) const {
    if (index >= _items.size()) {
        return nullptr;
    }
    return _items[index];
}

// ---------------------------------------------------------------------------
// 对齐
// ---------------------------------------------------------------------------

void MenuBar::SetJustify(LayoutJustify justify) {
    if (_layout.justify == justify) {
        return;  // 幂等
    }
    // `_layout` 是基类的 `protected` 成员——派生类可以直接改（静态档的
    // "几何定死"针对的是**运行期几何**，布局规则本来就是配置的一部分）。
    // 改完显式重排一次：框架不做隐式重排（`AGENTS.md` §6 第 86 条）。
    _layout.justify = justify;
    Relayout();
}

LayoutJustify MenuBar::GetJustify() const noexcept {
    return _layout.justify;
}

// ---------------------------------------------------------------------------
// 自绘标题栏（无边框窗口）
// ---------------------------------------------------------------------------

void MenuBar::RebuildCaptions() {
    // 先把旧的弹簧与按钮摘掉（`RemoveChild` 拿回所有权，不接返回值 = 就地销毁）。
    // **不能**对拥有的子节点直接 `SetParent`（`AGENTS.md` §6 第 67 条）。
    if (_spacer != nullptr) {
        RemoveChild(_spacer);
        _spacer = nullptr;
    }
    for (InkingStaticButton* button : _captionButtons) {
        if (button != nullptr) {
            RemoveChild(button);
        }
    }
    _captionButtons.clear();

    // 弹簧：宽 0 + fillMain → 主轴剩余空间全归它，后面的按钮自然贴右边。
    AnchorData spacerData;
    spacerData.width = 0;
    spacerData.height = 1;
    spacerData.color = kInvisibleColor;
    spacerData.layoutItem.fillMain = true;
    _spacer = static_cast<InkingStaticWidget*>(
        AddChild(std::make_unique<InkingStaticWidget>(nullptr, spacerData)));
    if (_spacer != nullptr) {
        _spacer->ChangeZIndex(GetZIndex() + 1);
    }

    // 最小化 / 关闭。文字用 "—" / "×"（不引图标：图标列在第一版不做）。
    ButtonData look = _style.captionLook;
    look.width = static_cast<int>(_style.captionWidth + 0.5f);
    look.height = GetHeight() > 0 ? GetHeight() - 4 : look.height;
    look.label = "—";
    InkingStaticButton* minimize = static_cast<InkingStaticButton*>(
        AddChild(std::make_unique<InkingStaticButton>(nullptr, look)));
    if (minimize != nullptr) {
        minimize->ChangeZIndex(GetZIndex() + 1);
        minimize->clicked.Subscribe([this] {
            InkingWindow::Instance().MinimizeWindow();
            minimizeClicked.Emit();
        });
        _captionButtons.push_back(minimize);
    }

    look.label = "×";
    InkingStaticButton* close = static_cast<InkingStaticButton*>(
        AddChild(std::make_unique<InkingStaticButton>(nullptr, look)));
    if (close != nullptr) {
        close->ChangeZIndex(GetZIndex() + 1);
        close->clicked.Subscribe([this] {
            closeClicked.Emit();
            InkingWindow::Instance().RequestClose();
        });
        _captionButtons.push_back(close);
    }
}

void MenuBar::AddCaptionButtons() {
    if (!_style.captionButtons) {
        _style.captionButtons = true;
    }
    RebuildCaptions();
    Relayout();
}

std::size_t MenuBar::GetCaptionButtonCount() const noexcept {
    return _captionButtons.size();
}

// ---------------------------------------------------------------------------
// 拖动（无边框下的自绘标题栏）
// ---------------------------------------------------------------------------

void MenuBar::onPointerPress(bool press) {
    _dragging = press && _style.draggable;
    InkingStaticPanel::onPointerPress(press);  // 配了滚动时那条路照旧
}

void MenuBar::onPointerDrag(float designX, float designY, float deltaX,
                            float deltaY) {
    InkingStaticPanel::onPointerDrag(designX, designY, deltaX, deltaY);

    if (!_dragging) {
        return;
    }
    // 设计坐标 → **屏幕像素**：letterbox 是整幅等比缩放，所以乘展示倍率即可。
    // delta 是"相对上一次拖动事件"的位移（只有第一次相对按下点），直接累加
    // ——这正是 `AGENTS.md` §6 第 96 条③ 那条实测（写成"本次位移"会每帧从
    // 按下时的基准重算，手感是"拉一点就弹回去"）。
    const float magnification = GetMagnification() > 0.0f ? GetMagnification()
                                                          : 1.0f;
    const int dx = static_cast<int>(std::lround(deltaX * magnification));
    const int dy = static_cast<int>(std::lround(deltaY * magnification));
    if (dx != 0 || dy != 0) {
        InkingWindow::Instance().MoveWindowBy(dx, dy);
    }
}

void MenuBar::SetHideInFullscreen(bool hide) noexcept {
    _style.hideInFullscreen = hide;
    // 运行期关掉这条规则时，把"之前被它藏起来的"放回去——否则会留下一个
    // 明明没全屏、却怎么也不出现的菜单栏。
    if (!hide && _hiddenByFullscreen) {
        SetVisible(true);
        _hiddenByFullscreen = false;
    }
}

// ---------------------------------------------------------------------------
// 滚轮缩放（无边框窗口的代偿，D 需求）
// ---------------------------------------------------------------------------

bool MenuBar::HandlesSubtreeWheel() const noexcept {
    // 为什么必须重写：场景只把滚轮给"指针下最上面那个"（`InkingScene` 的
    // 派发第 5 步）。指针停在菜单**按钮**上时那个节点是按钮而不是菜单栏，
    // 不声明这条，滚轮就派给按钮、然后什么都不会发生——表现正是"菜单栏上
    // 滚轮没反应"，而把指针挪到空白处又好了（最难查的那种半好半坏）。
    return _style.zoomWheel || InkingStaticPanel::HandlesSubtreeWheel();
}

void MenuBar::onPointerWheel(float deltaX, float deltaY) {
    if (_style.zoomWheel) {
        const int step = _style.zoomWheelStep > 0 ? _style.zoomWheelStep : 1;
        // 滚轮向上（远离用户，`deltaY > 0`）= 放大——和几乎所有看图 / 编辑器一致。
        if (deltaY > 0.0f) {
            InkingWindow::Instance().ZoomBy(step);
            return;
        }
        if (deltaY < 0.0f) {
            InkingWindow::Instance().ZoomBy(-step);
            return;
        }
        // 只有横向滚动（deltaY == 0）：不吞掉，交给面板那条滚动路径。
    }
    InkingStaticPanel::onPointerWheel(deltaX, deltaY);
}

}  // namespace inking::prefab
