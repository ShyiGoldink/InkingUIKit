#pragma once

// 预制件 C1「选项栏」的**纯数据**面。
//
// 这一批（C 系列）的形态先说清：**不写新底层，用现有配件组装**。
// 所以这里没有新的"组件类型"，只有"一份项列表 + 一份外观与排列"——
// 排列用的是 `PanelData::layout`（A6 那条现成的布局通道），项的外观用的是
// `ButtonData`（按钮那份现成配置）。**生成器一行都不用改**：默认外观就是
// `CXXCSS/Button/optionItem.json` 生成出来的那个类。
//
// 数据与逻辑分开的理由和组件一样（`docs/SKILL.md` §3.1）：这份结构没有需要
// 维护的不变量，就是个"预制件长什么样"的记录 → struct；干活的是
// `include/prefab/OptionBar.h` 里那个类。

#include <button/ButtonData.h>
#include <dataStruct/InkingLayout.h>
#include <panel/PanelData.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inking::prefab {

/// 模块名常量：日志与自检要用（`docs/AGENTS.md` §3 的老规矩）。
inline constexpr const char* kOptionBarModuleName = "OptionBar";

/**
 * @brief 选项栏里的**一项**。
 *
 * "用户加一个选项 = 加一个对象"就是这一条：往 `std::vector<OptionItem>`
 * 里多塞一个结构体，别的一行都不用写。
 */
struct OptionItem {
    /**
     * 标识：回调带出去的就是它（`optionClicked(id, index)`）。
     *
     * 空字符串时**回退成索引的十进制串**（`ItemId()` 负责这条）——
     * 允许"只要一个能区分的东西"的用法，但别指望空 id 能当业务键用。
     */
    std::string id;

    /// 显示文字。空 = 用 `id`（两者都空就是一个没有字的面板色块）。
    std::string text;

    /**
     * 能不能点。
     *
     * 静态按钮**没有**禁用态这个词汇（三态只有常 / 悬 / 按），所以"禁用"在
     * 预制件这一层落成两件事：① 回调**不发**（点了等于没点）；② 文字调暗
     * （借用 `ButtonAppearance` 那三态**之外**的 `SetTextColor`）。
     * 于是背景仍会随悬停变化——这是"用现有配件组装"的边界，写在
     * `docs/UsageReport.md` 的预制件那一节里，不藏着。
     */
    bool enabled = true;

    /// 这一项占多高（设计坐标）。0 = 用 `OptionBarStyle::itemHeight`。
    float height = 0.0f;

    /// 内容比较：`SetItems` 靠它判"要不要重建"（幂等）。
    bool operator==(const OptionItem& other) const noexcept {
        return id == other.id && text == other.text && enabled == other.enabled
               && height == other.height;
    }
    bool operator!=(const OptionItem& other) const noexcept {
        return !(*this == other);
    }
};

/**
 * @brief 选项栏的外观与排列。
 *
 * **排列规则写在 `panel.layout` 里**（不在这里再抄一份 axis / gap / padding）：
 * 两份真相迟早分叉，而布局通道本来就是"规则住在容器上"那一条
 * （`include/ink/dataStruct/InkingLayout.h` 顶上那三句话）。
 * 默认那份（`MakeDefault()`）是：竖向 `Stack` + `gap = 6` + `padding = 6` + 交叉轴拉伸。
 */
struct OptionBarStyle {
    /**
     * 面板那一层：尺寸、背景、形状、裁剪、滚动、以及**排列规则**（`layout`）。
     *
     * 项多时想滚就把 `panel.scroll.enabled` 打开（顺手 `clipChildren = true`），
     * 想剪溢出同上——两条都是 Panel 现成的能力，预制件不转发、不重写。
     */
    PanelData panel{};

    /**
     * 每项的外观模板（`ButtonData`）。
     *
     * 换成自己的外观只有一步：把你那份配置生成出来的类里的静态 `Data()`
     * 传进来（`inking::cxxcss::OptionItem::Data()`，见 `CXXCSS/Button/optionItem.json`）。
     * 默认那份是 `MakeDefaultItemLook()`——灰底白字，够看清，不好看。
     *
     * ⚠️ 这里给的那份**三态继承关系由你自己负责**：`ButtonData::Normalize()` 的
     * 规矩是"`hoverInheritsNormal` 为真就拿 normal 覆盖 hover"（`AGENTS.md`
     * §6 第 43 条），所以想给悬停 / 按下**不同**的颜色，必须显式把
     * `hoverInheritsNormal` / `onclickedInheritsNormal` 置 false，否则你给的那两份
     * 会被 normal 悄悄盖掉。
     */
    ButtonData itemLook{};

    /// 项高（设计坐标）：`OptionItem::height` 为 0 时用它。
    float itemHeight = 40.0f;

    /**
     * 项文字的颜色（0xAARRGGBB）。**0 = 用 `itemLook` 自带的那份**。
     *
     * 为什么要有这个字段：CXXCSS 的 `text` 段里**没有颜色**（那是刻意的，
     * 见 `CXXCSS.md` §5：文字色属于主题 / 运行期）。所以"用自己那份 json 的外观"
     * 时，文字色只能在这里补——否则亮色主题下会变成白字白底。
     */
    std::uint32_t textColor = 0;

    /// 一份能直接用的默认样式。
    static OptionBarStyle MakeDefault();
};

/**
 * 一份"什么都不配也看得见"的项外观：圆角矩形 + 深灰底 + 浅色文字，
 * 三态各不相同（`hoverInheritsNormal` / `onclickedInheritsNormal` 显式关掉）。
 */
ButtonData MakeDefaultItemLook();

}  // namespace inking::prefab
