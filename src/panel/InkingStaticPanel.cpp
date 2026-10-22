#include <render/InkingDraw.h>
#include <button/ButtonPaint.h>
#include <panel/InkingStaticPanel.h>
#include <scene/InkingScene.h>

#include "PanelPaint.h"

#include <detail/InkLog.h>
#include <detail/InkingHit.h>

#include <string>

namespace inking {

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

InkingStaticPanel::InkingStaticPanel(InkingWidget* parent, const PanelData& data)
    : InkingStaticWidget(parent, data.name) {
    applyData(data);
}

void InkingStaticPanel::applyData(const PanelData& data) {
    // 拷一份再收尾：调用方给的 PanelData 可能是"还没补齐 open 态"的，
    // 而本类要求 _data 永远处于补齐状态（渲染与命中都按它来）。
    _data = data;
    _data.Normalize();

    // 两态外观展开存好、状态机与过渡一起复位。`startOpen` 在这里生效——
    // 构造时就直接是目标态，不会先按关闭态起一段过渡再打断。
    _look.Reset(_data);

    syncBaseColor();

    // -----------------------------------------------------------------------
    // 几何：构造期一次性写入基类的受保护成员
    //
    // 和静态按钮走的是同一条路，理由也一样（`InkingStaticButton.cpp` 里那段
    // 注释讲得细）：静态组件的几何构造即定型，所以**没有**写入口；
    // 但"数据驱动的构造"里基类只拿到名字，尺寸与锚点是这之后才从 data 取到的，
    // 不给它写进去，面板就是个 0×0——画不出来也点不到。
    // -----------------------------------------------------------------------
    _width = _data.width > 0 ? _data.width : 0;
    _height = _data.height > 0 ? _data.height : 0;
    _selfAnchor = _data.selfAnchor;
    _traceAnchor = _data.traceAnchor;
    _offsetX = _data.offsetX;
    _offsetY = _data.offsetY;
    // 布局数据（A6）同一条路：基类不存整份 XxxData，新增字段要显式搬一次
    // （AGENTS §6 第 83 条）。面板是最需要布局的那个组件——它就是容器。
    _layout = _data.layout;
    _layoutItem = _data.layoutItem;

    // 可见性与层级走现成的写入口：它们自己会夹取、自己判"真的变了没有"。
    SetVisible(_data.visible);
    ChangeZIndex(_data.zIndex);

    // 几何定下来了 → 这一份"待消费的改动"就没了。之后开合只改变换，
    // 而变换按设计**不碰命中表**（`InputDesign.md` §11）。
    ClearDirty();

    // 尺寸护栏（T-02）：判据与文案统一在 Widget 层。放在几何写完之后调用，
    // 所以报出来的是**真的写进基类的那一份**（而不是 `_data` 里的原始值）。
    WarnIfZeroSize(kPanelModuleName);

    // 几何是构造期写入的，场景绘制列表里的绝对坐标快照要重算一次。
    NotifySceneStructureChanged();
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

bool InkingStaticPanel::IsOpen() const noexcept {
    return _look.IsOpen();
}

PanelState InkingStaticPanel::GetState() const noexcept {
    return _look.state;
}

std::uint32_t InkingStaticPanel::GetDisplayColor() const noexcept {
    // 过渡进行中它是插值出来的中间色；没有过渡时等于当前状态那份配置的颜色。
    return _look.displayColor;
}

const TransformSpec& InkingStaticPanel::GetDisplayTransform() const noexcept {
    return _look.displayTransform;
}

const ShapeSpec& InkingStaticPanel::GetShape() const noexcept {
    return _data.shape;
}

bool InkingStaticPanel::IsTransitioning() const noexcept {
    return _look.IsTransitioning();
}

float InkingStaticPanel::TransitionProgress() const noexcept {
    return _look.TransitionProgress();
}

const PanelData& InkingStaticPanel::GetData() const noexcept {
    return _data;
}

TransformSpec InkingStaticPanel::GetSubtreeTransform() const noexcept {
    // 容器与子树的唯一连接点：返回当前显示变换，提交层会把它复合进整棵子树
    // 的每一笔绘制（形状 / 文字 / 图片都吃这一份）。子节点一个字都不用改。
    TransformSpec transform = _look.displayTransform;
    if (_data.scroll.enabled) {
        // B12：滚动就是**在开合动画的变换上再平移一点**——两者互不干扰
        // （抽屉该拉还拉，滚动该滚还滚），也不用给子树变换加第二条通道。
        transform.translateX -= _scrollX;
        transform.translateY -= _scrollY;
    }
    return transform;
}

// ---------------------------------------------------------------------------
// 命中：容器自己递归解答（子孙不进全局命中表）
// ---------------------------------------------------------------------------

bool InkingStaticPanel::HitTest(float worldX, float worldY) const {
    // 整棵子树都不可见时不必问下去（`HitTestShape` 内部也会查，这里提前短路）。
    if (!IsVisibleInTree()) {
        return false;
    }

    // ① 自己本体：撤祖先变换 → 减自己的绝对位置 → 过自己变换的逆 → 问形状。
    //    和绘制共用同一份定义（`detail::HitTestShape`）。
    //    自己那一笔**不受自己的裁剪影响**（裁剪只剪子孙）——和绘制同一条规矩。
    if (detail::HitTestShape(*this, GetShape(), _look.displayTransform, worldX,
                             worldY)) {
        return true;
    }

    // ①.5 裁剪（A5）：点落在裁剪区之外 → **整棵子树都不命中**。
    //      这就是"画出来被剪、点起来也被剪"里的后一半，和绘制用的是同一个矩形
    //      （绘制侧由 `detail::AncestorClipInDevice` 算，命中侧在这里判）。
    if (HasSubtreeClip()
        && !detail::HitTestLocalRect(*this, GetSubtreeClipBounds(),
                                     _look.displayTransform, worldX, worldY)) {
        return false;
    }

    // ② 子树：**世界坐标原样**交给每个子节点。子节点的 `AncestorAffine` 里
    //    已经含了我这一层变换，它自己会撤干净；反过来（先换算到我的本地空间
    //    再递归）会让子节点多撤一次我的变换，"画的和点的"就差一个变换——
    //    而两边单独看都对（`AGENTS.md` §6 第 38 条那一类）。
    for (const InkingWidget* child : GetChildren()) {
        if (child != nullptr && child->HitTest(worldX, worldY)) {
            return true;
        }
    }
    return false;
}

InkingWidget* InkingStaticPanel::HitTestNode(float worldX,
                                            float worldY) noexcept {
    // 整棵子树都不可见时不必问下去（和 `HitTest` 同一条短路）。
    if (!IsVisibleInTree()) {
        return nullptr;
    }

    // 裁剪（A5）：点落在裁剪区之外 → **整棵子树都不命中**。它只剪子孙，
    // 所以下面自己本体那一笔不受它影响——和绘制同一条规矩。
    const bool insideClip =
        !HasSubtreeClip()
        || detail::HitTestLocalRect(*this, GetSubtreeClipBounds(),
                                    _look.displayTransform, worldX, worldY);

    // ① 子树优先：子节点画在容器之上，所以它们先答。
    //
    //    按 `IsAbove` 取**最上面那个 Block**（规则只有一份：树序 + 兄弟内 z）。
    //    注意 `hit` 常常是深层后代而不是直接子节点，但 `IsAbove` 比的是
    //    "从根到自己的路径"，深层后代与另一个子树的节点照样可比，而且
    //    "同一个子树里更深的那支在上面"这件事天然成立。
    //    `PassThrough` 的（标签之类）记下来但不算数——按契约返回 nullptr，
    //    让调用方保留"容器自己"那份答案。
    InkingWidget* block = nullptr;
    if (insideClip) {
        for (InkingWidget* child : GetChildren()) {
            if (child == nullptr) {
                continue;
            }
            InkingWidget* hit = child->HitTestNode(worldX, worldY);
            if (hit == nullptr || hit->IsHitPassThrough()) {
                continue;
            }
            if (block == nullptr || InkingWidget::IsAbove(*hit, *block)) {
                block = hit;
            }
        }
    }
    if (block != nullptr) {
        return block;  // 子树里有 Block：它就是这次事件的目标
    }

    // ② 自己本体：和绘制共用同一份形状定义（`detail::HitTestShape`）。
    //    自己是 PassThrough 时也返回 nullptr——那种情况交给调用方按三态处理。
    if (!IsHitPassThrough()
        && detail::HitTestShape(*this, GetShape(), _look.displayTransform,
                                worldX, worldY)) {
        return this;
    }

    // ③ 剩下两种情况都返回 nullptr（契约第 2、3 条）：
    //    "子树里只有 PassThrough 命中" 与 "什么都没命中"。
    return nullptr;
}

ShapeBounds InkingStaticPanel::GetHitEnvelope() const {
    // 包络 = 布局空间里整棵子树的并集 × 两个状态的变换并集。
    // 它**只在建表时算一次**：抽屉来回拉只改变换，这份包络不动，
    // 所以命中表的条目数一条都不变（`InputDesign.md` §11 的变换通道）。
    return detail::PanelHitEnvelope(*this, GetShape(), GetWidth(), GetHeight(),
                                    _look.closed, _look.open);
}

// ---------------------------------------------------------------------------
// 每渲染帧一次：开合过渡（本地几何一动不动）
// ---------------------------------------------------------------------------

void InkingStaticPanel::onAnimationTick(float deltaSeconds) {
    // 没有过渡在跑时 `Advance` 直接返回两个 false（稳态 = 一个分支 + 什么都不做）。
    const PanelLook::AdvanceResult advanced = _look.Advance(deltaSeconds);
    if (!advanced.repaint && !advanced.hit) {
        return;
    }

    if (advanced.repaint) {
        syncBaseColor();
        // 画面变了 → **重绘脏**。别标成命中脏：颜色不改命中，
        // 标错了就会每帧白重建一次绘制列表（`AGENTS.md` §6 第 40 条）。
        MarkRepaintDirty();
    }
    if (advanced.hit) {
        // 变换变了 → **命中脏**：面板搬走之后，同一个点可能落到别的组件上。
        // 但它仍然**不改表**——子树不进表，容器那条包络覆盖所有状态。
        MarkDirty();
    }
}

// ---------------------------------------------------------------------------
// [final] 开合写入口
// ---------------------------------------------------------------------------

bool InkingStaticPanel::SetOpen(bool open) noexcept {
    if (!_look.SetOpen(open)) {
        return false;
    }

    syncBaseColor();

    // 这一次开合让画面立刻变了（起点色 / 起点变换取自当前显示值），
    // 所以两笔账当场就记：**重绘脏**（画面变了）与**命中脏**（位置变了）。
    //
    // 但**不标结构脏**（不调 MarkStructureChanged）：布局一个字节都没动，
    // 子节点的绝对坐标快照仍然有效，命中表也不用重建。这正是"抽屉开合时
    // 命中表条目数不变"的来源。
    //
    // 顺带说明为什么这里不必等 `Advance` 的待报告账：面板的开合是**业务动作**，
    // 主动标一次最直白；`PanelLook` 里那笔账是给"没配过渡、显示值当场被换掉"
    // 的情况兜底的（两条都标同一个 bool，重复标脏是幂等的）。
    MarkRepaintDirty();
    MarkDirty();
    return true;
}

bool InkingStaticPanel::Toggle() noexcept {
    return SetOpen(!IsOpen());
}

void InkingStaticPanel::syncBaseColor() noexcept {
    // 基类的 color 跟着**当前显示**的颜色走（过渡中是插值色），
    // 保持"从基类窗口读到的是同一件事"。
    _color = _look.displayColor;
}

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------

void InkingStaticPanel::onRender(float pixelX, float pixelY) const {
    // 只有自己那一笔。子节点由场景按 z 序逐个提交，各自带上"祖先子树变换"
    // （`SubmitToRenderer` 里算好的仿射槽），所以这里不需要（也不该）递归。
    //
    // T-07：有阴影先画阴影（它在填充**之下**——"形状之下的偏移副本"）。
    // 描边不在这里：它走装饰遍（`onRenderDecoration`），画在子孙之上。
    if (_look.displayShadow.Visible()) {
        detail::PaintSurfaceShadow(detail::currentRenderer(), *this, GetShape(),
                                   _look.displayShadow, _look.displayTransform,
                                   pixelX, pixelY);
    }
    detail::PaintPanelAppearance(*this, GetShape(), _look.displayColor,
                                 _look.displayTransform, pixelX, pixelY);

    // B12：两条滚动条。画在**容器自己的坐标**里（不含滚动偏移——条不该跟着内容
    // 跑），所以用 `_look.displayTransform`，不是 `GetSubtreeTransform()`。
    if (!_data.scroll.enabled) {
        return;
    }
    SDL_Renderer* renderer = detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }
    const float magnitude = GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
    const auto paint = [&](float x, float y, float width, float height,
                           std::uint32_t color) {
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }
        // 圆角：**0 = 胶囊**（短边的一半，默认观感）；> 0 = 指定的半径。
        // 轨道与滑块各自按自己的短边算——不然细轨道会配出一个"圆角比它还大"的怪形状。
        const float radius = _data.scroll.barRadius > 0.0f
                                 ? _data.scroll.barRadius
                                 : std::min(width, height) * 0.5f;
        detail::fillShape(renderer, ShapeSpec::RoundedRect(radius),
                          std::max(1, static_cast<int>(width + 0.5f)),
                          std::max(1, static_cast<int>(height + 0.5f)),
                          magnitude, pixelX + x * magnitude,
                          pixelY + y * magnitude, _look.displayTransform,
                          color);
    };
    for (int axis = 0; axis < 2; ++axis) {
        const ScrollBarMetrics bar = GetBarMetrics(axis == 0);
        if (!bar.visible) {
            continue;
        }
        // B12：先铺**条带区域的背景**（配了色才铺）——不然内容会从轨道与滑块的
        // 缝隙里透出来。它和"滑出去的内容被裁剪"是两件事：裁剪剪的是超出容器
        // 本体的部分，而条本身就浮在内容之上。
        if (_data.scroll.barBackgroundColor != 0u) {
            const float thickness =
                _data.scroll.barInset * 2.0f + _data.scroll.barWidth;
            if (axis == 0) {
                paint(static_cast<float>(GetWidth()) - thickness, 0.0f,
                      thickness, static_cast<float>(GetHeight()),
                      _data.scroll.barBackgroundColor);
            } else {
                paint(0.0f, static_cast<float>(GetHeight()) - thickness,
                      static_cast<float>(GetWidth()), thickness,
                      _data.scroll.barBackgroundColor);
            }
        }
        paint(bar.trackX, bar.trackY, bar.trackWidth, bar.trackHeight,
              _data.scroll.barColor);
        paint(bar.thumbX, bar.thumbY, bar.thumbWidth, bar.thumbHeight,
              _data.scroll.thumbColor);
    }
}

