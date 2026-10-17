#pragma once

#include <image/ImageData.h>
#include <widget/InkingStaticWidget.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inking {

/**
 * @brief 一张**静态图片**（A11）：不带点击回调的按钮。
 *
 * 抽出来的理由（`TaskGuide.md` 的 A11 原话）：img 的逻辑就是一个"没有写入
 * onClicked 事件的 button"——形状、外观（颜色 / 纹理）、描边阴影、锚点、命中
 * 全都要，只有三态与回调不要。所以它：
 *
 *   - **复用按钮那条绘制**（`detail::PaintButtonAppearance`，见
 *     `include/button/ButtonPaint.h`）：形状填充 / 图片贴图 / 描边阴影三笔的
 *     顺序与实现一个字都不重写——两份实现各自演化就会出现"按钮和图片画得不一样"；
 *   - **只认 `normal` 一态**：没有状态机、没有过渡，`GetDisplayColor()` /
 *     `GetDisplayTransform()` 直接给 `normal` 那份；
 *   - **不吃输入、也没有回调**：`onPointer*` 一个都不重写（基类的空实现就够），
 *     所以它不会被按下、不会悬停变样、也拿不到键盘焦点；
 *   - **参与命中**：指针落在它这一格就命中它（和标签的"默认穿透"是两回事——
 *     图片是画面上的实体，不该把点击漏给底下）。
 *
 * ---------------------------------------------------------------------------
 * 两种"图从哪来"（A11 的核心）
 *
 *   1. **生成期烘**（`runtimeAsset == false`）：`normal.source` 是绝对路径，
 *      生成器把文件字节烘成 `static const unsigned char kXxx[]`，运行期只借视图。
 *      好处是"exe 自带图、不会缺文件"；坏处是图一多 exe 就肥、编译也慢。
 *   2. **运行期读**（`runtimeAsset == true`）：`normal.source` 是**相对资源根**的
 *      路径（默认 exe 旁的 `assets/`），构造时（或 `LoadAsset` 时）由
 *      `inking::AssetLoad` 读进来，本类**自己持有**那份字节（`std::vector`），
 *      并把视图指给绘制层。资源模块还认一种轻量混淆头（`INKA` + 异或流），
 *      所以"资源包里直接拖出来就能用"这件事被挡住了（见 `asset/InkingAsset.h`）。
 *
 * 读不到图时**保留上一次的图**（首次则退回按颜色填）：把画面变成一个洞比
 * "缺图"更糟，而且那种失败会一路传到用户面前却说不清是哪一步。
 */
class InkingStaticImage : public InkingStaticWidget {
public:
    /**
     * 手写数据构造：不走配置，直接把一份 `ImageData` 交给它。
     *
     * @param parent 上级锚点；非拥有。图片必须在某个场景的子树下才能被渲染。
     * @param data   配置数据（构造时会 `Normalize` 一次；`runtimeAsset` 为真时
     *               会**立刻**从资源根读一次图）。
     */
    InkingStaticImage(InkingWidget* parent, const ImageData& data);

    /// 指针在这个位置上吗（世界坐标）。**照常参与命中**：它不是标签。
    bool HitTest(float worldX, float worldY) const override;

    /// 形状定义：渲染 / 命中 / 包围盒三者都从它派生（见 InkingShapeSpec.h）。
    const ShapeSpec& GetShape() const noexcept;

    /// 当前外观（图片只有一态，所以恒等于配置里的 `normal`）。
    const ButtonAppearance& GetAppearance() const noexcept;

    /// 画出来的颜色（0xAARRGGBB）：就是 `normal` 那份。
    std::uint32_t GetDisplayColor() const noexcept;

    /// 生效的变换：**外部显示变换 ∘ `normal` 那份**（A13）。
    ///
    /// 返回值而不是引用：A13 之后它可能是个**合成结果**（外部拖动叠在配置那份
    /// 之上），不再指向 `_data` 里的字段。绘制与命中**都必须读它**——两处各读
    /// 各的数据就是"画的是一个、点的是另一个"（`AGENTS.md` §6 第 38 条）。
    TransformSpec GetDisplayTransform() const noexcept;

    /// 变换**同时作用于子节点**（和按钮 / 面板同一条约定）。
    TransformSpec GetSubtreeTransform() const noexcept override;

    /// 图片认**外部显示变换**（A13）：拖动 / 缩放图片要靠它（框架默认不认）。
    bool AcceptsExternalTransform() const noexcept override { return true; }

    /// 命中索引用的保守包络：一态包络 **× 外部变换**（拖走了也要点得到）。
    ShapeBounds GetHitEnvelope() const override;

    /// 构造时用的那份配置（已经 Normalize 过）。
    const ImageData& GetData() const noexcept;

    /// 现在这张图的名字（**纹理缓存键** / 日志用）。没有图时是空串。
    const std::string& ImageName() const noexcept;

    /// 运行期读进来的字节数（0 = 没走那条路，或者没读到）。
    std::size_t AssetByteCount() const noexcept;

    /// 现在用的是不是运行期读进来的字节（而不是生成期烘的那份）。
    bool UsingAssetBytes() const noexcept;

    /// 这个组件**自己**加载过几次（全局计数在 `AssetStatsSnapshot()`）。
    int AssetLoadCount() const noexcept;

