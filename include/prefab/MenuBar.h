#pragma once

// 预制件 C2「菜单栏」（`docs/TaskGuide.md` 的 C 系列）。
//
// **它就是**：窗口顶部一条横向容器，用来替掉系统标题栏 / 菜单。里面不仅能放按钮，
// 还能放 `Image` 和**任意 `InkingWidget`**——所以本类的核心只有一个入口
// `AddItem(任意锚点)`，别的都是便利写法。
//
// 材料全是现成的：`Panel` + `layout`（`Horizontal` + `justify`——"偏左 / 偏右"
// 就是 `Start` / `End`，`SpaceBetween` 是"两端顶住"）+ `AddChild` 任意锚点。
//
// ---------------------------------------------------------------------------
// 内部怎么让"内容偏左 + 窗口按钮贴右上角"同时成立
//
// 一条 `justify` 表达不了这件事：`SpaceBetween` 会把**所有**项往两端推。
// 所以内部插一根**弹簧**（`InkingStaticWidget` + `layoutItem.fillMain = true`，
// 宽 0）：主轴剩余空间全被它吃掉，它后面的窗口按钮自然贴到右边——
// 而内容项在它前面，照旧按调用方给的 `justify` 排。
// 这也是"用现有配件组装"那条路的做法：不新增词汇，用布局通道的 `fillMain`。
//
// 弹簧与窗口按钮都排在**最后**：`AddItem` 每次都会把它们重新挪到末尾
// （`RemoveChild` 摘下来再 `AddChild` 挂回去——被拥有的节点只能走这条路，
// `AGENTS.md` §6 第 67 条）。
//
// ---------------------------------------------------------------------------
// 无边框窗口（C2 的另一半，需要用户裁定 → 已裁定：真做）
//
// 替换系统标题栏要两件事：① 窗口层的无边框开关（`InkingWindow::SetBorderless`
// + `MoveWindowBy` + `RequestClose`，窗口层已经提供）；② 自绘标题栏本身——
// 那就是 `MenuBarStyle::draggable`（按住空白处拖窗口）与 `captionButtons`
// （右上角最小化 / 关闭）。**本类不碰窗口层的实现**，只是把指针事件翻成那三个调用。
//
// 第一版**不做**：无边框下自绘改尺寸（要边缘把手 + 命中 + 最小尺寸约束）、
// 双击最大化、系统级吸附。

#include <widget/InkingStaticWidget.h>
#include <event/InkingEvent.h>
#include <input/Shortcut.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/MenuBarData.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace inking {

class InkingStaticButton;

namespace prefab {

/**
 * @brief C2：预制菜单栏（也是无边框窗口的自绘标题栏）。
 *
 * ```cpp
 * inking::prefab::MenuBarStyle style = inking::prefab::MenuBarStyle::MakeDefault();
 * style.draggable = true;        // 无边框：按住空白处拖窗口
 * style.captionButtons = true;   // 无边框：右上角自绘最小化 / 关闭
 * inking::prefab::MenuBar bar(&scene, style);
 * bar.AddButton(inking::cxxcss::MenuFile::Data(), "文件", [&] { ... });
 * bar.AddItem(std::make_unique<inking::cxxcss::TitleLogo>(nullptr));  // 任意锚点
 * bar.SetJustify(inking::LayoutJustify::Start);                       // 偏左
 * ```
 */
class MenuBar : public InkingStaticPanel {
public:
    /// 工厂：类型 / 配置名要到运行期才知道时用（`CXXCSS.md` §7.2 说的那件事——
    /// "容器 + 子项"本来就得靠代码组装，工厂就是那条组装的缝）。
    using ItemFactory = std::function<std::unique_ptr<InkingWidget>()>;

    /// 一个内容按钮被点了。
    using ButtonCallback = std::function<void()>;