void InkingStaticPanel::onRenderDecoration(float pixelX, float pixelY) const {
    // 只有描边（T-07）：阴影在填充那一趟（它在形状**之下**，挪到这里反而
    // 盖住自己的内容）。场景在"子树画完、上层兄弟未画"时调这里，所以这一笔
    // 压得住子孙、又不越界。
    detail::PaintSurfaceStroke(detail::currentRenderer(), *this, GetShape(),
                               _look.displayStroke, _look.displayTransform,
                               pixelX, pixelY);
}

// ---------------------------------------------------------------------------
// 滚动（B12）
// ---------------------------------------------------------------------------

void InkingStaticPanel::UpdateScrollRange() {
    if (!_data.scroll.enabled) {
        _contentWidth = 0.0f;
        _contentHeight = 0.0f;
        return;
    }
    // 内容尺寸 = 子节点矩形的并集（**相对容器左上角**）。
    // 只算子节点本体：描边 / 阴影 / 文字溢出都不算——否则"内容多大"会随
    // 观感细节漂移，而滚动条该反映的是"看得见的那块有多大"。
    const float originX = static_cast<float>(GetX());
    const float originY = static_cast<float>(GetY());
    float maxRight = 0.0f;
    float maxBottom = 0.0f;
    for (const InkingWidget* child : GetChildren()) {
        if (child == nullptr || !child->IsVisibleInTree()) {
            continue;
        }
        maxRight = std::max(
            maxRight, static_cast<float>(child->GetX())
                          + static_cast<float>(child->GetWidth()) - originX);
        maxBottom = std::max(
            maxBottom, static_cast<float>(child->GetY())
                           + static_cast<float>(child->GetHeight()) - originY);
    }
    _contentWidth = maxRight;
    _contentHeight = maxBottom;
}

