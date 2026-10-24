#pragma once

// 预制件：**菜单浮窗**（0.4.1 T-07 / T-08）。基于 `InkingFixedPopup`。
//
// ---------------------------------------------------------------------------
// 它就是
//
//   一个定点弹出的竖排菜单：`AddButton` 加项（可顺手绑一个**局部快捷键**）、
//   `AddItem` 加任意自定义件（分隔线 / 图标行 / 子菜单占位）、`OpenAt(点)` 打开、
//   点外部或点项后关。定位 / 吃掉 / 焦点 / 点外关闭全部继承自定点浮窗，
//   本类只补三件它没有的：**竖排布局、项注册表、局部快捷键**。
//
// ---------------------------------------------------------------------------
// 局部快捷键 = 焦点模态（用户裁定）
//
//   - 表住在**本对象**里，**不注册到窗口的 `ShortcutRegistry`**。所以菜单关着时
//     这些组合在系统里根本不存在（不是"注册了但判空"）；
//   - 生效条件：菜单开着 **且** 键盘焦点在本子树（`OpenAt` 夺取、点完内部按钮后
//     由定点浮窗在指针变化时取回）；
//   - **全局表优先**：窗口层先跑 `ShortcutRegistry`，命中就把这次 `KeyDown`
//     消费掉。所以 `BindLocalShortcut` 会用 `IsTaken` 预检，被占用时打 WARN 并
//     返回 false——不静默。
//
// ---------------------------------------------------------------------------
// 扩展点
//
//   - 换按钮样式：`AddButton(自己的 ButtonData, ...)`；
//   - 加非按钮内容：`AddItem(unique_ptr / 工厂)`；
//   - 换行为：派生本类重写 `onMenuActivated`（项被激活后、关闭之前调）。
//
// 第一版**不做**：子菜单、多浮窗互斥、开合动画、键盘方向键导航（↑↓ 选、Enter 确认）。

#include <dataStruct/InkingAnchorData.h>
#include <input/Shortcut.h>
#include <panel/InkingFixedPopup.h>
#include <prefab/MenuPopupData.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace inking {

class InkingStaticButton;

namespace prefab {

/**
 * @brief 菜单浮窗。
 *
 * ```cpp
 * inking::prefab::MenuPopup menu(&scene);          // 默认样式
 * menu.AddButton(look, "保存", Save, inking::Shortcut::Parse("Ctrl+S"));
 * menu.AddButton(look, "另存为", SaveAs);
 * menu.AddItem(std::make_unique<Separator>(nullptr));
 * menu.OpenAt(clickX, clickY);                     // 定点打开（父级坐标）
 * ```
 */
class MenuPopup : public InkingFixedPopup {
public:
    /// 工厂：类型 / 配置名要到运行期才知道时用（同 `MenuBar::ItemFactory`）。
    using ItemFactory = std::function<std::unique_ptr<InkingWidget>()>;
    using Action = std::function<void()>;

    explicit MenuPopup(
        InkingWidget* parent,
        const MenuPopupStyle& style = MenuPopupStyle::MakeDefault());

    // ---------------- 内容 ----------------

    /**
     * 挂一个自定义件（分隔线 / 图标行 / 子菜单占位）。
     *
     * 会进 `_items` 并触发一次高度重算，所以 `GetItemNode` / `GetItemRect`
     * 也把它算作一项。
     */
    InkingWidget* AddItem(std::unique_ptr<InkingWidget> item);
    InkingWidget* AddItem(const ItemFactory& factory);

    /**
     * 加一个菜单项（按钮）。
     *
     * @param look       外观；`width` / `height` 留 0 时用 `style.itemWidth` / `itemHeight`。
     * @param text       项文字。
     * @param onActivate 点了之后做什么（可空）。
     * @param shortcut   顺手绑一个**局部**快捷键（空 = 不绑）。绑定失败（与全局表
     *                   冲突 / 与已有局部项重复）时打 WARN，项仍然加上。
     */
    InkingStaticButton* AddButton(const ButtonData& look, const std::string& text,
                                  Action onActivate = {},
                                  const Shortcut& shortcut = {});

    // ---------------- 局部快捷键 ----------------

    /**
     * 绑一个"只有菜单开着、且焦点在本子树时"才生效的快捷键。
     *
     * 三条拒绝规则（都打 WARN、都返回 false，不静默）：
     *   1. 空组合 / 空回调；
     *   2. 已被**窗口全局表**占用（那样局部永远收不到，绑了等于没绑）；
     *   3. 已被本菜单的**另一个**局部项占用（先到先得，不覆盖）。
     */
    bool BindLocalShortcut(const Shortcut& shortcut, Action action,
                           std::string label = {});

    /// 现在挂着几条局部快捷键（自检观测口）。
    std::size_t GetLocalShortcutCount() const noexcept {
        return _localShortcuts.size();
    }

    // ---------------- 查询（矩形相对**本浮窗**） ----------------

    std::size_t GetItemCount() const noexcept { return _items.size(); }

    /// 第 `index` 项的矩形，相对本浮窗左上角。越界 = 零矩形。
    ShapeBounds GetItemRect(std::size_t index) const;

    /// 第 `index` 项那个节点（越界 = nullptr）。
    InkingWidget* GetItemNode(std::size_t index) const;

    const MenuPopupStyle& GetStyle() const noexcept { return _style; }

    /// 触发过几次项（点击或局部快捷键各算一次）。自检观测口。
    int GetActivatedCount() const noexcept { return _activatedCount; }

protected:
    /**
     * 键按下（只有拿着焦点时才来）：查局部表，命中就执行并关闭（按 `closeOnActivate`）。
     *
     * 一次按键只做一个动作：命中就 `return`，不继续试后面的。
     */
    void onKeyDown(const KeyEvent& key) override;

    /**
     * 一项被激活（点击或快捷键）之后、关闭之前调。
     *
     * 派生类重写它做"记住上次选了什么"这类事；**不要在这里再关一次**
     * （`closeOnActivate` 已经处理）。
     */
    virtual void onMenuActivated(std::size_t index, InkingWidget* item);

private:
    /// 按项数重算容器高度（宽 = `itemWidth + padding × 2`）。
    void relayoutHeight();

    /// 统一的激活出口：计数 → `onMenuActivated` → 按开关关闭。
    void activate(std::size_t index, InkingWidget* item);

    struct LocalShortcut {
        Shortcut shortcut{};
        Action action{};
        std::string label{};
    };

    MenuPopupStyle _style{};
    std::vector<InkingWidget*> _items{};
    std::vector<LocalShortcut> _localShortcuts{};
    int _activatedCount = 0;
};

}  // namespace prefab
}  // namespace inking
