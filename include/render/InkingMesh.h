#pragma once

// 形状几何核（`detail`）：**形状 → 轮廓点**，以及**简单多边形 → 索引**。
//
// 它是渲染层里**唯一不碰 SDL 的那一半**，所以单独成活：
//   - 轮廓采样与三角化都是纯算术，错了的表现却是"圆角按钮的边是锯齿"或
//     "画在这儿、点在别处"——这类问题必须能**不起窗口**就断言；
//   - 它回答的和 `ShapeContains`（`dataStruct/InkingShapeSpec.h`）是**同一个
//     几何问题**：`AppendContour` 采出来的轮廓与 `ShapeContains` 判的内外必须
//     同源，否则就是"画的圆角、点的是直角"那种只在角落露头的错。两边同层，
//     对账才写得出来。
//
// 谁在用：`render/InkingDraw.cpp` 的填充（`BuildShapeMesh`）与描边
// （`strokeShape`）。UI 层别直接用——它是框架内部件。

#include <dataStruct/InkingShapeSpec.h>

#include <vector>

namespace inking::detail {

/// 轮廓上的一个点，设备像素、相对组件左上角。
struct OutlinePoint {
    float x = 0.0f;
    float y = 0.0f;
};

/// π（float）。轮廓采样、环形网格、圆弧都用它。
inline constexpr float kPi = 3.14159265358979323846f;

/// 整圈（圆 / 椭圆）的采样段数上下限。
inline constexpr int kMinCircleSteps = 24;
inline constexpr int kMaxCircleSteps = 512;

/// 圆角矩形每段 1/4 弧的采样段数上下限。
inline constexpr int kMinArcSteps = 4;
inline constexpr int kMaxArcSteps = 64;

/**
 * 采样段数：按"每段大约 2 设备像素"估，再夹到上下限。
 *
 * 每段 2 像素是个折中：再密顶点白烧（一个按钮每帧重建顶点），
 * 再疏就有看得见的多边形棱角——1 像素的羽化带遮不住它。
 */
int StepsForLength(float pixels, int minSteps, int maxSteps);

/**
 * 轮廓采样密度：按形状的弧长估段数（直边形状用不到，给个最小值）。
 *
 * 抽出来是因为它现在有**两个**消费者：填充网格（`BuildShapeMesh`）与描边环带
 * （`strokeShape`）。两处必须给出**同一个**段数——描边是"两条轮廓之间的带子"，
 * 外圈与内圈点数不一致就没法按下标配对（自检里的"点数配对不上就不画"兜底）。
 */
int ContourSteps(const ShapeSpec& scaled, float width, float height);

/**
 * @brief 生成一个形状的轮廓（一圈闭合的点，顺时针）。
 *
 * 基准是 (0,0)-(w,h) 这个矩形，**不是**放大过的框。偏移不靠"先把框放大、
 * 再把点平移回来"凑——那个套路只有 rect / 圆 / 椭圆凑巧对得上：圆角矩形与
 * 胶囊是"半径 ± offset"、线段是"半轴 ± offset"、多边形是"内切圆半径 ±
 * offset"，各是各的算式，必须分开写。
 *
 * @param offset 沿**法向**的偏移：正 = 往外长（羽化带的外圈）、负 = 往里缩
 *               （内圈），单位是设备像素。外圈 alpha = 0、内圈 alpha = 源色，
 *               边界上自然插值出 1 像素宽的过渡带。
 *
 * **点数只由 kind、steps 与 sides 决定，与 offset 无关**：外圈和内圈要按
 * 下标配对。半径退化（0）时弧上的点会重合在一起，那只是零面积三角形，无害。
 */
void AppendContour(std::vector<OutlinePoint>& out, const ShapeSpec& shape,
                   float width, float height, int steps, float offset);

/**
 * 形状定义是不是同一个（顶点缓存的键之一）。
 *
 * **所有**影响几何的字段都要比——漏一个就是"形状换了、画面没变"（缓存命中），
 * 而且那种错只在某一个字段上露头。B5/B3 加字段时这里漏过一次（`chamfer` /
 * `points` / `innerRatio` / `exponent` / `startAngle` / `sweepAngle` 当时都没比），
 * 靠"静态组件的形状在运行期不变"侥幸没出事；B2 补全的时候一起修掉了。
 */
bool SameShape(const ShapeSpec& a, const ShapeSpec& b);

/**
 * 把一个**简单多边形**（一圈顶点）三角化成三角面片，索引追加到 `indices`
 * （顶点下标要加 `base`）。
 *
 * 为什么需要它：填充那条路原先只服务**凸形状**（"中心 + 内圈相邻两点"的扇形
 * 三角化），而 B2 的任意多边形可以是凹的、中心甚至落在形状外（U 形）——扇形
 * 会把它填实。ear clipping 是 O(n²) 的经典解法，点数少时够用。
 *
 * 退化情形（自交 / 重点 / 面积为零）会提前收手：**宁可不画完也不死循环**。
 */
void AppendEarClipIndices(const std::vector<OutlinePoint>& ring,
                          std::vector<int>& indices, int base);

}  // namespace inking::detail
