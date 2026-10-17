#pragma once

#include <cstdint>

namespace inking {

/**
 * @brief 滚动容器（TaskGuide B12）的配置与**纯几何**。
 *
 * 为什么单独一个头、而且几何全是自由函数：滚动条的"轨道在哪、滑块多长、点轨道
 * 跳到哪"是**纯算术**，跟渲染器、跟组件、跟场景都没关系。写成纯函数就能
 * 纯数字自检（`ink_scroll_test`），也就不会出现"看着对、量出来不对"那种事——
 * 这正是这一层最容易返工的地方，规则先钉死。
 *
 * ---------------------------------------------------------------------------
 * 这一版按用户裁定的范围做（2026-10-05）：
 *   - 滚轮步长 = **固定设计像素**（`wheelStep`），**不做**"视口比例"那一档——
 *     这个框架的尺寸本来就是设计像素，滚动量也用同一套（内容左上角对齐容器
 *     左上角，偏移就是滚动量）；
 *   - **不做缓动**：滚轮与点轨道都瞬时到位；
 *   - 点轨道 = **跳过去**（滑块中心落到点击处）；
 *   - 滚轮**容器优先**（这一层不管，实现层给"沿父链优先拿滚轮"的机制）。
 *
 * **不做**：惯性 / 越界回弹 / 吸附对齐、触摸"拖内容"、虚拟化（B9）、
 * 滑动条挤占内容区（**浮在内容之上**）。
 */

/// 一条轴上的滚动条什么时候显示。
enum class ScrollAxisMode {
    Never,   ///< 不画（但那条轴照样能滚，比如只靠滚轮 / 触摸）
    Auto,    ///< 内容超出可视区才显示（默认）
    Always,  ///< 一直显示
};

/// 容器的滚动配置（没配 `"scroll"` 段就是普通容器）。
struct ScrollSpec {
    bool enabled = false;

    bool vertical = true;    ///< 能不能上下滚
    bool horizontal = false; ///< 能不能左右滚

    /// 一格滚轮滚多少（**设计像素**）。触控板的高频小步长会按比例缩小，
    /// 由实现层按"这一帧的滚轮增量"算，这里只给基准。
    float wheelStep = 48.0f;

    ScrollAxisMode verticalBar = ScrollAxisMode::Auto;
    ScrollAxisMode horizontalBar = ScrollAxisMode::Auto;

    float barWidth = 8.0f;        ///< 条的粗细
    float barInset = 4.0f;        ///< 离容器边缘多远
    float minThumbLength = 24.0f; ///< 滑块最短（内容超长时不至于细如发丝）

    /// 条的圆角：**0 = 胶囊**（取短边的一半，默认观感）；> 0 = 指定圆角半径。
    /// 轨道与滑块各自按自己的短边算，所以两条轴、两种粗细都是一致的观感。
    float barRadius = 0.0f;

    /// 条带区域的**背景色**（0 = 不铺，默认）：铺了之后滑到条下面的内容会被挡住。
    ///
    /// 这一条与"滑出去的内容被裁剪"是两件事：裁剪剪的是**超出容器本体**的部分，
    /// 而滚动条是**浮在内容之上**的一条——不铺背景时，内容会从轨道与滑块的
    /// 缝隙里透出来。想要"条带区域干净"就配一个不透明的颜色。
    std::uint32_t barBackgroundColor = 0x00000000u;

    std::uint32_t barColor = 0x66000000u;       ///< 轨道底色（半透明黑）
    std::uint32_t thumbColor = 0xCCEAF2FFu;     ///< 滑块
    std::uint32_t thumbHoverColor = 0xFFEAF2FFu;///< 拖动 / 悬停时的滑块
};

/// 一条滚动条算出来的几何（都在**容器本地坐标**里，设计像素）。
struct ScrollBarMetrics {
    bool visible = false;

    float trackX = 0.0f;
    float trackY = 0.0f;
    float trackWidth = 0.0f;
    float trackHeight = 0.0f;

    float thumbX = 0.0f;
    float thumbY = 0.0f;
    float thumbWidth = 0.0f;
    float thumbHeight = 0.0f;
};

/// 可滚范围：内容比视口大多少（内容没超出就是 0）。**纯函数**。
float ScrollRange(float contentSize, float viewportSize) noexcept;

/// 把滚动量夹进 `[0, range]`（`range <= 0` 一律归 0）。**纯函数**。
float ClampScroll(float offset, float range) noexcept;

/**
 * 算一条滚动条的轨道与滑块。**纯函数**（不碰渲染器、不碰组件）。
 *
 * @param vertical       true = 竖直那条（贴右边），false = 水平那条（贴底边）
 * @param viewportWidth  容器宽（设计像素）
 * @param viewportHeight 容器高
 * @param contentSize    这个方向上的**内容尺寸**（量出来的）
 * @param offset         当前滚动量（会被夹）
 * @param range          可滚范围（`ScrollRange` 的结果）
 * @param spec           配置
 */
ScrollBarMetrics ComputeScrollBar(bool vertical, float viewportWidth,
                                  float viewportHeight, float contentSize,
                                  float offset, float range,
                                  const ScrollSpec& spec) noexcept;

/**
 * 点轨道（滑块以外的地方）**跳过去**：滑块中心落到点击处。**纯函数**。
 *
 * @return 新的滚动量（已夹进 `[0, range]`）
 */
float ScrollFromTrackClick(bool vertical, float clickX, float clickY,
                           const ScrollBarMetrics& bar, float range) noexcept;

/// 这一点落在滑块上吗（按下时用它决定"是拖滑块还是跳过去"）。
bool HitsThumb(bool vertical, float x, float y,
               const ScrollBarMetrics& bar) noexcept;

}  // namespace inking