float InkingStaticPanel::ContentViewportWidth() const noexcept {
    const float width = static_cast<float>(GetWidth());
    if (!_data.scroll.enabled || !GetBarMetrics(true).visible) {
        return width > 0.0f ? width : 0.0f;
    }
    const float thickness =
        _data.scroll.barInset * 2.0f + _data.scroll.barWidth;
    return std::max(0.0f, width - thickness);
}

float InkingStaticPanel::ContentViewportHeight() const noexcept {
    const float height = static_cast<float>(GetHeight());
    if (!_data.scroll.enabled || !GetBarMetrics(false).visible) {
        return height > 0.0f ? height : 0.0f;
    }
    const float thickness =
        _data.scroll.barInset * 2.0f + _data.scroll.barWidth;
    return std::max(0.0f, height - thickness);
}

float InkingStaticPanel::GetScrollRangeX() const noexcept {
    return ScrollRange(_contentWidth, ContentViewportWidth());
}

float InkingStaticPanel::GetScrollRangeY() const noexcept {
    return ScrollRange(_contentHeight, ContentViewportHeight());
}

void InkingStaticPanel::SetScroll(float x, float y) {
    if (!_data.scroll.enabled) {
        return;
    }
    const float nextX = ClampScroll(x, GetScrollRangeX());
    const float nextY = ClampScroll(y, GetScrollRangeY());
    if (nextX == _scrollX && nextY == _scrollY) {
        return;  // 没变就不标脏（幂等：多喊几次无害）
    }
    _scrollX = nextX;
    _scrollY = nextY;
    // 滚动只改**绘制**：子孙不进命中表（A8a），命中是递归实时问的、用的又是
    // "当前子树变换" ⇒ 不用标命中脏，也没有表要重烘（§6 第 54 条那笔白干省下了）。
    MarkRepaintDirty();
}

