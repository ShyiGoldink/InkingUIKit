#pragma once

// Sprite 的**纯数据**定义（0.6.0 T-02），对应 CXXCSS 里 `image` 段的 sprite 分支。
//
// 为什么独立成类型、而不是往 `ImageData` 里加 `frames` / `sheet`：
//   - 普通图片不该背一组永远为空的帧表与校验（"每张静态图都多背 100 字节 + 一段
//     走不到的分支"是纯粹的税）；
//   - 反过来 sprite 又确实要 `ImageData` 的全部字段（`normal` 是图、`runtimeAsset`
//     决定字节从哪儿来、`hitPassThrough` 决定挡不挡点击），所以是**继承**而不是组合
//     ——和 `ImageData : ButtonData` 同一条路（§2.1 第 7/8 条踩过这个坑：仓库里没有
//     `ImageAppearance` 这个类型，帧的类型就是 `ButtonAppearance`）。
//
// 两种素材**二选一**（用户"我全都要"落在"同一个组件支持两种来源"，不是"一份配置
// 同时用两种"）：
//   - **序列帧**：`frames` 里每项是独立的一张图（通常是散图，各自一份字节）；
//   - **图集**：`sheet` 描述"一张大图怎么切成 N 格"，换帧只改源矩形，纹理不动。
//
// 帧号本身**不在这里**：数据只说"有多少帧、怎么放"，"现在第几帧"是组件的状态
// （T-03）。数据与状态分开，才能让同一份配置被两个实例独立播放。

#include <button/ButtonData.h>
#include <detail/InkLog.h>
#include <image/ImageData.h>

#include <cstddef>
#include <string>
#include <vector>

namespace inking {

/// 模块名常量（日志用；约定：每个模块都暴露一个）。
inline constexpr const char* kSpriteModuleName = "InkingSprite";

/**
 * @brief 帧动画的配置（序列帧 / 图集二选一）。
 *
 * 字段名与 `CXXCSS.md` 的 sprite 段一一对应，不在这里改名——名字对不上时的排查
 * 成本比"换个更漂亮的命名"值钱得多（`ButtonData.h` 开头那段同一条理由）。
 */
struct SpriteData : public ImageData {
    /**
     * @brief 图集描述：一张图切成 `columns × rows` 格，按行优先取帧。
     *
     * 为什么 `source` 和 `SpriteData::normal.source` 并存：图集是"一张图 + 切法"，
     * 图上还压着 `fit` / 形状 / 命中这些**单图属性**（都在 `normal` 里），所以
     * `normal` 描述"这张图怎么画"，`sheet` 只描述"怎么切"。**`source` 留空 = 用
     * `normal.source`**——绝大多数配置只该写一处路径，重复写两次就是两份真相。
     */
    struct SpriteSheet {
        std::string source;  ///< 图集路径；空 = 沿用 `SpriteData::normal.source`

        int frameWidth = 0;   ///< 单格宽（像素）
        int frameHeight = 0;  ///< 单格高（像素）
        int columns = 0;      ///< 每行几格
        int rows = 0;         ///< 共几行

        /**
         * 帧数。0 = 取 `columns × rows`。
         *
         * 允许**显式给更小**的值：图集最后一格常常是空的，用给 `count` 比让美术
         * 去补一张透明图便宜。反过来 `count > rows × columns` 是配置错误（要的帧
         * 根本不存在），`Validate()` 会拦。
         */
        int count = 0;

        /**
         * 起始格（**绝对格号**，行优先，0.6.4 T-11）。
         *
         * "每行一个方向"是最常见的排布（`Slime1_Idle.png` 就是 6 列 × 4 行，
         * 第 0/1/2/3 行分别是正 / 背 / 左 / 右），而没有起点字段时**只能播第 0 行**。
         * 有了它：第 r 行 = `firstFrame = r × columns` + `count = columns`。
         *
         * 三条约定，写反了会**静默播到别的行**：
         *   - 它是**格号**，`count` 是**帧数**，两者不是一回事；
         *   - 它**不进** `IsConfigured()`：只写 `firstFrame` 不算"配了图集"，
         *     否则一处笔误就让整张图片变成"图集模式"；
         *   - `CellRect()` 的入参仍是**绝对格号**——"帧号 → 格号"的换算
         *     （`+ firstFrame`）只在 `InkingSprite::loadFrameBytes()` 里做一次。
         *     改 `CellRect` 的语义会让既有断言变成另一种意思。
         *
         * 默认 0 ⇒ 产物与 0.6.3 逐字一致（这是"加字段不改行为"的判据）。
         */
        int firstFrame = 0;

