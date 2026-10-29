#pragma once

// 帧动画组件（0.6.0 T-03）：把一组图按固定帧率轮流画出来。
//
// 为什么是 `InkingStaticImage` 的派生类、而不是另起一个"动画组件"：
//   - 帧动画的**每一帧都是一张图**——形状、裁剪、描边阴影、锚点、命中这些事
//     和静态图片一模一样，重写一遍必然分叉（"按钮和图片画得不一样"那条教训）；
//   - 换帧只改"用哪份字节"，**几何一个字节都不动** ⇒ 它仍是**静态档**
//     （`src/inking.cpp` 的编译期断言继续成立，命中表不用重烘）。
//
// ---------------------------------------------------------------------------
// 时间从哪来：场景动画时钟（0.6.0 T-01），不是"累加 delta"
//
// `frame = floor(t × fps) % count`，`t` 来自 `InkingScene::AnimationSeconds()`。
// 这一条决定了三件事，写在这里免得以后反复推：
//   - **慢帧不慢放**：一帧画了 0.25s，帧号直接跨 3 帧，而不是"一次回调 +1 帧"；
//   - **同屏同步**：两个同 fps 的 sprite 读同一个时钟，帧号恒等，不会各自漂；
//   - **长跑不丢精度**：先用 `fmod` 把时间夹进一个周期再换算，1 小时后仍然逐帧准确
//     （直接 `t × fps` 在 10^5 秒量级会把 double 的小数位吃掉）。
//
// ---------------------------------------------------------------------------
// 字节 vs 纹理：两件事别混
//
//   - **字节在构造期备齐**（`loadFrameBytes()`）：运行期换帧**零 I/O**。
//     这一条是有代价换来的——`LoadAsset` 每次都会 `AssetLoad` 读盘，
//     每 1/12 秒读一次文件不可接受。
//   - **纹理在第一次画到那一帧时由渲染层解码**，之后进纹理表（键 = 帧名）。
//     构造期**烘不出纹理**：组件构造时窗口 / 渲染器还不存在（`Show()` 之后才有），
//     而纹理属于渲染器。所以"预热"的准确含义是"字节预热 + 首轮播放建纹理"，
//     第二轮起换帧 `texturesCreated` 增量为 0（自检按这个断言）。
//
// 暂停语义是"**时间冻结**"，不是"停止换帧"：`Pause()` 记下暂停起点，
// `Play()` 把这段时间补进 `_pauseOffset`——于是"暂停 3 秒再继续"从原帧接着走，
// 而不是跳过 36 帧。这也让"同一场景里两个 sprite 各自暂停"互不影响。

#include <event/InkingEvent.h>
#include <image/InkingStaticImage.h>
#include <image/SpriteData.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inking {

/**
 * @brief 帧动画：序列帧 / 图集两种素材 + 播放 / 暂停 / 定位。
 *
 * 数据面见 `SpriteData`（序列帧与图集二选一，`Validate()` 会拦"都填/都不填"）。
 * 两种素材**共用同一套帧号计算**（`frame = floor(t × fps)`），只在"怎么把这一帧
 * 搬到画面上"分两路：
 *   - **序列帧**：每帧一份独立字节，换帧 = 换字节视图（`SetImageBytes`）；
 *   - **图集**：字节是同一张，换帧 = 换**源矩形**（`SetRenderSourceRect`），
 *     纹理一次建好、之后一个字节都不动（0.6.1 T-05）。
 *
 * 分叉点收在 `LoadedFrame`（每帧带一个 `sourceRect`）与 `applyFrame()` 一处，
 * 不写成两个 `if` 分支各走一遍——否则"序列帧的暂停语义"和"图集的暂停语义"
 * 迟早会不一致，而这种不一致只会表现为"某个模式有点怪"。
 */
class InkingSprite : public InkingStaticImage {
public:
    /**
     * @param parent 上级锚点；非拥有。
     * @param data   配置（构造时 `Normalize()` 一次并 `Validate()`；校验不过会记
     *               ERROR 且 `IsValid()` 为 false——**照常构造**，因为"少一个动画"
     *               不该让整个界面起不来）。
     */
    InkingSprite(InkingWidget* parent, const SpriteData& data);

