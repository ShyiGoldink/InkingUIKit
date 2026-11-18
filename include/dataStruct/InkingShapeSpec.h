#pragma once

//InkingShapeSpec是InkingUIKit的形状层，它基于的是SDF这种几何表示法，是UI常见的表示方式

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <limits>

//形状种类枚举
enum class InkingShapeKind : std::uint8_t {
    Rect = 0,//矩形
    RoundedRect,//圆角矩形
    Circle,//圆
    Ellipse,//椭圆
    Capsule,//胶囊体
    Ring,//圆环
    Line,//线
    Polygon,//多边形
    ChamferRect,  // 切角矩形
    Star, // 星形
    Superellipse, // 超椭圆
    Arc, // 圆弧
    PolygonPath,  // 顶点表给出来的任意多边形
};

//线段种类枚举
enum class InkingLineCap : std::uint8_t {
    Butt = 0,  // 平头
    Round,     // 圆头
    Square,    // 方头
};


//形状的结构体，这是一种假反射。对于有域的属性，会进行夹取来避免不合理的属性
struct InkingShapeSpec {
    InkingShapeKind kind = InkingShapeKind::Rect;//形状种类
    float radius = 0.0f;//圆角弧度，只对圆角矩形RoundedRect有用
    float thickness = 0.0f;//线宽，对于圆环Ring和线Line有用
    InkingLineCap cap = InkingLineCap::Butt;//线头，对于线Line有用
    int sides = 0;//边数，只对多边形Polygon有用
    float rotation = 0.0f;//起始角，对多边形Polygon和星形Star有用。0表示第一个顶点落在 +x（正右方），其余顶点按 360/sides 顺时针排。
    int points = 3;//角的数量，只对星形Star有用。
    float innerRatio = 0.5f;//内半径和外半径的比例，越小角越尖，只对星形Star有用
    float chamfer = 0.0f;//切角沿边的长度，只对切角矩形ChamferRect有用
    float exponent = 2.0f;// 指数 n，n = 2 是椭圆，越大越接近矩形，只对Superellipse超椭圆有用
    float startAngle = 0.0f;//起始角，只对圆弧Arc有用。0=正右方
    float sweepAngle = 360.0f;//扫过的角度，只对圆弧Arc有意义。
    // 借用的顶点数据；手动构造时，调用方须保证其生命周期覆盖所有使用者。
    const float* pathPoints = nullptr; // 顶点表（x0,y0,…)；仅任意多边形 PolygonPath有用
    int pathPointCount = 0;// 顶点数

    /*
    直角矩形Rect
    宽，采用组件的width，影响矩形的宽度，结构体中无域
    高，采用组件的height，影响矩形的高度，结构体中无域
    夹取规则：无
    */
    static constexpr InkingShapeSpec Rect() { return InkingShapeSpec{}; }

