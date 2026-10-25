#include <text/InkingTextInput.h>

#include <render/InkingDraw.h>
#include <render/InkingText.h>
#include <input/KeyboardInput.h>
#include <scene/InkingScene.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace inking {
namespace {

/// 单行化：这一版输入框只支持单行（多行要连排版、上下移动、滚动一起做）。
/// `wrap` 与 `maxLines` 一起改，光改一个会让排版在边界上给出反直觉的结果。
TextInputData AsSingleLine(const TextInputData& data) {
    TextInputData out = data;
    out.wrap = TextWrap::NoWrap;
    out.overflow = TextOverflow::Clip;
    out.maxLines = 1;
    return out;
}

/// 剪贴板：默认真调 SDL，自检里换成内存假货（真剪贴板会被别的程序改，测不稳）。
struct ClipboardState {
    InkingTextInput::ClipboardGet get;
    InkingTextInput::ClipboardSet set;
};

ClipboardState& clipboardState() {
    // 进程级记账，**故意不析构**（`AGENTS.md` §6 第 88 条）。
    static ClipboardState* state = new ClipboardState();
    return *state;
}

std::string DefaultClipboardGet() {
    char* text = SDL_GetClipboardText();
    if (text == nullptr) {
        return {};  // SDL 没起来 / 剪贴板不可用：当成空
    }
    std::string out(text);
    SDL_free(text);  // SDL 给的是自己分配的内存，必须还回去
    return out;
}

void DefaultClipboardSet(const std::string& text) {
    SDL_SetClipboardText(text.c_str());
}

std::string ClipboardGetText() {
    ClipboardState& state = clipboardState();
    return state.get ? state.get() : DefaultClipboardGet();
}

void ClipboardPutText(const std::string& text) {
    ClipboardState& state = clipboardState();
    if (state.set) {
        state.set(text);
    } else {
        DefaultClipboardSet(text);
    }
}

}  // namespace

void InkingTextInput::SetClipboardHandlers(ClipboardGet get, ClipboardSet set) {
    ClipboardState& state = clipboardState();
    state.get = std::move(get);
    state.set = std::move(set);
}

void InkingTextInput::ResetClipboardHandlers() {
    ClipboardState& state = clipboardState();
    state.get = nullptr;
    state.set = nullptr;
}

InkingTextInput::InkingTextInput(InkingWidget* parent, const TextInputData& data)
    : InkingStaticLabel(parent, AsSingleLine(data)), _input(data) {
    // 标签的默认是**穿透**（它常压在按钮上）；输入框必须吃点击，否则点不进去。
    // 这一条显式写：指望默认值就会得到一个"点不动的输入框"。
    SetHitPassThrough(false);

    // 初始文本必须同时写进**编辑状态**：基类只拿它做显示，`GetEditText()` 读的是
    // `_edit`。不同步的话，一个预填了 "12" 的输入框会"看着有字、读出来是空"，
    // 调用方 std::atoi("") 得 0，参数静默变成 0（2026-10-18 修复）。
    if (!_input.text.empty()) {
        _edit.SetText(_input.text);
    }
}

bool InkingTextInput::HitTest(float worldX, float worldY) const {
    // 本体矩形就是热区（输入框没有形状层——它是文字 + 光标）。
    // 入参是世界坐标（设计空间），必须用**绝对**位置比——GetX()/GetY() 是相对
    // 父级的偏移，嵌套在页面容器里的输入框会永远命中不了（2026-10-18 修复）。
    const float left = static_cast<float>(GetAbsX());
    const float top = static_cast<float>(GetAbsY());
    return worldX >= left && worldX < left + static_cast<float>(GetWidth())
        && worldY >= top && worldY < top + static_cast<float>(GetHeight());
}

// ---------------------------------------------------------------------------
// 内容与焦点
// ---------------------------------------------------------------------------

