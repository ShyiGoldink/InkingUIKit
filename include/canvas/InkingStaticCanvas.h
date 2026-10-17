#pragma once

// Canvas（A12）：一块"谁都能画"的表面 —— 组件那一半。
//
// 它只有**一档**（静态）：几何构造即定型（位置 / 尺寸 / 形状都不动），画面由
// 内容决定，而内容变了只标**重绘脏**——正是静态档的定义。所以它照常进命中表，
// 命中表也不会因为"画的内容变了"而重烘（`AGENTS.md` §6 第 40 条那一族）。
//
// 三件事说清楚就够了：
//
//   1. **本体**：形状 + 底色 + 描边，和图片 / 面板一样按形状画；命中判形状本体。
//      **默认透明**（`color = 0` 就不画），因为"只画内容的透明层"是画布最常见的
//      用法——这里**故意不做**按钮那种"全透明兜底成黑"。
//   2. **内容**：由注入的绘制器画（`SetPainter`），坐标是画布本地设计坐标，
//      默认**剪到画布边界**（画布 = 视口）。注意这条边界：裁剪区是**本体外接
//      矩形**，形状只决定"本体怎么画 / 命中在哪"——圆形画布的四角仍然画得出
//      内容（要按形状剪内容得先渲染到离屏纹理再按形状贴回来，那是第二版）。
//   3. **命中**：整块本体可点（按 `shape`），**内容不参与命中**——这是 A12 定下
//      的边界：内容里的热区（图表元素 / 节点）留给第二版，那时才有真实的
//      "点在哪一笔上"的需求。
//
// 脏驱动（"数据变了才重画"）：绘制器**不是每帧被调用**。画布把一次的绘制结果
// 记成命令表，之后每帧只是回放命令表；只有 `Invalidate()` 之后的那次重绘才会
// 重新调绘制器。于是绘制器里那些"比重新画更贵"的逻辑（布局计算 / 采样 / 排序）
// 每帧只跑一次，而画面照旧每帧提交（框架所有组件都是这样：提交是每帧的，省的是
// 上游的计算）。

#include <canvas/CanvasData.h>
#include <canvas/InkingCanvasPainter.h>
#include <widget/InkingStaticWidget.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace inking {

namespace detail {
class CanvasRecorder;
}

/**
 * @brief 一块可以自由作画的静态组件。
 *
 * 典型用法：
 * @code
 *   auto* canvas = new InkingStaticCanvas(&scene, data);
 *   canvas->SetPainter([&](inking::CanvasContext& ctx) {
 *       ctx.FillRect(10, 10, 100, 40, 0xFFE05252u);
 *       ctx.DrawLine(10, 60, 200, 60, 2.0f, 0xFF52E07Fu);
 *   });
 *   // 数据变了之后：
 *   canvas->Invalidate();
 * @endcode
 */
class InkingStaticCanvas : public InkingStaticWidget {
public:
    /**
     * @param parent 上级锚点；非拥有。画布必须在某个场景的子树下才会被渲染。
     * @param data   配置数据（构造时逐字段搬进基类）。
     */
    InkingStaticCanvas(InkingWidget* parent, const CanvasData& data);

    /// 析构必须在 .cpp 里定义：`_recorder` 是不完整类型（`unique_ptr` 的老规矩）。
    ~InkingStaticCanvas() override;

    // ---------------- 绘制器（写入口） ----------------

    /**
     * 换一个绘制器（`shared_ptr`：同一份绘制器可以喂给多块画布）。
     *
     * 内部会 `Invalidate()`：绘制器换了，画面当然要重画。传 nullptr 等于清空。
     */
    void SetPainter(std::shared_ptr<ICanvasPainter> painter);

    /// 换一个 lambda 绘制器（内部包一层 `ICanvasPainter`）。同样会 `Invalidate()`。
    void SetPainter(CanvasPaintFn paint);

    /// 清空绘制器：之后的画面只剩本体（形状 + 底色 + 描边）。
    void ClearPainter();

    /// 现在有没有绘制器。
    bool HasPainter() const noexcept;

    // ---------------- 内容脏（写入口） ----------------

    /**
     * @brief "我画的内容变了，下次重绘请重新调绘制器。"
     *
     * 这是把"什么时候重新画"交给调用方的那条通道（框架不做隐式判断——它没有
     * 办法知道你的数据变没变）。它**幂等**：连喊十次也只在下次重绘时重画一次。
     *
     * 它会标**重绘脏**（画面真的会变），但**不标命中脏 / 结构脏**：几何没动，
     * 命中表一条都不用改——这正是画布能待在静态档的原因。
     */
    void Invalidate();

    /// 内容脏（= 下一次重绘会重新调绘制器）。观测口，自检用。
    bool IsContentDirty() const noexcept;

    // ---------------- 内容裁剪（写入口） ----------------

    /**
     * 内容要不要剪到画布边界（默认跟 `CanvasData::clipContent`，即**剪**）。
     *
     * 关掉之后越界的内容会画到画布外面去（光晕 / 投影 / 特效用）。它只标重绘脏
     * ——裁剪是画的时候才生效的事，命令表不用重记。
     */
    void SetClipContent(bool clip);

    /// 现在剪不剪内容。
    bool ClipsContent() const noexcept;

    // ---------------- 查询 ----------------

    /// 指针在这个位置上吗（世界坐标）。按**本体形状**判，内容不参与。
    bool HitTest(float worldX, float worldY) const override;

    /// 命中索引用的保守包络：本体形状的包围盒（画布没有变换，不需要并三态）。
    ShapeBounds GetHitEnvelope() const override;

    /// 本体形状（渲染 / 命中 / 包围盒三者同源）。
    const ShapeSpec& GetShape() const noexcept;

    /// 构造时用的那份配置。
    const CanvasData& GetData() const noexcept;

    // ---------------- 观测口（"功能全对、只是白干活"只有计数抓得住） ----------------

    /// 绘制器总共被调用了几次（脏一次 +1，干净帧不动）。
    std::uint64_t GetPaintCount() const noexcept;

    /// 命令表总共被回放了几次（每渲染帧 +1）。
    std::uint64_t GetReplayCount() const noexcept;

    /// 当前命令表里有几条命令（最近一次录制的结果）。
    std::size_t GetCommandCount() const noexcept;

protected:
    /// 本体 + 内容。内容走"脏了重新记命令、否则回放"那条路。
    void onRender(float pixelX, float pixelY) const override;

private:
    /// 把 data 搬进自己 + 基类；构造函数用（`AGENTS.md` §6 第 83 条）。
    void applyData(const CanvasData& data);

    CanvasData _data{};

    /// 注入的绘制器；空 = 这块布上什么都没有。
    std::shared_ptr<ICanvasPainter> _painter;

    /// 命令表（`mutable`：提交是 const 的，而"录制 / 回放"是缓存的搭建，
    /// 不是可观测状态——和基类那个顶点缓存槽同一个理由）。
    mutable std::unique_ptr<detail::CanvasRecorder> _recorder;

    /// 内容脏：下次重绘要重新调绘制器。
    mutable bool _contentDirty = true;

    /// 观测计数（见上面的说明）。
    mutable std::uint64_t _paintCount = 0;
    mutable std::uint64_t _replayCount = 0;
};

}  // namespace inking
