// 形状填充的实现：**把形状采样成多边形，交给 GPU 三角化**。
//
// 为什么不用逐像素：那是 CPU 光栅化，项目明令禁止（docs/AGENTS.md §3 第 3 条），
// 而且和"DrawCall 合批 + 静态烘焙"的方向相反。
//
// 为什么不用 SDL3 GPU API：那等于把整个渲染后端换掉，而 `SDL_RenderGeometry`
// 已经在现有后端上、够用且便宜（等真到了"合批也救不了"的那天再谈换后端）。
//
// 抗锯齿的做法：不是多重采样，而是**边缘羽化**——最外一圈顶点 alpha=0、
// 往里 1 像素的顶点 alpha=源色 alpha，中间自然插值出一条 0→1 的过渡带。
// 实测（software 渲染器）顶点 alpha 确实参与混合、而且会插值，
// 详细数值记在 InkingDraw.h 的注释里。
//
// 这个文件现在只管**顶点**这一层：形状 → 轮廓（`InkingMesh.cpp`）→ 带颜色 /
// 带 UV 的 SDL 顶点 → 交给提交层。轮廓采样与三角化刻意留在别处，理由见
// `include/render/InkingMesh.h` 顶上那段。

#include <render/InkingDraw.h>

#include <render/InkingBatch.h>
#include <render/InkingMesh.h>

#include <algorithm>
#include <cmath>

