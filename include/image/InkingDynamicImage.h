#pragma once

// 动态档图片（G-01）：几何可以在运行期改的 `InkingStaticImage`。
//
// ---------------------------------------------------------------------------
// 与静态版的两条差别（别的都一样，别自己加第三条）
//
//   1. **几何能改**：继承 `InkingDynamicWidget`，于是 `Resize` / `ChangeSelfAnchor` /
//      `ChangeTraceAnchor` / `ChangeOffset` 四个写入口都在；
//   2. **输入靠拉**：动态档收不到 `PushPointer*`（场景对动态节点直接不投递），
//      只有 `onPointerStateChanged()` 这一次通知——所以内置的拖动 / 缩放是在
//      那里自己读场景的指针状态判的（`inking_detail::PointerProbe`）。
//
// 绘制（`inking_detail::PaintButtonAppearance`）、图片加载（`InkingAssetLoad` / 字节拷贝 /
// 纹理缓存键）、命中形状（`inking_detail::HitTestShape`）与静态版**同源**：G-02 起
// 前三者的数据与实现都在 `InkingImageLike`，不再各存一份。
//
// ---------------------------------------------------------------------------
// 内置拖动 / 缩放写的是**几何**，不是 A13
// ---------------------------------------------------------------------------
//
//   静态版的拖动改的是**外部显示变换**（`SetDisplayTransform`）——因为它的几何
//   写不了（`Resize` / `ChangeOffset` 只挂在动态档上，那是"静态几何定死"这条
//   铁律的实现方式）。动态版既然有几何写入口，就把拖动钉在几何上：
//   拖 = `ChangeOffset`，缩 = `Resize`（绕左上角，夹 `[0.2, 5]`）。
//
//   **A13 仍然保留**（`AcceptsExternalTransform()` 恒 true）：外部显示变换与几何
//   是**正交**的两件事（屏幕抖动 / 整体缩放 vs 占位变化），而且两套在绘制与命中处
//   天然复合——`GetDisplayTransform()` 就是"外部变换 ∘ 配置那份"，`PaintButtonAppearance`
//   与 `HitTest` 读的是同一个结果。风险从来不在"两者并存"，而在"**同一个拖动功能**
//   同时写两边"；把拖动钉死在几何上，问题就没了。
//
//   代价照实写：调用方要清楚 `GetOffset()`（几何）与 `GetDisplayTransform()`（显示）
//   回答的是**不同问题**——这正是 `ComponentGuide.md` §1 那套"目标值 / 显示值分开
//   命名"的同一根线。
//
// ---------------------------------------------------------------------------
// 命中表：不进表，占地记成"洞"
// ---------------------------------------------------------------------------
//
//   与 `InkingFixedPopup` 同档：`HitTest` 自己判、自己维护状态，场景只在点落进
//   它的洞里时才来问它。洞过期 = 那一片点不到，所以四个几何入口都走
//   `MarkGeometryMoved()`（T-19）——图片是叶子，于是移动只刷洞、不重建整表。
//   `OwnsSubtreeHit()` 保持 false（图片不当容器）。

#include <detail/InkingPointerProbe.h>
#include <image/ImageLike.h>
#include <widget/InkingDynamicWidget.h>

#include <cstddef>
#include <cstdint>
#include <string>

// G-02：与静态档共用 InkingImageLike；拖 / 缩仍写几何，不收进共用层。
class InkingDynamicImage : public InkingDynamicWidget, public InkingImageLike {
public:
    /**
     * 手写数据构造：不走配置，直接把一份 `InkingImageData` 交给它。
     *
     * @param parent 上级节点；非拥有。图片必须在某个场景的子树下才能被渲染，
     *               而且**必须有场景**：动态档的输入只有"读场景的指针状态"这一条来源。
     * @param data   配置数据（构造时会 `Normalize` 一次；`runtimeAsset` 为真时
     *               会**立刻**从资源根读一次图）。
     */
    InkingDynamicImage(InkingWidget* parent, const InkingImageData& data);

    // 继承来的几何写入口：`Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` /
    // `ChangeOffset`（`InkingDynamicWidget`）。它们内部走 `MarkGeometryMoved()`：
    // 图片是叶子 ⇒ 移动时只刷命中洞，不重建整张表（T-19）。

    // ---------------- 拖拽 / 缩放（写几何） ----------------

    /**
     * 允许按住图片**拖动**它（默认关）。拖动 = `ChangeOffset`。
     *
     * 与静态版同名，但落点不同：静态版改外部显示变换（A13），本类改**几何**
     * （占位跟着走，布局容器也看得见）。外部显示变换仍然可用，两套在绘制与
     * 命中处复合。
     */
    void SetDraggable(bool draggable) noexcept;

    /**
     * 允许拖**右下角把手**缩放（默认关）。缩放 = `Resize`，绕**左上角**，
     * 均匀缩放并夹在 `[0.2, 5]` 之间（相对按下那一刻的尺寸）。
     *
     * 左上角不动是**算出来的**：`selfAnchor` 不是 `(0,0)` 时，尺寸一变左上角会
     * 跟着跑，所以补一段 `ChangeOffset`（`Δoffset = Δsize × selfAnchor`）。
     */
    void SetResizable(bool resizable) noexcept;

    /// 拖动开关（见 `SetDraggable`）。
    bool IsDraggable() const noexcept;

    /// 缩放开关（见 `SetResizable`）。
    bool IsResizable() const noexcept;

    /// 现在是不是正被抓着拖 / 缩（观测口，自检用）。抬手就清。
    bool IsDragging() const noexcept;

    /// 这次交互被判成**缩放**了吗（**按下那一刻**就定了；观测口，自检用）。
    bool IsScalingDrag() const noexcept;

