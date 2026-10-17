#include <prefab/OptionBar.h>

#include <button/InkingStaticButton.h>
#include <detail/InkLog.h>
#include <prefab/PrefabTheme.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace inking::prefab {

namespace {

/// 禁用项的文字色：固定灰（比"按比例调暗"更好预期——用户给什么色都是这个）。
/// 这一版静态按钮没有禁用态，所以"禁用"只能落到"不发事件 + 文字变灰"两件事上。
/// 禁用项的文字色：用调色板里那一档（亮色 / 暗色换主题时一处改）。
constexpr std::uint32_t kDisabledTextColor = theme::kTextDisabled;

/// 项节点的名字（日志里能区分是哪一项；手写数据构造不查库，名字只为可读）。
std::string ItemNodeName(std::size_t index) {
    return "optionItem" + std::to_string(index);
}

/// 把布局规则里那份 padding 加起来（主轴 / 交叉轴各一份）。
float PaddingMain(const LayoutSpacing& padding, bool horizontal) noexcept {
    return horizontal ? padding.left + padding.right
                      : padding.top + padding.bottom;
}

float PaddingCross(const LayoutSpacing& padding, bool horizontal) noexcept {
    return horizontal ? padding.top + padding.bottom
                      : padding.left + padding.right;
}

/**
 * 把样式里的面板数据补成一份**能给基类用**的 `PanelData`：名字兜底 +
 * 布局兜底（竖向 Stack）。
 *
 * 放在匿名命名空间而不是私有成员函数里，是因为它要在**基类构造之前**用掉
 * （成员初始化列表里）——那时 `this` 还不能碰。
 */
PanelData MakePanelData(const OptionBarStyle& style) {
    PanelData panel = style.panel;
    if (panel.name.empty()) {
        panel.name = "optionBar";
    }
    if (!panel.layout.Valid()) {
        // 没声明布局的子节点会**全堆在 (0,0)**——那看起来像 bug，所以给一份
        // 竖向兜底。记一条 WARN 而不是悄悄改：静默兜底正是本项目最怕的那类
        // （`AGENTS.md` §6 第 30 / 43 条那一族）。
        INK_LOG_WARN(std::string(kOptionBarModuleName),
                     "选项栏的 panel.layout 没声明（kind = None）：项会全堆在 "
                     "左上角。这里兜底成竖向 Stack + 间距 6；想横排请显式配 "
                     "axis = Horizontal。名字：" + panel.name);
        panel.layout.kind = LayoutKind::Stack;
        panel.layout.axis = LayoutAxis::Vertical;
        panel.layout.gap = 6.0f;
        panel.layout.align = LayoutAlign::Stretch;
        panel.layout.padding = LayoutSpacing{6.0f, 6.0f, 6.0f, 6.0f};
    }
    return panel;
}

}  // namespace

ButtonData MakeDefaultItemLook() {
    ButtonData look;
    look.name = "optionItemDefault";
    look.width = 0;  // 0 = 用容器宽度（建项时会补上）
    look.height = 40;
    look.shape = ShapeSpec::RoundedRect(theme::kRadiusItem);
    // 亮色场景：白底 + 1px 浅描边；悬停 / 按下用主色的浅紫底（见 PrefabTheme.h）。
    look.normal = ButtonAppearance(AppearanceType::Color, theme::kSurface);
    look.hover = ButtonAppearance(AppearanceType::Color, theme::kAccentSoft);
    look.onclicked = ButtonAppearance(AppearanceType::Color,
                                      theme::kAccentSoftPressed);
    // ⚠️ 必须显式关掉：Normalize() 的规矩是"继承为真就拿 normal 覆盖"
    // （`AGENTS.md` §6 第 43 条），不关的话上面两份 hover / onclicked 白写。
    look.hoverInheritsNormal = false;
    look.onclickedInheritsNormal = false;
    // 亮色下的层次主要靠这 1px 描边站住（`AGENTS.md` §6 第 105 条那一族）。
    look.stroke.width = 1.0f;
    look.stroke.color = theme::kOutline;
    look.text.fontSize = 16.0f;
    look.text.leftSpace = 14.0f;  // 文字靠左，垂直居中
    look.text.alignV = TextAlignV::Middle;
    look.text.topSpace = 0.0f;
    look.text.path = theme::DefaultFontPath();  // 开源圆体（拿不到就机器默认）
    look.textColor = theme::kTextPrimary;
    return look;
}