    /*
    圆角矩形RoundedRect
    半径，采用入参radius，影响四个角的圆角半径，结构体中域为 radius
    宽，采用组件的width，影响矩形的宽度与半径上限，结构体中无域
    高，采用组件的height，影响矩形的高度与半径上限，结构体中无域
    夹取规则：负半径断言拦截（配置错误，不自动修正）；超过 min(width,height)/2 夹到上限（形状自身的数学要求）
    */
    static InkingShapeSpec RoundedRect(float radius) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::RoundedRect;
        assert(radius >= 0.0f && "shape:radius 不能为负");
        spec.radius = radius < 0.0f ? 0.0f : radius;
        return spec;
    }

    /*
    正圆Circle
    宽，采用组件的width，影响直径取值（直径 = min(width,height)），结构体中无域
    高，采用组件的height，影响直径取值（直径 = min(width,height)），结构体中无域
    夹取规则：无（宽高不等时按短边定直径，另一方向居中留白）
    */
    static constexpr InkingShapeSpec Circle() {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Circle;
        return spec;
    }

    /*
    椭圆Ellipse
    宽，采用组件的width，影响横向半轴（= width/2），结构体中无域
    高，采用组件的height，影响纵向半轴（= height/2），结构体中无域
    夹取规则：无（宽或高为 0 时自然退化成线或点）
    */
    static constexpr InkingShapeSpec Ellipse() {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Ellipse;
        return spec;
    }

    /*
    胶囊Capsule
    宽，采用组件的width，影响胶囊沿水平方向的长度与两端半径 min(width,height)/2，结构体中无域
    高，采用组件的height，影响胶囊沿垂直方向的长度与两端半径 min(width,height)/2，结构体中无域
    夹取规则：无（用满 width × height，两端半圆半径由短边决定）
    */
    static constexpr InkingShapeSpec Capsule() {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Capsule;
        return spec;
    }

    /*
    圆环Ring
    环宽，采用入参thickness，影响环带的厚度（从外径往里长），结构体中域为 thickness
    宽，采用组件的width，影响外径取值（外径 = min(width,height)），结构体中无域
    高，采用组件的height，影响外径取值（外径 = min(width,height)），结构体中无域
    夹取规则：环宽须大于 0，否则断言拦截（配置错误）；超过外半径则夹到外半径（再厚就没有孔了，退化成实心圆）
    */
    static InkingShapeSpec Ring(float thickness) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Ring;
        // 环宽没有"负"这种语义
        assert(thickness > 0.0f && "shape:thickness 必须大于 0");
        spec.thickness = thickness > 0.0f ? thickness : 0.0f;
        return spec;
    }

    /*
    水平线段Line
    线宽，采用入参thickness，影响线段的粗细（纵向），结构体中域为 thickness
    线头，采用入参cap，影响两端端头的形状（平头/圆头/方头），结构体中域为 cap
    宽，采用组件的width，影响线段的长度，结构体中无域
    高，采用组件的height，影响线段的纵向居中位置，结构体中无域
    夹取规则：线宽须大于 0，否则断言拦截（配置错误）；无自动夹取
    */
    static InkingShapeSpec Line(float thickness, InkingLineCap cap = InkingLineCap::Butt) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Line;
        assert(thickness > 0.0f && "shape:thickness 必须大于 0");
        spec.thickness = thickness > 0.0f ? thickness : 0.0f;
        spec.cap = cap;
        return spec;
    }

    /*
    正多边形Polygon
    边数，采用入参sides，影响边的条数，结构体中域为 sides
    起始角，采用入参rotation（度），影响第一个顶点的方向（0 = +x 正右方，其余顶点按 360/sides 顺时针排），结构体中域为 rotation
    宽，采用组件的width，影响外接圆半径（= min(width,height)/2），结构体中无域
    高，采用组件的height，影响外接圆半径（= min(width,height)/2），结构体中无域
    夹取规则：边数小于 3 断言拦截（配置错误）；无自动夹取
    */
    static InkingShapeSpec Polygon(int sides, float rotation = 0.0f) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Polygon;
        assert(sides >= 3 && "shape:sides 必须 >= 3");
        spec.sides = sides >= 3 ? sides : 3;
        spec.rotation = rotation;
        return spec;
    }

    /*
    切角矩形ChamferRect
    切角，采用入参chamfer，影响四个角各切掉的 45° 斜边长度，结构体中域为 chamfer
    宽，采用组件的width，影响矩形的宽度与切角上限，结构体中无域
    高，采用组件的height，影响矩形的高度与切角上限，结构体中无域
    夹取规则：负切角断言拦截（配置错误）；超过短边的一半则夹到短边的一半（再长就没有直边了）
    */
    static InkingShapeSpec ChamferRect(float chamfer) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::ChamferRect;
        assert(chamfer >= 0.0f && "shape:chamfer 不能为负");
        spec.chamfer = chamfer < 0.0f ? 0.0f : chamfer;
        return spec;
    }

    /*
    星形Star
    角数，采用入参points，影响角的数量，结构体中域为 points
    内外比，采用入参innerRatio，影响内半径与外半径之比（越小角越尖），结构体中域为 innerRatio
    起始角，采用入参rotation（度），影响第一个外角的方向，结构体中域为 rotation
    宽，采用组件的width，影响外接圆半径（= min(width,height)/2），结构体中无域
    高，采用组件的height，影响外接圆半径（= min(width,height)/2），结构体中无域
    夹取规则：角数小于 3 断言拦截（配置错误）；内外比夹到 [0,1]（0 = 内半径为 0 的尖角，1 = 退化成圆，都合法，超出就不是星形了）
    */
    static InkingShapeSpec Star(int points, float innerRatio = 0.5f,
                          float rotation = 0.0f) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Star;
        assert(points >= 3 && "shape:points 必须 >= 3");
        spec.points = points >= 3 ? points : 3;
        spec.innerRatio = std::clamp(innerRatio, 0.0f, 1.0f);
        spec.rotation = rotation;
        return spec;
    }

    /*
    超椭圆Superellipse
    指数，采用入参exponent，影响形状的方度（2 = 椭圆，越大越接近矩形），结构体中域为 exponent
    宽，采用组件的width，影响横向半轴（= width/2），结构体中无域
    高，采用组件的height，影响纵向半轴（= height/2），结构体中无域
    夹取规则：指数须大于 0，否则断言拦截（配置错误）；夹到 [0.05,16]（太小退化成十字线，太大 pow 在远离中心处溢出）
    */
    static InkingShapeSpec Superellipse(float exponent) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Superellipse;
        assert(exponent > 0.0f && "shape:exponent 必须大于 0");
        spec.exponent = std::clamp(exponent, 0.05f, 16.0f);
        return spec;
    }

    /*
    圆弧Arc
    环宽，采用入参thickness，影响弧带的厚度（从外径往里长），结构体中域为 thickness
    起始角，采用入参startAngle（度），影响弧的起点方向（0 = +x 正右方），结构体中域为 startAngle
    扫角，采用入参sweepAngle（度），影响从起点顺时针扫过的角度（负值表示逆时针），结构体中域为 sweepAngle
    宽，采用组件的width，影响外径取值（外径 = min(width,height)），结构体中无域
    高，采用组件的height，影响外径取值（外径 = min(width,height)），结构体中无域
    夹取规则：环宽须大于 0，否则断言拦截（配置错误）；环宽超过外半径则夹到外半径；扫角取绝对值后夹到 [0,360]，
    到 360 就退化成整圈圆环（数学要求，夹住而不报错）；两端径向切平，不做圆头
    */
    static InkingShapeSpec Arc(float thickness, float startAngle, float sweepAngle) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Arc;
        assert(thickness > 0.0f && "shape:thickness 必须大于 0");
        spec.thickness = thickness > 0.0f ? thickness : 0.0f;
        spec.startAngle = startAngle;
        spec.sweepAngle = sweepAngle;
        return spec;
    }

    /*
    任意多边形PolygonPath
    顶点表，采用入参points（x0,y0,x1,y1,… 的设计坐标），影响多边形轮廓，结构体中域为 pathPoints（不拷贝，生命周期须覆盖使用者）
    顶点数，采用入参count，影响读取的顶点个数（是点数、不是浮点数个数，至少 3），结构体中域为 pathPointCount
    宽，不参与几何计算（顶点已是设计坐标），结构体中无域
    高，不参与几何计算（顶点已是设计坐标），结构体中无域
    夹取规则：顶点表为空或顶点数小于 3 断言拦截（配置错误）；非法输入时顶点数落 0，填充与命中退化成整框
    */
    static InkingShapeSpec PolygonPath(const float* points, int count) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::PolygonPath;
        assert(points != nullptr && count >= 3 && "polygonPath 至少要 3 个点");
        spec.pathPoints = points;
        spec.pathPointCount = (points != nullptr && count >= 3) ? count : 0;
        return spec;
    }

    //静态辅助函数，夹取半径，满足形状上的数学要求
    static float ClampRadius(float radius, int width, int height) {
        const float shorter =
            static_cast<float>(width < height ? width : height);
        const float limit = shorter * 0.5f;
        if (radius < 0.0f) {
            return 0.0f;
        }
        return radius > limit ? limit : radius;
    }

    //实例辅助函数，夹取半径，满足形状上的数学要求
    float EffectiveRadius(int width, int height) const {
        return ClampRadius(radius, width, height);
    }
};

