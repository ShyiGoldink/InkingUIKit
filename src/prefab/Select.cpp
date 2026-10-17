#include <prefab/Select.h>

#include <detail/InkLog.h>
#include <prefab/PrefabTheme.h>
#include <scene/InkingScene.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <utility>

namespace inking::prefab {

namespace {

/// 选中项在展开列表里的前缀（这一版没有"选中态"这个外观词汇，用一个字符标出来）。
constexpr const char* kSelectedMark = "● ";

std::string ToLower(std::string text) {
    for (char& ch : text) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (value < 0x80u) {  // 只动 ASCII：UTF-8 的多字节部分不能被 toupper 拆坏
            ch = static_cast<char>(std::tolower(value));
        }
    }
    return text;
}

/// 子串匹配（大小写不敏感，只对 ASCII 做折叠——中文没有大小写）。
bool MatchesFilter(const std::string& text, const std::string& filter) {
    if (filter.empty()) {
        return true;
    }
    return ToLower(text).find(ToLower(filter)) != std::string::npos;
}

/**
 * Select 自己那一层面板的数据：尺寸 = 收起按钮的尺寸。
 *
 * `clipChildren` **必须关着**：展开的那一列挂在收起按钮外面（下面或上面），
 * 剪了就什么都看不见。这也是"不引入浮层"那条约定的代价，写在类注释里了。
 */
PanelData MakePanelData(const SelectStyle& style) {
    PanelData panel;
    panel.name = "select";
    panel.width = style.closedLook.width > 0 ? style.closedLook.width : 200;
    panel.height = style.closedLook.height > 0 ? style.closedLook.height : 36;
    // Select 自己在场景里的位置取自收起按钮那份配置（这样"把下拉放到某处"
    // 只需要配 `closedLook` 的偏移与锚点，不用再学一个新字段）。
    panel.traceAnchor = style.closedLook.traceAnchor;
    panel.selfAnchor = style.closedLook.selfAnchor;
    panel.offsetX = style.closedLook.offsetX;
    panel.offsetY = style.closedLook.offsetY;
    panel.zIndex = style.zIndex;  // Select 自己那一层（展开时会被临时提到 expandedZIndex）
    panel.closed.color = 0x02000000u;  // 极淡（不能全透明：Normalize 会兜底成黑）
    panel.open.color = panel.closed.color;
    panel.openInheritsClosed = false;
    panel.visible = true;
    return panel;
}

/// 展开那一块的纵向排布（相对收起按钮左上角，设计坐标）。
///
/// 向下展开：过滤框贴在按钮下面，选项栏再往下；向上展开：整块挂在按钮上方，
/// 顺序从下往上是"过滤框 → 选项栏"（贴近按钮的是过滤框，打字时手不用往上找）。
struct ExpandLayout {
    float filterOffsetY = 0.0f;
    float optionsOffsetY = 0.0f;
};

ExpandLayout ComputeExpandLayout(const SelectStyle& style) {
    ExpandLayout out;
    const float gap = style.expandGap;
    const float closedHeight =
        static_cast<float>(style.closedLook.height > 0 ? style.closedLook.height
                                                      : 36);
    const float filterHeight = style.filterable ? style.filterHeight : 0.0f;
    const float filterBlock = style.filterable ? filterHeight + gap : 0.0f;
    const float optionsHeight =
        style.expandHeight > 0.0f
            ? style.expandHeight
            : static_cast<float>(style.options.panel.height);

    if (style.direction == SelectExpandDirection::Down) {
        out.filterOffsetY = closedHeight + gap;
        out.optionsOffsetY = closedHeight + gap + filterBlock;
    } else {
        // 向上：选项栏的**底边**离按钮顶边 gap 远。
        out.filterOffsetY = -(filterHeight + gap);
        out.optionsOffsetY = -(optionsHeight + gap + filterBlock);
    }
    return out;
}

float FilterOffsetY(const SelectStyle& style) {
    return ComputeExpandLayout(style).filterOffsetY;
}

float OptionsOffsetY(const SelectStyle& style) {
    return ComputeExpandLayout(style).optionsOffsetY;
}

}  // namespace

