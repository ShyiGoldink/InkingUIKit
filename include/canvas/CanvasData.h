#pragma once

// Canvas（A12）的**纯数据**定义。
//
// 与其它组件最不同的一条：**内容不由配置描述**。
// 按钮 / 面板 / 标签的外观都在数据里（颜色、形状、文字），而"这块布上画什么"
// 来自一个**注入的绘制器**（见 `InkingCanvasPainter.h` / `CanvasContext.h`）。
// 所以这份数据只回答四件事：多大、什么形状、什么底色、摆在哪儿。
//
// 为什么这样切：画布存在的意义就是"**框架不认识你要画什么**"。把内容塞进数据
// （颜色列表 / 图元数组）等于让框架为每一种画法都长一个字段——那正是本任务要
// 消掉的东西（`TaskGuide.md` 的最终目标：加东西只写它自己的逻辑）。

#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingDecoration.h>
#include <dataStruct/InkingShapeSpec.h>

#include <cstdint>
#include <string>

namespace inking {

/// 模块名常量：日志与自检要用（`SKILL.md` §10）。
inline constexpr const char* kCanvasModuleName = "InkingStaticCanvas";

/**
 * @brief 一块画布的静态配置。
 *
 * 字段与其它组件的数据保持同一套命名（`width` / `height` / `shape` / 锚点那一组），
 * 因为构造时会**逐字段搬进基类**——漏一处的症状是"配置里写了、运行期没生效"，
 * 而且不报错（`AGENTS.md` §6 第 83 条）。
 */
struct CanvasData {
    std::string name;  ///< 与代码里用的名字一致（画布不走生成器，手写构造）

    int width = 0;   ///< 设计坐标
    int height = 0;

    ShapeSpec shape = ShapeSpec::Rect();  ///< 本体形状：底色与命中都从它派生

    /**
     * 本体底色，0xAARRGGBB。**默认 0 = 不画**。
     *
     * 与按钮 / 面板不同，这里**不做"全透明兜底成黑"那一步**（`ButtonData::Normalize`
     * 那条）：画布透明是完全合法的常见形态——它就是一块"只画内容"的透明层。
     * 命中不受影响（命中判的是形状本体，和底色无关）。
     */
    std::uint32_t color = 0u;

    /// 本体描边（B3 那一套：一半在外一半在内、不参与命中）。`width <= 0` 不画。
    StrokeSpec stroke{};

    AnchorPoint selfAnchor{};   ///< 自身锚点
    AnchorPoint traceAnchor{};  ///< 上级锚点
    float offsetX = 0.0f;  ///< 对齐之后再挪一点，设计坐标
    float offsetY = 0.0f;

    /// 布局（A6）：`layout` = 我是容器时怎么排孩子，`layoutItem` = 我在父容器里的
    /// 意愿。与 `AnchorData` 里同名的那两个是同一份语义（构造时逐字段搬）。
    LayoutSpec layout;
    LayoutItem layoutItem;

    int zIndex = 0;       ///< 越大越靠上
    bool visible = true;  ///< 自己这一层的可见性

    /**
     * 内容超出画布边界时剪掉吗，**默认剪**。
     *
     * 画布在语义上就是"视口"：绘制器按本地坐标画，越界的部分本该看不见
     * （越界内容画到邻居身上，是最难往裁剪上想的一类错）。要故意溢出
     * （光晕 / 投影 / 特效）就 `SetClipContent(false)`——那时裁剪交回调用方。
     */
    bool clipContent = true;
};

}  // namespace inking
