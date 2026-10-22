#pragma once

// Panel（面板 / 抽屉 / 下拉 / 弹窗那一档）的**纯数据**定义，
// 对应将来的 `CXXCSS/Panel/<名字>.json`（生成器支持第二个组件类型是任务 A2）。
//
// 与 `ButtonData` 同一套结构约定（数据与逻辑分开、字段名与配置一一对应），
// 但只有**两个状态**：关着 / 开着。它们和按钮的三态不是一回事——
//
//   按钮的三态是**指针状态**（悬停 / 按下），由框架推给组件；
//   面板的开合是**内容状态**（抽屉拉出来了 / 收起来了），由业务自己决定。
//
// 所以 `PanelData` 没有 `hover` 那一套，只有 `closed` / `open`，而开合这个
// 动作是组件上的写入口（`InkingStaticPanel::SetOpen` / `Toggle`），不是指针事件。
//
// 字段名将来要与 CXXCSS.md 一一对应，不在这里改名：名字对不上的排查成本，
// 比"换个更漂亮的命名"值钱得多。

#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingColor.h>
#include <dataStruct/InkingDecoration.h>
#include <dataStruct/InkingEasing.h>
#include <dataStruct/InkingScroll.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTransform.h>
#include <detail/InkLog.h>

#include <cstdint>
#include <string>

namespace inking {

/// 模块名常量：日志与自检要用（约定：每个模块都暴露一个）。
inline constexpr const char* kPanelModuleName = "InkingStaticPanel";

/**
 * @brief 一个开合状态的外观。
 *
 * 和 `ButtonAppearance` 是同一个形状：颜色 + 进入这个状态的过渡 + 目标变换。
 * 少了 `width` / `height`（逐态尺寸）：面板开合靠**平移**表达，改尺寸会让
 * 子节点的布局随之变，那是布局通道的事，不在本任务里。
 */
struct PanelAppearance {
    std::uint32_t color = 0xFF000000u;  ///< 0xAARRGGBB，alpha 参与合成

    /**
     * 这个颜色来自哪个**主题 token**（不带 `$`）；`nullptr` = 不是语义色。
     *
     * `color` 是生成期烘好的字面量（默认主题），token 名留着给运行期换肤用。
     * **必须指向静态生命周期的串**（生成器发的是字面量）。
     */
    const char* themeColor = nullptr;

    /**
     * **进入这个状态时**的过渡时长（秒）。0 = 瞬间切换（配置里没写 `transition`）。
     *
     * 写在状态自己身上：`TaskGuide.md` 的 D-1 定了"过场段各自带时长"——
     * 关→开用 `open` 这份、开→关用 `closed` 那份，可以一个快一个慢
     * （抽屉常见的手感就是"拉出来利落、收回去稳"）。
     */
    float transitionSeconds = 0.0f;

    /// 这段过渡用哪条缓动曲线（缺省线性）。曲线只改变进度，不改时长与端点。
    Easing easing = Easing::Linear;

    /**
     * 这个状态下的**目标变换**。它就是"抽屉拉出来多少"的唯一表达：
     * 关着时 `translateX = -宽度`（整块挪出父级左边），开着时单位变换。
     *
     * 注意它**作用于整棵子树**（见 `InkingWidget::GetSubtreeTransform`）：
     * 面板里的子节点不用各自知道自己在动。
     */
    TransformSpec transform{};

    /**
     * 描边与阴影（0.2.0 T-07）：**放在状态里、跟着开合过渡插值**——抽屉拉出时
     * 描边 / 阴影一起动（它们画的每一笔都用当前插值出来的显示变换）。
     *
     * 层次（这是 T-07 的核心决策，别改回"容器自绘一笔"）：
     *   - **阴影**画在填充**之下**（它就是"形状之下的偏移副本"），与填充同一趟；
     *   - **描边**画在**子孙之上**（装饰遍，`onRenderDecoration`）——跟着填充走
     *     会被子节点盖住，加完等于没加。
     * 两者都**不参与命中**（`InkingDecoration.h` 的规矩），也都**不走主题
     * token**（颜色是字面量；语义色先只在 `color` 上做）。
     */
    StrokeSpec stroke{};
    ShadowSpec shadow{};

    PanelAppearance() = default;
    PanelAppearance(std::uint32_t argb, float seconds, Easing curve,
                    const TransformSpec& move)
        : color(argb), transitionSeconds(seconds), easing(curve),
          transform(move) {}

    /// CXXCSS 里 `"0.1|0.1|0.12|0.95"` 那种 0~1 写法。
    static PanelAppearance FromUnitRgba(float r, float g, float b,
                                        float a = 1.0f) {
        // 必须写 inking::——本命名空间里就有同名函数，不加限定会找到自己。
        return PanelAppearance(inking::FromUnitRgba(r, g, b, a), 0.0f,
                               Easing::Linear, TransformSpec{});
    }
};

/**
 * @brief 一个 Panel 的全部静态配置。
 *
 * 两态平铺成两个具名字段（不是数组）：配置里就是两个具名段，
 * 展开之后字段名能直接对上，找起来不用在脑子里做下标映射。
 */
struct PanelData {
    std::string name;  ///< 与 json 文件名、代码里用的名字三处一致

    int width = 0;   ///< 设计坐标
    int height = 0;

    PanelAppearance closed{};  ///< 关着的状态（通常就是"挪出视野之外"）
    PanelAppearance open{};    ///< 开着的状态

