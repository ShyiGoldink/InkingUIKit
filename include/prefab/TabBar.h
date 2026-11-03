#pragma once

// 预制件「标签页」（`docs/zh/plans/2026-11-03-uikit-completeness-tasks.md` T-08）。
//
// **它就是**：一条标签 + 一块内容区，同一时刻只显示一页。
//
// ---------------------------------------------------------------------------
// 切页用 `SetVisible`，不用挪坐标 / 改 alpha
//
// 可见性是**骨架变化**（`InkingWidget::SetVisible` → `NotifySceneStructureChanged`），
// 命中表跟着重烘，所以"切走的那页点不到"是白拿的。反过来把页藏成 alpha=0 会出现
// "看不见但点得到"——那种 bug 排起来最费劲（`AGENTS.md` §6 第 18 条）。
//
// ---------------------------------------------------------------------------
// 为什么**不复用 `OptionBar`** 做标签条（与任务书 §7 T-08 初稿不同）
//
// `OptionBar` 的项宽是**构造期冻死**的：`itemLook.width` 存在它自己那份 style 里，
// 而 `SetItems` 只换数据、不回读 style；且 `itemLook.width <= 0` 时它把每项撑成
// **容器整宽**（那是竖向菜单的语义）。横向标签条要求"每项 = 容器宽 / 项数"，
// 项数又在运行期变（`AddPage`）——这条路要么让调用方预先给死宽度，要么每加一页
// 重建整条标签。两条都比直接摆按钮差。
//
// 所以标签条由本类**直接摆 `InkingStaticButton`**（仍是"用现成配件组装"，没有新的
// 底层）。代价写在下面"第一版不做"里：标签宽度是**固定值**（`tabLength`），
// 不做"按项数均分"。
//
// ---------------------------------------------------------------------------
// 第一版**不做**
//
//   - 标签滚动（标签多到超出条宽就是溢出）——任务书 §7 的风险段已写明；
//   - 自动均分标签宽（见上）；
//   - 侧边标签条的**右侧**（只做顶 / 底 / 左三档，够覆盖"页签在左"的常见布局）；
//   - `SetPainter`（同其余新控件）。

#include <panel/InkingStaticPanel.h>
#include <event/InkingEvent.h>
#include <button/ButtonData.h>
#include <dataStruct/InkingAnchorData.h>
#include <prefab/PrefabTheme.h>

#include <memory>
#include <string>
#include <vector>

namespace inking_prefab {

/// 标签条贴在哪一边。
enum class TabBarSide : std::uint8_t {
    Top = 0,  ///< 标签在顶上，内容在下面
    Bottom,   ///< 标签在底下，内容在上面
    Left,     ///< 标签竖排在左边，内容在右边
};

/// 标签页的外观与尺寸。
///
/// 定位 / 尺寸 / 层级复用 `InkingAnchorData`（与 `SliderData` / `ToggleData` 同一
/// 口径）：`width` / `height` / 锚点 / 偏移都从那一份来，于是"把标签页摆在容器里
/// 哪个位置"用的是框架同一套规则，不另造字段。
struct TabBarData : public InkingAnchorData {
    /// 默认框 480×320（`width` / `height` 来自 `InkingAnchorData`）。
    /// 外观那一份是黑的，用 `MakeDefault()` 才拿到主题色。
    TabBarData() noexcept;

    std::string name = "tabBar";

    TabBarSide side = TabBarSide::Top;

    /// 标签条**厚度**（顶 / 底 = 高，左 = 宽）。
    float stripThickness = 36.0f;
    /// 单个标签**沿条方向的长度**（顶 / 底 = 宽，左 = 高）。固定值，不参与均分。
    float tabLength = 96.0f;
    /// 标签条与内容区之间的间距。
    float gap = 0.0f;

    /// 标签按钮的外观（三态照旧）。默认：浅底、悬停浅紫、无描边。
    InkingButtonData tab{};
    /// 选中那一页的**文字色**（另有下划线由控件自己画，见 `onRenderDecoration`）。
    std::uint32_t selectedTextColor = theme::kAccentStrong;
    /// 未选中那一页的文字色。
    std::uint32_t textColor = theme::kTextPrimary;

