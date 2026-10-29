#include <image/InkingSprite.h>

#include <asset/InkingAsset.h>
#include <detail/InkLog.h>
#include <render/InkingText.h>
#include <scene/InkingScene.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace inking {

namespace {

/**
 * 帧边界比较的容差（秒）。
 *
 * `frame = floor(t × fps)` 在帧边界上是**脆弱**的：`t` 从 60Hz 的 `1/60` 累加上来，
 * 第 12 步得到 `1/12 ± 1e-17`，乘回 12 就可能是 `0.9999999999999998` —— 于是
 * "12fps 跑了 12 个 1/12" 会停在上一帧。1e-9 秒比任何真实帧间隔小 6 个数量级，
 * 放行它不会造成"提前一帧"，但能让边界判定稳定。
 *
 * 与 `InkingScene` 的动画订阅用同一个数量级，理由相同。
 */
constexpr double kSpriteEpsilon = 1e-9;

/// 一帧的结果：落在第几帧 + 是不是播完了。
struct FrameResolution {
    std::size_t index = 0;
    bool finished = false;
};

/**
 * 时间 → 帧号。**纯函数**，所以"两个同 fps 的 sprite 帧号恒等"是结构保证，
 * 不靠两边各自累加碰巧一致。
 */
FrameResolution resolveFrame(double seconds, std::size_t count, double fps,
                             bool loop) {
    if (count <= 1 || !(fps > 0.0)) {
        return FrameResolution{0u, false};
    }

    const double duration = static_cast<double>(count) / fps;

    if (loop) {
        // 先把时间夹进一个周期：长跑之后 `t × fps` 会落在 10^5 量级，
        // double 的小数位被吃掉，帧号就开始抖（"跑一小时之后动画开始抽"）。
        double wrapped = std::fmod(seconds, duration);
        if (wrapped < 0.0) {
            wrapped += duration;  // 负相位（startOffset 为负）回绕到正
        }
        std::size_t index =
            static_cast<std::size_t>(wrapped * fps + kSpriteEpsilon);
        if (index >= count) {
            index = count - 1;  // fmod 的边界抖动兜底
        }
        return FrameResolution{index, false};
    }

    // 非循环：早于起点停在首帧（startOffset 为负时会出现），过终点停在末帧。
    if (!(seconds > 0.0)) {
        return FrameResolution{0u, false};
    }
    if (seconds >= duration) {
        return FrameResolution{count - 1, true};
    }
    std::size_t index = static_cast<std::size_t>(seconds * fps + kSpriteEpsilon);
    if (index >= count) {
        index = count - 1;
    }
    return FrameResolution{index, false};
}

/**
 * 给基类的那份 `ImageData`：**序列帧模式下 `normal` 换成第 `startFrame` 帧**。
 *
 * 为什么不能直接把 `data` 切一刀传上去：
 *   - 基类构造里 `runtimeAsset == true` 会去读 `normal.source`——序列帧的
 *     `normal.source` 往往是空的（每帧自己带路径），那一读必然失败并打一条
 *     ERROR，看着像"配置错了"，其实只是"这张图没有单独的 normal"；
 *   - 换掉之后"构造完但还没收到第一次 tick"的第一帧也是对的画面，不是一块色块。
 */
ImageData baseImageData(const SpriteData& data) {
    ImageData base = static_cast<const ImageData&>(data);
    base.Normalize();

    if (data.UsesSheet()) {
        // 图集模式：字节就是那张大图。`sheet.source` 写了就用它——基类构造里
        // `runtimeAsset == true` 会读 `normal.source`，而配置常常只写 `sheet.source`
        // （切法是图集的事），不在这里对齐就会白打一条"运行期读图失败"。
        base.normal.source =
            data.sheet.source.empty() ? base.normal.source : data.sheet.source;
        return base;
    }
    if (data.frames.empty()) {
        return base;  // 既没 frames 也没 sheet：交给 Validate() 报错
    }

    // 夹取要在这里重算一次：基类构造早于本类的 `Normalize()`。
    SpriteData copy = data;
    copy.Normalize();
    const std::size_t index =
        std::min(copy.startFrame, copy.frames.size() - 1);
    base.normal = copy.frames[index];
    return base;
}

}  // namespace

