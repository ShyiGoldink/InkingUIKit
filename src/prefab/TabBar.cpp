#include <prefab/TabBar.h>

#include <button/InkingStaticButton.h>
#include <detail/InkLog.h>
#include <panel/PanelData.h>
#include <render/InkingDraw.h>
#include <scene/InkingScene.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace inking_prefab {

namespace {

constexpr const char* kModuleName = "TabBar";

/// 标签条与内容区两块矩形（都相对本控件左上角）。
struct TabRects {
    InkingShapeBounds strip{};
    InkingShapeBounds content{};
};

TabRects ComputeRects(const TabBarData& data) {
    const float w = static_cast<float>(std::max(1, data.width));
    const float h = static_cast<float>(std::max(1, data.height));
    const float along = data.side == TabBarSide::Left ? w : h;
    // 标签条厚度不能把整块吃掉：至少给内容区留 1 像素（退化成一条线也比"内容区
    // 是负高度"好——负尺寸在形状层会变成一堆退化顶点）。
    float thickness = data.stripThickness > 0.0f ? data.stripThickness : 1.0f;
    if (thickness > along - 1.0f) {
        thickness = std::max(1.0f, along - 1.0f);
    }
    const float gap = std::max(0.0f, data.gap);

    TabRects rects;
    switch (data.side) {
        case TabBarSide::Bottom:
            rects.strip = InkingShapeBounds{0.0f, h - thickness, w, thickness};
            rects.content =
                InkingShapeBounds{0.0f, 0.0f, w, h - thickness - gap};
            break;
        case TabBarSide::Left:
            rects.strip = InkingShapeBounds{0.0f, 0.0f, thickness, h};
            rects.content =
                InkingShapeBounds{thickness + gap, 0.0f, w - thickness - gap, h};
            break;
        case TabBarSide::Top:
        default:
            rects.strip = InkingShapeBounds{0.0f, 0.0f, w, thickness};
            rects.content =
                InkingShapeBounds{0.0f, thickness + gap, w, h - thickness - gap};
            break;
    }
    return rects;
}

InkingPanelData MakePanelData(const TabBarData& data, const TabRects& rects,
                              const InkingButtonAppearance& look,
                              const char* suffix) {
    InkingPanelData panel;
    panel.name = (data.name.empty() ? "tabBar" : data.name) + suffix;
    panel.width = std::max(1, static_cast<int>(std::lround(rects.content.width)));
    panel.height = std::max(1, static_cast<int>(std::lround(rects.content.height)));
    panel.shape = InkingShapeSpec::Rect();
    panel.closed.color = look.color;
    panel.open.color = look.color;
    // **token 要一起传**：只搬 `color` 的话，配置里写的 `themeColor` 到这儿就断了，
    // 症状是"运行期换主题，别的都变了、标签页还是浅色"（而且不报错）。
    panel.closed.themeColor = look.themeColor;
    panel.open.themeColor = look.themeColor;
    panel.openInheritsClosed = false;
    return panel;
}

}  // namespace

TabBarData::TabBarData() noexcept {
    // 默认值集中在这里（成员默认初始化器只写"语义上的 0"，尺寸要有值才画得出来）。
    width = 480;
    height = 320;
}

TabBarData TabBarData::MakeDefault() {
    TabBarData data;
    // 标签条与内容区**同一档浅底**，靠下划线指出当前页（不给整条条另一个色，
    // 否则两页之间会多出"两种白"的观感差）。
    data.background = InkingButtonAppearance(InkingAppearanceType::Color, theme::kSurface);
    data.content = InkingButtonAppearance(InkingAppearanceType::Color, theme::kSurface);

    // 标签按钮：浅底、悬停浅紫、按下更实一点（与 OptionBar 的项同一个语言）。
    data.tab.name = "tabBarItem";
    data.tab.shape = InkingShapeSpec::Rect();
    data.tab.normal = InkingButtonAppearance(InkingAppearanceType::Color, theme::kSurface);
    data.tab.hover = InkingButtonAppearance(InkingAppearanceType::Color, theme::kAccentSoft);
    data.tab.onclicked =
        InkingButtonAppearance(InkingAppearanceType::Color, theme::kAccentSoftPressed);
    // 必须显式关掉继承：`Normalize()` 的规矩是"继承为真就拿 normal 覆盖"
    // （`AGENTS.md` §6 第 43 条），不关的话上面两份白写。
    data.tab.hoverInheritsNormal = false;
    data.tab.onclickedInheritsNormal = false;
    data.tab.text.fontSize = 15.0f;
    data.tab.text.align = InkingTextAlignH::Center;
    data.tab.text.alignV = InkingTextAlignV::Middle;
    data.tab.text.path = theme::DefaultFontPath();
    return data;
}

