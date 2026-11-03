#pragma once

// 预制件的**部件几何**：把"一条轨道 + 一个比例"算成"那一块已填充的矩形"。
//
// 为什么单拎一个头（`AGENTS.md` §6 第 94 条：同一件事写两遍就会分家）：
// `Slider` 与 `ProgressBar` 的差别只在"比例从哪来"——一个是值，一个是进度；
// 而"比例怎么变成矩形"这件事必须**同一份实现**。抄两份的后果是可见的：
// 调试时滑块填充到 37% 是 74.0 像素、进度条是 73.6 像素（一个取整一个不取整），
// 两条单独看都对，并排放在一张面板上就能看出差一像素。
//
// 本图头只放**纯函数**：不碰节点、不碰 SDL、不标脏。绘制与命中读的是同一个
// 结果（`GetParts()`），这样"画在哪"和"点在哪"不会分家。
//
// 另有两个**按部件矩形画一笔**的入口（`FillPart` / `StrokePart`）：它们只是
// `inking_detail::fillShape` / `strokeShape` 的一层"设计坐标 → 设备像素"换算。
// 单拎出来的原因是**三处要用**（Slider / ProgressBar / ToggleLike）——抄第三遍
// 的时候就是该抽的时候，而两份各自演化的换算迟早会差半个像素。

#include <dataStruct/InkingDecoration.h>
#include <dataStruct/InkingShapeSpec.h>

#include <cstdint>

// 只前向声明：本头**不拖 SDL 进来**（公开头的纪律，同 `button/ButtonPaint.h`）。
struct SDL_Renderer;

namespace inking_prefab {

/**
 * @brief 「轨道 + 比例」→ 「已填充段」的矩形。**Slider 与 ProgressBar 的唯一一份**。
 *
 * 三条语义（写在这里，免得两个调用方各自解释）：
 *   - `ratio` 先夹到 `[0, 1]`；`<= 0` 或 `NaN` 时返回**零宽零高**——调用方据此
 *     **不画这一笔**。0 宽的三角化没有意义，还会多一条提交记录；
 *   - 填充段从轨道的**左端**向右长，纵向与轨道对齐（同 `y`、同 `height`）；
 *   - 不扣圆角：形状层用 `capsule` 时宽度不足两个半径会自然收口成半圆
 *     （`InkingShapeKind::Capsule` 的半径取短边一半），不需要这里再算一次。
 *
 * @param track 轨道矩形（**相对组件左上角**的设计坐标）
 * @param ratio 已填充比例，0~1（越界会被夹）
 */
inline InkingShapeBounds FillBoundsInTrack(const InkingShapeBounds& track,
                                           float ratio) noexcept {
    InkingShapeBounds fill = track;
    // `!(x > 0)` 同时挡住 0、负数和 NaN——写成 `x <= 0` 的话 NaN 会漏过去，
    // 于是填充块宽度变成 NaN，`fillShape` 那条路会提交一堆退化顶点。
    if (!(ratio > 0.0f) || !(track.width > 0.0f)) {
        fill.width = 0.0f;
        fill.height = 0.0f;
        return fill;
    }
    fill.width = track.width * (ratio >= 1.0f ? 1.0f : ratio);
    return fill;
}

/**
 * @brief 按部件矩形**填一笔**（唯一的那一条"画部件"的路）。
 *
 * `box` 是**相对组件左上角**的设计坐标；`pixelX` / `pixelY` 是组件的设备像素
 * 左上角。变换固定传**单位变换**：`fillShape` 的变换绕**传入矩形的中心**转，
 * 逐部件传组件的显示变换会让每个部件绕自己转（`InkingTextInput` 的选区 / 光标
 * 也是这么画的，同一先例）。要画的东西本身带变换时，调用方自己拼。
 *
 * `color == 0`、矩形零宽零高、渲染器为空——三种情况都是**空操作**（不画、
 * 不报错）：0 宽的三角化没有意义。
 */
void FillPart(SDL_Renderer* renderer, const InkingShapeSpec& shape,
              const InkingShapeBounds& box, float magnification, float pixelX,
              float pixelY, std::uint32_t color);

/// 描边同上（宽度 0 / 全透明也算空操作，见 `InkingStrokeSpec::Visible`）。
void StrokePart(SDL_Renderer* renderer, const InkingShapeSpec& shape,
                const InkingShapeBounds& box, float magnification, float pixelX,
                float pixelY, const InkingStrokeSpec& stroke);

}  // namespace inking_prefab