void InkingTextInput::SetEditText(const std::string& text) {
    _edit.SetText(text);
    syncToLabel();
    // 换文本后光标归零、文本长度也变了：滚动量要重新算（T-12）。
    EnsureCaretVisible();
    _blink = 0.0f;
    _caretVisible = _focused;
    MarkRepaintDirty();
}

void InkingTextInput::SetFocused(bool focused) {
    if (_focused == focused) {
        return;
    }
    _focused = focused;
    if (focused) {
        EnsureCaretVisible();  // 刚拿到焦点：把光标滚进可视区
    }
    _blink = 0.0f;
    // 拿到焦点立刻显示光标（不等半个闪烁周期），失焦立刻收起来。
    _caretVisible = focused;
    MarkRepaintDirty();
}

void InkingTextInput::syncToLabel() {
    // 交给基类：它会重排、让所在容器重排（`RelayoutParent`）、标重绘脏。
    // 编辑状态是"唯一真相"，基类那份文本只是它的显示形态。
    InkingStaticLabel::SetText(_edit.Text());
}

// ---------------------------------------------------------------------------
// 输入
// ---------------------------------------------------------------------------

bool InkingTextInput::HandleKey(const KeyEvent& key) {
    const bool shift = key.shift;
    const bool ctrl = key.ctrl;

    switch (key.key) {
        case SDLK_BACKSPACE:
            _edit.Backspace();
            break;
        case SDLK_DELETE:
            _edit.DeleteForward();
            break;
        case SDLK_LEFT:
            _edit.MoveLeft(shift);
            break;
        case SDLK_RIGHT:
            _edit.MoveRight(shift);
            break;
        case SDLK_HOME:
            _edit.MoveHome(shift);
            break;
        case SDLK_END:
            _edit.MoveEnd(shift);
            break;
        case SDLK_A:
            if (!ctrl) {
                return false;  // 普通字母交给 TEXT_INPUT（输入法那条路）
            }
            _edit.SelectAll();
            break;
        case SDLK_C:
            if (!ctrl) {
                return false;
            }
            if (const std::string selected = _edit.CopySelection();
                !selected.empty()) {
                ClipboardPutText(selected);
                ++_copies;
            }
            break;
        case SDLK_X:
            if (!ctrl) {
                return false;
            }
            if (std::string cut = _edit.CutSelection(); !cut.empty()) {
                ClipboardPutText(cut);
                ++_copies;
            }
            break;
        case SDLK_V:
            if (!ctrl) {
                return false;
            }
            _edit.Paste(ClipboardGetText());
            ++_pastes;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            ++_submits;
            submitted.Emit(_edit.Text());
            break;
        default:
            return false;  // 其余（字母 / 数字 / 标点）走 onTextInput
    }

    if (_edit.LastChange().text) {
        syncToLabel();  // 文本变了：要重排（行宽 / 高度可能变）
    } else {
        MarkRepaintDirty();  // 只是光标 / 选区动了：重绘就够
    }
    // 光标可能被键动过（方向键 / Home / End）或文本长度变了：让它在框里。
    EnsureCaretVisible();
    _blink = 0.0f;
    _caretVisible = _focused;
    return true;
}

void InkingTextInput::HandleText(const std::string& text) {
    ++_textInputs;
    // **输入法提交走的就是这条路**（SDL 的 `SDL_EVENT_TEXT_INPUT` → 键盘层 →
    // 场景 → `onTextInput`）：中文一个字和英文一个字母在这里没有区别。
    _edit.InsertText(text);
    if (_edit.LastChange().text) {
        syncToLabel();
    }
    MarkRepaintDirty();
    EnsureCaretVisible();
    _blink = 0.0f;
    _caretVisible = _focused;
}

void InkingTextInput::PressAt(float designX, float designY) {
    _edit.MoveTo(OffsetAtPoint(designX, designY));
    // 按下的那一点就是拖选的锚点（`MoveTo(..., select=true)` 会拿它当另一端）。
    _dragAnchor = _edit.Caret();
    MarkRepaintDirty();
    EnsureCaretVisible();
    _blink = 0.0f;
    _caretVisible = _focused;
}