TabBar::TabBar(InkingWidget* parent, const TabBarData& data)
    : InkingStaticPanel(
          parent, [&data] {
              // 本控件自己的底（含标签条那一条）。几何 = 整个框，一块到底。
              TabRects rects;
              rects.content = InkingShapeBounds{
                  0.0f, 0.0f, static_cast<float>(std::max(1, data.width)),
                  static_cast<float>(std::max(1, data.height))};
              InkingPanelData panel =
                  MakePanelData(data, rects, data.background, "");
              // 定位从 `InkingAnchorData` 那一份搬过来（与 Slider / Toggle 同一口径：
              // 锚点 / 偏移用框架同一套规则，不另造字段）。
              panel.selfAnchor = data.selfAnchor;
              panel.traceAnchor = data.traceAnchor;
              panel.offsetX = data.offsetX;
              panel.offsetY = data.offsetY;
              return panel;
          }()),
      _data(data) {
    const TabRects rects = ComputeRects(data);
    auto content = std::make_unique<InkingStaticPanel>(
        nullptr, [&data, &rects] {
            InkingPanelData panel =
                MakePanelData(data, rects, data.content, ".content");
            // 内容区贴在"整框减标签条"的那块上：左上空 + 偏移。
            panel.traceAnchor = InkingChangeAnchor::LeftTop;
            panel.selfAnchor = InkingChangeAnchor::LeftTop;
            panel.offsetX = rects.content.x;
            panel.offsetY = rects.content.y;
            // 页比内容区大时别画到标签条上（裁剪是容器的一层属性，子孙一个字不用改）。
            panel.clipChildren = true;
            return panel;
        }());
    _content = static_cast<InkingStaticPanel*>(AddChild(std::move(content)));
    if (_content == nullptr) {
        INK_LOG_ERROR(std::string(kModuleName),
                      "内容区没能挂上（AddChild 返回空）：" + _data.name);
    }
}

InkingShapeBounds TabBar::tabRect(int index) const {
    const TabRects rects = ComputeRects(_data);
    const float length = _data.tabLength > 0.0f ? _data.tabLength : 1.0f;
    const float step = static_cast<float>(index) * length;
    if (_data.side == TabBarSide::Left) {
        return InkingShapeBounds{rects.strip.x, rects.strip.y + step,
                                 rects.strip.width, length};
    }
    return InkingShapeBounds{rects.strip.x + step, rects.strip.y, length,
                             rects.strip.height};
}

int TabBar::AddPage(const std::string& title, std::unique_ptr<InkingWidget> page) {
    if (_content == nullptr || page == nullptr) {
        return -1;
    }
    const int index = static_cast<int>(_pages.size());

    // ① 标签按钮：几何在**这一刻**定死（静态组件没有几何写入口）。
    InkingButtonData look = _data.tab;
    look.name = (_data.name.empty() ? "tabBar" : _data.name) + ".tab" +
                std::to_string(index);
    const InkingShapeBounds rect = tabRect(index);
    look.width = std::max(1, static_cast<int>(std::lround(rect.width)));
    look.height = std::max(1, static_cast<int>(std::lround(rect.height)));
    look.traceAnchor = InkingChangeAnchor::LeftTop;
    look.selfAnchor = InkingChangeAnchor::LeftTop;
    look.offsetX = rect.x;
    look.offsetY = rect.y;
    look.label = title;
    auto tab = std::make_unique<InkingStaticButton>(this, look);
    auto* tabNode = static_cast<InkingStaticButton*>(AddChild(std::move(tab)));
    if (tabNode != nullptr) {
        // 捕获索引而不是指针：标签节点可能被重建，索引总是对应当前那一页。
        tabNode->clicked.Subscribe([this, index] { Select(index); });
    }
    _tabs.push_back(tabNode);

    // ② 内容：挂进内容区，先藏起来（第一页由下面的 `Select(0)` 显出来）。
    auto* pageNode = _content->AddChild(std::move(page));
    if (pageNode != nullptr) {
        pageNode->SetVisible(false);
    }
    _pages.push_back(pageNode);

    // ③ 第一页成为当前页；其余只刷新一下文字色（新标签默认是"未选中"色）。
    //    **首页那次不发事件**：那是初始状态，不是"用户换了页"（见头文件）。
    if (_visible < 0) {
        selectTo(0, false);
    } else {
        applySelection();
    }
    MarkRepaintDirty();
    return index;
}

