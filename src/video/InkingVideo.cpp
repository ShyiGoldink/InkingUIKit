#include <video/InkingVideo.h>

#include <asset/InkingAsset.h>
#include <audio/InkingAudio.h>
#include <button/ButtonPaint.h>
#include <detail/InkLog.h>
#include <render/InkingDraw.h>
#include <detail/InkingHit.h>
#include <dataStruct/InkingImageFit.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(INK_HAS_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

namespace inking {

namespace {

#if defined(INK_HAS_FFMPEG)

/// 视频帧队列上限：3 帧。太大 = 内存与操作延迟，太小 = worker 频繁等（解码不连续）。
constexpr std::size_t kMaxQueuedFrames = 3;

/// 音频块队列上限：约 1 秒（48k 立体声 S16 ⇒ 192000 字节/秒）。
constexpr std::size_t kMaxQueuedAudioBytes = 192000;

/// 追帧：队首落后主时钟超过这么多秒、**且后面还有更新的帧**，就丢掉它。
constexpr double kDropThresholdSeconds = 0.06;

/// 提前量：pts 进入"主时钟 + 这个值"就该上屏（免得永远差一帧）。
constexpr double kPresentAheadSeconds = 0.005;

/// 音频一律转成这个格式交给音频层（S16 交错 / 48k / 立体声）。
constexpr int kAudioOutputRate = 48000;
constexpr int kAudioOutputChannels = 2;

/// `VideoFit` → 图片那套 `ImageFit`（形状层只认后者，语义一一对应）。
ImageFit ToImageFit(VideoFit fit) {
    switch (fit) {
        case VideoFit::Contain:
            return ImageFit::Contain;
        case VideoFit::Cover:
            return ImageFit::Cover;
        case VideoFit::Fill:
            break;
    }
    return ImageFit::Fill;
}

#endif  // INK_HAS_FFMPEG

/**
 * 把"相对资源根的 source"解析成**绝对路径**；找不到返回空串。
 *
 * 与"解码器开没开"分开：素材不在和没后端是两种失败，混成一句话时排错只能猜
 * （`IsOpened()` 与 `AssetExists()` 两个观测口就是为这件事分的）。
 */
std::string ResolveVideoPath(const std::string& relative) {
    if (relative.empty()) {
        return {};
    }
    std::error_code ec;
    const std::filesystem::path given(relative);
    if (given.is_absolute()) {
        return std::filesystem::exists(given, ec) ? given.string() : std::string{};
    }
    const std::filesystem::path full = std::filesystem::path(AssetRoot()) / given;
    return std::filesystem::exists(full, ec) ? full.string() : std::string{};
}

}  // namespace

// ---------------------------------------------------------------------------
// 状态
//
// 两档共用一份 `Impl` 的**主线程状态**；解码侧（FFmpeg 对象、队列、线程）只在接了
// FFmpeg 时才有——没接时 `opened` 恒为 false，所有播控都是安全空操作，画面退回
// 兜底色（"能编、能跑、不假装在播"，见 InkingVideo.h 的类注释）。
// ---------------------------------------------------------------------------
struct InkingStaticVideo::Impl {
    // ---- 主线程状态（两档共用）----
    bool opened = false;
    bool playing = false;
    bool finished = false;
    bool hasAudio = false;
    double duration = 0.0;
    int videoWidth = 0;
    int videoHeight = 0;
    float volume = 1.0f;
    bool muted = false;
    bool loop = false;
    std::string path;  ///< 解析出来的绝对路径（可能为空 = 文件不在）

    /// 主时钟：**音频位置 + `clockBase`**；没音轨 / 没音频后端时退化成真实时间。
    double clock = 0.0;
    double clockBase = 0.0;
    bool clockStarted = false;
    std::chrono::steady_clock::time_point lastPump{};

    PcmStreamId pcm = kInvalidPcmStream;  ///< 音频层那条外部 PCM 流
    bool pcmPlaying = false;

    /// 画面纹理（属于渲染器，见 AGENTS §6 第 46 条）。
    SDL_Texture* texture = nullptr;
    SDL_Renderer* textureRenderer = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;
    bool textureHasFrame = false;

