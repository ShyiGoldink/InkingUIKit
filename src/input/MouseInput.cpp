#include <input/MouseInput.h>

#include <SDL3/SDL.h>

namespace inking {

const MouseState& MouseInput::GetState() const noexcept {
    return state_;
}

void MouseInput::SetDesignPosition(float x, float y) noexcept {
    state_.design.x = x;
    state_.design.y = y;
}

void MouseInput::SetWindowSize(float width, float height) noexcept {
    // 触摸的归一化坐标要乘它。宽高为 0（窗口还没建好）时触摸会被算到 (0,0)
    // ——那种情况下本来也没有触摸事件。
    windowWidth_ = width > 0.0f ? width : 0.0f;
    windowHeight_ = height > 0.0f ? height : 0.0f;
}

bool MouseInput::IsTouchActive() const noexcept { return touchActive_; }

std::int64_t MouseInput::GetTouchFinger() const noexcept { return touchFinger_; }

bool MouseInput::IsLeftDown() const noexcept {
    return state_.leftButtonDown;
}

bool MouseInput::IsRightDown() const noexcept {
    return state_.rightButtonDown;
}

bool MouseInput::IsMiddleDown() const noexcept {
    return state_.middleButtonDown;
}

bool MouseInput::IsMoved() const noexcept {
    return state_.moved;
}

float MouseInput::GetWheelX() const noexcept {
    return state_.wheelX;
}

float MouseInput::GetWheelY() const noexcept {
    return state_.wheelY;
}

bool MouseInput::IsWheelScrolled() const noexcept {
    return state_.wheelX != 0.0f || state_.wheelY != 0.0f;
}

void MouseInput::ResetFrameFlags() noexcept {
    state_.moved = false;
    // 滚轮和 moved 同一档：都是"这一帧发生过的事"。累积值交给场景派发过了，
    // 不清掉的话下一帧会被重复派发一次（帧率越高滚得越多）。
    state_.wheelX = 0.0f;
    state_.wheelY = 0.0f;
}

void MouseInput::UpdateFromSDL(const union SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION: {
        // 运动事件中，坐标直接更新为最新位置。
        state_.position.x = static_cast<int>(event.motion.x);
        state_.position.y = static_cast<int>(event.motion.y);
        state_.moved = true;
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        switch (event.button.button) {
        case SDL_BUTTON_LEFT:
            state_.leftButtonDown = true;
            break;
        case SDL_BUTTON_RIGHT:
            state_.rightButtonDown = true;
            break;
        case SDL_BUTTON_MIDDLE:
            state_.middleButtonDown = true;
            break;
        default:
            break;
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP: {
        switch (event.button.button) {
        case SDL_BUTTON_LEFT:
            state_.leftButtonDown = false;
            break;
        case SDL_BUTTON_RIGHT:
            state_.rightButtonDown = false;
            break;
        case SDL_BUTTON_MIDDLE:
            state_.middleButtonDown = false;
            break;
        default:
            break;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // 触摸（B6）：**最简映射**——把第一根手指当成鼠标
    //
    // SDL3 的 finger 事件给的是**归一化**坐标（0~1，相对窗口），乘窗口像素尺寸
    // 就是窗口坐标；之后的"窗口坐标 → 设计坐标"由窗口层那条现成的路算
    // （`toDesign`，它会处理 letterbox）——触摸因此不需要第二条通路。
    //
    // 三条规矩：
    //   1. **只认第一根按下的手指**（多指针这一版不做，见 InputDesign.md §15）：
    //      后来的手指明确忽略。两根手指抢一个指针状态的表现是"点击乱跳"，
    //      比"只认第一根"糟得多；
    //   2. 抬起（`FINGER_UP`）与**被系统收走**（`FINGER_CANCELED`）都算抬起
    //      ——后者不处理的话，指针会永远停在"按着"，和 §6 第 59 条那条
    //      "窗口失焦时抬起事件不会来"是同一类坑；
    //   3. 触摸合成的是**左键**（和鼠标点一下同一个语义），所以业务侧不用写分支。
    // -----------------------------------------------------------------------
    case SDL_EVENT_FINGER_DOWN: {
        if (touchActive_) {
            // 已经有手指按着：后来的忽略（多指针的结论）。
            break;
        }
        touchActive_ = true;
        touchFinger_ = static_cast<std::int64_t>(event.tfinger.fingerID);
        state_.position.x = static_cast<int>(event.tfinger.x * windowWidth_);
        state_.position.y = static_cast<int>(event.tfinger.y * windowHeight_);
        state_.leftButtonDown = true;
        state_.moved = true;
        break;
    }

    case SDL_EVENT_FINGER_MOTION: {
        if (!touchActive_
            || static_cast<std::int64_t>(event.tfinger.fingerID) != touchFinger_) {
            break;
        }
        state_.position.x = static_cast<int>(event.tfinger.x * windowWidth_);
        state_.position.y = static_cast<int>(event.tfinger.y * windowHeight_);
        state_.moved = true;
        break;
    }

    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        if (!touchActive_
            || static_cast<std::int64_t>(event.tfinger.fingerID) != touchFinger_) {
            break;
        }
        // 抬手位置也更新一次：最后那一下点在哪，命中判定就该按哪算
        // （手指划出去再抬起时，这一点尤其明显）。
        state_.position.x = static_cast<int>(event.tfinger.x * windowWidth_);
        state_.position.y = static_cast<int>(event.tfinger.y * windowHeight_);
        state_.moved = true;
        state_.leftButtonDown = false;
        touchActive_ = false;
        touchFinger_ = 0;
        break;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
        // 滚轮是**瞬时量**（不像按键是跨帧状态）：本帧累积，帧末清零。
        // 一帧里可能来好几个事件（触控板尤其），累加而不是覆盖——
        // 后一个事件盖掉前一个就是"滚得快反而滚得少"。
        float dx = event.wheel.x;
        float dy = event.wheel.y;

        // SDL 说 FLIPPED 表示"自然滚动"之类的系统偏好把方向反了，
        // 这里翻正，好让上层永远按同一个坐标系理解（正 = 远离用户 / 向右）。
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            dx = -dx;
            dy = -dy;
        }
        state_.wheelX += dx;
        state_.wheelY += dy;

        // 顺手用滚轮事件自带的光标位置更新窗口坐标：滚轮事件不一定伴随
        // MOTION 事件（尤其"不动鼠标只滚轮"），不更新的话这一帧的设计坐标
        // 会停在旧位置——"滚轮派发给谁"就会发给上一个位置下面的组件。
        state_.position.x = static_cast<int>(event.wheel.mouse_x);
        state_.position.y = static_cast<int>(event.wheel.mouse_y);
        break;
    }

    default:
        break;
    }
}

}  // namespace inking