    // ---------------- 拖拽 / 缩放（A13） ----------------

    /**
     * 允许按住图片**拖动**它（默认关）。
     *
     * 拖动改的是**外部显示变换**的平移，本地几何一个字节都不动——所以命中跟着
     * 一起走（走 `SetDisplayTransform` 那条通道），而且**不需要**这里额外标脏。
     */
    void SetDraggable(bool draggable) noexcept;

    /**
     * 允许拖**右下角把手**缩放（默认关）。
     *
     * 缩放绕**左上角**（对角固定，手感才是"抓住一个角往外拉"），均匀缩放并夹在
     * `[0.2, 5]` 之间。两个开关都开时：按在下图右下角那块 = 缩放，其余地方 = 移动。
     */
    void SetResizable(bool resizable) noexcept;

    /// 拖动开关（见 `SetDraggable`）。
    bool IsDraggable() const noexcept;

    /// 缩放开关（见 `SetResizable`）。
    bool IsResizable() const noexcept;

    /// 现在是不是正被抓着拖 / 缩（观测口，自检用）。
    bool IsDragging() const noexcept;

    /// 这次交互被判成**缩放**了吗（**按下那一刻**就定了；观测口，自检用）。
    bool IsScalingDrag() const noexcept;

    /**
     * 运行期换图：从资源根读一个相对路径。
     *
     * @return 读到了才返回 true；读不到**保留旧图**（不把画面变成洞）。
     *
     * 它会标**重绘脏**（画面变了），但**不标命中脏**（几何没动）。
     */
    bool LoadAsset(const std::string& relative);

    /**
     * 运行期换图：直接给字节。本类**拷贝一份**自己拥有（`data` 可以为临时缓冲）。
     *
     * `name` 是纹理缓存键：同一张图给同一个名字才会复用纹理（见
     * `ImageBytes::name`）。传空指针 / 0 字节 = 把图清掉，退回按颜色填。
     */
    void SetImageBytes(const std::uint8_t* data, std::size_t size,
                       const std::string& name);

protected:
    /// 按形状画这一笔（颜色 / 图片 + 描边阴影）。**不画文字**——那是 Label 的事。
    void onRender(float pixelX, float pixelY) const override;

    /// 抬手时收尾这次拖动交互（拖动捕获由框架保证：按下即捕获、抬起 / 失焦释放）。
    void onPointerPress(bool press) override;

    /**
     * 拖着走：移动或缩放，两者都落成一次 `SetDisplayTransform`。
     *
     * **第一次**事件里反推按下点（框架对首次拖动的约定：位移是相对**按下点**），
     * 用它判定"抓的是右下角把手还是整块"——之后这次交互的模式就定下了。
     */
    void onPointerDrag(float designX, float designY, float deltaX,
                       float deltaY) override;

private:
    /// 把 data 搬进自己 + 基类；构造函数用。
    void applyData(const ImageData& data);

    /// 把"要画的那份外观"重算一遍（`normal` + 可能的运行期字节视图）。
    void refreshRenderAppearance();

    /**
     * 用**世界坐标**里的一个按下点决定这次交互是"移动"还是"抓右下角缩放"。
     *
     * 判定必须与**命中**同源（`detail::HitTestLocalRect`：撤祖先变换 → 减绝对位置 →
     * 过自己变换的**逆**）。只减绝对位置（不过逆变换）的话，图片一旦被缩放，
     * "哪一块算把手"就和"看到的把手"错位——**缩小时抓把手会被判成移动**
     * （表现就是"右下角缩放好像有延迟 / 抓不住"），放大时图片中部又会被误判成缩放。
     */
    void resolveDragMode(float worldX, float worldY) noexcept;

    ImageData _data{};

    /// 运行期读进来的字节（本类拥有）；空 = 用生成期那份。
    std::vector<std::uint8_t> _assetBytes;

    /// 绘制用外观：`normal` 的副本，必要时把 `imageBytes/imageSize` 指到
    /// `_assetBytes` 上（**不能**指向 `_data.normal`，那是另一份存储）。
    ButtonAppearance _renderAppearance{};

    std::string _imageName;
    int _assetLoadCount = 0;

    // ---- 拖拽 / 缩放（A13）：一次交互的状态 ----
    bool _draggable = false;  ///< 允许整块拖动
    bool _resizable = false;  ///< 允许抓右下角缩放
    bool _dragActive = false;  ///< 这次按下有没有开始拖动（抬起时清）
    bool _dragScaling = false;  ///< 这次交互是缩放（否则是移动）
    TransformSpec _dragStartTransform{};  ///< 按下那一刻的外部变换（缩放的基准）
    /// 按下点的**世界坐标**（首次拖动事件反推：`designX - deltaX`）。
    /// 缩放必须用"相对按下点的**累计**位移"，不能用本次事件的增量——
    /// 增量每次都从 `_dragStartTransform` 重算，除了第一帧以外全被算回原位
    /// （表现是"稍微拉开一点就弹回去、像在对抗缩放"，`AGENTS.md` §6 第 96 条）。
    float _dragPressX = 0.0f;
    float _dragPressY = 0.0f;
};

}  // namespace inking
