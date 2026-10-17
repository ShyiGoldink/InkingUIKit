#pragma once

// Button 的**纯数据**定义，对应 CXXCSS/Button/<名字>.json 的全部字段。
//
// 为什么数据与组件分开：
//   - 这份结构没有需要维护的不变量，就是个"配置长什么样"的记录 → struct；
//   - 代码生成器将来生成的就是**填这个结构的代码**，它跟"按钮怎么响应鼠标"
//     一点关系都没有；
//   - InkingStaticButton 只持有它、不解释它，于是"字段名对不上配置"
//     这类错最后全都收在 .cpp 里那几行映射上（见 InkingStaticButton.cpp）。
//
// 字段名与 CXXCSS.md / button.example.json 一一对应，不在这里改名：
// 名字对不上时的排查成本，比"换个更漂亮的命名"值钱得多。

// 只要**数据**那一半（锚点与布局），不要树层：这里是个纯数据头，
// 拖 `widget/InkingWidget.h` 进来会把"组件配置"和"组件树"绑在一起。
#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingColor.h>
#include <dataStruct/InkingDecoration.h>
#include <dataStruct/InkingEasing.h>
#include <dataStruct/InkingImageFit.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTextLayout.h>
#include <dataStruct/InkingTransform.h>

#include <cstdint>
#include <cstddef>
#include <string>

namespace inking {

/// 模块名常量：日志和自检要用（约定：每个模块都暴露一个）。
inline constexpr const char* kButtonModuleName = "InkingStaticButton";

/// 三态外观的类型，对应配置里的 `"type": "color" | "img" | "svg"`。
enum class AppearanceType : std::uint8_t {
    Color = 0,  ///< source 是 `"r|g|b|a"`（0~1 浮点）
    Image,      ///< source 是图像绝对路径，编译期转成二进制纹理
    Svg,        ///< source 是 SVG 代码
};

/**
 * @brief 一种状态下的外观。
 *
 * `color` 是 `source` 解析后的结果，渲染直接用这个字段，不在每帧去解析字符串。
 *
 * **Image 已经落地**：`ButtonPaint` 用 `detail::PaintImage` 把图像按形状裁剪后
 * 画进按钮（见 `src/button/ButtonPaint.cpp`）。
 * **Svg 还没落地**：没有任何画法，配置里写成 `svg` 会**静默退回纯色 `color`**
 * ——不报错、只是看不到图。类型与字段先留着：配置里能写的东西，运行期的数据
 * 结构就得能表达，否则生成器一落地就要改接口。
 */
struct ButtonAppearance {
    AppearanceType type = AppearanceType::Color;
    std::uint32_t color = 0xFF000000u;  ///< type == Color 时有效，0xAARRGGBB
    std::string source;                 ///< 原样的配置串，留给纹理层用

    /**
     * `type == Image` 时的**图像字节**（T-4）。
     *
     * 这里只是**借一个视图**，不拥有：真正的存储是生成代码里那个静态数组
     * （`static const unsigned char kXxxImage[] = {...};`），生命周期是**整个进程**
     * ——比任何按钮都长，所以不用拷贝、也不用管释放。
     *
     * 尺寸上限由生成器把关（见 `CXXCSS/CXXCSS.md` §4.1：超过上限构建期就报错），
     * 因为把二进制烘进源码会让编译时间随字节数线性上涨。
     */
    const std::uint8_t* imageBytes = nullptr;
    std::size_t imageSize = 0;

    /**
     * `type == Image` 时图在组件框里怎么摆（配置里 `"fit"`，缺省 `fill`）。
     *
     * 三个值：`fill`（拉伸铺满，**保持 B1 之前的行为**）、`contain`（完整放进框、
     * 居中，留白处不画）、`cover`（铺满框、居中，超出的一边被框裁掉）。
     * 三种模式都**按组件形状裁剪**：圆角按钮配图片时四个角跟着圆角一起被剪。
     *
     * 几何计算在 `ImageFitSpec`（纯函数），绘制在 `detail::fillShapeTextured`。
     */
    ImageFit imageFit = ImageFit::Fill;

    /**
     * **进入这个状态时**的过渡时长（秒）。0 = 瞬间切换（配置里没写 `transition`）。
     *
     * 写在状态自己身上，是因为"过场段各自带时长"（`TaskGuide.md` 的 D-1）：
     * normal→hover 用 hover 这份，hover→normal 用 normal 那份，各配各的。
     *
     * 过渡**只插值颜色与透明度，绝不碰几何**——那是按钮能继续待在静态档
     * （几何不变 → 进命中表）的前提。运行期怎么插值见 `ButtonLook::Advance`。
     */
    float transitionSeconds = 0.0f;