void InkingStaticPanel::onPointerWheel(float deltaX, float deltaY) {
    if (!_data.scroll.enabled) {
        return;
    }
    // 步长是**设计像素**（用户裁定）：一格 = wheelStep，与视口大小无关。
    // 方向：滚轮向上（deltaY > 0）看更上面的内容 ⇒ 滚动量减小。
    const float step = _data.scroll.wheelStep;
    float nextX = _scrollX;
    float nextY = _scrollY;
    if (_data.scroll.horizontal) {
        nextX -= deltaX * step;
    }
    if (_data.scroll.vertical) {
        nextY -= deltaY * step;
    }
    SetScroll(nextX, nextY);
}

void InkingStaticPanel::WheelScroll(float deltaX, float deltaY) {
    onPointerWheel(deltaX, deltaY);
}

ScrollBarMetrics InkingStaticPanel::GetBarMetrics(bool vertical) const noexcept {
    if (!_data.scroll.enabled) {
        return ScrollBarMetrics{};
    }
    const float content = vertical ? _contentHeight : _contentWidth;
    const float offset = vertical ? _scrollY : _scrollX;
    // 条自己用**原始视口**算"有没有得滚 / 滑块多长"——不调 `GetScrollRange*`：
    // 那个已经按"扣掉条带的内容视口"算，而条带宽度又取决于条是否可见 ⇒ 会绕成环。
    // 两者差一个条带宽度（十几像素），对"滑块多长"没有可见影响。
    const float rawRange = ScrollRange(
        content, vertical ? static_cast<float>(GetHeight())
                          : static_cast<float>(GetWidth()));
    return ComputeScrollBar(vertical, static_cast<float>(GetWidth()),
                            static_cast<float>(GetHeight()), content, offset,
                            rawRange, _data.scroll);
}