// 内部小工具。名字统一带 Shape/shape前缀
namespace inking_detail {

constexpr float shapePi = 3.14159265358979323846f;//圆周率
constexpr float shapeDegToRad = shapePi / 180.0f;//弧度

// 相对于组件左上角的一个点。只为形状内部算几何用，不进公开接口。
struct ShapePoint {
    float x = 0.0f;
    float y = 0.0f;
};

//SDF几何辅助函数：用于确定点px，py和矩形的关系
inline float ShapeBoxDistance(float px, float py, float cx, float cy,
                              float halfW, float halfH) {
    //先将点px，py处理，将全部象限的情况统一成一个象限的内容
    const float qx = std::fabs(px - cx) - halfW;
    const float qy = std::fabs(py - cy) - halfH;

    /*
    使用0来截取qx和qy。
    因为qx和qy还需要在下方继续使用，所以使用outsideX和outsideY来代替计算
    如果qx或者qy其中一方大于零，那么求得的outside就会大于零。否则outside就为0。
    这是情况A
    */
    const float outsideX = qx > 0.0f ? qx : 0.0f;
    const float outsideY = qy > 0.0f ? qy : 0.0f;
    const float outside = std::sqrt(outsideX * outsideX + outsideY * outsideY);

    /*
    使用0来截取qx和qy
    先求出qx和qy中大的那一个，如果大的那一个小于0，说明双方都小于0
    只有小于0的情况下，inside才会为负数
    这是情况B
    */
    const float nearer = qx > qy ? qx : qy;
    const float inside = nearer < 0.0f ? nearer : 0.0f;

    /*
     情况A和情况B天然互斥，其中一方满足条件，另一方就为0
     所以返回的值就是满足条件情况的那个值
    */
    return outside + inside;
}
// SDF几何辅助函数：用于确定点px，py到由点bx，by和ax，ay组成的线段的距离
inline float ShapeSegmentDistance(float px, float py, float ax, float ay,float bx, float by) {
    /*
    先使用向量法求出线段的长度
    */
    const float ex = bx - ax;
    const float ey = by - ay;
    const float lengthSquared = ex * ex + ey * ey;

    /*
    根据三角函数的相关知识，推导出t
    t是线段ab上，做p到ab的垂线，垂点p‘，ap’/p‘b的比值
    算法是很简单的推导，根据三点组成三角形的边长关系推导出cosPAB
    再根据cosPAC求得t
    由于点和线段有多种关系，如果点在线段的延长线上，那么要取的距离是最近的那个线段
    如果点不在线段的延长线上，那么要取的是垂线的长度
    clamp就是做了这个选择
    */
    float t = 0.0f;
    if (lengthSquared > 0.0f) {
        t = ((px - ax) * ex + (py - ay) * ey) / lengthSquared;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    /*
    最后基于各种值，算出dx和dy，再算出长度
    */
    const float dx = px - (ax + ex * t);
    const float dy = py - (ay + ey * t);
    return std::sqrt(dx * dx + dy * dy);
}

//SDF夹取辅助函数：让thickness半价夹取到0到min(width,height)/2之中
inline float ShapeRingThickness(float thickness, int width, int height) {
    const float shorter = static_cast<float>(width < height ? width : height);
    const float outerRadius = shorter * 0.5f;
    if (thickness <= 0.0f) {
        return 0.0f;
    }
    return thickness > outerRadius ? outerRadius : thickness;
}

// SDF夹取辅助函数：确保多边形的边数至少为3
inline int ShapePolygonSides(const InkingShapeSpec& shape) {
    return shape.sides >= 3 ? shape.sides : 3;
}

// SDF夹取辅助函数：让多边形的外接圆半价夹取到0到min(width,height)/2之中
inline float ShapePolygonCircumradius(int width, int height) {
    const float shorter = static_cast<float>(width < height ? width : height);
    return shorter * 0.5f;
}

// SDF几何辅助函数：获取到正多边形第n个顶点的位置，相对于(0,0)
inline ShapePoint ShapePolygonVertex(const InkingShapeSpec& shape, int width,
                                     int height, int k) {
    const int sides = ShapePolygonSides(shape);
    const float radius = ShapePolygonCircumradius(width, height);
    const float angle =
        (shape.rotation + 360.0f * static_cast<float>(k)
                              / static_cast<float>(sides))
        * shapeDegToRad;
    return ShapePoint{static_cast<float>(width) * 0.5f + std::cos(angle) * radius,
                      static_cast<float>(height) * 0.5f
                          + std::sin(angle) * radius};
}

//SDF夹取辅助函数：将星形的角数夹取到>=3
inline int ShapeStarPoints(const InkingShapeSpec& shape) {
    return shape.points >= 3 ? shape.points : 3;
}

// SDF夹取辅助函数：让星形的外接圆半价夹取到0到min(width,height)/2之中
inline float ShapeStarOuterRadius(int width, int height) {
    const float shorter = static_cast<float>(width < height ? width : height);
    return shorter * 0.5f;
}

//SDF夹取辅助函数：让星形的内接圆比例夹取到[0,1]
inline float ShapeStarInnerRadius(const InkingShapeSpec& shape, int width, int height) {
    const float ratio = std::clamp(shape.innerRatio, 0.0f, 1.0f);
    return ShapeStarOuterRadius(width, height) * ratio;
}

//SDF几何辅助函数：获取到星形第k个定点的位置，相对于(0,0)
inline ShapePoint ShapeStarVertex(const InkingShapeSpec& shape, int width, int height,
                                  int k) {
    const int points = ShapeStarPoints(shape);
    const int count = points * 2;
    int index = k % count;
    if (index < 0) {
        index += count;
    }
    const bool outer = (index % 2) == 0;
    const float radius = outer ? ShapeStarOuterRadius(width, height)
                               : ShapeStarInnerRadius(shape, width, height);
    const float step = 360.0f / static_cast<float>(count);
    const float angle =
        (shape.rotation + step * static_cast<float>(index)) * shapeDegToRad;
    return ShapePoint{static_cast<float>(width) * 0.5f + std::cos(angle) * radius,
                      static_cast<float>(height) * 0.5f
                          + std::sin(angle) * radius};
}

// SDF夹取辅助函数：将切角矩形被切去的等腰三角形的纵边/横边夹取到下限0和上限为矩形较短那边的一半
inline float ShapeChamferAmount(float chamfer, float w, float h) {
    const float shorter = w < h ? w : h;
    const float limit = shorter * 0.5f;
    if (chamfer <= 0.0f) {
        return 0.0f;
    }
    return chamfer > limit ? limit : chamfer;
}

//SDF夹取辅助函数：将超椭圆的比值夹取到一个合理的范围内
inline float ShapeSuperellipseExponent(const InkingShapeSpec& shape) {
    return std::clamp(shape.exponent, 0.05f, 16.0f);
}

// SDF夹取辅助函数：将圆弧的thickness和outerRadius夹取到能够不退化的范围
inline float ShapeArcThickness(float thickness, float outerRadius) {
    if (thickness <= 0.0f) {
        return 0.0f;
    }
    return thickness > outerRadius ? outerRadius : thickness;
}

//SDF夹取辅助函数：把圆弧扫角的幅值夹到 0..360 度，再转成弧度
inline float ShapeArcSweepRadians(const InkingShapeSpec& shape) {
    const float degrees = std::clamp(std::fabs(shape.sweepAngle), 0.0f, 360.0f);
    return degrees * shapeDegToRad;
}

//SDF几何辅助函数：给出圆弧的扫向符号，+1 顺时针、-1 逆时针
inline float ShapeArcDirection(const InkingShapeSpec& shape) {
    return shape.sweepAngle < 0.0f ? -1.0f : 1.0f;
}

//SDF几何辅助函数：取出顶点表第 k 个顶点，设计坐标、相对组件左上角、对点数取模
inline ShapePoint ShapePathVertex(const InkingShapeSpec& shape, int k) {
    const int count = shape.pathPointCount;
    if (count <= 0 || shape.pathPoints == nullptr) {
        return ShapePoint{};
    }
    int index = k % count;
    if (index < 0) {
        index += count;
    }
    return ShapePoint{shape.pathPoints[index * 2],
                      shape.pathPoints[index * 2 + 1]};
}

template <typename VertexAt>
//SDF几何辅助函数，计算点(x,y)到多边形边界的最短距离，并用符号表示是否在边界内部，为星形和多边形服务
inline float ShapePolygonSdf(int count, VertexAt vertexAt, float x, float y) {
    if (count < 3) {
        // 顶点不足以构成多边形，无法定义有效的带符号距离。
        return 0.0f;
    }
    // 射线与边的交点采用奇偶规则判定内外，因此也适用于凹多边形。
    bool inside = false;
    // 保存点到所有边的最小距离，作为结果的绝对值。
    float nearest = std::numeric_limits<float>::max();
    for (int i = 0; i < count; ++i) {
        const ShapePoint a = vertexAt(i);
        const ShapePoint b = vertexAt(i + 1);
        const float edge = ShapeSegmentDistance(x, y, a.x, a.y, b.x, b.y);
        if (edge < nearest) {
            nearest = edge;
        }
        // 射线法：从这点往 +x 打一条射线，数它穿过几条边。顶点绕向无关。
        if ((a.y > y) != (b.y > y)) {
            const float crossX = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x < crossX) {
                inside = !inside;
            }
        }
    }
    // SDF符号约定：内部为负，外部为正。
    return inside ? -nearest : nearest;
}

}  // namespace inking_detail


 //SDF距离场函数：判断相对于组件左上角的坐标点(x,y)与形状shape的关系，返回值f < 0 在内部、f = 0 在边界、f > 0 在外部
