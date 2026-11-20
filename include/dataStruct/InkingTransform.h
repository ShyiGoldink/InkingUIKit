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

// 正向变换函数：将本地坐标 (x, y) 绕旋转中心 (cx, cy)，按照 transform 的变换方向，返回变换后的坐标
inline InkingVec2 InkingApplyTransform(const InkingTransformSpec& transform, float x, float y,
                             float cx, float cy) {
    // 以旋转中心为原点，计算缩放后的相对坐标。
    const float dx = (x - cx) * transform.scale;
    const float dy = (y - cy) * transform.scale;

    // 旋转相对坐标，再加回旋转中心；最后应用平移。
    const float radians = transform.rotate * inking_detail::kTransformDegToRad;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);

    return InkingVec2{cx + dx * cosA - dy * sinA + transform.translateX,
                  cy + dx * sinA + dy * cosA + transform.translateY};
}

// 反向变换函数：将变换后的坐标 (x, y) 绕旋转中心 (cx, cy)，按照 transform 的反向变换，返回本地坐标，用于判断命中
inline InkingVec2 InkingInverseTransform(const InkingTransformSpec& transform, float x, float y,
                               float cx, float cy) {
    // 先撤销平移，并以旋转中心为原点计算相对坐标。
    const float dx = x - transform.translateX - cx;
    const float dy = y - transform.translateY - cy;

    // 撤销旋转（角度取负），再撤销缩放。
    const float radians = transform.rotate * inking_detail::kTransformDegToRad;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);
    const float rx = dx * cosA + dy * sinA;
    const float ry = -dx * sinA + dy * cosA;

    // scale 为 0 时不存在逆变换；这里按 1 处理，避免除零。
    const float scale = transform.scale != 0.0f ? transform.scale : 1.0f;

    return InkingVec2{cx + rx / scale, cy + ry / scale};
}

//两个变换之间线形插值
inline InkingTransformSpec InkingLerpTransform(const InkingTransformSpec& from,
                                   const InkingTransformSpec& to, float t) {
    return InkingTransformSpec{
        from.translateX + (to.translateX - from.translateX) * t,
        from.translateY + (to.translateY - from.translateY) * t,
        from.rotate + (to.rotate - from.rotate) * t,
        from.scale + (to.scale - from.scale) * t};
}

