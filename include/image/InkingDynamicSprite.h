#pragma once

// 动态档帧动画（G-03）：几何可以在运行期改的帧动画。
//
// ---------------------------------------------------------------------------
// 与静态版 `InkingSprite` 的差别：只有几何与输入（别自己加第三条）
//
//   1. **几何能改**：继承 `InkingDynamicWidget`，四个写入口都在
//      （`Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset`）；
//      静态版的 `InkingSprite` 反过来是**编译期**钉死的——`src/inking.cpp` 的
//      `static_assert(!HasResize<InkingSprite>)` 一族，谁把静态档顺手改成动态
//      就当场红。
//   2. **输入靠拉**：动态档收不到 `PushPointer*`，只有 `onPointerStateChanged()`
//      那一次通知。**本任务没有内置拖动**（G-03 的 API 里没有 `SetDraggable`）：
//      精灵的"每帧走位"是实体层控制器自己调 `ChangeOffset` 算出来的，不是用户
//      拖着走。要拖动就自己读场景指针再调那四个入口，别指望框架替它记历史。
//
// ---------------------------------------------------------------------------
// 帧逻辑与静态版**逐字相同**（G-03 复制，G-04 起收进 `InkingSpriteLike`）
//
//   G-03 动手时按"先复制、再抽取"的纪律，把 `resolveFrame` / `applyFrame` /
//   `ensureTimeBase` / `fireEventsBetween` / `loadFrameBytes` / `rebuildFromData` /
//   `validateSheetGeometry` / `syncSheetBytes` 整段照抄过来——先让两档各自跑起来、
//   用"100 帧后三项计数逐一相等"证明同源，重构那一步才有客观判据（`selftest`
//   断言不改就过）。**G-04 已把那份实现收进 `InkingSpriteLike`**，本类不再持有帧机；
//   所以"改一处必须改两处"这条复制期的纪律**已经作废**，帧机只有一个落点。
//
//   为什么当初不能直接继承 `InkingSprite`：它继承的是 `InkingStaticImage`，那条
//   继承链上带着"静态几何定死"这条铁律（没有几何写入口）。动态档要的是
//   `InkingDynamicWidget` 那一支，两者是兄弟，不是父子——共用只能靠 `Like` 层。
//
// ---------------------------------------------------------------------------
// 命中表：不进表，占地记成"洞"
// ---------------------------------------------------------------------------
//
//   与 `InkingDynamicImage` 同档：`HitTest` 自己判、自己维护状态，场景只在点落进
//   它的洞里时才来问它。四个几何入口都走 `MarkGeometryMoved()`（T-19）——精灵是
//   叶子，于是移动只刷洞、不重建整表。`OwnsSubtreeHit()` 保持 false（精灵不当容器；
//   要"一个节点多个动作"用 `InkingAnimateGroup`，那是另一件事）。
//
//   洞过期 = 那一片**点不到**，所以"`Resize` 之后命中跟着变"是 G-03 的验收项之一：
//   它同时证明几何真的改了、且洞跟着刷了。
//
// ---------------------------------------------------------------------------
// `SetSpriteData` 仍不改尺寸（沿静态版语义）
// ---------------------------------------------------------------------------
//
//   配置里也有 `width/height`，但换动作顺手改几何这件事**不在这里做**：一份配置
//   描述的是"这段动画长什么样"，尺寸归布局层（`InkingSprite` 的同名注释、
//   `CXXCSS.md` 的 sprite 段）。动态档把选择权交给调用方：要跟着动作改尺寸，就
//   自己调 `Resize`——那正是动态档比静态档多出来的能力，而"多出来的能力"不该被
//   藏在换配置的副作用里。

