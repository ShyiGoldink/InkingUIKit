#pragma once

// 静态 / 动态精灵共用的**帧机**（G-04，纯重构）。
//
// 承载：配置与帧表、时间 → 帧号的换算、落帧、事件区间扫描、图集切格、
// 全部观测口，以及状态机（`Play` / `Pause` / `Stop` / `SeekToFrame` / `SetSpriteData`）。
//
// 抽取的判据只有一条：**行为零变化**。所以这里的方法与 `G-03` 之前写在
// `InkingSprite.cpp` 里的那份**逐字相同**（去注释、类名归一后逐字节可比），
// 只是"把画面交出去"与"发事件"从"组件自己的成员"换成了注入的回调。
//
// ---------------------------------------------------------------------------
// 为什么不是 Widget、也**一个虚函数都没有**
//
//   1. **不是 Widget**：两档各自继承自己的 Widget 基类（静态 / 动态是二选一），
//      再多继承本类。`InkingSpriteLike` 不是删除用的多态手柄，没有 `onRender` / `onTick`。
//   2. **不能有虚函数**：虚析构会给"次基类"引入析构 thunk，`nm -g` 的符号集合
//      当场变——G-02 踩过（`InkingImageLike` 首轮加了虚析构，两个 thunk 让判据红）。
//      `InkingSprite` 的 `nm` diff 为空是 G-04 的硬判据之一，所以这里连析构都
//      是**受保护且非虚**的。
//   3. 于是"`Like` 要组件替它做的事"只能靠**构造期注入的回调**（`Hooks`）：
//      换字节 / 换源矩形 / 发三种事件。回调在**调用时机**上与原来写在
//      `applyFrame` / `onAnimationTick` 里的那几行完全等价（同一行位置、同一个
//      先后顺序），所以行为不变，`selftest` 一条断言都不用动。
//   4. 本类**没有纯虚函数**（`TaskGuide-G.md` §4 第 2 条）：它不是契约，而是
//      "一份实现被两个类共用"。`src/inking.cpp` 用 `is_abstract_v` 钉住它——
//      谁哪天加了纯虚函数，构建当场红。
//
// ---------------------------------------------------------------------------
// 两个引用参数都是必需的（别为了"少一个字段"合并掉）
//
//   - `_host`：只用来读**场景动画时钟**（`GetScene()->AnimationSeconds()`）。
//     帧号是时间的**纯函数**，同场景的所有精灵必须读同一个源；各累加各的会慢慢
//     错开（那正是 0.6.0 T-01 要解决的问题）。
//   - `_image`：只用来在**构造期**问"绘制层现在拿到的是哪份字节"（图集量图
//     校验）。那份字节可能是运行期读进来的、也可能是生成期烘的，只有绘制层知道；
//     数据层看不到图片多大。
//
// 构造顺序保证这两个引用在传进来时都已构造好：静态档是
// `InkingStaticImage`（内含 `InkingImageLike`）在前，动态档是 `InkingDynamicWidget`
// 与 `InkingImageLike` 在前，**本类永远排在基类列表最后**。
//
// ---------------------------------------------------------------------------
// 时间从哪来：场景动画时钟（0.6.0 T-01），不是"累加 delta"
//
// `frame = floor(t × fps) % count`，`t` 来自 `InkingScene::AnimationSeconds()`。
// 这一条决定了三件事，写在这里免得以后反复推：
//   - **慢帧不慢放**：一帧画了 0.25s，帧号直接跨 3 帧，而不是"一次回调 +1 帧"；
//   - **同屏同步**：两个同 fps 的精灵读同一个时钟，帧号恒等，不会各自漂；
//   - **长跑不丢精度**：先用 `fmod` 把时间夹进一个周期再换算，1 小时后仍然逐帧准确
//     （直接 `t × fps` 在 10^5 秒量级会把 double 的小数位吃掉）。
//
// 暂停语义是"**时间冻结**"，不是"停止换帧"：`Pause()` 记下暂停起点，
// `Play()` 把这段时间补进 `_pauseOffset`——于是"暂停 3 秒再继续"从原帧接着走，
// 而不是跳过 36 帧。这也让"同一场景里两个精灵各自暂停"互不影响。
//
// ---------------------------------------------------------------------------
// 字节 vs 纹理：两件事别混
//
//   - **字节在构造期备齐**（`loadFrameBytes()`）：运行期换帧**零 I/O**。
//     这一条是有代价换来的——`LoadAsset` 每次都会读盘，每 1/12 秒读一次文件不可接受。
//   - **纹理在第一次画到那一帧时由渲染层解码**，之后进纹理表（键 = 帧名）。
//     构造期**烘不出纹理**：组件构造时窗口 / 渲染器还不存在，而纹理属于渲染器。
//     所以"预热"的准确含义是"字节预热 + 首轮播放建纹理"，第二轮起换帧
//     `texturesCreated` 增量为 0（自检按这个断言）。

