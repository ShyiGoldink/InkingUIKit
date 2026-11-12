#pragma once

// "按形状画一笔 + 判一下命中"的实现：**静态按钮、动态按钮、以及静态图片（A11）
// 共用**。
//
// 为什么升成公开头（A11）：图片组件要的正是"形状 + 外观（颜色 / 纹理）+ 描边阴影"
// 这一笔——它和按钮的差别只在"没有三态、没有点击"，绘制一个字都不该重写。
// 放在 `src/button/` 里时它只能被同目录的 .cpp 拿到，图片组件要么抄一份、
// 要么写 `../button/...` 这种跨目录相对包含——两条都不好。
//
// 它**不拖 SDL 进来**（SDL 的类型只在 `ButtonPaint.cpp` 那一侧出现），所以
// 放在 `include/` 里符合公开头的纪律（`AGENTS.md` §3 第 2 条）。
//
// 为什么不能各写一份：形状填充、文字占位块、以及**带变换的命中判定**是
// "按钮 / 图片看起来是什么样、点得中哪儿"的唯一来源，两份实现各自演化就会出现
// "按钮和图片画得不一样"——而这种不一致两边单独看都对，合起来才错。
//
// 绘制与命中放在同一个文件里是**刻意的**：它们必须共用同一份形状定义
// （`InkingShapeContains`）与同一份变换定义（`InkingTransform.h` 的正/逆变换），
// 而那两份东西的正确性恰恰取决于"正变换与逆变换严格互逆"。

#include <button/ButtonData.h>
// 只要**定位层**：这两个绘制助手用的是 `GetMagnification` / `GetWidth` /
// `GetHeight`，全在 `InkingAnchor` 上。锚点层不认识树，绘制助手也不该认识。
#include <anchor/InkingAnchor.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTransform.h>

#include <cstdint>

// 前向声明就够（与 `InkingWidget.h` 同一形态）：`PaintSurface` 一族把渲染器当
// 形参传，但本头**不拖 SDL 进来**（见顶上那段纪律）。
struct SDL_Renderer;

