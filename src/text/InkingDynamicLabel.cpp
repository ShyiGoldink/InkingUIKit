#include <text/InkingDynamicLabel.h>

// 动态标签的实现（G-05 新增；G-06 把排版 / 取色 / 画字抽进 `InkingLabelLike`）。
//
// 这里**只剩动态档自己的四件事**：
//   1. 把 `InkingLabelData` 搬进 `InkingLabelLike`（数据面）与几何（四个写入口）；
//   2. `SetText` **不**重排、不改几何（关键决定一）；
//   3. `FitToContent()` / `IsManagedByLayout()`（G-05 唯一的新语义）；
//   4. 绘制与包络两笔（转发 / 与静态版同一份）。
//
// 排版缓存、`LayoutForWidth`、`CacheMatches`、`EffectiveTextColor`、测量与
// `paintLabelText` 全在 `InkingLabelLike`——G-05 时这里是那份"复制品"，当时靠
// "两份逐字节相同"证明同源；现在只有一份，那条复制期的纪律作废（同 G-04）。

// `inking_detail::TransformBounds` 落到了 `detail/InkingGeometry.h`，而
// `widget/InkingDynamicWidget.h` 不传递它——凡是调它的 .cpp 都要自己带。
#include <detail/InkingGeometry.h>
#include <detail/InkLog.h>

#include <cmath>
#include <string>
#include <utility>