inline float InkingSignedDistance(const InkingShapeSpec& shape, float x, float y, int width,
                            int height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);

    switch (shape.kind) {
        case InkingShapeKind::RoundedRect: {
            // 半径先夹取，再按内缩矩形和角心距离计算。
            /*
             只有角部需要从角心距离中减去半径；沿直边的距离已经是准确值。
             因此只在 qx、qy 都为正时减半径，避免把四条直边也缩短 r。
             例如，圆角矩形中心到上边的距离是 h/2，而不是 h/2 - r。
            */
            const float r = shape.EffectiveRadius(width, height);
            const float halfW = w * 0.5f;
            const float halfH = h * 0.5f;
            const float innerW = halfW - r;
            const float innerH = halfH - r;

            // 将点映射到内缩矩形角部对应的象限，通过绝对值复用同一套公式。
            const float qx = std::fabs(x - halfW) - innerW;
            const float qy = std::fabs(y - halfH) - innerH;

            const float outsideX = qx > 0.0f ? qx : 0.0f;
            const float outsideY = qy > 0.0f ? qy : 0.0f;
            const float outside =
                std::sqrt(outsideX * outsideX + outsideY * outsideY);

            // 点在内侧时，max(qx, qy) 为负值，表示到最近边线的距离。
            const float nearer = qx > qy ? qx : qy;
            const float inside = nearer < 0.0f ? nearer : 0.0f;

            // 只有两个方向都越过内缩矩形边界，也就是位于圆角区域时，才减去半径。
            return outside + inside - (nearer < 0.0f ? 0.0f : r);
        }

        case InkingShapeKind::Circle: {
            // 居中、直径取 min(w,h)：所以半径是 min(w,h)/2，圆心是 (w/2, h/2)。
            const float r = (w < h ? w : h) * 0.5f;
            const float dx = x - w * 0.5f;
            const float dy = y - h * 0.5f;
            return std::sqrt(dx * dx + dy * dy) - r;
        }

        case InkingShapeKind::Ellipse: {
            /*
             椭圆的精确 SDF 需要数值求解最近点。这里使用符号准确的近似，
             并保证边界附近的距离足够精确。

             在归一化空间计算到单位圆的距离，再乘回最小半径。这样内部为负、
             外部为正、边界为零，InkingShapeContains 的内外判定保持准确。
            */
            const float rx = w * 0.5f;
            const float ry = h * 0.5f;
            if (rx <= 0.0f || ry <= 0.0f) {
                const float dx = rx <= 0.0f ? std::fabs(x) : 0.0f;
                const float dy = ry <= 0.0f ? std::fabs(y) : 0.0f;
                return dx + dy;  // 退化成线或点，仅退化形状本身属于内部
            }
            const float k0 = std::hypot((x - rx) / rx, (y - ry) / ry);
            const float k1 = std::hypot((x - rx) / (rx * rx),
                                        (y - ry) / (ry * ry));
            const float scale = rx < ry ? rx : ry;
            if (k1 <= 0.0f) {
                return -scale;
            }
            return (k0 * (k0 - 1.0f) / k1) * scale;
        }

        case InkingShapeKind::Capsule: {
            /*
             胶囊由一条脊椎线段向外扩展半径 r 形成，r 为短边的一半。
             到胶囊的距离等于到线段的距离减去 r。
            */
            const float radius = (w < h ? w : h) * 0.5f;
            float ax = 0.0f;
            float ay = 0.0f;
            float bx = 0.0f;
            float by = 0.0f;
            if (w >= h) {
                ax = radius;
                ay = h * 0.5f;
                bx = w - radius;
                by = h * 0.5f;
            } else {
                ax = w * 0.5f;
                ay = radius;
                bx = w * 0.5f;
                by = h - radius;
            }
            return inking_detail::ShapeSegmentDistance(x, y, ax, ay, bx, by) - radius;
        }

        case InkingShapeKind::Ring: {
            /*
             圆环的距离等于到中径圆的距离减去半个环宽，
             即 |d - rMid| - t/2。
            */
            const float outerRadius = (w < h ? w : h) * 0.5f;
            const float thickness =
                inking_detail::ShapeRingThickness(shape.thickness, width, height);
            const float midRadius = outerRadius - thickness * 0.5f;
            const float dx = x - w * 0.5f;
            const float dy = y - h * 0.5f;
            const float distance = std::sqrt(dx * dx + dy * dy);
            // 环宽为零时仅圆周本身属于内部，圆孔和圆外均属于外部。
            return std::fabs(distance - midRadius) - thickness * 0.5f;
        }

        case InkingShapeKind::Line: {
            // 水平线段、纵向居中：起点 (0, h/2)、终点 (w, h/2)。
            const float centerY = h * 0.5f;
            const float half = shape.thickness * 0.5f;
            if (shape.cap == InkingLineCap::Round) {
                // 圆头通过线段距离减去半径计算，两端自然延伸为半圆。
                return inking_detail::ShapeSegmentDistance(x, y, 0.0f, centerY, w,
                                                    centerY)
                     - half;
            }
            // 平头和方头都按矩形计算；方头在两端各外扩 half。
            const float extra = shape.cap == InkingLineCap::Square ? half : 0.0f;
            return inking_detail::ShapeBoxDistance(x, y, w * 0.5f, centerY,
                                            w * 0.5f + extra, half);
        }

        case InkingShapeKind::Polygon: {
            /*
             正多边形是凸形。内部距离取到各边所在直线的最大带符号距离，
             该值不大于零等价于点位于所有边的内侧。外部距离取到边界线段的
             最短距离，两者组合得到精确 SDF。
            */
            const float radius = inking_detail::ShapePolygonCircumradius(width, height);
            if (radius <= 0.0f) {
                const float dx = x - w * 0.5f;
                const float dy = y - h * 0.5f;
                return std::sqrt(dx * dx + dy * dy);  // 半径为零时退化为中心点
            }
            const int sides = inking_detail::ShapePolygonSides(shape);
            const float centerX = w * 0.5f;
            const float centerY = h * 0.5f;

            float maxPlaneDistance = -std::numeric_limits<float>::max();
            float minEdgeDistance = std::numeric_limits<float>::max();
            for (int i = 0; i < sides; ++i) {
                const inking_detail::ShapePoint a =
                    inking_detail::ShapePolygonVertex(shape, width, height, i);
                const inking_detail::ShapePoint b =
                    inking_detail::ShapePolygonVertex(shape, width, height, i + 1);

                /*
                 外法向垂直于边并背离中心。用顶点相对中心的方向确定法向，
                 因而不依赖顶点绕向，也适用于任意旋转角度。
                */
                float nx = b.y - a.y;
                float ny = -(b.x - a.x);
                const float length = std::sqrt(nx * nx + ny * ny);
                if (length > 0.0f) {
                    nx /= length;
                    ny /= length;
                    if (nx * (a.x - centerX) + ny * (a.y - centerY) < 0.0f) {
                        nx = -nx;
                        ny = -ny;
                    }
                    const float plane =
                        (x - a.x) * nx + (y - a.y) * ny;
                    if (plane > maxPlaneDistance) {
                        maxPlaneDistance = plane;
                    }
                }

                const float edge =
                    inking_detail::ShapeSegmentDistance(x, y, a.x, a.y, b.x, b.y);
                if (edge < minEdgeDistance) {
                    minEdgeDistance = edge;
                }
            }
            return maxPlaneDistance <= 0.0f ? maxPlaneDistance : minEdgeDistance;
        }

        case InkingShapeKind::ChamferRect: {
            /*
             切角矩形由四条直边和四条 45 度斜边围成，是八个半平面的交集。
             凸集的 SDF 等于到各边所在平面的带符号距离最大值。
            */
            const float chamfer =
                inking_detail::ShapeChamferAmount(shape.chamfer, w, h);
            const float dx = std::fabs(x - w * 0.5f);
            const float dy = std::fabs(y - h * 0.5f);

            // 四条直边对应的矩形边界。
            const float box = std::max(dx - w * 0.5f, dy - h * 0.5f);

            /*
             四条斜边满足 |x| + |y| <= 半周长 - 切角。法向长度为 sqrt(2)，
             因此乘以 1/sqrt(2) 才得到欧氏距离。
            */
            constexpr float kInvSqrt2 = 0.70710678f;
            const float diagonal =
                (dx + dy - (w * 0.5f + h * 0.5f - chamfer)) * kInvSqrt2;

            return std::max(box, diagonal);
        }

        case InkingShapeKind::Star: {
            /*
             星形是非凸形，不能使用凸多边形的半平面距离算法。
             使用射线法判断内外，并计算点到边界的最短距离，和 PolygonPath 共用实现。
            */
            const int count = inking_detail::ShapeStarPoints(shape) * 2;
            return inking_detail::ShapePolygonSdf(
                count,
                [&](int k) {
                    return inking_detail::ShapeStarVertex(shape, width, height, k);
                },
                x, y);
        }

        case InkingShapeKind::PolygonPath: {
            /*
             任意多边形也使用射线法判断内外，并计算到最近边的距离。该算法支持
             非凸多边形，包括中心落在凹口中的 U 形区域。
            */
            const int count = shape.pathPointCount;
            if (count < 3 || shape.pathPoints == nullptr) {
                return 0.0f;  // 顶点表无效时，退化为整个组件区域均属于内部
            }
            return inking_detail::ShapePolygonSdf(
                count, [&](int k) { return inking_detail::ShapePathVertex(shape, k); },
                x, y);
        }

        case InkingShapeKind::Superellipse: {
            /*
             超椭圆 |x/a|^n + |y/b|^n = 1 没有简单的精确距离公式。
             通过归一化函数与 1 的比较准确判断内外，再按最小半轴缩放，
             得到边界附近较准确的距离。
            */
            const float a = w * 0.5f;
            const float b = h * 0.5f;
            if (a <= 0.0f || b <= 0.0f) {
                const float dx = a <= 0.0f ? std::fabs(x) : 0.0f;
                const float dy = b <= 0.0f ? std::fabs(y) : 0.0f;
                return dx + dy;  // 退化成线或点
            }
            const float n = inking_detail::ShapeSuperellipseExponent(shape);
            const float u = std::fabs(x - a) / a;
            const float v = std::fabs(y - b) / b;
            const float f = std::pow(std::pow(u, n) + std::pow(v, n), 1.0f / n);
            const float scale = a < b ? a : b;
            return (f - 1.0f) * scale;
        }

        case InkingShapeKind::Arc: {
            /*
             圆弧带由环带与两个端头半平面的交集构成。各部分距离取最大值，
             可得到符号准确的距离场；交界角附近是近似值。
             环带距离为 |半径 - 中径| - 半个环宽。端头沿径向切平，
             用经过端点的径向直线所定义的半平面距离表示。

             不能用到端点的距离减去半个环宽来计算端头，否则会形成圆头。
             渲染端采用平头，距离场也必须使用平头，才能保证绘制和命中一致。
            */
            const float outerRadius = (w < h ? w : h) * 0.5f;
            const float thickness =
                inking_detail::ShapeArcThickness(shape.thickness, outerRadius);
            const float midRadius = outerRadius - thickness * 0.5f;
            const float cx = w * 0.5f;
            const float cy = h * 0.5f;
            const float dx = x - cx;
            const float dy = y - cy;
            const float radius = std::sqrt(dx * dx + dy * dy);
            const float half = thickness * 0.5f;
            const float radial = std::fabs(radius - midRadius) - half;
            const float sweep = inking_detail::ShapeArcSweepRadians(shape);

            // 扫过整圈时形状就是完整环带，不再有端头。
            if (sweep >= 2.0f * inking_detail::shapePi - 1e-4f) {
                return radial;
            }
            if (thickness <= 0.0f) {
                // 环宽为零时退化为弧线，端头仍按切平方式处理。
            }

            const float direction = inking_detail::ShapeArcDirection(shape);
            const float start = shape.startAngle * inking_detail::shapeDegToRad;
            const float end = start + direction * sweep;
            /*
             端头半平面的内法向由切向确定。起点法向指向扫过区域，
             终点法向指向扫过区域的反方向。
            */
            const float startNormalX = -std::sin(start) * direction;
            const float startNormalY = std::cos(start) * direction;
            const float endNormalX = std::sin(end) * direction;
            const float endNormalY = -std::cos(end) * direction;
            const float startX = cx + std::cos(start) * midRadius;
            const float startY = cy + std::sin(start) * midRadius;
            const float endX = cx + std::cos(end) * midRadius;
            const float endY = cy + std::sin(end) * midRadius;

            // 半平面的带符号距离为投影的相反数，内部为负。
            const float startPlane = -((x - startX) * startNormalX
                                       + (y - startY) * startNormalY);
            const float endPlane = -((x - endX) * endNormalX
                                     + (y - endY) * endNormalY);
            const float nearest = startPlane > endPlane ? startPlane : endPlane;
            return radial > nearest ? radial : nearest;
        }

        case InkingShapeKind::Rect:
        default: {
            // 直角矩形：按两条中轴线将点映射到右下象限，再调用 ShapeBoxDistance。
            return inking_detail::ShapeBoxDistance(x, y, w * 0.5f, h * 0.5f, w * 0.5f,
                                            h * 0.5f);
        }
    }
}