ButtonData MakeDefaultClosedLook() {
    ButtonData look;
    look.name = "selectClosed";
    look.width = 200;
    look.height = 36;
    look.shape = ShapeSpec::RoundedRect(theme::kRadiusItem);
    // 亮色：白底 + 1px 浅描边；悬停 / 按下用主色浅底。
    look.normal = ButtonAppearance(AppearanceType::Color, theme::kSurface);
    look.hover = ButtonAppearance(AppearanceType::Color, theme::kAccentSoft);
    look.onclicked = ButtonAppearance(AppearanceType::Color,
                                      theme::kAccentSoftPressed);
    look.hoverInheritsNormal = false;
    look.onclickedInheritsNormal = false;
    look.stroke.width = 1.0f;
    look.stroke.color = theme::kOutline;
    look.text.fontSize = 16.0f;
    look.text.leftSpace = 12.0f;  // 文字靠左（右边还要留给 ▾ 指示符）
    look.text.alignV = TextAlignV::Middle;  // 垂直居中：按钮高 36、字号 16
    look.text.topSpace = 0.0f;
    look.text.path = theme::DefaultFontPath();
    look.textColor = theme::kTextPrimary;
    return look;
}

SelectStyle SelectStyle::MakeDefault() {
    SelectStyle style;
    style.closedLook = MakeDefaultClosedLook();
    style.options = OptionBarStyle::MakeDefault();
    // 展开容器默认跟收起按钮一样宽、高度用选项栏自己那份。
    style.options.panel.width = style.closedLook.width;
    style.direction = SelectExpandDirection::Down;
    style.expandWidth = 0.0f;
    style.expandHeight = 0.0f;
    style.expandGap = 2.0f;
    style.filterable = false;
    style.filterHeight = 30.0f;
    style.placeholder = "请选择";

    style.filterLook.name = "selectFilter";
    style.filterLook.width = style.closedLook.width - 2;
    style.filterLook.height = static_cast<int>(style.filterHeight + 0.5f);
    style.filterLook.style.fontSize = 15.0f;
    style.filterLook.style.padding = LayoutSpacing{8.0f, 4.0f, 8.0f, 4.0f};
    style.filterLook.style.fontPath = theme::DefaultFontPath();
    style.filterLook.color = theme::kTextPrimary;
    style.filterLook.caretColor = theme::kAccentStrong;
    return style;
}

Select::Select(InkingWidget* parent, const SelectStyle& style)
    : InkingStaticPanel(parent, MakePanelData(style)),
      _style(style),
      _closed(this, [&style] {
          // 收起按钮是 Select 的**子节点**：它自己的偏移与锚点清零（相对父级左上角），
          // 那份"放到场景哪里"的锚点/偏移已经被 Select 自己用掉了（见 MakePanelData）。
          ButtonData look = style.closedLook;
          look.traceAnchor = InkingChangeAnchor::LeftTop;
          look.selfAnchor = InkingChangeAnchor::LeftTop;
          look.offsetX = 0.0f;
          look.offsetY = 0.0f;
          return look;
      }()),
      _optionsBar(this, [&style] {
          OptionBarStyle options = style.options;
          if (style.expandWidth > 0.0f) {
              options.panel.width = static_cast<int>(style.expandWidth + 0.5f);
          } else {
              options.panel.width = style.closedLook.width;
          }
          if (style.expandHeight > 0.0f) {
              options.panel.height = static_cast<int>(style.expandHeight + 0.5f);
          }
          // 展开容器的位置：挂在收起按钮外面（下 / 上，带过滤框时再让开一格）。
          options.panel.traceAnchor = InkingChangeAnchor::LeftTop;
          options.panel.selfAnchor = InkingChangeAnchor::LeftTop;
          options.panel.offsetX = 0.0f;
          options.panel.offsetY = OptionsOffsetY(style);
          return options;
      }()),
      _filter(this, [&style] {
          TextInputData data = style.filterLook;
          if (data.width <= 0) {
              data.width = style.closedLook.width;
          }
          if (data.height <= 0) {
              data.height = static_cast<int>(style.filterHeight + 0.5f);
          }
          data.traceAnchor = InkingChangeAnchor::LeftTop;
          data.selfAnchor = InkingChangeAnchor::LeftTop;
          data.offsetX = 0.0f;
          data.offsetY = FilterOffsetY(style);
          return data;
      }()) {
    // ---- 收起按钮：点了展开 / 收起 ----
    _closed.SetText(_style.placeholder);
    _closed.clicked.Subscribe([this] { ToggleExpanded(); });

    // ---- 展开态：C1 选项栏，一开始藏着 ----
    _optionsBar.optionClicked.Subscribe(
        [this](const std::string& id, std::size_t visibleIndex) {
            onOptionChosen(id, visibleIndex);
        });
    _optionsBar.SetVisible(false);

    // ---- 过滤框：一开始藏着（`filterable` 为假时永远藏着）----
    _filter.SetVisible(false);

    // 展开态的位置在初始化列表里就算好了（静态档的几何构造即定型，没有运行期
    // 写入口）：选项栏与过滤框各自让开对方那一格，见 `ComputeExpandLayout`。
    applyFilter();
    refreshClosedText();
}

