#pragma once

// 画布的**绘制词汇**（A12）：使用者唯一的作画入口。
//
// 它是"业务代码也能画"这件事的公开面——以前只有框架内部的组件能画
// （`detail::fillShape` 那些是**框架内部件**，见 `InkingDraw.h`）。这份头
// 把它重新包一层：词汇用**框架自己的类型**（形状 / 颜色 / 变换 / 文字请求），
// 所以：
//
//   - **公开头不拖 SDL 进来**（`AGENTS.md` §3 第 2 条）：`SDL_Renderer` 只做
//     前向声明（它本来就是个不透明类型）。要用 SDL 自己画的场合见 `ImmediateDraw`；
//   - 加一种画法**不用改框架**：调用方直接调这里的词汇组合出来；真需要新词汇时
//     是往这里加一个方法（一次），而不是给每个组件各写一遍。
//
// ---------------------------------------------------------------------------
// 坐标系（唯一一件必须记住的事）
//
// 所有坐标都是**画布本地的设计坐标**：`(0,0)` 是画布左上角，单位与
// `CanvasData::width / height` 同一套。画布在屏幕上的位置、祖先容器的子树变换
// （抽屉拉开 / 面板滚动）、当前展示倍率——**都由框架负责**，绘制器一律按本地
// 坐标写，不换算、不乘倍率。
//
// 形状类的词汇（`FillShape` / `StrokeShape`）遵循形状层的老规矩：形状**填满**
// 调用方给的那个矩形（见 `InkingShapeSpec.h` 的坐标语义），所以每个形状都要
// 给 `x / y / w / h`。
//
// ---------------------------------------------------------------------------
// 与"脏驱动"的关系（写绘制器之前先看这条）
//
// 绘制器**不是每帧被调用**：画布脏的时候调一次，把这次调用记成命令表；之后
// 干净帧只是拿命令表回放。所以绘制器里写的应当是"**把整块画布画完**"的纯绘制
// 逻辑，而**不要**在里面做"和上一帧比较""累加状态"这类事——那种逻辑放在外面，
// 改完数据调 `InkingStaticCanvas::Invalidate()`。
//
// 回放是按命令表重跑的，所以每一笔的参数（形状、颜色、位置、变换）都被存下来了；
// `PolygonPath` 那种借顶点表的形状会被**拷贝一份**（所以传临时点列也安全）。

#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTextRequest.h>
#include <dataStruct/InkingTransform.h>

#include <cstdint>
#include <functional>
#include <string>

struct SDL_Renderer;  // 只前向声明：公开头不拖 SDL 进来（AGENTS §3 第 2 条）

namespace inking {

namespace detail {
class CanvasRecorder;
}

/**
 * @brief 一次绘制里拿到的画布句柄：能画的词汇都在这里。
 *
 * 它**不拥有**任何东西，只是"这一笔往哪记"的句柄；由 `InkingStaticCanvas` 在
 * 调绘制器时构造在栈上，生命周期只到那次 `Paint` 返回。别存它。
 */
class CanvasContext {
public:
    CanvasContext(const CanvasContext&) = default;
    CanvasContext& operator=(const CanvasContext&) = default;

    /// 画布宽度（设计坐标）。
    float Width() const noexcept;

    /// 画布高度（设计坐标）。
    float Height() const noexcept;

    // ---------------- 形状（十三种 SDF，见 InkingShapeSpec.h） ----------------

    /**
     * 按形状填一块（带抗锯齿）。
     *
     * @param shape     形状定义；`ShapeSpec::Rect()` 就是矩形。
     * @param x,y,w,h   形状要填满的框，**画布本地设计坐标**（w / h 会取整到设计单位
     *                  ——形状层的尺寸是整数，与组件尺寸同一套约定）。
     * @param color     0xAARRGGBB，alpha 参与合成。
     * @param transform 逐笔变换（平移 / 旋转 / 缩放），**绕这个框的中心**。
     */
    void FillShape(const ShapeSpec& shape, float x, float y, float w, float h,
                   std::uint32_t color,
                   const TransformSpec& transform = TransformSpec{});

    /// 沿形状边界画一圈描边（一半在外、一半在内，见 B3）。`ring` / `arc` 不描边。
    void StrokeShape(const ShapeSpec& shape, float x, float y, float w, float h,
                     float strokeWidth, std::uint32_t color,
                     const TransformSpec& transform = TransformSpec{});