    // ---- 观测口 ----
    /// 解出的帧数：**worker 写、主线程读**，所以是原子的（第 40 条那条"过程对不对
    /// 只有计数说得清"的另一面——计数自己也得线程安全）。
    std::atomic<std::uint32_t> decoded{0};
    std::uint32_t presented = 0;  ///< 上屏帧数（只在主线程改）
    std::uint32_t dropped = 0;    ///< 追帧丢掉的帧数（只在主线程改）

#if defined(INK_HAS_FFMPEG)
    // ---- 解码侧：AVFormatContext / 编解码器只在 worker 里动 ----
    AVFormatContext* format = nullptr;
    AVCodecContext* videoCodec = nullptr;
    AVCodecContext* audioCodec = nullptr;
    SwsContext* scaler = nullptr;
    SwrContext* resampler = nullptr;
    int scalerWidth = 0;   ///< 缩放器是按这个尺寸建的（**不是**纹理尺寸，两者是两回事）
    int scalerHeight = 0;
    int videoStream = -1;
    int audioStream = -1;
    AVRational videoTimeBase{1, 1000};

    struct VideoFrame {
        std::vector<std::uint8_t> bgra;
        double pts = 0.0;
    };
    struct AudioChunk {
        std::vector<std::uint8_t> bytes;  // S16 交错 / 48k / 立体声
        double pts = 0.0;
    };

    std::deque<VideoFrame> frames;  ///< worker 生产、主线程消费
    std::deque<AudioChunk> audio;
    std::size_t audioBytes = 0;
    std::mutex mutex;
    std::condition_variable frameRoom;  ///< 队列腾出位置了（worker 等它）
    std::condition_variable audioRoom;

