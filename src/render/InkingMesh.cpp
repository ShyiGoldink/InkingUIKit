// 形状几何核的实现（`include/render/InkingMesh.h`）。
//
// 各函数的契约（@param / @return / 为什么这么选）都写在头文件里，这里只留实现。
//
// 一段历史：这一整块原先和 `fillShape` 一起住在 `InkingDraw.cpp` 的匿名
// 名字空间里。拆出来的唯一理由是**它不需要 SDL**——而"轮廓采得对不对"是
// 必须能单独断言的一件事（`AppendContour` 的轮廓和 `ShapeContains` 的判定
// 是同源关系，画错了和点错了是同一个 bug 的两副面孔）。

#include <render/InkingMesh.h>

#include <algorithm>
#include <cmath>

namespace inking::detail {

namespace {

/**
 * 追加一段椭圆弧上的点。
 *
 * @param includeEnd 是否包含终点。圆 / 椭圆的**整圈**不包含（否则首尾重合）；
 *                   圆角矩形的每段 1/4 弧包含两端——相邻两段弧的端点就是
 *                   它们之间那段直边的两个端点，于是直边不用单独生成。
 */
void AppendArc(std::vector<OutlinePoint>& out, float cx, float cy, float rx,
               float ry, float startAngle, float endAngle, int steps,
               bool includeEnd) {
    const float safeSteps = static_cast<float>(steps > 0 ? steps : 1);
    const int count = includeEnd ? steps + 1 : steps;
    for (int i = 0; i < count; ++i) {
        const float t = startAngle
                      + (endAngle - startAngle)
                            * (static_cast<float>(i) / safeSteps);
        out.push_back(OutlinePoint{cx + std::cos(t) * rx,
                                   cy + std::sin(t) * ry});
    }
}

/**
 * @brief 追加一个**轴对齐矩形**的四个角（顺时针，屏幕坐标 y 向下）。
 */
void AppendBox(std::vector<OutlinePoint>& out, float cx, float cy, float halfW,
               float halfH) {
    out.push_back(OutlinePoint{cx - halfW, cy - halfH});
    out.push_back(OutlinePoint{cx + halfW, cy - halfH});
    out.push_back(OutlinePoint{cx + halfW, cy + halfH});
    out.push_back(OutlinePoint{cx - halfW, cy + halfH});
}

/// ear clipping 的工作列表（复用缓冲，避免每帧分配）。
std::vector<int>& earClipScratch() {
    static std::vector<int> work;
    return work;
}

/// 三个点的叉积（屏幕坐标；符号与绕向一致即可，不需要"数学上的正负"）。
float CrossOf(const OutlinePoint& a, const OutlinePoint& b,
              const OutlinePoint& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/// 点是否落在三角形里（含边界；与三角形绕向无关）。
bool PointInTriangle(const OutlinePoint& p, const OutlinePoint& a,
                     const OutlinePoint& b, const OutlinePoint& c) {
    const float d1 = CrossOf(a, b, p);
    const float d2 = CrossOf(b, c, p);
    const float d3 = CrossOf(c, a, p);
    const bool hasNegative = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
    const bool hasPositive = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
    return !(hasNegative && hasPositive);
}

}  // namespace

int StepsForLength(float pixels, int minSteps, int maxSteps) {
    const int raw = static_cast<int>(std::ceil(pixels / 2.0f));
    return std::clamp(raw, minSteps, maxSteps);
}

bool SameShape(const ShapeSpec& a, const ShapeSpec& b) {
    if (a.kind != b.kind || a.radius != b.radius
        || a.thickness != b.thickness || a.cap != b.cap
        || a.sides != b.sides || a.rotation != b.rotation
        || a.chamfer != b.chamfer || a.points != b.points
        || a.innerRatio != b.innerRatio || a.exponent != b.exponent
        || a.startAngle != b.startAngle || a.sweepAngle != b.sweepAngle
        || a.pathPointCount != b.pathPointCount) {
        return false;
    }
    // 顶点表按**内容**比：借的是视图，指针相等说明不了形状相同；
    // 不比内容就会"换了路径、画面还是旧的"。
    for (int i = 0; i < a.pathPointCount * 2; ++i) {
        if (a.pathPoints[i] != b.pathPoints[i]) {
            return false;
        }
    }
    return true;
}

int ContourSteps(const ShapeSpec& scaled, float w, float h) {
    const float shorter = w < h ? w : h;
    switch (scaled.kind) {
        case ShapeKind::RoundedRect: {
            const float r = std::clamp(scaled.radius, 0.0f, shorter * 0.5f);
            // 每段 1/4 弧的弧长 = r * π/2。
            return StepsForLength(r * (kPi * 0.5f), kMinArcSteps, kMaxArcSteps);
        }
        case ShapeKind::Circle:
            return StepsForLength(kPi * shorter, kMinCircleSteps,
                                  kMaxCircleSteps);
        case ShapeKind::Ellipse:
            // 椭圆周长没有初等闭式；π(a+b) 是个够用的近似（介于 2πa 与 2πb 之间）。
            return StepsForLength(kPi * (w + h) * 0.5f, kMinCircleSteps,
                                  kMaxCircleSteps);
        case ShapeKind::Capsule: {
            // 两段半圆，每段弧长 π·r（r = 短边的一半）。
            const float r = shorter * 0.5f;
            return StepsForLength(kPi * r, kMinArcSteps, kMaxCircleSteps);
        }
        case ShapeKind::Line: {
            // 只有圆头才有弧；平头 / 方头是矩形（用不到 steps）。
            const float r = scaled.thickness * 0.5f;
            return StepsForLength(kPi * r, kMinArcSteps, kMaxCircleSteps);
        }
        case ShapeKind::Superellipse:
            // 周长没有简单闭式：n = 2 时是椭圆的 π(a+b)/2，n 大时逼近矩形。
            return StepsForLength(w + h, kMinCircleSteps, kMaxCircleSteps);
        case ShapeKind::Polygon:
            // 每条边都是直的：点数就是 sides，不按弧长估（也就不会写死密度）。
            return kMinCircleSteps;
        case ShapeKind::ChamferRect:
        case ShapeKind::Star:
        case ShapeKind::Arc:
        case ShapeKind::PolygonPath:
        case ShapeKind::Rect:
        default:
            // 点数固定（8 个角 / 2×points 个顶点 / 自建网格 / 顶点表 / 4 个角）：
            // 用不到 steps。
            return kMinCircleSteps;
    }
}

void AppendContour(std::vector<OutlinePoint>& out, const ShapeSpec& shape,
                   float w, float h, int steps, float offset) {
    const float shorter = w < h ? w : h;
    const float halfW = w * 0.5f;
    const float halfH = h * 0.5f;

    switch (shape.kind) {
        case ShapeKind::RoundedRect: {
            // 半径夹取在这里就地做（float 版）：ShapeSpec::ClampRadius 收的是
            // 整数尺寸，而这里的 w/h 已经乘过展示倍率，不该再截断回整数。
            const float base = std::clamp(shape.radius, 0.0f, shorter * 0.5f);

            // 角**心**在原地不动，动的是半径：外扩时半径变大、内缩时变小。
            // 这段偏移曲线正好等价于"四条直边各平移 offset、角上用圆弧接上"。
            // 半径被磨到 0 之后（内缩超过圆角半径）角心要跟着走到 -offset：
            // 那时形状已经退化成矩形，尖角就在内缩后的框角上。
            const float corner = std::max(base, -offset);
            const float r = std::max(0.0f, base + offset);

            // 四段 1/4 弧。相邻两段的端点连线就是四条直边，所以直边
            // 不需要单独生成点——多生成反而会和弧的端点重合。
            AppendArc(out, corner, corner, r, r, kPi, 1.5f * kPi, steps, true);
            AppendArc(out, w - corner, corner, r, r, 1.5f * kPi, 2.0f * kPi,
                      steps, true);
            AppendArc(out, w - corner, h - corner, r, r, 0.0f, 0.5f * kPi,
                      steps, true);
            AppendArc(out, corner, h - corner, r, r, 0.5f * kPi, kPi, steps,
                      true);
            break;
        }

        case ShapeKind::Circle: {
            // 正圆：直径取 min(w,h)，**居中**（和 ShapeSpec 的语义一致）。
            const float radius = std::max(0.0f, shorter * 0.5f + offset);
            AppendArc(out, halfW, halfH, radius, radius, 0.0f, 2.0f * kPi,
                      steps, false);
            break;
        }

        case ShapeKind::Ellipse: {
            AppendArc(out, halfW, halfH, std::max(0.0f, halfW + offset),
                      std::max(0.0f, halfH + offset), 0.0f, 2.0f * kPi, steps,
                      false);
            break;
        }

        case ShapeKind::Capsule: {
            // 用满 w×h：半径取短边的一半，**脊椎线段**沿长轴放，两端各一个半圆。
            // 往外长就是半径变大（两端圆心不动），上下/左右两条直边跟着平移。
            const float base = shorter * 0.5f;
            const float r = std::max(0.0f, base + offset);
            if (w >= h) {
                // 横放：先右端（上→右→下），再左端（下→左→上）；
                // 两段弧的端点之间就是上下两条直边。
                AppendArc(out, w - base, halfH, r, r, 1.5f * kPi, 2.5f * kPi,
                          steps, true);
                AppendArc(out, base, halfH, r, r, 0.5f * kPi, 1.5f * kPi, steps,
                          true);
            } else {
                // 竖放：先下端（右→下→左），再上端（左→上→右）。
                AppendArc(out, halfW, h - base, r, r, 0.0f, kPi, steps, true);
                AppendArc(out, halfW, base, r, r, kPi, 2.0f * kPi, steps, true);
            }
            break;
        }

        case ShapeKind::Line: {
            // 水平线段、纵向居中。平头 / 方头是矩形，圆头是两端各一个半圆。
            const float half = shape.thickness * 0.5f;
            if (shape.cap == LineCap::Round) {
                const float r = std::max(0.0f, half + offset);
                AppendArc(out, w, halfH, r, r, 1.5f * kPi, 2.5f * kPi, steps,
                          true);
                AppendArc(out, 0.0f, halfH, r, r, 0.5f * kPi, 1.5f * kPi, steps,
                          true);
                break;
            }
            const float extra = shape.cap == LineCap::Square ? half : 0.0f;
            AppendBox(out, halfW, halfH, halfW + extra + offset,
                      std::max(0.0f, half + offset));
            break;
        }

        case ShapeKind::Polygon: {
            // 正多边形每条边都是直的：点数就是边数，不需要按弧长估采样密度。
            // 往外偏移 offset 等价于**内切圆半径**加 offset，也就是外接圆半径
            // 加 offset / cos(π/n)。
            // 外接圆半径就是 min(w,h)/2（和 SDF / 命中那边同一个约定）。
            // 这里**直接算**，别再走 `ShapePolygonCircumradius(int,int)`——
            // 那会把乘过展示倍率的 w/h 截断回整数，倍率是小数时对不上。
            const float base = shorter * 0.5f;
            const int sides = inking::detail::ShapePolygonSides(shape);
            const float apothemScale =
                std::cos(kPi / static_cast<float>(sides));
            const float r = std::max(0.0f, base + offset / apothemScale);

            const float angleStep = 2.0f * kPi / static_cast<float>(sides);
            const float start = shape.rotation * inking::detail::shapeDegToRad;
            for (int i = 0; i < sides; ++i) {
                const float angle = start + angleStep * static_cast<float>(i);
                out.push_back(OutlinePoint{halfW + std::cos(angle) * r,
                                           halfH + std::sin(angle) * r});
            }
            break;
        }

        case ShapeKind::ChamferRect: {
            // 八条边（四条直边 + 四条 45° 斜边）。沿法向内缩 offset 等价于
            // **每条边各向内平移 offset**：直边是半轴减 offset，斜边的切点
            // 也跟着挪——于是切角长度要跟着缩，缩到 0 就退化成矩形。
            const float halfWidth = w * 0.5f - offset;
            const float halfHeight = h * 0.5f - offset;
            const float chamfer = std::clamp(
                inking::detail::ShapeChamferAmount(shape.chamfer, w, h), 0.0f,
                std::max(0.0f, std::min(halfWidth, halfHeight)));

            // 顺时针，从左上角那条斜边开始（屏幕 y 向下）。
            out.push_back(OutlinePoint{halfW - halfWidth + chamfer,
                                       halfH - halfHeight});
            out.push_back(OutlinePoint{halfW + halfWidth - chamfer,
                                       halfH - halfHeight});
            out.push_back(OutlinePoint{halfW + halfWidth,
                                       halfH - halfHeight + chamfer});
            out.push_back(OutlinePoint{halfW + halfWidth,
                                       halfH + halfHeight - chamfer});
            out.push_back(OutlinePoint{halfW + halfWidth - chamfer,
                                       halfH + halfHeight});
            out.push_back(OutlinePoint{halfW - halfWidth + chamfer,
                                       halfH + halfHeight});
            out.push_back(OutlinePoint{halfW - halfWidth,
                                       halfH + halfHeight - chamfer});
            out.push_back(OutlinePoint{halfW - halfWidth,
                                       halfH - halfHeight + chamfer});
            break;
        }

        case ShapeKind::Star: {
            // 2×points 个顶点（外、内交替）。沿法向偏移在这里近似成"半径各加减
            // offset"——尖角处会略窄一点，但羽化带只有 1 像素，看不出来。
            const int points = inking::detail::ShapeStarPoints(shape);
            const int count = points * 2;
            const float outer =
                std::max(0.0f, shorter * 0.5f + offset);
            const float inner = std::max(
                0.0f, shorter * 0.5f
                          * std::clamp(shape.innerRatio, 0.0f, 1.0f) + offset);
            const float step = 2.0f * kPi / static_cast<float>(count);
            const float start = shape.rotation * inking::detail::shapeDegToRad;
            for (int i = 0; i < count; ++i) {
                const float radius = (i % 2) == 0 ? outer : inner;
                const float angle = start + step * static_cast<float>(i);
                out.push_back(OutlinePoint{halfW + std::cos(angle) * radius,
                                           halfH + std::sin(angle) * radius});
            }
            break;
        }

        case ShapeKind::Superellipse: {
            // 参数化：x = a·sgn(cos t)·|cos t|^(2/n)，y 同理。n = 2 时就是椭圆
            // （|cos|^1），n 大时点会往四个"角"聚，正好是要的形状。
            const float a = std::max(0.0f, halfW + offset);
            const float b = std::max(0.0f, halfH + offset);
            const float n = inking::detail::ShapeSuperellipseExponent(shape);
            const float power = 2.0f / n;
            const float safeSteps = static_cast<float>(steps > 0 ? steps : 1);
            for (int i = 0; i < steps; ++i) {
                const float t =
                    2.0f * kPi * static_cast<float>(i) / safeSteps;
                const float ct = std::cos(t);
                const float st = std::sin(t);
                const float px = std::copysign(std::pow(std::fabs(ct), power), ct);
                const float py = std::copysign(std::pow(std::fabs(st), power), st);
                out.push_back(OutlinePoint{halfW + px * a, halfH + py * b});
            }
            break;
        }

        case ShapeKind::Ring:
        case ShapeKind::Arc:
            // 圆环与圆弧带有孔（圆弧带的圆心落在孔里），走不了"外圈 + 内圈 +
            // 中心扇形"这套——`BuildShapeMesh` 里各自单独建网格。这里留空是兜底：
            // 万一漏进这条分支，下面的点数检查会拦住，宁可不画也别画成一个实心圆。
            break;

        case ShapeKind::PolygonPath: {
            // 任意多边形：按顶点表生成，偏移走**角平分线**（miter）——每条边各
            // 平移 offset，顶点取相邻两条边的交点。尖角处 miter 会被拉得很长，
            // 所以夹到 2×|offset|（超过就退化成"按一条边的法向平移"）。
            const int count = shape.pathPointCount;
            if (count < 3 || shape.pathPoints == nullptr) {
                break;
            }

            // 绕向：屏幕坐标 y 向下，shoelace > 0 就是"视觉上的顺时针"。
            float twiceArea = 0.0f;
            for (int i = 0; i < count; ++i) {
                const int next = (i + 1) % count;
                twiceArea += shape.pathPoints[i * 2]
                                 * shape.pathPoints[next * 2 + 1]
                           - shape.pathPoints[next * 2]
                                 * shape.pathPoints[i * 2 + 1];
            }
            const float orientation = twiceArea >= 0.0f ? 1.0f : -1.0f;

            // 边 i→i+1 的**外**法向（单位向量）。
            const auto edgeNormal = [&](int i) {
                const int j = (i + 1) % count;
                const float dx = shape.pathPoints[j * 2]
                               - shape.pathPoints[i * 2];
                const float dy = shape.pathPoints[j * 2 + 1]
                               - shape.pathPoints[i * 2 + 1];
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length <= 0.0f) {
                    return ShapePoint{0.0f, 0.0f};
                }
                return ShapePoint{orientation * dy / length,
                                  orientation * (-dx) / length};
            };

            const float miterLimit = std::fabs(offset) * 2.0f;
            for (int i = 0; i < count; ++i) {
                const ShapePoint n1 = edgeNormal((i + count - 1) % count);
                const ShapePoint n2 = edgeNormal(i);
                const float bx = n1.x + n2.x;
                const float by = n1.y + n2.y;
                const float blend = std::sqrt(bx * bx + by * by);
                float moveX = 0.0f;
                float moveY = 0.0f;
                if (blend > 1e-4f) {
                    // |n1 + n2| = 2·cos(θ/2) ⇒ miter 长度 = offset / cos(θ/2)。
                    const float miter = std::clamp(2.0f * offset / blend,
                                                   -miterLimit, miterLimit);
                    moveX = bx / blend * miter;
                    moveY = by / blend * miter;
                }
                out.push_back(OutlinePoint{shape.pathPoints[i * 2] + moveX,
                                           shape.pathPoints[i * 2 + 1] + moveY});
            }
            break;
        }

        case ShapeKind::Rect:
        default: {
            AppendBox(out, halfW, halfH, halfW + offset, halfH + offset);
            break;
        }
    }
}

void AppendEarClipIndices(const std::vector<OutlinePoint>& ring,
                          std::vector<int>& indices, int base) {
    const int count = static_cast<int>(ring.size());
    if (count < 3) {
        return;
    }

    float twiceArea = 0.0f;
    for (int i = 0; i < count; ++i) {
        const int next = (i + 1) % count;
        twiceArea += ring[i].x * ring[next].y - ring[next].x * ring[i].y;
    }
    const bool positiveArea = twiceArea > 0.0f;

    std::vector<int>& work = earClipScratch();
    work.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        work[static_cast<std::size_t>(i)] = i;
    }

