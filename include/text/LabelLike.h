#pragma once

// 静态 / 动态标签共用的**排版与取色层**（G-06，纯重构）。
//
// 承载：一份 `InkingLabelData`、排版缓存（`LayoutCache`）、按宽度排版（`LayoutForWidth`）、
// 缓存键比对（`CacheMatches`）、文字色解析（`EffectiveTextColor`）、测量
// （`MeasureLabel`）、画文字那一笔（`paintLabelText`）、以及观测口 `GetTextLayout`。
//
// 抽取的判据只有一条：**行为零变化**。所以这里的每个函数都与 G-06 之前写在
// `InkingStaticLabel.cpp` 里的那份**逐字节相同**（去注释、类名归一后 `cmp` 可比），
// 只有两处机械改名：`MeasureLayout` → `MeasureLabel`（那个名字是
// `InkingWidget` 的虚函数，覆盖必须留在组件里）、`onRender` → `paintLabelText`
// （同上，且它多一个"水平滚动量"参数，见下）。
//
// ---------------------------------------------------------------------------
// 为什么不是 Widget、也**一个虚函数都没有**
//
//   1. **不是 Widget**：两档各自继承自己的 Widget 基类（静态 / 动态是二选一），
//      再多继承本类。`InkingLabelLike` 不是删除用的多态手柄，没有 `onRender` / `onTick`。
//   2. **不能有虚函数**（`TaskGuide-G.md` §4 第 2 条）：虚析构会给"次基类"引入
//      析构 thunk，`nm -g` 的符号集合当场变——G-02（`InkingImageLike`）踩过、G-04
//      （`InkingSpriteLike`）据此后置。所以这里的析构是**受保护且非虚**的。
//   3. 于是"要组件替它做的事"只能走两种口子：**构造期注入**（`_host` 指针）或者
//      **调用方传参**（`paintLabelText` 的 `scrollX`）。没有第三种。
//   4. 本类**没有纯虚函数**：它不是契约，而是"一份实现被两个类共用"。
//      `src/inking.cpp` 用 `is_abstract_v` / `has_virtual_destructor_v` 钉住它。
//
// ---------------------------------------------------------------------------
// `_host` 只读两样东西（别把它当成"Widget 的替身"）
//
//   - `GetMagnification()`：绘制的设计坐标 → 设备像素倍率（`paintLabelText` 用）；
//   - `GetExternalTransform()`：A13 的外部显示变换（同一笔要跟着走）。
//   尺寸（`GetWidth/GetHeight`）也是从宿主读的——排版与绘制都按**当前实际几何**排，
//   而不是按配置里那份（动态档的 `Resize` 改的正是宿主几何）。
//
// ---------------------------------------------------------------------------
// 写入口不进共层：`SetText` / `SetTextColor` 的"之后做什么"两档不同
//
//   静态版换文字要 `RelayoutParent()`（兄弟让位、无布局容器时自测量）；动态版
//   **故意**不动几何（要显式 `FitToContent()`，见 G-05 的关键决定一）。把这两条
//   合并进 `Like` 就会把"什么时候改几何"这个语义差别抹掉——所以 `Like` 只做
//   **数据手术**（`assignText` / `assignTextColor`：改字段 + 让缓存失效 / 刷新
//   主题色缓存），标脏与重排留在两档各自的薄转发里（与 `InkingImageLike` 的
//   "Like 自己不适标脏"同一条纪律）。
//
// ---------------------------------------------------------------------------
// 排版缓存键必须完整（`LayoutCache` 那段注释是要紧的）
//
//   缓存对象是"某一次排版的结果"，键是**所有影响排版的字段**：文本、字号、行距、
//   字体（含回退链）、对齐、内缩、可用宽/高、行数上限、换行与省略、语言版本。
//   少比一个就是"改了配置、画面没变"（`AGENTS.md` §6 第 72 条那类静默失效）。
//   高度进键是 0.8.3 T-01 加的（它能改变"排几行"），语言版本是 A7 加的。
//
// ---------------------------------------------------------------------------
// 两档留着同名私有转发（ABI 纪律，与 G-02 / G-04 同一条）
//
//   `LayoutForWidth` / `CacheMatches` / `EffectiveTextColor` 在 G-06 之前是
//   `InkingStaticLabel` 的私有成员，`nm -g` 能看到它们的符号。挪走之后两档各留
//   一份**一行转发**，符号一个不少（判据：`nm -g` 的符号名集合只增不减）。

#include <dataStruct/InkingLayout.h>
#include <dataStruct/InkingTextLayout.h>
#include <text/LabelData.h>

#include <cstdint>
#include <string>

class InkingWidget;

/**
 * @brief 两档标签共用的排版与取色（G-06，纯重构）。
 *
 * 无虚函数、无纯虚函数、受保护的非虚析构。见文件头。
 */
class InkingLabelLike {
public:
    InkingLabelLike(const InkingLabelLike&) = delete;
    InkingLabelLike& operator=(const InkingLabelLike&) = delete;

    /// 构造时那份数据（已由配置装好；`InkingLabelData` 没有 `Normalize`）。
    const InkingLabelData& GetData() const noexcept { return _data; }

