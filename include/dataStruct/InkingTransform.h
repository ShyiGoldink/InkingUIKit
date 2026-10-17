#pragma once

// 仿射变换（平移 / 旋转 / 缩放）这是变换通道，不是布局通道。
//
//  本项目的变换通道和布局通道是不一样的。
//  把两者分得很清楚，这份文件只描述前者：
//   变换通道（translate / scale / rotate）不改命中表。
// 表按"本地几何"烘， 查询时把点**反变换**回本地空间。于是静态组件也能有它，命中表照样有效，
//   只是每帧多一次反变换（几个乘加）。
//   布局通道（width / height / 会引发兄弟重排的定位）：必须重烘受影响的子树，
//   那是每态一份表或动态档的事，不在本文件的范围里。
//   不碰本地几何（形状、尺寸、锚点一个字节都不动），
//   允许变换，因为变换不改表，静态档"进命中表"的前提没被破坏。
// 两个容易写错的地方，先在这里定死
//
// 1. 原点取组件中心（等价 CSS 的 `transform-origin: 50% 50%`）。形状都是按
//    左上角定义的，绕左上角转起来不是直觉里的样子：一个 200×100 的按钮转 90°
//    会整个跑到左边去。
// 2. rotate 的单位是度，顺时针为正（屏幕坐标 y 向下，所以数学上的正角看起来就是顺时针）。
// 理由与 CSS 一致：配置里写 `15` 比写 `0.2618` 直观。
// 变换的正向用于绘制（本地 → 屏幕），反向用于命中（屏幕 → 本地）。
// 两者必须严格互逆，否则就会回到"画的是一个、点的是另一个"那条老路，
// 所以它们挨着写在这里，而不是各写各的。

#include <cmath>
#include <cstdint>

namespace inking {

// 设计坐标系里的一个点。
struct Point2 {
    float x = 0.0f;
    float y = 0.0f;
};

// 一个状态的目标变换。缺省（全零 + scale 1）是单位变换。
struct TransformSpec {
    float translateX = 0.0f;  // 设计坐标
    float translateY = 0.0f;  // 设计坐标
    float rotate = 0.0f;      // 度，顺时针为正
    float scale = 1.0f;       // 均匀缩放；负值等于镜像（允许，但很少用）

    constexpr bool IsIdentity() const noexcept {
        return translateX == 0.0f && translateY == 0.0f && rotate == 0.0f
            && scale == 1.0f;
    }
};

// 两个变换按位相等。用来判断"这次切换其实什么都没变"（那就连过渡都不用起）。
constexpr bool SameTransform(const TransformSpec& a,
                             const TransformSpec& b) noexcept {
    return a.translateX == b.translateX && a.translateY == b.translateY
        && a.rotate == b.rotate && a.scale == b.scale;
}

namespace detail {
// 名字带 transform 前缀是必须的：`inking::detail::kPi` 这种通用名字
// 框架里已经有别的实现在用（例如 InkingDraw.cpp 的形状填充），
// 重名不会报"重复定义"，只会在使用处报"引用不明确"，更难查。
constexpr float kTransformPi = 3.14159265358979323846f;
constexpr float kTransformDegToRad = kTransformPi / 180.0f;
}  // namespace detail

/**
 * @brief 正向：本地坐标 → 变换后的坐标（绕 `(cx, cy)`）。
 *
 * 顺序是 CSS 的 `translate(...) rotate(...) scale(...)`：
 * 先绕中心缩放、再旋转、最后平移。绘制用。
 */
inline Point2 ApplyTransform(const TransformSpec& transform, float x, float y,
                             float cx, float cy) {
    const float radians = transform.rotate * detail::kTransformDegToRad;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);

    const float dx = (x - cx) * transform.scale;
    const float dy = (y - cy) * transform.scale;

    return Point2{cx + dx * cosA - dy * sinA + transform.translateX,
                  cy + dx * sinA + dy * cosA + transform.translateY};
}

/**
 * @brief 反向：变换后的坐标 → 本地坐标。命中用。
 *
 * 它必须与 `ApplyTransform` 严格互逆（顺序反过来、角度取负、缩放取倒数）。
 * `scale == 0` 时没有逆（形状被压成一个点），这里退回按 1 处理——
 * 那是配置错误（生成期就会拦），但宁可不命中，也不要除零得到 inf 之后到处乱命中。
 */
inline Point2 InverseTransform(const TransformSpec& transform, float x, float y,
                               float cx, float cy) {
    const float radians = transform.rotate * detail::kTransformDegToRad;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);

    // 先把平移和中心一起减掉
    const float dx = x - transform.translateX - cx;
    const float dy = y - transform.translateY - cy;

    // 反向旋转（转置），再反向缩放
    const float rx = dx * cosA + dy * sinA;
    const float ry = -dx * sinA + dy * cosA;
    const float scale = transform.scale != 0.0f ? transform.scale : 1.0f;

    return Point2{cx + rx / scale, cy + ry / scale};
}

/**
 * @brief 两个变换之间线性插值。
 *
 * 角度也是直接线性插
 * 一方面是相对方便差值，一方面是为了尊重用户意图
 */
inline TransformSpec LerpTransform(const TransformSpec& from,
                                   const TransformSpec& to, float t) {
    return TransformSpec{
        from.translateX + (to.translateX - from.translateX) * t,
        from.translateY + (to.translateY - from.translateY) * t,
        from.rotate + (to.rotate - from.rotate) * t,
        from.scale + (to.scale - from.scale) * t};
}

}  // namespace inking
