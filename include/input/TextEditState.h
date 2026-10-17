#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace inking {

inline constexpr const char* kTextEditModuleName = "TextEditState";

/**
 * @brief 可编辑文本的**状态机**（B7）：光标、选区、插入 / 删除、剪贴板。
 *
 * 它**不是组件**、不画东西、也不知道排版：输入框组件（下一层）负责把键盘 / IME
 * 事件喂进来、把光标画出来。分开的理由和 A7 的排版一样——**纯逻辑才测得干净**：
 * 这个类一行渲染代码都没有，所以"汉字退格是不是整字删"这类问题能在
 * `ink_text_edit_test` 里当场钉住，不用起窗口、不用读像素。
 *
 * ---------------------------------------------------------------------------
 * 三条约定（都是踩过才知道疼的那种）
 *
 * 1. **位置一律是"字节偏移里的码点边界"**：`std::string` 是按字节的，而中文一个字
 *    三字节——按字节退格会把汉字切成两半（留下一个孤立的续字节，排出来是乱码）。
 *    所以所有公开接口收 / 返的偏移都保证落在**码点边界**上：传进来的偏移会
 *    **向前吸附**（`SnapToBoundary`），移动时整码点跳（见 `PrevCodePoint` /
 *    `NextCodePoint`）；
 * 2. **`SetCaret` / `MoveTo` 会吸附，不报错**：点击定位算出来的偏移常常落在字符
 *    中间（尤其是按比例字体），"吸附到左边那个字的起点"是唯一不会产生非法光标
 *    的选择；
 * 3. **换行在这一版被丢掉**：这是**单行输入框**。粘贴多行文本时 `\r` / `\n` 被删掉
 *    （其余原样），Enter 也不换行（那个键留给"提交"）。多行输入要改的是排版 +
 *    光标上下移动 + 滚动，属于另一件事，见 `docs/UsageReport.md` 里 B7 那段。
 *
 * **没做**的（写在文档里，不含糊）：撤销 / 重做、双向文本、字素簇
 * （emoji 组合、国旗是多个码点，这里按码点删）、点击的"字内精确定位"
 * （要排版层的测量，组件那一层用 `TextLine` 做）。
 */
class TextEditState {
public:
    /// 一次操作**改动了什么**——组件靠它决定"要不要重排 / 重画"。
    ///
    /// 对应项目里"标脏要覆盖所有让显示值变化的路径"那条教训（`AGENTS.md` §6
    /// 第 39 条）：光标动只影响重绘，文本动才影响重排（行宽 / 行数会变）。
    struct Change {
        bool text = false;       ///< 文本变了 ⇒ 要重新排版 + 重绘
        bool caret = false;      ///< 光标动了 ⇒ 至少重绘
        bool selection = false;  ///< 选区变了 ⇒ 至少重绘
    };

    TextEditState() = default;

    explicit TextEditState(std::string text) { SetText(std::move(text)); }

    // ---------------- 文本 ----------------

    const std::string& Text() const noexcept { return text_; }

    /**
     * 换掉整段文本：**光标归零、选区清空**。
     *
     * 归零而不是去末尾：清空 / 重新赋值之后的光标位置必须**可预测**，
     * 否则"外部改了文本、光标跑到奇怪的地方"这类问题很难复现。
     */
    void SetText(std::string text);

    bool Empty() const noexcept { return text_.empty(); }

    /// 字节数（不是字符数——要字符数请自己走 UTF-8，别把两者混起来用）。
    std::size_t Size() const noexcept { return text_.size(); }

    // ---------------- 光标与选区 ----------------

    std::size_t Caret() const noexcept { return caret_; }

    /// 设光标（**吸附到码点边界**、清掉选区）。
    void SetCaret(std::size_t byteOffset) noexcept;

    /// 选区的起点 / 终点（升序，不含光标哪边在前的信息——那是内部细节）。
    std::size_t SelectionBegin() const noexcept;
    std::size_t SelectionEnd() const noexcept;

    bool HasSelection() const noexcept { return anchor_ != caret_; }

    /// 选中的文本；没有选区返回空串。
    std::string SelectedText() const;

    void SelectAll() noexcept;

    /// 清掉选区（光标不动）。
    void ClearSelection() noexcept;

    // ---------------- 移动（`select` = 按住 Shift 时扩选） ----------------

    void MoveLeft(bool select = false) noexcept;
    void MoveRight(bool select = false) noexcept;
    void MoveHome(bool select = false) noexcept;
    void MoveEnd(bool select = false) noexcept;

    /// 移到某个字节偏移（**点击定位**走它）：吸附 + 可选扩选。
    void MoveTo(std::size_t byteOffset, bool select = false) noexcept;

    // ---------------- 编辑 ----------------

    /**
     * 插入一段文本（普通按键 / IME 提交走它）。
     *
     * 有选区时**先替换选区**（这是所有输入框的共同语义）。插入之后光标在插入内容
     * 之后。空串是空操作（但**不算**改动）。
     */
    void InsertText(std::string_view utf8);

    /// 退格：有选区删选区，否则删光标**前**一个码点。
    void Backspace();

    /// 前向删除（Delete 键）：有选区删选区，否则删光标**后**一个码点。
    void DeleteForward();

    /// 选中的文本（同 `SelectedText`）；没有选区返回空串。**不改动任何状态**。
    std::string CopySelection() const;

    /// 剪切：返回被剪掉的文本并从文本里删掉；没有选区返回空串。
    std::string CutSelection();

    /**
     * 粘贴：**换行会被丢掉**（单行输入框，见类注释第 3 条）。
     *
     * 有选区时先替换选区。空串是空操作。
     */
    void Paste(std::string_view text);

    // ---------------- 观测 ----------------

    /// 上一次操作改了什么（构造函数与 `SetText` 之后是默认值）。
    const Change& LastChange() const noexcept { return lastChange_; }

    // ---------------- 静态小工具（自检与组件都会用到） ----------------

    /// `pos` 是不是码点边界（`pos == Size()` 也算）。
    static bool IsBoundary(std::string_view text, std::size_t pos) noexcept;

    /// 把 `pos` **向前**吸附到最近的码点边界（越界会夹到 `[0, Size()]`）。
    static std::size_t SnapToBoundary(std::string_view text,
                                      std::size_t pos) noexcept;

    /// `pos` 处码点的起始偏移（`pos` 是边界时返回它自己）。
    static std::size_t PrevCodePoint(std::string_view text,
                                     std::size_t pos) noexcept;

    /// `pos` 处码点之后的偏移（跳过整个码点）。
    static std::size_t NextCodePoint(std::string_view text,
                                     std::size_t pos) noexcept;

private:
    /// 把 `[begin,end)` 删掉，光标落在 `begin`。
    void eraseRange(std::size_t begin, std::size_t end);

    std::string text_{};

    /// 光标（码点边界上的字节偏移）。
    std::size_t caret_ = 0;

    /// 选区的另一端：等于 `caret_` 表示没有选区。
    std::size_t anchor_ = 0;

    Change lastChange_{};
};

}  // namespace inking
