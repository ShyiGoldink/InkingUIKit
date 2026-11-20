#pragma once
/*
InkingTransform为图形的位置变换，包括平移，旋转，缩放提供了通道
InkingTransform不持有任何几何状态，只提供变换的模式
*/

#include <cmath>
#include <cstdint>

// 设计坐标系里的一个点（也当二维偏移用，所以叫 Vec 而不是 Point）
struct InkingVec2 {
    float x = 0.0f;
    float y = 0.0f;
};
 
//InkingTransformSpec记录了状态变换的目标，缺省（scale为1，其余为0）则意味着不做变换
struct InkingTransformSpec {
    float translateX = 0.0f;  // 设计坐标
    float translateY = 0.0f;  // 设计坐标
    float rotate = 0.0f;      // 度，顺时针为正
    float scale = 1.0f;       // 均匀缩放；负值等于镜像

    constexpr bool IsIdentity() const noexcept {
        return translateX == 0.0f && translateY == 0.0f && rotate == 0.0f
            && scale == 1.0f;
    }
};

// 两个变换按位相等。用来判断"这次切换其实什么都没变"（那就连过渡都不用起）。
constexpr bool InkingSameTransform(const InkingTransformSpec& a,
                             const InkingTransformSpec& b) noexcept {
    return a.translateX == b.translateX && a.translateY == b.translateY
        && a.rotate == b.rotate && a.scale == b.scale;
}

namespace inking_detail {
// 名字带 transform 前缀来和其它可能相同的名称进行区分
constexpr float kTransformPi = 3.14159265358979323846f;
constexpr float kTransformDegToRad = kTransformPi / 180.0f;
}  // namespace inking_detail

/**
 * @brief 正向：本地坐标 → 变换后的坐标（绕 `(cx, cy)`）。
 *
 * 顺序是 CSS 的 `translate(...) rotate(...) scale(...)`：
 * 先绕中心缩放、再旋转、最后平移。绘制用。
 */
inline InkingVec2 InkingApplyTransform(const InkingTransformSpec& transform, float x, float y,
                             float cx, float cy) {
    const float radians = transform.rotate * inking_detail::kTransformDegToRad;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);

    const float dx = (x - cx) * transform.scale;
    const float dy = (y - cy) * transform.scale;

    return InkingVec2{cx + dx * cosA - dy * sinA + transform.translateX,
                  cy + dx * sinA + dy * cosA + transform.translateY};
}

/**
 * @brief 反向：变换后的坐标 → 本地坐标。命中用。
 *
 * 它必须与 `InkingApplyTransform` 严格互逆（顺序反过来、角度取负、缩放取倒数）。
 * `scale == 0` 时没有逆（形状被压成一个点），这里退回按 1 处理——
 * 那是配置错误（生成期就会拦），但宁可不命中，也不要除零得到 inf 之后到处乱命中。
 */
inline InkingVec2 InkingInverseTransform(const InkingTransformSpec& transform, float x, float y,
                               float cx, float cy) {
    const float radians = transform.rotate * inking_detail::kTransformDegToRad;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);

    // 先把平移和中心一起减掉
    const float dx = x - transform.translateX - cx;
    const float dy = y - transform.translateY - cy;

    // 反向旋转（转置），再反向缩放
    const float rx = dx * cosA + dy * sinA;
    const float ry = -dx * sinA + dy * cosA;
    const float scale = transform.scale != 0.0f ? transform.scale : 1.0f;

    return InkingVec2{cx + rx / scale, cy + ry / scale};
}

/**
 * @brief 两个变换之间线性插值。
 *
 * 角度也是直接线性插
 * 一方面是相对方便差值，一方面是为了尊重用户意图
 */
inline InkingTransformSpec InkingLerpTransform(const InkingTransformSpec& from,
                                   const InkingTransformSpec& to, float t) {
    return InkingTransformSpec{
        from.translateX + (to.translateX - from.translateX) * t,
        from.translateY + (to.translateY - from.translateY) * t,
        from.rotate + (to.rotate - from.rotate) * t,
        from.scale + (to.scale - from.scale) * t};
}