namespace inking::detail {

// ---------------------------------------------------------------------------
// 两个"整个进程只有一份"的槽位（槽本体搬出头的理由见 InkingDraw.h）
//
// 头内 inline 函数里的函数内静态量在**每个翻译单元各有一份**；动态库下
// exe 与 DLL 会各持一份，于是"外面设置渲染器 / 变换、DLL 里的绘制读不到"
// 会**静默**发生（不报错，只是画不出来）。所以槽本体落在这个 .cpp
// （它一定编进 DLL），头里只留一个声明。函数内静态量这个写法本身不变
// ——仍然避开静态初始化顺序问题（AGENTS §6 第 12 条）。
// ---------------------------------------------------------------------------

SDL_Renderer*& currentRendererSlot() {
    static SDL_Renderer* renderer = nullptr;
    return renderer;
}

Affine& currentAncestorAffineSlot() {
    static Affine affine;
    return affine;
}

namespace {

/**
 * 羽化带的宽度，**设备像素**。
 *
 * 1 像素是"刚好把硬台阶抹平"的量：再宽会让形状看起来发虚，
 * 再窄就盖不住多边形采样点的棱角。
 */
constexpr float kFeatherPixels = 1.0f;

/**
 * 把刚算好的**本地**顶点交给提交层（T-6）。有缓存槽就顺手存一份：
 * 下一帧形状/尺寸/倍率/变换没变，就直接复用，不必再采样一遍轮廓。
 */
void StoreAndAppend(VertexCacheSlot* slot, const ShapeSpec& scaled, int width,
                    int height, float mag, const TransformSpec& transform,
                    const std::vector<SDL_Vertex>& vertices,
                    const std::vector<int>& indices, float pixelX,
                    float pixelY, std::uint32_t color) {
    if (slot != nullptr) {
        slot->vertices = vertices;
        slot->indices = indices;
        slot->shape = scaled;
        slot->width = width;
        slot->height = height;
        slot->magnification = mag;
        slot->transform = transform;
        slot->color = color;  // 顶点里带颜色 ⇒ 颜色必须进键
        slot->valid = true;
    }
    AppendLocalGeometry(vertices.data(), vertices.size(), indices.data(),
                        indices.size(), pixelX, pixelY, /*reused=*/false);
}

// ---------------------------------------------------------------------------
// 复用缓冲
//
// 每个按钮每帧都要重建同一份顶点（形状其实没变），"每帧重新分配"是现在
// 最容易看见的浪费。用函数内静态量把容量留住，绘制全程在 UI 线程，不需要锁。
//
// 缓冲由**调用方拥有**（不放进 InkingMesh）：形状那几个函数是纯的，
// 谁调谁提供输出数组——它们不该共享一份全局状态。
//
// 真正的解法是"静态形状烘一次网格"（DrawCall 合批 + 静态烘焙那一轮），
// 那时这几个缓冲会连同 RenderGeometry 一起收进提交层。
// ---------------------------------------------------------------------------

/// 轮廓复用缓冲：填充要外圈 + 内圈**同时**在场，所以是两个。
std::vector<OutlinePoint>& outerContourScratch() {
    static std::vector<OutlinePoint> points;
    return points;
}

std::vector<OutlinePoint>& innerContourScratch() {
    static std::vector<OutlinePoint> points;
    return points;
}

std::vector<SDL_Vertex>& vertexScratch() {
    static std::vector<SDL_Vertex> vertices;
    return vertices;
}

std::vector<int>& indexScratch() {
    static std::vector<int> indices;
    return indices;
}

/// 顶点色：rgb 取源色，alpha 单独给（羽化带的两圈就是靠它区分的）。
SDL_FColor VertexColor(std::uint32_t color, float alpha) {
    return SDL_FColor{static_cast<float>(ColorRed(color)) / 255.0f,
                      static_cast<float>(ColorGreen(color)) / 255.0f,
                      static_cast<float>(ColorBlue(color)) / 255.0f, alpha};
}

SDL_Vertex MakeVertex(float x, float y, const SDL_FColor& color) {
    SDL_Vertex vertex;
    vertex.position = SDL_FPoint{x, y};
    vertex.color = color;
    vertex.tex_coord = SDL_FPoint{0.0f, 0.0f};  // 不用纹理，但别留垃圾值
    return vertex;
}

/**
 * 生成形状的**本地设备像素**顶点 + 索引（含边缘羽化、已过 `transform`）。
 *
 * 抽出来是因为它有**两个消费者**：无纹理的填充（`fillShape`）与带纹理的填充
 * （`fillShapeTextured`，B1 的"纹理按形状裁剪"）。两份实现各自演化就会出现
 * "颜色那条把圆角羽化对了、纹理那条忘了"这种只在某一个外观下露头的错。
 *
 * @param scaled 已经乘过展示倍率的形状（半径 / 线宽都换到设备像素了）。
 * @param w,h    组件尺寸，**设备像素**。
 * @param uv     非空时给每个顶点算纹理坐标与"图覆盖率"；**用变换前**的本地坐标算，
 *               这样纹理跟着形状一起转，而不是被转两次。
 * @return false = 这个形状没有面积 / 轮廓生成出了岔子（**宁可不画也别画错**）。
 */
bool BuildShapeMesh(const ShapeSpec& scaled, float w, float h, float mag,
                    std::uint32_t color, const TransformSpec& transform,
                    const ImageFitSpec* uv, std::vector<SDL_Vertex>& vertices,
                    std::vector<int>& indices) {
    const float shorter = w < h ? w : h;
    // 羽化带不许宽到把形状吃光：至少留一半实心区。
    const float feather = std::min(kFeatherPixels, shorter * 0.25f);
    // 羽化带**对称地跨在边界上**：外圈在形状外 half 像素处（alpha=0），
    // 内圈在形状内 half 像素处（alpha=1）。等价于标准 SDF 抗锯齿的
    // `alpha = clamp(0.5 - f, 0, 1)`（f 是到边界的带符号距离）。
    //
    // 为什么不把带子整条放在**边界内侧**：那样像素对齐的直边（面板、
    // 背景板、分割线这些最常见的矩形）最外面那一列像素会被算成覆盖一半，
    // 于是"一条本该实的边"变成半透明。对称放的话，边界正好落在像素边界上时
    // 覆盖率的判断是对的，斜边照样平滑。
    const float half = feather * 0.5f;

    // ---- 顶点变换：在**设备像素**空间里过一次仿射，绕的是组件中心 ----
    // 平移量按设计坐标写，所以先乘展示倍率；旋转与缩放跟单位无关。
    // 中心点（扇形三角化的那个点）也要过变换，否则形状会被拧歪。
    TransformSpec deviceTransform = transform;
    deviceTransform.translateX *= mag;
    deviceTransform.translateY *= mag;
    const float centerX = w * 0.5f;
    const float centerY = h * 0.5f;
    const auto transformPoint = [&](float x, float y) {
        return ApplyTransform(deviceTransform, x, y, centerX, centerY);
    };

    // 一个顶点 = 位置（过变换）+ 颜色（羽化 alpha）+ 可选的纹理坐标。
    // UV 取**变换前**的本地坐标：纹理要跟着形状一起转。
    //
    // 注意这里**不**处理 contain 的留白：形状网格的顶点只在轮廓上，图区域的边界
    // 在形状内部，靠逐顶点 alpha 表达不出来（那会变成"整块半透明"）。留白由
    // `fillShapeTextured` 里的**裁剪矩形**负责（形状 ∩ 图区域）。
    const auto makeVertex = [&](float rawX, float rawY, const SDL_FColor& c) {
        const Point2 point = transformPoint(rawX, rawY);
        SDL_Vertex vertex = MakeVertex(point.x, point.y, c);
        if (uv != nullptr) {
            uv->UvAt(rawX, rawY, vertex.tex_coord.x, vertex.tex_coord.y);
        }
        return vertex;
    };

    vertices.clear();
    indices.clear();

    // ---- 圆环：中间有孔，走不了"外圈 + 内圈 + 中心扇形"那条路 ----
    //
    // 环带的 alpha 只跟**到中心距离**有关（到环带上下两条边界的距离），在半径
    // 方向上是一条分段线性函数：外侧羽化 0→1、实心 1、内侧羽化 1→0。把它按
    // 折点采样成 3~4 圈顶点，圈与圈之间线性插值出来的就是那条精确的曲线。
    // 环宽不到 1 像素时实心段消失，合成一条"到中径取峰值"的折线。
    if (scaled.kind == ShapeKind::Ring) {
        const float outerRadius = shorter * 0.5f;
        // 环宽顶到外半径就停（再厚就没有孔了，退化成实心圆）。
        const float thickness = std::min(scaled.thickness, outerRadius);
        if (thickness <= 0.0f || outerRadius <= 0.0f) {
            return false;
        }
        const float innerRadius = outerRadius - thickness;
        const float midRadius = outerRadius - thickness * 0.5f;
        const float solidAlpha =
            static_cast<float>(ColorAlpha(color)) / 255.0f;

        struct RingSample {
            float radius = 0.0f;
            float alpha = 0.0f;
        };
        RingSample rings[4];
        int ringCount = 0;
        rings[ringCount++] = RingSample{outerRadius + half, 0.0f};
        if (outerRadius - half >= innerRadius + half) {
            rings[ringCount++] = RingSample{outerRadius - half, solidAlpha};
            rings[ringCount++] = RingSample{innerRadius + half, solidAlpha};
        } else {
            const float peak = std::clamp(0.5f + thickness * 0.5f, 0.0f, 1.0f);
            rings[ringCount++] = RingSample{midRadius, peak * solidAlpha};
        }
        // 没有孔（环宽夹到外半径）时最里面那圈直接给实心色：画出来就是实心圆，
        // 与 SDF 判"中心也在内"一致。
        rings[ringCount++] = RingSample{
            std::max(0.0f, innerRadius - half),
            innerRadius <= 0.0f ? solidAlpha : 0.0f};

        const int ringSteps = StepsForLength(2.0f * kPi * outerRadius,
                                             kMinCircleSteps, kMaxCircleSteps);

        for (int ring = 0; ring < ringCount; ++ring) {
            const SDL_FColor ringColor = VertexColor(color, rings[ring].alpha);
            for (int i = 0; i < ringSteps; ++i) {
                const float angle = 2.0f * kPi * static_cast<float>(i)
                                  / static_cast<float>(ringSteps);
                vertices.push_back(makeVertex(
                    centerX + std::cos(angle) * rings[ring].radius,
                    centerY + std::sin(angle) * rings[ring].radius, ringColor));
            }
        }

        for (int ring = 0; ring + 1 < ringCount; ++ring) {
            for (int i = 0; i < ringSteps; ++i) {
                const int next = (i + 1) % ringSteps;
                const int outerA = ring * ringSteps + i;
                const int outerB = ring * ringSteps + next;
                const int innerA = (ring + 1) * ringSteps + i;
                const int innerB = (ring + 1) * ringSteps + next;
                indices.push_back(outerA);
                indices.push_back(outerB);
                indices.push_back(innerB);
                indices.push_back(outerA);
                indices.push_back(innerB);
                indices.push_back(innerA);
            }
        }
        return true;
    }

    // ---- 圆弧带：环形网格的"一段" ----
    //
    // 与整环同一个套路（沿半径分圈、圈与圈之间铺四边形），多了两样：
    //   1. 角度只走一段，两端**径向切平**；
    //   2. 角的两个端头也要羽化 —— 首尾各补一列 alpha = 0 的顶点，
    //      列间距是"半像素"换算出来的角度（`half / midRadius`）。
    // 圆心落在孔里，所以"中心扇形三角化"那套在这里根本不成立——这也是圆弧
    // 必须单独建网格、不能只加一个 SDF 分支的原因。
    if (scaled.kind == ShapeKind::Arc) {
        const float outerRadius = shorter * 0.5f;
        const float thickness = std::min(
            scaled.thickness > 0.0f ? scaled.thickness : 0.0f, outerRadius);
        const float sweep = inking::detail::ShapeArcSweepRadians(scaled);
        if (thickness <= 0.0f || outerRadius <= 0.0f || sweep <= 0.0f) {
            return false;  // 零扫角没有面积
        }
        const float innerRadius = outerRadius - thickness;
        const float midRadius = outerRadius - thickness * 0.5f;
        const float direction = inking::detail::ShapeArcDirection(scaled);
        const float start = scaled.startAngle * inking::detail::shapeDegToRad;
        const float solidAlpha = static_cast<float>(ColorAlpha(color)) / 255.0f;

        // 径向圈：与整环那套折点完全一致（外羽化 / 实心外 / 实心内 / 内羽化）。
        struct ArcRing {
            float radius = 0.0f;
            float alpha = 0.0f;
        };
        ArcRing rings[4];
        int ringCount = 0;
        rings[ringCount++] = ArcRing{outerRadius + half, 0.0f};
        if (outerRadius - half >= innerRadius + half) {
            rings[ringCount++] = ArcRing{outerRadius - half, solidAlpha};
            rings[ringCount++] = ArcRing{innerRadius + half, solidAlpha};
        } else {
            const float peak = std::clamp(0.5f + thickness * 0.5f, 0.0f, 1.0f);
            rings[ringCount++] = ArcRing{midRadius, peak * solidAlpha};
        }
        rings[ringCount++] = ArcRing{std::max(0.0f, innerRadius - half),
                                     innerRadius <= 0.0f ? solidAlpha : 0.0f};

        // 角向的半像素羽化：中径很小的时候夹住，别把整段吃掉。
        float angularFeather = midRadius > 0.0f ? half / midRadius : 0.0f;
        angularFeather = std::min(angularFeather, sweep * 0.25f);

        const int arcSteps =
            StepsForLength(midRadius * sweep, kMinArcSteps, kMaxArcSteps);
        const int columnCount = arcSteps + 3;  // 0 / 实 / … / 实 / 0 五段以上
        const float solidSweep = sweep - 2.0f * angularFeather;
        const float end = start + direction * sweep;
        const auto columnAngle = [&](int column) {
            if (column == 0) {
                return start - direction * angularFeather;
            }
            if (column == 1) {
                return start + direction * angularFeather;
            }
            if (column >= columnCount - 2) {
                return column == columnCount - 2
                           ? end - direction * angularFeather
                           : end + direction * angularFeather;
            }
            const float t = static_cast<float>(column - 1)
                          / static_cast<float>(arcSteps);
            return start + direction * (angularFeather + solidSweep * t);
        };
        const auto columnAlpha = [&](int column) {
            return (column == 0 || column == columnCount - 1) ? 0.0f : 1.0f;
        };

        for (int ring = 0; ring < ringCount; ++ring) {
            for (int column = 0; column < columnCount; ++column) {
                const float angle = columnAngle(column);
                const float alpha = rings[ring].alpha * columnAlpha(column);
                vertices.push_back(makeVertex(
                    centerX + std::cos(angle) * rings[ring].radius,
                    centerY + std::sin(angle) * rings[ring].radius,
                    VertexColor(color, alpha)));
            }
        }

        for (int ring = 0; ring + 1 < ringCount; ++ring) {
            for (int column = 0; column + 1 < columnCount; ++column) {
                const int a = ring * columnCount + column;
                const int b = a + 1;
                const int c = (ring + 1) * columnCount + column;
                const int d = c + 1;
                indices.push_back(a);
                indices.push_back(b);
                indices.push_back(d);
                indices.push_back(a);
                indices.push_back(d);
                indices.push_back(c);
            }
        }
        return true;
    }

    // ---- 外轮廓与采样密度 ----
    const int steps = ContourSteps(scaled, w, h);

    // 外圈 = 边界往外 half，内圈 = 边界往里 half。偏移交给 AppendContour，
    // 尺寸始终是**真实的** w × h（不再"放大框再平移"）。
    std::vector<OutlinePoint>& outer = outerContourScratch();
    outer.clear();
    AppendContour(outer, scaled, w, h, steps, half);

    std::vector<OutlinePoint>& inner = innerContourScratch();
    inner.clear();
    AppendContour(inner, scaled, w, h, steps, -half);

    const std::size_t count = outer.size();
    if (count < 3 || inner.size() != count) {
        return false;  // 点数配对不上说明轮廓生成出了岔子，宁可不画也别画错
    }

    // ---- 顶点：外圈（alpha=0）、内圈（alpha=源色）、中心 ----
    //
    // 任意多边形**不需要中心点**：它的内部走 ear clipping（见下面那段），
    // 而"中心"在凹多边形里甚至可能落在形状外（U 形的中心就在凹口里）。
    const bool earClipInside = (scaled.kind == ShapeKind::PolygonPath);
    const SDL_FColor edgeColor = VertexColor(color, 0.0f);
    const SDL_FColor solidColor = VertexColor(
        color, static_cast<float>(ColorAlpha(color)) / 255.0f);

    vertices.reserve(2 * count + 1);
    indices.reserve(9 * count);

    for (std::size_t i = 0; i < count; ++i) {
        // **本地**坐标（相对组件左上角）：平移由提交层做，见上面那段缓存说明。
        vertices.push_back(makeVertex(outer[i].x, outer[i].y, edgeColor));
    }
    for (std::size_t i = 0; i < count; ++i) {
        vertices.push_back(makeVertex(inner[i].x, inner[i].y, solidColor));
    }
    if (!earClipInside) {
        vertices.push_back(makeVertex(centerX, centerY, solidColor));
    }

    // ---- 索引：边缘一圈的带子 + 里面的三角化 ----
    const int centerIndex = static_cast<int>(2 * count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t next = (i + 1) % count;
        const int outerA = static_cast<int>(i);
        const int outerB = static_cast<int>(next);
        const int innerA = static_cast<int>(count + i);
        const int innerB = static_cast<int>(count + next);

        // 羽化带：外圈 → 内圈，两个三角形。这一条对所有形状一样。
        indices.push_back(outerA);
        indices.push_back(outerB);
        indices.push_back(innerB);
        indices.push_back(outerA);
        indices.push_back(innerB);
        indices.push_back(innerA);

        if (earClipInside) {
            continue;  // 实心区在循环结束后统一三角化
        }

        // 实心区：中心 + 内圈相邻两点。**凸形状**下扇形三角化成立。
        indices.push_back(centerIndex);
        indices.push_back(innerA);
        indices.push_back(innerB);
    }

    if (earClipInside) {
        // 任意多边形可以凹、可以不是星形域，所以内部用 ear clipping 现算。
        AppendEarClipIndices(inner, indices, static_cast<int>(count));
    }
    return true;
}

}  // namespace

void fillShape(SDL_Renderer* renderer, const ShapeSpec& shape, int width,
               int height, float magnification, float pixelX, float pixelY,
               const TransformSpec& transform, std::uint32_t color) {
    if (renderer == nullptr || width <= 0 || height <= 0) {
        return;
    }

    const float mag = magnification > 0.0f ? magnification : 1.0f;
    const float w = static_cast<float>(width) * mag;
    const float h = static_cast<float>(height) * mag;
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }

    // 半径与线宽还得跟着展示倍率缩放：尺寸已经在 w/h 里了。
    ShapeSpec scaled = shape;
    scaled.radius = shape.radius * mag;
    scaled.thickness = shape.thickness * mag;

    // 线宽为 0 的 ring / line 没有面积可言（SDF 里只剩一圈测度为零的边界），
    // 直接不画——省得画出一条 1 像素的线，鼠标却点不中（画与点不同源）。
    if ((scaled.kind == ShapeKind::Ring || scaled.kind == ShapeKind::Line)
        && scaled.thickness <= 0.0f) {
        return;
    }

    // ---- T-6：顶点缓存 ----
    //
    // 顶点算的是**本地**坐标（相对组件左上角），所以"节点挪个位置"不该让缓存作废
    // ——只有形状 / 尺寸 / 倍率 / 变换这几样变了才重算。命中的话只做一次平移，
    // 轮廓采样、弧段细分、羽化那套全免（那才是每个节点每帧的大头）。
    auto matches = [&](const VertexCacheSlot& slot) {
        return slot.valid && slot.width == width && slot.height == height
            && slot.magnification == mag && SameShape(slot.shape, scaled)
            && SameTransform(slot.transform, transform)
            && slot.color == color;
    };
    VertexCacheSlot* slot = CurrentNode();
    if (slot != nullptr && matches(*slot)) {
        AppendLocalGeometry(slot->vertices.data(), slot->vertices.size(),
                            slot->indices.data(), slot->indices.size(), pixelX,
                            pixelY, /*reused=*/true);
        return;
    }