//命中判定函数：判定组件坐标(x,y)是否在图形shape中
inline bool InkingShapeContains(const InkingShapeSpec& shape, float x, float y, int width,
                          int height) {
    return InkingSignedDistance(shape, x, y, width, height) <= 0.0f;
}

//形状的包围盒AABB，相对于组件左上角
struct InkingShapeBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

//包围盒函数，相对组件左上角、轴向固定的保守或紧致矩形范围，用于快速排除、命中索引和可见性判断，并不等同于组件矩形，也不等同于形状本身。
inline InkingShapeBounds InkingGetShapeBounds(const InkingShapeSpec& shape, int width,
                                  int height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);

    switch (shape.kind) {
        case InkingShapeKind::Circle: {
            // 居中 + 直径 min(w,h)：宽高不等时两侧会留白，包围盒也跟着小。
            const float diameter = w < h ? w : h;
            return InkingShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case InkingShapeKind::Ring: {
            // 环带是从外径往里长的，所以包围盒就是外径那个圆（和 circle 一样）。
            const float diameter = w < h ? w : h;
            return InkingShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case InkingShapeKind::Line: {
            /*
             线段水平放置，包围盒高度为线宽，宽度根据端点形状向外扩展。
             必须包含线宽和端点外扩范围，否则命中索引登记的区域不完整，
             可能导致漏命中。
            */
            const float thickness = shape.thickness > 0.0f ? shape.thickness : 0.0f;
            const float extra = shape.cap == InkingLineCap::Butt ? 0.0f
                                                        : thickness * 0.5f;
            return InkingShapeBounds{-extra, (h - thickness) * 0.5f, w + 2.0f * extra,
                               thickness};
        }
        case InkingShapeKind::Polygon: {
            // 正多边形的包围盒随边数和起始角变化，按顶点计算。
            const float radius = inking_detail::ShapePolygonCircumradius(width, height);
            if (radius <= 0.0f) {
                return InkingShapeBounds{w * 0.5f, h * 0.5f, 0.0f, 0.0f};
            }
            const int sides = inking_detail::ShapePolygonSides(shape);
            const inking_detail::ShapePoint first =
                inking_detail::ShapePolygonVertex(shape, width, height, 0);
            float minX = first.x;
            float minY = first.y;
            float maxX = first.x;
            float maxY = first.y;
            for (int i = 1; i < sides; ++i) {
                const inking_detail::ShapePoint point =
                    inking_detail::ShapePolygonVertex(shape, width, height, i);
                minX = point.x < minX ? point.x : minX;
                minY = point.y < minY ? point.y : minY;
                maxX = point.x > maxX ? point.x : maxX;
                maxY = point.y > maxY ? point.y : maxY;
            }
            return InkingShapeBounds{minX, minY, maxX - minX, maxY - minY};
        }
        case InkingShapeKind::Star: {
            /*
             星形顶点位于外接圆上，因此包围盒取外接圆的正方形范围。
             该范围居中，直径为 min(w, h)。
            */
            const float diameter = w < h ? w : h;
            return InkingShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case InkingShapeKind::Arc: {
            /*
             圆弧带完全位于外径圆内，因此使用外径圆的范围作为保守包围盒。
             按实际角度计算虽可缩小包围盒，但会增加计算复杂度。
            */
            const float diameter = w < h ? w : h;
            return InkingShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case InkingShapeKind::PolygonPath: {
            /*
             任意多边形的包围盒按顶点表计算。多边形可能只占组件区域的一部分，
             也可能偏向一侧，使用顶点范围可以避免索引登记过大的区域。
            */
            const int count = shape.pathPointCount;
            if (count < 3 || shape.pathPoints == nullptr) {
                return InkingShapeBounds{0.0f, 0.0f, w, h};
            }
            float minX = shape.pathPoints[0];
            float minY = shape.pathPoints[1];
            float maxX = minX;
            float maxY = minY;
            for (int i = 1; i < count; ++i) {
                const float px = shape.pathPoints[i * 2];
                const float py = shape.pathPoints[i * 2 + 1];
                minX = px < minX ? px : minX;
                minY = py < minY ? py : minY;
                maxX = px > maxX ? px : maxX;
                maxY = py > maxY ? py : maxY;
            }
            return InkingShapeBounds{minX, minY, maxX - minX, maxY - minY};
        }
        case InkingShapeKind::Capsule:
        case InkingShapeKind::ChamferRect:
        case InkingShapeKind::Superellipse:
        case InkingShapeKind::RoundedRect:
        case InkingShapeKind::Ellipse:
        case InkingShapeKind::Rect:
        default: {
            /*
             这些形状都延伸到组件矩形的边界：圆角矩形和切角矩形只裁去角部，
             胶囊端点与矩形边界相切，超椭圆在横纵轴端点处抵达边界。
            */
            return InkingShapeBounds{0.0f, 0.0f, w, h};
        }
    }
}