// ---------------------------------------------------------------------------
// 选项
// ---------------------------------------------------------------------------

void Select::SetOptions(std::vector<OptionItem> options) {
    if (_options == options) {
        return;  // 幂等
    }
    _options = std::move(options);
    if (_selectedIndex >= static_cast<int>(_options.size())) {
        _selectedIndex = -1;
    }
    applyFilter();
    refreshClosedText();
}

void Select::applyFilter() {
    _visibleToRaw.clear();

    std::vector<OptionItem> visible;
    for (std::size_t i = 0; i < _options.size(); ++i) {
        const OptionItem& item = _options[i];
        const std::string text = item.text.empty() ? item.id : item.text;
        if (!MatchesFilter(text, _filterText)) {
            continue;
        }
        OptionItem shown = item;
        if (static_cast<int>(i) == _selectedIndex) {
            shown.text = std::string(kSelectedMark) + text;  // 标出当前值
        }
        visible.push_back(std::move(shown));
        _visibleToRaw.push_back(static_cast<int>(i));
    }

    _optionsBar.SetItems(std::move(visible));
}

void Select::refreshClosedText() {
    std::string text = _style.placeholder;
    if (_selectedIndex >= 0
        && _selectedIndex < static_cast<int>(_options.size())) {
        const OptionItem& item = _options[static_cast<std::size_t>(_selectedIndex)];
        text = item.text.empty() ? item.id : item.text;
    }
    // 展开指示：这一版就是两个字符（没有图标列）。
    _closed.SetText(text + "  ▾");
}

// ---------------------------------------------------------------------------
// 选择
// ---------------------------------------------------------------------------

void Select::SetSelected(int index) {
    if (index < -1 || index >= static_cast<int>(_options.size())) {
        return;
    }
    if (index == _selectedIndex) {
        return;  // 幂等
    }
    _selectedIndex = index;
    refreshClosedText();
    applyFilter();  // 展开列表里的 "●" 标记跟着走
    if (index >= 0) {
        selectionChanged.Emit(GetSelectedId(), index);
    }
}

std::string Select::GetSelectedId() const {
    if (_selectedIndex < 0
        || _selectedIndex >= static_cast<int>(_options.size())) {
        return {};
    }
    const OptionItem& item = _options[static_cast<std::size_t>(_selectedIndex)];
    return item.id.empty() ? std::to_string(_selectedIndex) : item.id;
}

std::string Select::GetSelectedText() const {
    if (_selectedIndex < 0
        || _selectedIndex >= static_cast<int>(_options.size())) {
        return {};
    }
    const OptionItem& item = _options[static_cast<std::size_t>(_selectedIndex)];
    return item.text.empty() ? item.id : item.text;
}

void Select::onOptionChosen(const std::string& /*id*/, std::size_t visibleIndex) {
    const int raw = RawIndexForVisible(static_cast<int>(visibleIndex));
    if (raw < 0) {
        return;
    }
    const int previous = _selectedIndex;
    _selectedIndex = raw;
    refreshClosedText();
    applyFilter();  // "●" 标记
    if (raw != previous) {
        selectionChanged.Emit(GetSelectedId(), raw);
    }
    // 选完就收起来（下拉的常见行为）。想"选完还开着"的用法请自己再 SetExpanded(true)。
    SetExpanded(false);
}

