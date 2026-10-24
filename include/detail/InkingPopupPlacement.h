#pragma once

// 定点浮窗的**落位算术**：给定一个定点（点击位置）、浮窗尺寸和一个可用范围，
// 算出"浮窗该放在哪、自锚点朝哪边、是不是被夹住了"。
//
// ---------------------------------------------------------------------------
// 为什么是一个纯函数
//
// 四方向 × 两轴 × "翻转后仍放不下"的组合一共十几种，靠开窗口一个个看是不可验的。
// 抽成纯算术之后，自检能拿四组数字把它钉死（`check_popup_placement`），
// 组件那边只剩"把结果写进锚点与偏移"这一层。
//
// 它**不认识组件、不标脏、不读场景**：输入是数字，输出是数字。
//
// ---------------------------------------------------------------------------
// 语义（每轴独立，两轴互不影响）
//
//   1. 先按 `preferred` 摆：`位置 = 定点 − 尺寸 × preferred`。
//      自锚点 `(0,0)` 表示"浮窗左上角对准定点"（于是向右下展开）。
//   2. 放得下就结束（`side = +1`）。
//   3. 放不下就**翻到对侧**：`preferred → 1 − preferred`，再算一次。
//      放得下就结束（`side = −1`）。`preferred == 0.5` 时翻转无意义，直接进第 4 步。
//   4. 还放不下就**夹到范围内**（`side = 0`、`clamped = true`）。
//      尺寸比范围还大时贴住左上（或下/右）边界、允许另一侧溢出——
//      这就是调用方说的"除非真的过大"。
//
// **两轴独立**是刻意的：贴着右边缘的小菜单仍应向下展开，不该因为 x 翻了一下
// 就把 y 也翻过去（那会把菜单放到指针上方，手感是"跳"）。

#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingShapeSpec.h>

namespace inking {
namespace detail {

/**
 * @brief 一次落位的结果。
 *
 * `x` / `y` 是**相对父级左上角**的位置（不是 offset）；组件那边按自己的
 * `traceAnchor` 与父级尺寸换算成 offset（见 `InkingFixedPopup::OpenAt`）。
 */
struct PopupPlacement {
    AnchorPoint selfAnchor{0.0f, 0.0f};  ///< 最终自锚点（通常是 0 或 1）
    float x = 0.0f;                      ///< 相对父级左上角
    float y = 0.0f;
    int sideX = 1;  ///< +1 正向 / −1 翻转 / 0 夹紧
    int sideY = 1;
    bool clampedX = false;  ///< 这一轴翻转后仍放不下，被夹到边界
    bool clampedY = false;
};

/**
 * @brief 解一次落位。
 *
 * @param targetX / targetY 定点（父级坐标，通常就是点击位置）。
 * @param width / height    浮窗尺寸；任一 <= 0 时退化成"位置 = 定点、不翻转不夹紧"。
 * @param bounds            可用范围（父级坐标）。`width` / `height` <= 0 时同样退化。
 * @param preferred         期望的自锚点；`(0,0)` = 向右下展开，`(1,1)` = 向左上。
 *
 * 退化不是错误：`OpenAt` 可能在布局算完之前被调用（尺寸还是 0），那时"放在定点上"
 * 是最合理的答案，等尺寸定下来再 `SetTarget` 一次即可。
 */
PopupPlacement SolvePopupPlacement(float targetX, float targetY, int width,
                                   int height, const ShapeBounds& bounds,
                                   const AnchorPoint& preferred);

}  // namespace detail
}  // namespace inking