    /**
     * 帧变了（新帧号, 旧帧号）。
     *
     * - **播放在推进**导致的换帧才发（`SeekToFrame` / `Stop` 也发，"画面确实变了"
     *   这件事对使用者是一致的）；
     * - 第一次落到起始帧**不发**（没有"旧帧"这回事）；
     * - 慢帧跨多帧只发一次（新帧号是落点，不是中间那些）。
     *
     * 帧事件（配置里逐帧命名的 `events`）是另一回事，见 0.6.3 T-09。
     */
    InkingEvent<std::size_t, std::size_t> frameChanged;

    /**
     * 播到某个**有事件的帧**了（事件名, 帧号）。配置里写 `events`（0.6.3 T-09）。
     *
     * - **由帧号变化驱动**：一次跨 3 帧，中间那几帧的事件照样发（`(旧, 新]` 区间
     *   扫描）——掉帧不等于吞事件；
     * - **回绕也发**：`3 → 0` 拆成 `(3, 末帧]` + `[0, 0]` 两段扫；
     * - **只有播放推进才发**：`SeekToFrame` / `Stop` / 构造期落帧都不发——"跳到第 5 帧"
     *   不是"播过了第 1~5 帧"，那些事件没发生过；
     * - 一帧多个事件按**帧号升序**发，同帧内按配置顺序发；
     * - `frameChanged` 在**这之后**发（订阅者收到"换帧"时，事件已经处理完了）。
     *
     * 名字是 `const std::string&`，**指向本组件自己那份配置**（生命周期同组件）：
     * 在回调里存下来没事，组件没了它就悬垂（和别的事件同一档约定）。
     */
    InkingEvent<const std::string&, std::size_t> frameEvent;

    /**
     * 非循环的动画**播完了**（`loop == false` 走到末帧）。只发一次。
     *
     * "再播一遍"要调 `Play()`——它会清掉 `_finished`，播完再发一次。循环动画
     * 永远不发（它没有"结束"这件事）。
     */
    InkingEvent<> finished;

    // ---------------- 状态机 ----------------

    /**
     * 开始 / 继续播放。
     *
     * 播完（非循环）之后再 `Play()` = **从头再来**（`_pauseOffset` 重算，`t` 回 0）。
     * 已经播着时是空操作。
     */
    void Play();

    /** 暂停：时间冻结在这个位置（恢复时从原帧继续，不跳帧）。 */
    void Pause();

    /** 停止：回到 `startFrame` 且不播（再 `Play()` 从那里开始）。 */
    void Stop();

    /**
     * 定位到某一帧（播放中也行，会重新对齐时间基准）。
     *
     * @param frame 越界会被夹到最后一帧（不报错：定位是交互，不是配置）。
     * @return 帧数 > 0 才返回 true（没有可播的帧时什么也不做）。
     */
    bool SeekToFrame(std::size_t frame);

    /**
     * @brief 换一份配置（0.6.5 T-13）。节点指针 / 父子关系 / 所有订阅**全部保留**。
     *
     * 为什么是"原地换"而不是"新建一个组件替换"：替换会让节点指针变、`Subscribe`
     * 全失效、z 序重排、父级重布局——而"切动作"（待机→跑步）在业务上是**同一个
     * 实体的同一个节点**，换掉它等于让调用方重新接线一遍。
     *
     * 语义四条，都写在这里免得以后猜：
     *   - **帧号**：`keepPhase == false`（默认）落到第 0 帧；`true` 保留换之前的帧号
     *     （越界夹到末帧）——"同一个动作换方向"时用它，画面不跳回第一帧；
     *   - **播放状态**：`IsPlaying()` 保持换之前的值。播完（`_finished`）的那份状态
     *     会被清掉：新配置是一段新动画，"上一段播完了"与它无关；
     *   - **不发 `frameChanged`**：换配置后的第一次落帧没有"旧帧"这回事，发出去就是
     *     一个假事件（订阅者会以为播过了）。帧事件同理；
     *   - **组件尺寸不动**：`width/height` 也在这份配置里，但改尺寸属于布局层
     *     （`ApplySelfSize` 会触发结构变 + `onSizeChanged`）。切到格宽不同的动画时
     *     由调用方自己调——留着不改，是"换动画顺手把布局也改了"更糟。
     *
     * 校验不过时**照常换**（`IsValid()` 变 false、记 ERROR、帧数可能为 0）：和构造
     * 那条路一个口径——"一个动画坏了"不该让整个界面停摆。
     */
    void SetSpriteData(const SpriteData& data, bool keepPhase = false);

