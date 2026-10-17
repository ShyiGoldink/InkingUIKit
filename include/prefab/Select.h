#pragma once

// 预制件 C4「下拉 / 选择器」（`docs/TaskGuide.md` 的 C 系列）。
//
// **它就是**：收起态一个按钮（当前值 + 占位提示 + 展开指示），展开态**就地
// 显示一个 C1 选项栏**——不引入浮层、不引入输入消费（任务里定死的那条）。
//
// 所以本类里没有一处新的绘制 / 命中代码：三个成员就是三个现成组件
// （收起按钮 / C1 选项栏 / 过滤输入框），位置在构造期算好，展开收起只改
// **可见性**——`SetVisible` 是骨架变化，命中表跟着重烘，所以"展开的那一列
// 点得到、收起之后点不到"是白拿的（`AGENTS.md` §6 第 18 条：可见性不能靠
// 从绘制列表里删掉来实现）。
//
// ---------------------------------------------------------------------------
// 三条边界（写在类注释里，别让人猜）
//
//   1. **点别处收起由调用方约定**：本类只提供 `SetExpanded(false)`。真做
//      "点外部自动关"要浮层 / 全局输入那套词汇，第一版不做；
//   2. **展开容器的溢出不被剪**：它挂在收起按钮**外面**（下面或上面），而
//      Select 自己不能 `clipChildren`（否则展开的那一列会被自己剪掉）。
//      容器里的东西盖不住容器外的兄弟，那是局部层级（A8b，暂缓）；
//   3. **第一版不做**：浮出式展开、多选、可直接输入（除过滤框外）、首字母定位。
//
// 过滤（`filterable`）的实现是"每渲染帧比一次文本"：`InkingTextInput` 没有
// "文本变了"的事件槽（它只有 `submitted`），而为一个可选特性去改那个组件不值当。
// 轮询是 O(1) 的字符串比较，且只在展开时做。

#include <button/InkingStaticButton.h>
#include <widget/InkingStaticWidget.h>
#include <event/InkingEvent.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/OptionBar.h>
#include <prefab/SelectData.h>
#include <text/InkingTextInput.h>

#include <cstddef>
#include <string>
#include <vector>

namespace inking {
namespace prefab {

/**
 * @brief C4：预制下拉 / 选择器。
 *
 * ```cpp
 * inking::prefab::Select picker(&scene);
 * picker.SetOptions({{"tianxin", "天心"}, {"luolisi", "洛里丝"}});
 * picker.selectionChanged.Subscribe([](const std::string& id, int index) { ... });
 * picker.SetExpanded(true);   // 展开（就地显示选项栏）
 * ```
 */
class Select : public InkingStaticPanel {
public:
    /// 选了一个选项。参数是 `(项的标识, **原始**列表里的索引)`。
    InkingEvent<const std::string&, int> selectionChanged;

    /// 展开 / 收起变了（`true` = 现在展开了）。
    InkingEvent<bool> expandedChanged;

    explicit Select(InkingWidget* parent,
                    const SelectStyle& style = SelectStyle::MakeDefault());

    // ---------------- 选项 ----------------

    /// 换一整份选项（会重建展开态的选项栏；幂等）。
    void SetOptions(std::vector<OptionItem> options);

    const std::vector<OptionItem>& GetOptions() const noexcept { return _options; }

    /// 选项总数（**不受过滤影响**）。
    std::size_t GetOptionCount() const noexcept { return _options.size(); }

    // ---------------- 选择 ----------------

    /// 设当前选中项（**原始索引**；传 −1 清空）。会发 `selectionChanged`。
    void SetSelected(int index);

    int GetSelectedIndex() const noexcept { return _selectedIndex; }

    /// 当前选中项的标识（没选 = 空串）。
    std::string GetSelectedId() const;

    /// 当前选中项的文字（没选 = 空串）。
    std::string GetSelectedText() const;

    // ---------------- 展开 / 收起 ----------------

    /// 展开或收起（就地显示 / 隐藏选项栏；幂等）。
    void SetExpanded(bool expanded);

    bool IsExpanded() const noexcept { return _expanded; }

    void ToggleExpanded() { SetExpanded(!_expanded); }

    /// 展开态建出来几个选项节点（**收起时也照样是这么多**——节点没被删，只是不可见）。
    std::size_t GetOptionNodeCount() const noexcept;

    /// 现在**看得见**几个选项节点（收起 = 0）。展开 / 收起那条验收判据用它。
    std::size_t GetVisibleOptionNodeCount() const noexcept;

    // ---------------- 过滤 ----------------

    /// 设过滤串（空 = 不过滤）。会重建展开态的选项（幂等）。
    void SetFilterText(const std::string& text);

    const std::string& GetFilterText() const noexcept { return _filterText; }

    /// 过滤之后还剩几个选项。
    std::size_t GetVisibleOptionCount() const noexcept {
        return _visibleToRaw.size();
    }

    /// 过滤之后第 `visibleIndex` 项对应**原始**列表里的第几项（越界 = −1）。
    int RawIndexForVisible(int visibleIndex) const noexcept;

    /// 直接激活过滤之后第 `visibleIndex` 项（跳过指针链路；自检用）。
    bool ActivateVisible(int visibleIndex);

    /// 直接激活原始列表里第 `index` 项（它得当前可见，否则返回 false）。
    bool ActivateOption(int index);

    // ---------------- 内部件（想直接配就用它们） ----------------

    const SelectStyle& GetStyle() const noexcept { return _style; }

    /// 展开态那个 C1 选项栏。
    OptionBar& Options() noexcept { return _optionsBar; }
    const OptionBar& Options() const noexcept { return _optionsBar; }

    /// 收起态那个按钮。
    InkingStaticButton& ClosedButton() noexcept { return _closed; }

    /// 过滤输入框（`filterable` 为假时它一直不可见）。
    InkingTextInput& FilterInput() noexcept { return _filter; }

protected:
    void onAnimationTick(float deltaSeconds) override;

private:
    /// 按 `_filterText` 重算可见选项，并让选项栏跟着重建（幂等）。
    void applyFilter();

    /// 按当前选择刷新收起按钮上的文字（值 + 展开指示）。
    void refreshClosedText();

    /// 选项被点了（来自选项栏的 `optionClicked`）的统一出口。
    void onOptionChosen(const std::string& id, std::size_t visibleIndex);

    SelectStyle _style{};

    /// 原始选项列表。
    std::vector<OptionItem> _options{};

    /// 过滤之后的可见项 → 原始索引。
    std::vector<int> _visibleToRaw{};

    std::string _filterText{};
    std::string _lastPolledFilterText{};
    int _selectedIndex = -1;
    bool _expanded = false;

    InkingStaticButton _closed;
    OptionBar _optionsBar;
    InkingTextInput _filter;
};

}  // namespace prefab
}  // namespace inking
