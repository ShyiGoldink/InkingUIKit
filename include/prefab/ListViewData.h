#pragma once

// 预制件 C3「列表」的**纯数据**面。
//
// 它是 `ScrollView`（B12 的 `scroll` 段）+ 布局 + 动态项组装：编辑器里列剧本
// （573 屏那个量级）/ 素材 / 角色用的就是它。
//
// **B9 后移之后"只建可见项"由这里接管**（任务原文）：框架不动，预制件按滚动
// 位置只建"看得见的那几项"。判据是硬的：1000 项时节点数与内存**不随总项数上涨**
// ——那条能断言，不靠感觉。

#include <button/ButtonData.h>
#include <dataStruct/InkingLayout.h>
#include <panel/PanelData.h>

#include <cstdint>
#include <string>
#include <vector>

namespace inking::prefab {

/// 模块名常量：日志与自检要用。
inline constexpr const char* kListModuleName = "ListView";

/**
 * @brief 列表里的一行。
 *
 * 和 C1 的 `OptionItem` 同一个形态（数据驱动：加一行 = 加一个对象），
 * 只是没有"每项单独的高度"——列表的行高是**统一**的，因为窗口化要靠
 * "行高 × 索引"直接算出位置（参差不齐的行高就没法只建可见项了）。
 */
struct ListItem {
    std::string id;    ///< 标识（回调带出去；空 = 用索引的十进制串）
    std::string text;  ///< 显示文字（空 = 用 id）
    bool enabled = true;

    bool operator==(const ListItem& other) const noexcept {
        return id == other.id && text == other.text && enabled == other.enabled;
    }
    bool operator!=(const ListItem& other) const noexcept {
        return !(*this == other);
    }
};

/// 选择模式。
enum class ListSelectionMode : std::uint8_t {
    None = 0,   ///< 不选（点了只发事件、不改选择集；当"命令列表"用）
    Single,     ///< 单选（默认）
    Multiple,   ///< 多选（再点一下取消那一项）
};

/**
 * @brief 列表的外观、行高与选择行为。
 *
 * 视口的尺寸 / 背景 / 形状写在 `panel` 里；**滚动与裁剪由预制件强制打开**
 * （列表不能滚就不是列表），`panel.scroll` 的其余字段（步长 / 条宽 / 颜色）
 * 照用户配的用。
 */
struct ListViewStyle {
    PanelData panel{};

    /// 行外观模板（`ButtonData`）。换成自己那份配置生成的类即可（同 C1）。
    ButtonData rowLook{};

    /// 行高（设计坐标）。**必须大于 0**：窗口化靠它算位置。
    float rowHeight = 32.0f;

    /**
     * 选中行的填充色（0 = 不覆盖，用外观自己的颜色）。
     *
     * 实现上是一层覆盖：`ListRow` 把外观那一笔的颜色换成它（文字照旧）。
     * 这是"用现有配件组装"能拿到的最近的一档——真要做"选中态是外观的第四态"
     * 得改按钮的三态模型，那是底层的事，不在 C 系列里。
     */
    std::uint32_t selectedColor = 0xFF3E6E9Eu;

    /// 选择模式。
    ListSelectionMode selection = ListSelectionMode::Single;

    /**
     * **只建可见项**（窗口化）。
     *
     * false（默认）：项数就是节点数——项少时最直观，也不依赖"滚动后同步"。
     * true：按当前滚动量只建可见的那几行（外加 `overscan` 行余量），
     * 1000 项也只有十几二十个节点。滚动之后会自动对齐
     * （`ListView::SyncVisibleWindow`，或每渲染帧自动兜底）。
     */
    bool windowed = false;

    /// 窗口化时上下各多建几行（滚动的余量：滚轮一格不至于看到空档）。
    int overscan = 2;

    /**
     * 行文字的颜色（0xAARRGGBB）。**0 = 用 `rowLook` 自带的那份**。
     *
     * 与 `OptionBarStyle::textColor` 同一条理由：CXXCSS 的 `text` 段没有颜色
     * （文字色属于主题 / 运行期），所以"用自己那份 json 外观"时要在这里补。
     */
    std::uint32_t textColor = 0;

    /// 一份能直接用的默认样式（深色视口 + 圆角行 + 单选 + 全部建出来）。
    static ListViewStyle MakeDefault();
};

/// 一份"深灰行 + 浅色字"的默认行外观（三态各不相同，继承关系显式关掉）。
ButtonData MakeDefaultRowLook();

}  // namespace inking::prefab
