#pragma once

// 画布内容由**谁**画（A12）。
//
// 这是"把绘制入口提升成公开 API"这条欠账的落点：以前能作画的只有框架内部的
// 组件（写一个类、重写 `onRender`、用 `detail::` 那套内部件）。现在业务代码
// 派生这个接口（或者直接给一个 `std::function`），把画布要画的东西写在自己的
// 类里——框架不认识它，也不需要认识。
//
// 为什么是"接口 + 回调"而不是"一份图元清单"：
//   - 接口这条路**不用框架为每种画法长字段**（加一种画法 = 调用方自己多写几行）；
//   - 绘制器可以是一整个子系统（图表 / 粒子 / 地图 / 波形），它自己管自己的状态；
//   - 反过来"框架持有一份图元数组"就得由框架解释每一种图元，那正是本任务
//     要消掉的东西（`TaskGuide.md` 的最终目标）。

#include <functional>

namespace inking {

class CanvasContext;

/**
 * @brief 往画布上画东西的那个人。
 *
 * 契约（三条，都很短）：
 *   1. **坐标系**：`CanvasContext` 给的是**画布本地设计坐标**（0,0 = 画布左上角，
 *      单位与 `width` / `height` 同一套）。画布自己在哪儿、被哪个容器带着转、
 *      当前展示倍率是多少——**一个都不用管**，框架全包；
 *   2. **只管"画成什么样"，别管"什么时候画"**：调用时机由画布决定
 *      （`Invalidate()` 之后的下一次重绘调一次）；每次调用都应当把**整块画布**
 *      画完（不是增量），因为回放是"拿着记录下来的命令重画一遍"；
 *   3. **别在 `Paint` 里改几何 / 调写入口**：它可能在渲染中途被调用
 *      （和 `onRender` 同一档时序）。要改数据就在外面改，改完 `Invalidate()`。
 *
 * 生命周期：`InkingStaticCanvas::SetPainter` 收 `std::shared_ptr`，所以同一份
 * 绘制器可以喂给多块画布；画布本身**不拥有**它的语义之外的东西。
 */
class ICanvasPainter {
public:
    /**
     * @brief 把整块画布画一遍。
     * @param canvas 绘制词汇的入口（`CanvasContext`）。它的生命周期**只到本次
     *               调用结束**——别存它的指针 / 引用（下一次重绘会换一个）。
     */
    virtual void Paint(CanvasContext& canvas) = 0;

    /// 虚析构必须有（`SKILL.md` §3.4）：否则通过基类指针销毁派生对象是 UB。
    virtual ~ICanvasPainter() = default;
};

/**
 * @brief 轻量替代品：一个 lambda 就够了，不必为它派生一个类。
 *
 * 语义与 `ICanvasPainter::Paint` 完全一样，只是不带任何状态。
 */
using CanvasPaintFn = std::function<void(CanvasContext&)>;

}  // namespace inking