void InkingTextInput::DragTo(float designX, float designY) {
    _edit.MoveTo(OffsetAtPoint(designX, designY), /*select=*/true);
    MarkRepaintDirty();
    EnsureCaretVisible();
}

void InkingTextInput::TickCaretBlink(float deltaSeconds) {
    onAnimationTick(deltaSeconds);
}

// ---------------------------------------------------------------------------
// 几何：字节偏移 ↔ 设计坐标
// ---------------------------------------------------------------------------

float InkingTextInput::localX(float designX) const {
    return designX - static_cast<float>(GetAbsX());
}

float InkingTextInput::localY(float designY) const {
    return designY - static_cast<float>(GetAbsY());
}

TextInputRect InkingTextInput::RawBoxForOffset(std::size_t offset) const {
    const TextLayoutResult& layout = GetTextLayout();
    if (layout.lines.empty()) {
        return TextInputRect{0.0f, 0.0f, _input.caretWidth,
                             _input.style.fontSize};
    }

    // 找 offset 落在哪一行（等于行尾也算这一行；超出就归最后一行）。
    std::size_t index = layout.lines.size() - 1;
    for (std::size_t i = 0; i < layout.lines.size(); ++i) {
        if (offset <= layout.lines[i].end) {
            index = i;
            break;
        }
    }
    const TextLine& line = layout.lines[index];
    const std::size_t target = std::min(offset, line.end);

    // 行内**逐码点**累加宽度：比例字体下光标才贴在正确的字缝上
    // （等宽估算在中英混排里必然偏）。
    const std::string& text = _edit.Text();
    TextRequest request;
    request.fontSize = _input.style.fontSize;
    request.fontPath = _input.style.fontPath;
    request.fontPaths = _input.style.fontPaths;

    float width = 0.0f;
    std::size_t pos = line.begin;
    while (pos < target) {
        const std::size_t next = TextEditState::NextCodePoint(text, pos);
        if (next > target) {
            break;
        }
        request.text = text.substr(pos, next - pos);
        width += MeasureText(request).width;
        pos = next;
    }

    return TextInputRect{line.x + width, line.y, _input.caretWidth,
                         line.height};
}

TextInputRect InkingTextInput::BoxForOffset(std::size_t offset) const {
    // 未滚动坐标 → 减去滚动量 → **夹进本体**（T-12）。
    //
    // 夹取是"滚动量还没跟上"那一帧的兜底：`EnsureCaretVisible` 只在跨越边界时
    // 动，而排版 / 文本可能在两次调用之间变（例如退格把文本变短）。没有这一层，
    // 光标就会有一两帧画在框外——而面板默认不剪子孙，它是**真的**画出去。
    TextInputRect box = RawBoxForOffset(offset);
    box.x -= _scrollX;
    const float width = static_cast<float>(GetWidth());
    const float maxX = std::max(0.0f, width - box.width);
    if (box.x < 0.0f) {
        box.x = 0.0f;
    } else if (box.x > maxX) {
        box.x = maxX;
    }
    return box;
}

float InkingTextInput::GetTextWidth() const {
    const TextLayoutResult& layout = GetTextLayout();
    if (layout.lines.empty()) {
        return 0.0f;
    }
    // 单行输入框：最后一行就是"整段文字"（多行还没做，见头文件顶上那段）。
    // 用**行尾**算，含光标宽度——和 `EnsureCaretVisible` 的上限用同一份定义。
    return RawBoxForOffset(_edit.Text().size()).x + _input.caretWidth;
}

