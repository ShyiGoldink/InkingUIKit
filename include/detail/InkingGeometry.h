#pragma once

// 几何辅助（`detail`）：**节点几何的复合、裁剪换算、包络合并**。
//
// 这些是**自由函数**，不属于任何一个类——它们回答的是"这棵子树的几何在设备像素
// 里长什么样"，而这个问题同时被绘制、命中、裁剪、包络四处问。
//
// 放在独立头里的理由（而不是塞在某个类的头里，旧仓库就是那么放的）：
//   - 它们是**跨类**的工具：Widget 自己、容器组件、画布、裁剪链都要用；
//   - 收在一处**只有一个口径**："几何怎么叠、怎么换算成设备像素"改一次，
//     四个调用方一起跟上——抄第二遍就会分叉（`SKILL.md` §4 的老规矩）。
//
// 注意本头**不包含** `widget/InkingWidget.h`（那会形成循环包含）：参数只用到
// 前向声明的 `InkingWidget`（声明里只需要类型名，不需要完整定义）。
// 实现见 `src/detail/InkingGeometry.cpp`，那一份才包含 Widget 的头。

#include <dataStruct/InkingAffine.h>
#include <dataStruct/InkingShapeSpec.h>

namespace inking {

class InkingWidget;

namespace detail {

/**
 * 从根到 `node` 的**祖先子树变换**复合（绝对设计坐标下的相似仿射，T-7）。
 *
 * 每个祖先的贡献都在**它自己的绝对中心**上作用（和它自己那一笔的绘制同源），
 * 所以这个仿射作用在"未经变换的布局坐标"上，就能把整棵子树搬到该去的地方。
 * 绘制过它、命中过它的逆——两边必须同源，所以放在一处算。
 */
Affine AncestorAffine(const InkingWidget& node) noexcept;

/**
 * 从根到 `node`（**含 `node` 自己的子树变换**）的复合仿射（A5）。
 *
 * 它就是"对 `node` 的子节点生效的那一份"（等于 `AncestorAffine(child)`）：
 * 容器的裁剪区要随**容器自己**一起走，而**不随子孙**走——所以算裁剪矩形时
 * 用的是这一份，不是 `AncestorAffine(node)`（那会漏掉容器自己的开合变换，
 * 抽屉一滑，裁剪区就留在原地）。
 */
Affine SubtreeChainAffine(const InkingWidget& node) noexcept;

/**
 * `node` 的**裁剪链**仿射：祖先那一份 + `node` 自己的 `GetClipTransform()`。
 *
 * 和 `SubtreeChainAffine` 只差一处：**自己那一层用裁剪变换**。
 * 为什么必须分开（B12 踩的坑）：滚动容器把滚动位移放进了
 * `GetSubtreeTransform()`（内容要跟着滚），而裁剪区若读那一份，裁剪窗口就会
 * 跟着内容一起跑——已经在框外的内容永远看不见、框内的内容滚出去还可见。
 * 祖先链照旧用各自的子树变换（祖先滚了，这个容器连同它的裁剪窗口一起动，那是对的）。
 */
Affine ClipChainAffine(const InkingWidget& node) noexcept;

/**
 * 节点 `node` 的**祖先裁剪交集**，换算成"提交给渲染器的**设备像素**坐标"（A5）。
 *
 * 坐标系与提交层那份顶点完全一致（本地设备像素 + 绝对设备像素 + 祖先仿射），
 * 所以裁剪矩形和图形天然对齐——不管 SDL 把 clip 当逻辑坐标还是设备像素，
 * 两者都在同一个空间里被同一套变换处理。
 *
 * @return true = 有祖先裁剪它，`out` 是交集；false = 没有被任何祖先剪（`out` 不变）。
 */
bool AncestorClipInDevice(const InkingWidget& node, ShapeBounds& out) noexcept;

/**
 * 整棵子树（含自己）的命中包络并集，坐标**相对 `node` 左上角**（T-7）。
 *
 * 容器组件在自己的 `GetHitEnvelope()` 里调它一下就行——子节点不进表之后，
 * "这条包络要盖住哪儿"由容器自己回答。
 */
ShapeBounds SubtreeHitEnvelope(const InkingWidget& node) noexcept;

/**
 * 两个矩形的交集（某一维不重叠时该维夹到 0 = 什么都不画）。
 *
 * 为什么要有这么一个朴素的小函数：多处都要"把两层裁剪合起来"（祖先裁剪链、
 * 画布的视口裁剪、画布自己 `PushClip` 的嵌套）。各写一遍的话，迟早有一处忘了
 * 对"负宽高"收口——那会让一个空裁剪变成一个大得离谱的反向矩形，于是"什么都
 * 不画"变成"什么都画"（并且只在两层裁剪不相交时才现形）。
 */
inline ShapeBounds IntersectBounds(const ShapeBounds& a,
                                   const ShapeBounds& b) noexcept {
    const float minX = a.x > b.x ? a.x : b.x;
    const float minY = a.y > b.y ? a.y : b.y;
    const float aMaxX = a.x + a.width;
    const float bMaxX = b.x + b.width;
    const float aMaxY = a.y + a.height;
    const float bMaxY = b.y + b.height;
    const float maxX = aMaxX < bMaxX ? aMaxX : bMaxX;
    const float maxY = aMaxY < bMaxY ? aMaxY : bMaxY;
    const float w = maxX - minX;
    const float h = maxY - minY;
    return ShapeBounds{minX, minY, w > 0.0f ? w : 0.0f, h > 0.0f ? h : 0.0f};
}

/**
 * 一个**本地矩形**经过 `transform`（绕 `(cx, cy)`）之后的**轴对齐包围盒**（A13）。
 *
 * 用途：给"认外部显示变换"的组件算命中包络（`GetHitEnvelope()`）——组件被挪走 /
 * 放大之后，包络必须跟着走：表里登记的仍是老位置的话，拖过去就**点不到**了。
 *
 * 做法是四个角各过一次 `ApplyTransform` 再取 min/max。形状被旋转时包围盒会变大，
 * 那是**对的**：包络本来就允许保守（表只负责缩小候选范围，命中与否仍由节点的
 * `HitTest` 精判）。
 */
inline ShapeBounds TransformBounds(const ShapeBounds& local,
                                   const TransformSpec& transform, float cx,
                                   float cy) noexcept {
    if (transform.IsIdentity()) {
        return local;  // 没变换：原样返回（绝大多数组件的路径，零开销）
    }
    const Point2 corners[4] = {
        ApplyTransform(transform, local.x, local.y, cx, cy),
        ApplyTransform(transform, local.x + local.width, local.y, cx, cy),
        ApplyTransform(transform, local.x, local.y + local.height, cx, cy),
        ApplyTransform(transform, local.x + local.width,
                       local.y + local.height, cx, cy)};

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
 * 把一个**本地矩形**（相对 `node` 左上角，设计坐标）换算成"提交给渲染器的
 * **设备像素**坐标"矩形（A12）。
 *
 * 两步与 `AncestorClipInDevice` 里那段完全一样，也与提交层的顶点换算同构：
 *   ① 本地矩形 + 节点的绝对布局位置，再乘展示倍率 → 设备像素；
 *   ② 套**裁剪链仿射**（`ClipChainAffine`：含节点自己那一层，且滚动容器用
 *      "不含滚动位移"的那一份），再取 AABB（旋转 / 缩放之后就只剩包围盒了）。
 *
 * 抽成函数是因为**现在有两个调用方**：容器裁剪链（`AncestorClipInDevice`）与
 * 画布自己的内容视口（A12）。抄第二遍就会分叉——"画被剪的位置"和"点被剪的
 * 位置"分家就是这么来的（`AGENTS.md` §6 第 62 条）。
 *
 * @param localBounds 本地矩形；宽或高 <= 0 时返回零矩形（= 什么都不画）。
 */
ShapeBounds ClipBoundsInDevice(const InkingWidget& node,
                               const ShapeBounds& localBounds) noexcept;

}  // namespace detail
}  // namespace inking