    int guard = 0;
    while (work.size() > 2 && guard <= count * count) {
        const int size = static_cast<int>(work.size());
        bool clipped = false;
        for (int i = 0; i < size; ++i) {
            const int prev = work[static_cast<std::size_t>((i + size - 1) % size)];
            const int current = work[static_cast<std::size_t>(i)];
            const int next = work[static_cast<std::size_t>((i + 1) % size)];

            // 只切"凸"的角（与整圈绕向一致的那个方向）。
            const float cross = CrossOf(ring[static_cast<std::size_t>(prev)],
                                        ring[static_cast<std::size_t>(current)],
                                        ring[static_cast<std::size_t>(next)]);
            const bool convex = positiveArea ? (cross > 0.0f) : (cross < 0.0f);
            if (!convex) {
                continue;
            }

            // 耳朵里不能包着别的顶点（否则切下去就切掉了别人的地盘）。
            bool swallowsVertex = false;
            for (int j = 0; j < size; ++j) {
                const int index = work[static_cast<std::size_t>(j)];
                if (index == prev || index == current || index == next) {
                    continue;
                }
                if (PointInTriangle(ring[static_cast<std::size_t>(index)],
                                    ring[static_cast<std::size_t>(prev)],
                                    ring[static_cast<std::size_t>(current)],
                                    ring[static_cast<std::size_t>(next)])) {
                    swallowsVertex = true;
                    break;
                }
            }
            if (swallowsVertex) {
                continue;
            }

            indices.push_back(base + prev);
            indices.push_back(base + current);
            indices.push_back(base + next);
            work.erase(work.begin() + i);
            clipped = true;
            break;
        }
        ++guard;
        if (!clipped) {
            break;  // 找不到耳朵了（退化多边形）：收手
        }
    }
}

}  // namespace inking::detail