    /**
     * @brief 最近一次排版结果（观测口 + 绘制用）。
     *
     * **不推脏**：按宿主**当前**宽高排一次并缓存，尺寸没变就直接给上一次的结果
     * （`AGENTS.md` §6 第 41 条那条"观测口要不要推脏"必须写死一条）。
     */
    const InkingTextLayoutResult& GetTextLayout() const;

protected:
    // 非虚且受保护：Like 不是删除用的多态手柄；也避免给旧组件新增析构 thunk。
    ~InkingLabelLike() = default;

    // 派生类须传 static_cast<InkingWidget&>(*this)：直接传 *this 会让禁拷贝
    // 构造函数也成为精确匹配候选，重载决议因此歧义（`InkingImageLike` 同一条注释）。
    InkingLabelLike(InkingWidget& host, const InkingLabelData& data) noexcept;

    /// 配置驱动那条路（只有名字、没有数据）用：数据留默认值，名字由派生类写。
    explicit InkingLabelLike(InkingWidget& host) noexcept;

    // ---------------- 数据手术（写入口留在两档） ----------------

    /// 换文字：真的变了才 true；变了就让排版缓存失效。**不标脏、不重排。**
    bool assignText(std::string text) noexcept;

    /**
     * 换文字色：真的变了才 true。
     *
     * 副作用三条（与 G-05 之前逐字相同）：清掉 `colorToken`（手动设色 = 这颜色
     * **不再是语义色**，不清的话下一帧主题版本的比较会把用户刚设的颜色盖回去）、
     * 把主题色缓存直接写成新值并记当前版本、**不动排版缓存**（颜色走纹理调制）。
     */
    bool assignTextColor(std::uint32_t color) noexcept;

    // ---------------- 测量与绘制（两档的转发目标） ----------------

    /// `InkingWidget::MeasureLayout` 的实现：宽度有约束就用约束，高度是排版结果。
    InkingLayoutSize MeasureLabel(const InkingLayoutConstraints& constraints) const;

    /**
     * @brief 画排好的每一行（`inking_detail::PaintText` 那一笔）。
     *
     * @param scrollX 文字这一笔的**水平滚动量**（设计坐标，0 = 不滚）。它是组件的
     *   虚钩子 `TextScrollX()` 的值，由组件的 `onRender` 传进来——`InkingLabelLike` 里
     *   不能有虚函数，也就不能自己去问（输入框 `InkingTextInput` 覆盖它，用来把
     *   超宽的单行文字左移，让光标停在框里）。只影响绘制：排版与命中用未滚动的坐标。
     */
    void paintLabelText(float pixelX, float pixelY, float scrollX) const;

    // ---------------- 两档的 ABI 薄转发目标（受保护：转发要调得到） ----------------

    /// 按给定可用宽度 / 高度排版（带缓存）。`<= 0` 表示该方向不限。
    const InkingTextLayoutResult& LayoutForWidth(float available,
                                           float availableHeight) const;

    /// 缓存键是否还匹配（逐字段比，见 `LayoutCache` 的注释）。
    bool CacheMatches(float available, float availableHeight) const noexcept;

    /// 文字色**真正要画**的值（语义色按当前主题重查，按 `InkingThemeVersion()` 缓存）。
    std::uint32_t EffectiveTextColor() const noexcept;

    /// 标签的数据面。受保护：两档的构造要写名字，观测口 `GetData()` 走上面的口。
    InkingLabelData _data;

private:
    /**
     * 排版的缓存键：**所有影响排版的字段**都要进来（`AGENTS.md` §6 第 72 条）。
     *
     * `languageVersion` 是 A7 的"切换时机"：它一变，缓存自然未命中 ⇒ 重查串、
     * 重排、重烘纹理。**不需要**谁来遍历所有标签通知一遍。
     * `resolvedText` 存的是**查表之后**的串：行区间（`InkingTextLine::begin/end`）
     * 指向的是它，而不是 `InkingLabelData::text` 里的那个占位符。
     */
    struct LayoutCache {
        bool valid = false;
        std::string text;
        std::string fontPath;
        /// 字体回退链（T-13）：**必须一起比**。链变了而这里不比，排出来的
        /// 宽高会停在旧字体上——"改了配置、框还是旧尺寸"，就是 §6 第 72 条
        /// 那类静默失效。
        std::vector<std::string> fontPaths;
        float fontSize = 0.0f;
        float lineSpacing = 0.0f;
        InkingTextAlignH align = InkingTextAlignH::Left;
        InkingTextAlignV alignV = InkingTextAlignV::Top;
        InkingLayoutSpacing padding{};
        float availableWidth = 0.0f;
        /// 排这一次时用的**高度上限**（0 = 不限）。它 0.8.3 起真的影响"排几行"
        /// （T-01），所以必须进键——漏了就是"改了框高、行数没变"。
        float availableHeight = 0.0f;
        int maxLines = 0;
        InkingTextWrap wrap = InkingTextWrap::Word;
        InkingTextOverflow overflow = InkingTextOverflow::Clip;
        std::uint64_t languageVersion = 0;
        std::string resolvedText;
        InkingTextLayoutResult result{};
    };

    InkingWidget* _host;  // 非拥有；与组件同生命周期，构造后永不为空。

    mutable LayoutCache _cache;

    /// 上一次解析文字色时的主题版本与结果（和 `LayoutCache` 分开：颜色变不改排版）。
    /// `_themeColorValid` 区分"还没算过"与"算出来正好是版本 0"。
    mutable bool _themeColorValid = false;
    mutable std::uint64_t _themeVersion = 0;
    mutable std::uint32_t _themeColor = 0xFFFFFFFFu;
};