    /// 省略 `open` 时**继承 `closed`**（只挪位置、不改颜色的面板很常见）。
    /// 这个收尾由 `Normalize()` 做，不在读取方各写一遍。
    bool openInheritsClosed = true;

    ShapeSpec shape = ShapeSpec::Rect();  ///< 形状层，省略即直角矩形

    AnchorPoint selfAnchor{};   ///< 自身锚点
    AnchorPoint traceAnchor{};  ///< 上级锚点
    float offsetX = 0.0f;  ///< 对齐之后再挪一点，设计坐标
    float offsetY = 0.0f;

    /**
     * 布局（A6）：面板是最典型的容器，所以这两项对它最有用——
     * `layout` 声明"我怎么排孩子"（竖排 / 横排 / 网格 + 间距 + 对齐），
     * `layoutItem` 声明"我在父容器布局里的意愿"（margin / fill / min-max）。
     *
     * ⚠️ 与 `AnchorData` 里同名的那两个是同一份语义：组件构造时会逐字段搬进
     * 基类，往这里加字段时别忘了那一处（`AGENTS.md` §6 第 83 条）。
     */
    LayoutSpec layout;
    LayoutItem layoutItem;

    int zIndex = 0;      ///< 越大越靠上
    bool visible = true;  ///< 自己这一层的可见性

    /**
     * 构造时就是开着的吗。
     *
     * 放在数据里而不是"构造完再调一次 SetOpen(true)"：那样会先按关闭态起一段
     * 过渡再立刻打断，配置驱动那条路（A2 的 json）也没法表达初始状态。
     */
    bool startOpen = false;

    /**
     * 要不要把**子孙**超出本体的部分剪掉（A5）。
     *
     * 默认 false：面板也常用来装阴影 / tooltip 这类**故意溢出**的东西，
     * 默认打开就会"悄悄少画一块"——那种错很难往裁剪上想。所以它必须显式声明，
     * 而声明之后绘制与命中会一起生效（`HasSubtreeClip` + `HitTest`）。
     *
     * 注意裁剪区就是**本体矩形**，且只剪子孙：面板自己那一笔不受影响。
     */
    bool clipChildren = false;

    /// B12：滚动容器。没配 `"scroll"` 段就是普通容器（`enabled == false`）。
    ///
    /// 它是**容器的一种能力**（和 `clipChildren` 同一族），不是新组件类型：
    /// 配了就能滚。滚动的几何规则全在 `InkingScroll.h` 那几个纯函数里
    /// （轨道 / 滑块 / 点轨道跳过去），这里只放配置。
    ScrollSpec scroll;

    /// 收尾：把"省略即继承"的默认值补齐。幂等。
    void Normalize() {
        if (openInheritsClosed) {
            // 颜色跟着走，但**过渡与变换不继承**：open 的 transform 就是
            // "开到位的目标"，继承 closed 那份等于"开了跟没开一样"；
            // 时长与曲线更是必须各配各的（D-1「过场段各自带一份」）。
            open.color = closed.color;
            // token 也要跟着走：只抄颜色的话，换主题时"开着的面板"还用旧色
            // ——而且不报错，只是那一块颜色不跟着变。
            open.themeColor = closed.themeColor;
            // 描边 / 阴影同样继承（T-07）：省略 open 的面板不该"关上时有描边、
            // 拉开就没了"。过渡与变换仍然**不继承**（见上面那段理由）。
            open.stroke = closed.stroke;
            open.shadow = closed.shadow;
        }
        // 两个状态都是纯透明的话，这个面板在任何情况下都看不见——那多半是配置
        // 写漏了（例如颜色串写成了 "0|0|0"）。给个不透明的**品红**兜底，至少看得见。
        // 与 `ButtonData::Normalize` 同一条规矩（`AGENTS.md` §6 第 52 条：
        // 真想做全透明热区就别用组件，或者给一个极小但非 0 的 alpha）。
        //
        // 0.1.1 T-03 改了两处：颜色 0xFF000000 → 0xFFFF00FF，以及**补一条 WARN**。
        // 理由与按钮那一处相同（深色界面上纯黑和正常底色无法区分 ⇒ 兜底等于没兜），
        // 而两条规矩必须一致：否则同一个错误在按钮上是黑、在面板上是品红，更难看懂。
        //
        // 0.2.0 T-07 加了一条豁免：**有可见描边或阴影的面板不兜底**——"透明底 +
        // 一圈描边"是正当配置（空心卡片），兜底成品红反而把它毁了。
        if (ColorAlpha(closed.color) == 0u && ColorAlpha(open.color) == 0u
            && !closed.stroke.Visible() && !open.stroke.Visible()
            && !closed.shadow.Visible() && !open.shadow.Visible()) {
            closed.color = 0xFFFF00FFu;
            open.color = 0xFFFF00FFu;
            // 兜底色不再是语义色了：token 必须清掉，否则换主题时它又会把
            // 这个兜底色盖回去（"配了个透明面板，换肤后突然不透明了"）。
            closed.themeColor = nullptr;
            open.themeColor = nullptr;
            INK_LOG_WARN(kPanelModuleName,
                         "两态颜色全透明（alpha 都是 0），已兜底成品红：" + name
                             + "。真要一个看不见的容器请给一个极小非零 alpha"
                               "（1/255），别用 0");
        }
    }
};

}  // namespace inking
