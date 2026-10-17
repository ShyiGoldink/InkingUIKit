#pragma once

// Panel"画一笔 + 算包络"的实现：**静态 / 动态 Panel 共用**（对应 ButtonPaint.h）。
//
// 放 src/ 而不是 include/：它 include 渲染那一侧的东西，属于**组件内部实现**，
// 不进公开头（公开头不拖 SDL 进来，docs/AGENTS.md §6 第 8 条）。
//
// 命中判定**不在这里**：它已经被抽成公共件 `detail::HitTestShape`
// （`include/ink/basic/InkingHit.h`）——面板和按钮用的是同一份"世界坐标 → 本地
// → 形状"的推导，不能各写一遍。

#include <widget/InkingWidget.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTransform.h>
#include <panel/PanelData.h>

#include <cstdint>

namespace inking::detail {

/**
 * 画这个面板**这一帧看起来的样子**（设备像素）：按形状填一笔。
 *
 * `displayColor` 是插值之后的显示色，`transform` 是插值之后的显示变换——
 * 两者与命中那边用的是**同一个**变换（命中取逆），所以"画在哪儿"和
 * "点在哪儿"永远是同一个答案。
 *
 * 子节点不在这里画：它们由场景按 z 序逐个提交，各自带上"祖先子树变换"。
 */
void PaintPanelAppearance(const InkingAnchor& node, const ShapeSpec& shape,
                          std::uint32_t displayColor,
                          const TransformSpec& transform, float pixelX,
                          float pixelY);

/**
 * 面板的**保守命中包络**（本地坐标，相对左上角）。
 *
 * 它比按钮那份多一层：按钮只要把"三个状态的自己"并起来，面板还要把
 * **整棵子树**并进来（子孙不进全局命中表之后，"这条包络要盖住哪儿"
 * 就完全由容器自己回答，见 `InkingWidget::OwnsSubtreeHit`）。
 *
 * 算法两步：
 *   1. 在**布局空间**里求整棵子树的并集：自己的形状 AABB + 每个子节点的
 *      `SubtreeHitEnvelope`（搬到本节点的坐标系里）；
 *   2. 把这份并集**按每个状态的变换各转一次**再取并集——容器的变换作用于
 *      整棵子树，所以整块包络一起转是对的，也比"逐个节点转了再并"更紧。
 */
ShapeBounds PanelHitEnvelope(const InkingWidget& node, const ShapeSpec& shape,
                             int width, int height,
                             const PanelAppearance& closed,
                             const PanelAppearance& open);

}  // namespace inking::detail