    // 形状 → 网格的那一段抽在 `BuildShapeMesh` 里（带纹理的填充共用它，
    // 见 `fillShapeTextured`）。
    std::vector<SDL_Vertex>& vertices = vertexScratch();
    std::vector<int>& indices = indexScratch();
    if (!BuildShapeMesh(scaled, w, h, mag, color, transform, nullptr, vertices,
                        indices)) {
        return;
    }
    StoreAndAppend(slot, scaled, width, height, mag, transform, vertices,
                   indices, pixelX, pixelY, color);
}

void strokeShape(SDL_Renderer* renderer, const ShapeSpec& shape, int width,
                 int height, float magnification, float pixelX, float pixelY,
                 const TransformSpec& transform, float strokeWidth,
                 std::uint32_t color) {
    if (renderer == nullptr || width <= 0 || height <= 0 || strokeWidth <= 0.0f) {
        return;
    }

    const float mag = magnification > 0.0f ? magnification : 1.0f;
    const float w = static_cast<float>(width) * mag;
    const float h = static_cast<float>(height) * mag;
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }

    // `ring` / `arc` 本身就是一条细带（两条边界）："沿边界描边"会变成四条线，
    // 现在直接不画（配置里写了也不报错，见 InkingDraw.h 那条取舍）。
    if (shape.kind == ShapeKind::Ring || shape.kind == ShapeKind::Arc) {
        return;
    }

    ShapeSpec scaled = shape;
    scaled.radius = shape.radius * mag;
    scaled.thickness = shape.thickness * mag;
    if (scaled.kind == ShapeKind::Line && scaled.thickness <= 0.0f) {
        return;
    }

    const float stroke = strokeWidth * mag;
    const float shorter = w < h ? w : h;
    const float half = std::min(kFeatherPixels, shorter * 0.25f) * 0.5f;
    const float solidAlpha = static_cast<float>(ColorAlpha(color)) / 255.0f;
    const float outerOffset = stroke * 0.5f;

    // 四条轮廓（外羽化 / 实心外 / 实心内 / 内羽化）。描边比羽化带还窄
    // （不到 1 像素）时实心段消失，合成一条"到边界取峰值"的三圈。
    struct StrokeRing {
        float offset = 0.0f;
        float alpha = 0.0f;
    };
    StrokeRing rings[4];
    int ringCount = 0;
    rings[ringCount++] = StrokeRing{outerOffset + half, 0.0f};
    if (stroke >= 2.0f * half) {
        rings[ringCount++] = StrokeRing{outerOffset - half, solidAlpha};
        rings[ringCount++] = StrokeRing{-outerOffset + half, solidAlpha};
    } else {
        const float peak = std::clamp(0.5f + stroke * 0.5f, 0.0f, 1.0f);
        rings[ringCount++] = StrokeRing{0.0f, peak * solidAlpha};
    }
    rings[ringCount++] = StrokeRing{-outerOffset - half, 0.0f};

    // 变换与填充那条路同一套：绕组件中心、平移量先乘展示倍率。
    TransformSpec deviceTransform = transform;
    deviceTransform.translateX *= mag;
    deviceTransform.translateY *= mag;
    const float centerX = w * 0.5f;
    const float centerY = h * 0.5f;
    const auto transformPoint = [&](float x, float y) {
        return ApplyTransform(deviceTransform, x, y, centerX, centerY);
    };

    const int steps = ContourSteps(scaled, w, h);
    std::vector<OutlinePoint>& contour = outerContourScratch();
    std::vector<SDL_Vertex>& vertices = vertexScratch();
    std::vector<int>& indices = indexScratch();
    vertices.clear();
    indices.clear();

    int pointCount = 0;
    for (int ring = 0; ring < ringCount; ++ring) {
        contour.clear();
        AppendContour(contour, scaled, w, h, steps, rings[ring].offset);
        if (contour.empty()) {
            return;  // 这个形状没有可采样的轮廓（Ring / Arc 已在上面挡掉）
        }
        if (ring == 0) {
            pointCount = static_cast<int>(contour.size());
            if (pointCount < 3) {
                return;
            }
        } else if (static_cast<int>(contour.size()) != pointCount) {
            return;  // 点数配对不上：宁可不画也别画错
        }
        const SDL_FColor ringColor = VertexColor(color, rings[ring].alpha);
        for (const OutlinePoint& point : contour) {
            const Point2 placed = transformPoint(point.x, point.y);
            // **本地**坐标：平移交给提交层（与填充同一条约定）。
            vertices.push_back(MakeVertex(placed.x, placed.y, ringColor));
        }
    }

    // 圈与圈之间铺四边形（首尾按下标闭合，和圆环那条网格同一个套路）。
    for (int ring = 0; ring + 1 < ringCount; ++ring) {
        for (int i = 0; i < pointCount; ++i) {
            const int next = (i + 1) % pointCount;
            const int a = ring * pointCount + i;
            const int b = ring * pointCount + next;
            const int c = (ring + 1) * pointCount + i;
            const int d = (ring + 1) * pointCount + next;
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(d);
            indices.push_back(a);
            indices.push_back(d);
            indices.push_back(c);
        }
    }

    AppendLocalGeometry(vertices.data(), vertices.size(), indices.data(),
                        indices.size(), pixelX, pixelY, /*reused=*/false);
}

