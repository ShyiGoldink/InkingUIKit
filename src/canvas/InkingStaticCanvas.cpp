#include <canvas/InkingStaticCanvas.h>

// `detail::AncestorClipInDevice` / `ClipBoundsInDevice` / `IntersectBounds` 在源项目里
// 住在**那个单体头** `include/ink/basic/InkingAnchor.h` 里，本文件是经它（或经
// `InkingDraw.h` / `InkingHit.h`）**传递**拿到的。拆层之后它们落到
// `detail/InkingGeometry.h`，而上面那几个头都不再传递它 ⇒ 必须显式包含。
// **拆单体头时现形的就是这一类依赖。**
#include <detail/InkingGeometry.h>
#include <canvas/CanvasContext.h>
#include <render/InkingBatch.h>
#include <render/InkingDraw.h>
#include <detail/InkingHit.h>
#include <dataStruct/InkingColor.h>

#include "CanvasRecorder.h"

#include <utility>

namespace inking {
namespace {

/// 把 lambda 包成 `ICanvasPainter`（`SetPainter(CanvasPaintFn)` 用）。
///
/// 为什么不把 lambda 直接存成 `std::function` 成员、非要在 `onRender` 里分叉：
/// 那样"接口"与"回调"就成了两条绘制路径，迟早分叉（一个支持状态、一个不支持）。
/// 包一层之后，画布只认识 `ICanvasPainter` 一种东西。
class FnPainter final : public ICanvasPainter {
public:
    explicit FnPainter(CanvasPaintFn paint) : _paint(std::move(paint)) {}