    /// 自绘标题栏的关闭按钮被点了（**只是通知**：真正关窗由本类转发给
    /// `InkingWindow::RequestClose`，同时发这个事件）。
    InkingEvent<> closeClicked;

    /// 自绘标题栏的最小化按钮被点了。
    InkingEvent<> minimizeClicked;

    explicit MenuBar(
        InkingWidget* parent,
        const MenuBarStyle& style = MenuBarStyle::MakeDefault());

    /**
     * 析构：**必须**退订窗口的全屏事件、并注销所有快捷键。
     *
     * 那两样都住在**进程级**对象里（`InkingWindow` 单例 / 它的快捷键表），
     * 比菜单栏活得久。不退订 = 回调里拿着已经析构的 `this`——那是
     * "关掉一个场景之后，按快捷键就崩"这类最难复现的 bug。
     */
    ~MenuBar() override;

    // ---------------- 内容项：任意锚点 ----------------

    /**
     * @brief 顶栏**现在占多少高度**（B3）：显示时是自身高度，隐藏（全屏）时是 0。
     *
     * 存在的理由很具体：`MenuBarStyle::hideInFullscreen` 会让整条顶栏在全屏时
     * 消失，而宿主的内容区高度必须按"窗口高 − 顶栏**当前**高"算——写死
     * `窗口高 − 40` 的结果就是全屏时底下空一条。
     *
     * 与 `GetHeight()` 的分工：后者是**布局尺寸**（配置那份，隐藏时也不变），
     * 这一条回答的是"画面上还剩多少"。所以内容区用这一个，别用那个。
     */
    int GetCurrentHeight() const noexcept {
        return IsVisibleInTree() ? GetHeight() : 0;
    }

    /// 顶栏现在可见吗（全屏且 `hideInFullscreen` 时为 false）。B3 的另一个观测口。
    bool IsBarVisible() const noexcept { return IsVisibleInTree(); }

    /**
     * 挂一个**由本菜单栏拥有**的子项（按钮 / 图片 / 自定义锚点都行）。
     *
     * 传进来的节点父级会被改成本菜单栏的内容容器（原父级自动摘干净）。
     * 挂完自动重排一次——布局是显式的（§6 第 86 条），但"加了一项"这件事
     * 太容易忘了喊，所以这里替你喊（幂等，多喊无妨）。
     */
    InkingWidget* AddItem(std::unique_ptr<InkingWidget> item);

    /// 工厂版：`AddItem([]{ return std::make_unique<Xxx>(nullptr); })`。
    InkingWidget* AddItem(const ItemFactory& factory);

    /**
     * 便利：加一个按钮（用给定的外观 + 文字 + 回调）。
     *
     * @param shortcut 顺手绑一个**全局快捷键**（空 = 不绑）。绑定落在窗口的
     *                 `ShortcutRegistry` 上，所以它**不依赖焦点**、也不依赖
     *                 菜单栏是否可见（全屏隐藏后照样能用，这正是"全局"的意思）。
     */
    InkingStaticButton* AddButton(const ButtonData& look, const std::string& text,
                                  ButtonCallback onActivate = {},
                                  const Shortcut& shortcut = {});

    // ---------------- 全局快捷键 ----------------

    /**
     * 绑一个快捷键到某个动作（菜单项的快捷键走 `AddButton` 的第 4 参即可）。
     *
     * @return 绑定 id；冲突（组合已被占）或参数为空时返回 0。
     *         本类的析构会把自己的全部绑定注销掉，调用方不用管。
     */
    std::uint64_t BindShortcut(const Shortcut& shortcut,
                               ButtonCallback onActivate,
                               std::string label = {});

    /// 本菜单栏现在挂着几个快捷键（自检观测口）。
    std::size_t GetBoundShortcutCount() const noexcept;

    // ---------------- 查询（矩形都相对**菜单栏**，方便直接断言） ----------------

    /// 内容项数。
    std::size_t GetItemCount() const noexcept;