    /// 内容区的底色。
    InkingButtonAppearance content{};
    /// 整个控件（含标签条那一条）的底色。
    InkingButtonAppearance background{};
    /// 下划线（选中指示）的颜色与高度。
    std::uint32_t indicatorColor = theme::kAccent;
    float indicatorHeight = 3.0f;

    static TabBarData MakeDefault();
};

/**
 * @brief 标签页控件。
 *
 * ```cpp
 * inking_prefab::TabBar tabs(&panel);
 * tabs.AddPage("常规", std::make_unique<SettingsGeneral>(nullptr));
 * tabs.AddPage("音量", std::make_unique<SettingsAudio>(nullptr));
 * tabs.pageChanged.Subscribe([](int now, int before) { ... });
 * ```
 *
 * **页内容的定位**：页是内容区的子节点，`(0,0)` = **内容区左上角**。
 * 页自己怎么摆由它自己的锚点 / 偏移决定，本类不重排（静态组件没有几何写入口，
 * 而"内容区左上角"这个原点就是容器能给的全部约定）。内容区尺寸用
 * `GetContentWidth()/GetContentHeight()` 读，页要铺满就照这两个数配。
 */
class TabBar : public InkingStaticPanel {
public:
    /// 换页了。参数是 `(现在的页索引, 之前的页索引)`；切到当前页**不发**。
    InkingEvent<int, int> pageChanged;

    explicit TabBar(InkingWidget* parent,
                    const TabBarData& data = TabBarData::MakeDefault());

    /**
     * @brief 加一页（标签文字 + 内容节点，内容由本类拥有）。
     *
     * @return 新页的索引（从 0 起）。第一页会自动成为当前页。
     *
     * 标签按钮在**这一刻**按 `tabLength` 摆好；之后再改 `TabBarData` 不会回头
     * 重排（静态组件没有几何写入口），所以搭建期一次配好。
     */
    int AddPage(const std::string& title, std::unique_ptr<InkingWidget> page);

    /**
     * @brief 切到第 `index` 页。
     *
     * 顺序：**先藏旧、再显新**（反过来的话会有一帧两页同显），然后更新标签文字色
     * 与下划线，最后发 `pageChanged`。
     *
     * @return 真的换了才 true；索引越界 / 就是当前页都返回 false（且不发事件）。
     */
    bool Select(int index);

    int GetPageCount() const noexcept {
        return static_cast<int>(_pages.size());
    }

    /// 当前页索引（一页都没有时是 −1）。
    int GetVisiblePageIndex() const noexcept { return _visible; }

    /// 第 `index` 页的内容节点（越界 = nullptr）。它的定位原点是内容区左上角。
    InkingWidget* GetPage(int index) const noexcept;

    /// 内容区的节点（要往里再挂浮动件时用）。
    InkingStaticPanel* GetContentNode() const noexcept { return _content; }

    int GetContentWidth() const noexcept;
    int GetContentHeight() const noexcept;

    const TabBarData& GetData() const noexcept { return _data; }

protected:
    /// 选中指示（下划线）画在**子孙之上**——画在填充那一趟会被标签按钮盖住。
    void onRenderDecoration(float pixelX, float pixelY) const override;

    /// 有下划线要画（基类只在"有描边"时为真，这里恒为真）。
    bool HasRenderDecoration() const noexcept override { return true; }

private:
    /**
     * @brief 切页的实际实现。`emitEvent == false` 用于**第一次把首页显出来**。
     *
     * 为什么首页那次不发事件：那是"初始状态"，不是"用户换了页"。订阅者通常在
     * `AddPage` 之后才关心"换页"，构造期的自动选中发一条 `(0, -1)` 只会让人
     * 在回调里多写一个 `if (before < 0) return;`。
     */
    bool selectTo(int index, bool emitEvent);

    /// 把"选中 / 未选中"的文字色刷到每个标签上。
    void applySelection();

    /// 第 `index` 个标签的矩形（相对本控件左上角）。
    InkingShapeBounds tabRect(int index) const;

    TabBarData _data{};

    /// 内容区（由本节点拥有，`AddChild` 在构造函数体里挂上）。
    InkingStaticPanel* _content = nullptr;

    /// 标签按钮（由本节点拥有）。顺序 = 页顺序。
    std::vector<InkingWidget*> _tabs{};
    /// 页的内容节点（由内容区拥有）。
    std::vector<InkingWidget*> _pages{};
    int _visible = -1;
};

}  // namespace inking_prefab