bool TabBar::Select(int index) { return selectTo(index, true); }

bool TabBar::selectTo(int index, bool emitEvent) {
    if (index < 0 || index >= static_cast<int>(_pages.size())) {
        return false;  // 越界：不报错（调用点常在循环里），但不能装作切了
    }
    if (index == _visible) {
        return false;  // 就是当前页：不发事件、一个脏都不标
    }
    const int before = _visible;

    // **先藏旧、再显新**：反过来的话中间那一帧两页同显（任务书 §7 T-08 第 3 条）。
    if (before >= 0 && before < static_cast<int>(_pages.size())) {
        if (InkingWidget* oldPage = _pages[static_cast<std::size_t>(before)]) {
            oldPage->SetVisible(false);
        }
    }
    if (InkingWidget* newPage = _pages[static_cast<std::size_t>(index)]) {
        newPage->SetVisible(true);
    }

    _visible = index;
    applySelection();
    MarkRepaintDirty();  // 下划线跟着走
    if (emitEvent) {
        pageChanged.Emit(index, before);
    }
    return true;
}

void TabBar::applySelection() {
    for (int i = 0; i < static_cast<int>(_tabs.size()); ++i) {
        auto* button = static_cast<InkingStaticButton*>(_tabs[static_cast<std::size_t>(i)]);
        if (button == nullptr) {
            continue;
        }
        // 静态按钮没有"选中"这个第四态，所以选中只落到**文字色**上；
        // 更显眼的那条指示由 `onRenderDecoration` 的下划线给（那是本控件画的）。
        button->SetTextColor(i == _visible ? _data.selectedTextColor : _data.textColor);
    }
}

InkingWidget* TabBar::GetPage(int index) const noexcept {
    if (index < 0 || index >= static_cast<int>(_pages.size())) {
        return nullptr;
    }
    return _pages[static_cast<std::size_t>(index)];
}

int TabBar::GetContentWidth() const noexcept {
    return _content != nullptr ? _content->GetWidth() : 0;
}

int TabBar::GetContentHeight() const noexcept {
    return _content != nullptr ? _content->GetHeight() : 0;
}

void TabBar::onRenderDecoration(float pixelX, float pixelY) const {
    // 基类那一笔（描边）照旧——它本来也画在子孙之上，同一个时机。
    InkingStaticPanel::onRenderDecoration(pixelX, pixelY);

    if (_visible < 0 || _visible >= static_cast<int>(_tabs.size())) {
        return;
    }
    SDL_Renderer* renderer = inking_detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }
    const float magnification =
        GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
    const float height = std::max(1.0f, _data.indicatorHeight);
    const InkingShapeBounds tab = tabRect(_visible);

    // 指示条贴在那条标签**朝向内容区**的那一边：顶标签贴下缘、底标签贴上缘、
    // 左标签贴右缘。
    InkingShapeBounds bar{};
    switch (_data.side) {
        case TabBarSide::Bottom:
            bar = InkingShapeBounds{tab.x, tab.y, tab.width, height};
            break;
        case TabBarSide::Left:
            bar = InkingShapeBounds{tab.x + tab.width - height, tab.y, height,
                                    tab.height};
            break;
        case TabBarSide::Top:
        default:
            bar = InkingShapeBounds{tab.x, tab.y + tab.height - height, tab.width,
                                    height};
            break;
    }
    if (!(bar.width > 0.0f) || !(bar.height > 0.0f)) {
        return;
    }
    inking_detail::fillShape(renderer, InkingShapeSpec::Rect(),
                             std::max(1, static_cast<int>(std::lround(bar.width))),
                             std::max(1, static_cast<int>(std::lround(bar.height))),
                             magnification, pixelX + bar.x * magnification,
                             pixelY + bar.y * magnification, InkingTransformSpec{},
                             _data.indicatorColor);
}

}  // namespace inking_prefab