void fillShapeTextured(SDL_Renderer* renderer, const ShapeSpec& shape, int width,
                       int height, float magnification, float pixelX,
                       float pixelY, const TransformSpec& transform,
                       SDL_Texture* texture, bool hasAlpha,
                       const ImageFitSpec& fit, std::uint32_t tintColor) {
    if (renderer == nullptr || texture == nullptr || width <= 0 || height <= 0
        || !fit.Valid()) {
        return;
    }

    const float mag = magnification > 0.0f ? magnification : 1.0f;
    const float w = static_cast<float>(width) * mag;
    const float h = static_cast<float>(height) * mag;
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }

    ShapeSpec scaled = shape;
    scaled.radius = shape.radius * mag;
    scaled.thickness = shape.thickness * mag;
    if ((scaled.kind == ShapeKind::Ring || scaled.kind == ShapeKind::Line)
        && scaled.thickness <= 0.0f) {
        return;
    }

    // ---- 矩形 + Fill：**默认也并进几何批**（0.6.2 T-08）----
    //
    // 这块的历史值得留着：0.2.0 在这里开了一条 `SDL_RenderTexture` 快路，理由是
    // "走 `SDL_RenderGeometry` 时纹理的 alpha 不参与混合，透明底 PNG 的透明区
    // 变成半透明黑"（当时实测读回 `[0 0 0]`，而背景该是 `[78 76 113]`）。
    //
    // 0.6.2 T-07 用探针重新量了 **SDL 3.4.16** 的两个后端（software / direct3d11）：
    // 透明区、50% 半透明内容、放大 2 倍的双线性采样——几何批与 `SDL_RenderTexture`
    // **逐位相同**。那条老结论在现在的 SDL 上不复现（旧记录保留在任务书 §2.3 里）。
    // 所以默认合并：同纹理连续 N 笔 = **一次** `SDL_RenderGeometry`。
    //
    // 开关关掉时仍走快路（`detail::SetImageBatchingEnabled(false)`）：它是回退预案，
    // 也是"快路那份像素"的基准。**注意快路不看 `transform`**（也不看祖先仿射），
    // 所以关掉时旋转 / 缩放的矩形图片会画成没变换——这是既有缺陷，不是开关引入的。
    const bool rectFastPath = scaled.kind == ShapeKind::Rect
        && fit.fit == ImageFit::Fill && !ImageBatchingEnabled();
    if (rectFastPath) {
        SDL_FRect destination{pixelX, pixelY, w, h};
        // 源矩形（图集切格）：全零 = 整张纹理，`srcRect` 传 nullptr——**与 0.6.1
        // 之前那条调用逐字节一致**（SDL 对 nullptr 走"整张"的默认路径）。
        SDL_FRect source{};
        const SDL_FRect* sourceRect = nullptr;
        if (fit.HasSourceRect()) {
            const ShapeBounds& raw = fit.srcRect;
            source = SDL_FRect{raw.x, raw.y, raw.width, raw.height};
            sourceRect = &source;
        }
        SDL_SetTextureColorMod(texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture, 255);
        // 顺序：这一笔绕开批次（纹理绘制本来就是批次边界）。
        NoteImmediateDraw();
        SDL_RenderTexture(renderer, texture, sourceRect, &destination);
        return;
    }

    // 网格：与无纹理那条**同一份**生成（含羽化），只多带一份 UV + 图覆盖率。
    std::vector<SDL_Vertex>& vertices = vertexScratch();
    std::vector<int>& indices = indexScratch();
    if (!BuildShapeMesh(scaled, w, h, mag, tintColor, transform, &fit, vertices,
                        indices)) {
        return;
    }

    // 这条**不走几何批时**的写法（下面 contain 那条）要自己刷一次批：
    // 纹理绘制本来就能与纯色几何同批（B4 起批按纹理分），但裁剪矩形是
    // "绘制那一刻"生效的，所以 contain 那一笔必须是直接提交。
    const bool clipToImageArea = (fit.fit == ImageFit::Contain);
    if (!clipToImageArea) {
        // 正常路径：交给提交层。同一张纹理连续画 N 笔会合并成**一次**
        // `SDL_RenderGeometry`（`BatchStats::textureDraws` 可断言）。
        AppendTexturedGeometry(texture, vertices.data(), vertices.size(),
                               indices.data(), indices.size(), pixelX, pixelY);
        return;
    }

    // ---- contain 的留白：形状 ∩ 图区域 ----
    // 裁剪是"绘制那一刻"生效的，所以这一笔**绕开批次**直接提交。
    NoteImmediateDraw();
    ApplyAncestorAffineToVertices(vertices.data(), vertices.size(), pixelX,
                                  pixelY);
    const ShapeBounds area = fit.ImageArea();
    SDL_Vertex corners[4];
    const float xs[4] = {area.x, area.x + area.width, area.x, area.x + area.width};
    const float ys[4] = {area.y, area.y, area.y + area.height, area.y + area.height};
    for (int i = 0; i < 4; ++i) {
        corners[i] = MakeVertex(xs[i], ys[i], SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f});
    }
    ApplyAncestorAffineToVertices(corners, 4, pixelX, pixelY);
    float minX = corners[0].position.x;
    float minY = corners[0].position.y;
    float maxX = minX;
    float maxY = minY;
    for (int i = 1; i < 4; ++i) {
        minX = std::min(minX, corners[i].position.x);
        minY = std::min(minY, corners[i].position.y);
        maxX = std::max(maxX, corners[i].position.x);
        maxY = std::max(maxY, corners[i].position.y);
    }
    SetClipRect(renderer, ShapeBounds{minX, minY, maxX - minX, maxY - minY});

    // 颜色只用来调制（图片本身不换色，只借 tint 的 alpha 做淡入淡出）；
    // 顶点色里的 alpha 是形状羽化，所以这里不动 alpha mod。
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
    SDL_RenderGeometry(renderer, texture, vertices.data(),
                       static_cast<int>(vertices.size()), indices.data(),
                       static_cast<int>(indices.size()));

    ClearClipRect(renderer);
}

}  // namespace inking::detail