namespace {

/// 设计坐标里的尺寸取整：四舍五入（与 `InkingWidget.cpp` 里那份同名同义，
/// 但那份是那个 TU 的匿名命名空间里的，这里要自己有一份）。
int RoundSize(float value) noexcept {
    if (value <= 0.0f) {
        return 0;
    }
    return static_cast<int>(std::lround(value));
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

InkingDynamicLabel::InkingDynamicLabel(InkingWidget* parent,
                                       const InkingLabelData& data)
    : InkingDynamicWidget(parent, data.name),
      InkingLabelLike(static_cast<InkingWidget&>(*this), data) {
    // ---- 几何：走动态档的四个写入口（它们自己标脏、自己通知场景） ----
    //
    // 负值一律忽略（`Resize` 的语义），0 表示"这一维没配置"——下面 `FitOnConstruction`
    // 会补上它。构造期写入口是安全的：它们只标脏 / 只通知场景，不读派生类状态。
    Resize(data.width > 0 ? data.width : 0, data.height > 0 ? data.height : 0);
    ChangeSelfAnchor(data.selfAnchor);
    ChangeTraceAnchor(data.traceAnchor);
    ChangeOffset(data.offsetX, data.offsetY);

    // 布局：静态版两样都搬，这里照抄（`_layout` 决定"我当容器时怎么排孩子"，
    // 标签几乎不会用到，但字段有、语义又没变，就不制造第二套规矩）。
    _layout = data.layout;
    _layoutItem = data.layoutItem;

    SetVisible(data.visible);
    ChangeZIndex(data.zIndex);

    // 标签不吃点击：它常压在按钮 / 图片上，吃掉点击就会变成"看得见、点不动"。
    SetHitPassThrough(true);

    ClearDirty();

    // 构造期补一次自测量（静态版对应的是构造末尾那句 `RelayoutParent()`）：
    // 不补的话 `width/height` 留 0 就是一个像素都不画、也零日志的空白。
    FitOnConstruction();

    // 尺寸护栏（T-02）放在自测量之后：正常标签此时已经有尺寸，不该再报。
    // **空文本不报**：空标签本来就不占地方，0 尺寸是预期结果。
    if (!_data.text.empty()) {
        WarnIfZeroSize(InkingDynamicLabelModuleName);
    }

    NotifySceneStructureChanged();
}

void InkingDynamicLabel::FitOnConstruction() noexcept {
    // 布局容器里一律不自测量：尺寸归父容器管，自己量和布局打架
    // （与 `RelayoutSelfIfContentSized()` 的边界完全同一条）。
    if (IsManagedByLayout()) {
        return;
    }
    if (_width > 0 && _height > 0) {
        return;  // 两维都配置了，没有"补"的余地
    }

    const InkingLayoutSize measured = MeasureLayout(InkingLayoutConstraints{0.0f, 0.0f});
    // **只补零的那一维**：`width: 200` 是配置，测量值（比如 76）不许覆盖它
    // （`RelayoutSelfIfContentSized` 那条硬约束，踩过的坑：右对齐文字会全部错位）。
    const int width = _width > 0 ? _width : RoundSize(measured.width);
    const int height = _height > 0 ? _height : RoundSize(measured.height);
    Resize(width, height);
}

// ---------------------------------------------------------------------------
// 写入口（与静态版同名同义，只有 SetText 的"改不改几何"不同）
// ---------------------------------------------------------------------------

bool InkingDynamicLabel::SetText(std::string text) noexcept {
    // 数据手术在 `InkingLabelLike`（改字段 + 让排好的行作废）。
    if (!assignText(std::move(text))) {
        return false;
    }

    // **只标重绘脏**（关键决定一）：几何是调用方的事，喊 `FitToContent()` 才动。
    // 也不标命中脏：文字不参与命中（`AGENTS.md` §6 第 47 条）。
    MarkRepaintDirty();
    return true;
}

bool InkingDynamicLabel::SetTextColor(std::uint32_t color) noexcept {
    if (!assignTextColor(color)) {
        return false;
    }
    MarkRepaintDirty();
    return true;
}

// ---------------------------------------------------------------------------
// FitToContent：按内容改几何（G-05 的核心，也是唯一会改几何的入口）
// ---------------------------------------------------------------------------

bool InkingDynamicLabel::IsManagedByLayout() const noexcept {
    // 判据与 `InkingWidget::RelayoutParent()` 逐字一致：往上游找第一个声明了
    // 有效 `InkingLayoutSpec` 的祖先（不含自己）。`ArrangeSubtree` 会排**每一个**孩子，
    // 所以"父容器有布局"等价于"我的几何归它写"——没有"不参与布局"的开关。
    for (const InkingWidget* node = GetParent(); node != nullptr;
         node = node->GetParent()) {
        if (node->GetLayoutSpec().Valid()) {
            return true;
        }
    }
    return false;
}

bool InkingDynamicLabel::FitToContent() {
    // 关键决定二：布局容器里**不写几何**。写了也会在下一帧被
    // `ApplyLayoutRect` 整体覆盖（尺寸归父容器管），那种"改了又没改"是最难查的
    // 静默失效——所以这里返回 false 并**喊一声**，而不是假装成功。
    if (IsManagedByLayout()) {
        INK_LOG_WARN(std::string(InkingDynamicLabelModuleName),
                     "FitToContent() 在布局容器里不生效：几何归父容器的布局管，"
                     "自己量的尺寸下一次 Relayout 就被覆盖。名字："
                         + _name + "（要按内容撑高，把它从带 InkingLayoutSpec 的容器里"
                                   "拿出来，或改写父容器的布局）");
        return false;
    }

    // 第一趟：按**当前宽度**换行，拿到"内容想要多大"。宽度不限时（`_width == 0`）
    // 就是不换行的自然宽。
    int width = RoundSize(MeasureLayout(InkingLayoutConstraints{0.0f, 0.0f}).width);
    if (width <= 0) {
        // 空文本（或者量出来就是 0 宽）：宽度不缩到 0——一个暂时没字的标签不该
        // 把自己的框收成一条线。几何整个不动 ⇒ `Resize` 幂等 ⇒ 返回 false。
        return Resize(_width, _height);
    }

    // 收窄到自洽：排版交出的 `width` 是**内容宽**（≤ 可用宽），而它写回去就成了
    // 新的换行基准 ⇒ 必须量到"在这个整数宽下再量还是这个宽"为止，否则写进去的
    // 高度是按旧宽排出来的，"事后问 `MeasureLayout` 又是另一个数"（窄文本上差
    // 一整行）。实测 1~2 趟收敛；上限 4 趟只是防病态输入里来回跳。
    //
    // 约束一律用**取整之后**的宽：写进 `_width` 的就是这个整数，用它量出来的
    // 行数与之后 `GetTextLayout()` / `MeasureLayout()` 看到的行数才严格同源。
    int height = 0;
    for (int i = 0; i < 4; ++i) {
        const InkingLayoutSize atWidth = MeasureLayout(
            InkingLayoutConstraints{static_cast<float>(width), 0.0f});
        const int nextWidth = RoundSize(atWidth.width);
        height = RoundSize(atWidth.height);
        if (nextWidth == width || nextWidth <= 0) {
            break;  // 自洽（或者内容已经窄到 0，用当前这一趟的结果）
        }
        width = nextWidth;
    }

    // 走动态档的写入口：顺带标脏、顺带让场景刷洞（标签是叶子 ⇒ 只刷洞，T-19）。
    return Resize(width, height);
}

// ---------------------------------------------------------------------------
// 排版 / 绘制 / 包络
// ---------------------------------------------------------------------------

const InkingTextLayoutResult& InkingDynamicLabel::GetTextLayout() const {
    return InkingLabelLike::GetTextLayout();
}

InkingLayoutSize InkingDynamicLabel::MeasureLayout(
    const InkingLayoutConstraints& constraints) const {
    // 测量本体在 `InkingLabelLike`：宽度有约束就按约束换行，高度是排版结果
    // （**高度传 0**，否则"框多高"永远等于"一行高"，内容驱动的高度撑不开）。
    return MeasureLabel(constraints);
}

void InkingDynamicLabel::onRender(float pixelX, float pixelY) const {
    // 文字那一笔在 `InkingLabelLike`（与静态版同一份）。滚动量走 `TextScrollX()`
    // ——动态标签恒 0，将来真有动态输入框就覆盖它（静态档那边是 `InkingTextInput`）。
    paintLabelText(pixelX, pixelY, TextScrollX());
}

InkingShapeBounds InkingDynamicLabel::GetHitEnvelope() const {
    // 整块矩形 + 外部显示变换（与静态版同义）。动态档拿它算**洞**：标签不吃点击，
    // 所以这个洞只是"绘制区域"——点落在里面时动态层问一圈没人认，
    // `QueryHit` 回退到静态表的答案，不会挡住底下的按钮。
    //
    // 为什么仍要按外部变换算：被挪走 / 放大之后，洞必须跟着走；洞过期 = 那一片
    // 的动态层永远问不到（T-19 的静默失效形态）。
    const InkingShapeBounds base{0.0f, 0.0f, static_cast<float>(_width),
                           static_cast<float>(_height)};
    return inking_detail::TransformBounds(base, GetExternalTransform(),
                                   static_cast<float>(_width) * 0.5f,
                                   static_cast<float>(_height) * 0.5f);
}

// ---------------------------------------------------------------------------
// G-06：保留既有私有符号（唯一实现在 `InkingLabelLike`）
// ---------------------------------------------------------------------------

const InkingTextLayoutResult& InkingDynamicLabel::LayoutForWidth(
    float available, float availableHeight) const {
    return InkingLabelLike::LayoutForWidth(available, availableHeight);
}

bool InkingDynamicLabel::CacheMatches(float available,
                                      float availableHeight) const noexcept {
    return InkingLabelLike::CacheMatches(available, availableHeight);
}

std::uint32_t InkingDynamicLabel::EffectiveTextColor() const noexcept {
    return InkingLabelLike::EffectiveTextColor();
}