void InkingStaticPanel::PressScrollAt(float designX, float designY) {
    _dragAxis = 0;
    if (!_data.scroll.enabled) {
        return;
    }
    const float localX = designX - static_cast<float>(GetX());
    const float localY = designY - static_cast<float>(GetY());

    // 竖直优先：两条都可见时它们在右下角重叠，竖直条画在外面（更常用）。
    const ScrollBarMetrics bars[2] = {GetBarMetrics(true), GetBarMetrics(false)};
    for (int axis = 0; axis < 2; ++axis) {
        const bool vertical = axis == 0;
        const ScrollBarMetrics& bar = bars[axis];
        if (!bar.visible) {
            continue;
        }
        if (HitsThumb(vertical, localX, localY, bar)) {
            _dragAxis = vertical ? 1 : 2;  // 拖：跟着指针走
            return;
        }
        const bool onTrack =
            localX >= bar.trackX && localX < bar.trackX + bar.trackWidth
            && localY >= bar.trackY && localY < bar.trackY + bar.trackHeight;
        if (onTrack) {
            // 点轨道 = **跳过去**（滑块中心落到点击处，用户裁定的手感）。
            const float range =
                vertical ? GetScrollRangeY() : GetScrollRangeX();
            const float jumped =
                ScrollFromTrackClick(vertical, localX, localY, bar, range);
            if (vertical) {
                SetScroll(_scrollX, jumped);
            } else {
                SetScroll(jumped, _scrollY);
            }
            return;
        }
    }
}