namespace inking_detail {

/**
 * 「形状表面」三笔的公共件（0.2.0 T-07）：**阴影 → 填充 → 描边**。
 *
 * 为什么抽出来：这三笔原来内联在 `PaintButtonAppearance` 里，面板要描边 /
 * 阴影时只能抄第二份——而两份"形状怎么变成像素"各自演化就会分叉
 * （按钮与面板画得不一样，两边单独看都对、合起来才错）。现在**只有这一份**：
 *
 *   - `PaintSurface`：三笔合一（阴影 → 按颜色填充 → 描边），面板与按钮的
 *     纯颜色路径都走它；
 *   - 三个单笔（`PaintSurfaceShadow` / `PaintSurfaceFill` / `PaintSurfaceStroke`）：
 *     给"中间那笔不是纯色填充"的调用方用——按钮的 `img` 外观把填充换成纹理，
 *     但**顺序不变**（阴影仍在最下、描边仍在最上）；
 *   - 面板的描边要画在**子孙之上**（装饰遍，`InkingWidget::onRenderDecoration`），
 *     它只调 `PaintSurfaceStroke`；阴影留在填充那一趟——它是"形状之下的偏移
 *     副本"，跟着装饰遍走反而会盖住内容。
 *
 * 三笔都**不参与命中**（`InkingDecoration.h` 的规矩）。`pixelX` / `pixelY`
 * 是设备像素（与 `PaintButtonAppearance` 同一约定）。
 */
void PaintSurfaceShadow(SDL_Renderer* renderer, const InkingAnchor& node,
                        const InkingShapeSpec& shape, const InkingShadowSpec& shadow,
                        const InkingTransformSpec& transform, float pixelX,
                        float pixelY);
void PaintSurfaceFill(SDL_Renderer* renderer, const InkingAnchor& node,
                      const InkingShapeSpec& shape, std::uint32_t fillColor,
                      const InkingTransformSpec& transform, float pixelX,
                      float pixelY);
void PaintSurfaceStroke(SDL_Renderer* renderer, const InkingAnchor& node,
                        const InkingShapeSpec& shape, const InkingStrokeSpec& stroke,
                        const InkingTransformSpec& transform, float pixelX,
                        float pixelY);
void PaintSurface(SDL_Renderer* renderer, const InkingAnchor& node,
                  const InkingShapeSpec& shape, const InkingStrokeSpec& stroke,
                  const InkingShadowSpec& shadow, std::uint32_t fillColor,
                  const InkingTransformSpec& transform, float pixelX, float pixelY);

/**
 * 画这个组件**这一帧看起来的样子**（设备像素）——按当前状态的**外观类型**分派：
 *
 *   - `color` / `svg`：按形状填一笔（`inking_detail::fillShape`）。形状是唯一真相：
 *     几何取值与命中用的 `InkingShapeContains` 同一份定义，所以"画的是圆角、点到的是
 *     直角"这条已知不一致不存在。`svg` 外观的几何在**生成期**就被转成了形状
 *     （`CXXCSS.md` §4.1），运行期和 color 走同一条路，只差填充色；
 *   - `img`：画一张纹理铺满组件框（`inking_detail::PaintImage`，T-4）。图片解不出来
 *     （没接 SDL3_image / 字节坏了）时**退回按颜色填**，不会变成一个洞。
 *
 * `displayColor` 是**插值之后**的显示色（过渡期间是中间色），它的 alpha 也
 * 当作图片的淡入淡出用。静态图片（A11）没有过渡，直接给 `normal` 那份颜色。
 *
 * `transform` 是**变换通道**（平移 / 旋转 / 缩放）：只改这一笔的位置与朝向，
 * 不改形状与尺寸，也不改命中表。
 *
 * **绘制顺序**（B3）：阴影 → 填充（或图片）→ 描边。三笔都是"形状的副本"，
 * 顺序反了就会被自己盖住（阴影盖住填充、描边被填充盖住）。它们**都不参与命中**。
 */
void PaintButtonAppearance(const InkingAnchor& node, const InkingShapeSpec& shape,
                           const InkingButtonAppearance& appearance,
                           const InkingStrokeSpec& stroke, const InkingShadowSpec& shadow,
                           std::uint32_t displayColor,
                           const InkingTransformSpec& transform, float pixelX,
                           float pixelY, bool edgeFeather = true);

/**
 * 组件的**保守命中包络**（本地坐标，相对左上角）。
 *
 * 为什么要专门算它：按钮的变换会随三态变（悬停平移+放大、按下下沉），
 * 而命中表的格子登记**冻结**（§11「变换通道不改表」）——所以登记时用的必须是
 * "三个状态盖住的全部范围"，精判才用当前变换。**只按当前变换登记会漏命中**：
 * 悬停那一帧按钮挪出去一点，边缘的点就落在表外了。
 *
 * 静态 / 动态按钮共用这一份：洞（动态组件占地）与表条目（静态组件）要的是
 * 同一件事——"它可能盖住哪儿"。**静态图片（A11）也走它**：图片只有一态，
 * 于是这份"三态包络"自然退化成"那一态 + 它的变换"，没有第二条规则。
 */
InkingShapeBounds ButtonHitEnvelope(const InkingButtonData& data, const InkingShapeSpec& shape,
                              int width, int height);

/**
 * @brief 按钮文字"这一帧画多少"的结果（1.3.0 T-04）。
 *
 * `painted` 是要交给 `PaintText` 的那串（截断后的前缀，`Ellipsis` 时末尾还有
 * 一个 `…`）；`width` / `height` 是它量出来的尺寸——**已经有了，不要再量一次**
 * （算对齐偏移要用它，而偏移必须是"截断后"的宽度）。
 */
struct InkingButtonTextCut {
    std::string painted;   ///< 真正要画的串（空 = 什么都不画）
    float width = 0.0f;    ///< `painted` 的宽度（设计坐标）
    float height = 0.0f;   ///< `painted` 的高度（设计坐标）
    bool truncated = false;  ///< 真的被截了吗（悬停提示的判据）
};

/**
 * @brief 截断结果缓存：文字 / 可用宽 / 字体 / 策略任何一个变了才重算。
 *
 * 为什么必须有（不是优化）：改造前默认路径（`align = Left` / `Top`）**零测量**，
 * 改造后至少要量一次才知道"超没超"。`PaintButtonText` 每帧调用，不缓存就是
 * 每帧每个按钮一次 `TTF_SizeUTF8`。缓存之后稳态 0 次，只有 `SetText` / 尺寸变化
 * / 换策略时才重算一次。
 *
 * 放在**按钮实例**里而不是 `InkingButtonData` 里：后者是配置，不该被运行期改写。
 * 不用的调用方（动态按钮 / `ListRow`）传 `nullptr` 即可，行为完全一样。
 */
struct InkingButtonTextCutCache {
    bool valid = false;
    std::string label;      ///< 键：源串
    std::string fontPath;   ///< 键：首选字体
    std::vector<std::string> fontPaths;  ///< 键：回退链
    float fontSize = 0.0f;               ///< 键：字号
    float available = 0.0f;              ///< 键：可用宽（含右边距变化）
    InkingTextOverflow overflow = InkingTextOverflow::Clip;  ///< 键：策略
    InkingButtonTextCut cut;             ///< 值
};

/**
 * @brief 按可用宽裁出"这一帧要画的字"（1.3.0 T-04）。**纯查询，不改配置**。
 *
 * 可用宽 = `GetWidth() - leftSpace - 右边距`，其中右边距 = `rightSpace > 0 ?
 * rightSpace : leftSpace`（0 表示对称）。放得下就**原样返回**（不算截断，这条是
 * 老行为的回归基线）；放不下才调 `InkingCutTextToWidth` 回退（`Ellipsis` 时预算里
 * 先减掉 `…` 的宽，所以"加完省略号也不溢出"）。
 *
 * 同一个函数也给"要不要弹提示"用——**判据只有一份**：谁想再判一次"文字是否超宽"，
 * 就是第二份真相。
 */
InkingButtonTextCut ResolveButtonTextCut(const InkingAnchor& node,
                                         const InkingButtonData& data,
                                         InkingButtonTextCutCache* cache = nullptr);

/**
 * 画按钮上的文字——**真文字了**（`TaskGuide.md` T-3 的纹理层落地）。
 *
 * 这里只做"配置 → 一次 `InkingTextRequest`"的翻译，真正的栅格化 / 缓存 / 上色 /
 * 跟着变换走都在 `include/render/InkingText.h` 那一层：
 *   - 文字块的位置就是配置里的 `leftSpace` / `topSpace`（设计坐标，相对
 *     组件左上角）；**宽度不再估算**，纹理层按真实字形量；
 *   - 传进来的 `transform` 与形状那一笔用的是**同一个**（绕组件中心），
 *     所以按钮转了、字跟着转——这是之前用 `SDL_RenderFillRect` 画方块时
 *     做不到的那条已知限制。
 *
 * 1.3.0 起先按可用宽把文字**截到放得下**（`ResolveButtonTextCut`），再量、再算
 * 偏移、再画——顺序不能反，否则 `Center` / `Right` 会按原来更宽的尺寸算偏移，
 * 文字整体挪位。`cache` 非空时复用上一次的截断结果（每帧 0 次测量）。
 *
 * 静态图片（A11）不画文字——那是 `Label` 的事。
 */
void PaintButtonText(const InkingAnchor& node, const InkingButtonData& data,
                     const InkingTransformSpec& transform, float pixelX, float pixelY,
                     InkingButtonTextCutCache* cache = nullptr);

/**
 * @brief 文字块左上角在组件里的位置（**设计坐标，相对组件左上角**）。纯函数。
 *
 * 分两条路：
 *   - `text.align == Left` 且 `text.alignV == Top`（默认）：直接给
 *     `leftSpace` / `topSpace`——**老行为，一个字节都不变**；
 *   - 否则**量一遍文字块**（`InkingMeasureText`，不依赖渲染器：拿不到字体时它给估算值），
 *     再按 `(组件尺寸 − 文字尺寸)` 算居中 / 右 / 贴底。
 *
 * 抽成函数是因为**两处要用**：绘制（`PaintButtonText`）与"文字到底摆在哪"的
 * 数值自检。抄第二遍就会分叉——"自检算的居中"和"画出来的居中"不一致，是那种
 * 两边单独看都对、合起来才错的 bug。
 *
 * 四参数的那个重载收**已经量好的文字尺寸**：绘制路径必须先截断再算偏移，
 * 用原始串的宽度去居中会让截断后的文字整体左移（T-04）。
 */
InkingPoint2 ButtonTextOffset(const InkingAnchor& node, const InkingButtonData& data);

InkingPoint2 ButtonTextOffset(const InkingAnchor& node, const InkingButtonData& data,
                              float textWidth, float textHeight);

}  // namespace inking_detail
