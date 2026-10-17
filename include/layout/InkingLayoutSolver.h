#pragma once

// 布局求解器（A6）：**纯函数**——给一份规则、容器尺寸、每个孩子的期望尺寸，
// 算出每个孩子的矩形。不碰组件、不碰场景、不标脏，所以可以单独用数值断言验。
//
// 为什么单拎出来：布局的坑几乎全在"算术"上（间距、剩余空间怎么分、
// min/max 什么时候夹、溢出怎么办），而那些都能用数字钉死。真正的组件接线
// （把结果写回去 + 重烘命中表）只是薄薄一层。

#include <dataStruct/InkingLayout.h>

#include <vector>

namespace inking {
namespace detail {
namespace layout {

/// 一个孩子交给求解器的输入。
struct LayoutInput {
    /// `Measure` 出来的期望尺寸（内容驱动：文字多高、图片多大）。
    LayoutSize desired{};
    /// 这个孩子在布局里的意愿（margin / fillMain / min-max / selfAlign）。
    LayoutItem item{};
};

/// 求解器给一个孩子算出的结果（相对**父级左上角**）。
struct LayoutOutput {
    LayoutRect rect{};  ///< 最终位置与尺寸
    LayoutSize size{};  ///< 最终尺寸（与 rect 的 width/height 一致，方便取用）
};

/**
 * @brief 解一次布局。
 *
 * @param spec          父容器的布局规则（`kind == None` 时原样返回期望尺寸、
 *                      位置全 0——调用方本该先判 `Valid()`，这里兜底不崩）。
 * @param containerSize 容器**自己**的尺寸（fill / stretch / 对齐都相对它算）。
 * @param items         孩子，**顺序 = 排布顺序**。
 * @return 与 `items` 等长的结果。
 *
 * 规则（与 `InkingLayout.h` 里的字段一一对应）：
 *
 *   1. 先在容器里扣掉 `padding`，剩下的叫**内容区**；
 *   2. 主轴上：每项的主轴尺寸 = `desired` 夹到 `[min, max]`；再把
 *      `Σ(主轴尺寸 + 主轴方向 margin) + gap × (n-1)` 求和；
 *   3. **剩余空间**（内容区 − 上面的和）：
 *      - `> 0` 且有人声明 `fillMain` → **均分给它们**（均分后再夹一次 max）；
 *      - `> 0` 且没人 `fillMain` → 交给 `justify` 分配（center / end /
 *        space-between / around / evenly）；
 *      - `<= 0`（内容超出）→ **一律按 Start 排**，溢出的部分交给容器自己的
 *        裁剪 / 滚动（A5），求解器不挤、不缩；
 *   4. 交叉轴上：`Stretch` 拉满内容区（扣掉两侧 margin），否则用 `desired`
 *      的交叉轴尺寸，再按 `align`（孩子可用 `selfAlign` 覆盖）定位；
 *   5. **`min/max` 永远最后夹**——它是约束，不是建议；
 *   6. `Grid`：按 `columns` 等分列宽（`columnGap` 分隔），行高 = 该行里最高的
 *      那一项（含其 margin），单元格内按 `align` 摆放。
 */
std::vector<LayoutOutput> Solve(const LayoutSpec& spec,
                                const LayoutSize& containerSize,
                                const std::vector<LayoutInput>& items);

}  // namespace layout
}  // namespace detail
}  // namespace inking
