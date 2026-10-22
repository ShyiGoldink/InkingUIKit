// 动态档 Widget 的实现（`include/widget/InkingDynamicWidget.h`）。
//
// 与静态档的**唯一区别**就是下面这四个几何写入口：几何会自己变，所以它不进
// 命中表、自己维护命中状态，并且每帧收到一次 `Tick`。

#include <widget/InkingDynamicWidget.h>

#include <detail/InkingGeometry.h>

namespace inking {

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

// 动态档：几何会自己变，所以 dynamic 传 true。
InkingDynamicWidget::InkingDynamicWidget(InkingWidget* parent,
                                         const AnchorData& data)
    : InkingWidget(parent, data, true) {}

InkingDynamicWidget::InkingDynamicWidget(InkingWidget* parent,
                                         const std::string& name)
    : InkingWidget(parent, name, true) {}

// ---------------------------------------------------------------------------
// [final] 写入口：几何变化
// ---------------------------------------------------------------------------
//
// 一律不加 virtual（子类不可重写），标脏与钩子都在内部完成；返回 bool 表示
// 这次是否真的改了，没变就**不标脏、不触发钩子**。四条的骨架完全一样：
//   判等 → 写字段 → MarkStructureChanged()（命中脏 + 重绘脏 + 场景重排）→ 钩子。

bool InkingDynamicWidget::Resize(int width, int height) noexcept {
    const bool changeWidth =
        (width != InkingResize::none) && (width >= 0) && (width != _width);
    const bool changeHeight =
        (height != InkingResize::none) && (height >= 0) && (height != _height);

    if (!changeWidth && !changeHeight) {
        return false;
    }
    if (changeWidth) {
        _width = width;
    }
    if (changeHeight) {
        _height = height;
    }

    // 几何变了：命中脏 + 重绘脏 + 让场景重算（绘制列表里缓存的绝对坐标过期了）。
    // 最后那条正是"父级一动，静态子节点的缓存绝对坐标会过期"：
    // 动态节点自己每帧现算没问题，但**它的静态子孙**靠的是列表里的快照。
    MarkStructureChanged();

    // 自己是容器 ⇒ 尺寸变了要**立刻重排孩子**（0.4.0 A4）。
    //
    // 以前只有构造期那一趟布局，所以"抽屉从 56 拉到 200"之后里面的
    // `fillMain` / `Stretch` 全停在旧值上——核的抽屉只能跳变后整树重建。
    // 布局规则住在自己身上（`GetLayoutSpec()`），所以尺寸写入口是它唯一的
    // 触发点；放在 `MarkStructureChanged` 之后，重排里再标脏也只会合并成一次。
    if (GetLayoutSpec().Valid()) {
        Relayout();
    }

    onSizeChanged();
    return true;
}

bool InkingDynamicWidget::ChangeSelfAnchor(const AnchorPoint& anchor) noexcept {
    if (_selfAnchor.x == anchor.x && _selfAnchor.y == anchor.y) {
        return false;
    }
    _selfAnchor = anchor;
    MarkStructureChanged();
    onSelfAnchorChanged();
    return true;
}

bool InkingDynamicWidget::ChangeTraceAnchor(const AnchorPoint& anchor) noexcept {
    if (_traceAnchor.x == anchor.x && _traceAnchor.y == anchor.y) {
        return false;
    }
    _traceAnchor = anchor;
    MarkStructureChanged();
    onTraceAnchorChanged();
    return true;
}

bool InkingDynamicWidget::ChangeOffset(float offsetX, float offsetY) noexcept {
    if (_offsetX == offsetX && _offsetY == offsetY) {
        return false;
    }
    _offsetX = offsetX;
    _offsetY = offsetY;
    MarkStructureChanged();
    onOffsetChanged();
    return true;
}

// ---------------------------------------------------------------------------
// 每帧一步与默认渲染
// ---------------------------------------------------------------------------

void InkingDynamicWidget::onTick(float /*deltaSeconds*/) {}

void InkingDynamicWidget::onRender(float pixelX, float pixelY) const {
    // 和静态版一样：默认一块实心矩形。动态档默认差的只是"位置每帧变"。
    // 这里不能调 InkingStaticWidget::onRender——两者是**兄弟**（都直接继承
    // InkingWidget），静态版不是动态版的基类，没有对象可借用。
    detail::FillDefaultRect(*this, pixelX, pixelY);
}

}  // namespace inking