InkingSprite::InkingSprite(InkingWidget* parent, const SpriteData& data)
    : InkingStaticImage(parent, baseImageData(data)) {
    _sprite = data;

    // 起始帧先落位（事件与计数都不计：`fromPlayback == false`）。
    //
    // 时间基准**不在这里对**：构造函数可能早于"进场景"（先 new 后 AddChild 是
    // 常见写法），那时 `nowSeconds()` 还是兜底的 0，拿它对出来的 `_pauseOffset`
    // 会在第一次 tick（场景时钟可能已经是 30 秒）时把帧号甩飞。
    // 所以基准是**惰性**的：第一次 tick / Play / Seek 时才对齐（见 `ensureTimeBase`）。
    rebuildFromData(_sprite.startFrame);

    // 播不播由配置定；`rebuildFromData()` 不碰它（换配置时"保持原播放状态"）。
    _playing = _sprite.autoplay;
}

void InkingSprite::rebuildFromData(std::size_t frame) {
    _sprite.Normalize();

    _configError.clear();
    _mode = FrameMode::Sequence;
    _configError = _sprite.Validate();
    if (!_configError.empty()) {
        INK_LOG_ERROR(kSpriteModuleName,
                      "帧动画配置不合法（" + _sprite.name + "）：" + _configError);
    } else if (_sprite.UsesSheet()) {
        // 图集：换帧只改源矩形（0.6.1 T-05）。几何对不对在**构造期**就要知道——
        // "格数与图宽不符"在画面上只表现为"图偏了一点 / 最后一格是上一格"，
        // 播放时才露头的错误最难定位。
        _mode = FrameMode::Sheet;
        validateSheetGeometry();
    }

    loadFrameBytes();

    if (!_frames.empty()) {
        applyFrame(std::min(frame, _frames.size() - 1), false);
    }
}

void InkingSprite::syncSheetBytes() {
    // 图集只有一份字节：`sheet.source` 写了就是它，否则沿用 `normal.source`
    // （与 `baseImageData()` 里那条"两处只该写一处"的约定同源）。
    const std::string& source = _sprite.sheet.source.empty()
                                    ? _sprite.normal.source
                                    : _sprite.sheet.source;
    if (_sprite.runtimeAsset) {
        // 失败时保留旧图（基类那条规矩）：`LoadAsset` 自己会记 ERROR。
        LoadAsset(source);
        return;
    }
    SetImageBytes(_sprite.normal.imageBytes, _sprite.normal.imageSize,
                  _sprite.normal.source);
}

void InkingSprite::SetSpriteData(const SpriteData& data, bool keepPhase) {
    const std::size_t previousFrame = _frameIndex;
    const bool wasPlaying = _playing;

    _sprite = data;

    // 字节先对齐、再重建：`validateSheetGeometry()` 量的是"绘制层现在那份字节"，
    // 顺序反了就会拿旧图去校验新图集的格数（很可能"恰好"通过，于是错得更隐蔽）。
    if (_sprite.UsesSheet()) {
        syncSheetBytes();
    }

    // 换配置后的第一次落帧**不算换帧**：没有"旧帧"这回事，发 `frameChanged`
    // 就是一个假事件。时间基准也要重对（帧号/图集都换了，旧相位没有意义）。
    _applied = false;
    _offsetReady = false;
    _finished = false;
    _frameIndex = 0;

    // 落帧交给 `rebuildFromData`：**不走 `SeekToFrame`**——后者会发一次
    // `frameChanged`（"画面确实变了"），而换配置的第一次落帧没有"旧帧"这回事，
    // 发出去订阅者会以为播过了一帧（自检抓过这一条）。
    rebuildFromData(keepPhase ? previousFrame : 0u);

    // 帧号定下来之后再对齐时间基准：于是"换完接着播"从这一帧走，不会因为旧相位
    // 在下一帧直接跳走（`_offsetReady` 在进来时已经被清掉）。
    ensureTimeBase();

    // 播放状态保持原值；但"没有可播的帧"时不能留在"正在播"（那是空转）。
    _playing = wasPlaying && !_frames.empty();
    if (!_playing) {
        _pauseStartSeconds = nowSeconds();
    }
}


