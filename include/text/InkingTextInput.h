#pragma once

#include <event/InkingEvent.h>
#include <input/TextEditState.h>
#include <text/InkingStaticLabel.h>
#include <text/LabelData.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace inking {

inline constexpr const char* kTextInputModuleName = "InkingTextInput";

/**
 * @brief 输入框的数据（B7）：**标签那份 + 三样"编辑期"的东西**。
 *
 * 继承而不是再抄一份字段，理由和 `ImageData : ButtonData` 一样（A11）：输入框
 * 就是"能打字的 label"——文字、字号、对齐、换行、颜色全都同构，多出来的只有
 * 光标与选区怎么画。生成器那边也因此能逐字复用标签的烘法。
 *
 * **这一版是单行输入框**（`wrap` 会被强制成 `noWrap`、粘贴时换行被丢掉）：
 * 多行要连排版、光标上下移动、滚动一起做，属于另一件事。
 */
struct TextInputData : public LabelData {
    std::uint32_t caretColor = 0xFFEAF2FFu;      ///< 光标颜色 0xAARRGGBB
    std::uint32_t selectionColor = 0x663E7BD8u;  ///< 选区底色（半透明）
    float caretBlinkSeconds = 0.6f;              ///< 亮 / 灭各持续多久（0 = 不闪）
    float caretWidth = 1.5f;                     ///< 光标宽度（设计坐标）
};

/// 一个小矩形（设计坐标，**相对组件左上角**）：光标与选区都用它。
struct TextInputRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/**
 * @brief 可编辑文本框（B7）：光标、选区、键盘编辑、剪贴板、IME 提交。
 *
 * 它**继承 `InkingStaticLabel`**：排版（A7）、按内容撑高（A6 的 `MeasureLayout`）、
 * 文字绘制、多语言查表全都是现成的——输入框要加的只是"编辑状态 + 光标 / 选区 +
 * 键盘接线"。这和 A11 的"图片 = 没有 onClicked 的按钮"是同一个套路：
 * **能继承就别抄**（抄一份就要在第五个地方同步字段，`AGENTS.md` §6 第 83 条）。
 *
 * ---------------------------------------------------------------------------
 * 三条约定
 *
 * 1. **编辑状态是独立的一份**（`TextEditState`，纯逻辑、可单测）；每次改动之后
 *    `syncToLabel()` 把它交给基类 ⇒ 重排 + 重绘。所以"打一个字"的代价是
 *    一次排版（有缓存）+ 一次重绘，和 `SetText` 一样；
 * 2. **吃点击**（`SetHitPassThrough(false)`）：输入框要点击定位光标，而标签的
 *    默认是"穿透"（标签常压在按钮上）。这一条在构造里显式写，别指望默认值；
 * 3. **快捷键认 `ctrl`**（Ctrl+A / C / X / V），**IME 提交走 `onTextInput`**
 *    （SDL 的 `SDL_EVENT_TEXT_INPUT`，中文输入法组合好的结果从这条路进来）——
 *    TaskGuide B7 点名的"自检覆盖输入法提交"就是这条路。
 *
 * **没做**（写清楚，别让人猜）：多行、撤销 / 重做、Tab 焦点遍历（键盘那层就还没做）、
 * 候选窗位置（`SDL_SetTextInputArea` 的手感留给用户在真机上判）。
 */
class InkingTextInput : public InkingStaticLabel {
public:
    InkingTextInput(InkingWidget* parent, const TextInputData& data);

    /// Enter 提交：回调收到当前文本（校验 / 多语言由业务侧做）。
    InkingEvent<const std::string&> submitted;

    /// 输入框要键盘焦点（不声明就拿不到——`AGENTS.md` §6 第 57 条）。
    bool WantsKeyboardFocus() const noexcept override { return true; }

    /// 吃点击（矩形本体即热区）：点它定位光标、拖它扩选。
    bool HitTest(float worldX, float worldY) const override;

    // ---------------- 内容 ----------------

    const std::string& GetEditText() const noexcept { return _edit.Text(); }

    /// 换整段文本（光标归零）：走编辑状态机，再同步给基类排版。
    void SetEditText(const std::string& text);

    TextEditState& Edit() noexcept { return _edit; }
    const TextEditState& Edit() const noexcept { return _edit; }

    // ---------------- 焦点 ----------------

    bool IsFocused() const noexcept { return _focused; }

    /// 设焦点（走和 `onFocusChanged` 同一条路：闪烁复位）。
    void SetFocused(bool focused);

    // ---------------- 输入（公开写入口：真链路与自检都走它） ----------------

    /**
     * 处理一次按键。
     *
     * @return **吃掉了这次按键吗**（退格 / 方向键 / 快捷键都算吃掉）。
     *         返回 false 的键（比如字母）交给 `onTextInput`——那是 SDL 的
     *         `TEXT_INPUT` 事件，中文输入法组合好的结果从那里来。
     */
    bool HandleKey(const KeyEvent& key);

    /// 一段文本输入（普通按键 / IME 提交都走它）。
    void HandleText(const std::string& text);

    /// 点击定位（设计坐标）：算偏移 → 移光标。
    void PressAt(float designX, float designY);

    /// 拖动扩选（设计坐标）：从按下那一点开始选到这儿。
    void DragTo(float designX, float designY);

    /// 推进光标闪烁（转发给 `onAnimationTick`）。
    ///
    /// 钩子是 protected，自检调不到；这个公开转发让"闪烁相位"也能纯逻辑验
    /// （和 `HandleKey` / `PressAt` 同类：真链路与自检走同一份实现）。
    void TickCaretBlink(float deltaSeconds);