void InkingTextInput::SetScrollX(float x) {
    const float width = static_cast<float>(GetWidth());
    // 上限 = "文字宽度 − 视口宽度"，负数夹到 0（文本没超宽时永远滚不动）。
    const float maxScroll = std::max(0.0f, GetTextWidth() - width);
    const float next = x < 0.0f ? 0.0f : (x > maxScroll ? maxScroll : x);
    if (next == _scrollX) {
        return;  // 没变就不标脏（幂等：光标停在同一处反复调也无害）
    }
    _scrollX = next;
    // ⚠️ 这一行不能省：滚动量只影响绘制，**不会**让排版缓存或命中表自己失效
    // ——漏掉它就是"滚动量变了、画面不动"（T-12 明确记过这条）。
    MarkRepaintDirty();
}

void InkingTextInput::EnsureCaretVisible() {
    const float width = static_cast<float>(GetWidth());
    if (width <= 0.0f) {
        return;
    }
    const TextInputRect caret = RawBoxForOffset(_edit.Caret());
    const float caretLeft = caret.x;
    const float caretRight = caret.x + caret.width;

    if (caretLeft < _scrollX) {
        // 光标跑到可视区左边：文字往右收，正好让光标贴住左边缘。
        SetScrollX(caretLeft);
        return;
    }
    if (caretRight > _scrollX + width) {
        // 光标跑到右边：文字左移，让光标（含宽度）贴住右边缘。
        SetScrollX(caretRight - width);
    }
}

TextInputRect InkingTextInput::CaretBox() const {
    return BoxForOffset(_edit.Caret());
}

std::vector<TextInputRect> InkingTextInput::SelectionBoxes() const {
    std::vector<TextInputRect> boxes;
    if (!_edit.HasSelection()) {
        return boxes;
    }
    const std::size_t begin = _edit.SelectionBegin();
    const std::size_t end = _edit.SelectionEnd();
    const TextLayoutResult& layout = GetTextLayout();

    for (const TextLine& line : layout.lines) {
        const std::size_t from = std::max(begin, line.begin);
        const std::size_t to = std::min(end, line.end);
        if (from > to) {
            continue;
        }
        const float x0 = BoxForOffset(from).x;
        const float x1 = BoxForOffset(to).x;
        if (x1 - x0 <= 0.0f) {
            continue;  // 空行 / 零宽：没有可画的选区
        }
        boxes.push_back(TextInputRect{x0, line.y, x1 - x0, line.height});
    }
    return boxes;
}

std::size_t InkingTextInput::OffsetAtPoint(float designX,
                                           float designY) const {
    const TextLayoutResult& layout = GetTextLayout();
    if (layout.lines.empty()) {
        return 0;
    }
    // 加回滚动量（T-12）：`BoxForOffset` 画的时候减了多少，这里就要加多少。
    // 两处必须成对——只改一处会让"点到的字"和"看到的字"差一个滚动距离，
    // 而且文本越宽偏得越多。
    const float x = localX(designX) + _scrollX;
    const float y = localY(designY);

    // 先按 y 找行（落在行底之下就归最后一行）。
    std::size_t index = layout.lines.size() - 1;
    for (std::size_t i = 0; i < layout.lines.size(); ++i) {
        if (y < layout.lines[i].y + layout.lines[i].height) {
            index = i;
            break;
        }
    }
    const TextLine& line = layout.lines[index];
    const float target = x - line.x;

    const std::string& text = _edit.Text();

    // 排版可能来自上一版文本（例如刚 SetEditText("")）：此时 line.end 会超出
    // text.size()，下面的 while 里 NextCodePoint 不再前进 ⇒ 死循环、整个 UI 挂死。
    // 直接收到末尾，别把过期排版喂给逐码点累加（2026-10-18 修复）。
    if (line.end > text.size()) {
        return text.size();
    }
    TextRequest request;
    request.fontSize = _input.style.fontSize;
    request.fontPath = _input.style.fontPath;
    request.fontPaths = _input.style.fontPaths;

    float width = 0.0f;
    std::size_t pos = line.begin;
    while (pos < line.end) {
        const std::size_t next = TextEditState::NextCodePoint(text, pos);
        if (next > line.end || next <= pos) {  // next <= pos = 不前进，防死循环
            break;
        }
        request.text = text.substr(pos, next - pos);
        const float charWidth = MeasureText(request).width;
        // 落在字的中线左边 ⇒ 光标停在这个字的起点（右半边则往下一个字走）。
        if (target < width + charWidth * 0.5f) {
            return pos;
        }
        width += charWidth;
        pos = next;
    }
    return line.end;
}