        int margin = 0;   ///< 图集四边的留白（像素）
        int spacing = 0;  ///< 格与格之间的间隔（像素）

        /**
         * 帧表：每帧一个**精确像素矩形**（0.6.5 T-14）。非空时优先，网格字段一律不用。
         *
         * 为什么需要：等比网格覆盖大部分素材，但"一张大页里混装多个动作"必然出现
         * 帧宽高不等（跑步比待机宽、攻击比受伤高），`columns × frameWidth` 表达不了。
         *
         * 三条约定：
         *   - **与网格互斥**（`columns` / `rows` / `frameWidth` / `frameHeight` /
         *     `margin` / `spacing` / `firstFrame` 一个都不能给）：两套切法同时生效时
         *     画面会错位，而且错得"看起来像素材问题"，所以宁可报错；
         *   - **它是绝对的**：`CellRect(i)` 直接给 `frameRects[i]`，**不叠加**
         *     `firstFrame`（起点是网格的概念，"按帧给"没有起点）；
         *   - **帧数由它定**：`count` 会被 `Normalize()` 覆盖成 `frameRects.size()`，
         *     不给调用方"表和 count 各说一套"的机会。
         *
         * 不支持 TexturePacker 的 `spriteSourceSize`（帧内偏移）与 `rotated`——那要改
         * 绘制矩形原点与 UV，属于绘制层，任务书 §11 明确不做。
         */
        std::vector<ShapeBounds> frameRects;

        /// 有没有配过（任意一个几何字段非 0 就算配了）。
        bool IsConfigured() const noexcept {
            return !frameRects.empty() || columns > 0 || rows > 0 || frameWidth > 0
                   || frameHeight > 0 || count > 0;
        }

        /// 真正的帧数：`count > 0` 用它，否则 `rows × columns`（不合法时为 0）。
        int ResolveCount() const noexcept {
            if (count > 0) {
                return count;
            }
            if (columns > 0 && rows > 0) {
                return columns * rows;
            }
            return 0;
        }

        /**
         * 按这套切法，**整张图应该有多大**（像素）。
         *
         * `frameWidth × columns + spacing × (columns − 1) + 2 × margin`，
         * 高度同理。构造期拿真实图片尺寸与它比对，不一致就是 ERROR——
         * "格数与图宽不符"是图集最常见的配错，而它在画面上只表现为"图偏了一点"
         * 或"最后一格是上一格"，肉眼很难当场定位。
         *
         * 只在**放得下一整格**的前提下有意义；`columns/rows/frameWidth` 不合法时返回 0。
         */
        int SheetWidth() const noexcept {
            if (columns <= 0 || frameWidth <= 0) {
                return 0;
            }
            return frameWidth * columns + spacing * (columns - 1) + 2 * margin;
        }

        int SheetHeight() const noexcept {
            if (rows <= 0 || frameHeight <= 0) {
                return 0;
            }
            return frameHeight * rows + spacing * (rows - 1) + 2 * margin;
        }

        /**
         * 第 `index` 帧在整张图上的像素矩形。
         *
         * **帧表非空时直接取表**（0.6.5 T-14，绝对矩形、不叠加 `firstFrame`），
         * 否则按网格算（**行优先**：先横着走完一行）。
         *
         * 几何算术**只在这里**做一次：`InkingSprite` 每帧拿它去设源矩形，
         * 自检直接断言它（不必为了验 margin/spacing 真画一张图）。
         * 未配置 / 越界时返回全零矩形（调用方据此跳过，不画错块）。
         */
        ShapeBounds CellRect(std::size_t index) const noexcept {
            if (!frameRects.empty()) {
                return index < frameRects.size() ? frameRects[index] : ShapeBounds{};
            }
            if (frameWidth <= 0 || frameHeight <= 0 || columns <= 0) {
                return ShapeBounds{};
            }
            const std::size_t column = index % static_cast<std::size_t>(columns);
            const std::size_t row = index / static_cast<std::size_t>(columns);
            if (rows > 0 && row >= static_cast<std::size_t>(rows)) {
                return ShapeBounds{};
            }
            return ShapeBounds{
                static_cast<float>(margin
                                   + static_cast<int>(column) * (frameWidth + spacing)),
                static_cast<float>(margin
                                   + static_cast<int>(row) * (frameHeight + spacing)),
                static_cast<float>(frameWidth),
                static_cast<float>(frameHeight)};
        }
    };