    // ---------------- 几何观测（自检用；绘制用的也是这份） ----------------

    /// 光标矩形（设计坐标，相对组件左上角）。
    TextInputRect CaretBox() const;

    /// 选区的矩形们（一行一个；没有选区时是空的）。
    std::vector<TextInputRect> SelectionBoxes() const;

    /// 某个点（设计坐标）对应的**字节偏移**（已吸附到码点边界）。
    std::size_t OffsetAtPoint(float designX, float designY) const;

    // ---------------- 水平滚动（0.2.0 T-12） ----------------

    /**
     * 文字左移了多少（设计坐标）。0 = 没滚（文本没超宽，或光标在开头）。
     *
     * 为什么要有它：单行输入框**不换行**，文本一超宽光标就画到本体之外
     * （面板默认 `clipChildren = false`，于是它真的会出现在框外）。两件事一起做：
     * 光标位置夹进 `[0, width]`（兜底），再加一段水平滚动让光标可见（体验）。
     */
    float GetScrollX() const noexcept { return _scrollX; }

    /// 整段文字**未滚动时**的宽度（设计坐标，单行 = 最后一行的行尾）。
    /// 自检用它断言"文本确实超宽了"，也给 `EnsureCaretVisible` 算上限。
    float GetTextWidth() const;

    // ---------------- 计数观测 ----------------

    int TextInputCount() const noexcept { return _textInputs; }
    int SubmitCount() const noexcept { return _submits; }
    int CopyCount() const noexcept { return _copies; }
    int PasteCount() const noexcept { return _pastes; }

    /// 这一帧光标该不该画出来（闪烁相位）。
    bool IsCaretVisible() const noexcept { return _caretVisible; }

    // ---------------- 剪贴板（可注入） ----------------

    using ClipboardGet = std::function<std::string()>;
    using ClipboardSet = std::function<void(const std::string&)>;

    /**
     * 换掉剪贴板实现（默认走 SDL 的 `SDL_GetClipboardText` / `SDL_SetClipboardText`）。
     *
     * 可注入是为了**让复制 / 粘贴也能纯逻辑自检**：自检里塞一对内存假剪贴板，
     * 就不用去碰真实系统剪贴板（那东西会被别的程序改，测不稳）。
     */
    static void SetClipboardHandlers(ClipboardGet get, ClipboardSet set);
    static void ResetClipboardHandlers();

protected:
    void onRender(float pixelX, float pixelY) const override;
    void onAnimationTick(float deltaSeconds) override;
    void onKeyDown(const KeyEvent& key) override;
    void onTextInput(const std::string& text) override;
    void onFocusChanged(bool focused) override;
    /// 按下 / 抬起。**只认主键**：开始编辑 / 选区都是主键手势；
    /// 右键留给将来的"粘贴 / 全选"上下文菜单（0.5.0）。
    void onPointerPress(PointerButton button, bool press) override;
    void onPointerDrag(float designX, float designY, float deltaX,
                       float deltaY) override;

private:
    /// 编辑状态 → 基类：内容变了就 `SetText`（它会重排 + 让容器重排 + 标重绘）。
    void syncToLabel();

    /// 文字这一笔的水平滚动量（基类的虚钩子，T-12）。
    float TextScrollX() const noexcept override { return _scrollX; }

    /// 设滚动量（夹进 `[0, maxScroll]`；没变化不标脏）。
    ///
    /// ⚠️ 滚动量**只影响绘制**（排版与命中都用未滚动坐标），所以它不进排版
    /// 缓存键、也不进命中脏——但**必须标重绘脏**，否则"滚动量变了、画面不动"。
    void SetScrollX(float x);

    /// 让光标（含宽度）落在 `[0, width]` 里：越界才滚，不逐帧跟随。
    /// 每次光标 / 文本 / 选区变化之后调用。
    void EnsureCaretVisible();

    /// 光标 / 选区几何的共同底座：**字节偏移 → 行内矩形**。
    ///
    /// 用了基类那份排好行的结果（`GetTextLayout()`，它是 `TextLine{begin,end,width,x,y,height}`）
    /// 加 `MeasureText` 逐码点累加——所以比例字体下光标也贴在正确的字缝上。
    /// 返回的是**未滚动、未夹取**的原始坐标（`BoxForOffset` 会再套上滚动与夹取）。
    TextInputRect RawBoxForOffset(std::size_t offset) const;

    /// 带滚动与夹取的光标 / 选区矩形（绘制与观测口用的都是这一份）。
    TextInputRect BoxForOffset(std::size_t offset) const;

    /// 设计坐标 → 组件本地坐标（标签没有变换，减左上角就够）。
    float localX(float designX) const;
    float localY(float designY) const;

    TextInputData _input{};

    /// 编辑状态机（纯逻辑，B7 的核心；判据在 `ink_text_edit_test`）。
    TextEditState _edit{};

    bool _focused = false;
    bool _caretVisible = true;
    float _blink = 0.0f;

    /// 拖动扩选的起点（按下那一刻的偏移）。
    std::size_t _dragAnchor = 0;

    /// 文字左移了多少（设计坐标，T-12）。只影响绘制：排版与命中都用未滚动坐标。
    float _scrollX = 0.0f;

    int _textInputs = 0;
    int _submits = 0;
    int _copies = 0;
    int _pastes = 0;
};

}  // namespace inking