// ---------------------------------------------------------------------------
// 绘制与动画
// ---------------------------------------------------------------------------

void InkingTextInput::onRender(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = detail::currentRenderer();
    const float magnitude = GetMagnification() > 0.0f ? GetMagnification() : 1.0f;

    // ① 选区：一行一个矩形，画在**文字下面**（不然会盖住字）。
    if (renderer != nullptr) {
        for (const TextInputRect& box : SelectionBoxes()) {
            detail::fillShape(
                renderer, ShapeSpec::Rect(),
                std::max(1, static_cast<int>(box.width + 0.5f)),
                std::max(1, static_cast<int>(box.height + 0.5f)), magnitude,
                pixelX + box.x * magnitude, pixelY + box.y * magnitude,
                TransformSpec{}, _input.selectionColor);
        }
    }

    // ② 文字：基类那一笔（排版 + 纹理层 + 多语言），一个字都不重写。
    InkingStaticLabel::onRender(pixelX, pixelY);

    // ③ 光标：有焦点 + 闪烁相位在"亮"的那半边。
    if (renderer != nullptr && _focused && _caretVisible) {
        const TextInputRect box = CaretBox();
        detail::fillShape(
            renderer, ShapeSpec::Rect(),
            std::max(1, static_cast<int>(box.width + 0.5f)),
            std::max(1, static_cast<int>(box.height + 0.5f)), magnitude,
            pixelX + box.x * magnitude, pixelY + box.y * magnitude,
            TransformSpec{}, _input.caretColor);
    }
}

void InkingTextInput::onAnimationTick(float deltaSeconds) {
    if (!_focused) {
        _caretVisible = false;
        return;
    }
    if (_input.caretBlinkSeconds <= 0.0f) {
        _caretVisible = true;  // 配了 0 = 不闪，常亮
        return;
    }
    _blink += deltaSeconds;
    const float period = _input.caretBlinkSeconds;
    const bool visible = std::fmod(_blink, period * 2.0f) < period;
    if (visible == _caretVisible) {
        return;  // **相位没翻就不要标脏**：闪烁是 2Hz 的事，不该让画面每帧重画
    }
    _caretVisible = visible;
    MarkRepaintDirty();
}

// ---------------------------------------------------------------------------
// 钩子转发：真链路与公开写入口走同一份实现
// ---------------------------------------------------------------------------

void InkingTextInput::onKeyDown(const KeyEvent& key) { HandleKey(key); }

void InkingTextInput::onTextInput(const std::string& text) {
    HandleText(text);
}

void InkingTextInput::onFocusChanged(bool focused) { SetFocused(focused); }

void InkingTextInput::onPointerPress(PointerButton button, bool press) {
    // 开始编辑 / 选区都是**主键**手势（0.5.0）。右键留给将来的"粘贴 / 全选"
    // 上下文菜单——那时在这个守卫之后加一条 Secondary 分支即可。
    if (button != PointerButton::Primary) {
        return;
    }
    if (!press) {
        return;
    }
    // 钩子的形状是"只有按没按"，没有坐标；按下的那一点正好是场景这一帧刚算好的
    // 设计坐标，所以问它（输入框不在场景下时什么都不做——那种节点也点不到）。
    InkingScene* scene = GetScene();
    if (scene == nullptr) {
        return;
    }
    PressAt(scene->GetPointerX(), scene->GetPointerY());
}

void InkingTextInput::onPointerDrag(float designX, float designY, float,
                                    float) {
    DragTo(designX, designY);
}

}  // namespace inking
