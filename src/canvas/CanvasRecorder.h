#pragma once

// Canvas 的**命令表 + 回放器**（A12 的内部件）。
//
// 为什么要有它：`InkingWidget::SubmitToRenderer` 是**每帧无条件**调 `onRender`
// 的（`IsRepaintDirty()` 那边只管统计"还有几个节点在动"，不是跳过绘制的条件）。
// 而 A12 要的语义是"**数据变了才重画**"——所以"重画"这件事落在这一层：
//
//   脏帧：调绘制器 → 把这次调用记成一串命令 → 回放
//   干净帧：不调绘制器 → 直接回放上次那串命令
//
// 于是绘制器里那些比"重画一遍"更贵的逻辑（布局计算 / 采样 / 排序）只在脏帧跑
// 一次，而画面每帧照常提交（框架所有组件都是这样：提交每帧都做，省的是上游）。
//
// 两条实现上的取舍：
//   1. **录制与回放是两条路，但只有一份"画法"**：录制只存参数，回放才真的调
//      `detail::fillShape` 那一套。绝不让"录制时顺手画一笔"——那样脏帧与干净帧
//      就成了两份实现，迟早分叉（`AGENTS.md` 里"抄第二遍就会分叉"那一族）；
//   2. **借视图的数据要拷贝**：`ShapeSpec::PolygonPath` 借的是调用方的顶点表，
//      而回放发生在**以后**（下次重绘）——那时调用方的临时缓冲多半已经没了。
//      所以录制时把点列拷进命令里（绘制器传临时点列也安全）。
//
// 含 SDL，所以是内部头（不进公开的 `include/canvas/*.h`）。

#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTextRequest.h>
#include <dataStruct/InkingTransform.h>

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace inking::detail {

/// 一条命令是哪种。
enum class CanvasCommandKind : std::uint8_t {
    FillShape = 0,
    StrokeShape,
    DrawText,
    PushClip,
    PopClip,
    Immediate,
};

/**
 * @brief 一条绘制命令。
 *
 * 用一个"大结构 + kind"而不是 `std::variant`：命令不追求内存紧凑，而
 * `variant` 会让返回类型与访问代码都要写 `std::get` 那一套；这里字段少、
 * 语义直白，反而更不容易写错。
 */
struct CanvasCommand {
    CanvasCommandKind kind = CanvasCommandKind::FillShape;

    // ---- 形状类（FillShape / StrokeShape）----
    ShapeSpec shape{};
    /// `PolygonPath` 的点列（**拷贝**一份，见文件头的取舍 2）。
    /// 回放时把 `shape.pathPoints` 指到这里，所以它必须活得比命令表短。
    std::vector<float> pathPoints{};
    float x = 0.0f;  ///< 形状要填满的框（画布本地设计坐标）
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    TransformSpec transform{};
    std::uint32_t color = 0u;
    float strokeWidth = 0.0f;

    // ---- 文本 ----
    TextRequest text{};

    // ---- 裁剪（PushClip）----
    float clipX = 0.0f;
    float clipY = 0.0f;
    float clipW = 0.0f;
    float clipH = 0.0f;

    // ---- 逃生舱 ----
    std::function<void(SDL_Renderer*)> immediate{};
};

/**
 * @brief 一次绘制的录制器：先存命令，再按命令回放。
 *
 * 全程在 UI 线程（绘制路径本来就不加锁，`SKILL.md` §8）。
 */
class CanvasRecorder {
public:
    /// 开始录制前告诉它画布多大（`CanvasContext::Width/Height` 要用）。
    void BeginRecord(int width, int height) noexcept;

    /// 清空命令表（脏帧重新录制之前调）。
    void Clear() noexcept;

    /// 现在有几条命令。
    std::size_t Count() const noexcept { return _commands.size(); }

    int NodeWidth() const noexcept { return _nodeWidth; }
    int NodeHeight() const noexcept { return _nodeHeight; }

    // ---- 录制（由 `CanvasContext` 调；它只记，不画）----
    void AddFillShape(const ShapeSpec& shape, float x, float y, float w, float h,
                      std::uint32_t color, const TransformSpec& transform);
    void AddStrokeShape(const ShapeSpec& shape, float x, float y, float w, float h,
                        float strokeWidth, std::uint32_t color,
                        const TransformSpec& transform);
    void AddDrawText(const TextRequest& request);
    void AddPushClip(float x, float y, float w, float h);
    void AddPopClip();
    void AddImmediate(std::function<void(SDL_Renderer*)> draw);

    /**
     * @brief 按记录顺序把命令表画一遍。
     *
     * @param renderer      渲染器。
     * @param magnification 展示倍率（设计单位 → 设备像素）。
     * @param pixelX,pixelY 画布左上角，**设备像素**（与 `onRender` 拿到的那两个同源）。
     * @param baseClip      裁剪栈的**基底**（**设备像素世界坐标**）。它是回放开始前
     *                      就已经生效的那一层裁剪：画布自己的内容边界，或者
     *                      （关掉内容裁剪时）祖先容器那一份。
     * @param baseClipValid `baseClip` 是否真的生效。**false 表示"现在没有裁剪"**，
     *                      于是 `PopClip` 弹到栈底时要 `ClearClipRect` 而不是设一个
     *                      假的巨大矩形——"没有裁剪"和"裁剪到无限大"在这里不能混
     *                      （混了就会让后来者继承一个奇怪的 clip 状态）。
     */
    void Replay(SDL_Renderer* renderer, float magnification, float pixelX,
                float pixelY, const ShapeBounds& baseClip,
                bool baseClipValid) const;

private:
    std::vector<CanvasCommand> _commands;
    int _nodeWidth = 0;
    int _nodeHeight = 0;
};

}  // namespace inking::detail
