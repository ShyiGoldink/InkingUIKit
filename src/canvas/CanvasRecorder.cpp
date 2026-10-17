#include "CanvasRecorder.h"

// `detail::AncestorClipInDevice` / `ClipBoundsInDevice` / `IntersectBounds` 在源项目里
// 住在**那个单体头** `include/ink/basic/InkingAnchor.h` 里，本文件是经它（或经
// `InkingDraw.h` / `InkingHit.h`）**传递**拿到的。拆层之后它们落到
// `detail/InkingGeometry.h`，而上面那几个头都不再传递它 ⇒ 必须显式包含。
// **拆单体头时现形的就是这一类依赖。**
#include <detail/InkingGeometry.h>
#include <render/InkingBatch.h>
#include <render/InkingDraw.h>
#include <render/InkingText.h>
#include <dataStruct/InkingAffine.h>

#include <cmath>
#include <utility>

namespace inking::detail {
namespace {

/**
 * 本地设计坐标的矩形 → 设备像素世界坐标的 AABB。
 *
 * 与 `ApplyAncestorAffineToVertices`（提交层搬顶点的那一段）**同构**：先加画布
 * 的绝对左上角、再过祖先子树变换。两份各写一遍就会出现"内容跟着容器转、
 * 裁剪框留在原地"——那正是 B12 踩过的那个坑（`AGENTS.md` §6 第 94 条）的同族。
 */
ShapeBounds LocalRectToDevice(float lx, float ly, float lw, float lh,
                              float magnification, float pixelX,
                              float pixelY) noexcept {
    const ShapeBounds rect{pixelX + lx * magnification,
                           pixelY + ly * magnification, lw * magnification,
                           lh * magnification};
    const Affine ancestor = currentAncestorAffine();
    const Point2 corners[4] = {
        ApplyAffine(ancestor, rect.x, rect.y),
        ApplyAffine(ancestor, rect.x + rect.width, rect.y),
        ApplyAffine(ancestor, rect.x, rect.y + rect.height),
        ApplyAffine(ancestor, rect.x + rect.width, rect.y + rect.height)};

    float minX = corners[0].x;
    float minY = corners[0].y;
    float maxX = corners[0].x;
    float maxY = corners[0].y;
    for (int i = 1; i < 4; ++i) {
        minX = corners[i].x < minX ? corners[i].x : minX;
        minY = corners[i].y < minY ? corners[i].y : minY;
        maxX = corners[i].x > maxX ? corners[i].x : maxX;
        maxY = corners[i].y > maxY ? corners[i].y : maxY;
    }
    return ShapeBounds{minX, minY, maxX - minX, maxY - minY};
}

/**
 * 回放用的形状：`PolygonPath` 借的是顶点表（`ShapeSpec` 的既有约定），
 * 录制时已经拷进命令里，这里把视图指回去。
 */
ShapeSpec ShapeWithPoints(const CanvasCommand& command) {
    ShapeSpec shape = command.shape;
    if (shape.kind == ShapeKind::PolygonPath && command.pathPoints.size() >= 6) {
        shape.pathPoints = command.pathPoints.data();
        shape.pathPointCount =
            static_cast<int>(command.pathPoints.size() / 2);
    }
    return shape;
}

/// 形状层的尺寸是整数（和组件尺寸同一套约定），这里取整。
int RoundSize(float value) noexcept {
    return static_cast<int>(std::lround(value));
}

/// 录制形状命令的公共部分（两处填一样的字段，抽一次省得漏）。
void FillShapeCommand(CanvasCommand& command, const ShapeSpec& shape, float x,
                      float y, float w, float h, std::uint32_t color,
                      const TransformSpec& transform) {
    command.shape = shape;
    if (shape.kind == ShapeKind::PolygonPath && shape.pathPoints != nullptr
        && shape.pathPointCount >= 3) {
        // 拷一份：回放可能发生在很久以后，调用方那块缓冲多半已经没了。
        command.pathPoints.assign(shape.pathPoints,
                                  shape.pathPoints + shape.pathPointCount * 2);
    }
    command.x = x;
    command.y = y;
    command.w = w;
    command.h = h;
    command.color = color;
    command.transform = transform;
}

}  // namespace

void CanvasRecorder::BeginRecord(int width, int height) noexcept {
    _nodeWidth = width;
    _nodeHeight = height;
}

void CanvasRecorder::Clear() noexcept { _commands.clear(); }

void CanvasRecorder::AddFillShape(const ShapeSpec& shape, float x, float y,
                                  float w, float h, std::uint32_t color,
                                  const TransformSpec& transform) {
    CanvasCommand command;
    command.kind = CanvasCommandKind::FillShape;
    FillShapeCommand(command, shape, x, y, w, h, color, transform);
    _commands.push_back(std::move(command));
}

void CanvasRecorder::AddStrokeShape(const ShapeSpec& shape, float x, float y,
                                    float w, float h, float strokeWidth,
                                    std::uint32_t color,
                                    const TransformSpec& transform) {
    CanvasCommand command;
    command.kind = CanvasCommandKind::StrokeShape;
    FillShapeCommand(command, shape, x, y, w, h, color, transform);
    command.strokeWidth = strokeWidth;
    _commands.push_back(std::move(command));
}

void CanvasRecorder::AddDrawText(const TextRequest& request) {
    CanvasCommand command;
    command.kind = CanvasCommandKind::DrawText;
    command.text = request;
    _commands.push_back(std::move(command));
}

void CanvasRecorder::AddPushClip(float x, float y, float w, float h) {
    CanvasCommand command;
    command.kind = CanvasCommandKind::PushClip;
    command.clipX = x;
    command.clipY = y;
    command.clipW = w;
    command.clipH = h;
    _commands.push_back(std::move(command));
}

void CanvasRecorder::AddPopClip() {
    CanvasCommand command;
    command.kind = CanvasCommandKind::PopClip;
    _commands.push_back(std::move(command));
}

void CanvasRecorder::AddImmediate(std::function<void(SDL_Renderer*)> draw) {
    if (!draw) {
        return;
    }
    CanvasCommand command;
    command.kind = CanvasCommandKind::Immediate;
    command.immediate = std::move(draw);
    _commands.push_back(std::move(command));
}

void CanvasRecorder::Replay(SDL_Renderer* renderer, float magnification,
                            float pixelX, float pixelY,
                            const ShapeBounds& baseClip,
                            bool baseClipValid) const {
    if (renderer == nullptr) {
        return;
    }
    const float mag = magnification > 0.0f ? magnification : 1.0f;

    // 裁剪栈：**空栈 = "现在没有裁剪"**（不是"裁剪到无限大"）。栈非空时栈顶就是
    // 当前生效的那一份，于是 PushClip 是"与栈顶求交"、PopClip 是"退回上一层"。
    std::vector<ShapeBounds> clipStack;
    if (baseClipValid) {
        clipStack.push_back(baseClip);
    }

    for (const CanvasCommand& command : _commands) {
        switch (command.kind) {
            case CanvasCommandKind::FillShape: {
                const ShapeSpec shape = ShapeWithPoints(command);
                fillShape(renderer, shape, RoundSize(command.w),
                          RoundSize(command.h), mag, pixelX + command.x * mag,
                          pixelY + command.y * mag, command.transform,
                          command.color);
                break;
            }
            case CanvasCommandKind::StrokeShape: {
                const ShapeSpec shape = ShapeWithPoints(command);
                strokeShape(renderer, shape, RoundSize(command.w),
                            RoundSize(command.h), mag,
                            pixelX + command.x * mag,
                            pixelY + command.y * mag, command.transform,
                            command.strokeWidth, command.color);
                break;
            }
            case CanvasCommandKind::DrawText: {
                if (!command.text.Empty()) {
                    // 变换给单位：文字的朝向已经由"画布自己的变换"体现
                    // （那条通道在 `SubmitToRenderer` 里统一作用）。
                    PaintText(renderer, command.text, _nodeWidth, _nodeHeight,
                              mag, pixelX, pixelY, TransformSpec{});
                }
                break;
            }
            case CanvasCommandKind::PushClip: {
                const ShapeBounds own =
                    LocalRectToDevice(command.clipX, command.clipY,
                                      command.clipW, command.clipH, mag, pixelX,
                                      pixelY);
                // 与已有裁剪取交集（**叠加**语义，不是替换）：画布边界与祖先容器
                // 都还在管着这一笔。
                const ShapeBounds merged =
                    clipStack.empty() ? own
                                      : IntersectBounds(clipStack.back(), own);
                clipStack.push_back(merged);
                SetClipRect(renderer, merged);
                break;
            }
            case CanvasCommandKind::PopClip: {
                if (clipStack.empty()) {
                    break;  // 多弹了：什么都不做（不报错、不崩）
                }
                clipStack.pop_back();
                if (clipStack.empty()) {
                    // 退回"没有裁剪"那一档：基底有效就恢复基底，否则真的清掉。
                    if (baseClipValid) {
                        SetClipRect(renderer, baseClip);
                    } else {
                        ClearClipRect(renderer);
                    }
                } else {
                    SetClipRect(renderer, clipStack.back());
                }
                break;
            }
            case CanvasCommandKind::Immediate: {
                // 顺序纪律：绕开批次的直接绘制必须先登记边界（AGENTS §6 第 49 条）。
                NoteImmediateDraw();
                if (command.immediate) {
                    command.immediate(renderer);
                }
                break;
            }
        }
    }

    // 收尾：绘制器漏了 PopClip 也不能把裁剪留在半路（恢复成栈底）。
    if (!clipStack.empty()) {
        SetClipRect(renderer, clipStack.front());
    }
}

}  // namespace inking::detail