#include <image/ImageLike.h>
#include <image/SpriteData.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class InkingWidget;

/**
 * @brief 两档精灵共用的帧机（G-04，纯重构）。
 *
 * 无虚函数、无纯虚函数、受保护的非虚析构；派生类在构造时把"要组件替它做的事"
 * 注入进来（`Hooks`）。见文件头。
 */
class InkingSpriteLike {
public:
    InkingSpriteLike(const InkingSpriteLike&) = delete;
    InkingSpriteLike& operator=(const InkingSpriteLike&) = delete;

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
    const InkingSpriteData& GetSpriteData() const noexcept;

    /// 配置校验的错误说明；空串 = 合法。
    const std::string& GetConfigError() const noexcept;

    /// 配置合法吗（`GetConfigError()` 为空）。
    bool IsValid() const noexcept;

    /// 实际走的是图集模式吗（序列帧配置恒为 false）。
    bool IsSheetMode() const noexcept;

    // ---------------- 状态机 ----------------

    /**
     * 开始 / 继续播放。
     *
     * 播完（非循环）之后再 `Play()` = **从头再来**（`_pauseOffset` 重算，`t` 回 0）。
     * 已经播着时是空操作。
     */
    void Play();

    /// 暂停：时间冻结在这个位置（恢复时从原帧继续，不跳帧）。
    void Pause();

    /// 停止：回到 `startFrame` 且不播（再 `Play()` 从那里开始）。
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
     * 语义四条：**帧号**（`keepPhase == false` 落第 0 帧，`true` 保留换之前的帧号）、
     * **播放状态**（`IsPlaying()` 保持；播完的状态被清掉）、**不发 `frameChanged`**
     * （换配置后的第一次落帧没有"旧帧"）、**组件尺寸不动**（改尺寸属于布局层）。
     * 逐条理由在 `InkingSprite.h` 的同名注释里——那份文档仍是对外的承诺。
     *
     * 校验不过时**照常换**（`IsValid()` 变 false、记 ERROR、帧数可能为 0）：和构造
     * 那条路一个口径——"一个动画坏了"不该让整个界面停摆。
     */
    void SetSpriteData(const InkingSpriteData& data, bool keepPhase = false);

protected:
    /**
     * 派生类在构造期注入的六个动作。**都不是虚函数**（理由见文件头第 2 条）：
     * 它们只是"把某个具体类的成员函数绑成一个可调用对象"，调用时机由本类决定。
     *
     * 谁会填空：静态档绑 `InkingStaticImage` 那套（`SetImageBytes` /
     * `LoadAsset` / `SetRenderSourceRect`，三者都会标重绘脏）；动态档绑自己的同名成员。
     */
    struct Hooks {
        /// 换"这一笔画哪份字节"（序列帧）。`name` 是纹理缓存键。
        std::function<void(const std::uint8_t*, std::size_t, const std::string&)>
            setBytes;
        /// 从资源根读一份图并接到绘制层（运行期读的那条路）。返回 false = 读不到
        /// （组件自己会记 ERROR 并保留旧图）。
        std::function<bool(const std::string&)> loadAsset;
        /// 换源矩形（图集切格）。全零 = 整张纹理。
        std::function<void(const InkingShapeBounds&)> setSourceRect;
        /// 帧变了（新帧号, 旧帧号）。
        std::function<void(std::size_t, std::size_t)> frameChanged;
        /// 播到某个**有事件的帧**了（事件名, 帧号）。
        std::function<void(const std::string&, std::size_t)> frameEvent;
        /// 非循环的动画播完了。
        std::function<void()> finished;
    };

    // 非虚且受保护：本类不是删除用的多态手柄；也避免给两档新增析构 thunk。
    ~InkingSpriteLike() = default;

    InkingSpriteLike(InkingWidget& host, InkingImageLike& image, Hooks hooks) noexcept;

    /**
     * 构造用：搬配置 → 落起始帧 → 定播放状态。
     *
     * 与 G-04 之前 `InkingSprite` 构造体里那三行**逐字等价**（`_sprite = data;`
     * → `rebuildFromData(startFrame)` → `_playing = autoplay`）。两档的构造都调它，
     * 于是"构造时怎么落地"只有一份写法。
     */
    void resetSpriteData(const InkingSpriteData& data);