void InkingSprite::ensureTimeBase(double alreadyElapsedSeconds) {
    if (_offsetReady) {
        return;
    }
    // 退回到"这一帧开始之前的时钟读数"：第一次 tick 就能推进一帧，
    // 否则每段播放都会白丢一帧（"动画第一帧好像卡了一下"）。
    const double elapsed =
        alreadyElapsedSeconds > 0.0 ? alreadyElapsedSeconds : 0.0;
    const double base = nowSeconds() - elapsed;

    // 让 `base` 那一刻正好落在 `_frameIndex` 上：定位语义的时间基准，
    // 构造 / 首次 tick / Play / Seek 全走同一条。
    _pauseOffset = base + _sprite.startOffsetSeconds
                   - static_cast<double>(_frameIndex) / fps();
    _pauseStartSeconds = base;
    _offsetReady = true;
}

// ---------------------------------------------------------------------------
// 构造期的字节预热 / 几何校验
// ---------------------------------------------------------------------------

void InkingSprite::validateSheetGeometry() {
    const SpriteData::SpriteSheet& sheet = _sprite.sheet;
    const bool byRects = !sheet.frameRects.empty();
    const int expectedWidth = sheet.SheetWidth();
    const int expectedHeight = sheet.SheetHeight();
    if (!byRects && (expectedWidth <= 0 || expectedHeight <= 0)) {
        // `Validate()` 已经拦过"几何字段 <= 0"，走到这里说明是 count/count 之类的
        // 边角组合（比如只配了 columns 没配 rows）。不重复报同一条错。
        return;
    }

    // 借绘制层"当前生效的那份字节"：运行期读进来的在 `_assetBytes` 里，
    // 生成期烘的直接指向静态数组——两种都由 `GetAppearance()` 统一视图给出。
    const ButtonAppearance& appearance = GetAppearance();
    if (!_sprite.runtimeAsset
        && (appearance.imageBytes == nullptr || appearance.imageSize == 0)) {
        // 生成期那条路：图集必须把 `sheet.source` 那份文件烘进 `normal.imageBytes`。
        // 没有字节的话绘制层会**静默退回纯色填**（"图集变成一块底色"），
        // 而配置层完全看不出问题——所以在构造期就拦下来。
        // 运行期那条路不重复报：基类构造里 `LoadAsset` 失败已经报过一条。
        _configError =
            "图集没有字节：请把整张图集烘进 normal.imageBytes（sheet.source 只是路径）";
        INK_LOG_ERROR(std::string(kSpriteModuleName),
                      "图集没有可切的字节（" + _sprite.name + "）："
                          + _configError);
        return;
    }
    const ImageBytes image{appearance.imageBytes, appearance.imageSize,
                           appearance.source};
    int imageWidth = 0;
    int imageHeight = 0;
    if (!detail::ProbeImageSize(image, &imageWidth, &imageHeight)) {
        // 解不出来（没接 SDL3_image / 字节坏了 / 是 SVG）：**不假装通过**，
        // 但也别拦着播——这台机器上整条图片路都画不出来，报两条错只是噪音。
        INK_LOG_WARN(std::string(kSpriteModuleName),
                     "量不到图集尺寸，切格几何未校验：" + _sprite.name);
        return;
    }

    if (byRects) {
        // 帧表那条路（0.6.5 T-14）：没有"整张应该多大"这回事，只能逐帧验**落在图内**。
        // 越界帧在画面上表现为"取到图外 → 一整块拉伸的边"或者干脆不画，很难归因到
        // 某一帧，所以文案把帧号与矩形都写出来。
        for (std::size_t i = 0; i < sheet.frameRects.size(); ++i) {
            const ShapeBounds& rect = sheet.frameRects[i];
            if (rect.x + rect.width <= static_cast<float>(imageWidth)
                && rect.y + rect.height <= static_cast<float>(imageHeight)) {
                continue;
            }
            _configError =
                "第 " + std::to_string(i) + " 帧的矩形 x=" + std::to_string(rect.x)
                + " y=" + std::to_string(rect.y) + " w="
                + std::to_string(rect.width) + " h=" + std::to_string(rect.height)
                + " 超出图片 " + std::to_string(imageWidth) + "×"
                + std::to_string(imageHeight);
            INK_LOG_ERROR(std::string(kSpriteModuleName),
                          "图集帧表越界（" + _sprite.name + "）：" + _configError);
            return;
        }
        return;
    }

    if (imageWidth == expectedWidth && imageHeight == expectedHeight) {
        return;
    }

    _configError = "图集尺寸对不上：真实图片是 " + std::to_string(imageWidth) + "×"
                   + std::to_string(imageHeight) + "，按 frameWidth="
                   + std::to_string(sheet.frameWidth) + " / columns="
                   + std::to_string(sheet.columns) + " / rows="
                   + std::to_string(sheet.rows) + " / spacing="
                   + std::to_string(sheet.spacing) + " / margin="
                   + std::to_string(sheet.margin) + " 算出来应是 "
                   + std::to_string(expectedWidth) + "×"
                   + std::to_string(expectedHeight);
    INK_LOG_ERROR(std::string(kSpriteModuleName),
                  "图集切格不成立（" + _sprite.name + "）：" + _configError);
}

