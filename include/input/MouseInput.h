#pragma once

// 只做前向声明：这个头文件被 InkingWindow.h 包含，包含它不该被迫拖进
// 整个 SDL。注意 SDL3 的 SDL_Event 是 union，前向声明也必须写 union
// （写成 struct 会和真头文件冲突）。
union SDL_Event;

#include <cstdint>

// MouseInput 负责维护鼠标状态；
// 它不直接负责 hit test，也不负责渲染更新。
// 命中检测和事件投递应由 Scene / InputRouter 在下一帧做，
// 这样才能和设计稿里的 isDirty + query 流程一致。
namespace inking {

struct MousePosition {
    int x = 0;
    int y = 0;
};

/// 设计坐标（letterbox 换算之后）。场景层只认这一套。
struct MousePoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct MouseState {
    /// 窗口像素，SDL 事件给的原样。
    MousePosition position{};
    /// 设计坐标，由窗口层换算后写入（每帧一次）。
    MousePoint design{};
    bool leftButtonDown = false;
    bool rightButtonDown = false;
    bool middleButtonDown = false;
    bool moved = false;

    /**
     * **本帧累积的滚轮增量**（A4）。
     *
     * 语义与 `moved` 同一档：它是**这一帧发生过的事**，不是跨帧状态——
     * 所以 `ResetFrameFlags()` 会把它清零。一帧里收到多个滚轮事件会**累加**
     * （触控板的高频小步长不该被后一个事件吃掉），派发时一次交给组件。
     *
     * 方向按 SDL3 的口径（`SDL_MOUSEWHEEL_FLIPPED` 已在 `UpdateFromSDL` 里
     * 翻正）：`wheelY > 0` = 远离用户（滚轮向上 / 内容上滚），
     * `wheelX > 0` = 向右。数值是**滚动量**，不是"格数"——鼠标滚轮通常
     * 一步 ±1，触控板会是小数。
     */
    float wheelX = 0.0f;
    float wheelY = 0.0f;
};

class MouseInput {
public:
    MouseInput() = default;

    // 由 SDL3 事件循环直接驱动：移动、按下、抬起都在这里更新状态。
    void UpdateFromSDL(const SDL_Event& event);

    // 窗口坐标 → 设计坐标的换算结果写进来。
    // 换算是渲染器的事（letterbox 有偏移和留边），所以由窗口层算完再写，
    // MouseInput 自己不去碰渲染器。
    void SetDesignPosition(float x, float y) noexcept;

    /**
     * 窗口的像素尺寸（B6）：**触摸事件给的是归一化坐标**（0~1），要乘它才是
     * 窗口坐标——之后的换算与鼠标共用同一条路，所以触摸不需要第二条通路。
     *
     * 由窗口层在每帧的事件泵开头喂一次（`SDL_GetWindowSize` 很便宜，而且这样
     * 缩放窗口之后不用额外记"什么时候该更新"）。
     */
    void SetWindowSize(float width, float height) noexcept;

    /// 当前有手指按着吗（触摸合成的那个"指针"）。
    bool IsTouchActive() const noexcept;

    /// 现在认的是哪根手指（0 = 没有；观测口 / 自检用）。
    std::int64_t GetTouchFinger() const noexcept;

    // 每一帧结束时清理瞬时状态，避免 moved 持续影响后续逻辑。
    void ResetFrameFlags() noexcept;

    const MouseState& GetState() const noexcept;
    bool IsLeftDown() const noexcept;
    bool IsRightDown() const noexcept;
    bool IsMiddleDown() const noexcept;
    bool IsMoved() const noexcept;

    /// 本帧的滚轮增量（见 `MouseState::wheelX` / `wheelY`）。
    float GetWheelX() const noexcept;
    float GetWheelY() const noexcept;

    /// 本帧滚过轮子吗（两个方向任意一个有量就算）。
    bool IsWheelScrolled() const noexcept;

private:
    MouseState state_{};

    /// 触摸（B6）：**只认第一根按下的手指**。
    ///
    /// 多指针的结论是"这一版不做"（`InputDesign.md` §15）：两根手指同时按会互相
    /// 抢一个指针状态，表现是"点击乱跳"——那比"只认第一根"糟得多。所以后来的
    /// 手指**明确忽略**：它的 down / motion / up 都不影响指针。第一根抬起之后，
    /// 下一根按下的手指可以接管（`_touchFinger` 归零）。
    std::int64_t touchFinger_ = 0;
    bool touchActive_ = false;

    /// 窗口像素尺寸：触摸的归一化坐标乘它才是窗口坐标。
    float windowWidth_ = 0.0f;
    float windowHeight_ = 0.0f;
};

}  // namespace inking