OptionBarStyle OptionBarStyle::MakeDefault() {
    OptionBarStyle style;
    style.panel.name = "optionBar";
    style.panel.width = 220;
    style.panel.height = 200;
    style.panel.shape = ShapeSpec::RoundedRect(theme::kRadiusPanel);
    // 面板只有"关 / 开"两态，选项栏不用开合动画 ⇒ 两态同一个颜色。
    // 直接写字段而不是走 `PanelAppearance` 的构造：那个构造是
    // `(颜色, 时长, 缓动, 变换)`，四个参数凑一个颜色读起来反而费劲。
    style.panel.closed.color = theme::kSurface;
    style.panel.open.color = theme::kSurface;
    style.panel.openInheritsClosed = false;
    // 排列：竖向 Stack + 间距 6 + 内缩 6 + 交叉轴拉伸（一列按钮撑满宽度是常态）。
    style.panel.layout.kind = LayoutKind::Stack;
    style.panel.layout.axis = LayoutAxis::Vertical;
    style.panel.layout.gap = 6.0f;
    style.panel.layout.align = LayoutAlign::Stretch;
    style.panel.layout.padding = LayoutSpacing{6.0f, 6.0f, 6.0f, 6.0f};
    style.itemLook = MakeDefaultItemLook();
    style.itemHeight = 40.0f;
    return style;
}

OptionBar::OptionBar(InkingWidget* parent, const OptionBarStyle& style)
    : InkingStaticPanel(parent, MakePanelData(style)), _style(style) {
    // 项列表在构造时是空的（数据由 `SetItems` 给），所以这里不建任何子节点。
    // 布局兜底那条 WARN 在 `MakePanelData` 里（它要在基类构造之前跑）。
    _layerZ = GetZIndex();  // 基类已经把我这一层的 z 写进来了
}

void OptionBar::SetLayerZ(int zIndex) {
    if (_layerZ == zIndex) {
        return;  // 幂等
    }
    _layerZ = zIndex;
    ChangeZIndex(_layerZ);
    // 已建出来的项跟着走：它们和容器是**同一块东西**（`AGENTS.md` §6 第 104 条
    // 那条"背景被盖住、选项浮在上面"的怪象就是两者不一致造成的）。
    for (InkingWidget* child : GetChildren()) {
        if (child != nullptr) {
            child->ChangeZIndex(_layerZ + 1);
        }
    }
}

float OptionBar::itemHeightAt(std::size_t index) const noexcept {
    if (index < _items.size() && _items[index].height > 0.0f) {
        return _items[index].height;
    }
    return _style.itemHeight > 0.0f ? _style.itemHeight : 1.0f;
}

void OptionBar::SetItems(std::vector<OptionItem> items) {
    if (_items == items) {
        return;  // 幂等：内容没变就一个脏都不标、节点一个都不重建
    }
    _items = std::move(items);
    rebuildItems();
}

void OptionBar::rebuildItems() {
    ClearChildren();

    const LayoutSpec& spec = GetLayoutSpec();
    const bool horizontal = spec.axis == LayoutAxis::Horizontal;
    const float containerWidth = static_cast<float>(GetWidth());

    for (std::size_t i = 0; i < _items.size(); ++i) {
        const OptionItem& item = _items[i];

        ButtonData look = _style.itemLook;
        look.name = ItemNodeName(i);
        // 宽度：模板没给就用容器宽度——不然"交叉轴不拉伸"的那些配置会画出一个
        // 零宽的项（看着像没建出来）。
        if (look.width <= 0) {
            look.width = containerWidth > 0.0f
                             ? static_cast<int>(containerWidth + 0.5f)
                             : 0;
        }
        look.height = static_cast<int>(itemHeightAt(i) + 0.5f);
        look.label = item.text.empty() ? item.id : item.text;
        // 项跟着容器走一层之上：容器画背景、项画在它上面，两者作为一个整体叠放
        // （见 `SetLayerZ` 那段——只给容器高 z 会出现"背景被盖、选项浮顶"）。
        look.zIndex = _layerZ + 1;

        auto node = std::make_unique<InkingStaticButton>(nullptr, look);
        InkingStaticButton* button =
            static_cast<InkingStaticButton*>(AddChild(std::move(node)));
        if (button == nullptr) {
            continue;  // 理论上不会（AddChild 总是成功），但别解引用 nullptr
        }

        if (_style.textColor != 0u) {
            // 调用方可以**统一**指定项的文字色（"用自己那份 json 外观"时特别有用：
            // json 里没有文字色这个字段，见 `CXXCSS.md` §5）。
            button->SetTextColor(_style.textColor);
        }
        if (!item.enabled) {
            button->SetTextColor(kDisabledTextColor);
        }
        // 点击 → 统一出口。捕获索引而不是指针：重建之后索引仍然指同一个位置。
        button->clicked.Subscribe([this, i] { onItemActivated(i); });
    }

    // 排一次（幂等）：布局把每个项的矩形写进它的运行期几何。
    Relayout();
    // 内容尺寸是**显式**量的（§6 第 86 条那条纪律）：滚动范围靠它。
    UpdateScrollRange();
    MarkRepaintDirty();
}