void InkingSprite::loadFrameBytes() {
    _frames.clear();
    _frameBytes.clear();
    _loadedFrameCount = 0;

    if (_mode == FrameMode::Sheet) {
        // 图集：所有帧共用同一份字节（`normal` 那张大图），差别只在源矩形。
        // `data` 留空 ⇒ `applyFrame` 不会调 `SetImageBytes`（那会把整张图集
        // 每帧拷一遍），只改源矩形。
        const std::size_t count = _sprite.FrameCount();
        // "帧号 → 格号"的换算**只在这里**做一次（0.6.4 T-11）：`CellRect` 的语义
        // 仍是绝对格号，`firstFrame` 只影响起点。负值在这里夹成 0（合法性由
        // `Validate()` 报过；这条只是不让 size_t 转换把 0 变成天文数字）。
        const std::size_t first =
            _sprite.sheet.firstFrame > 0
                ? static_cast<std::size_t>(_sprite.sheet.firstFrame)
                : 0u;
        // 帧表那条路**不叠加起点**（`firstFrame` 与 `frameRects` 互斥，这里写清楚
        // 是为了以后别把两者"顺手"合到一起）。
        const bool byRects = !_sprite.sheet.frameRects.empty();
        _frames.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            _frames[i].sourceRect =
                _sprite.sheet.CellRect(byRects ? i : first + i);
            if (_frames[i].sourceRect.width <= 0.0f
                || _frames[i].sourceRect.height <= 0.0f) {
                // 越界格（`count > rows×columns` 已被拦，但 count 与几何不一致时
                // 仍可能算不出矩形）：跳过这一格，画面停在上一帧。
                INK_LOG_ERROR(std::string(kSpriteModuleName),
                              "第 " + std::to_string(i) + " 帧切不出矩形（"
                                  + _sprite.name + "）：检查 columns / rows");
                continue;
            }
            ++_loadedFrameCount;
        }
        return;
    }

    const std::size_t count = _sprite.frames.size();
    if (count == 0) {
        return;  // 配置不合法：没有可播的帧
    }

    // **先定尺寸再取指针**：`_frameBytes` 一旦扩容，先前取到的 `data()` 就悬垂。
    // 用 `resize + 按下标赋值` 而不是 `push_back`，就是为了这条。
    if (_sprite.runtimeAsset) {
        _frameBytes.resize(count);
    }
    _frames.resize(count);

    for (std::size_t i = 0; i < count; ++i) {
        const ButtonAppearance& source = _sprite.frames[i];
        LoadedFrame& frame = _frames[i];

        frame.name = source.source.empty()
                         ? _sprite.name + "#" + std::to_string(i)
                         : source.source;

        if (_sprite.runtimeAsset) {
            // 运行期读：一次性全读进来（构造期一次 I/O，换帧零 I/O）。
            _frameBytes[i] = AssetLoad(source.source);
            if (_frameBytes[i].empty()) {
                // 单帧失败**不中止**：其余帧照播，这一帧停在上一帧的画面上。
                // 中止会让整个组件不画——"一块空白"比"少一帧"难查得多。
                INK_LOG_ERROR(std::string(kSpriteModuleName),
                              "第 " + std::to_string(i) + " 帧读不到："
                                  + source.source + "（资源根 " + AssetRoot()
                                  + "）");
                continue;
            }
            frame.data = _frameBytes[i].data();
            frame.size = _frameBytes[i].size();
            ++_loadedFrameCount;
            continue;
        }

        // 生成期烘的字节：**只借视图不拷贝**，静态数组的生命周期是整个进程。
        frame.data = source.imageBytes;
        frame.size = source.imageSize;
        if (frame.data == nullptr || frame.size == 0) {
            INK_LOG_ERROR(std::string(kSpriteModuleName),
                          "第 " + std::to_string(i) + " 帧没有字节（"
                              + _sprite.name + "）");
            continue;
        }
        ++_loadedFrameCount;
    }
}