    /// 第 `index` 项的矩形，**相对菜单栏左上角**（设计坐标）。越界 = 零矩形。
    ShapeBounds GetItemRect(std::size_t index) const;

    /// 第 `index` 项那个锚点（越界 = nullptr）。
    InkingWidget* GetItemNode(std::size_t index) const;

    // ---------------- 对齐 ----------------

    /**
     * 设内容对齐（`TaskGuide.md` C2 点名的映射）：
     * `Start` = 偏左、`End` = 偏右、`SpaceBetween` = 两端顶住。
     * 改完重排一次（幂等）。
     */
    void SetJustify(LayoutJustify justify);

    LayoutJustify GetJustify() const noexcept;

    // ---------------- 无边框标题栏 ----------------

    /// 运行期开关"按住空白处拖窗口"。
    void SetDraggable(bool draggable) noexcept { _style.draggable = draggable; }
    bool IsDraggable() const noexcept { return _style.draggable; }

    /// 现在正在拖窗口吗（按下了、还没抬起）。自检观测口。
    bool IsDragging() const noexcept { return _dragging; }

    /**
     * 加"最小化 / 关闭"两个按钮（构造时按 `style.captionButtons` 自动调一次；
     * 也可以运行期手动加）。重复调用是空操作。
     */
    void AddCaptionButtons();

    /// 现在有几个标题栏按钮。
    std::size_t GetCaptionButtonCount() const noexcept;

    const MenuBarStyle& GetStyle() const noexcept { return _style; }

    // ---------------- 全屏联动 ----------------

    /// 运行期开关"全屏时自动隐藏"。
    void SetHideInFullscreen(bool hide) noexcept;

    /// 现在是被"全屏"这条规则藏起来的吗（自检观测口）。
    bool IsHiddenByFullscreen() const noexcept { return _hiddenByFullscreen; }

    /**
     * 本子树的滚轮归我（重写 B12 那条"容器优先"）。
     *
     * 必须重写：场景只把滚轮给"指针下最上面那个"，而指针停在菜单**按钮**上时
     * 那个节点是按钮、不是菜单栏；不声明这条，滚轮会派给按钮，然后什么都不会发生。
     * 声明之后场景沿父链找到本菜单栏（`InkingWidget::FindSubtreeWheelOwner`）。
     */
    bool HandlesSubtreeWheel() const noexcept override;

protected:
    void onPointerPress(bool press) override;
    void onPointerDrag(float designX, float designY, float deltaX,
                       float deltaY) override;

    /// 滚轮：缩放优先（见 `MenuBarStyle::zoomWheel`），没开才交给面板去滚动。
    void onPointerWheel(float deltaX, float deltaY) override;

private:
    /// 内容项（按添加顺序）。`GetItemCount` / `GetItemRect` / `GetItemNode` 读它。
    std::vector<InkingWidget*> _items;

    /// 标题栏按钮（最小化 / 关闭）。没有 `captionButtons` 时是空的。
    std::vector<InkingStaticButton*> _captionButtons;

    /// 把内容与窗口按钮之间撑开的"弹簧"（`fillMain`）。没有标题栏时不建。
    InkingStaticWidget* _spacer = nullptr;

    /// 本菜单栏注册过的快捷键 id（析构时要逐个注销）。
    std::vector<std::uint64_t> _shortcutIds;

    /// 全屏事件的订票（`InkingEvent::Token`）；0 = 没订。
    std::uint64_t _fullscreenToken = 0;

    MenuBarStyle _style{};
    bool _dragging = false;

    /// 这次隐藏是不是"全屏"那条规则干的（退出全屏只恢复这一次）。
    bool _hiddenByFullscreen = false;

    /// 把弹簧与窗口按钮**重新挂到末尾**（`AddItem` 之后调；幂等）。
    void RebuildCaptions();
};

}  // namespace prefab
}  // namespace inking