    // ---------------- 与静态版同名同义的观测口 ----------------

    /// 指针在这个位置上吗（世界坐标）。**照常参与命中**：它不是标签。
    bool HitTest(float worldX, float worldY) const override;

    /// 形状定义：渲染 / 命中 / 包围盒三者都从它派生（见 InkingShapeSpec.h）。
    const InkingShapeSpec& GetShape() const noexcept;

    /// 当前外观（图片只有一态，所以恒等于配置里的 `normal`）。
    const InkingButtonAppearance& GetAppearance() const noexcept;

    /// 画出来的颜色（0xAARRGGBB）：就是 `normal` 那份。
    std::uint32_t GetDisplayColor() const noexcept;

    /// 生效的变换：**外部显示变换 ∘ `normal` 那份**（A13）。
    ///
    /// 与几何（`GetOffset` / `GetWidth`）回答的是不同问题：几何是"我占哪儿"，
    /// 这个是"我画在哪儿"。两者在绘制与命中处复合，所以"拖走之后点得到"仍然成立。
    InkingTransformSpec GetDisplayTransform() const noexcept;

    /// 变换**同时作用于子节点**（和按钮 / 面板同一条约定）。
    InkingTransformSpec GetSubtreeTransform() const noexcept override;

    /// 图片认**外部显示变换**（A13）：默认不认，图片要认（整体缩放 / 抖动）。
    bool AcceptsExternalTransform() const noexcept override { return true; }

    /// 命中索引用的保守包络：一态包络 **× 外部变换**（拖走了也要点得到）。
    ///
    /// 动态档拿它算**洞**（不进表），所以它必须盖住"当前形状 + 所有可能的变换"。
    InkingShapeBounds GetHitEnvelope() const override;

    /// 构造时用的那份配置（已经 Normalize 过）。
    const InkingImageData& GetData() const noexcept;

    /// 现在这张图的名字（**纹理缓存键** / 日志用）。没有图时是空串。
    const std::string& ImageName() const noexcept;

    /// 运行期读进来的字节数（0 = 没走那条路，或者没读到）。
    std::size_t AssetByteCount() const noexcept;

    /// 现在用的是不是运行期读进来的字节（而不是生成期烘的那份）。
    bool UsingAssetBytes() const noexcept;

    /// 这个组件**自己**加载过几次（全局计数在 `InkingAssetStatsSnapshot()`）。
    int AssetLoadCount() const noexcept;

    /// 运行期换图：从资源根读一个相对路径。读不到**保留旧图**。
    bool LoadAsset(const std::string& relative);

    /// 运行期换图：直接给字节（本类拷贝一份自己拥有，`name` 是纹理缓存键）。
    void SetImageBytes(const std::uint8_t* data, std::size_t size,
                       const std::string& name);

protected:
    /// 按形状画这一笔（颜色 / 图片 + 描边阴影）。**不画文字**——那是 Label 的事。
    void onRender(float pixelX, float pixelY) const override;

    /**
     * 指针状态变了（**动态档唯一的输入时机**）。
     *
     * 场景只在指针真的变了的那一帧推一次（`InkingScene::DispatchPointer` 第 0 步），
     * 鼠标不动就一次都不推。这里用 `inking_detail::PointerProbe` 判边沿与捕获，
     * 拖动 / 缩放按累计位移写几何。
     */
    void onPointerStateChanged() override;

    /**
     * 换"这一笔画纹理的哪一块"（图集切格，0.6.1 T-04）。
     *
     * 与静态版同一条：只标**重绘脏**、不标命中脏——切格只换取哪块，几何不动。
     * （动态精灵 G-03 要靠它切图集，所以这一条在动态档上必须先有。）
     */
    void SetRenderSourceRect(const InkingShapeBounds& srcRect);

    /// 当前生效的源矩形（全零 = 整张图）。
    const InkingShapeBounds& GetRenderSourceRect() const noexcept;

private:
    /// 把 data 搬进自己 + 基类；构造函数用。
    void applyData(const InkingImageData& data);

    /// G-02：保留既有私有符号；唯一实现已移到 InkingImageLike。
    void refreshRenderAppearance();

    /// 按下那一刻定模式：整块拖动，还是抓右下角把手缩放。
    /// 与**命中**同源（`inking_detail::HitTestLocalRect`）——只减绝对位置不过逆变换的话，
    /// 图片被缩放后"看到的把手"与"算出来的把手"会错位。
    void resolveDragMode(float worldX, float worldY) noexcept;

    /// 按"按下点到当前点"的累计位移写一次几何（拖动 → offset，缩放 → size）。
    void applyDrag(float worldX, float worldY) noexcept;

    /// 动态档的指针边沿判定（`detail/InkingPointerProbe.h`）：hover / 捕获 /
    /// 抬起算不算点击。动态组件没人替它记历史，只能自己存一份。
    inking_detail::PointerProbe _probe{};

    // ---- 拖拽 / 缩放：一次交互的状态 ----
    bool _draggable = false;   ///< 允许整块拖动
    bool _resizable = false;   ///< 允许抓右下角缩放
    bool _dragArmed = false;   ///< 这次按下归我（按下在那时判出、抬起清）
    bool _dragMoved = false;   ///< 这次交互里**真的写过几何**（`IsDragging` 读它）
    bool _dragScaling = false; ///< 这次交互是缩放（否则是移动）

    /// 按下那一刻的几何基准：拖动按"加位移"算，缩放按"乘比例"算，都必须有原点。
    float _dragPressX = 0.0f;
    float _dragPressY = 0.0f;
    float _dragStartOffsetX = 0.0f;
    float _dragStartOffsetY = 0.0f;
    int _dragStartWidth = 0;
    int _dragStartHeight = 0;
};