#include <event/InkingEvent.h>
#include <image/ImageLike.h>
#include <image/SpriteData.h>
#include <image/SpriteLike.h>
#include <widget/InkingDynamicWidget.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 几何可变、可每帧走位的帧动画（G-03）。
 *
 * 数据面与静态版同一份 `InkingSpriteData`（序列帧 / 图集二选一，`Validate()` 拦
 * "都填 / 都不填"）；绘制走 `inking_detail::PaintButtonAppearance`（与图片同一笔），
 * 换帧仍走 `SetRenderSourceRect`（图集）/ `SetImageBytes`（序列帧）。
 *
 * 时间源与静态版同一个：`InkingScene::AnimationSeconds()`，`frame =
 * floor(t × fps) % count`——不是"每次 tick +1 帧"。于是"同一场景里静态精灵与
 * 动态精灵帧号恒等"是**结构保证**，不靠两边碰巧一致（G-03 的验收第 1 条正是它）。
 *
 * **G-04 起帧机在 `InkingSpriteLike`**（两档共用，纯重构）：本类只剩"动态档的接线"——
 * 构造时把 `SetImageBytes` / `LoadAsset` / `SetRenderSourceRect` 与三个事件槽
 * 绑给帧机，每渲染帧转调一次 `advanceFrame()`。
 */
