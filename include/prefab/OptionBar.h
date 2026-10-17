#pragma once

// 预制件 C1「选项栏」（`docs/TaskGuide.md` 的 C 系列）。
//
// **它就是**：`layout` 排列的按钮组——一个 Panel 容器 + 每个选项一个按钮。
// 用户"加一个选项"= 往 `std::vector<OptionItem>` 里加一个对象（`SetItems`）。
//
// 项的 `optionClicked` 里唤出新的选项栏，于是**选项栏能互相嵌套**：
// 那是调用方的约定，框架**不需要知道"嵌套"这件事**（任务里特意写明了）。
// 本类不提供"选项栏栈"、也不提供"子选项栏自动回退"——第一版不做。
//
// ---------------------------------------------------------------------------
// 为什么是"预制件"而不是"新组件"
//
// `CXXCSS` 只描述**单层**组件（`CXXCSS.md` §7.2：生成场景那条路撤掉，正是因为它
// 不能嵌套），所以"容器 + 子项"这件事一直没有归处。预制件补的就是这个空：
// **组装封装一次，以后按数据复用**，用户侧只给数据（项列表 + 外观），不写 C++。
//
// 实现上它就是 `InkingStaticPanel` 的派生类——容器的五条重写（子树变换 /
// 子树命中 / 命中深挖 / 包络 / 裁剪）**一条都不用再写**，白拿：
//   - 子项不进全局命中表，事件靠 A8a 的"深挖一层"下发到被点中的那一项；
//   - 裁剪 / 滚动直接用 Panel 的 `clipChildren` / `scroll`；
//   - 布局用 A6 的通道（`Relayout()` 把求解结果写进子项的运行期几何）。
//
// 所以本类里**没有一处**新的绘制 / 命中 / 布局代码——这正是"不写新底层"。

#include <widget/InkingStaticWidget.h>
#include <event/InkingEvent.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/OptionBarData.h>

#include <cstddef>
#include <string>
#include <vector>

namespace inking {

// 只前向声明：公开头不拖别的组件的实现进来（`AGENTS.md` §6 第 8 条那条纪律的
// 同族——能不全量包含就不全量包含）。
class InkingStaticButton;

namespace prefab {

/**
 * @brief C1：预制选项栏。
 *
 * ```cpp
 * inking::prefab::OptionBar bar(&scene);
 * bar.SetItems({{"save", "存档"}, {"load", "读档"}, {"quit", "退出"}});
 * bar.optionClicked.Subscribe([](const std::string& id, std::size_t index) {
 *     if (id == "quit") { ... }   // 想嵌套就在这里再建一个 OptionBar
 * });
 * ```
 *
 * 组装约定（`docs/TaskGuide.md` C 系列的三个通用约定）：
 *   1. **数据驱动**：项列表变了就 `SetItems`，内部 `ClearChildren()` + 重建；
 *   2. **外观可配置**：默认 json（`CXXCSS/Button/optionItem.json`）或自己那份；
 *   3. **不引入浮层、不引入输入消费**：要显示 / 隐藏就 `SetVisible(false)`，
 *      要"点外部关闭"由调用方约定（别处一点就隐藏）。
 *
 * 第一版**不做**：浮层 / 弹出定位、选项栏栈、项的键盘导航、子选项栏自动回退。
 */
class OptionBar : public InkingStaticPanel {
public:
    /**
     * 某一项被点了。参数是 `(项的标识, 项的索引)`。
     *
     * 标识取自 `OptionItem::id`；id 为空时是索引的十进制串（见 `ItemId`）。
     * 禁用项**不会**触发这个事件。
     */
    InkingEvent<const std::string&, std::size_t> optionClicked;

    explicit OptionBar(
        InkingWidget* parent,
        const OptionBarStyle& style = OptionBarStyle::MakeDefault());

