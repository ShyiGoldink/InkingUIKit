#include <panel/InkingDynamicPanel.h>

// 动态面板的实现（G-07 新增）。分三段：
//
//   1. **构造与开合**：数据搬进 `InkingPanelLook`（两档共用）+ 几何走动态档的四个写入口；
//   2. **容器**：命中 / 包络 / 子树变换 / 裁剪——语义与静态版逐条相同，实现自己写
//      （静态版那份和"推通道"耦合在一起，抄过来会带进一套用不上的状态）；
//   3. **滚动**：状态（量内容 / 设滚动量 / 条几何）与静态版同源同义；
//      **输入**不同——从 `onPointerStateChanged()` 自判，见头文件"关键决定一"。
//
// 三笔绘制（填充 / 阴影 / 描边）只是接线：实现在 `src/panel/PanelPaint.cpp`，
// 与静态版共用同一份。**滚动条那一段是复制品**（静态版写在它的 `onRender` 里，
// 与"它自己的滚动状态"缠在一起）：它是纯绘制、无时序依赖，等第三个能滚的容器
// 出现时再抽进 `PanelPaint.h`——那时才符合"抄第二遍就抽"的那条门槛。

#include <button/ButtonPaint.h>
#include <detail/InkLog.h>
#include <detail/InkingGeometry.h>
#include <detail/InkingHit.h>
#include <render/InkingDraw.h>
#include <scene/InkingScene.h>

#include "PanelPaint.h"

#include <algorithm>

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

InkingDynamicPanel::InkingDynamicPanel(InkingWidget* parent,
                                       const InkingPanelData& data)
    : InkingDynamicWidget(parent, data.name) {
    applyData(data);
}