    /**
     * 序列帧：每项是一张独立图（`AppearanceType::Image` 或 `Svg`）。
     *
     * 用 `ButtonAppearance` 而不是新造一个 "FrameAppearance"：帧就是"一张图 + 它
     * 自己的字节"，那正是 `ButtonAppearance` 在 `type == Image` 时的全部内容
     * （`imageBytes` / `imageSize` / `imageFit`）。
     */
    std::vector<ButtonAppearance> frames;

    /// 图集（`frames` 为空时才看它）。
    SpriteSheet sheet;

    /**
     * @brief 帧事件：播到第 `frame` 帧时发一个名字（0.6.3 T-09）。
     *
     * 用途是"动画里的关键点"——脚步声、命中判定、喷火的那一帧。它是**离散的
     * 时间点**，不是"第几帧长什么样"，所以不进 `frames` / `sheet`。
     *
     * 三条约定（都在 `InkingSprite::applyFrame` 里落实）：
     *   - **由帧号变化驱动**，不是"时间到点"驱动——慢帧一次跨 3 帧时，中间那几帧
     *     的事件**照样触发**（区间扫描），否则掉帧就等于吞事件；
     *   - **一帧可以挂多个事件**（同帧两条不同名字），也可以不挂；
     *   - 顺序**不要求**排好，扫描时按帧号升序逐帧查表（表很小，线性即可）。
     *
     * 越界（`frame >= 总帧数`）与空名字都归 `Validate()` 报错——这两件事没法
     * 安全地"猜"（改到哪一帧？叫什么名？）。
     */
    struct SpriteEvent {
        std::size_t frame = 0;  ///< 第几帧（0 起）
        std::string name;       ///< 事件名（给业务用，框架不解释）
    };

    std::vector<SpriteEvent> events;

    /**
     * 播放帧率。**默认 12**。
     *
     * 为什么默认是 12 而不是 24/30：手绘 / 像素素材的常识值是 12fps，而更重要的是
     * 这条默认值**不生效就会立刻被发现**——12fps 播得慢但看得见在动；如果默认成
     * "跟随渲染帧"（60fps），配错一个数量级反而看不出来。
     */
    double fps = 12.0;

    /// 循环播放（false = 播到末帧停住，`InkingSprite::IsFinished()` 为真）。
    bool loop = true;

    /// 构造后自动开始播（false = 停在 `startFrame`，等 `Play()`）。
    bool autoplay = true;

    /**
     * 起始帧。越界会被 `Normalize()` **夹到最后帧**并记 WARN（不是静默忽略）。
     *
     * 为什么夹取而不是报错：越界通常是"美术少交了几帧"，夹住还能看见画面；
     * 报错则整块空白——而空白最难查（`AGENTS.md` §7 那份清单的主题）。
     */
    std::size_t startFrame = 0;

    /**
     * 起始相位（秒）：实际时间是 `场景时钟 + startOffsetSeconds`。
     *
     * 用途是"让同屏两个 sprite 错开播放"（帧动画最常见的需求），不必各写一个
     * 起始帧。负值同样有意义（相当于反方向错相），不做夹取。
     */
    double startOffsetSeconds = 0.0;

    /// 用的是图集模式吗（`frames` 为空且 `sheet` 配过）。
    bool UsesSheet() const noexcept {
        return frames.empty() && sheet.IsConfigured();
    }

    /**
     * 帧数。序列帧 = `frames.size()`，图集 = `sheet.ResolveCount()`。
     *
     * **统一在这里算**：组件只认这一个数。两处各自算一遍是分叉的起点——
     * 尤其是"图集省略 count"这种默认值，散在两处就一定会有一处忘。
     */
    std::size_t FrameCount() const noexcept {
        if (!frames.empty()) {
            return frames.size();
        }
        const int resolved = sheet.ResolveCount();
        return resolved > 0 ? static_cast<std::size_t>(resolved) : 0u;
    }

