// 菜单浮窗的实现（`include/prefab/MenuPopup.h`）。
//
// 三件事：竖排布局（构造时把 `LayoutSpec` 交给基类、项数变了重算高度）、
// 项注册（点击与局部快捷键走同一条激活路径）、局部快捷键（自己的表，不碰全局）。

#include <prefab/MenuPopup.h>

#include <button/InkingStaticButton.h>
#include <button/ButtonLook.h>
#include <dataStruct/InkingDecoration.h>
#include <detail/InkLog.h>
#include <prefab/PrefabTheme.h>
#include <window/InkingWindow.h>

namespace inking {
namespace prefab {
namespace {

constexpr const char* kMenuPopupModuleName = "MenuPopup";

/// `MenuPopupStyle` → `PanelData`：只补"调用方没显式指定"的那几项。
///
/// 布局规则在这里落地（`InkingWidget` 没有运行期 `SetLayoutSpec`，只能构造时给）：
/// 竖排 + 固定间距 + 四边等距内边距 + 交叉轴拉满（项宽度跟着菜单走）。
PanelData MakePanelData(const MenuPopupStyle& style) {
    PanelData panel = style.panel;

    if (panel.width <= 0) {
        panel.width = style.itemWidth + style.padding * 2;
    }
    if (panel.height <= 0) {
        panel.height = style.padding * 2;  // 空菜单的最小高度；有项时 `relayoutHeight` 重算
    }
    if (panel.zIndex == 0) {
        panel.zIndex = style.zIndex;
    }

    if (!panel.layout.Valid()) {
        LayoutSpec spec;
        spec.kind = LayoutKind::Stack;
        spec.axis = LayoutAxis::Vertical;
        spec.gap = static_cast<float>(style.gap);
        spec.align = LayoutAlign::Stretch;
        const float pad = static_cast<float>(style.padding);
        spec.padding = LayoutSpacing{pad, pad, pad, pad};
        panel.layout = spec;
    }
    return panel;
}

}  // namespace

MenuPopupStyle MenuPopupStyle::MakeDefault() {
    MenuPopupStyle style;

    style.textColor = theme::kTextPrimary;

    // 容器：白卡片 + 1px 浅紫灰描边 + 一层柔和阴影。阴影是"浮层"的关键——
    // 没有它，菜单和底下的白卡片会糊在一起（这是浮窗和普通面板唯一的视觉差别）。
    style.panel.name = "menu_popup";
    style.panel.width = style.itemWidth + style.padding * 2;
    style.panel.height = style.padding * 2;
    style.panel.zIndex = style.zIndex;
    style.panel.shape = ShapeSpec::RoundedRect(theme::kRadiusPanel);
    style.panel.closed.color = theme::kSurface;
    style.panel.closed.stroke = StrokeSpec{1.0f, theme::kOutline};
    style.panel.closed.shadow = ShadowSpec{0.0f, 6.0f, 0x33000000u};

    // 项：白底、悬停浅紫、圆角（行是表格单元才用直角，菜单项不是）。
    style.itemLook.width = style.itemWidth;
    style.itemLook.height = style.itemHeight;
    style.itemLook.shape = ShapeSpec::RoundedRect(theme::kRadiusItem);
    style.itemLook.normal =
        ButtonAppearance(AppearanceType::Color, theme::kSurface);
    style.itemLook.hover =
        ButtonAppearance(AppearanceType::Color, theme::kAccentSoft);
    style.itemLook.onclicked =
        ButtonAppearance(AppearanceType::Color, theme::kAccentSoftPressed);
    style.itemLook.text.fontSize = 20.0f;
    style.itemLook.text.align = TextAlignH::Left;
    style.itemLook.text.alignV = TextAlignV::Middle;
    style.itemLook.textColor = theme::kTextPrimary;
    return style;
}

MenuPopup::MenuPopup(InkingWidget* parent, const MenuPopupStyle& style)
    : InkingFixedPopup(parent, MakePanelData(style)), _style(style) {
    // 空菜单也要有一次布局：`Padding × 2` 的高度是 `MakePanelData` 给的，
    // 但内容区还没有项——跑一次让内部的布局结果与尺寸对齐（幂等）。
    Relayout();
}

// ---------------------------------------------------------------------------
// 内容
// ---------------------------------------------------------------------------

InkingWidget* MenuPopup::AddItem(std::unique_ptr<InkingWidget> item) {
    InkingWidget* raw = InkingFixedPopup::AddItem(std::move(item));
    if (raw == nullptr) {
        return nullptr;
    }
    _items.push_back(raw);
    relayoutHeight();
    return raw;
}

InkingWidget* MenuPopup::AddItem(const ItemFactory& factory) {
    if (!factory) {
        return nullptr;
    }
    return AddItem(factory());
}

InkingStaticButton* MenuPopup::AddButton(const ButtonData& look,
                                        const std::string& text,
                                        Action onActivate,
                                        const Shortcut& shortcut) {
    ButtonData use = look;
    // 尺寸留 0 = "跟随菜单"：不加这一段，`AddButton(look, text)` 传一份只写了
    // 颜色的 look，项就是 0×0——布局里看得见一个 0 高的洞，不报错。
    if (use.width <= 0) {
        use.width = _style.itemWidth;
    }
    if (use.height <= 0) {
        use.height = _style.itemHeight;
    }
    if (use.name.empty()) {
        use.name = text;
    }

    auto node = std::make_unique<InkingStaticButton>(nullptr, use);
    InkingStaticButton* button = node.get();
    button->SetText(text);
    if (_style.textColor != 0u) {
        button->SetTextColor(_style.textColor);
    }

    const std::size_t index = _items.size();
    InkingWidget* raw = AddItem(std::move(node));
    if (raw == nullptr) {
        return nullptr;
    }

    if (onActivate) {
        // 点击：动作 → 计数 → `onMenuActivated` → 按开关关闭（`activate` 一手包）。
        button->clicked.Subscribe([this, index, callback = onActivate] {
            callback();
            activate(index, _items[index]);
        });

        if (!shortcut.Empty()) {
            // 快捷键走同一条激活路径；关闭由 `onKeyDown` 统一做（幂等，
            // 这里不重复 Close，否则"先执行 Action 再关闭"的顺序会被打乱）。
            BindLocalShortcut(
                shortcut,
                [this, index, callback = onActivate] {
                    callback();
                    ++_activatedCount;
                    onMenuActivated(index, _items[index]);
                },
                text);
        }
    }
    return button;
}

void MenuPopup::relayoutHeight() {
    const int count = static_cast<int>(_items.size());
    int height = _style.padding * 2;
    if (count > 0) {
        height += count * _style.itemHeight + (count - 1) * _style.gap;
    }
    // `Resize` 内部会在布局规则有效时重排一次，所以这里不必再喊 `Relayout()`。
    Resize(_style.itemWidth + _style.padding * 2, height);
}

void MenuPopup::activate(std::size_t index, InkingWidget* item) {
    ++_activatedCount;
    onMenuActivated(index, item);
    if (_style.closeOnActivate) {
        Close();
    }
}

void MenuPopup::onMenuActivated(std::size_t /*index*/, InkingWidget* /*item*/) {}

// ---------------------------------------------------------------------------
// 局部快捷键
// ---------------------------------------------------------------------------

bool MenuPopup::BindLocalShortcut(const Shortcut& shortcut, Action action,
                                  std::string label) {
    if (shortcut.Empty() || !action) {
        return false;
    }
    if (label.empty()) {
        label = shortcut.ToString();
    }

    // 全局表优先：窗口层**先**跑 `ShortcutRegistry` 并消费掉按键，所以这里
    // 被占用的组合永远收不到。绑之前就报出来，别让它变成"按了没反应"。
    if (InkingWindow::Instance().GetShortcuts().IsTaken(shortcut)) {
        INK_LOG_WARN(kMenuPopupModuleName,
                     "局部快捷键与全局表冲突，已忽略：" + label);
        return false;
    }

    // 局部表内部先到先得（与 `ShortcutRegistry` 同一条：不静默覆盖）。
    for (const LocalShortcut& entry : _localShortcuts) {
        if (entry.shortcut == shortcut) {
            INK_LOG_WARN(kMenuPopupModuleName,
                         "局部快捷键重复绑定，已忽略：" + label);
            return false;
        }
    }

    _localShortcuts.push_back(LocalShortcut{shortcut, std::move(action),
                                            std::move(label)});
    return true;
}

void MenuPopup::onKeyDown(const KeyEvent& key) {
    if (!IsOpen()) {
        return;
    }
    for (const LocalShortcut& entry : _localShortcuts) {
        if (!entry.shortcut.Matches(key)) {
            continue;
        }
        // 拷贝一份再调：回调里可能改这张表（绑新的 / 关菜单）。
        Action action = entry.action;
        action();
        // 项自己可能已经关了（`activate` 里做的），这里再关一次是幂等的；
        // 用户自己绑的动作则由这一句负责关。
        if (_style.closeOnActivate) {
            Close();
        }
        return;  // 一次按键只做一个动作
    }
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

ShapeBounds MenuPopup::GetItemRect(std::size_t index) const {
    if (index >= _items.size() || _items[index] == nullptr) {
        return ShapeBounds{};
    }
    const InkingWidget* node = _items[index];
    return ShapeBounds{node->GetX(), node->GetY(),
                       static_cast<float>(node->GetWidth()),
                       static_cast<float>(node->GetHeight())};
}

InkingWidget* MenuPopup::GetItemNode(std::size_t index) const {
    return index < _items.size() ? _items[index] : nullptr;
}

}  // namespace prefab
}  // namespace inking