void InkingDynamicPanel::applyData(const InkingPanelData& data) {
    // 拷一份再收尾：调用方给的 `InkingPanelData` 可能还没补齐 open 态，而本类要求
    // `_data` 永远处于补齐状态（渲染与命中都按它来）。与静态版同一个理由。
    _data = data;
    _data.Normalize();

    // 两态外观展开存好、状态机与过渡一起复位（`startOpen` 在这里生效）。
    _look.Reset(_data);
    syncBaseColor();

    // -----------------------------------------------------------------------
    // 几何：走**动态档的写入口**（这正是与静态版最本质的差别，同 G-01 / G-03）
    //
    // 写入口自己会标脏、自己会通知场景（走 T-19 的分流），于是"改了几何忘了
    // 通知"这条路在动态档上不存在。构造函数体里调是安全的：写入口只标脏、
    // 只通知场景，不读派生类状态。
    // -----------------------------------------------------------------------
    Resize(_data.width > 0 ? _data.width : 0,
           _data.height > 0 ? _data.height : 0);
    ChangeSelfAnchor(_data.selfAnchor);
    ChangeTraceAnchor(_data.traceAnchor);
    ChangeOffset(_data.offsetX, _data.offsetY);

    // 布局数据（A6）：面板是最需要布局的那个组件——它就是容器。
    _layout = _data.layout;
    _layoutItem = _data.layoutItem;

    SetVisible(_data.visible);
    ChangeZIndex(_data.zIndex);

    // 构造期还没有孩子，内容尺寸就是 0；显式量一次只是为了把状态摆正
    // （`scroll.enabled == false` 时它会把两维清 0）。
    UpdateScrollRange();

    // 几何定下来了 → 这一份"待消费的改动"就没了。之后开合只改变换 / 颜色。
    ClearDirty();

    // 尺寸护栏（T-02）：判据与文案统一在 Widget 层。
    WarnIfZeroSize(InkingDynamicPanelModuleName);

    // 几何是构造期写入的，场景绘制列表里的绝对坐标快照要重算一次。
    NotifySceneStructureChanged();
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

bool InkingDynamicPanel::IsOpen() const noexcept {
    return _look.IsOpen();
}

InkingPanelState InkingDynamicPanel::GetState() const noexcept {
    return _look.state;
}

bool InkingDynamicPanel::IsTransitioning() const noexcept {
    return _look.IsTransitioning();
}

float InkingDynamicPanel::TransitionProgress() const noexcept {
    return _look.TransitionProgress();
}

const InkingShapeSpec& InkingDynamicPanel::GetShape() const noexcept {
    return _data.shape;
}

const InkingPanelAppearance& InkingDynamicPanel::GetAppearance() const noexcept {
    return _look.Current();
}

std::uint32_t InkingDynamicPanel::GetDisplayColor() const noexcept {
    return _look.displayColor;
}

const InkingTransformSpec& InkingDynamicPanel::GetDisplayTransform() const noexcept {
    return _look.displayTransform;
}

const InkingStrokeSpec& InkingDynamicPanel::GetDisplayStroke() const noexcept {
    return _look.displayStroke;
}

const InkingShadowSpec& InkingDynamicPanel::GetDisplayShadow() const noexcept {
    return _look.displayShadow;
}

const InkingPanelData& InkingDynamicPanel::GetData() const noexcept {
    return _data;
}

std::uint64_t InkingDynamicPanel::GetHitEnvelopeCallCount() const noexcept {
    return _envelopeCalls;
}

void InkingDynamicPanel::ResetHitEnvelopeCallCount() noexcept {
    _envelopeCalls = 0;
}

// ---------------------------------------------------------------------------
// [final] 开合写入口（与静态版同名同义）
// ---------------------------------------------------------------------------

bool InkingDynamicPanel::SetOpen(bool open) noexcept {
    if (!_look.SetOpen(open)) {
        return false;
    }
    syncBaseColor();

    // 两笔账当场记：画面变了（重绘脏）、位置变了（命中脏）。
    // **不标结构脏**：布局一个字节没动，子节点的绝对坐标快照仍有效，
    // 命中表也不用重建——这正是"抽屉开合时命中表条目数不变"的来源。
    MarkRepaintDirty();
    MarkDirty();
    return true;
}

bool InkingDynamicPanel::Toggle() noexcept {
    return SetOpen(!IsOpen());
}

void InkingDynamicPanel::syncBaseColor() noexcept {
    // 基类的 color 跟着**当前显示**的颜色走（过渡中是插值色）。
    _color = _look.displayColor;
}

// ---------------------------------------------------------------------------
// 容器：子树变换 / 裁剪（与静态版同一条口径）
// ---------------------------------------------------------------------------

InkingTransformSpec InkingDynamicPanel::GetSubtreeTransform() const noexcept {
    // 容器与子树的唯一连接点：提交层把它复合进整棵子树的每一笔绘制。
    InkingTransformSpec transform = _look.displayTransform;
    if (_data.scroll.enabled) {
        // 滚动就是**在开合动画的变换上再平移一点**——两者互不干扰
        // （抽屉该拉还拉，滚动该滚还滚），也不用给子树变换加第二条通道。
        transform.translateX -= _scrollX;
        transform.translateY -= _scrollY;
    }
    return transform;
}

InkingShapeBounds InkingDynamicPanel::GetSubtreeClipBounds() const noexcept {
    // 内容视口（扣掉可见条带）：子节点本来就画在容器那一笔之上，滚动条画在
    // `onRender` 里会被文字盖住——所以让**内容根本画不到条带上**（与静态版
    // B12 同一条决定）。绘制与命中的裁剪用同一份，两边自动一致。
    return InkingShapeBounds{0.0f, 0.0f, ContentViewportWidth(),
                       ContentViewportHeight()};
}

bool InkingDynamicPanel::HitTest(float worldX, float worldY) const {
    if (!IsVisibleInTree()) {
        return false;
    }

    // ① 自己本体：撤祖先变换 → 减绝对位置 → 过自己变换的逆 → 问形状。
    //    与绘制共用同一份定义（`inking_detail::HitTestShape`）；自己那一笔**不受自己
    //    的裁剪影响**（裁剪只剪子孙）——和绘制同一条规矩。
    if (inking_detail::HitTestShape(*this, GetShape(), _look.displayTransform, worldX,
                             worldY)) {
        return true;
    }

    // ①.5 裁剪：点落在裁剪区之外 ⇒ **整棵子树都不命中**。这就是"画出来被剪、
    //      点起来也被剪"里的后一半，和绘制用的是同一个矩形。
    if (HasSubtreeClip()
        && !inking_detail::HitTestLocalRect(*this, GetSubtreeClipBounds(),
                                     GetClipTransform(), worldX, worldY)) {
        return false;
    }

    // ② 子树：**世界坐标原样**交给每个子节点。子节点的 `AncestorAffine` 里
    //    已经含了我这一层子树变换（含滚动位移），它自己会撤干净；反过来
    //    （先换算到我的本地空间再递归）会让子节点多撤一次，"画的和点的"就差
    //    一个变换——而两边单独看都对（`AGENTS.md` §6 第 38 条那一类）。
    for (const InkingWidget* child : GetChildren()) {
        if (child != nullptr && child->HitTest(worldX, worldY)) {
            return true;
        }
    }
    return false;
}

InkingShapeBounds InkingDynamicPanel::GetHitEnvelope() const {
    // 观测口（§2.1 第 3 条）：动态容器的包络是"整棵子树并集"，每帧刷洞都要算，
    // 所以记一个计数让自检能断言"一帧内不超过 1 次/节点"。
    ++_envelopeCalls;

    // 子树并集 × 两个状态的变换（静态版同一份实现，`PanelPaint.cpp`）。
    InkingShapeBounds bounds =
        inking_detail::PanelHitEnvelope(*this, GetShape(), GetWidth(), GetHeight(),
                                 _look.closed, _look.open);

    // 再加滚动范围：内容能滚到"上半截露到容器外"的位置，洞必须盖住**所有**
    // 滚动位置——否则滚过之后那一片的动态层永远问不到（点不到）。
    // 滚动是单向的（translate -= scroll），所以只往负方向扩一整个 range。
    if (_data.scroll.enabled) {
        const float rangeX = GetScrollRangeX();
        const float rangeY = GetScrollRangeY();
        bounds.x -= rangeX;
        bounds.width += rangeX;
        bounds.y -= rangeY;
        bounds.height += rangeY;
    }
    return bounds;
}

// ---------------------------------------------------------------------------
// 滚动：状态与几何（与静态版同源同义）
// ---------------------------------------------------------------------------

void InkingDynamicPanel::UpdateScrollRange() {
    if (!_data.scroll.enabled) {
        _contentWidth = 0.0f;
        _contentHeight = 0.0f;
        return;
    }
    // 内容尺寸 = 子节点矩形的并集（**相对容器左上角**）。只算子节点本体：
    // 描边 / 阴影 / 文字溢出都不算，否则"内容多大"会随观感细节漂移。
    //
    // 两条口径与静态版逐字相同（0.5.3 那两条更正）：
    //   ① **不看可见性**：几何与布局从来不看可见性（"隐藏的项仍占位"是既有规矩），
    //      测量跟同一条口径才不会出现"隐藏一下内容就变短"；
    //   ② **不减 `GetX()/GetY()`**：子项坐标本来就是相对父级的，再减一次
    //      等于凭空少算一截（列表末尾永远滚不到）。
    float maxRight = 0.0f;
    float maxBottom = 0.0f;
    for (const InkingWidget* child : GetChildren()) {
        if (child == nullptr) {
            continue;
        }
        maxRight = std::max(maxRight,
                            child->GetX() + static_cast<float>(child->GetWidth()));
        maxBottom = std::max(maxBottom,
                             child->GetY() + static_cast<float>(child->GetHeight()));
    }
    _contentWidth = maxRight;
    _contentHeight = maxBottom;
}

float InkingDynamicPanel::ContentViewportWidth() const noexcept {
    const float width = static_cast<float>(GetWidth());
    if (!_data.scroll.enabled || !GetBarMetrics(true).visible) {
        return width > 0.0f ? width : 0.0f;
    }
    const float thickness = _data.scroll.barInset * 2.0f + _data.scroll.barWidth;
    return std::max(0.0f, width - thickness);
}

float InkingDynamicPanel::ContentViewportHeight() const noexcept {
    const float height = static_cast<float>(GetHeight());
    if (!_data.scroll.enabled || !GetBarMetrics(false).visible) {
        return height > 0.0f ? height : 0.0f;
    }
    const float thickness = _data.scroll.barInset * 2.0f + _data.scroll.barWidth;
    return std::max(0.0f, height - thickness);
}

float InkingDynamicPanel::GetScrollRangeX() const noexcept {
    return InkingScrollRange(_contentWidth, ContentViewportWidth());
}

float InkingDynamicPanel::GetScrollRangeY() const noexcept {
    return InkingScrollRange(_contentHeight, ContentViewportHeight());
}

void InkingDynamicPanel::SetScroll(float x, float y) {
    if (!_data.scroll.enabled) {
        return;
    }
    const float nextX = InkingClampScroll(x, GetScrollRangeX());
    const float nextY = InkingClampScroll(y, GetScrollRangeY());
    if (nextX == _scrollX && nextY == _scrollY) {
        return;  // 没变就不标脏（幂等）
    }
    _scrollX = nextX;
    _scrollY = nextY;
    // 滚动只改**绘制**（与静态版同一条）：子孙不进命中表，命中是递归实时问的、
    // 用的又是"当前子树变换" ⇒ 不用标命中脏，也没有表要重烘。
    //
    // 与静态版唯一的差别是"动态档要交洞"，而这里**特意不**标几何移动：
    // 本类的 `GetHitEnvelope()` 已经按滚动范围把洞扩过（见那里），所以
    // **洞与滚动量无关**——每滚一像素都去刷一遍洞等于白付一次"整棵子树并集"。
    // 反过来说：谁把那段扩范围的代码删了，滚动之后就会有一片点不到，
    // 而那时症状是"滚过去点不动"，不会有任何报错。
    MarkRepaintDirty();
}

InkingScrollBarMetrics InkingDynamicPanel::GetBarMetrics(bool vertical) const noexcept {
    if (!_data.scroll.enabled) {
        return InkingScrollBarMetrics{};
    }
    const float content = vertical ? _contentHeight : _contentWidth;
    const float offset = vertical ? _scrollY : _scrollX;
    // 条自己用**原始视口**算"有没有得滚 / 滑块多长"——不调 `GetScrollRange*`
    // （那个已按"扣掉条带的内容视口"算，而条带宽度又取决于条是否可见 ⇒ 成环）。
    const float rawRange = InkingScrollRange(
        content, vertical ? static_cast<float>(GetHeight())
                          : static_cast<float>(GetWidth()));
    return InkingComputeScrollBar(vertical, static_cast<float>(GetWidth()),
                            static_cast<float>(GetHeight()), content, offset,
                            rawRange, _data.scroll);
}

InkingPoint2 InkingDynamicPanel::localFromWorld(float worldX, float worldY) const noexcept {
    // 与 `inking_detail::HitTestShape` 的前三步同源（缺一步就是"点在这儿、滚的是别处"）：
    // 撤祖先仿射 → 减绝对位置 → 过自身显示变换的逆（中心同绘制）。
    InkingPoint2 world{worldX, worldY};
    const InkingAffine ancestor = inking_detail::AncestorAffine(*this);
    if (!ancestor.IsIdentity()) {
        world = InkingApplyAffine(InkingInvertAffine(ancestor), worldX, worldY);
    }
    return InkingInverseTransform(_look.displayTransform, world.x - GetAbsX(),
                            world.y - GetAbsY(),
                            static_cast<float>(GetWidth()) * 0.5f,
                            static_cast<float>(GetHeight()) * 0.5f);
}

void InkingDynamicPanel::PressScrollAt(float designX, float designY) {
    _dragAxis = 0;
    if (!_data.scroll.enabled) {
        return;
    }
    const InkingPoint2 local = localFromWorld(designX, designY);

    // 竖直优先：两条都可见时它们在右下角重叠，竖直条画在外面（更常用）。
    const InkingScrollBarMetrics bars[2] = {GetBarMetrics(true), GetBarMetrics(false)};
    for (int axis = 0; axis < 2; ++axis) {
        const bool vertical = axis == 0;
        const InkingScrollBarMetrics& bar = bars[axis];
        if (!bar.visible) {
            continue;
        }
        if (InkingHitsThumb(local.x, local.y, bar)) {
            _dragAxis = vertical ? 1 : 2;  // 拖：跟着指针走
            return;
        }
        const bool onTrack = local.x >= bar.trackX
                          && local.x < bar.trackX + bar.trackWidth
                          && local.y >= bar.trackY
                          && local.y < bar.trackY + bar.trackHeight;
        if (onTrack) {
            // 点轨道 = **跳过去**（滑块中心落到点击处，用户裁定的手感）。
            const float range = vertical ? GetScrollRangeY() : GetScrollRangeX();
            const float jumped =
                InkingScrollFromTrackClick(vertical, local.x, local.y, bar, range);
            if (vertical) {
                SetScroll(_scrollX, jumped);
            } else {
                SetScroll(jumped, _scrollY);
            }
            return;
        }
    }
}

void InkingDynamicPanel::DragScrollTo(float designX, float designY) {
    if (_dragAxis == 0 || !_data.scroll.enabled) {
        return;
    }
    const bool vertical = _dragAxis == 1;
    const InkingScrollBarMetrics bar = GetBarMetrics(vertical);
    if (!bar.visible) {
        return;
    }
    const InkingPoint2 local = localFromWorld(designX, designY);
    const float range = vertical ? GetScrollRangeY() : GetScrollRangeX();
    // 拖动与"点轨道跳过去"是**同一套算术**（滑块中心对到指针上）——所以拖动
    // 天然跟手、也和点击一致。
    const float next =
        InkingScrollFromTrackClick(vertical, local.x, local.y, bar, range);
    if (vertical) {
        SetScroll(_scrollX, next);
    } else {
        SetScroll(next, _scrollY);
    }
}

// ---------------------------------------------------------------------------
// 输入（动态档唯一的时机）：滚动条拖动
// ---------------------------------------------------------------------------

void InkingDynamicPanel::onPointerStateChanged() {
    // 没配滚动就没有要判的东西（`_data` 构造后不变，所以这里可以早退）。
    if (!_data.scroll.enabled) {
        return;
    }
    InkingScene* scene = GetScene();
    if (scene == nullptr) {
        return;
    }

    // 指针不在窗口里一律不算"在身上"（窗口外不该继续判拖动）——与动态按钮同一条。
    const bool hit = scene->IsPointerInside()
                  && HitTest(scene->GetPointerX(), scene->GetPointerY());
    const inking_detail::PointerProbeResult r =
        _probe.Update(hit, scene->GetPointerDown());

    const float pointerX = scene->GetPointerX();
    const float pointerY = scene->GetPointerY();

    if (r.primary.released) {
        _dragAxis = 0;  // 抬起：拖动结束（捕获已由 PointerProbe 释放）
        return;
    }
    if (r.primary.pressed) {
        // 按下那一刻定"拖滑块还是点轨道"——按下点在这一帧才算数，
        // 之后滑出去仍然算按着（捕获语义，与静态档一致）。
        PressScrollAt(pointerX, pointerY);
        return;
    }
    if (_dragAxis != 0 && scene->GetPointerDown().IsDown(InkingPointerButton::Primary)) {
        // 按住期间每次指针变化都跟着走。**这不是每帧轮询**：只有指针真的变了
        // 才会收到这次通知（场景那边加了"值没变不推"的判断）。
        DragScrollTo(pointerX, pointerY);
    }
}

// ---------------------------------------------------------------------------
// 每渲染帧一次：开合过渡
// ---------------------------------------------------------------------------

void InkingDynamicPanel::onAnimationTick(float deltaSeconds) {
    const InkingPanelLook::AdvanceResult advanced = _look.Advance(deltaSeconds);
    if (!advanced.repaint && !advanced.hit) {
        return;
    }
    if (advanced.repaint) {
        syncBaseColor();
        MarkRepaintDirty();  // 颜色 / 描边 / 阴影变了，命中不变
    }
    if (advanced.hit) {
        // 变换变了 ⇒ 命中可能跟着变。但**不改表**：子树不进表，容器那条洞
        // 由 `MarkGeometryMoved` 刷（叶子语义：面板不保证没有静态子孙，所以
        // 那一条会按"有没有静态子孙"自己分流）。
        MarkGeometryMoved();
    }
}

// ---------------------------------------------------------------------------
// 绘制（三笔接线 + 两条滚动条）
// ---------------------------------------------------------------------------

void InkingDynamicPanel::onRender(float pixelX, float pixelY) const {
    // 有阴影先画阴影（它在填充**之下**——"形状之下的偏移副本"）。
    // 描边不在这里：它走装饰遍（`onRenderDecoration`），画在子孙之上。
    if (_look.displayShadow.Visible()) {
        inking_detail::PaintSurfaceShadow(inking_detail::currentRenderer(), *this, GetShape(),
                                   _look.displayShadow, _look.displayTransform,
                                   pixelX, pixelY);
    }
    inking_detail::PaintPanelAppearance(*this, GetShape(), _look.displayColor,
                                 _look.displayTransform, pixelX, pixelY);

    // 两条滚动条：画在**容器自己的坐标**里（不含滚动偏移——条不该跟着内容跑），
    // 所以用 `_look.displayTransform`，不是 `GetSubtreeTransform()`。
    paintScrollBars(pixelX, pixelY);
}

void InkingDynamicPanel::paintScrollBars(float pixelX, float pixelY) const {
    if (!_data.scroll.enabled) {
        return;
    }
    SDL_Renderer* renderer = inking_detail::currentRenderer();
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
        // 轨道与滑块各自按自己的短边算——不然细轨道会配出"圆角比它还大"的怪形状。
        const float radius = _data.scroll.barRadius > 0.0f
                                 ? _data.scroll.barRadius
                                 : std::min(width, height) * 0.5f;
        inking_detail::fillShape(renderer, InkingShapeSpec::RoundedRect(radius),
                          std::max(1, static_cast<int>(width + 0.5f)),
                          std::max(1, static_cast<int>(height + 0.5f)),
                          magnitude, pixelX + x * magnitude,
                          pixelY + y * magnitude, _look.displayTransform,
                          color);
    };
    for (int axis = 0; axis < 2; ++axis) {
        const InkingScrollBarMetrics bar = GetBarMetrics(axis == 0);
        if (!bar.visible) {
            continue;
        }
        // 先铺**条带区域的背景**（配了色才铺）——不然内容会从轨道与滑块的缝隙里
        // 透出来。它与"滑出去的内容被裁剪"是两件事。
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

void InkingDynamicPanel::onRenderDecoration(float pixelX, float pixelY) const {
    // 只有描边：阴影在填充那一趟（它在形状**之下**，挪到这里反而盖住内容）。
    inking_detail::PaintSurfaceStroke(inking_detail::currentRenderer(), *this, GetShape(),
                               _look.displayStroke, _look.displayTransform,
                               pixelX, pixelY);
}

