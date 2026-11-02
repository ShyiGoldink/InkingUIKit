#include <text/InkingStaticLabel.h>

// `inking_detail::TransformBounds` 在源项目里住在**那个单体头** `include/ink/basic/InkingAnchor.h`
// 里，本文件是经 `text/InkingStaticLabel.h` -> `ink/basic/InkingAnchor.h` **传递**
// 拿到的。搬过来之后单体头被拆开（`TransformBounds` 落到了 `detail/InkingGeometry.h`），
// 而 `widget/InkingStaticWidget.h` 不再传递它——所以这里必须显式包含。
// **拆单体头时现形的就是这一类依赖；凡是调 `TransformBounds` 的 .cpp 都要自己带。**
#include <detail/InkingGeometry.h>

#include <string>
#include <utility>

// 标签（A7）的实现。G-06 起**排版、取色、测量、画字**都只有一份，在
// `src/text/InkingLabelLike.cpp`；本文件只剩静态档自己的三件事：
//
//   1. **数据驱动的构造**和面板 / 按钮走同一条路：基类只拿到名字，
//      尺寸与锚点是这之后从 data 取到的（不给它写进去，标签就是 0×0——
//      画不出来也点不到）。新增字段要记得在这儿搬一次（AGENTS §6 第 83 条）；
//   2. **`SetText` 之后重排**：文字变长 ⇒ 自己变高 ⇒ 兄弟要让位，那是父容器的
//      活儿（`RelayoutParent`）。动态档故意不做这一件（G-05 的关键决定一）；
//   3. 三行转发：`MeasureLayout` / `onRender` / 两个观测量。
//
// 文件末尾那三个私有转发是 ABI 用的——G-06 之前它们是本类的私有成员，
// `nm -g` 能看到符号，挪走之后也不能少（与 G-02 / G-04 同一条纪律）。


InkingStaticLabel::InkingStaticLabel(InkingWidget* parent, const InkingLabelData& data)
    : InkingStaticWidget(parent, data.name),
      InkingLabelLike(static_cast<InkingWidget&>(*this), data) {
    _width = data.width > 0 ? data.width : 0;
    _height = data.height > 0 ? data.height : 0;
    _selfAnchor = data.selfAnchor;
    _traceAnchor = data.traceAnchor;
    _offsetX = data.offsetX;
    _offsetY = data.offsetY;
    _layout = data.layout;
    _layoutItem = data.layoutItem;

    SetVisible(data.visible);
    ChangeZIndex(data.zIndex);

    // 标签不吃点击：它常压在按钮 / 图片上，吃掉点击就会变成"看得见、点不动"。
    SetHitPassThrough(true);

    ClearDirty();

    // 构造期没有 `SetText` 来触发重排（数据是直接写进 `_data` 的），所以这里
    // 补一次（`0.1.1` T-01）：
    //   - 在**有布局的容器**里：父容器排一次（幂等，`ApplyLayoutRect` 没变就不标脏）；
    //   - 在**没有布局的容器**里：走 `RelayoutSelfIfContentSized` 自己量，
    //     否则宽高都留在 0，一个像素都不画且零日志——这正是反馈方那张 200×0 的标签。
    RelayoutParent();

    // 尺寸护栏（T-02）放在自测量**之后**：正常标签此时已经有尺寸，不该再报。
    // **空文本不报**：空标签本来就不占地方（不画东西），0 尺寸是预期结果。
    if (!_data.text.empty()) {
        WarnIfZeroSize(InkingLabelModuleName);
    }

    NotifySceneStructureChanged();
}

InkingStaticLabel::InkingStaticLabel(InkingWidget* parent,
                                     const std::string& name)
    : InkingStaticWidget(parent, name),
      InkingLabelLike(static_cast<InkingWidget&>(*this)) {
    _data.name = name;
    SetHitPassThrough(true);
    ClearDirty();
    NotifySceneStructureChanged();
}

bool InkingStaticLabel::SetText(std::string text) noexcept {
    // 数据手术在 `InkingLabelLike`（改字段 + 让排好的行作废）；"之后做什么"是两档的差别。
    if (!assignText(std::move(text))) {
        return false;
    }

    // 尺寸可能变了 ⇒ 让**所在容器**重排（兄弟要让位）。
    // 不标命中脏：文字不参与命中（AGENTS §6 第 47 条）。
    RelayoutParent();
    MarkRepaintDirty();
    return true;
}

bool InkingStaticLabel::SetTextColor(std::uint32_t color) noexcept {
    if (!assignTextColor(color)) {
        return false;
    }
    MarkRepaintDirty();
    return true;
}

const InkingTextLayoutResult& InkingStaticLabel::GetTextLayout() const {
    return InkingLabelLike::GetTextLayout();
}

InkingLayoutSize InkingStaticLabel::MeasureLayout(
    const InkingLayoutConstraints& constraints) const {
    // 测量本体在 `InkingLabelLike`：宽度有约束就按约束换行，高度是排版结果
    // （**高度传 0**，否则"框多高"永远等于"一行高"，内容驱动的高度撑不开）。
    return MeasureLabel(constraints);
}

void InkingStaticLabel::onRender(float pixelX, float pixelY) const {
    // 文字那一笔在 `InkingLabelLike`（与动态档同一份）。水平滚动量由本类提供：
    // 标签恒 0，输入框（`InkingTextInput`）覆盖 `TextScrollX()`。
    paintLabelText(pixelX, pixelY, TextScrollX());
}

InkingShapeBounds InkingStaticLabel::GetHitEnvelope() const {
    // 整块矩形（基类默认那一份）+ 外部显示变换：被挪走 / 放大之后，表里登记的
    // 包络必须跟着走，否则哪天标签改成吃点击时会"看着在这儿、点不到"
    // （`AGENTS.md` §6 第 42 条那条"保守包络"的纪律）。
    const InkingShapeBounds base{0.0f, 0.0f, static_cast<float>(_width),
                           static_cast<float>(_height)};
    return inking_detail::TransformBounds(base, GetExternalTransform(),
                                   static_cast<float>(_width) * 0.5f,
                                   static_cast<float>(_height) * 0.5f);
}

// ---------------------------------------------------------------------------
// G-06：保留既有私有符号（唯一实现在 `InkingLabelLike`）
// ---------------------------------------------------------------------------

const InkingTextLayoutResult& InkingStaticLabel::LayoutForWidth(
    float available, float availableHeight) const {
    return InkingLabelLike::LayoutForWidth(available, availableHeight);
}

bool InkingStaticLabel::CacheMatches(float available,
                                     float availableHeight) const noexcept {
    return InkingLabelLike::CacheMatches(available, availableHeight);
}

std::uint32_t InkingStaticLabel::EffectiveTextColor() const noexcept {
    return InkingLabelLike::EffectiveTextColor();
}

