// 几何辅助的实现（`detail`）。声明见 `include/detail/InkingGeometry.h`。
//
// 为什么单独一个 .cpp 而不是塞进 `widget/InkingWidget.cpp`：
//   - 这些函数**不属于 Widget 类**，塞在它的实现文件里会让"谁负责几何换算"
//     变得看不见；
//   - 本文件包含 `InkingWidget.h`（要用到完整类型），而那个头**不**包含
//     `InkingGeometry.h`——这样依赖是单向的，不会循环包含。

#include <detail/InkingGeometry.h>
#include <widget/InkingWidget.h>

// 第 0 层渲染要用渲染层的 `fillShape` / `currentRenderer`（`SKILL.md` §3.5 的
// 可选依赖思路：渲染层是框架内部共用的一层，不是公开 API）。
#include <render/InkingDraw.h>

namespace inking {
namespace detail {

namespace {

/// 设计坐标 → 设备像素的倍率；展示倍率非法时退回 1。
float magnifyOf(const InkingWidget& node) {
    const float value = node.GetMagnification();
    return value > 0.0f ? value : 1.0f;
}

}  // namespace

void FillDefaultRect(const InkingWidget& node, float pixelX, float pixelY) {
    fillShape(currentRenderer(), ShapeSpec::Rect(), node.GetWidth(),
              node.GetHeight(), magnifyOf(node), pixelX, pixelY,
              TransformSpec{}, node.GetColor());
}

Affine AncestorAffine(const InkingWidget& node) noexcept {
    // 递归（深度通常很小）：A(node) = A(parent) ∘ (parent 的子树变换)。
    const InkingWidget* parent = node.GetParent();
    if (parent == nullptr) {
        return Affine{};
    }
    const Affine outer = AncestorAffine(*parent);
    const TransformSpec transform = parent->GetSubtreeTransform();
    if (transform.IsIdentity()) {
        return outer;
    }
    // 作用中心是**父节点的绝对中心**——和它自己那一笔的绘制同一个原点。
    const float centerX = parent->GetAbsX()
                        + static_cast<float>(parent->GetWidth()) * 0.5f;
    const float centerY = parent->GetAbsY()
                        + static_cast<float>(parent->GetHeight()) * 0.5f;
    return ComposeAffine(outer, AffineFromTransform(transform, centerX, centerY));
}

/**
 * 和 `SubtreeChainAffine` 的复合顺序一样，**只把自己那一层**换成
 * `GetClipTransform()`：裁剪窗口是容器本体，不该跟着**自己**的滚动位移跑
 * （B12 的坑：滑出去的内容照样可见）；而祖先滚了，这个容器连同它的裁剪窗口
 * 当然要一起动——所以祖先链照旧用各自的子树变换。
 */
Affine ClipChainAffine(const InkingWidget& node) noexcept {
    Affine chain = AncestorAffine(node);
    const TransformSpec transform = node.GetClipTransform();
    if (transform.IsIdentity()) {
        return chain;
    }
    const float centerX = node.GetAbsX()
                        + static_cast<float>(node.GetWidth()) * 0.5f;
    const float centerY = node.GetAbsY()
                        + static_cast<float>(node.GetHeight()) * 0.5f;
    return ComposeAffine(chain, AffineFromTransform(transform, centerX, centerY));
}

Affine SubtreeChainAffine(const InkingWidget& node) noexcept {
    // 祖先那一份先算出来，再叠上**自己**的子树变换：结果就是"对子节点生效的
    // 那一份"（= AncestorAffine(任意直接子节点)）。
    Affine chain = AncestorAffine(node);
    const TransformSpec transform = node.GetSubtreeTransform();
    if (transform.IsIdentity()) {
        return chain;
    }
    const float centerX = node.GetAbsX()
                        + static_cast<float>(node.GetWidth()) * 0.5f;
    const float centerY = node.GetAbsY()
                        + static_cast<float>(node.GetHeight()) * 0.5f;
    return ComposeAffine(chain, AffineFromTransform(transform, centerX, centerY));
}

ShapeBounds ClipBoundsInDevice(const InkingWidget& node,
                               const ShapeBounds& localBounds) noexcept {
    if (localBounds.width <= 0.0f || localBounds.height <= 0.0f) {
        // 空裁剪区 = 什么都不画（合法但罕见；直接给一个零矩形）。
        return ShapeBounds{0.0f, 0.0f, 0.0f, 0.0f};
    }

    const float scale = node.GetMagnification() > 0.0f
                          ? node.GetMagnification() : 1.0f;
    // ① 本地矩形 + 节点绝对布局位置，再乘展示倍率 → 设备像素（和顶点同源）。
    ShapeBounds rect{(localBounds.x + node.GetAbsX()) * scale,
                     (localBounds.y + node.GetAbsY()) * scale,
                     localBounds.width * scale, localBounds.height * scale};

    // ② 套"对本节点这一层生效"的仿射——裁剪区随**节点自己**走，不随子孙走。
    //    这里用 `ClipChainAffine`（不是 `SubtreeChainAffine`）：节点自己的
    //    **滚动位移不算**，否则裁剪窗口跟着内容跑、滑出去的内容还看得见（B12）。
    //    仿射的平移量也要乘倍率（和 `SubmitToRenderer` 里那份一致）。
    const Affine chainRaw = ClipChainAffine(node);
    if (!chainRaw.IsIdentity() || chainRaw.tx != 0.0f || chainRaw.ty != 0.0f) {
        const Affine chain{chainRaw.a, chainRaw.b, chainRaw.tx * scale,
                           chainRaw.ty * scale};
        const Point2 corners[4] = {
            ApplyAffine(chain, rect.x, rect.y),
            ApplyAffine(chain, rect.x + rect.width, rect.y),
            ApplyAffine(chain, rect.x, rect.y + rect.height),
            ApplyAffine(chain, rect.x + rect.width, rect.y + rect.height)};
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
        rect = ShapeBounds{minX, minY, maxX - minX, maxY - minY};
    }

    return rect;
}

bool AncestorClipInDevice(const InkingWidget& node, ShapeBounds& out) noexcept {
    bool has = false;
    ShapeBounds acc{};

    for (const InkingWidget* up = node.GetParent(); up != nullptr;
         up = up->GetParent()) {
        if (!up->HasSubtreeClip()) {
            continue;
        }
        // 单个祖先的裁剪区（本地矩形 → 设备像素）：与画布那边**共用同一份换算**。
        const ShapeBounds rect =
            ClipBoundsInDevice(*up, up->GetSubtreeClipBounds());

        if (!has) {
            acc = rect;
            has = true;
            continue;
        }
        // 多个祖先都在剪 → 取**交集**（和 SDL 的裁剪矩形嵌套语义一致）。
        acc = IntersectBounds(acc, rect);
    }

    if (!has) {
        return false;
    }
    out = acc;
    return true;
}

ShapeBounds SubtreeHitEnvelope(const InkingWidget& node) noexcept {
    // 先要自己的（按钮那边是三态变换的并集），再把每个子节点的子树包络
    // 按"相对本节点左上角"的偏移并进来。递归深度很小；成本正比于子树节点数，
    // 而且只在**建表**时付一次（容器动起来不用重烘，这正是这一档的目标）。
    // 显式限定到 `InkingWidget`：容器会把自己的 `GetHitEnvelope` 重写成"含子树"，
    // 这里要的是**它自己那一份**，否则就是无限递归。
    ShapeBounds merged = node.InkingWidget::GetHitEnvelope();
    const float baseX = node.GetAbsX();
    const float baseY = node.GetAbsY();
    for (const InkingWidget* child : node.GetChildren()) {
        if (child == nullptr) {
            continue;
        }
        ShapeBounds one = SubtreeHitEnvelope(*child);
        // 子节点的包络是**相对它自己**的，搬到本节点的坐标系里。
        const float offsetX = child->GetAbsX() - baseX;
        const float offsetY = child->GetAbsY() - baseY;
        one.x += offsetX;
        one.y += offsetY;

        const float maxX = merged.x + merged.width;
        const float maxY = merged.y + merged.height;
        const float oneMaxX = one.x + one.width;
        const float oneMaxY = one.y + one.height;
        const float minX = one.x < merged.x ? one.x : merged.x;
        const float minY = one.y < merged.y ? one.y : merged.y;
        const float outMaxX = oneMaxX > maxX ? oneMaxX : maxX;
        const float outMaxY = oneMaxY > maxY ? oneMaxY : maxY;
        merged = ShapeBounds{minX, minY, outMaxX - minX, outMaxY - minY};
    }
    return merged;
}

}  // namespace detail
}  // namespace inking