// ---------------------------------------------------------------------------
// 播放
// ---------------------------------------------------------------------------

bool InkingSprite::IsPlaying() const noexcept { return _playing; }
bool InkingSprite::IsFinished() const noexcept { return _finished; }

std::size_t InkingSprite::GetFrameIndex() const noexcept { return _frameIndex; }

std::size_t InkingSprite::GetFrameCount() const noexcept {
    return _frames.size();
}

std::size_t InkingSprite::GetLoadedFrameCount() const noexcept {
    return _loadedFrameCount;
}

std::uint64_t InkingSprite::GetFrameAdvanceCount() const noexcept {
    return _frameAdvanceCount;
}

std::uint64_t InkingSprite::GetSkippedFrameCount() const noexcept {
    return _skippedFrameCount;
}

double InkingSprite::GetAnimationTime() const noexcept {
    // 暂停中就把"这一段还没结算的暂停"先扣掉（`Play()` 时才并进 `_pauseOffset`）。
    // 于是这个口的语义是"**已经播了多久**"，不是"时钟走了多久"——两者在暂停期间
    // 正好差一段，而使用者（进度条 / 自检）要的显然是前者。
    double pending = 0.0;
    if (!_playing && !_finished) {
        pending = nowSeconds() - _pauseStartSeconds;
        if (pending < 0.0) {
            pending = 0.0;
        }
    }
    return nowSeconds() - _pauseOffset - pending + _sprite.startOffsetSeconds;
}

const SpriteData& InkingSprite::GetSpriteData() const noexcept { return _sprite; }

const std::string& InkingSprite::GetConfigError() const noexcept {
    return _configError;
}

bool InkingSprite::IsValid() const noexcept { return _configError.empty(); }

bool InkingSprite::IsSheetMode() const noexcept {
    return _mode == FrameMode::Sheet;
}

void InkingSprite::Play() {
    ensureTimeBase();
    if (_playing || _frames.empty()) {
        return;
    }

    const double now = nowSeconds();
    if (_finished) {
        // 播完再播 = 从头再来（时间基准重算，不是"接着最后一帧"）。
        _finished = false;
        _pauseOffset = now + _sprite.startOffsetSeconds;
        applyFrame(0, false);
    } else {
        // 把暂停的这段时间从时间轴里扣掉：恢复后仍在原帧，不"跳过去"。
        _pauseOffset += now - _pauseStartSeconds;
    }
    _playing = true;
}

void InkingSprite::Pause() {
    ensureTimeBase();
    if (!_playing) {
        return;
    }
    _playing = false;
    _pauseStartSeconds = nowSeconds();
}

void InkingSprite::Stop() {
    ensureTimeBase();
    _playing = false;
    _finished = false;
    _pauseStartSeconds = nowSeconds();
    if (!_frames.empty()) {
        SeekToFrame(std::min(_sprite.startFrame, _frames.size() - 1));
    }
}

bool InkingSprite::SeekToFrame(std::size_t frame) {
    if (_frames.empty()) {
        return false;
    }
    ensureTimeBase();

    if (frame >= _frames.size()) {
        frame = _frames.size() - 1;
    }

    // 反解 `_pauseOffset`：让 `t = frame / fps`，于是"定位之后立刻恢复播放"
    // 从这一帧接着走，而不是从定位前的相位继续（那是两件事）。
    _pauseOffset = nowSeconds() + _sprite.startOffsetSeconds
                   - static_cast<double>(frame) / fps();
    _finished = false;
    if (!_playing) {
        _pauseStartSeconds = nowSeconds();
    }

    applyFrame(frame, false);
    return true;
}

// ---------------------------------------------------------------------------
// 每帧推进
// ---------------------------------------------------------------------------

