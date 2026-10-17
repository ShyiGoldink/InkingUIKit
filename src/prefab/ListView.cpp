#include <prefab/ListView.h>

#include <detail/InkLog.h>
#include <prefab/ListRow.h>
#include <prefab/PrefabTheme.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

namespace inking::prefab {

namespace {

/// 占位块（撑位置用）的颜色：alpha = 2/255。
///
/// **不能写全透明**：`PanelData::Normalize()` 会把"三态 alpha 全 0"兜底成不透明黑
/// （和 `ButtonData` 同一条规矩，`AGENTS.md` §6 第 52 条），那样占位块会变成一块
/// 黑条压在列表上。给一个极小但非 0 的 alpha：看不见，但仍然参与合成。
constexpr std::uint32_t kInvisibleColor = 0x02000000u;

/// 禁用行的文字色（和 C1 同一个做法：静态按钮没有禁用态，只能"不发事件 + 文字变灰"）。
constexpr std::uint32_t kDisabledTextColor = theme::kTextDisabled;

PanelData MakePanelData(const ListViewStyle& style) {
    PanelData panel = style.panel;
    if (panel.name.empty()) {
        panel.name = "listView";
    }
    if (!panel.layout.Valid()) {
        INK_LOG_WARN(std::string(kListModuleName),
                     "列表的 panel.layout 没声明（kind = None）：行会叠在左上角。"
                     "这里兜底成竖向 Stack + gap 0 + 交叉轴拉伸。名字："
                         + panel.name);
        panel.layout.kind = LayoutKind::Stack;
        panel.layout.axis = LayoutAxis::Vertical;
        panel.layout.gap = 0.0f;
        panel.layout.align = LayoutAlign::Stretch;
    }
    if (panel.layout.axis != LayoutAxis::Vertical) {
        // 列表这一版只做竖排：窗口化靠"行高 × 索引"直接算位置，横排没有对应的
        // 语义（也没有那种需求）。不静默——记一条 WARN 再按竖排走。
        INK_LOG_WARN(std::string(kListModuleName),
                     "列表这一版只支持竖向排列（axis = Vertical）；配置里写的是"
                     "横排，这里按竖排处理。名字：" + panel.name);
        panel.layout.axis = LayoutAxis::Vertical;
    }
    if (!panel.scroll.enabled) {
        // 列表不能滚就不是列表：滚动是它的**能力**而不是可选项（CXXCSS 那边
        // 也是这个口径：`PanelData::scroll` 是容器的一种能力）。
        panel.scroll.enabled = true;
    }
    if (!panel.clipChildren) {
        // 滚出去的内容必须被剪掉：绘制与命中同源（A5），所以"看不见的行也点不到"
        // 是白拿的。不打开的话内容会画到列表框外面去。
        panel.clipChildren = true;
    }
    return panel;
}

}  // namespace

ButtonData MakeDefaultRowLook() {
    ButtonData look;
    look.name = "listRowDefault";
    look.width = 0;  // 0 = 用视口宽度（建行时会补上）
    look.height = 32;
    // **行用直角**（`Rect`，不是 `RoundedRect`）：行是**表格单元**——一行挨一行时
    // 圆角会让每一行看起来像一块独立的小卡片，排起来"很奇怪"（用户看过第一版后
    // 的反馈）。要圆角的是**按钮**（选项 / 菜单项 / 底部按钮），不是列表行。
    look.shape = ShapeSpec::Rect();
    // **常态是不透明白**（用户点名要的）：行是一块"白卡片"，悬停 / 按下 / 选中
    // 才铺浅紫底。视口因此要比它沉一档（`kSurfaceSunken`），白行才浮得起来。
    look.normal = ButtonAppearance(AppearanceType::Color, theme::kSurface);
    look.hover = ButtonAppearance(AppearanceType::Color, theme::kAccentSoft);
    look.onclicked = ButtonAppearance(AppearanceType::Color,
                                      theme::kAccentSoftPressed);
    // 显式关掉"省略即继承"（`AGENTS.md` §6 第 43 条）。
    look.hoverInheritsNormal = false;
    look.onclickedInheritsNormal = false;
    look.text.fontSize = 15.0f;
    look.text.leftSpace = 14.0f;
    look.text.alignV = TextAlignV::Middle;  // 行内文字垂直居中
    look.text.topSpace = 0.0f;
    look.text.path = theme::DefaultFontPath();
    look.textColor = theme::kAccentStrong;  // 深紫的字（用户点名要的）
    return look;
}

ListViewStyle ListViewStyle::MakeDefault() {
    ListViewStyle style;
    style.panel.name = "listView";
    style.panel.width = 320;
    style.panel.height = 400;
    style.panel.shape = ShapeSpec::RoundedRect(theme::kRadiusPanel);
    // 视口用**承载面**（浅紫灰）：行是白卡片，白行浮在比它沉一档的底上才看得清
    // （用户点名要"行常态是白的"——那就得让视口不是白的）。
    style.panel.closed.color = theme::kSurfaceSunken;
    style.panel.open.color = theme::kSurfaceSunken;
    style.panel.openInheritsClosed = false;
    style.panel.clipChildren = true;
    style.panel.scroll.enabled = true;
    // 亮色下的滚动条：极淡的轨道 + 半透明主色滑块。
    style.panel.scroll.barColor = theme::kScrollTrack;
    style.panel.scroll.thumbColor = theme::kScrollThumb;
    style.panel.scroll.thumbHoverColor = theme::kScrollThumbHover;
    style.panel.layout.kind = LayoutKind::Stack;
    style.panel.layout.axis = LayoutAxis::Vertical;
    style.panel.layout.gap = 0.0f;
    style.panel.layout.align = LayoutAlign::Stretch;
    style.panel.layout.padding = LayoutSpacing{};
    style.rowLook = MakeDefaultRowLook();
    style.rowHeight = 32.0f;
    style.selectedColor = theme::kAccentSoftPressed;  // 选中行：实一点的浅紫
    style.textColor = theme::kAccentStrong;           // 深紫的字
    style.selection = ListSelectionMode::Single;
    style.windowed = false;
    style.overscan = 2;
    return style;
}

ListView::ListView(InkingWidget* parent, const ListViewStyle& style)
    : InkingStaticPanel(parent, MakePanelData(style)), _style(style) {
    if (_style.rowHeight <= 0.0f) {
        INK_LOG_WARN(std::string(kListModuleName),
                     "行高必须为正（行高 × 索引就是行的位置）；这里兜底成 32。"
                     "名字：" + GetName());
        _style.rowHeight = 32.0f;
    }
    if (_style.overscan < 0) {
        _style.overscan = 0;
    }
}

// ---------------------------------------------------------------------------
// 数据
// ---------------------------------------------------------------------------

void ListView::SetItems(std::vector<ListItem> items) {
    if (_items == items) {
        return;  // 幂等：内容没变就什么都不做
    }
    _items = std::move(items);
    pruneSelection();

    // 项列表变了，建出来的区间一定失效 → 清掉"已建区间"再重建。
    _builtFirst = -1;
    _builtLast = -1;
    if (_style.windowed) {
        rebuildVisible();
    } else {
        rebuildAll();
    }
}

std::string ListView::ItemId(int index) const {
    if (index < 0 || index >= static_cast<int>(_items.size())) {
        return {};
    }
    if (!_items[index].id.empty()) {
        return _items[index].id;
    }
    return std::to_string(index);
}

// ---------------------------------------------------------------------------
// 建行
// ---------------------------------------------------------------------------

void ListView::addRowNode(int index) {
    const ListItem& item = _items[static_cast<std::size_t>(index)];

    ButtonData look = _style.rowLook;
    look.name = "listRow" + std::to_string(index);
    look.height = static_cast<int>(_style.rowHeight + 0.5f);
    if (look.width <= 0) {
        look.width = GetWidth();
    }
    look.label = item.text.empty() ? item.id : item.text;

    auto node = std::make_unique<ListRow>(nullptr, look, _style.selectedColor);
    ListRow* row = static_cast<ListRow*>(AddChild(std::move(node)));
    if (row == nullptr) {
        return;
    }
    row->SetSelected(IsSelected(index));
    if (_style.textColor != 0u) {
        row->SetTextColor(_style.textColor);  // 统一行文字色（见 ListViewStyle::textColor）
    }
    if (!item.enabled) {
        row->SetTextColor(kDisabledTextColor);
    }
    // 捕获全局索引：滚动重建之后，同一个索引仍然指同一项。
    row->clicked.Subscribe([this, index] { onRowActivated(index); });
    _rowNodes.push_back(row);
}

void ListView::addSpacer(float height) {
    if (height <= 0.0f) {
        return;
    }
    AnchorData data;
    data.width = 1;
    data.height = static_cast<int>(height + 0.5f);
    data.color = kInvisibleColor;
    AddChild(std::make_unique<InkingStaticWidget>(nullptr, data));
}

void ListView::rebuildAll() {
    ClearChildren();
    _rowNodes.clear();

    const int count = static_cast<int>(_items.size());
    for (int index = 0; index < count; ++index) {
        addRowNode(index);
    }
    _builtFirst = count > 0 ? 0 : -1;
    _builtLast = count > 0 ? count - 1 : -1;

    Relayout();
    UpdateScrollRange();
    MarkRepaintDirty();
}

void ListView::rebuildVisible() {
    const int count = static_cast<int>(_items.size());
    if (count == 0) {
        ClearChildren();
        _rowNodes.clear();
        _builtFirst = -1;
        _builtLast = -1;
        Relayout();
        UpdateScrollRange();
        return;
    }

    const float rowHeight = _style.rowHeight;
    const float scrollY = GetScrollY();
    const float viewport = viewportHeight();

    int first = static_cast<int>(std::floor(scrollY / rowHeight))
                - _style.overscan;
    int last = static_cast<int>(std::floor((scrollY + viewport) / rowHeight))
               + _style.overscan;
    first = std::max(0, std::min(first, count - 1));
    last = std::max(first, std::min(last, count - 1));

    if (first == _builtFirst && last == _builtLast) {
        return;  // 幂等：可见区间没变，一个节点都不重建
    }

    ClearChildren();
    _rowNodes.clear();

    // 前面那块占位：把第一行顶到 `first × 行高`。
    addSpacer(static_cast<float>(first) * rowHeight);
    for (int index = first; index <= last; ++index) {
        addRowNode(index);
    }
    // 后面那块占位：把内容总高补到 `项数 × 行高`（滚动范围靠它）。
    const float covered = static_cast<float>(last + 1) * rowHeight;
    addSpacer(GetContentHeight() - covered);

    _builtFirst = first;
    _builtLast = last;

    Relayout();
    UpdateScrollRange();
    MarkRepaintDirty();
    INK_LOG_DEBUG(std::string(kListModuleName),
                  "可见窗口重建：[" + std::to_string(first) + ", "
                      + std::to_string(last) + "]，共 "
                      + std::to_string(_rowNodes.size()) + " 行节点（总项数 "
                      + std::to_string(count) + "）");
}

void ListView::SyncVisibleWindow() {
    if (!_style.windowed) {
        return;  // 不窗口化时所有行都在，没有"同步窗口"这回事
    }
    rebuildVisible();
}

float ListView::viewportHeight() const noexcept {
    const float height = ContentViewportHeight();
    if (height > 0.0f) {
        return height;
    }
    // 还没量出视口（尺寸为 0 的构造中间态）：退回本体高度，至少别算出负区间。
    return GetHeight() > 0 ? static_cast<float>(GetHeight()) : 0.0f;
}

float ListView::GetContentHeight() const noexcept {
    return static_cast<float>(_items.size()) * _style.rowHeight;
}

// ---------------------------------------------------------------------------
// 选择
// ---------------------------------------------------------------------------

bool ListView::IsSelected(int index) const {
    return std::find(_selection.begin(), _selection.end(), index)
           != _selection.end();
}

void ListView::pruneSelection() {
    const int count = static_cast<int>(_items.size());
    _selection.erase(std::remove_if(_selection.begin(), _selection.end(),
                                    [count](int index) {
                                        return index < 0 || index >= count;
                                    }),
                     _selection.end());
}

void ListView::applySelectionVisuals() {
    for (std::size_t i = 0; i < _rowNodes.size(); ++i) {
        if (_rowNodes[i] == nullptr) {
            continue;
        }
        const int globalIndex = _builtFirst + static_cast<int>(i);
        _rowNodes[i]->SetSelected(IsSelected(globalIndex));
    }
}

void ListView::SetSelected(int index, bool selected) {
    const int count = static_cast<int>(_items.size());
    if (index < 0 || index >= count) {
        return;
    }

    bool changed = false;
    if (selected) {
        if (_style.selection == ListSelectionMode::Single) {
            // 单选：把别的清掉（已经是它自己就什么都不做）。
            if (!(_selection.size() == 1 && _selection[0] == index)) {
                _selection.assign(1, index);
                changed = true;
            }
        } else if (!IsSelected(index)) {
            _selection.push_back(index);
            std::sort(_selection.begin(), _selection.end());
            changed = true;
        }
    } else if (IsSelected(index)) {
        _selection.erase(std::remove(_selection.begin(), _selection.end(), index),
                         _selection.end());
        changed = true;
    }

    if (!changed) {
        return;
    }
    applySelectionVisuals();
    MarkRepaintDirty();
    selectionChanged.Emit(ItemId(index), index);
    selectionSetChanged.Emit();
}

void ListView::ClearSelection() {
    if (_selection.empty()) {
        return;
    }
    _selection.clear();
    applySelectionVisuals();
    MarkRepaintDirty();
    selectionSetChanged.Emit();
}

void ListView::onRowActivated(int index) {
    const int count = static_cast<int>(_items.size());
    if (index < 0 || index >= count) {
        return;
    }
    if (!_items[static_cast<std::size_t>(index)].enabled) {
        return;  // 禁用行：点了等于没点
    }

    const std::string id = ItemId(index);
    // "一项被点了"与"选择变了"是两件事：`None` 模式下前者照样发。
    itemActivated.Emit(id, index);

    if (_style.selection == ListSelectionMode::None) {
        return;
    }
    if (_style.selection == ListSelectionMode::Single) {
        SetSelected(index, true);
    } else {
        SetSelected(index, !IsSelected(index));
    }
}

// ---------------------------------------------------------------------------
// 几何观测 / 滚动
// ---------------------------------------------------------------------------

ListRow* ListView::GetRowNode(int index) const {
    if (_builtFirst < 0 || index < _builtFirst || index > _builtLast) {
        return nullptr;
    }
    const std::size_t slot = static_cast<std::size_t>(index - _builtFirst);
    if (slot >= _rowNodes.size()) {
        return nullptr;
    }
    return _rowNodes[slot];
}

ShapeBounds ListView::GetRowRect(int index) const {
    const ListRow* row = GetRowNode(index);
    if (row == nullptr) {
        return ShapeBounds{};
    }
    return ShapeBounds{row->GetX(), row->GetY(),
                       static_cast<float>(row->GetWidth()),
                       static_cast<float>(row->GetHeight())};
}

void ListView::ScrollTo(float x, float y) {
    SetScroll(x, y);
    SyncVisibleWindow();
}

void ListView::ScrollToItem(int index) {
    ScrollTo(0.0f, static_cast<float>(index) * _style.rowHeight);
}

// ---------------------------------------------------------------------------
// 每帧与指针钩子：先让基类处理（滚动 / 拖条），再同步可见窗口
// ---------------------------------------------------------------------------

void ListView::onAnimationTick(float deltaSeconds) {
    InkingStaticPanel::onAnimationTick(deltaSeconds);
    // 兜底：滚动量可能从任何地方变（`SetScroll` / 拖条 / 滚轮 / 触摸），
    // 而窗口化的正确性依赖"建出来的行跟可见区间一致"。这里是 O(1) 的，且幂等。
    SyncVisibleWindow();
}

void ListView::onPointerWheel(float deltaX, float deltaY) {
    InkingStaticPanel::onPointerWheel(deltaX, deltaY);
    SyncVisibleWindow();
}

void ListView::onPointerPress(bool press) {
    InkingStaticPanel::onPointerPress(press);
    SyncVisibleWindow();
}

void ListView::onPointerDrag(float designX, float designY, float deltaX,
                             float deltaY) {
    InkingStaticPanel::onPointerDrag(designX, designY, deltaX, deltaY);
    SyncVisibleWindow();
}

}  // namespace inking::prefab
