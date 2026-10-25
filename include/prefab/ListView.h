#pragma once

// 预制件 C3「列表」（`docs/TaskGuide.md` 的 C 系列）。
//
// **它就是**：`ScrollView`（B12 的 `scroll` 段）+ 布局 + 动态项组装。
//
// ---------------------------------------------------------------------------
// "只建可见项"是怎么做的（B9 后移之后由这里接管）
//
// 框架**不动**：`ListView` 按当前滚动量算出可见区间 `[first, last]`，只把这几行
// 建成子节点，再在两头各放一个**撑位置的占位块**（`InkingStaticWidget`）——
// 前面那个把第一行顶到 `first × 行高`，后面那个把内容总高补到 `项数 × 行高`。
// 于是：
//   - 滚动范围仍然是"全部项"（后面那块占位撑出来的）；
//   - 可见的行落在**正确的位置**上（前面那块占位顶出来的）；
//   - 节点数只跟"看得见几行"有关，跟总项数无关。
//
// 判据是硬的（`AGENTS.md` §4 第 4 条）：1000 项时 `GetBuiltRowCount()` 仍然只有
// 十几二十个，`GetItemCount()` 才是 1000。
//
// 占位块承担两个"框架没给写入口"的地方：静态档改不了自己的几何，而布局规则
// （`LayoutItem` / `LayoutSpec`）也是构造期定死的——所以"把第一行顶下去"只能靠
// **在它前面放一个已知高度的兄弟**。这是"用现有配件组装"那条路的解法，不是绕过。
//
// ---------------------------------------------------------------------------
// 另外两条现成的机制直接用，不重写
//
//   - **滚动**：`PanelData::scroll`（B12）——滚轮 / 拖滑块 / 点轨道全在基类里；
//     本类只重写那几个指针钩子，在**基类处理完之后**同步一次可见窗口。
//   - **裁剪**：`PanelData::clipChildren`——绘制与命中同源（A5），所以"滚出去的
//     行点不到"是白拿的。本类构造时强制打开它（列表不能滚出框外还看得见）。
//
// 第一版**不做**：拖拽重排、行内编辑、多列 / 排序、树形缩进（要树再单开一条）。

#include <widget/InkingStaticWidget.h>
#include <event/InkingEvent.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/ListViewData.h>

#include <cstddef>
#include <string>
#include <vector>

namespace inking {
namespace prefab {

class ListRow;

/**
 * @brief C3：预制列表（可滚、可选、可只建可见项）。
 *
 * ```cpp
 * inking::prefab::ListViewStyle style = inking::prefab::ListViewStyle::MakeDefault();
 * style.panel.width = 360;
 * style.panel.height = 480;
 * style.windowed = true;          // 只建可见项（1000 项也只有十几个节点）
 * inking::prefab::ListView list(&scene, style);
 * list.SetItems(rows);
 * list.selectionChanged.Subscribe([](const std::string& id, int index) { ... });
 * ```
 */
class ListView : public InkingStaticPanel {
public:
    /**
     * 选择变了。参数是 `(刚点的那一项的标识, 它的索引)`。
     *
     * 单选：每次点都发；多选：点了就切换那一项、每次都发；
     * `ListSelectionMode::None` 时**不发**（那一路是"命令列表"，
     * 用 `itemActivated`）。
     */
    InkingEvent<const std::string&, int> selectionChanged;

    /// 一项被点了（**不管选择模式**、也不管选中没选中）。
    InkingEvent<const std::string&, int> itemActivated;

    /// 选择集整体变了（多选时"我改的是哪一项"往往不如"现在选了哪些"有用）。
    InkingEvent<> selectionSetChanged;

    explicit ListView(
        InkingWidget* parent,
        const ListViewStyle& style = ListViewStyle::MakeDefault());

    // ---------------- 数据 ----------------

    /**
     * 换一整份项列表（`ClearChildren()` + 重建 + 重排 + 重量内容尺寸）。
     *
     * **幂等**：逐项相等时什么都不做。项列表变了会把选择集里越界的索引清掉
     * （不然"选了第 5 项、列表只剩 3 项"会留下一个查不到的选中态）。
     */
    void SetItems(std::vector<ListItem> items);

    const std::vector<ListItem>& GetItems() const noexcept { return _items; }