class InkingDynamicSprite : public InkingDynamicWidget,
                            public InkingImageLike,
                            public InkingSpriteLike {
public:
    /**
     * @param parent 上级节点；非拥有。
     * @param data   配置（构造时 `Normalize()` 一次并 `Validate()`；校验不过会记
     *               ERROR 且 `IsValid()` 为 false——**照常构造**，因为"少一个动画"
     *               不该让整个界面起不来）。
     */
    InkingDynamicSprite(InkingWidget* parent, const InkingSpriteData& data);

    // ---------------- 状态机（与静态版同名同义） ----------------

    /// 帧变了（新帧号, 旧帧号）。语义见 `InkingSprite::frameChanged`，逐条不变。
    InkingEvent<std::size_t, std::size_t> frameChanged;

    /// 播到某个**有事件的帧**了（事件名, 帧号）。区间扫描、回绕拆两段，同上。
    InkingEvent<const std::string&, std::size_t> frameEvent;

    /// 非循环的动画**播完了**（走到末帧）。只发一次；`Play()` 会清掉重来。
    InkingEvent<> finished;

    /// 开始 / 继续播放。播完之后再 `Play()` = 从头再来。
    void Play();

    /// 暂停：**时间冻结**在这个位置（恢复时从原帧继续，不跳帧）。
    void Pause();

    /// 停止：回到 `startFrame` 且不播。
    void Stop();

    /// 定位到某一帧（播放中也行，会重新对齐时间基准）。越界夹到末帧。
    bool SeekToFrame(std::size_t frame);

    /**
     * @brief 换一份配置。节点指针 / 父子关系 / 订阅全部保留。
     *
     * 语义四条与静态版一致（帧号、播放状态、不发 `frameChanged`、**尺寸不动**），
     * 差别只在最后一条的**后果**：静态档想改尺寸得回头重建布局，动态档自己调
     * `Resize` 就行——但那是调用方的事，不在这里替它决定。
     */
    void SetSpriteData(const InkingSpriteData& data, bool keepPhase = false);

    // ---------------- 观测（状态机） ----------------

    bool IsPlaying() const noexcept;
    bool IsFinished() const noexcept;
    std::size_t GetFrameIndex() const noexcept;

    /// 实际可播的帧数（配置里的帧数；读失败的那一帧仍占一个槽位）。
    std::size_t GetFrameCount() const noexcept;

    /// 字节备齐的帧数（读失败的帧不计）。
    std::size_t GetLoadedFrameCount() const noexcept;

    /// 播放推进导致的换帧次数（`SeekToFrame` / 初始落帧不计）。
    std::uint64_t GetFrameAdvanceCount() const noexcept;

    /// 播放中被**跳过**的帧数（慢帧跨 k 帧 ⇒ +k-1；回绕与倒退不计）。
    std::uint64_t GetSkippedFrameCount() const noexcept;

    /// 当前用于算帧的时间（秒，已含 `startOffsetSeconds`、已扣掉暂停时长）。
    double GetAnimationTime() const noexcept;

    /// 构造时那份配置（已 `Normalize`）。
    const InkingSpriteData& GetSpriteData() const noexcept;

    /// 配置校验的错误说明；空串 = 合法。
    const std::string& GetConfigError() const noexcept;

    /// 配置合法吗（`GetConfigError()` 为空）。
    bool IsValid() const noexcept;

    /// 实际走的是图集模式吗（序列帧配置恒为 false）。
    bool IsSheetMode() const noexcept;

    // ---------------- 与静态版同名同义的图片面 ----------------
    //
    // 静态版有的公开面这里一个不少（G-01 立的规矩）：G-04 抽 `InkingSpriteLike` 时
    // 不用回头补"漏掉的那几个观测口"。

    /// 指针在这个位置上吗（世界坐标）。**照常参与命中**：精灵是实体，不是标签。
    bool HitTest(float worldX, float worldY) const override;

    /// 形状定义：渲染 / 命中 / 包围盒三者都从它派生。
    const InkingShapeSpec& GetShape() const noexcept;

    /// 当前外观（精灵只有一态，恒等于配置里的 `normal` + 当前源矩形）。
    const InkingButtonAppearance& GetAppearance() const noexcept;

    /// 画出来的颜色（0xAARRGGBB）：就是 `normal` 那份。
    std::uint32_t GetDisplayColor() const noexcept;

    /// 生效的变换：**外部显示变换 ∘ `normal` 那份**（A13 保留，与几何正交）。
    InkingTransformSpec GetDisplayTransform() const noexcept;

    /// 变换同时作用于子节点（和图片 / 面板同一条约定）。
    InkingTransformSpec GetSubtreeTransform() const noexcept override;

    /// 精灵认外部显示变换（A13）：静态版认，动态版也认——两套在绘制与命中处复合。
    bool AcceptsExternalTransform() const noexcept override { return true; }

    /// 命中索引用的保守包络：一态包络 **× 外部变换**（动态档拿它算洞）。
    InkingShapeBounds GetHitEnvelope() const override;

    /// 构造时用的那份配置（`InkingImageData` 那一半，已 Normalize 过）。
    const InkingImageData& GetData() const noexcept;

    /// 现在这张图的名字（**纹理缓存键** / 日志用）。没有图时是空串。
    const std::string& ImageName() const noexcept;

    /// 运行期读进来的字节数（0 = 没走那条路，或者没读到）。
    std::size_t AssetByteCount() const noexcept;

    /// 现在用的是不是运行期读进来的字节。
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
     * 每渲染帧一次：按**场景时钟**重算帧号。
     *
     * 两档都收这个钩子（见 `InkingDynamicWidget.h` 六项差异第 5 项）——所以
     * "帧动画在动态档上怎么推进"与静态档是**同一条路**，不是 `Tick`。
     */
    void onAnimationTick(float deltaSeconds) override;

    /// 换"这一笔画纹理的哪一块"（图集切格）。只标重绘脏、不标命中脏。
    void SetRenderSourceRect(const InkingShapeBounds& srcRect);

    /// 当前生效的源矩形（全零 = 整张图）。
    const InkingShapeBounds& GetRenderSourceRect() const noexcept;

private:
    /// 把 data 搬进自己 + 基类；构造函数用。
    void applyData(const InkingSpriteData& data);

    /// G-02：保留既有私有符号；唯一实现已移到 InkingImageLike。
    void refreshRenderAppearance();

    /// 构造期把"帧机要组件替它做的六件事"绑成本类的成员（见 `InkingSpriteLike::Hooks`）。
    /// 动态档绑自己的 `SetImageBytes` / `LoadAsset` / `SetRenderSourceRect`
    /// （它们比共用层多标一次重绘脏）。
    static InkingSpriteLike::Hooks hooksFor(InkingDynamicSprite& self);
};