void OptionBar::onItemActivated(std::size_t index) {
    if (index >= _items.size() || !_items[index].enabled) {
        return;  // 禁用项：点了等于没点（不静默——`Activate()` 会返回 false）
    }
    const std::string id = ItemId(index);
    optionClicked.Emit(id, index);
}

bool OptionBar::Activate(std::size_t index) {
    if (index >= _items.size() || !_items[index].enabled) {
        return false;
    }
    onItemActivated(index);
    return true;
}

ShapeBounds OptionBar::GetItemRect(std::size_t index) const {
    const std::vector<InkingWidget*>& children = GetChildren();
    if (index >= children.size() || children[index] == nullptr) {
        return ShapeBounds{};
    }
    const InkingWidget* node = children[index];
    return ShapeBounds{node->GetX(), node->GetY(),
                       static_cast<float>(node->GetWidth()),
                       static_cast<float>(node->GetHeight())};
}

InkingStaticButton* OptionBar::GetItemNode(std::size_t index) const {
    const std::vector<InkingWidget*>& children = GetChildren();
    if (index >= children.size() || children[index] == nullptr) {
        return nullptr;
    }
    return static_cast<InkingStaticButton*>(children[index]);
}

std::string OptionBar::ItemId(std::size_t index) const {
    if (index >= _items.size()) {
        return {};
    }
    if (!_items[index].id.empty()) {
        return _items[index].id;
    }
    return std::to_string(index);
}

std::size_t OptionBar::GetBuiltItemCount() const noexcept {
    return GetOwnedChildCount();
}

LayoutSize OptionBar::MeasureLayout(const LayoutConstraints& constraints) const {
    // ⚠️ 一条**既有的边界**（写在这里免得下一个人找半天）：静态按钮重写了
    // `GetWidth()` / `GetHeight()`（T-5 的逐态尺寸），于是布局通道写进 `_width`
    // 的那份**对按钮不生效**——按钮的尺寸永远取配置里那两份。
    // 所以这里算的是"我这个容器需要多大"，而不是"布局能把项拉多大"；
    // 建项时也已经按容器宽兜底了（见 `rebuildItems`）。
    const LayoutSpec& spec = GetLayoutSpec();
    const bool horizontal = spec.axis == LayoutAxis::Horizontal;
    const float gap = spec.Valid() ? spec.gap : 0.0f;

    const bool explicitWidth = _style.itemLook.width > 0;
    const bool explicitHeight = _style.itemLook.height > 0;
    const float itemWidth = explicitWidth ? static_cast<float>(_style.itemLook.width)
                                          : static_cast<float>(GetWidth());

    float mainSum = 0.0f;
    float crossMax = 0.0f;
    for (std::size_t i = 0; i < _items.size(); ++i) {
        const float itemHeight = itemHeightAt(i);
        mainSum += horizontal ? itemWidth : itemHeight;
        const float cross = horizontal ? itemHeight : itemWidth;
        crossMax = cross > crossMax ? cross : crossMax;
        if (i + 1 < _items.size()) {
            mainSum += gap;
        }
    }

    const float main = mainSum + PaddingMain(spec.padding, horizontal);
    const float crossContent = crossMax + PaddingCross(spec.padding, horizontal);

    if (horizontal) {
        // 横排：宽度是内容说了算；高度听父级的（没给约束、也没显式项高时用自己的高）。
        const float height = constraints.maxHeight > 0.0f
                                 ? constraints.maxHeight
                                 : (explicitHeight ? crossContent
                                                   : static_cast<float>(GetHeight()));
        return LayoutSize{main, height};
    }
    // 竖排：高度是内容说了算；宽度听父级的（同上）。
    const float width = constraints.maxWidth > 0.0f
                            ? constraints.maxWidth
                            : (explicitWidth ? crossContent
                                             : static_cast<float>(GetWidth()));
    return LayoutSize{width, main};
}

}  // namespace inking::prefab