    // ---------------- 观测 ----------------

    bool IsPlaying() const noexcept;
    bool IsFinished() const noexcept;  ///< 非循环播到末尾了（`Play()` 会清掉）
    std::size_t GetFrameIndex() const noexcept;

    /// **实际可播的帧数**（配置里的帧数；读失败的那一帧仍占一个槽位）。
    std::size_t GetFrameCount() const noexcept;

    /// 字节备齐的帧数（读失败的帧不计）。正常应与 `GetFrameCount()` 相等。
    std::size_t GetLoadedFrameCount() const noexcept;

    /// 播放推进导致的换帧次数（`SeekToFrame` / 初始落帧不计）。
    std::uint64_t GetFrameAdvanceCount() const noexcept;

    /**
     * 播放中被**跳过**的帧数（慢帧跨 k 帧 ⇒ +k-1；回绕与倒退不计）。
     *
     * 它是"画面掉帧"的量化口：长时间跑下来这个数一直涨，说明帧率定得比渲染高。
     */
    std::uint64_t GetSkippedFrameCount() const noexcept;

    /// 当前用于算帧的时间（秒，已含 `startOffsetSeconds`、已扣掉暂停时长）。
    double GetAnimationTime() const noexcept;

    /// 构造时那份配置（已 `Normalize`）。
    const SpriteData& GetSpriteData() const noexcept;

    /// 配置校验的错误说明；空串 = 合法。
    const std::string& GetConfigError() const noexcept;

    /// 配置合法吗（`GetConfigError()` 为空）。
    bool IsValid() const noexcept;

    /// 实际走的是图集模式吗（序列帧配置恒为 false）。
    bool IsSheetMode() const noexcept;

protected:
    /// 每渲染帧一次：按**场景时钟**重算帧号（不是"收一次回调 +1 帧"）。
    void onAnimationTick(float deltaSeconds) override;

private:
    /// 一帧的字节视图 + 纹理缓存键。
    struct LoadedFrame {
        const std::uint8_t* data = nullptr;
        std::size_t size = 0;
        /// 纹理缓存键：`source` 非空就用它（同源两帧共用纹理是**正确**的），
        /// 为空时退化成 `<组件名>#<帧号>`。
        std::string name;
        /// 这一帧在纹理里的像素矩形（图集切格）。全零 = 整张纹理（序列帧）。
        ShapeBounds sourceRect{};
    };

    /// 构造期把每帧的**字节**（序列帧）或**源矩形**（图集）备齐。
    void loadFrameBytes();

    /**
     * 把"配置怎么落地"整段走一遍：`Normalize` → `Validate` → 定 `_mode` →
     * 图集量一次图 → 备帧 → 落起始帧（0.6.5 T-13 从构造函数抽出来的）。
     *
     * 构造函数与 `SetSpriteData` **共用这一段**，于是"构造时的配置"和"换来的配置"
     * 不会走出两条略有差别的路——那种差别只会在"换过去之后有点怪"时露头。
     * @param frame 落哪一帧（越界由它夹到末帧）。构造传 `startFrame`，换配置传
     *        0 或 `keepPhase` 保留的那个帧号——**由调用方定**，因为"从哪一帧开始"
     *        正是两条路唯一的差别（各落一次会多发一个 `frameChanged`）。
     *
     * 它**不碰** `_playing` / 时间基准（那是两条调用方各自的事）。
     */
    void rebuildFromData(std::size_t frame);