void InkingSprite::onAnimationTick(float deltaSeconds) {
    // 兜底时间：组件被摘出场景（`GetScene()` 为空）时仍能自己走。
    // 有场景时**不用它**——同一场景里的所有 sprite 必须读同一个时钟，
    // 各累加各的会慢慢错开（那正是 T-01 要解决的问题）。
    if (deltaSeconds > 0.0f) {
        _fallbackSeconds += static_cast<double>(deltaSeconds);
    }

    if (!_playing || _finished || _frames.empty()) {
        return;
    }

    ensureTimeBase(deltaSeconds);
    const FrameResolution resolution =
        resolveFrame(GetAnimationTime(), _frames.size(), fps(), _sprite.loop);

    if (resolution.index != _frameIndex) {
        applyFrame(resolution.index, true);
    }
    if (resolution.finished) {
        // 先落末帧、再置 finished：订阅者在 `frameChanged` 里能读到"已到末帧但
        // 还没结束"，下一帧才是"结束"。两件事拆开，回调和状态才都说得清。
        //
        // **同时清 `_playing`**：播完就是"不在播了"。留着 `_playing == true`
        // 会让 `Play()` 在开头早退（"播完再播"变成空操作），自检抓过这一条。
        _finished = true;
        _playing = false;
        _pauseStartSeconds = nowSeconds();
        // 状态先落定再发：回调里问 `IsFinished()` / `IsPlaying()` 拿到的是
        // "已经播完了"，不是"正在播最后一下"。
        finished.Emit();
    }
}

void InkingSprite::fireEventsBetween(std::size_t previous,
                                     std::size_t current) {
    // 没配事件就是**零开销**（每个换帧都会走到这里）。
    if (_sprite.events.empty() || _frames.empty()) {
        return;
    }
    const std::size_t count = _frames.size();
    const auto fire = [this](std::size_t frame) {
        for (const SpriteData::SpriteEvent& event : _sprite.events) {
            if (event.frame == frame) {
                frameEvent.Emit(event.name, event.frame);
            }
        }
    };
    if (current >= previous) {
        for (std::size_t frame = previous + 1; frame <= current; ++frame) {
            fire(frame);
        }
        return;
    }
    // 回绕（3 → 0）：区间不连续，拆成 `(previous, 末帧]` 与 `[0, current]` 两段。
    // 用 `current < previous` 当判据，而不是"是不是循环"——Seek 造成的倒退
    // 根本走不到这里（只有 `fromPlayback` 才发）。
    for (std::size_t frame = previous + 1; frame < count; ++frame) {
        fire(frame);
    }
    for (std::size_t frame = 0; frame <= current; ++frame) {
        fire(frame);
    }
}

void InkingSprite::applyFrame(std::size_t index, bool fromPlayback) {
    if (index >= _frames.size()) {
        return;
    }

    const LoadedFrame& frame = _frames[index];
    const bool hadFrame = _applied;
    const std::size_t previous = _frameIndex;

    _frameIndex = index;
    _applied = true;

    // 换画面：序列帧换字节视图，图集换源矩形。**两者只在这里分一次**。
    //
    // 序列帧：`SetImageBytes` 会拷一份（KB 级）并标重绘脏——这是"不改基类
    // 访问控制"换来的代价，任务书里认过这一笔。读失败的帧 `data` 为空，
    // 这里跳过设置：画面停在上一张成功的图上（不是一块空白）。
    if (frame.data != nullptr && frame.size > 0) {
        SetImageBytes(frame.data, frame.size, frame.name);
    }
    if (_mode == FrameMode::Sheet) {
        // 切不出矩形的帧（几何不一致）不动源矩形：停在上一帧的画面上。
        if (frame.sourceRect.width > 0.0f && frame.sourceRect.height > 0.0f) {
            SetRenderSourceRect(frame.sourceRect);
        }
    }

    if (!hadFrame) {
        // 第一次落帧：没有"旧帧号"，事件与计数都不该出现。
        return;
    }
    if (index == previous) {
        return;
    }

    if (fromPlayback) {
        ++_frameAdvanceCount;
        if (index > previous) {
            // 只统计**向前跨过**的帧：回绕（3 → 0）与倒退（Seek）不是"掉帧"。
            _skippedFrameCount +=
                static_cast<std::uint64_t>(index - previous - 1);
        }
        // 帧事件先发、`frameChanged` 后发：订阅者收到"换帧"时，这一帧的关键点
        // 已经处理完了（顺序反过来，回调里读到的状态就和事件对不上）。
        fireEventsBetween(previous, index);
    }
    frameChanged.Emit(index, previous);
}

double InkingSprite::nowSeconds() const noexcept {
    const InkingScene* scene = GetScene();
    return scene != nullptr ? scene->AnimationSeconds() : _fallbackSeconds;
}

double InkingSprite::fps() const noexcept {
    return _sprite.fps > 0.0 ? _sprite.fps : 12.0;
}

}  // namespace inking
