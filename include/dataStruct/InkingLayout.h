#pragma once

// 布局的数据层：规则怎么写、意图怎么表达。
//
// 三句话先说清这一层在整个布局里的位置：
//
//   1. 规则住在父容器上（`LayoutSpec`）
//    孩子只在自己身上写"我在这个布局里 的意愿"（`LayoutItem`：margin / 是否吃剩余空间 / min-max）
//    就像 CSS 里 `display:flex` 写在父级、`flex-grow` 写在子级；
//   2. 这里只有意图，没有结果。算出来的矩形由布局求解器（`detail::layout::ArrangeSubtree`）产出
//      产出写进组件的运行期几何（`_width/_height/_offsetX/_offsetY`），然后标结构脏让命中表重烘。
//      为什么不把结果也放回数据里：`AnchorData` 是配置、是要被反复读取的输入，
//      一旦被结果覆盖，重排就再也找不到原始意图了；
//   3. 位置推导一个字都不改：布局只是把锚点公式里的两个量（尺寸、偏移）算出来。
//      于是绘制 / 命中 / AABB / 子树变换全都不知道"有布局这回事"。
//
// 单位全是设计坐标（和 `AnchorData::width` / `offsetX` 同一套）。

#include <cstdint>

namespace inking {

// 布局种类。`None` = 不布局（老行为：锚点自己定位）。
enum class LayoutKind : std::uint8_t {
    None = 0,
    Stack,  // 一维排布：竖着或横着，一个接一个
    Grid,   // 网格：定列数，逐行填
};

// `Stack` 的主轴方向。
enum class LayoutAxis : std::uint8_t {
    Vertical = 0,  // 竖排（一行一个）
    Horizontal,    // 横排（一列一个）
};

// 主轴上"多余的空间怎么分"。
enum class LayoutJustify : std::uint8_t {
    Start = 0,     // 挤在一头（默认）
    Center,        // 居中
    End,           // 挤在另一头
    SpaceBetween,  // 两端顶住，间隙均分
    SpaceAround,   // 每个项两侧各留半份
    SpaceEvenly,   // 项与两端都是等份
};

/// 交叉轴上"单个项怎么放"。
enum class LayoutAlign : std::uint8_t {
    Start = 0,
    Center,
    End,
    Stretch,  // 拉满交叉轴（默认；一列按钮左右撑满是常态）
};

// 四个方向的间距（padding 与 margin 共用）。设计坐标。
struct LayoutSpacing {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// 一个尺寸（求解器内部的表达；`AnchorData` 那边是 int）。
struct LayoutSize {
    float width = 0.0f;
    float height = 0.0f;
};

// 可用空间（父级给孩子的约束）。`0` 表示那个方向不限。
struct LayoutConstraints {
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;
};

// 一个算好的矩形（相对父级左上角）。
struct LayoutRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/**
 * @brief 一个孩子在父容器布局里的意愿（挂在孩子自己的数据上）。
 *
 * 全部有默认值，所以"什么都不写"= 老样子：按自己的尺寸、不吃剩余空间、
 * 没有 min-max、margin 为 0。
 */
struct LayoutItem {
    // 外边距：算在这个孩子的矩形之外（间隙之外再加一圈）。
    LayoutSpacing margin{};

    /**
     * 主轴方向上吃掉剩余空间。
     *
     * 一列里多个 `fillMain` 就均分剩余空间；剩余空间为负（内容超出）时
     * 谁也不吃，超出的部分交给容器自己的裁剪 / 滚动策略
     * 这与"min 夹住"是同一条规则的两种结果。
     */
    bool fillMain = false;

    // 尺寸下限（0 = 不限）。
    float minWidth = 0.0f;
    float minHeight = 0.0f;

    // 尺寸上限（0 = 不限）。
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;

    // 是否用下面的 `selfAlign` 覆盖容器的 `align`（默认不覆盖）。
    bool hasSelfAlign = false;
    LayoutAlign selfAlign = LayoutAlign::Start;
};

/**
 * @brief 一个容器的布局规则（挂在父容器的 `AnchorData::layout` 上）。
 *
 * `kind == None` 时整份规则都不生效——这是"老配置一行不改"的保证。
 */
struct LayoutSpec {
    LayoutKind kind = LayoutKind::None;

    // ---- Stack ----
    LayoutAxis axis = LayoutAxis::Vertical;
    // 主轴间距（相邻两项之间；不算 padding，也不算 margin）。
    float gap = 0.0f;
    LayoutJustify justify = LayoutJustify::Start;
    // 交叉轴的默认对齐（孩子可以用 `LayoutItem::selfAlign` 覆盖）。
    LayoutAlign align = LayoutAlign::Stretch;

    // Grid
    int columns = 1;         // 列数（至少 1）
    float rowGap = 0.0f;
    float columnGap = 0.0f;

    //共用
    // 内容区内缩：所有孩子都在这个范围里排。
    LayoutSpacing padding{};

    // 这份规则要不要生效。
    bool Valid() const noexcept { return kind != LayoutKind::None; }
};

}  // namespace inking