    /**
     * 收尾：补齐默认值、夹取能修正的越界。**幂等**（重复调用结果不变）。
     *
     * 与 `ButtonData::Normalize` / `PanelData::Normalize` 同一条规矩：这类收尾只在
     * 一个地方做，不在渲染、命中、自检各判一次。生成器填完字段后调一次即可。
     *
     * **能修的就地修 + 记一条 WARN；修不了的留给 `Validate()` 报错**——两者分工
     * 在 `Validate()` 的注释里说明。
     */
    void Normalize() {
        // 先让基类把三态继承、全透明兜底这些走完（`sprite` 也吃这一套）。
        ImageData::Normalize();

        if (!sheet.frameRects.empty()) {
            // 帧表非空：帧数**由表定**（0.6.5 T-14）。`count` 是网格的"末尾几格是空的"
            // 那个口，与"按帧给矩形"没有共同语义——让两者各说一套只会带来"配了 3 帧
            // 却只播 2 帧"这种难查的错。
            sheet.count = static_cast<int>(sheet.frameRects.size());
        } else if (sheet.count <= 0 && sheet.columns > 0 && sheet.rows > 0) {
            sheet.count = sheet.columns * sheet.rows;
        }

        const std::size_t count = FrameCount();
        if (count > 0 && startFrame >= count) {
            INK_LOG_WARN(kSpriteModuleName,
                         "startFrame 越界（要第 " + std::to_string(startFrame)
                             + " 帧，共有 " + std::to_string(count)
                             + " 帧），已夹到最后一帧：" + name);
            startFrame = count - 1;
        }
    }