    /// 总项数（和"建出来几个行节点"不是一回事——窗口化时后者小得多）。
    std::size_t GetItemCount() const noexcept { return _items.size(); }

    /// 第 `index` 项的标识（`id` 为空时给索引的十进制串）。越界 = 空串。
    std::string ItemId(int index) const;

    // ---------------- 选择 ----------------

    /// 当前选择集（**项索引**，升序）。
    const std::vector<int>& GetSelection() const noexcept { return _selection; }

    bool IsSelected(int index) const;

    /// 设某一项选不选（单选模式下 `true` 会先把别的清掉）。
    void SetSelected(int index, bool selected);

    /// 清空选择集。
    void ClearSelection();

    // ---------------- 窗口化（只建可见项） ----------------

    /**
     * 把建出来的行对齐到当前的可见区间（**幂等**：区间没变就什么都不做）。
     *
     * 什么时候需要它：滚动量变了之后。本类已经在 `onPointerWheel` /
     * `onPointerPress` / `onPointerDrag` 里替你调了一次，**并且**每个渲染帧
     * （`onAnimationTick`）兜一次底——所以正常用法下你可以不管它。
     * 自检与"我一帧里连滚了三次"那种场合可以自己调（多调无害）。
     */
    void SyncVisibleWindow();

    /// 现在建出来几个**行节点**（不含两头的占位块）。窗口化判据的观测口。
    std::size_t GetBuiltRowCount() const noexcept { return _rowNodes.size(); }

    /// 现在建出来的第一行 / 最后一行是第几项（空窗口时都是 −1）。
    int GetBuiltFirstIndex() const noexcept { return _builtFirst; }
    int GetBuiltLastIndex() const noexcept { return _builtLast; }

    /// 第 `index` 行的矩形（**相对视口左上角**，设计坐标）。
    /// 没建出来的行（窗口化时在可见区之外）返回零矩形。
    ShapeBounds GetRowRect(int index) const;

    /// 第 `index` 行的节点；没建出来时 nullptr。
    ListRow* GetRowNode(int index) const;

    // ---------------- 滚动 ----------------

    /// 滚到某个位置（**设计像素**，会夹进可滚范围），并按需对齐可见窗口。
    void ScrollTo(float x, float y);

    /// 滚到第 `index` 项（让它出现在视口顶部）。
    void ScrollToItem(int index);

    /// 行高（设计坐标）。
    float GetRowHeight() const noexcept { return _style.rowHeight; }

    /// 全部项排起来的内容高度（= 项数 × 行高）。
    float GetContentHeight() const noexcept;

    const ListViewStyle& GetStyle() const noexcept { return _style; }

    /// 现在开着窗口化吗。
    bool IsWindowed() const noexcept { return _style.windowed; }

protected:
    void onAnimationTick(float deltaSeconds) override;
    void onPointerWheel(float deltaX, float deltaY) override;
    /// 按下 / 抬起。**只认主键**（行选中 / 拖动滚动）；右键菜单留给下一版（0.5.0）。
    void onPointerPress(PointerButton button, bool press) override;
    void onPointerDrag(float designX, float designY, float deltaX,
                       float deltaY) override;

private:
    /// 全部重建（非窗口化那条路）。
    void rebuildAll();

    /// 按当前滚动量重建可见窗口（窗口化那条路）。
    void rebuildVisible();

    /// 造一行的节点并挂上（`index` 是**全局项索引**）。
    void addRowNode(int index);

    /// 造一个占位块（`height` 为 0 时不造）。
    void addSpacer(float height);

    /// 把每一行的选中外观刷成当前选择集（不重建节点）。
    void applySelectionVisuals();

    /// 一行被激活的统一出口（真点击与 `itemActivated` 共用）。
    void onRowActivated(int index);

    /// 清掉选择集里越界的索引（项列表变了之后）。
    void pruneSelection();

    /// 视口高度（扣掉滚动条条带；给窗口化的区间计算用）。
    float viewportHeight() const noexcept;

    ListViewStyle _style{};
    std::vector<ListItem> _items{};
    std::vector<int> _selection{};  ///< 升序
    std::vector<ListRow*> _rowNodes{};
    int _builtFirst = -1;
    int _builtLast = -1;
};

}  // namespace prefab
}  // namespace inking