    /**
     * 这段过渡用哪条**缓动曲线**（配置里 `transition.easing`，缺省 `linear`）。
     *
     * 和 `transitionSeconds` 并排放着，理由一样："过场段各自带时长"在这里
     * 精确成"各自带时长**与曲线**"——normal→hover 用 hover 这份，反过来用
     * normal 那份。换曲线不改时长：它只把线性进度过一遍曲线
     * （`include/dataStruct/InkingEasing.h`），时长、起点、终点都不动。
     *
     * 时长是 0（没配过渡）时这条曲线不生效——生成期会把那种配置拦下来。
     */
    Easing easing = Easing::Linear;

    /**
     * 这个状态下的**目标变换**（平移 / 旋转 / 缩放），缺省是单位变换。
     *
     * 它属于 InputDesign.md §11 的**变换通道**：不改本地几何、**也不改命中表**——
     * 表按本地形状烘，查询时把点反变换回本地空间。所以**静态组件也能有它**，
     * 命中照样忠实（画的时候顶点过正变换，点的时候过一次逆变换，两者恒等）。
     *
     * 与 `transitionSeconds` 配合：进这个状态时，颜色与变换**一起**按同一段时长插过去。
     *
     * 数据上放在"外观"里是刻意的：配置里它就写在同一个状态对象里
     * （`"hover": { ..., "transform": {...} }`），`ButtonLook::For(state)` 一次就能拿到
     * "这个状态长什么样、摆在哪儿"。要是拆成另一组三态字段，两边就得同步维护了。
     */
    TransformSpec transform{};

    /**
     * 这个状态下的**尺寸**（T-5 的布局通道）。0 = 用组件自己的 `width`/`height`。
     *
     * 和 `transform` 的区别：变换只改"画在哪儿 / 画多大"，**不改本地几何**；
     * 这里改的就是本地几何本身（`ShapeContains` 与 `GetShapeBounds` 都吃它），
     * 所以"命中区域跟着变"是应该的、也是自检要钉住的。
     *
     * 三条约定（`CXXCSS.md` §4.1.3）：
     *   - **从左上角生长**：锚点与偏移一个字节都不动，尺寸变了就从左上角往外长。
     *     要"以中心变大"请用 `transform.scale`（变换通道，T-2 就有）；
     *   - **不进任何写入口**：它来自构造期烘好的有限集合（三态各一份），由状态机
     *     切换——所以组件仍然是**静态档**（`src/inking.cpp` 的编译期断言继续成立）；
     *   - **命中表不用重烘**：登记的是"三态包络"（`ButtonHitEnvelope` 本来就把
     *     三个状态并起来），任何中间尺寸都落在这份包络里；精判用**当前显示尺寸**。
     *     这正是这一轮**不做**"每态一份表"的原因。
     */
    float width = 0.0f;
    float height = 0.0f;

    ButtonAppearance() = default;
    ButtonAppearance(AppearanceType appearanceType, std::uint32_t argb)
        : type(appearanceType), color(argb) {}

    /// CXXCSS 里 `"0|0|0|0.75"` 那种 0~1 写法。
    static ButtonAppearance FromUnitRgba(float r, float g, float b,
                                         float a = 1.0f) {
        // 必须写 inking::——本类里有个同名静态函数，不加限定会找到自己。
        return ButtonAppearance(AppearanceType::Color,
                                inking::FromUnitRgba(r, g, b, a));
    }

    bool IsColor() const noexcept { return type == AppearanceType::Color; }
};

/// 文字段（配置里的 `text`）。`path` 为空表示用机器默认字体。
struct ButtonTextData {
    float fontSize = 0.0f;   ///< 设计坐标的字号
    float leftSpace = 0.0f;  ///< 距左侧，设计坐标（`align == Left` 时用）
    float topSpace = 0.0f;   ///< 距顶部，设计坐标（`alignV == Top` 时用）
    std::string path;        ///< 字体绝对路径；空 = 机器默认字体