    /**
     * 图集模式：把"那张大图"的字节同步到绘制层（0.6.5 T-13）。
     *
     * 为什么换配置时必须补这一笔：基类的字节视图是**构造时**那份（序列帧的第一帧），
     * 换成图集后如果不换，`applyFrame` 只改源矩形不动字节 ⇒ 画面仍是旧图，而
     * `IsSheetMode()` / 帧数全是对的——一个"看起来换成功了"的静默错。
     * 生成期烘的走 `SetImageBytes`（拷一份），运行期读的走 `LoadAsset`。
     */
    void syncSheetBytes();

    /// 图集模式的构造期几何校验（量图 vs `SheetWidth/SheetHeight`）。
    void validateSheetGeometry();

    /// 落到某一帧：换字节视图（序列帧）/ 换源矩形（图集）、累加计数、发事件。
    /// @param fromPlayback true = 播放推进导致（要计 skipped），false = 定位 / 初始。
    void applyFrame(std::size_t index, bool fromPlayback);

    /**
     * 发 `(previous, current]` 这一段里所有帧事件（回绕时拆两段）。
     *
     * 为什么按**区间**而不是"只看落点"：慢帧一次跨 3 帧时，只判落点会把中间那几帧
     * 的事件吞掉——而"吞事件"在业务上是静默的（少播一次脚步声，谁也看不出来）。
     */
    void fireEventsBetween(std::size_t previous, std::size_t current);

    /// 场景动画时钟（无场景时退化成自己累加的 delta）。
    double nowSeconds() const noexcept;

    /**
     * 惰性对齐时间基准：让"现在"正好落在 `_frameIndex` 这一帧上。
     *
     * 为什么不能放在构造函数里：构造可能早于"进场景"（先 `new` 后 `AddChild`
     * 是常见写法），那时时钟还是兜底的 0，对出来的基准会在第一次 tick（场景时钟
     * 可能已经是 30 秒）把帧号甩飞。第一次 tick / `Play` / `SeekToFrame` 时才对齐，
     * 于是"构造 → 进场景 → 开始播"和"运行期新建一个 sprite"是同一套语义。
     *
     * @param alreadyElapsedSeconds 第一次 tick 传**这一帧的 delta**：基准要退回到
     *        "上一帧的时钟读数"，否则第一帧白丢（表现为"每次播放第一帧多显示
     *        一个渲染帧"）。`Play` / `Seek` 没有"上一帧"，传 0。
     */
    void ensureTimeBase(double alreadyElapsedSeconds = 0.0);

    /// 有效帧率（`<= 0` 时退 12，和 `SpriteData` 的默认值一致）。
    double fps() const noexcept;

    SpriteData _sprite{};

    /// 素材形态（构造时按 `SpriteData::UsesSheet()` 定，之后不变）。
    enum class FrameMode { Sequence, Sheet };
    FrameMode _mode = FrameMode::Sequence;

    /// 每帧的字节视图。指向 `_frameBytes[i]`（运行期读）或生成期烘的静态数组。
    std::vector<LoadedFrame> _frames;

    /// 运行期读进来的字节（本类拥有）。`runtimeAsset == false` 时全空。
    std::vector<std::vector<std::uint8_t>> _frameBytes;

    std::string _configError;
    std::size_t _loadedFrameCount = 0;

    std::size_t _frameIndex = 0;

    /// 已经落过至少一帧了吗（没落过就没有"旧帧"，`frameChanged` 不发）。
    bool _applied = false;

    bool _playing = false;
    bool _finished = false;

    /// 暂停累计时长：`t = 时钟 − _pauseOffset + startOffset`。
    double _pauseOffset = 0.0;

    /// 时间基准对齐过了吗（见 `ensureTimeBase`）。
    bool _offsetReady = false;

    /// 本次暂停开始时的时钟读数（只在 `!_playing` 时有意义）。
    double _pauseStartSeconds = 0.0;

    /// 没有场景时的兜底时间（组件被摘出树之后仍能自己走，自检用）。
    double _fallbackSeconds = 0.0;

    std::uint64_t _frameAdvanceCount = 0;
    std::uint64_t _skippedFrameCount = 0;
};

}  // namespace inking
