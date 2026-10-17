#include <input/Shortcut.h>

#include <input/KeyboardInput.h>

// SDL 只在这个 .cpp 里出现：键码是"数值"，公开头存数值就够，不必拖 SDL 进去
// （`AGENTS.md` §6 第 8 条：公开头不拖第三方）。
#include <SDL3/SDL_keycode.h>

#include <cctype>
#include <cstdlib>
#include <vector>

namespace inking {

namespace {

/// 名字 ⇄ 键码。**正向表**用于 `Parse`，**反向查**用于 `ToString`。
///
/// 反查取"表里第一条"：所以别名的先后就是回写时的优先名（`Enter` 而不是
/// `Return`、`Escape` 而不是 `Esc`）——回写要稳定，自检才断言得了往返一致。
struct NamedKey {
    const char* name;
    std::int32_t key;
};

constexpr NamedKey kNamedKeys[] = {
    {"enter", SDLK_RETURN},
    {"return", SDLK_RETURN},
    {"escape", SDLK_ESCAPE},
    {"esc", SDLK_ESCAPE},
    {"space", SDLK_SPACE},
    {"tab", SDLK_TAB},
    {"backspace", SDLK_BACKSPACE},
    {"delete", SDLK_DELETE},
    {"del", SDLK_DELETE},
    {"insert", SDLK_INSERT},
    {"home", SDLK_HOME},
    {"end", SDLK_END},
    {"pageup", SDLK_PAGEUP},
    {"pagedown", SDLK_PAGEDOWN},
    {"left", SDLK_LEFT},
    {"right", SDLK_RIGHT},
    {"up", SDLK_UP},
    {"down", SDLK_DOWN},
    {"minus", SDLK_MINUS},
    {"equals", SDLK_EQUALS},
    {"plus", SDLK_PLUS},
    {"comma", SDLK_COMMA},
    {"period", SDLK_PERIOD},
    {"slash", SDLK_SLASH},
    {"backslash", SDLK_BACKSLASH},
    {"semicolon", SDLK_SEMICOLON},
    {"apostrophe", SDLK_APOSTROPHE},
    {"grave", SDLK_GRAVE},
    {"leftbracket", SDLK_LEFTBRACKET},
    {"rightbracket", SDLK_RIGHTBRACKET},
};

/// F1 ~ F24 **不是**连续键码（F13~F24 落在另一段扫描码上），所以显式列。
constexpr std::int32_t kFunctionKeys[] = {
    SDLK_F1,  SDLK_F2,  SDLK_F3,  SDLK_F4,  SDLK_F5,  SDLK_F6,
    SDLK_F7,  SDLK_F8,  SDLK_F9,  SDLK_F10, SDLK_F11, SDLK_F12,
    SDLK_F13, SDLK_F14, SDLK_F15, SDLK_F16, SDLK_F17, SDLK_F18,
    SDLK_F19, SDLK_F20, SDLK_F21, SDLK_F22, SDLK_F23, SDLK_F24,
};

constexpr std::size_t kFunctionKeyCount =
    sizeof(kFunctionKeys) / sizeof(kFunctionKeys[0]);

std::string Lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

/// 单个字符 → 键码。SDL3 里字母/数字/常见符号的键码**就是**它们的小写 ASCII
/// （`SDLK_A == 'a'`、`SDLK_0 == '0'`、`SDLK_EQUALS == '='`），所以这里不用查表。
std::int32_t KeyFromChar(char ch) {
    const unsigned char byte = static_cast<unsigned char>(ch);
    if (byte < 0x20u || byte > 0x7Eu) {
        return 0;  // 非可打印 ASCII：不认（中文按键名没有意义）
    }
    if (std::isalpha(byte) != 0) {
        return static_cast<std::int32_t>(std::tolower(byte));
    }
    if (std::isdigit(byte) != 0) {
        return static_cast<std::int32_t>(byte);
    }
    switch (ch) {
        case '=': case '-': case ',': case '.': case '/': case '\\':
        case ';': case '\'': case '`': case '[': case ']': case '+':
            return static_cast<std::int32_t>(byte);
        default:
            return 0;
    }
}

/// 名字 → 键码（`f5` / `space` / `pageup` …）。
std::int32_t KeyFromName(const std::string& lower) {
    for (const NamedKey& item : kNamedKeys) {
        if (lower == item.name) {
            return item.key;
        }
    }
    // F1 ~ F24
    if (lower.size() >= 2 && lower[0] == 'f') {
        const std::string digits = lower.substr(1);
        if (digits.find_first_not_of("0123456789") == std::string::npos) {
            const long index = std::strtol(digits.c_str(), nullptr, 10);
            if (index >= 1 && index <= static_cast<long>(kFunctionKeyCount)) {
                return kFunctionKeys[index - 1];
            }
        }
    }
    return 0;
}

/// 键码 → 名字（`ToString` 用）。找不到返回空串。
std::string NameFromKey(std::int32_t key) {
    for (std::size_t i = 0; i < kFunctionKeyCount; ++i) {
        if (kFunctionKeys[i] == key) {
            return "F" + std::to_string(i + 1);
        }
    }
    for (const NamedKey& item : kNamedKeys) {
        if (item.key == key) {
            std::string name = item.name;
            // 表里存的是小写名；回写成 PascalCase（"PageUp" / "Escape"）。
            if (!name.empty()) {
                name[0] = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(name[0])));
            }
            return name;
        }
    }
    if (key >= 'a' && key <= 'z') {
        std::string name(1, static_cast<char>(key));
        name[0] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(name[0])));
        return name;
    }
    if (key >= '0' && key <= '9') {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= 0x20 && key <= 0x7E) {
        return std::string(1, static_cast<char>(key));
    }
    return {};
}

}  // namespace