    // ---------------- 便捷封装（常见形状，少写几个字） ----------------

    /// 一个矩形：`FillShape(ShapeSpec::Rect(), ...)`。
    void FillRect(float x, float y, float w, float h, std::uint32_t color,
                  const TransformSpec& transform = TransformSpec{});

    /**
     * 一个正圆。
     *
     * @param centerX,centerY 圆心（本地设计坐标）——**不是**左上角。形状层的
     *        `circle` 是"框里居中、直径取 min(w,h)"，所以这里替调用方把框摆好，
     *        免得每处都自己减半个直径。
     * @param diameter        直径（设计坐标）。
     */
    void FillCircle(float centerX, float centerY, float diameter,
                    std::uint32_t color,
                    const TransformSpec& transform = TransformSpec{});

    /**
     * 任意两点之间的线段。
     *
     * 形状层的 `line` 是**水平**的，所以这里把它摆成"长度 × 线宽"的框再旋转到
     * 目标角度（绕框中心，与 `TransformSpec::rotate` 的语义一致：度、顺时针为正）。
     *
     * @param cap 端点形状：圆头（默认）/ 平头 / 方头（见 `LineCap`）。
     */
    void DrawLine(float x0, float y0, float x1, float y1, float thickness,
                  std::uint32_t color, LineCap cap = LineCap::Round);

    // ---------------- 文本 ----------------

    /**
     * 画一段文字。位置写在 `request.offsetX / offsetY`（本地设计坐标）。
     *
     * 走的是框架那条共享的文字路径（排版缓存 / 字形图集 / 颜色调制），所以画布上
     * 的文字与按钮 / 标签上的文字是同一份实现——不会"两边长得不一样"。
     */
    void DrawText(const TextRequest& request);

    /// 便捷重载：给文字、位置、字号、颜色就够（字体走机器默认）。
    void DrawText(const std::string& text, float x, float y, float fontSize,
                  std::uint32_t color);

    // ---------------- 裁剪（可嵌套） ----------------

    /**
     * 压入一层裁剪：之后的绘制只在这个矩形内可见（本地设计坐标）。
     *
     * 与已有裁剪（画布边界 + 祖先容器）取**交集**，所以"再窄一点的视口"是
     * 叠加语义。必须与 `PopClip()` 配对（画布记着嵌套层数，绘制结束时自动收尾）。
     */
    void PushClip(float x, float y, float w, float h);

    /// 弹出最近一层 `PushClip`。多弹了什么都不做（不报错、不崩）。
    void PopClip();

    // ---------------- 逃生舱（要自己用 SDL 画时） ----------------

    /**
     * @brief 把一段"自己动手画"的代码交给画布，它会**跟着命令表一起回放**。
     *
     * 这是给"词汇表里还没有的东西"留的口子：函数拿到当前渲染器，爱画什么画什么。
     * 选它而不是裸拿渲染器，是因为两件事由框架兜住：
     *   1. **顺序**：执行前会先登记批次边界（`detail::NoteImmediateDraw`），
     *      于是你这一笔不会被后刷出去的几何批盖掉（`AGENTS.md` §6 第 49 条）；
     *   2. **回放**：干净帧重画时这段代码**会**被再执行一次——直接拿 `Renderer()`
     *      自己画就没有这个保证（见那里的说明）。
     *
     * @param draw 收到渲染器后画一笔；渲染器只在本次调用期间有效。
     */
    void ImmediateDraw(std::function<void(SDL_Renderer*)> draw);

    /**
     * 当前渲染器（可能为 nullptr——不在绘制过程中时）。
     *
     * ⚠️ 用它直接画的笔**不会**进命令表：绘制器被调用的那一帧画得出来，
     * 之后干净帧回放时**就没有了**（画面会莫名少一块）。要让一笔跟着回放，
     * 用 `ImmediateDraw`。留这个口子是给"只在脏帧画一次、且清楚后果"的场合
     * （例如自己做了整屏后处理）。
     */
    SDL_Renderer* Renderer() const noexcept;

private:
    friend class InkingStaticCanvas;  // 只有画布能造它（构造是 private）

    explicit CanvasContext(detail::CanvasRecorder& recorder) noexcept;

    detail::CanvasRecorder* _recorder = nullptr;  ///< 非拥有；生命周期 = 一次 Paint
};

}  // namespace inking