    void Paint(CanvasContext& canvas) override {
        if (_paint) {
            _paint(canvas);
        }
    }

private:
    CanvasPaintFn _paint;
};

}  // namespace

InkingStaticCanvas::InkingStaticCanvas(InkingWidget* parent,
                                       const CanvasData& data)
    : InkingStaticWidget(parent, data.name) {
    applyData(data);
}

InkingStaticCanvas::~InkingStaticCanvas() = default;

void InkingStaticCanvas::applyData(const CanvasData& data) {
    _data = data;

    // -----------------------------------------------------------------------
    // 几何：构造期一次性写进基类
    //
    // 基类**不存整份 `CanvasData`**，所以每个字段都要在这里显式搬一次——漏一处
    // 的症状是"配置里写了、运行期就是没生效"，而且不报错（`AGENTS.md` §6 第 83 条）。
    // 静态组件的几何**构造即定型**：这里写的是"构造期一次性写入"，不是运行期
    // 几何变化（那条保证由 src/inking.cpp 的编译期断言钉着）。
    // -----------------------------------------------------------------------
    _width = _data.width > 0 ? _data.width : 0;
    _height = _data.height > 0 ? _data.height : 0;
    _selfAnchor = _data.selfAnchor;
    _traceAnchor = _data.traceAnchor;
    _offsetX = _data.offsetX;
    _offsetY = _data.offsetY;
    _layout = _data.layout;
    _layoutItem = _data.layoutItem;

    // 可见性与层级走现成的写入口：它们自己会夹取、自己判"真的变了没有"。
    SetVisible(_data.visible);
    ChangeZIndex(_data.zIndex);

    ClearDirty();
    // 几何是构造期写入的，场景列表里的坐标快照要重算一次。
    NotifySceneStructureChanged();
}

// ---------------------------------------------------------------------------
// 绘制器
// ---------------------------------------------------------------------------

void InkingStaticCanvas::SetPainter(std::shared_ptr<ICanvasPainter> painter) {
    _painter = std::move(painter);
    Invalidate();
}

void InkingStaticCanvas::SetPainter(CanvasPaintFn paint) {
    if (!paint) {
        ClearPainter();
        return;
    }
    SetPainter(std::make_shared<FnPainter>(std::move(paint)));
}

void InkingStaticCanvas::ClearPainter() {
    _painter.reset();
    Invalidate();
}

bool InkingStaticCanvas::HasPainter() const noexcept { return _painter != nullptr; }

// ---------------------------------------------------------------------------
// 内容脏 / 裁剪开关
// ---------------------------------------------------------------------------

void InkingStaticCanvas::Invalidate() {
    _contentDirty = true;
    // 画面会变，所以标重绘脏；但几何没动，**不标命中脏 / 结构脏**——
    // 命中表一条都不用改，这正是画布能待在静态档的原因。
    MarkRepaintDirty();
}

bool InkingStaticCanvas::IsContentDirty() const noexcept { return _contentDirty; }

void InkingStaticCanvas::SetClipContent(bool clip) {
    if (_data.clipContent == clip) {
        return;  // 幂等：没变就不白标一次脏
    }
    _data.clipContent = clip;
    // 裁剪是"画的时候"才生效的事，命令表不用重记。
    MarkRepaintDirty();
}

bool InkingStaticCanvas::ClipsContent() const noexcept {
    return _data.clipContent;
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

bool InkingStaticCanvas::HitTest(float worldX, float worldY) const {
    // 世界坐标 → 本地 → 形状判定，三步和绘制共用同一套定义。
    // 画布没有自己的变换（A12 这一版不做旋转的视口），所以传单位变换。
    return detail::HitTestShape(*this, _data.shape, TransformSpec{}, worldX,
                                worldY);
}

ShapeBounds InkingStaticCanvas::GetHitEnvelope() const {
    // 画布没有变换，包络就是本体形状的包围盒（`circle` 之类会比整框小）。
    return GetShapeBounds(_data.shape, GetWidth(), GetHeight());
}

const ShapeSpec& InkingStaticCanvas::GetShape() const noexcept {
    return _data.shape;
}

const CanvasData& InkingStaticCanvas::GetData() const noexcept { return _data; }

std::uint64_t InkingStaticCanvas::GetPaintCount() const noexcept {
    return _paintCount;
}

std::uint64_t InkingStaticCanvas::GetReplayCount() const noexcept {
    return _replayCount;
}

std::size_t InkingStaticCanvas::GetCommandCount() const noexcept {
    return _recorder != nullptr ? _recorder->Count() : 0u;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

void InkingStaticCanvas::onRender(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }

    const float magnification =
        GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
    const int nodeWidth = GetWidth();
    const int nodeHeight = GetHeight();

    // ---- ① 本体：底色 → 描边（顺序与 B3 一致；两者都不参与命中）----
    // 底色的 alpha 为 0 就是"不画"：透明画布是合法且常见的形态，**故意不做**
    // 按钮那种"全透明兜底成黑"（那会把一块透明层变成黑方块）。
    if (ColorAlpha(_data.color) > 0u) {
        detail::fillShape(renderer, _data.shape, nodeWidth, nodeHeight,
                          magnification, pixelX, pixelY, TransformSpec{},
                          _data.color);
    }
    if (_data.stroke.Visible()) {
        detail::strokeShape(renderer, _data.shape, nodeWidth, nodeHeight,
                            magnification, pixelX, pixelY, TransformSpec{},
                            _data.stroke.width, _data.stroke.color);
    }

    if (_recorder == nullptr) {
        _recorder = std::make_unique<detail::CanvasRecorder>();
    }

    // ---- ② 内容：脏了才重新录一遍（这一帧调绘制器）----
    //
    // 干净帧**不调绘制器**，直接用上次那串命令——这就是"数据变了才重画"的落点。
    if (_contentDirty) {
        _contentDirty = false;
        _recorder->BeginRecord(nodeWidth, nodeHeight);
        _recorder->Clear();
        ++_paintCount;
        if (_painter != nullptr) {
            // `CanvasContext` 只**记录**，不画：绘制与回放必须只有一份实现
            // （见 `CanvasRecorder.h` 文件头的取舍 1）。
            CanvasContext canvas(*_recorder);
            _painter->Paint(canvas);
        }
    }

    if (_recorder->Count() == 0) {
        return;  // 空画布：连裁剪都不用折腾
    }

    // ---- ③ 内容裁剪：画布边界 ∩ 祖先裁剪 ----
    //
    // 场景在提交本节点之前已经按"祖先裁剪"设过 SDL 的裁剪矩形（A5），这里要把
    // 内容再收进画布边界，然后**恢复**成祖先那一份——否则场景的裁剪记账
    // （`clipActive` / `activeClip`）就和 SDL 的真实状态不一致，后面那些节点的
    // 裁剪会莫名其妙地不对。
    ShapeBounds ancestorClip{};
    const bool hasAncestorClip =
        detail::AncestorClipInDevice(*this, ancestorClip);

    ShapeBounds baseClip = ancestorClip;
    bool baseClipValid = hasAncestorClip;

    if (_data.clipContent) {
        const ShapeBounds own = detail::ClipBoundsInDevice(
            *this, ShapeBounds{0.0f, 0.0f, static_cast<float>(nodeWidth),
                               static_cast<float>(nodeHeight)});
        baseClip = hasAncestorClip
                       ? detail::IntersectBounds(ancestorClip, own)
                       : own;
        baseClipValid = true;
        detail::SetClipRect(renderer, baseClip);
    }

    // 内容是多笔不同形状，而**一个节点只有一个顶点缓存槽**：共用它只会互相顶掉
    // （每笔都重算，还白写一份缓存）。明确不进缓存——省得"看起来有缓存、
    // 其实全是 miss"这种事需要靠计数才发现。
    detail::SetCurrentNode(nullptr);

    ++_replayCount;
    _recorder->Replay(renderer, magnification, pixelX, pixelY, baseClip,
                      baseClipValid);

    // 把裁剪恢复成"场景以为的那一份"。
    if (_data.clipContent) {
        if (hasAncestorClip) {
            detail::SetClipRect(renderer, ancestorClip);
        } else {
            detail::ClearClipRect(renderer);
        }
    }
}

}  // namespace inking
