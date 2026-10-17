#include <input/TextEditState.h>

#include <algorithm>
#include <utility>

namespace inking {
namespace {

/// UTF-8 续字节（`10xxxxxx`）。
bool IsContinuation(char ch) noexcept {
    return (static_cast<unsigned char>(ch) & 0xC0u) == 0x80u;
}

/// 单行输入框：粘贴时把换行**丢掉**（不是换成空格——换行在单行里没有对应字符，
/// 换成空格会凭空多出一个用户没打过的空位）。
std::string StripNewlines(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch != '\r' && ch != '\n') {
            out.push_back(ch);
        }
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// 静态小工具：码点边界
//
// 和排版层那份 UTF-8 步进（`src/dataStruct/InkingTextLayout.cpp` 的 `DecodeUtf8`）
// **同源**：那边要的是"码点值 + 宽度"（为了测量），这边只要"边界在哪"。
// 两份都很小、且关注点不同；哪天要抽公共 util，抽这两处。
// ---------------------------------------------------------------------------

bool TextEditState::IsBoundary(std::string_view text, std::size_t pos) noexcept {
    if (pos == 0 || pos >= text.size()) {
        return true;  // 0 与末尾天然是边界（越界也当成边界，调用方自己夹）
    }
    return !IsContinuation(text[pos]);
}

std::size_t TextEditState::SnapToBoundary(std::string_view text,
                                          std::size_t pos) noexcept {
    if (pos >= text.size()) {
        return text.size();
    }
    // 往前退到不是续字节为止：**向前吸附**。点击落在"汉字中间"时，
    // 光标停在这个字的起点——那是唯一不会产生非法光标的选择。
    while (pos > 0 && IsContinuation(text[pos])) {
        --pos;
    }
    return pos;
}

std::size_t TextEditState::PrevCodePoint(std::string_view text,
                                         std::size_t pos) noexcept {
    pos = SnapToBoundary(text, pos);
    if (pos == 0) {
        return 0;
    }
    --pos;
    while (pos > 0 && IsContinuation(text[pos])) {
        --pos;
    }
    return pos;
}

std::size_t TextEditState::NextCodePoint(std::string_view text,
                                         std::size_t pos) noexcept {
    pos = SnapToBoundary(text, pos);
    if (pos >= text.size()) {
        return text.size();
    }
    ++pos;
    while (pos < text.size() && IsContinuation(text[pos])) {
        ++pos;
    }
    return pos;
}

// ---------------------------------------------------------------------------
// 文本
// ---------------------------------------------------------------------------

void TextEditState::SetText(std::string text) {
    const bool changed = text != text_;
    text_ = std::move(text);
    caret_ = 0;
    anchor_ = 0;
    lastChange_ = Change{};
    lastChange_.text = changed;
    lastChange_.caret = true;      // 光标一定回到了 0
    lastChange_.selection = true;  // 选区一定被清掉了
}

// ---------------------------------------------------------------------------
// 光标与选区
// ---------------------------------------------------------------------------

void TextEditState::SetCaret(std::size_t byteOffset) noexcept {
    const std::size_t next = SnapToBoundary(text_, byteOffset);
    const bool caretMoved = next != caret_;
    // 选区的"变化"要连"从有到无"一起算：只比 `anchor_` 与 `next` 会漏掉
    // "SelectAll 之后点一下"（那时 anchor 恰好也是 0 ⇒ 看着像没变，其实选区没了）。
    const bool hadSelection = anchor_ != caret_;
    caret_ = next;
    anchor_ = next;
    lastChange_ = Change{};
    lastChange_.caret = caretMoved;
    lastChange_.selection = hadSelection || caretMoved;
}

std::size_t TextEditState::SelectionBegin() const noexcept {
    return std::min(caret_, anchor_);
}

std::size_t TextEditState::SelectionEnd() const noexcept {
    return std::max(caret_, anchor_);
}

std::string TextEditState::SelectedText() const {
    if (!HasSelection()) {
        return {};
    }
    return text_.substr(SelectionBegin(), SelectionEnd() - SelectionBegin());
}

void TextEditState::SelectAll() noexcept {
    anchor_ = 0;
    caret_ = text_.size();
    lastChange_ = Change{};
    lastChange_.caret = true;
    lastChange_.selection = true;
}

void TextEditState::ClearSelection() noexcept {
    if (!HasSelection()) {
        return;
    }
    anchor_ = caret_;
    lastChange_ = Change{};
    lastChange_.selection = true;  // 光标没动，只是选区没了
}

// ---------------------------------------------------------------------------
// 移动
// ---------------------------------------------------------------------------

void TextEditState::MoveLeft(bool select) noexcept {
    const std::size_t next = PrevCodePoint(text_, caret_);
    const bool moved = next != caret_;
    caret_ = next;
    if (!select) {
        anchor_ = next;
    }
    lastChange_ = Change{};
    lastChange_.caret = moved;
    lastChange_.selection = select ? moved : false;
}

void TextEditState::MoveRight(bool select) noexcept {
    const std::size_t next = NextCodePoint(text_, caret_);
    const bool moved = next != caret_;
    caret_ = next;
    if (!select) {
        anchor_ = next;
    }
    lastChange_ = Change{};
    lastChange_.caret = moved;
    lastChange_.selection = select ? moved : false;
}

void TextEditState::MoveHome(bool select) noexcept {
    const bool moved = caret_ != 0;
    caret_ = 0;
    if (!select) {
        anchor_ = 0;
    }
    lastChange_ = Change{};
    lastChange_.caret = moved;
    lastChange_.selection = select ? moved : false;
}

void TextEditState::MoveEnd(bool select) noexcept {
    const bool moved = caret_ != text_.size();
    caret_ = text_.size();
    if (!select) {
        anchor_ = text_.size();
    }
    lastChange_ = Change{};
    lastChange_.caret = moved;
    lastChange_.selection = select ? moved : false;
}

void TextEditState::MoveTo(std::size_t byteOffset, bool select) noexcept {
    const std::size_t next = SnapToBoundary(text_, byteOffset);
    const bool moved = next != caret_;
    caret_ = next;
    if (!select) {
        anchor_ = next;
    }
    lastChange_ = Change{};
    lastChange_.caret = moved;
    lastChange_.selection = select ? moved : false;
}

// ---------------------------------------------------------------------------
// 编辑
// ---------------------------------------------------------------------------

void TextEditState::eraseRange(std::size_t begin, std::size_t end) {
    text_.erase(begin, end - begin);
    caret_ = begin;
    anchor_ = begin;
}

void TextEditState::InsertText(std::string_view utf8) {
    if (utf8.empty()) {
        // 空串是空操作：**把 LastChange 清零**——它描述的是"最近一次操作改了什么"，
        // 空操作就该报告"什么都没改"（不然调用方会把上一次的改动当成本次的）。
        lastChange_ = Change{};
        return;
    }
    if (HasSelection()) {
        eraseRange(SelectionBegin(), SelectionEnd());
    }
    const std::size_t at = SnapToBoundary(text_, caret_);
    text_.insert(at, utf8.data(), utf8.size());
    caret_ = at + utf8.size();
    anchor_ = caret_;
    lastChange_ = Change{};
    lastChange_.text = true;
    lastChange_.caret = true;
    lastChange_.selection = true;
}

void TextEditState::Backspace() {
    if (HasSelection()) {
        eraseRange(SelectionBegin(), SelectionEnd());
        lastChange_ = Change{};
        lastChange_.text = true;
        lastChange_.caret = true;
        lastChange_.selection = true;
        return;
    }
    const std::size_t at = SnapToBoundary(text_, caret_);
    if (at == 0) {
        lastChange_ = Change{};  // 已经在最前面：空操作（不沿用上一次的改动）
        return;
    }
    const std::size_t prev = PrevCodePoint(text_, at);
    eraseRange(prev, at);
    lastChange_ = Change{};
    lastChange_.text = true;
    lastChange_.caret = true;
    lastChange_.selection = false;  // 两边本来就相等（没选区）
}

void TextEditState::DeleteForward() {
    if (HasSelection()) {
        eraseRange(SelectionBegin(), SelectionEnd());
        lastChange_ = Change{};
        lastChange_.text = true;
        lastChange_.caret = true;
        lastChange_.selection = true;
        return;
    }
    const std::size_t at = SnapToBoundary(text_, caret_);
    if (at >= text_.size()) {
        lastChange_ = Change{};  // 已经在末尾：空操作
        return;
    }
    const std::size_t next = NextCodePoint(text_, at);
    eraseRange(at, next);
    lastChange_ = Change{};
    lastChange_.text = true;
    lastChange_.caret = true;
    lastChange_.selection = false;
}

std::string TextEditState::CopySelection() const { return SelectedText(); }

std::string TextEditState::CutSelection() {
    if (!HasSelection()) {
        return {};
    }
    std::string cut = SelectedText();
    eraseRange(SelectionBegin(), SelectionEnd());
    lastChange_ = Change{};
    lastChange_.text = true;
    lastChange_.caret = true;
    lastChange_.selection = true;
    return cut;
}

void TextEditState::Paste(std::string_view text) {
    const std::string clean = StripNewlines(text);
    InsertText(clean);  // 空结果（比如剪贴板只有换行）就是空操作
}

}  // namespace inking
