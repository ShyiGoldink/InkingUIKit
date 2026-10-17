#pragma once

// 过渡的**缓动曲线**：把"线性进度"变成"实际画出来的进度"。
//
// 一条硬约定：缓动**只改变进度**，不改变时长、起点、终点。
//   - `ApplyEasing(..., 0) == 0`、`ApplyEasing(..., 1) == 1` 是精确的（不是
//     "约等于"）——起点和终点必须由曲线本身对齐，不能指望调用方补偿；
//   - 时长与曲线**互相独立**：同一段 0.15s 的过渡，换曲线不改时长。
//
// 为什么是这么一组（`TaskGuide.md` 的 T-2）：自定义 `cubic-bezier` 要解析
// 四个控制点、还得想清楚怎么写进文档，先给一组**具名曲线**就够用。名字跟
// CSS 的 `transition-timing-function` 对齐，配置里写驼峰：
//
//   linear（缺省）/ easeIn / easeOut / easeInOut
//
// 具体算式取的是**平方式**（Penner 那套里最基础的一组）：
//
//   linear      k
//   easeIn      k²                    （开头慢）
//   easeOut     1 − (1−k)²            （结尾慢）
//   easeInOut   2k²  /  1 − 2(1−k)²   （前半段 / 后半段，关于中点对称）
//
// 为什么是二次而不是三次：按钮的过渡通常只有 0.1~0.3 秒，二次已经能明显看出
// "快进慢出"，三次在这么短的时间里太冲。另外它与 CSS 默认的
// `ease` / `ease-in`（`cubic-bezier(0.25,0.1,0.25,1)` 那类）**不是同一组数**——
// 那要解方程，这里写清楚我们用的是哪一条，别让人以为能跟 CSS 逐帧对上。

#include <cstdint>

namespace inking {

/// 一组具名缓动曲线。`Linear` 是缺省（= 配置里不写 `easing`）。
enum class Easing : std::uint8_t {
    Linear = 0,
    EaseIn,
    EaseOut,
    EaseInOut,
};

/// 曲线在配置里的写法。报错信息要列取值、自检要打印，都用它。
inline const char* EasingName(Easing easing) {
    switch (easing) {
        case Easing::EaseIn:
            return "easeIn";
        case Easing::EaseOut:
            return "easeOut";
        case Easing::EaseInOut:
            return "easeInOut";
        case Easing::Linear:
        default:
            return "linear";
    }
}

/**
 * @brief 把线性进度 `k` 过一遍曲线。
 *
 * @param k 线性进度（0 = 刚开始、1 = 已到位）。超出 [0,1] 的输入被夹住：
 *          调用方本来就只在 [0,1) 里给值，这里夹一次是防"拿它当公共函数用"。
 * @return 缓动后的进度；端点精确（`k <= 0` 给 0，`k >= 1` 给 1）。
 *
 * 单调递增、`f(k) <= k` 还是 `>= k` 由曲线决定（easeIn 前段 <= k、
 * easeOut 前段 >= k），这正是"手感"的来源，也是自检里最能钉住的一条。
 */
inline float ApplyEasing(Easing easing, float k) {
    if (k <= 0.0f) {
        return 0.0f;
    }
    if (k >= 1.0f) {
        return 1.0f;
    }

    switch (easing) {
        case Easing::EaseIn:
            return k * k;

        case Easing::EaseOut: {
            const float inverse = 1.0f - k;
            return 1.0f - inverse * inverse;
        }

        case Easing::EaseInOut: {
            // 关于中点对称的两段抛物线：在 k = 0.5 处两边都等于 0.5、斜率也
            // 都是 2（连续可导），所以中点不会出现"折一下"的观感。
            if (k < 0.5f) {
                return 2.0f * k * k;
            }
            const float inverse = 1.0f - k;
            return 1.0f - 2.0f * inverse * inverse;
        }

        case Easing::Linear:
        default:
            return k;
    }
}

}  // namespace inking