    /**
     * 换一整份项列表：`ClearChildren()` + 逐个重建 + `Relayout()`。
     *
     * **幂等**：新列表与当前列表逐项相等时什么都不做（一个脏都不标、节点不重建）
     * ——"重建不该泄漏节点"这条判据靠的就是"没变就不重建"，另一边
     * `GetBuiltItemCount()` 会一直等于项数。
     *
     * ⚠️ 项很多（几百）时这里是 O(n²)：`AddChild` 每次都要在拥有表里查重
     * （`docs/UsageReport.md` §6.7 第 6 条）。几十项无所谓；真到几百项请用
     * C3 的 `ListView`（它按需只建可见项）。
     */
    void SetItems(std::vector<OptionItem> items);

    /// 当前项列表。
    const std::vector<OptionItem>& GetItems() const noexcept { return _items; }

    /// 现在有几项。
    std::size_t GetItemCount() const noexcept { return _items.size(); }

    /**
     * 第 `index` 项的矩形，**相对本容器左上角**（设计坐标）。
     *
     * 它是布局算出来的那个矩形（`GetX/Y/GetWidth/GetHeight` 的拼装），
     * 所以"项数 / 顺序 / 每项矩形"这三条验收判据能直接用数字钉住。
     * 越界返回零矩形。
     */
    ShapeBounds GetItemRect(std::size_t index) const;

    /// 第 `index` 项的按钮节点（要自己挂事件 / 改文字时用）。越界 = nullptr。
    InkingStaticButton* GetItemNode(std::size_t index) const;

    /// 第 `index` 项的标识（`id` 为空时给索引的十进制串）。越界 = 空串。
    std::string ItemId(std::size_t index) const;

    /// 现在真的建了几个项节点（= 项数）。重建泄漏的观测口。
    std::size_t GetBuiltItemCount() const noexcept;

    /**
     * 内容驱动的高度（A6 的 `MeasureLayout`）：`padding` + n 项 + (n-1) 个间距。
     *
     * **纯函数**（布局可能问好几遍）：不标脏、不改几何。想让它"按内容长高"，
     * 就把它挂在声明了布局的父容器下，让父级把这份尺寸写下来——静态档自己
     * 没有几何写入口，这是有意保留的那条铁律。
     */
    LayoutSize MeasureLayout(const LayoutConstraints& constraints) const override;

    /**
     * 直接触发第 `index` 项（跳过指针链路，模拟一次点击）。
     *
     * 给自检与"快捷键也走同一条路"的用法：它和真的点一下**走同一个出口**
     * （`optionClicked`），所以两条路不会分叉。禁用项返回 false。
     */
    bool Activate(std::size_t index);

    /// 构造时用的那份样式（可选的外观覆盖会写进 `itemLook`）。
    const OptionBarStyle& GetStyle() const noexcept { return _style; }

    /**
     * @brief 整栏的层级（`zIndex`）：容器自己用这个值，**每一项用 `+1`**。
     *
     * 为什么要有它（实测踩的）：项是**运行期重建**的子节点（`SetItems` /
     * 过滤），注册序号比容器晚得多——如果只给容器一个高 z、项还是默认 0，
     * 就会出现"展开的那块**背景**被别人盖住、**选项**却浮在最上面"这种怪象
     * （`AGENTS.md` §6 第 104 条）。把两者绑成一层，它们才作为一个整体叠放。
     *
     * 调它会连带改已建项的 z（`ChangeZIndex` 是骨架变化，会重排绘制顺序 +
     * 重烘命中表），所以"展开到最上层"这件事是把它整块提上去，而不是只提背景。
     */
    void SetLayerZ(int zIndex);

    int GetLayerZ() const noexcept { return _layerZ; }

private:
    /// 把 `_items` 建成子节点（只在真的变了之后调）。
    void rebuildItems();

    /// 一个项被激活的统一出口（真点击与 `Activate` 共用）。
    void onItemActivated(std::size_t index);

    /// `index` 项的高度（`OptionItem::height` 为 0 时用 `style.itemHeight`）。
    float itemHeightAt(std::size_t index) const noexcept;

    OptionBarStyle _style{};
    std::vector<OptionItem> _items{};

    /// 整栏的层级：容器用它、项用 `+1`（见 `SetLayerZ`）。
    int _layerZ = 0;
};

}  // namespace prefab
}  // namespace inking