    std::thread worker;
    std::atomic<bool> quit{false};
    std::atomic<bool> seekPending{false};
    std::atomic<double> seekTarget{0.0};
    std::atomic<bool> atEnd{false};   ///< 读到末尾（非循环）
    std::atomic<bool> looped{false};  ///< 循环重来了：主线程要重置音频
#endif
};

#if defined(INK_HAS_FFMPEG)

namespace {

/// 打开文件 + 两个解码器。**同步**做（第一帧之前要知道时长 / 尺寸）。
bool OpenVideo(InkingStaticVideo::Impl& impl, const std::string& path) {
    if (avformat_open_input(&impl.format, path.c_str(), nullptr, nullptr) < 0) {
        INK_LOG_ERROR(std::string(kVideoModuleName),
                      "打不开视频：" + path + "（格式不认识 / 文件坏了）");
        return false;
    }
    if (avformat_find_stream_info(impl.format, nullptr) < 0) {
        INK_LOG_ERROR(std::string(kVideoModuleName), "读不出流信息：" + path);
        return false;
    }

    const AVCodec* videoCodec = nullptr;
    impl.videoStream = av_find_best_stream(impl.format, AVMEDIA_TYPE_VIDEO, -1, -1,
                                           &videoCodec, 0);
    if (impl.videoStream < 0 || videoCodec == nullptr) {
        INK_LOG_ERROR(std::string(kVideoModuleName), "没有视频流：" + path);
        return false;
    }
    impl.videoCodec = avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(
        impl.videoCodec, impl.format->streams[impl.videoStream]->codecpar);
    if (avcodec_open2(impl.videoCodec, videoCodec, nullptr) < 0) {
        INK_LOG_ERROR(std::string(kVideoModuleName),
                      std::string("视频解码器打不开：") + path);
        return false;
    }
    impl.videoTimeBase = impl.format->streams[impl.videoStream]->time_base;
    impl.videoWidth = impl.videoCodec->width;
    impl.videoHeight = impl.videoCodec->height;

    impl.audioStream =
        av_find_best_stream(impl.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (impl.audioStream >= 0) {
        const AVCodec* audioCodec = avcodec_find_decoder(
            impl.format->streams[impl.audioStream]->codecpar->codec_id);
        if (audioCodec != nullptr) {
            impl.audioCodec = avcodec_alloc_context3(audioCodec);
            avcodec_parameters_to_context(
                impl.audioCodec, impl.format->streams[impl.audioStream]->codecpar);
            if (avcodec_open2(impl.audioCodec, audioCodec, nullptr) < 0) {
                // 音轨解不开不影响画面：丢掉它，只播视频。
                avcodec_free_context(&impl.audioCodec);
                impl.audioStream = -1;
            }
        } else {
            impl.audioStream = -1;
        }
    }
    impl.hasAudio = impl.audioStream >= 0;

    if (impl.format->duration > 0) {
        impl.duration =
            static_cast<double>(impl.format->duration) / AV_TIME_BASE;
    }
    return true;
}

/// seek 到某一秒（worker 里调）。
void SeekTo(InkingStaticVideo::Impl& impl, double seconds) {
    if (impl.format == nullptr || impl.videoStream < 0) {
        return;
    }
    const int64_t timestamp = static_cast<int64_t>(
        seconds / av_q2d(impl.videoTimeBase));
    const int seekResult =
        av_seek_frame(impl.format, impl.videoStream, timestamp, AVSEEK_FLAG_BACKWARD);
    if (seekResult < 0) {
        // seek 失败要**说出来**：不说的话表现成"位置到了、画面还在后面慢慢追"，
        // 很难往 seek 上想（实测踩过：末尾诊断显示"读到末尾 否"）。
        INK_LOG_WARN(std::string(kVideoModuleName),
                     "seek 到 " + std::to_string(seconds) + " 秒失败（"
                         + std::to_string(seekResult) + "）");
    }
    if (impl.videoCodec != nullptr) {
        avcodec_flush_buffers(impl.videoCodec);
    }
    if (impl.audioCodec != nullptr) {
        avcodec_flush_buffers(impl.audioCodec);
    }
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        impl.frames.clear();
        impl.audio.clear();
        impl.audioBytes = 0;
    }
    impl.atEnd.store(false);
    impl.frameRoom.notify_all();
    impl.audioRoom.notify_all();
}

/// 把一帧解码结果转成 BGRA 并塞进队列（塞不下就等——这就是对 worker 的背压）。
void PushVideoFrame(InkingStaticVideo::Impl& impl, AVFrame* frame) {
    const int width = frame->width > 0 ? frame->width : impl.videoWidth;
    const int height = frame->height > 0 ? frame->height : impl.videoHeight;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (impl.scaler == nullptr || impl.scalerWidth != width
        || impl.scalerHeight != height) {
        if (impl.scaler != nullptr) {
            sws_freeContext(impl.scaler);
        }
        impl.scaler = sws_getContext(
            width, height, static_cast<AVPixelFormat>(frame->format), width, height,
            AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        impl.scalerWidth = width;
        impl.scalerHeight = height;
    }
    if (impl.scaler == nullptr) {
        return;
    }

    InkingStaticVideo::Impl::VideoFrame out;
    out.bgra.resize(static_cast<std::size_t>(width) * height * 4);
    std::uint8_t* dst[4] = {out.bgra.data(), nullptr, nullptr, nullptr};
    int dstLinesize[4] = {width * 4, 0, 0, 0};
    sws_scale(impl.scaler, frame->data, frame->linesize, 0, height, dst, dstLinesize);
    out.pts = frame->pts == AV_NOPTS_VALUE
                  ? 0.0
                  : static_cast<double>(frame->pts) * av_q2d(impl.videoTimeBase);

    {
        std::unique_lock<std::mutex> lock(impl.mutex);
        impl.frameRoom.wait(lock, [&impl] {
            return impl.quit.load() || impl.frames.size() < kMaxQueuedFrames;
        });
        if (impl.quit.load()) {
            return;
        }
        impl.frames.push_back(std::move(out));
    }
    ++impl.decoded;
    impl.frameRoom.notify_all();
}

/// 把一帧音频解码结果转成 S16 交错 / 48k / 立体声并塞进队列。
void PushAudioFrame(InkingStaticVideo::Impl& impl, AVFrame* frame) {
    if (impl.audioCodec == nullptr) {
        return;
    }
    if (impl.resampler == nullptr) {
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, kAudioOutputChannels);
        if (swr_alloc_set_opts2(&impl.resampler, &outLayout, AV_SAMPLE_FMT_S16,
                                kAudioOutputRate, &impl.audioCodec->ch_layout,
                                impl.audioCodec->sample_fmt,
                                impl.audioCodec->sample_rate, 0, nullptr)
            < 0) {
            av_channel_layout_uninit(&outLayout);
            return;
        }
        av_channel_layout_uninit(&outLayout);
        if (swr_init(impl.resampler) < 0) {
            swr_free(&impl.resampler);
            return;
        }
    }

    const int capacity = swr_get_out_samples(impl.resampler, frame->nb_samples);
    if (capacity <= 0) {
        return;
    }
    InkingStaticVideo::Impl::AudioChunk out;
    out.bytes.resize(static_cast<std::size_t>(capacity) * kAudioOutputChannels
                     * sizeof(std::int16_t));
    std::uint8_t* dst[1] = {out.bytes.data()};
    const int got = swr_convert(
        impl.resampler, dst, capacity,
        const_cast<const std::uint8_t**>(frame->extended_data), frame->nb_samples);
    if (got <= 0) {
        return;
    }
    out.bytes.resize(static_cast<std::size_t>(got) * kAudioOutputChannels
                     * sizeof(std::int16_t));
    out.pts = frame->pts == AV_NOPTS_VALUE
                  ? 0.0
                  : static_cast<double>(frame->pts)
                        * av_q2d(impl.format->streams[impl.audioStream]->time_base);

    {
        std::unique_lock<std::mutex> lock(impl.mutex);
        impl.audioRoom.wait(lock, [&impl] {
            return impl.quit.load() || impl.audioBytes < kMaxQueuedAudioBytes;
        });
        if (impl.quit.load()) {
            return;
        }
        impl.audioBytes += out.bytes.size();
        impl.audio.push_back(std::move(out));
    }
    impl.audioRoom.notify_all();
}

/// worker：只做"读包 + 解码 + 入队"，**绝不碰播放状态**（AGENTS §6 第 82 条）。
void DecodeLoop(InkingStaticVideo::Impl& impl, bool loop) {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    while (!impl.quit.load()) {
        if (impl.seekPending.exchange(false)) {
            SeekTo(impl, impl.seekTarget.load());
        }
        // 队列满了就等（主线程消费之后会通知）。
        {
            std::unique_lock<std::mutex> lock(impl.mutex);
            impl.frameRoom.wait_for(lock, std::chrono::milliseconds(50), [&impl] {
                return impl.quit.load() || impl.frames.size() < kMaxQueuedFrames;
            });
        }
        if (impl.quit.load()) {
            break;
        }
        {
            std::lock_guard<std::mutex> lock(impl.mutex);
            if (impl.frames.size() >= kMaxQueuedFrames) {
                continue;
            }
        }

        const int ret = av_read_frame(impl.format, packet);
        if (ret < 0) {
            if (loop) {
                SeekTo(impl, 0.0);
                impl.looped.store(true);
                continue;
            }
            impl.atEnd.store(true);
            // 不退出线程：等 seek（或 quit）。退出的话每次 seek 都要重启线程，
            // 而线程的生死正是最容易被漏掉的那种状态。
            std::unique_lock<std::mutex> lock(impl.mutex);
            impl.frameRoom.wait_for(lock, std::chrono::milliseconds(50), [&impl] {
                return impl.quit.load() || impl.seekPending.load();
            });
            continue;
        }

        if (packet->stream_index == impl.videoStream) {
            if (avcodec_send_packet(impl.videoCodec, packet) >= 0) {
                while (avcodec_receive_frame(impl.videoCodec, frame) >= 0) {
                    PushVideoFrame(impl, frame);
                    av_frame_unref(frame);
                }
            }
        } else if (impl.audioStream >= 0
                   && packet->stream_index == impl.audioStream) {
            if (avcodec_send_packet(impl.audioCodec, packet) >= 0) {
                while (avcodec_receive_frame(impl.audioCodec, frame) >= 0) {
                    PushAudioFrame(impl, frame);
                    av_frame_unref(frame);
                }
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
}

/// 主线程：把队列里的音频推给音频层（**受背压**：推不进去就留着下次）。
void PushQueuedAudio(InkingStaticVideo::Impl& impl) {
    if (impl.pcm == kInvalidPcmStream) {
        return;
    }
    while (AudioPcmStreamWantsData(impl.pcm)) {
        InkingStaticVideo::Impl::AudioChunk chunk;
        {
            std::lock_guard<std::mutex> lock(impl.mutex);
            if (impl.audio.empty()) {
                return;
            }
            chunk = std::move(impl.audio.front());
            impl.audio.pop_front();
            impl.audioBytes -= chunk.bytes.size();
        }
        impl.audioRoom.notify_all();

        const std::size_t left = AudioPushPcm(impl.pcm, chunk.bytes.data(),
                                              chunk.bytes.size());
        if (left > 0) {
            // 没吃下（水位刚好到了）：把没推的那截塞回队首，下次再来。
            chunk.bytes.erase(chunk.bytes.begin(),
                              chunk.bytes.end() - static_cast<std::ptrdiff_t>(left));
            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.audioBytes += chunk.bytes.size();
            impl.audio.push_front(std::move(chunk));
            return;
        }
    }
}

/// 主线程：保证纹理与当前渲染器尺寸匹配（换渲染器 / 换尺寸就重建）。
/// 返回 nullptr 表示这一帧画不了（调用方退回兜底色）。
SDL_Texture* EnsureTexture(InkingStaticVideo::Impl& impl, SDL_Renderer* renderer,
                           int width, int height) {
    if (renderer == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }
    if (impl.texture != nullptr
        && (impl.textureRenderer != renderer || impl.textureWidth != width
            || impl.textureHeight != height)) {
        // 换渲染器之后旧纹理已经无效了：只丢记账，**不要去 destroy**（§6 第 46 条）。
        if (impl.textureRenderer == renderer) {
            SDL_DestroyTexture(impl.texture);
        }
        impl.texture = nullptr;
        impl.textureHasFrame = false;
    }
    if (impl.texture == nullptr) {
        impl.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, width,
                                         height);
        if (impl.texture == nullptr) {
            return nullptr;
        }
        impl.textureRenderer = renderer;
        impl.textureWidth = width;
        impl.textureHeight = height;
    }
    return impl.texture;
}

/**
 * 主线程每帧推进：推音频 → 定主时钟 → 取该显示的那一帧 → 上传纹理。
 *
 * 主时钟的顺序是**先音频后画面**：音频位置（拿不到就真实时间）是权威，
 * 视频按它丢帧 / 等待——这就是"音画同步"的全部内容（见 InkingVideo.h 的类注释）。
 */
void Pump(InkingStaticVideo::Impl& impl) {
    const auto now = std::chrono::steady_clock::now();
    const double realDelta =
        impl.clockStarted
            ? std::chrono::duration<double>(now - impl.lastPump).count()
            : 0.0;
    impl.lastPump = now;
    impl.clockStarted = true;

    // 0. 循环重来了：音频要重新开一条（位置归零），基准也归零。
    if (impl.looped.exchange(false)) {
        if (impl.pcm != kInvalidPcmStream) {
            AudioClosePcmStream(impl.pcm);
            impl.pcm = kInvalidPcmStream;
            impl.pcmPlaying = false;
        }
        impl.clockBase = 0.0;
        impl.clock = 0.0;
        impl.finished = false;
    }

    if (!impl.playing) {
        return;
    }

    // 1. 音频：先推数据（背压），再拿它当主时钟。
    if (impl.hasAudio && impl.pcm == kInvalidPcmStream) {
        impl.pcm = AudioOpenPcmStream(kAudioOutputRate, kAudioOutputChannels);
        if (impl.pcm != kInvalidPcmStream) {
            AudioSetPcmStreamVolume(impl.pcm, impl.muted ? 0.0f : impl.volume);
            AudioSetPcmStreamPlaying(impl.pcm, true);
            impl.pcmPlaying = true;
        }
    }
    if (impl.pcm != kInvalidPcmStream) {
        PushQueuedAudio(impl);
        const float position = AudioPcmStreamPositionSeconds(impl.pcm);
        if (impl.pcmPlaying && position > 0.0f) {
            impl.clock = impl.clockBase + position;  // 音频位置 = 主时钟
        } else {
            impl.clock += realDelta;  // 音频还没出声：先按真实时间走
        }
    } else {
        impl.clock += realDelta;  // 没音轨 / 没音频后端：真实时间当主时钟
    }
    // 位置不超过时长（音频位置可能比视频尾巴多走一点）。
    if (impl.duration > 0.0 && impl.clock > impl.duration) {
        impl.clock = impl.duration;
    }

    // 2. 视频：丢帧追赶 + 该上屏的取出来。
    InkingStaticVideo::Impl::VideoFrame frame;
    bool haveFrame = false;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        // 追帧：**队首**已经过去太多（落后主时钟超过阈值）、而后面还有更新的帧，
        // 就把它丢掉。⚠️ 判据必须是 front（要丢的那个）与 clock 比——第一版写成
        // "frames[1] <= clock + 阈值"（下标与方向都反了），结果队列被一路清空、
        // 一帧都上不了屏（自检读出"已出帧 0，丢 19"）。
        while (impl.frames.size() > 1
               && impl.frames.front().pts < impl.clock - kDropThresholdSeconds) {
            impl.frames.pop_front();
            ++impl.dropped;
        }
        if (!impl.frames.empty()
            && impl.frames.front().pts <= impl.clock + kPresentAheadSeconds) {
            frame = std::move(impl.frames.front());
            impl.frames.pop_front();
            haveFrame = true;
        }
    }
    if (haveFrame) {
        impl.frameRoom.notify_all();
        SDL_Renderer* renderer = detail::currentRenderer();
        // 纹理按**视频尺寸**建（不是缩放器那个尺寸——两者是两回事）。
        SDL_Texture* texture =
            EnsureTexture(impl, renderer, impl.videoWidth, impl.videoHeight);
        if (texture != nullptr && impl.textureWidth > 0 && impl.textureHeight > 0) {
            if (SDL_UpdateTexture(texture, nullptr, frame.bgra.data(),
                                  impl.textureWidth * 4)) {
                impl.textureHasFrame = true;
                ++impl.presented;
            }
        }
    }

    // 3. 播完了吗（非循环 + 队列空了）。
    const bool atEnd = impl.atEnd.load();
    bool queueEmpty = false;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        queueEmpty = impl.frames.empty();
    }
    if (atEnd && queueEmpty && !impl.finished) {
        impl.finished = true;
        impl.playing = false;
        if (impl.pcm != kInvalidPcmStream) {
            AudioSetPcmStreamPlaying(impl.pcm, false);
            impl.pcmPlaying = false;
        }
        // 事件在 `onRender` 里发（那里是唯一的每帧入口）；这里只落状态。
    }
}

}  // namespace

#endif  // INK_HAS_FFMPEG

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

InkingStaticVideo::InkingStaticVideo(InkingWidget* parent, const VideoData& data)
    : InkingStaticWidget(parent, data.name), _impl(new Impl()) {
    applyData(data);
}

InkingStaticVideo::~InkingStaticVideo() {
#if defined(INK_HAS_FFMPEG)
    Impl& impl = *_impl;
    // 1. 先让 worker 停下来（它可能正卡在"等队列腾位置"上）。
    impl.quit.store(true);
    impl.frameRoom.notify_all();
    impl.audioRoom.notify_all();
    if (impl.worker.joinable()) {
        impl.worker.join();
    }
    // 2. 音频流（属于音频层，要显式关）。
    if (impl.pcm != kInvalidPcmStream) {
        AudioSetPcmStreamPlaying(impl.pcm, false);
        AudioClosePcmStream(impl.pcm);
        impl.pcm = kInvalidPcmStream;
    }
    // 3. 纹理：**只在还是当前渲染器的时候销毁**（渲染器可能已经被销毁了，
    //    那时这个句柄已经无效，destroy 它就是踩 §6 第 46 条那个坑）。
    if (impl.texture != nullptr
        && impl.textureRenderer == detail::currentRenderer()) {
        SDL_DestroyTexture(impl.texture);
    }
    impl.texture = nullptr;
    // 4. 解码器与解封装。
    if (impl.scaler != nullptr) {
        sws_freeContext(impl.scaler);
    }
    if (impl.resampler != nullptr) {
        swr_free(&impl.resampler);
    }
    if (impl.audioCodec != nullptr) {
        avcodec_free_context(&impl.audioCodec);
    }
    if (impl.videoCodec != nullptr) {
        avcodec_free_context(&impl.videoCodec);
    }
    if (impl.format != nullptr) {
        avformat_close_input(&impl.format);
    }
#endif
    _impl.reset();
}

void InkingStaticVideo::applyData(const VideoData& data) {
    // 拷一份再 Normalize（和图片一样的理由：本类要求 `_data` 永远补齐）。
    _data = data;
    _data.Normalize();
    _renderAppearance = _data.normal;

    // 几何：构造期一次性写进基类（逐字段——漏一处就是"配置写了不生效"且不报错，
    // AGENTS §6 第 83 条）。
    _width = _data.width > 0 ? _data.width : 0;
    _height = _data.height > 0 ? _data.height : 0;
    _selfAnchor = _data.selfAnchor;
    _traceAnchor = _data.traceAnchor;
    _offsetX = _data.offsetX;
    _offsetY = _data.offsetY;
    _layoutItem = _data.layoutItem;

    SetVisible(_data.visible);
    ChangeZIndex(_data.zIndex);
    ClearDirty();
    NotifySceneStructureChanged();

    Impl& impl = *_impl;
    impl.volume = std::clamp(_data.options.volume, 0.0f, 1.0f);
    impl.muted = _data.options.muted;
    impl.loop = _data.options.loop;
    impl.path = ResolveVideoPath(_data.source);

    if (_data.source.empty()) {
        INK_LOG_ERROR(std::string(kVideoModuleName),
                      "没给 source（视频路径）：这个组件只会画兜底色");
        return;
    }
    if (impl.path.empty()) {
        INK_LOG_ERROR(std::string(kVideoModuleName),
                      "素材不在：" + _data.source + "（资源根 " + AssetRoot()
                          + "）——这个组件只会画兜底色");
        return;
    }

#if defined(INK_HAS_FFMPEG)
    if (!OpenVideo(impl, impl.path)) {
        return;  // 具体原因 OpenVideo 里已经报过了
    }
    impl.opened = true;
    impl.worker = std::thread([&impl, loop = impl.loop] { DecodeLoop(impl, loop); });
    if (_data.options.autoplay) {
        Play();
    }
    opened.Emit();
    INK_LOG_PASS(std::string(kVideoModuleName),
                 "视频打开了：" + _data.source + "，时长 "
                     + std::to_string(impl.duration) + " 秒，画面 "
                     + std::to_string(impl.videoWidth) + "x"
                     + std::to_string(impl.videoHeight)
                     + (impl.hasAudio ? "（带音轨）" : "（无音轨）"));
#else
    INK_LOG_WARN(std::string(kVideoModuleName),
                 "构建时没接 FFmpeg：视频组件是**空转档**（能编、能跑、画面不动，"
                 "画兜底色）。启用方式见 docs/SETUP.md 的「FFmpeg（视频）」一节。"
                 "素材：" + _data.source);
#endif
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

bool InkingStaticVideo::HitTest(float worldX, float worldY) const {
    return detail::HitTestShape(*this, GetShape(), GetDisplayTransform(), worldX,
                                worldY);
}

const ShapeSpec& InkingStaticVideo::GetShape() const noexcept { return _data.shape; }

std::uint32_t InkingStaticVideo::GetDisplayColor() const noexcept {
    return _data.normal.color;
}

TransformSpec InkingStaticVideo::GetDisplayTransform() const noexcept {
    // 第一版**不认**外部显示变换（见类注释第 4 条）。
    return _data.normal.transform;
}

TransformSpec InkingStaticVideo::GetSubtreeTransform() const noexcept {
    return _data.normal.transform;
}

ShapeBounds InkingStaticVideo::GetHitEnvelope() const {
    return detail::ButtonHitEnvelope(_data, GetShape(), GetWidth(), GetHeight());
}

const VideoData& InkingStaticVideo::GetData() const noexcept { return _data; }

// ---------------------------------------------------------------------------
// 播控（没打开时全是**安全空操作**，而且不假装在播）
// ---------------------------------------------------------------------------

void InkingStaticVideo::Play() {
    Impl& impl = *_impl;
    if (!impl.opened) {
        return;
    }
    if (impl.finished && !impl.loop) {
        Seek(0.0f);  // 播完再 Play = 从头再来
    }
    impl.finished = false;
    _finishedNotified = false;
    impl.playing = true;
    impl.lastPump = std::chrono::steady_clock::now();
    impl.clockStarted = true;
    if (impl.pcm != kInvalidPcmStream) {
        AudioSetPcmStreamPlaying(impl.pcm, true);
        impl.pcmPlaying = true;
    }
}

void InkingStaticVideo::Pause() {
    Impl& impl = *_impl;
    impl.playing = false;
    if (impl.pcm != kInvalidPcmStream) {
        AudioSetPcmStreamPlaying(impl.pcm, false);
        impl.pcmPlaying = false;
    }
}

void InkingStaticVideo::Stop() {
    Impl& impl = *_impl;
    impl.playing = false;
    impl.finished = false;
    if (impl.pcm != kInvalidPcmStream) {
        AudioSetPcmStreamPlaying(impl.pcm, false);
        impl.pcmPlaying = false;
    }
    Seek(0.0f);
}

void InkingStaticVideo::Seek(float seconds) {
    Impl& impl = *_impl;
    if (!impl.opened) {
        return;
    }
    float clamped = std::max(0.0f, seconds);
    if (impl.duration > 0.0) {
        clamped = static_cast<float>(std::min<double>(clamped, impl.duration));
    }
#if defined(INK_HAS_FFMPEG)
    impl.seekTarget.store(static_cast<double>(clamped));
    impl.seekPending.store(true);
    impl.atEnd.store(false);
    impl.frameRoom.notify_all();
#endif
    // seek 之后音频位置从 0 重新算：主时钟 = 基准 + 音频位置。
    // seek 之后音频要**重新开一条流**：主时钟 = 基准 + 音频位置，而音频位置是
    // "这条流播了多久"。不重开的话位置会带着 seek 之前那一段继续涨——实测症状是
    // "位置冲到 4.45 秒，而时长才 3 秒"。
    if (impl.pcm != kInvalidPcmStream) {
        AudioSetPcmStreamPlaying(impl.pcm, false);
        AudioClosePcmStream(impl.pcm);
        impl.pcm = kInvalidPcmStream;
        impl.pcmPlaying = false;
    }
    impl.clockBase = clamped;
    impl.clock = clamped;
    impl.finished = false;
    _finishedNotified = false;
    impl.textureHasFrame = false;
}

void InkingStaticVideo::SetLoop(bool loop) noexcept { _impl->loop = loop; }

void InkingStaticVideo::SetVolume(float volume) noexcept {
    Impl& impl = *_impl;
    impl.volume = std::clamp(volume, 0.0f, 1.0f);
    if (impl.pcm != kInvalidPcmStream) {
        AudioSetPcmStreamVolume(impl.pcm, impl.muted ? 0.0f : impl.volume);
    }
}

void InkingStaticVideo::SetMuted(bool muted) noexcept {
    Impl& impl = *_impl;
    impl.muted = muted;
    if (impl.pcm != kInvalidPcmStream) {
        AudioSetPcmStreamVolume(impl.pcm, impl.muted ? 0.0f : impl.volume);
    }
}

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------

bool InkingStaticVideo::IsPlaying() const noexcept { return _impl->playing; }
bool InkingStaticVideo::IsFinished() const noexcept { return _impl->finished; }
bool InkingStaticVideo::IsOpened() const noexcept { return _impl->opened; }

float InkingStaticVideo::GetPositionSeconds() const noexcept {
    return static_cast<float>(_impl->clock);
}

float InkingStaticVideo::GetDurationSeconds() const noexcept {
    return static_cast<float>(_impl->duration);
}

int InkingStaticVideo::GetVideoWidth() const noexcept {
    return _impl->opened ? _impl->videoWidth : 0;
}

int InkingStaticVideo::GetVideoHeight() const noexcept {
    return _impl->opened ? _impl->videoHeight : 0;
}

bool InkingStaticVideo::HasAudioTrack() const noexcept { return _impl->hasAudio; }

bool InkingStaticVideo::AssetExists() const noexcept { return !_impl->path.empty(); }

const std::string& InkingStaticVideo::AssetPath() const noexcept {
    return _impl->path;
}

bool InkingStaticVideo::IsAtEnd() const noexcept {
#if defined(INK_HAS_FFMPEG)
    return _impl->atEnd.load();
#else
    return false;
#endif
}

std::uint32_t InkingStaticVideo::GetDecodedFrameCount() const noexcept {
    return _impl->decoded.load();
}

std::uint32_t InkingStaticVideo::GetPresentedFrameCount() const noexcept {
    return _impl->presented;
}

std::uint32_t InkingStaticVideo::GetDroppedFrameCount() const noexcept {
    return _impl->dropped;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

void InkingStaticVideo::onRender(float pixelX, float pixelY) const {
    Impl& impl = *_impl;
#if defined(INK_HAS_FFMPEG)
    Pump(impl);
    if (impl.finished && !_finishedNotified) {
        // 播完通知一次。`finished` 与 `_finishedNotified` 都是 mutable——
        // `onRender` 是 const，而"发通知"正是逻辑常量性允许的那种写入。
        _finishedNotified = true;
        finished.Emit();
    }
    if (impl.textureHasFrame && impl.texture != nullptr) {
        const float mag = GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
        ImageFitSpec spec;
        spec.fit = ToImageFit(_data.fit);
        spec.frameWidth = static_cast<float>(GetWidth()) * mag;
        spec.frameHeight = static_cast<float>(GetHeight()) * mag;
        spec.imageWidth = static_cast<float>(impl.textureWidth);
        spec.imageHeight = static_cast<float>(impl.textureHeight);
        // 画面走**形状层**：形状裁剪 / UV / 跟随变换 / 圆角羽化全复用 B1 那份实现。
        detail::fillShapeTextured(detail::currentRenderer(), GetShape(), GetWidth(),
                                  GetHeight(), mag, pixelX, pixelY,
                                  GetDisplayTransform(), impl.texture,
                                  /*hasAlpha=*/false, spec,
                                  _data.normal.color);
        return;
    }
#endif
    // 没有画面（没后端 / 素材没打开 / 还没解出第一帧）：退回**兜底色**。
    // 把画面变成一个洞比"缺视频"更难查——形状 / 描边阴影照常画。
    detail::PaintButtonAppearance(*this, GetShape(), _renderAppearance,
                                  _data.stroke, _data.shadow, _data.normal.color,
                                  GetDisplayTransform(), pixelX, pixelY);
}

}  // namespace inking
