#include <input/KeyboardInput.h>

#include <SDL3/SDL.h>

#include <algorithm>

namespace inking {

namespace {

/// SDL 修饰键掩码 → 四个 bool。
void ReadModifiers(std::uint16_t mod, bool& shift, bool& ctrl, bool& alt,
                   bool& super) {
    shift = (mod & SDL_KMOD_SHIFT) != 0;
    ctrl = (mod & SDL_KMOD_CTRL) != 0;
    alt = (mod & SDL_KMOD_ALT) != 0;
    super = (mod & SDL_KMOD_GUI) != 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// KeyboardState
// ---------------------------------------------------------------------------

bool KeyboardState::IsKeyDown(std::int32_t key) const noexcept {
    return std::any_of(_down.begin(), _down.end(),
                       [key](const DownKey& down) { return down.key == key; });
}

bool KeyboardState::IsScancodeDown(std::int32_t scancode) const noexcept {
    return std::any_of(
        _down.begin(), _down.end(),
        [scancode](const DownKey& down) { return down.scancode == scancode; });
}

void KeyboardState::Press(std::int32_t key, std::int32_t scancode) noexcept {
    if (!IsKeyDown(key)) {
        _down.push_back(DownKey{key, scancode});
    }
}

void KeyboardState::Release(std::int32_t key) noexcept {
    // 按 key 移除。SDL 的抬起事件带同一个 key，所以这里够用；
    // 用 erase-remove 而不是"找到第一个就删"，是为了让重复条目不可能积累
    // （`Press` 已经查过重了，这里是第二道保险）。
    _down.erase(std::remove_if(_down.begin(), _down.end(),
                               [key](const DownKey& down) {
                                   return down.key == key;
                               }),
                _down.end());
}

void KeyboardState::SetModifiers(bool shift, bool ctrl, bool alt,
                                 bool super) noexcept {
    _shift = shift;
    _ctrl = ctrl;
    _alt = alt;
    _super = super;
}

// ---------------------------------------------------------------------------
// KeyboardInput
// ---------------------------------------------------------------------------

void KeyboardInput::UpdateFromSDL(const union SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const bool isDown = event.type == SDL_EVENT_KEY_DOWN;
            const auto key = static_cast<std::int32_t>(event.key.key);
            const auto scancode = static_cast<std::int32_t>(event.key.scancode);

            bool shift = false;
            bool ctrl = false;
            bool alt = false;
            bool super = false;
            ReadModifiers(event.key.mod, shift, ctrl, alt, super);
            _state.SetModifiers(shift, ctrl, alt, super);

            KeyInputEvent item;
            item.kind = isDown ? KeyInputEvent::Kind::Down
                               : KeyInputEvent::Kind::Up;
            item.key.key = key;
            item.key.scancode = scancode;
            item.key.repeat = event.key.repeat;
            item.key.shift = shift;
            item.key.ctrl = ctrl;
            item.key.alt = alt;
            item.key.super = super;
            _events.push_back(std::move(item));

            // 状态跟着事件走。注意**先记事件再改状态**：事件里带的是"这一刻"的
            // 修饰键快照，与状态无关；顺序反了也不会错，但保持"事件是事实、
            // 状态是结果"更好读。
            if (isDown) {
                _state.Press(key, scancode);
            } else {
                _state.Release(key);
            }
            break;
        }

        case SDL_EVENT_TEXT_INPUT: {
            // 输入法已经帮我们组合好了，这里拿到的就是"用户想输入的那串文本"。
            // 空串不发事件（SDL 理论上不会给空的，但没必要为它制造一条无意义的事件）。
            if (event.text.text == nullptr || event.text.text[0] == '\0') {
                break;
            }
            KeyInputEvent item;
            item.kind = KeyInputEvent::Kind::Text;
            item.text = event.text.text;
            _events.push_back(std::move(item));
            break;
        }

        default:
            break;
    }
}

bool KeyboardInput::ConsumeFrameEvent(std::size_t index) noexcept {
    if (index >= _events.size()) {
        return false;
    }
    // 单条删除（不是 erase-remove）：调用方拿的是**下标**，删一条就少一条，
    // 所以要倒序调用——这条约定写在 `ShortcutRegistry::Dispatch` 里。
    _events.erase(_events.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void KeyboardInput::ResetFrameFlags() noexcept {
    // 只清本帧事件。按下的键留着——那是跨帧状态（见头文件里的说明）。
    //
    // 用 clear() 而不是"换成空 vector"：容量留着，下一帧不必重新分配。
    _events.clear();
}

}  // namespace inking