int Select::RawIndexForVisible(int visibleIndex) const noexcept {
    if (visibleIndex < 0
        || visibleIndex >= static_cast<int>(_visibleToRaw.size())) {
        return -1;
    }
    return _visibleToRaw[static_cast<std::size_t>(visibleIndex)];
}

bool Select::ActivateVisible(int visibleIndex) {
    return _optionsBar.Activate(static_cast<std::size_t>(
        visibleIndex < 0 ? 0 : visibleIndex));
}

bool Select::ActivateOption(int index) {
    for (std::size_t i = 0; i < _visibleToRaw.size(); ++i) {
        if (_visibleToRaw[i] == index) {
            return _optionsBar.Activate(i);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 展开 / 收起
// ---------------------------------------------------------------------------

void Select::SetExpanded(bool expanded) {
    if (_expanded == expanded) {
        return;  // 幂等
    }
    _expanded = expanded;

    // 展开 = **整块**提到最上层。注意命中的排序看的是**容器自己**的 z
    // （`OwnsSubtreeHit` 之下子孙不进命中表，表里只有 Select 这一条），
    // 所以"提层"必须从 Select 自己开始，再让内部件依次 +1：
    //   Select(200) → 收起按钮 / 过滤框 / 选项栏(201) → 选项(202)
    // 只提内部那一栏是**不够的**——表里那条还是 0，同 z 下后构造的邻居就赢了
    // （`AGENTS.md` §6 第 104 条）。
    const bool expandedNow = expanded;
    const int base = expandedNow ? _style.expandedZIndex : _style.zIndex;
    ChangeZIndex(base);
    _closed.ChangeZIndex(base + 1);
    _filter.ChangeZIndex(expandedNow ? base + 1 : _style.filterLook.zIndex);
    _optionsBar.SetLayerZ(expandedNow ? base + 1 : _style.options.panel.zIndex);

    // 只改**可见性**：`SetVisible` 是骨架变化（下标脏 + 通知场景重排），
    // 所以展开的那一列立刻能点到、收起之后立刻点不到（§6 第 18 条）。
    _optionsBar.SetVisible(expandedNow);
    if (_style.filterable) {
        _filter.SetVisible(expanded);
        if (expanded) {
            // 展开就打字：焦点交给过滤框（焦点是"点击获得"的，这里显式接管）。
            if (InkingScene* scene = GetScene()) {
                scene->SetKeyboardFocus(&_filter);
            }
            _lastPolledFilterText = _filter.GetEditText();
        } else if (InkingScene* scene = GetScene()) {
            if (scene->GetKeyboardFocus() == &_filter) {
                scene->ClearKeyboardFocus();
            }
        }
    }

    expandedChanged.Emit(expanded);
}

std::size_t Select::GetOptionNodeCount() const noexcept {
    return _optionsBar.GetBuiltItemCount();
}

std::size_t Select::GetVisibleOptionNodeCount() const noexcept {
    return _expanded ? _optionsBar.GetBuiltItemCount() : 0u;
}

// ---------------------------------------------------------------------------
// 过滤
// ---------------------------------------------------------------------------

void Select::SetFilterText(const std::string& text) {
    if (_filterText == text) {
        return;  // 幂等（轮询那条路每秒会问很多次）
    }
    _filterText = text;
    if (_style.filterable && _filter.GetEditText() != text) {
        _filter.SetEditText(text);
    }
    _lastPolledFilterText = text;
    applyFilter();
}

void Select::onAnimationTick(float deltaSeconds) {
    InkingStaticPanel::onAnimationTick(deltaSeconds);

    if (!_style.filterable || !_expanded) {
        return;
    }
    // 轮询过滤框：`InkingTextInput` 没有"文本变了"的事件槽（只有 `submitted`），
    // 而为一个可选特性去改那个组件不值当。比较是 O(1)，且只在展开时做。
    const std::string now = _filter.GetEditText();
    if (now != _lastPolledFilterText) {
        SetFilterText(now);
    }
}

}  // namespace inking::prefab