    /**
     * @brief 数据校验：返回**错误说明**，空串 = 通过。
     *
     * 分工（为什么不是 `Normalize()` 一个人干完）：
     *   - `Normalize()` 负责**能修正**的事（补默认、夹取、继承），修完记 WARN；
     *   - `Validate()` 负责**不能猜**的事（frames 与 sheet 都填 / 都不填、
     *     `fps <= 0`、图集几何自相矛盾），只报告、不改数据，所以是 `const`。
     *
     * 两者不重叠：一个字段要么"能被修正"，要么"只能被拒绝"，不存在既修又拒。
     *
     * 判据都用**原始字段**，不用 `FrameCount()` 兜底后的值——"没配"和"配成 0"
     * 都要能说清楚是哪一条。
     */
    std::string Validate() const {
        if (!frames.empty() && sheet.IsConfigured()) {
            return "同时配了 frames 与 sheet：帧动画只能二选一（序列帧 / 图集），"
                   "不替你猜哪个优先";
        }
        if (frames.empty() && !sheet.IsConfigured()) {
            return "既没配 frames 也没配 sheet：没有可播的帧";
        }
        if (!(fps > 0.0)) {
            // 写成 !(fps > 0) 而不是 fps <= 0：NaN 也要落到这条上。
            return "fps 必须 > 0（当前 " + std::to_string(fps) + "）";
        }

        if (!frames.empty()) {
            for (std::size_t i = 0; i < frames.size(); ++i) {
                const ButtonAppearance& frame = frames[i];
                if (frame.type == AppearanceType::Color) {
                    return "第 " + std::to_string(i)
                           + " 帧是纯色：帧必须是图片或 SVG";
                }
                if (frame.source.empty() && frame.imageBytes == nullptr) {
                    return "第 " + std::to_string(i) + " 帧没有图像内容（source 空）";
                }
            }
            return ValidateEvents();
        }

        // ---- 图集 ----
        // 帧表与网格**互斥**（0.6.5 T-14）：两套切法同时生效时 `CellRect` 只会用一套，
        // 另一套变成静默无效——正是"配了却不生效"的那类错，所以宁可报错。
        if (!sheet.frameRects.empty()) {
            if (sheet.columns > 0 || sheet.rows > 0 || sheet.frameWidth > 0
                || sheet.frameHeight > 0 || sheet.margin != 0 || sheet.spacing != 0) {
                return "图集的 frameRects 与网格切法（columns / rows / frameWidth / "
                       "frameHeight / margin / spacing）互斥：两套切法会打架，选一个";
            }
            if (sheet.firstFrame != 0) {
                return "图集的 frameRects 与 firstFrame 互斥：帧表是按帧给的，没有"
                       "\"起点\"这回事";
            }
            for (std::size_t i = 0; i < sheet.frameRects.size(); ++i) {
                const ShapeBounds& rect = sheet.frameRects[i];
                if (!(rect.width > 0.0f) || !(rect.height > 0.0f)) {
                    return "图集第 " + std::to_string(i) + " 帧的矩形宽高必须 > 0（当前 "
                           + std::to_string(rect.width) + "×"
                           + std::to_string(rect.height) + "）";
                }
                if (rect.x < 0.0f || rect.y < 0.0f) {
                    return "图集第 " + std::to_string(i) + " 帧的矩形左上角不能为负（当前 "
                           + std::to_string(rect.x) + "," + std::to_string(rect.y)
                           + "）";
                }
            }
            if (sheet.source.empty() && normal.source.empty()) {
                return "图集没有图：sheet.source 与 normal.source 都是空的";
            }
            return ValidateEvents();
        }

        if (sheet.frameWidth <= 0 || sheet.frameHeight <= 0) {
            return "图集的 frameWidth / frameHeight 必须 > 0（当前 "
                   + std::to_string(sheet.frameWidth) + "×"
                   + std::to_string(sheet.frameHeight) + "）";
        }
        if (sheet.columns <= 0 || sheet.rows <= 0) {
            return "图集的 columns / rows 必须 > 0（当前 "
                   + std::to_string(sheet.columns) + "×"
                   + std::to_string(sheet.rows) + "）";
        }
        if (sheet.count > sheet.columns * sheet.rows) {
            return "图集的 count（" + std::to_string(sheet.count)
                   + "）比 columns × rows（"
                   + std::to_string(sheet.columns * sheet.rows)
                   + "）还大：要的帧不存在";
        }
        // 起始格（0.6.4 T-11）。两个数都打出来：这一条最容易被写成"格号 = 帧数"
        // 反了（`firstFrame` 是格号、`count` 是帧数），只报一个数看不出反在哪。
        if (sheet.firstFrame < 0) {
            return "图集的 firstFrame 不能为负（当前 "
                   + std::to_string(sheet.firstFrame) + "）";
        }
        if (static_cast<long long>(sheet.firstFrame)
                + static_cast<long long>(sheet.ResolveCount())
            > static_cast<long long>(sheet.columns) * sheet.rows) {
            return "图集的起点与帧数超出格子总数：要 "
                   + std::to_string(sheet.ResolveCount()) + " 格但从第 "
                   + std::to_string(sheet.firstFrame) + " 格起，图集只有 "
                   + std::to_string(sheet.columns * sheet.rows) + " 格";
        }
        if (sheet.margin < 0 || sheet.spacing < 0) {
            return "图集的 margin / spacing 不能为负";
        }
        if (sheet.source.empty() && normal.source.empty()) {
            return "图集没有图：sheet.source 与 normal.source 都是空的";
        }
        // 注意这里**不验**"切出来的尺寸是否等于图宽"——那要图片的真实尺寸，数据层
        // 拿不到。这一条由 `InkingSprite` 构造期量一次图（`detail::ProbeImageSize`）
        // 与 `SheetWidth()/SheetHeight()` 比对，不符就 ERROR（T-05）。
        return ValidateEvents();
    }

    /**
     * 帧事件的校验（两条素材路共用，所以单独抽出来）。
     *
     * 判在**帧数确定之后**：越界与否要看总数，而总数由 `FrameCount()` 统一算。
     */
    std::string ValidateEvents() const {
        const std::size_t count = FrameCount();
        for (std::size_t i = 0; i < events.size(); ++i) {
            const SpriteEvent& event = events[i];
            if (event.name.empty()) {
                return "第 " + std::to_string(i) + " 条帧事件没有名字";
            }
            if (count > 0 && event.frame >= count) {
                return "帧事件 \"" + event.name + "\" 指向第 "
                       + std::to_string(event.frame) + " 帧，但只有 "
                       + std::to_string(count) + " 帧";
            }
        }
        return {};
    }
};

}  // namespace inking
