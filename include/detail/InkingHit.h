#pragma once

// 形状命中判定：**世界坐标 → 本地坐标 → `ShapeContains`**。
//
// 它原来叫 `HitTestButton`、住在 `src/button/ButtonPaint.h` 里，只有按钮用。
// Panel 要判命中时它就成了第二个调用者——"抄第二遍就抽出来"（`InkingDraw.h`
// 头顶同一条理由）：两份实现各自演化，就会出现"一个组件把祖先变换撤干净了、
// 另一个忘了撤"，而那种错的表现是"画在这儿、点在别处"。
//
// 为什么放在公开头而不是 `src/` 下：include/ 是本项目唯一的头文件搜索根，
// 内部件放 src/ 就得再挂一个 PRIVATE include 目录；而它**不碰 SDL**
// （只用形状、变换、仿射三个纯数学件），所以进公开头不破"公开头不拖 SDL
// 进来"那条纪律（docs/AGENTS.md §6 第 8 条）。
//
// 三步必须与绘制**同源**，缺一步就是"画的是一个、点的是另一个"：
//   1. 撤掉**祖先子树变换**（容器动了 / 转了，子树里的点在世界上也跟着动）；
//   2. 减去自己的绝对布局位置（`GetAbsX/Y`，与提交给渲染器的左上角同源）；
//   3. 过一次自己**变换的逆**（绘制那边过的是正变换，两者严格互逆、
//      绕同一个中心）；最后问形状层（`ShapeContains`）。
//
// 参数为什么是 `InkingWidget` 而不是 `InkingAnchor`：第 2 步的 `GetAbsX/Y`
// 与第 3 步之前那一次 `AncestorAffine` 都在 **Widget 层**——前者要问"当前显示
// 尺寸"，后者要走父链读每一层的子树变换。定位层只给锚点推导与整棵树的可见性，
// 这两个问题它答不了。所以这里只需要 `InkingWidget` 的**前向声明**，
// 完整定义留给 .cpp（本头因此仍然不拖 SDL 进来）。
//
// 实现见 `src/detail/InkingHit.cpp`。

#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTransform.h>

namespace inking {

/// 本头只按引用用它，不需要完整定义（见上面那段）。
class InkingWidget;

namespace detail {

/**
 * 这个点在不在这个形状里（世界坐标 = 设计坐标）。
 *
 * 调用方负责 z 序与"让不让过"：本函数只回答"几何上在不在"
 * （和 `InkingWidget::HitTest` 的分工一致，见那里的注释）。
 *
 * @param node      被测的节点（读它的绝对位置与宽高）。
 * @param shape     它的形状定义（渲染、命中、AABB 都从这一份派生）。
 * @param transform 它**当前**的变换；没有变换时传 `TransformSpec{}`。
 */
bool HitTestShape(const InkingWidget& node, const ShapeSpec& shape,
                  const TransformSpec& transform, float worldX, float worldY);

/**
 * 这个点在不在一个**布局矩形**里（世界坐标，A5）。
 *
 * 与 `HitTestShape` 走的是同一条三步（撤祖先变换 → 减绝对位置 → 过自身变换的
 * 逆），只有最后一步不同：形状换成调用方给的矩形。裁剪要用它——
 * 容器的裁剪区不一定等于它的形状（将来可以是内缩的视口 / 带内边距的窗口）。
 *
 * 边界算**在内**（和 SDF 的 `f <= 0` 一致）。渲染侧那个矩形会按包含式取整成
 * 整数像素，所以"边界那一像素"两边可能差一格——自检别贴着边界写断言。
 */
bool HitTestLocalRect(const InkingWidget& node, const ShapeBounds& bounds,
                      const TransformSpec& transform, float worldX,
                      float worldY);

}  // namespace detail
}  // namespace inking
