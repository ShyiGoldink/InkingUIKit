#pragma once

// 相似仿射：**均匀缩放 + 旋转 + 平移**。T-7 的子树变换靠它复合。
//
// 为什么不能只用 `TransformSpec`：`TransformSpec` 表达的是"绕**某个中心**做缩放 +
// 旋转，再平移"——中心是它语义的一部分。容器把变换作用到**子节点**时，旋转中心是
// **容器**的中心（不是子节点的），于是"复合"的结果没法再塞回一个
// `TransformSpec`（它的中心已经固定是"自己的中心"）。相似仿射没有这个问题：
// 相似仿射的复合仍然是相似仿射，闭包成立。
//
// 形式（和 `ApplyTransform` 同一套屏幕坐标约定：y 向下，正角 = 顺时针）：
//   x' = a·x − b·y + tx
//   y' = a·y + b·x + ty
// 其中 a = s·cosθ、b = s·sinθ。单位阵是 a=1、b=0、tx=ty=0。

#include <dataStruct/InkingTransform.h>

#include <cmath>

namespace inking {

struct Affine {
    float a = 1.0f;
    float b = 0.0f;
    float tx = 0.0f;
    float ty = 0.0f;

    constexpr bool IsIdentity() const noexcept {
        return a == 1.0f && b == 0.0f && tx == 0.0f && ty == 0.0f;
    }
};

inline bool SameAffine(const Affine& x, const Affine& y) noexcept {
    return x.a == y.a && x.b == y.b && x.tx == y.tx && x.ty == y.ty;
}

/// 把一个点过一遍仿射。
inline Point2 ApplyAffine(const Affine& m, float x, float y) noexcept {
    return Point2{m.a * x - m.b * y + m.tx, m.a * y + m.b * x + m.ty};
}

/// `outer ∘ inner`：先 inner 再 outer（读起来就是"外层套内层"）。
inline Affine ComposeAffine(const Affine& outer, const Affine& inner) noexcept {
    Affine out;
    out.a = outer.a * inner.a - outer.b * inner.b;
    out.b = outer.a * inner.b + outer.b * inner.a;
    out.tx = outer.a * inner.tx - outer.b * inner.ty + outer.tx;
    out.ty = outer.a * inner.ty + outer.b * inner.tx + outer.ty;
    return out;
}

/**
 * `TransformSpec` 绕 `(cx, cy)` 作用那一下，写成一个仿射。
 *
 * 和 `ApplyTransform` 严格同一个语义：先绕中心缩放、再旋转、最后平移——
 * 因为 `ApplyTransform` 自己的结果就是这四项拼出来的。
 */
inline Affine AffineFromTransform(const TransformSpec& transform, float cx,
                                  float cy) noexcept {
    const float radians = transform.rotate * detail::kTransformDegToRad;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);
    const float scale = transform.scale;

    Affine m;
    m.a = scale * cosA;
    m.b = scale * sinA;
    // 把"绕中心"折进平移项：T(cx,cy) ∘ R·S ∘ T(-cx,-cy) 再叠上 translate。
    m.tx = cx + transform.translateX - (m.a * cx - m.b * cy);
    m.ty = cy + transform.translateY - (m.a * cy + m.b * cx);
    return m;
}

/// 逆变换（相似仿射的逆仍是相似仿射）。退化（a=b=0）时返回单位阵——
/// 那是配置错误（scale 不能为 0），宁可不命中也不要除出 inf。
inline Affine InvertAffine(const Affine& m) noexcept {
    const float det = m.a * m.a + m.b * m.b;
    if (det <= 0.0f) {
        return Affine{};
    }
    Affine out;
    out.a = m.a / det;
    out.b = -m.b / det;
    out.tx = -(out.a * m.tx - out.b * m.ty);
    out.ty = -(out.a * m.ty + out.b * m.tx);
    return out;
}

}  // namespace inking