bool Shortcut::Matches(const KeyEvent& event) const noexcept {
    if (key == 0 || event.key != key) {
        return false;
    }
    if (event.repeat && !allowRepeat) {
        return false;
    }
    // 修饰键**完全相等**：多按一个 Shift 就不算命中（见头文件第 ① 条）。
    const std::uint8_t got = static_cast<std::uint8_t>(
        (event.shift ? kShift : 0u) | (event.ctrl ? kCtrl : 0u)
        | (event.alt ? kAlt : 0u) | (event.super ? kSuper : 0u));
    return got == modifiers;
}

Shortcut Shortcut::Parse(std::string_view text, bool* ok) {
    if (ok != nullptr) {
        *ok = false;
    }
    const std::string_view trimmed = Trim(text);
    if (trimmed.empty()) {
        return {};  // 空串 = "没绑定"，不算错误（配置里留空是常态）
    }

    std::int32_t parsedKey = 0;
    std::uint8_t parsedModifiers = 0;

    std::size_t start = 0;
    while (start <= trimmed.size()) {
        const std::size_t plus = trimmed.find('+', start);
        const std::size_t end =
            (plus == std::string_view::npos) ? trimmed.size() : plus;
        const std::string token = Lower(Trim(trimmed.substr(start, end - start)));
        start = end + 1;

        if (token.empty()) {
            if (end == trimmed.size()) {
                break;  // 尾随 '+'：忽略
            }
            return {};  // "Ctrl++S" 这种：中间的 '+' 当主键会歧义
        }

        if (token == "ctrl" || token == "control") {
            parsedModifiers |= kCtrl;
        } else if (token == "shift") {
            parsedModifiers |= kShift;
        } else if (token == "alt") {
            parsedModifiers |= kAlt;
        } else if (token == "super" || token == "win" || token == "cmd"
                   || token == "meta") {
            parsedModifiers |= kSuper;
        } else {
            if (parsedKey != 0) {
                return {};  // 两个主键：不认（"A+B" 不是快捷键）
            }
            parsedKey = (token.size() == 1)
                            ? KeyFromChar(token[0])
                            : KeyFromName(token);
            if (parsedKey == 0) {
                return {};
            }
        }
    }

    if (parsedKey == 0) {
        return {};  // 只有修饰键（"Ctrl"）：不是快捷键
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return Shortcut{parsedKey, parsedModifiers};
}

std::string Shortcut::ToString() const {
    if (key == 0) {
        return {};
    }
    std::string out;
    // 顺序固定（Ctrl → Shift → Alt → Super）：回写要稳定，`Parse` 之后
    // 再 `ToString` 必须和常用写法一致，自检才断言得了往返。
    if ((modifiers & kCtrl) != 0u) {
        out += "Ctrl+";
    }
    if ((modifiers & kShift) != 0u) {
        out += "Shift+";
    }
    if ((modifiers & kAlt) != 0u) {
        out += "Alt+";
    }
    if ((modifiers & kSuper) != 0u) {
        out += "Super+";
    }
    const std::string name = NameFromKey(key);
    if (name.empty()) {
        out += "Key" + std::to_string(key);  // 兜底：至少别丢信息
    } else {
        out += name;
    }
    return out;
}

}  // namespace inking
