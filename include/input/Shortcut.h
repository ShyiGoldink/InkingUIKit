#pragma once

// 快捷键：**一个值类型**，不是"某个按钮的属性"。
//
// 为什么要单独一个类型（而不是让 MenuBar 存一对 (mods, key)）：
//   1. 它要能**从字符串来**（"Ctrl+Shift+S"）。本项目的配置最终都从 CXXCSS json
//      读（`CXXCSS.md`），菜单配置里写 `"shortcut": "Ctrl+S"` 是迟早的事；
//      现在就把 `Parse` / `ToString` 的双向定下来，避免那一轮再改 API；
//   2. 它要能**被比较**（冲突检测）、被日志打印、被自检断言；
//   3. 它是**纯数据**：不含 SDL 头（key 存 `SDL_Keycode` 的数值），所以能进公开头。
//
// ---------------------------------------------------------------------------
// 两条语义，先钉住
//
// ① **修饰键完全匹配**。绑了 `Ctrl+S`，按 `Ctrl+Shift+S` **不**触发。
//    理由：宽松匹配会让"保存"吃掉将来的"另存为"，而那种 bug 极难发现
//    （按下没反应、或者两个动作都发生）。要宽松匹配就显式加一档，不默认。
//
// ② **长按默认不重复触发**（`allowRepeat = false`）。快捷键是"命令"，
//    按住不放连发十次命令几乎没有一次是用户想要的；要连发的（例如缩放）
//    把它设成 true 即可。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <string_view>

namespace inking {

// 只做前向声明：`Matches` 的本体在 .cpp 里（它要读 KeyEvent 的字段，
// 而那件事不值得把 `KeyboardInput.h` 拖进每一个包含 Shortcut 的文件）。
struct KeyEvent;

/**
 * @brief 一个快捷键组合：修饰键位掩码 + 主键（SDL_Keycode 数值）。
 *
 * ```cpp
 * auto save = inking::Shortcut::Parse("Ctrl+S");   // {"S", Ctrl}
 * bar.AddButton(look, "保存", Save, save);         // 菜单项顺手绑定
 * ```
 */
class Shortcut {
public:
    // 修饰键位（可以按位或）。名字对齐 SDL 的 KMOD，但这里只是普通常量，
    // 所以公开头不依赖 SDL。
    static constexpr std::uint8_t kShift = 1u;
    static constexpr std::uint8_t kCtrl  = 2u;
    static constexpr std::uint8_t kAlt   = 4u;
    static constexpr std::uint8_t kSuper = 8u;  ///< Win 键 / Command 键

    /// SDL_Keycode 的数值；**0 表示"没绑定"**（SDL 的 `SDLK_UNKNOWN` 也是 0）。
    std::int32_t key = 0;

    /// 修饰键位掩码（`kCtrl | kShift` 这样组合）。
    std::uint8_t modifiers = 0;

    /// 长按是否重复触发（默认否，见文件顶上第 ② 条）。
    bool allowRepeat = false;

    Shortcut() = default;
    Shortcut(std::int32_t keyCode, std::uint8_t modifierMask,
             bool repeats = false) noexcept
        : key(keyCode), modifiers(modifierMask), allowRepeat(repeats) {}

    /// 空快捷键（没绑定）。
    bool Empty() const noexcept { return key == 0; }

    /**
     * 这次按键是不是这个快捷键。
     *
     * - 主键必须相等；
     * - 修饰键必须**完全相等**（见文件顶上第 ① 条）；
     * - `repeat = true` 的事件在 `allowRepeat == false` 时不算命中。
     */
    bool Matches(const KeyEvent& event) const noexcept;

    /**
     * 解析 `"Ctrl+Shift+S"` 这类写法。
     *
     * 认的修饰键名（不分大小写）：`Ctrl` / `Control`、`Shift`、`Alt`、`Super` /
     * `Win` / `Cmd` / `Meta`。主键可以是：
     *   - 单个字母或数字：`A` ~ `Z`、`0` ~ `9`；
     *   - 单个符号：`=` `-` `,` `.` `/` `\` `;` `'` `` ` `` `[` `]` `+`；
     *   - `F1` ~ `F24`；
     *   - 具名键：`Space` `Enter` `Tab` `Escape` `Backspace` `Delete` `Insert`
     *     `Home` `End` `PageUp` `PageDown` `Left` `Right` `Up` `Down`，
     *     以及 `Minus` `Equals` `Plus` `Comma` `Period` `Slash` `Backslash`
     *     `Semicolon` `Apostrophe` `Grave` `LeftBracket` `RightBracket`。
     *
     * @param ok 可为空；解析失败时写入 false（返回的快捷键为空）。
     */
    static Shortcut Parse(std::string_view text, bool* ok = nullptr);

    /// 回写成人读的串（`"Ctrl+Shift+S"`）。空快捷键返回空串。
    std::string ToString() const;

    bool operator==(const Shortcut& other) const noexcept {
        return key == other.key && modifiers == other.modifiers
            && allowRepeat == other.allowRepeat;
    }
    bool operator!=(const Shortcut& other) const noexcept {
        return !(*this == other);
    }
};

}  // namespace inking
