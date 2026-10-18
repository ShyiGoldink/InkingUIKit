#pragma once

// 锚点定位层的数据（纯数据，无逻辑）。
//
// 这一层回答的是**唯一一个问题**：「我在哪儿？」
//   - 位置**不存**，由锚点推导：
//     自身矩形里的「自身锚点」落在父级矩形里的「上级锚点」上，再叠偏移；
//   - 尺寸存（`width` / `height`），因为它是推导的输入之一。
//
// **这里不放的东西**（它们属于 Widget 层，见 `include/widget/InkingWidget.h`）：
//   树结构（父子表 / 子节点所有权）、场景绑定、层级 z、两种脏、命中、布局、
//   变换、输入钩子、渲染。
//
// 命名空间：本层用 `inking`（`dataStruct/` 下的文件正在逐个迁到这个命名空间）。

#include <dataStruct/InkingLayout.h>

#include <cstdint>

namespace inking {

/// 锚点：百分比，(0,0) 是左上角，(1,1) 是右下角。组件使用锚点时都被视作矩形。
///
/// 名字从 `Anchor` 改成 `AnchorPoint`：它表达的是「矩形上的一个百分比点」，
/// 不是「锚点机制」本身——后者是 `InkingAnchor` 这个类的职责，两个名字
/// 并列时容易互相误解。
struct AnchorPoint {
    float x = 0.0f;
    float y = 0.0f;
};

/// 预置锚点，拿来即用；要自定义直接填 `AnchorPoint`。
struct InkingChangeAnchor {
    static constexpr AnchorPoint LeftTop{0.0f, 0.0f};
    static constexpr AnchorPoint CenterTop{0.5f, 0.0f};
    static constexpr AnchorPoint RightTop{1.0f, 0.0f};
    static constexpr AnchorPoint LeftCenter{0.0f, 0.5f};
    static constexpr AnchorPoint Center{0.5f, 0.5f};
    static constexpr AnchorPoint RightCenter{1.0f, 0.5f};
    static constexpr AnchorPoint LeftBottom{0.0f, 1.0f};
    static constexpr AnchorPoint CenterBottom{0.5f, 1.0f};
    static constexpr AnchorPoint RightBottom{1.0f, 1.0f};
};

/// Resize 的哨兵值：某个方向传 `none`，表示这个方向不动。
struct InkingResize {
    static constexpr int none = -1;
};

/**
 * 层级的下限：zindex 一律不小于这个值。
 *
 * 绘制是**父先于子、兄弟按 z 从低到高**提交的，也就是"画得最晚的盖在最上面"。
 * 在**同一批兄弟**里，背景板这种铺满整屏、又想待在一切之下的东西必须有一个比
 * 其它兄弟都低的 z——一旦同 z，注册序号靠后就会把它排到最后一个画，整屏全被
 * 盖掉。这条下限把"背景层"单独留出来，其它组件正常用 0 及以上即可。
 *
 * 注：`zIndex` 的**语义**（局部层级：只在兄弟之间生效）归 Widget 层，
 * 定义在 `InkingWidget::IsAbove`；这里只留**下限常量**供配置默认值使用。
 */
struct InkingZIndex {
    static constexpr int background = -128;
};

/**
 * 静态初始化用的数据。
 *
 * 上半段是**定位层**要读的（锚点 / 偏移 / 尺寸），下半段是 **Widget 层**要读的
 * （层级 / 可见性 / 颜色 / 布局）。类构造时逐字段拆存进基类——**基类不存整份
 * `AnchorData`**，所以要加字段时有几处要同步（见 `AGENTS.md` §6 第 83 条）。
 */
struct AnchorData {
    // ---------------- 定位层（InkingAnchor 读） ----------------
    AnchorPoint selfAnchor{};  ///< 自身锚点
    AnchorPoint traceAnchor{}; ///< 上级锚点（追踪的父级锚点）
    float offsetX = 0.0f;      ///< 对齐之后再挪一点，设计坐标
    float offsetY = 0.0f;
    int width = 0;   ///< 自身尺寸，设计坐标
    int height = 0;

    // ---------------- Widget 层 ----------------
    int zIndex = 0;   ///< 越大越靠上，**只在兄弟之间**比较（局部层级）
    bool visible = true;  ///< 可见性；不可见连命中都不必问
    std::uint32_t color = 0xFF569CD6u;  ///< 占位填充色 0xAARRGGBB（样式层接手）

    /**
     * @name 布局
     *
     * 两个字段管两件事，别混：
     *   - `layout`：**我是容器时**，我怎么排我的孩子（默认 `kind = None` = 不布局，
     *     孩子的锚点自己定位——老配置一行不用改）；
     *   - `layoutItem`：**我是某个容器的孩子时**，我在那个布局里的意愿
     *     （margin / 要不要吃剩余空间 / min-max）。
     *
     * 这份数据是**意图**、只读：布局算出来的尺寸与位置写进组件的运行期几何，
     * **不会**回写这里（否则重排就丢了输入）。见 `InkingLayout.h` 头顶那三句话。
     */
    ///@{
    LayoutSpec layout{};
    LayoutItem layoutItem{};
    ///@}
};

}  // namespace inking
