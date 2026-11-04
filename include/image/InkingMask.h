#pragma once

// 形状遮罩（dev010 T-05）：把一张方图裁成任意 SDF 形状。
//
// 为什么需要：`PushClip` 只认矩形，`InkingStaticImage` 的"按形状裁"是**形状套图**
// （形状网格自己决定哪些顶点存在）——那是"形状是主体、图填进去"。反过来
// "图是主体、用一个形状当遮罩"（圆形头像、不规则立绘、技能范围高亮）现在做不到。
//
// 难点**不在提交**（T-03 已经能把任意顶点交出去了），在**几何**：
//
//   一个 100×100 的方图只有 4 个顶点，全在圆外 ⇒ 如果只把 SDF 烘进这 4 个顶点
//   的 alpha，整块会变成完全透明（四个角都在外，插值出来也全是 0）。这就是最
//   容易"看起来做了"的假实现。
//
// 所以必须**先细分再烘**：按网格把四边形切成 N×N 格，每个顶点算一次 SDF，
// `alpha = clamp(0.5 - f, 0, 1)`（与 `fillShape` 的羽化带同一条公式），
// 边缘因此有约 1 像素的过渡。判据直接调 `InkingSignedDistance`
// （`dataStruct/InkingShapeSpec.h`）——那是"画"和"点"共用的同一份定义，
// 自己另写一份就会出现"裁的圆和点的圆不是一个圆"。
//
// 代价要说清楚：一个图元从 4 顶点变成 (N+1)² 顶点、2N² 个三角形。
// N=16 时 289 顶点 / 512 三角形，比不遮罩贵两个数量级。**静态图元用它，动态的
// 每帧重建遮罩网格会很痛**（回退路径见任务书 T-05）。

#include <dataStruct/InkingShapeSpec.h>
#include <image/InkingGeometryLayer.h>

#include <cstdint>
#include <vector>

/**
 * @brief 遮罩细分出来的网格（可以直接喂给 `InkingGeometryLayer::SetGeometry`）。
 */
struct InkingMaskedQuad {
    std::vector<InkingVertex> vertices;
    std::vector<std::uint16_t> indices;
    /// 请求的细分密度被夹到上限了（画面只是边缘糙一点，不是错误）。
    bool clamped = false;

    bool Empty() const noexcept { return vertices.empty() || indices.empty(); }
};

/// 细分密度的上限：`(64+1)² = 4225` 顶点，仍低于提交层的单批上限 16384。
inline constexpr int InkingMaskMaxDensity = 64;

/**
 * @brief 把 `quad` 按 `mask` 的形状裁出来。
 *
 * @param quad          源四边形。用它的 `dst`（设计坐标）、`src`（纹理像素矩形）、
 *                      `tint`；**`rotation` 被忽略**——遮罩要在形状自己的坐标系里
 *                      算 SDF，先转再裁会让"遮罩跟着图一起转"，那是另一个语义。
 * @param mask          遮罩形状（与命中/描边同一份定义）。
 * @param textureWidth  纹理像素宽（算 UV 用；传 <= 0 时按整张图 0~1 处理）。
 * @param textureHeight 同上。
 * @param maskWidth     遮罩铺开的宽（通常 = `quad.dst.width`）。
 * @param maskHeight    同上。
 * @param density       每边细分几格。<= 0 用 16；> `InkingMaskMaxDensity` 夹到上限，
 *                      并把 `clamped` 置起来。
 *
 * @return 顶点 / 索引；`dst` 退化（宽或高 <= 0）时返回空网格。
 *
 * 坐标系：输出的 `x/y` 仍是**设计坐标、相对图层左上角**（与 `InkingQuad::dst`
 * 同一套），`u/v` 是 0~1 归一化——与 `InkingVertex` 的约定完全一致。
 */
InkingMaskedQuad InkingBuildMaskedQuad(const InkingQuad& quad,
                                       const InkingShapeSpec& mask,
                                       int textureWidth, int textureHeight,
                                       float maskWidth, float maskHeight,
                                       int density = 16);