void InkingStaticPanel::DragScrollTo(float designX, float designY) {
    if (_dragAxis == 0 || !_data.scroll.enabled) {
        return;
    }
    const bool vertical = _dragAxis == 1;
    const ScrollBarMetrics bar = GetBarMetrics(vertical);
    if (!bar.visible) {
        return;
    }
    const float localX = designX - static_cast<float>(GetX());
    const float localY = designY - static_cast<float>(GetY());
    const float range = vertical ? GetScrollRangeY() : GetScrollRangeX();
    // 拖动与"点轨道跳过去"是**同一套算术**（滑块中心对到指针上）——所以拖动
    // 天然跟手、也和点击一致，不会出现"点一下跳到的位置"和"拖到那儿"不一样。
    const float next =
        ScrollFromTrackClick(vertical, localX, localY, bar, range);
    if (vertical) {
        SetScroll(_scrollX, next);
    } else {
        SetScroll(next, _scrollY);
    }
}

void InkingStaticPanel::onPointerPress(bool press) {
    if (!press) {
        _dragAxis = 0;  // 抬起：拖动结束
        return;
    }
    // 钩子只有"按没按"，坐标问场景（它这一帧刚算好）——和 B7 的输入框同一条路。
    InkingScene* scene = GetScene();
    if (scene == nullptr) {
        return;
    }
    PressScrollAt(scene->GetPointerX(), scene->GetPointerY());
}

void InkingStaticPanel::onPointerDrag(float designX, float designY, float,
                                      float) {
    DragScrollTo(designX, designY);
}

}  // namespace inking
