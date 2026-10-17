#include "PanelPaint.h"

// `detail::TransformBounds` 与 `detail::SubtreeHitEnvelope` 在源项目里都住在**那个
// 单体头** `include/ink/basic/InkingAnchor.h` / 单体 `InkingAnchor.cpp` 里——本文件是
// 经 `PanelPaint.h` -> `ink/basic/InkingAnchor.h` **传递**拿到的。拆层之后两者都落到
// `detail/InkingGeometry.h`（都要"沿父链/子表"的视野），而 `PanelPaint.h` 现在只拖
// `widget/InkingWidget.h`、不再传递它们 ⇒ 必须显式包含。
// **拆单体头时现形的就是这一类依赖。**
#include <detail/InkingGeometry.h>
#include <render/InkingDraw.h>

#include <SDL3/SDL.h>

#include <cstdint>

namespace inking::detail {

namespace {

/// 设计坐标 → 设备像素的倍率（和 ButtonPaint.cpp 同一份口径）。
float Magnify(const InkingAnchor& node) {
    const float value = node.GetMagnification();
    return value > 0.0f ? value : 1.0f;
}

/// 两个 AABB 的并集。
ShapeBounds UnionBounds(const ShapeBounds& a, const ShapeBounds& b) {
    const float maxX = a.x + a.width;
    const float maxY = a.y + a.height;
    const float bMaxX = b.x + b.width;
    const float bMaxY = b.y + b.height;

    const float minX = b.x < a.x ? b.x : a.x;
    const float minY = b.y < a.y ? b.y : a.y;
    const float outMaxX = bMaxX > maxX ? bMaxX : maxX;
    const float outMaxY = bMaxY > maxY ? bMaxY : maxY;
    return ShapeBounds{minX, minY, outMaxX - minX, outMaxY - minY};
}

// 这里原来有一份本地的 `TransformBounds`（"AABB 绕中心过一遍变换之后的外接
// AABB"）。A13 把它抽成了公共件 `inking::detail::TransformBounds`
// （`include/ink/basic/InkingAnchor.h`）——图片 / 标签算**外部显示变换**的包络
// 要的是同一件事，抄第二遍就会分叉（注释里那句"和 ButtonHitEnvelope 里那份是
// 同一个算法"正是它该被抽出来的证据）。

}  // namespace

void PaintPanelAppearance(const InkingAnchor& node, const ShapeSpec& shape,
                          std::uint32_t displayColor,
                          const TransformSpec& transform, float pixelX,
                          float pixelY) {
    // 和按钮共用同一份填充实现（`detail::fillShape`）：轮廓三角化 + 边缘羽化
    // + 顶点过变换。形状是唯一真相，所以"画出来的区域"与"命中判定的区域"
    // 从同一份定义派生。
    fillShape(currentRenderer(), shape, node.GetWidth(), node.GetHeight(),
              Magnify(node), pixelX, pixelY, transform, displayColor);
}

ShapeBounds PanelHitEnvelope(const InkingWidget& node, const ShapeSpec& shape,
                             int width, int height,
                             const PanelAppearance& closed,
                             const PanelAppearance& open) {
    // ① 布局空间里的整棵子树：自己的形状 AABB 打底。
    //    形状本身的 AABB 由形状层给（圆 / 椭圆不是整块矩形）。
    ShapeBounds layout = GetShapeBounds(shape, width, height);

    const float baseX = node.GetAbsX();
    const float baseY = node.GetAbsY();
    for (const InkingWidget* child : node.GetChildren()) {
        if (child == nullptr) {
            continue;
        }
        // 子节点上报的是"相对它自己左上角"的子树包络，搬到本节点的坐标系里。
        // 注意这里要的是**布局位置**（GetAbsX/Y），不带任何变换：变换在
        // 第 ② 步统一作用——容器的变换是作用在整棵子树上的。
        ShapeBounds one = SubtreeHitEnvelope(*child);
        one.x += child->GetAbsX() - baseX;
        one.y += child->GetAbsY() - baseY;
        layout = UnionBounds(layout, one);
    }

    // ② 两个状态各转一次取并集：抽屉开着和关着差一个身位，只按当前状态登记的话，
    //    拉出来之后表里就找不到它了（那是"偶尔点不中"里最难查的一种）。
    const float centerX = static_cast<float>(width) * 0.5f;
    const float centerY = static_cast<float>(height) * 0.5f;
    return UnionBounds(TransformBounds(layout, closed.transform, centerX, centerY),
                       TransformBounds(layout, open.transform, centerX, centerY));
}

}  // namespace inking::detail