    /**
     * 把 `InkingSpriteData` 收成 `InkingImageLike` 该收的那份 `InkingImageData`（构造期一次）。
     *
     * 两档的 `applyData` 都在喂 `InkingImageLike::resetImageData` 之前调它，于是
     * "`normal` 取哪一帧 / 图集路径从哪儿对齐"只有一份写法。静态档在**基类构造
     * 之前**就要它（`InkingStaticImage` 的 ctor 参数），所以它是 `static` 的。
     *
     * @note 细节见 `src/image/InkingSprite.cpp` 里那份实现的注释（序列帧为什么
     *       要换 `normal`、图集为什么只对齐 `source`）。
     */
    static InkingImageData baseImageData(const InkingSpriteData& data);

    /**
     * 每渲染帧一次：按**场景时钟**重算帧号（不是"收一次回调 +1 帧"）。
     *
     * 两档的 `onAnimationTick` 各转调一次——`onAnimationTick` 本身仍是**组件**的
     * 虚函数（`InkingWidget` 那一层），本类不是 Widget，不参与那条链。
     */
    void advanceFrame(float deltaSeconds);

    // ---------------- 下面这些是 `InkingSprite` 的 ABI 转发目标 ----------------
    //
    // G-03 之前它们是 `InkingSprite` 的私有成员函数，因此被
    // `WINDOWS_EXPORT_ALL_SYMBOLS` 导出。G-04 的硬判据是"`nm -g` 里
    // `InkingSprite` 的符号一个都不能少"，所以两档仍在 .cpp 里保留同名的一行
    // 转发（`nm` 看得到 `InkingSprite::applyFrame` 等），实现只有这里这一份。
    // 它们**不再有内部调用者**——那是纯重构的代价，认下。

    /// 构造期把每帧的**字节**（序列帧）或**源矩形**（图集）备齐。
    void loadFrameBytes();

    /**
     * 把"配置怎么落地"整段走一遍：`Normalize` → `Validate` → 定 `_mode` →
     * 图集量一次图 → 备帧 → 落起始帧（0.6.5 T-13 从构造函数抽出来的）。
     *
     * 构造函数与 `SetSpriteData` **共用这一段**，于是"构造时的配置"和"换来的配置"
     * 不会走出两条略有差别的路——那种差别只会在"换过去之后有点怪"时露头。
     * 它**不碰** `_playing` / 时间基准（那是调用方各自的事）。
     */
    void rebuildFromData(std::size_t frame);

    /**
     * 图集模式：把"那张大图"的字节同步到绘制层（0.6.5 T-13）。
     *
     * 为什么换配置时必须补这一笔：绘制层的字节视图是**构造时**那份（序列帧的第一帧），
     * 换成图集后如果不换，`applyFrame` 只改源矩形不动字节 ⇒ 画面仍是旧图，而
     * `IsSheetMode()` / 帧数全是对的——一个"看起来换成功了"的静默错。
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
     * 可能已经是 30 秒）把帧号甩飞。第一次 tick / `Play` / `SeekToFrame` 时才对齐。
     *
     * @param alreadyElapsedSeconds 第一次 tick 传**这一帧的 delta**：基准要退回到
     *        "上一帧的时钟读数"，否则第一帧白丢（表现为"每次播放第一帧多显示
     *        一个渲染帧"）。`Play` / `Seek` 没有"上一帧"，传 0。
     */
    void ensureTimeBase(double alreadyElapsedSeconds = 0.0);

    /// 有效帧率（`<= 0` 时退 12，和 `InkingSpriteData` 的默认值一致）。
    double fps() const noexcept;

private:
    /// 一帧的字节视图 + 纹理缓存键。
    struct LoadedFrame {
        const std::uint8_t* data = nullptr;
        std::size_t size = 0;
        /// 纹理缓存键：`source` 非空就用它（同源两帧共用纹理是**正确**的），
        /// 为空时退化成 `<组件名>#<帧号>`。
        std::string name;
        /// 这一帧在纹理里的像素矩形（图集切格）。全零 = 整张纹理（序列帧）。
        InkingShapeBounds sourceRect{};
    };

    InkingWidget* _host;    // 非拥有：只读场景时钟
    InkingImageLike* _image;      // 非拥有：只问"绘制层现在哪份字节"
    Hooks _hooks{};

    InkingSpriteData _sprite{};

    /// 素材形态（构造时按 `InkingSpriteData::UsesSheet()` 定，之后不变）。
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