    /**
     * @brief 文字块在按钮里**怎么摆**（配置里的 `"align"` / `"alignV"`）。
     *
     * 默认 `Left` / `Top` = 老行为：文字左上角就在 `(leftSpace, topSpace)`——
     * 一个字都不改，所以既有配置与产物完全不受影响。
     *
     * 另外两个值时**先量文字块的真实尺寸**（`MeasureText`），再按它算偏移：
     *   - `align == Center`：水平居中（忽略 `leftSpace`）；
     *   - `align == Right`：右对齐（`leftSpace` 当右边距用）；
     *   - `alignV == Middle` / `Bottom`：垂直居中 / 贴底（忽略 `topSpace`）。
     *
     * 为什么需要它：按钮的文字以前**只能**按固定偏移贴左上角（`leftSpace` /
     * `topSpace` 是"距左 / 距上多少"），于是"菜单项文字居中"这种最基本的需求要靠
     * 手填一个估算值——比例字体下必然歪。量一遍文字再算，是唯一不用猜的做法。
     * 复用的是标签那套现成枚举（`TextAlignH` / `TextAlignV`），不新增词汇。
     */
    TextAlignH align = TextAlignH::Left;
    TextAlignV alignV = TextAlignV::Top;
};

/**
 * @brief 一个 Button 的全部静态配置。
 *
 * 三态外观（normal / hover / onclicked）按配置写法平铺成三个字段，
 * 而不是塞一个 `ButtonAppearance[3]`：配置里就是三个具名段，
 * 展开之后字段名能直接对上，找起来不用在脑子里做下标映射。
 */
struct ButtonData {
    std::string name;  ///< 与 json 文件名、代码里用的名字三处一致

    /// 设计坐标尺寸。配置里历史上有 `"200"` 这种字符串写法，
    /// 那是文件格式的事，生成器两种都接受；运行期一律是 int。
    int width = 0;
    int height = 0;

    ButtonAppearance normal{};     ///< 通常状态
    ButtonAppearance hover{};      ///< 悬停
    ButtonAppearance onclicked{};  ///< 按下

    /// 省略 hover / onclicked 时**继承 normal**。
    /// 这个收尾工作由 Normalize() 做，不在读取方那边各写一遍。
    bool hoverInheritsNormal = true;
    bool onclickedInheritsNormal = true;

    ButtonTextData text{};
    std::string label;  ///< text.content.normal 的运行时形态（当前按它画）

    /// 按钮文字的颜色。**配置里没有这个字段**（json 的 text 段没定义颜色），
    /// 这里按"浅色文字"给了个默认值，是为了让文字真的看得见。
    /// 等 text 段定稿，这个字段要么被配置覆盖，要么被删掉。
    std::uint32_t textColor = 0xFFFFFFFFu;

    AnchorPoint selfAnchor{};   ///< 自身锚点
    AnchorPoint traceAnchor{};  ///< 上级锚点
    float offsetX = 0.0f;  ///< 对齐之后再挪一点，设计坐标
    float offsetY = 0.0f;

    /**
     * 布局（A6）：`layout` = 我是容器时怎么排孩子；`layoutItem` = 我在父容器
     * 布局里的意愿。两个都默认空，所以不带布局的按钮一行配置都不用改。
     *
     * ⚠️ 这两个字段与 `AnchorData` 里同名的那两个是**同一份语义**：组件构造时
     * 会逐字段把它们拷进基类（和 `selfAnchor` / `offsetX` 那几行走同一条路）。
     * 往这里加字段时别忘了那一处（`AGENTS.md` §6 第 83 条）。
     */
    LayoutSpec layout;
    LayoutItem layoutItem;

    int zIndex = 0;          ///< 越大越靠上
    bool visible = true;     ///< 自己这一层的可见性

    ShapeSpec shape = ShapeSpec::Rect();  ///< 形状层，省略即直角矩形

    /**
     * 描边与阴影（B3）：与 `shape` **平级**的两笔装饰（`CXXCSS.md` §3.5）。
     *
     * 它们**不进形状参数**：形状只管几何，描边/阴影画在几何之上/之下。
     * 命中也不受它们影响——命中判的是形状本体（`ShapeContains`）。
     * 两者都是**组件级**（三态共用），要"悬停加亮描边"得等真有需求再谈。
     */
    StrokeSpec stroke;
    ShadowSpec shadow;

    /**
     * 收尾：把"省略即继承"的默认值补齐。
     *
     * 必须在一个地方做，不能在渲染、命中、自检各判一次——那样三处的
     * 判断迟早会分叉。生成器生成的代码填完字段后调一次这个方法就行。
     * 幂等：重复调用结果不变。
     */
    void Normalize() {
        if (hoverInheritsNormal) {
            hover = normal;
        }
        if (onclickedInheritsNormal) {
            onclicked = normal;
        }
        // 三态都是纯透明的话，这个按钮在任何情况下都看不见——那多半是配置写漏了
        // （例如 source 写成了 "0|0|0"）。给个不透明的黑兜底，至少看得见。
        if (normal.IsColor() && ColorAlpha(normal.color) == 0u
            && ColorAlpha(hover.color) == 0u
            && ColorAlpha(onclicked.color) == 0u) {
            normal.color = 0xFF000000u;
            hover.color = 0xFF000000u;
            onclicked.color = 0xFF000000u;
        }
    }

    /// 有文字内容要画吗（当前只认 label）。
    bool HasLabel() const noexcept { return !label.empty(); }
};

}  // namespace inking
