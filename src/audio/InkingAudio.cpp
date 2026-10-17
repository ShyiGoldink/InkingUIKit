#include <audio/InkingAudio.h>

#include <detail/InkLog.h>
#include <thread/TaskQueue.h>

#include <algorithm>
#include <any>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(INK_HAS_SDL3_MIXER)
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#endif

// 音频层的实现（B10）。取向写在公开头里，这里只说实现上的三条：
//
//   1. **后端是 SDL3_mixer 的 Track API**：`MIX_Track` = "调音台上一路推子"，
//      多路可以同时响。BGM 交叉淡入淡出就是"两条轨道各写各的增益"，不需要
//      "先停再放"那种将就做法。
//   2. **增益由我们自己算**（`主 × 通道 × 单笔 × 淡入淡出`），每帧写进轨道。
//      为什么不用 mixer 自带的 fade：自带的那条曲线**读不回来**，而"淡入到
//      一半时增益正好是 0.5"是要被自检断言的。自己算还顺带把交叉淡入淡出
//      变成了同一套账本的自然结果。
//   3. **异步加载走 TaskQueue**（框架现成的 4 条管家线程 + 音频任务并发上限 2）：
//      worker 只做"打开文件 + 解码"（`MIX_LoadAudio` 明确标注可从任意线程调用），
//      结果放进一个互斥保护的小队列，**主线程在 `AudioTick` 里消费**。
//      于是主线程从不阻塞，而播放状态（轨道、增益、淡入淡出）仍然只有一个线程碰。

namespace inking {

namespace {

#if defined(INK_HAS_SDL3_MIXER)

/// SE 轨道池大小：同一时刻最多这么多条 SE 叠着响，满了抢最旧的那条。
constexpr int kSeTrackCount = 16;

/// 同时最多几个后台解码任务（再多就是几个线程一起砸磁盘，反而更慢）。
constexpr int kMaxConcurrentLoads = 2;

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

/// 一段已经加载好的声音。
struct Sound {
    MIX_Audio* audio = nullptr;
    std::string name;
    float durationSeconds = 0.0f;
    AudioLoadState state = AudioLoadState::Loading;
    bool streaming = false;
    /// 从内存加载时后端是**零拷贝**接过去的，所以这份字节要活到声音销毁。
    std::vector<std::uint8_t> ownedBytes;
};

/// 一条 SE 轨道的记账。
struct SeVoice {
    MIX_Track* track = nullptr;
    SoundId sound = kInvalidSound;
    float volume = 1.0f;
    bool active = false;
    std::uint64_t serial = 0;  ///< 起播顺序，池满时抢最旧的那条
};

/// 正在淡出的**上一条** BGM（交叉淡入淡出靠它 + 当前 BGM 两条轨道）。
struct FadingBgm {
    MIX_Track* track = nullptr;
    float gain = 1.0f;
    float fadePerSecond = 0.0f;
    bool active = false;
};

/**
 * 一条**外部 PCM 流轨道**的记账（A14）。
 *
 * `stream` 是调用方喂数据的地方（我们往里推 PCM），mixer 把它当这条轨道的
 * 输入源——于是音量 / 暂停 / 位置都归 mixer 管，我们只负责推数据与背压。
 */
struct PcmStream {
    MIX_Track* track = nullptr;         ///< 混音器里的轨道（**随混音器销毁**）
    SDL_AudioStream* stream = nullptr;  ///< 数据源；**要自己销毁**（不属于 mixer）
    float volume = 1.0f;                ///< 这一笔的音量
    bool playing = false;
    int sampleRate = 0;
    int channels = 0;
};

/// 流池大小：视频一条 + 角色语音几条（片头与对话可能同时在响）。
constexpr int kPcmStreamCount = 4;

/// 排队水位（秒）：推到这个量就先别推了——解码侧的**背压**就靠它。
constexpr float kPcmQueueTargetSeconds = 0.25f;

/// 一条流的目标排队字节数（按**源格式**算：S16 交错）。
std::size_t PcmQueueTargetBytes(const PcmStream& slot) {
    const std::size_t bytesPerFrame =
        static_cast<std::size_t>(slot.channels) * sizeof(std::int16_t);
    const float frames =
        static_cast<float>(slot.sampleRate) * kPcmQueueTargetSeconds;
    return static_cast<std::size_t>(frames) * bytesPerFrame;
}

struct AudioState {
    /**
     * 混音器句柄。**后台线程也会读它**（解码要用），所以是原子的：
     * 关掉的一瞬间它变成 nullptr，而已经在跑的那个任务读到的要么是旧值
     * （那时 `AudioShutdown` 正在等它回来，句柄还有效），要么是新值（直接放弃）。
     */
    std::atomic<MIX_Mixer*> mixer{nullptr};
    std::vector<Sound> sounds;  ///< 下标 + 1 = SoundId

    MIX_Track* bgmTrack = nullptr;
    SoundId bgmSound = kInvalidSound;
    float bgmVolume = 1.0f;   ///< 这一笔的音量
    float bgmGain = 1.0f;     ///< 淡入淡出的当前增益
    float bgmFadeTarget = 1.0f;
    float bgmFadePerSecond = 0.0f;
    bool bgmRequested = false;  ///< 我们请求它响着（与"轨道真的在响"分开）

    FadingBgm fading;
    std::vector<SeVoice> seVoices;
    std::uint64_t seSerial = 0;

    /**
     * 角色语音（B13）：**一条轨道、一句一放**——新的直接把这条轨道的输入换掉，
     * 于是"打断上一句"是白拿的（不需要池、不需要抢轨道）。
     *
     * 为什么不复用 SE 池：SE 是"可叠加"的账（16 条并行），而语音要的是
     * "同一时刻只有一句"；共用一套记账就得在池里再加一层"谁属于哪一路"的判断，
     * 而分开只需要一条轨道。
     */
    MIX_Track* voiceTrack = nullptr;
    SoundId voiceSound = kInvalidSound;
    float voiceVolume = 1.0f;    ///< 这一笔的音量
    bool voiceRequested = false; ///< 我们请求它响着（播完了它自己也还是 true）

    /// "等就绪再播"的那一句（与 BGM 那条同源：迟到的语音要补上）。
    bool hasPendingVoice = false;
    SoundId pendingVoice = kInvalidSound;
    float pendingVoiceVolume = 1.0f;

    /**
     * 外部 PCM 流轨道池（A14）：下标 + 1 = `PcmStreamId`；`stream == nullptr`
     * 表示这个槽空着（轨道在 `AudioInit` 里一次建好，stream 开着才建）。
     *
     * **只在主线程动**——理由写在 `InkingAudio.h` 那段线程前提里：解码 worker
     * 只把 PCM 块交回主线程，和 BGM / SE 同一套分工。
     */
    std::vector<PcmStream> pcmStreams;

    // ⚠️ 这里**每一项都要写满**：改成 `{1.0f, 1.0f}` 的话，新加的 `Voice` 通道
    // 会被值初始化成 **0**——症状是"视频音轨怎么调都没声音"，而且不报错
    // （A14 加第三路通道时差点就这么漏过去）。
    float channelVolume[kAudioChannelCount] = {1.0f, 1.0f, 1.0f};
    float master = 1.0f;

    // ---- 异步加载 ----
    struct Request {
        SoundId id = kInvalidSound;
        std::string path;
        AudioLoadMode mode = AudioLoadMode::Auto;
    };
    std::vector<Request> waiting;   ///< 还没投递出去的（主线程独占）
    /**
     * 已经投出去、还没回来的任务数。
     *
     * **由后台线程自减**（结果入队之后），所以它反映的是"真在跑的任务"——
     * `AudioShutdown` 靠它等任务排空（那时才敢销毁混音器）。
     */
    std::atomic<std::uint32_t> inFlight{0};

    struct Completed {
        SoundId id = kInvalidSound;
        MIX_Audio* audio = nullptr;
        float durationSeconds = 0.0f;
        bool streaming = false;
        std::uint64_t micros = 0;
        std::uint64_t generation = 0;
    };
    std::mutex doneMutex;
    std::vector<Completed> done;
    /// 关一次 +1：迟到的结果按代数丢弃（`AGENTS.md` §6 第 12 条那条"生命周期
    /// 比对象长"的同族问题，这里用代数而不是裸指针判断）。
    std::atomic<std::uint64_t> generation{0};

    // ---- 等就绪再播的 BGM ----
    bool hasPendingBgm = false;
    SoundId pendingBgm = kInvalidSound;
    float pendingBgmFade = 0.0f;
    float pendingBgmVolume = 1.0f;

    AudioStats stats;
};

/**
 * 函数内静态量而不是类静态成员：避开静态初始化顺序问题（`AGENTS.md` §6 第 12 条）。
 *
 * **但它故意"永不析构"**：这里是堆对象，进程退出时**不** delete。
 * 理由是一次实测出来的崩溃（`AGENTS.md` §6 第 88 条，源项目自检
 * `tests/test_shutdown.cpp`——该套件未搬入本仓库）：
 * `InkingWindow` 的静态单例析构时会调 `AudioShutdown()`，而"只开窗、不预先
 * `AudioInit()`"的程序里，音频状态是在 `Show()` 内部才构造的 ⇒ 它比窗口单例
 * **后**构造 ⇒ 按"后构造先析构"它**先**析构，于是那次 `AudioShutdown()` 会去
 * lock 一把已经不存在的 mutex（`std::system_error` → `terminate`；
 * `panel_demo` 关窗时就是这么死的）。
 *
 * 这块状态本来就是**进程级**记账（设备、混音器、缓存的声音），真正的收尾是
 * 显式的 `AudioShutdown()`（关设备、销毁混音器）；退出时把这几百字节交还给
 * 操作系统即可，没必要在析构顺序上赌一把。`AudioInit()` / `AudioShutdown()`
 * 反复调用仍然幂等。
 */
AudioState& state() {
    static AudioState* instance = new AudioState();
    return *instance;
}

const Sound* FindSound(SoundId id) {
    AudioState& audio = state();
    if (id == kInvalidSound || id > audio.sounds.size()) {
        return nullptr;
    }
    return &audio.sounds[id - 1];
}

Sound* MutableSound(SoundId id) {
    AudioState& audio = state();
    if (id == kInvalidSound || id > audio.sounds.size()) {
        return nullptr;
    }
    return &audio.sounds[id - 1];
}

/// 同名（同路径）只解一次。
SoundId FindLoaded(const std::string& name) {
    AudioState& audio = state();
    for (std::size_t i = 0; i < audio.sounds.size(); ++i) {
        if (audio.sounds[i].name == name
            && audio.sounds[i].state != AudioLoadState::Failed) {
            return static_cast<SoundId>(i + 1);
        }
    }
    return kInvalidSound;
}

/// 真要解的时候用哪个模式：Auto 先按流式打开，读到时长再决定要不要重载。
bool ShouldPredecode(AudioLoadMode mode, float durationSeconds) {
    switch (mode) {
        case AudioLoadMode::InMemory:
            return true;
        case AudioLoadMode::Streaming:
            return false;
        case AudioLoadMode::Auto:
        default:
            return durationSeconds > 0.0f
                && durationSeconds <= kStreamingThresholdSeconds;
    }
}

float DurationOfAudio(MIX_Audio* audio) {
    if (audio == nullptr) {
        return 0.0f;
    }
    const Sint64 frames = MIX_GetAudioDuration(audio);
    if (frames <= 0) {
        return 0.0f;  // 未知（流式）/ 无限
    }
    const Sint64 micros = MIX_AudioFramesToMS(audio, frames);
    return static_cast<float>(micros) / 1000.0f;
}

/**
 * 真正加载一段音频（**可以在后台线程调用**）。
 *
 * Auto 的做法：先按流式打开拿时长，若它其实很短再重载成整段——短文件重载便宜，
 * 长文件省下一次整段解码。
 */
MIX_Audio* LoadAudioFor(MIX_Mixer* mixer, const std::string& path,
                        AudioLoadMode mode, float* durationOut,
                        bool* streamingOut) {
    if (mixer == nullptr) {
        return nullptr;
    }
    MIX_Audio* audio = MIX_LoadAudio(mixer, path.c_str(), /*predecode=*/false);
    if (audio == nullptr) {
        return nullptr;
    }
    float duration = DurationOfAudio(audio);
    if (ShouldPredecode(mode, duration) && duration > 0.0f) {
        MIX_Audio* decoded = MIX_LoadAudio(mixer, path.c_str(), /*predecode=*/true);
        if (decoded != nullptr) {
            MIX_DestroyAudio(audio);
            audio = decoded;
        }
    }
    if (durationOut != nullptr) {
        *durationOut = duration;
    }
    if (streamingOut != nullptr) {
        *streamingOut = !ShouldPredecode(mode, duration);
    }
    return audio;
}

/// 把"账本算出来的增益"写进轨道（三级音量 + 淡入淡出都在这里落地）。
void ApplyVolumes() {
    AudioState& audio = state();
    MIX_Mixer* mixer = audio.mixer.load();
    if (mixer == nullptr) {
        return;
    }
    MIX_SetMixerGain(mixer, audio.master);

    const float bgmChannel = audio.channelVolume[static_cast<int>(AudioChannel::Bgm)];
    if (audio.bgmTrack != nullptr) {
        MIX_SetTrackGain(audio.bgmTrack,
                         bgmChannel * audio.bgmVolume * audio.bgmGain);
    }
    if (audio.fading.active && audio.fading.track != nullptr) {
        MIX_SetTrackGain(audio.fading.track, bgmChannel * audio.fading.gain);
    }
    const float seChannel = audio.channelVolume[static_cast<int>(AudioChannel::Se)];
    for (SeVoice& voice : audio.seVoices) {
        if (voice.active && voice.track != nullptr) {
            MIX_SetTrackGain(voice.track, seChannel * voice.volume);
        }
    }
    // A14：外部 PCM 流也算一路（视频音轨 / 语音）——主音量或通道音量一变，
    // 它要跟着走，所以增益统一在这里算，别在推数据那条路上自己写一套。
    const float voiceChannel =
        audio.channelVolume[static_cast<int>(AudioChannel::Voice)];
    // B13：角色语音与 PCM 流共用 `Voice` 这一路通道音量（玩家眼里的"语音"就一处）。
    if (audio.voiceTrack != nullptr && audio.voiceRequested) {
        MIX_SetTrackGain(audio.voiceTrack, voiceChannel * audio.voiceVolume);
    }
    for (PcmStream& slot : audio.pcmStreams) {
        if (slot.track != nullptr && slot.stream != nullptr) {
            MIX_SetTrackGain(slot.track, voiceChannel * slot.volume);
        }
    }
}

/// 起播一段 BGM（内部用；调用方负责把旧的处理掉）。
void StartBgm(SoundId sound, float fadeInSeconds, float volume) {
    AudioState& audio = state();
    const Sound* loaded = FindSound(sound);
    if (loaded == nullptr || loaded->audio == nullptr || audio.bgmTrack == nullptr) {
        return;
    }
    MIX_SetTrackAudio(audio.bgmTrack, loaded->audio);
    MIX_SetTrackPlaybackPosition(audio.bgmTrack, 0);

    // 循环与淡入都走**一次播放的选项**（mixer 的官方姿势）：这样"这条轨道这次
    // 怎么播"只由这一次调用决定，不会和轨道上一次的残留状态打架。
    // 淡入仍然是**我们自己的账本**在做（`bgmGain` 每帧推进），所以"淡入到一半
    // 时增益是多少"能被自检直接读出来。
    SDL_PropertiesID props = SDL_CreateProperties();
    if (props != 0) {
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
        const bool played = MIX_PlayTrack(audio.bgmTrack, props);
        SDL_DestroyProperties(props);
        if (!played) {
            INK_LOG_WARN("Audio",
                         std::string("BGM 起播失败：") + SDL_GetError());
        }
    } else {
        MIX_SetTrackLoops(audio.bgmTrack, -1);
        if (!MIX_PlayTrack(audio.bgmTrack, 0)) {
            INK_LOG_WARN("Audio",
                         std::string("BGM 起播失败：") + SDL_GetError());
        }
    }

    audio.bgmSound = sound;
    audio.bgmVolume = Clamp01(volume);
    audio.bgmRequested = true;
    if (fadeInSeconds > 0.0f) {
        audio.bgmGain = 0.0f;
        audio.bgmFadeTarget = 1.0f;
        audio.bgmFadePerSecond = 1.0f / fadeInSeconds;
    } else {
        audio.bgmGain = 1.0f;
        audio.bgmFadeTarget = 1.0f;
        audio.bgmFadePerSecond = 0.0f;
    }
    ++audio.stats.voicesStarted;
    ApplyVolumes();
}

/// 把当前 BGM 挪去"淡出中"（或直接停掉）。
void RetireBgm(float fadeOutSeconds) {
    AudioState& audio = state();
    if (!audio.bgmRequested) {
        return;
    }
    audio.bgmRequested = false;
    if (audio.bgmTrack == nullptr) {
        return;
    }
    if (fadeOutSeconds > 0.0f) {
        // 交叉淡入淡出：旧的那条**换到备用轨道上继续响**，音量降到 0 再停。
        if (audio.fading.active && audio.fading.track != nullptr) {
            MIX_StopTrack(audio.fading.track, 0);
        }
        MIX_Track* spare = audio.fading.track;
        if (spare != nullptr) {
            // 备用轨道上的输入要跟着走：mixer 允许同一个 MIX_Audio 绑多条轨道。
            // **播放位置也要对齐**：不对齐的话两条轨道会差一点点相位，
            // 交叉的那一两秒听起来像"合唱"，而不是同一首歌在换音量。
            const Sint64 position = MIX_GetTrackPlaybackPosition(audio.bgmTrack);
            const Sound* loaded = FindSound(audio.bgmSound);
            if (loaded != nullptr && loaded->audio != nullptr) {
                MIX_SetTrackAudio(spare, loaded->audio);
                MIX_SetTrackLoops(spare, -1);
                MIX_SetTrackPlaybackPosition(spare, position > 0 ? position : 0);
                MIX_PlayTrack(spare, 0);
            }
            audio.fading.active = true;
            audio.fading.gain = audio.bgmGain;
            audio.fading.fadePerSecond = audio.bgmGain / fadeOutSeconds;
        }
        MIX_StopTrack(audio.bgmTrack, 0);
    } else {
        MIX_StopTrack(audio.bgmTrack, 0);
    }
    audio.bgmGain = 0.0f;
    audio.bgmVolume = 0.0f;
    audio.bgmSound = kInvalidSound;
    ApplyVolumes();
}

/// 挑一条 SE 轨道：优先空闲的，池满就抢最旧的那条。
MIX_Track* AcquireSeTrack(SeVoice** outVoice) {
    AudioState& audio = state();
    for (SeVoice& voice : audio.seVoices) {
        if (!voice.active) {
            *outVoice = &voice;
            return voice.track;
        }
    }
    SeVoice* oldest = &audio.seVoices.front();
    for (SeVoice& voice : audio.seVoices) {
        if (voice.serial < oldest->serial) {
            oldest = &voice;
        }
    }
    if (oldest->track != nullptr) {
        MIX_StopTrack(oldest->track, 0);
    }
    *outVoice = oldest;
    return oldest->track;
}

// ---------------------------------------------------------------------------
// 后台加载：只做"打开 + 解码"，结果丢进一个小队列，主线程消费
// ---------------------------------------------------------------------------
void QueueDecode(SoundId id, const std::string& path, AudioLoadMode mode) {
    state().inFlight.fetch_add(1);
    auto task = Task<std::any>{};
    task.action = [id, path, mode](const std::vector<std::any>&) -> std::any {
        AudioState& audio = state();
        const std::uint64_t generation = audio.generation.load();
        MIX_Mixer* mixer = audio.mixer.load();
        if (mixer == nullptr) {
            // 还没开始就被关掉了（或者压根没起来）：直接收工。
            audio.inFlight.fetch_sub(1);
            return {};
        }

        const auto started = std::chrono::steady_clock::now();
        float duration = 0.0f;
        bool streaming = false;
        MIX_Audio* decoded =
            LoadAudioFor(mixer, path, mode, &duration, &streaming);
        const auto finished = std::chrono::steady_clock::now();
        const std::uint64_t micros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(finished - started)
                .count());

        AudioState::Completed completed;
        completed.id = id;
        completed.audio = decoded;
        completed.durationSeconds = duration;
        completed.streaming = streaming;
        completed.micros = micros;
        completed.generation = generation;
        {
            std::lock_guard<std::mutex> lock(audio.doneMutex);
            audio.done.push_back(completed);
        }
        // 自减放在结果入队**之后**：`AudioShutdown` 等它归零，等的就是
        // "再也不会有人碰混音器了"这件事。
        audio.inFlight.fetch_sub(1);
        return {};
    };

    TaskQueue::instance().addTask(std::move(task));
}

#else  // 没有 SDL3_mixer：整层空转

struct AudioState {
    AudioStats stats;
};

AudioState& state() {
    // 同样是**永不析构**的堆对象，理由与上面那份 `state()` 完全一样
    // （`AGENTS.md` §6 第 88 条）：这一档虽然只有统计量、没有 mutex，
    // 但两条分支的寿命语义保持一致，免得"接了 SDL3_mixer 就崩、没接就不崩"。
    static AudioState* instance = new AudioState();
    return *instance;
}

#endif  // INK_HAS_SDL3_MIXER

}  // namespace

#if defined(INK_HAS_SDL3_MIXER)

bool AudioInit() {
    AudioState& audio = state();
    if (audio.mixer.load() != nullptr) {
        return true;
    }
    if (!MIX_Init()) {
        INK_LOG_ERROR("Audio", std::string("MIX_Init 失败：") + SDL_GetError());
        return false;
    }
    MIX_Mixer* mixer =
        MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (mixer == nullptr) {
        INK_LOG_ERROR("Audio",
                      std::string("建混音器失败：") + SDL_GetError());
        MIX_Quit();
        return false;
    }
    audio.mixer.store(mixer);

    audio.bgmTrack = MIX_CreateTrack(mixer);
    audio.fading.track = MIX_CreateTrack(mixer);
    audio.voiceTrack = MIX_CreateTrack(mixer);  // B13：角色语音（一句话一条）
    audio.seVoices.clear();
    audio.seVoices.resize(kSeTrackCount);
    for (SeVoice& voice : audio.seVoices) {
        voice.track = MIX_CreateTrack(mixer);
        voice.active = false;
    }
    // A14：外部 PCM 流的**轨道**一次建好（便宜），`SDL_AudioStream` 等真的开流
    // 时再建——因为那个 stream 的格式要按调用方给的采样率 / 声道数来定。
    audio.pcmStreams.clear();
    audio.pcmStreams.resize(kPcmStreamCount);
    for (PcmStream& slot : audio.pcmStreams) {
        slot.track = MIX_CreateTrack(mixer);
    }
    MIX_SetMixerGain(mixer, audio.master);

    audio.stats.ready = true;
    audio.stats.decoderCount = static_cast<std::uint32_t>(MIX_GetNumAudioDecoders());
    audio.stats.masterVolume = audio.master;

    INK_LOG_PASS("Audio",
                 "混音器起来了：解码器 "
                     + std::to_string(audio.stats.decoderCount) + " 个"
                     + "（" + AudioDecoderName(0) + " …），轨道 "
                     + std::to_string(kSeTrackCount + 3) + " 条");
    return true;
}

void AudioShutdown() {
    AudioState& audio = state();
    // 1. 先让"还没投递的"失效，再 +1 代数让**已经在跑的**任务回来时被丢弃。
    audio.waiting.clear();
    audio.generation.fetch_add(1);

    // 2. 等已经在跑的解码任务收工。这一步是**必须**的：它们正拿着混音器句柄
    //    （`MIX_LoadAudio` 是长调用），此刻销毁混音器就是 use-after-free。
    //    正常情况下一两毫秒就排空（解码几十毫秒，且关音频只会发生在关窗时），
    //    真卡住也不会把退出流程挂死——200ms 之后放弃等待并照常关。
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (audio.inFlight.load() > 0
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // 3. 收结果、销毁声音与混音器。
    {
        std::lock_guard<std::mutex> lock(audio.doneMutex);
        for (const AudioState::Completed& completed : audio.done) {
            if (completed.audio != nullptr) {
                MIX_DestroyAudio(completed.audio);
            }
        }
        audio.done.clear();
    }
    // A14：外部 PCM 流——**先解绑再销毁 stream**。混音器头文件写明"这个 stream
    // 要有效到轨道不再需要它"，反过来就是 use-after-free（`AGENTS.md` §6 第 46 条
    // 那族"纹理属于渲染器"的音频版）。轨道本身随混音器销毁，不用单独处理。
    for (PcmStream& slot : audio.pcmStreams) {
        if (slot.stream == nullptr) {
            continue;
        }
        if (slot.track != nullptr) {
            MIX_SetTrackAudioStream(slot.track, nullptr);
            MIX_StopTrack(slot.track, 0);
        }
        SDL_DestroyAudioStream(slot.stream);
        slot.stream = nullptr;
    }
    audio.pcmStreams.clear();

    MIX_Mixer* mixer = audio.mixer.exchange(nullptr);
    if (mixer != nullptr) {
        for (const Sound& sound : audio.sounds) {
            if (sound.audio != nullptr) {
                MIX_DestroyAudio(sound.audio);
            }
        }
        MIX_DestroyMixer(mixer);  // 轨道随混音器一起销毁
        MIX_Quit();
    }
    audio.sounds.clear();
    audio.seVoices.clear();
    audio.bgmTrack = nullptr;
    audio.fading = FadingBgm{};
    audio.bgmSound = kInvalidSound;
    audio.bgmRequested = false;
    audio.bgmGain = 0.0f;
    audio.bgmVolume = 0.0f;
    audio.hasPendingBgm = false;
    audio.pendingBgm = kInvalidSound;
    audio.voiceTrack = nullptr;  // 轨道随混音器一起销毁
    audio.voiceSound = kInvalidSound;
    audio.voiceVolume = 1.0f;
    audio.voiceRequested = false;
    audio.hasPendingVoice = false;
    audio.pendingVoice = kInvalidSound;
    audio.stats = AudioStats{};
}

bool AudioReady() { return state().mixer.load() != nullptr; }

AudioStats AudioStatsSnapshot() {
    AudioState& audio = state();
    audio.stats.masterVolume = audio.master;
    audio.stats.soundsResident = static_cast<std::uint32_t>(audio.sounds.size());
    audio.stats.loadsPending = audio.inFlight.load()
        + static_cast<std::uint32_t>(audio.waiting.size());

    std::uint32_t tracks = 0;
    if (audio.bgmRequested && audio.bgmTrack != nullptr
        && MIX_TrackPlaying(audio.bgmTrack)) {
        ++tracks;
    }
    if (audio.fading.active && audio.fading.track != nullptr
        && MIX_TrackPlaying(audio.fading.track)) {
        ++tracks;
    }
    for (const SeVoice& voice : audio.seVoices) {
        if (voice.active) {
            ++tracks;
        }
    }
    // B13：角色语音也占一条轨道（"现在到底有几条在响"要给观测口算全）。
    if (audio.voiceRequested && audio.voiceTrack != nullptr
        && MIX_TrackPlaying(audio.voiceTrack)) {
        ++tracks;
    }
    audio.stats.tracksActive = tracks;
    return audio.stats;
}

std::string AudioDecoderName(int index) {
    if (index < 0) {
        return {};
    }
    const char* name = MIX_GetAudioDecoder(index);
    return name != nullptr ? std::string(name) : std::string{};
}

// ---------------------------------------------------------------------------
// 加载
// ---------------------------------------------------------------------------

SoundId AudioLoad(const std::string& path, AudioLoadMode mode) {
    AudioState& audio = state();
    MIX_Mixer* mixer = audio.mixer.load();
    if (path.empty() || mixer == nullptr) {
        return kInvalidSound;
    }
    const SoundId cached = FindLoaded(path);
    if (cached != kInvalidSound) {
        return cached;
    }

    float duration = 0.0f;
    bool streaming = false;
    MIX_Audio* decoded = LoadAudioFor(mixer, path, mode, &duration, &streaming);
    if (decoded == nullptr) {
        ++audio.stats.loadFailures;
        INK_LOG_WARN("Audio", "加载失败：" + path + "（" + SDL_GetError() + "）");
        return kInvalidSound;
    }

    Sound sound;
    sound.audio = decoded;
    sound.name = path;
    sound.durationSeconds = duration;
    sound.streaming = streaming;
    sound.state = AudioLoadState::Ready;
    audio.sounds.push_back(std::move(sound));
    ++audio.stats.loads;
    ++audio.stats.loadsCompleted;
    return static_cast<SoundId>(audio.sounds.size());
}

SoundId AudioLoadAsync(const std::string& path, AudioLoadMode mode) {
    AudioState& audio = state();
    if (path.empty() || audio.mixer.load() == nullptr) {
        return kInvalidSound;
    }
    const SoundId cached = FindLoaded(path);
    if (cached != kInvalidSound) {
        return cached;
    }

    // 立刻占一个句柄（状态 Loading），后台解码完再把它变成 Ready。
    Sound sound;
    sound.name = path;
    sound.state = AudioLoadState::Loading;
    audio.sounds.push_back(std::move(sound));
    const SoundId id = static_cast<SoundId>(audio.sounds.size());

    AudioState::Request request;
    request.id = id;
    request.path = path;
    request.mode = mode;
    audio.waiting.push_back(std::move(request));
    return id;
}

SoundId AudioLoadFromMemory(const void* data, std::size_t size,
                            const std::string& name, AudioLoadMode mode) {
    AudioState& audio = state();
    MIX_Mixer* mixer = audio.mixer.load();
    if (data == nullptr || size == 0 || mixer == nullptr) {
        return kInvalidSound;
    }
    const SoundId cached = FindLoaded(name);
    if (cached != kInvalidSound) {
        return cached;
    }

    // 后端是零拷贝接过去的，所以这份字节得留着（跟随 Sound 一起活）。
    Sound sound;
    sound.name = name;
    sound.ownedBytes.assign(static_cast<const std::uint8_t*>(data),
                            static_cast<const std::uint8_t*>(data) + size);
    sound.audio = MIX_LoadAudioNoCopy(mixer, sound.ownedBytes.data(),
                                      sound.ownedBytes.size(), /*free_when_done=*/false);
    if (sound.audio == nullptr) {
        ++audio.stats.loadFailures;
        INK_LOG_WARN("Audio",
                     "从内存加载失败：" + name + "（" + SDL_GetError() + "）");
        return kInvalidSound;
    }
    sound.durationSeconds = DurationOfAudio(sound.audio);
    sound.streaming = false;
    sound.state = AudioLoadState::Ready;
    audio.sounds.push_back(std::move(sound));
    ++audio.stats.loads;
    ++audio.stats.loadsCompleted;
    return static_cast<SoundId>(audio.sounds.size());
}

void AudioPreload(const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        AudioLoadAsync(path, AudioLoadMode::Auto);
    }
}

AudioLoadState AudioLoadStateOf(SoundId id) {
    const Sound* sound = FindSound(id);
    return sound != nullptr ? sound->state : AudioLoadState::None;
}

bool AudioIsReady(SoundId id) {
    const Sound* sound = FindSound(id);
    return sound != nullptr && sound->state == AudioLoadState::Ready
        && sound->audio != nullptr;
}

float AudioDurationSeconds(SoundId id) {
    const Sound* sound = FindSound(id);
    return sound != nullptr ? sound->durationSeconds : 0.0f;
}

// ---------------------------------------------------------------------------
// BGM
// ---------------------------------------------------------------------------

void AudioPlayBgm(SoundId sound, float fadeInSeconds, float volume) {
    AudioState& audio = state();
    if (!AudioIsReady(sound) || audio.bgmTrack == nullptr) {
        return;
    }
    if (audio.bgmRequested) {
        RetireBgm(fadeInSeconds > 0.0f ? fadeInSeconds : 0.0f);
    }
    StartBgm(sound, fadeInSeconds, volume);
}

void AudioPlayBgmAsync(SoundId sound, float fadeInSeconds, float volume) {
    AudioState& audio = state();
    if (sound == kInvalidSound) {
        return;
    }
    if (AudioIsReady(sound)) {
        AudioPlayBgm(sound, fadeInSeconds, volume);
        return;
    }
    if (AudioLoadStateOf(sound) != AudioLoadState::Loading) {
        return;  // 压根没这个句柄 / 已经失败：不排队
    }
    // 记住这次请求，`AudioTick` 里等它 Ready 的那一帧起播。
    audio.hasPendingBgm = true;
    audio.pendingBgm = sound;
    audio.pendingBgmFade = fadeInSeconds;
    audio.pendingBgmVolume = volume;
}

void AudioStopBgm(float fadeOutSeconds) {
    AudioState& audio = state();
    audio.hasPendingBgm = false;  // 还没起播就喊停：连待播请求一起撤掉
    RetireBgm(fadeOutSeconds);
}

void AudioPauseBgm() {
    AudioState& audio = state();
    if (audio.bgmTrack != nullptr && audio.bgmRequested) {
        MIX_PauseTrack(audio.bgmTrack);
    }
    if (audio.fading.active && audio.fading.track != nullptr) {
        MIX_PauseTrack(audio.fading.track);
    }
}

void AudioResumeBgm() {
    AudioState& audio = state();
    if (audio.bgmTrack != nullptr && audio.bgmRequested) {
        MIX_ResumeTrack(audio.bgmTrack);
    }
    if (audio.fading.active && audio.fading.track != nullptr) {
        MIX_ResumeTrack(audio.fading.track);
    }
}

bool AudioIsBgmPlaying() {
    AudioState& audio = state();
    return audio.bgmRequested && audio.bgmTrack != nullptr
        && MIX_TrackPlaying(audio.bgmTrack);
}

bool AudioIsBgmPaused() {
    AudioState& audio = state();
    return audio.bgmTrack != nullptr && MIX_TrackPaused(audio.bgmTrack);
}

float AudioBgmVolume() {
    const AudioState& audio = state();
    if (!audio.bgmRequested) {
        return 0.0f;
    }
    return audio.channelVolume[static_cast<int>(AudioChannel::Bgm)] * audio.bgmVolume
         * audio.bgmGain;
}

float AudioBgmPositionSeconds() {
    AudioState& audio = state();
    if (audio.bgmTrack == nullptr) {
        return 0.0f;
    }
    const Sint64 frames = MIX_GetTrackPlaybackPosition(audio.bgmTrack);
    if (frames < 0) {
        return 0.0f;
    }
    return static_cast<float>(MIX_TrackFramesToMS(audio.bgmTrack, frames)) / 1000.0f;
}

// ---------------------------------------------------------------------------
// SE
// ---------------------------------------------------------------------------

void AudioPlaySe(SoundId sound, float volume) {
    AudioState& audio = state();
    // **未就绪就丢弃**：迟到的音效不如不响。
    if (!AudioIsReady(sound)) {
        return;
    }
    const Sound* loaded = FindSound(sound);
    if (loaded == nullptr || audio.seVoices.empty()) {
        return;
    }

    SeVoice* voice = nullptr;
    MIX_Track* track = AcquireSeTrack(&voice);
    if (track == nullptr || voice == nullptr) {
        return;
    }
    MIX_SetTrackAudio(track, loaded->audio);
    MIX_SetTrackLoops(track, 0);
    MIX_SetTrackPlaybackPosition(track, 0);
    MIX_PlayTrack(track, 0);

    voice->sound = sound;
    voice->volume = Clamp01(volume);
    voice->active = true;
    voice->serial = ++audio.seSerial;
    ++audio.stats.voicesStarted;
    ApplyVolumes();
}

void AudioPlaySeAsync(SoundId sound, float volume) { AudioPlaySe(sound, volume); }

std::uint32_t AudioActiveSeCount() {
    std::uint32_t count = 0;
    for (const SeVoice& voice : state().seVoices) {
        if (voice.active) {
            ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// 角色语音（B13）
// ---------------------------------------------------------------------------

void AudioPlayVoice(SoundId sound, float volume) {
    AudioState& audio = state();
    // 与 SE 不同：语音**未就绪时不丢**——但那条路由 `AudioPlayVoiceAsync` 负责，
    // 这个函数只做"现在就能播"的那一半。
    if (!AudioIsReady(sound) || audio.voiceTrack == nullptr) {
        return;
    }
    const Sound* loaded = FindSound(sound);
    if (loaded == nullptr) {
        return;
    }
    // 一条轨道、直接换输入：上一句**当场**被截断（这就是"新的一句打断旧的一句"）。
    MIX_SetTrackAudio(audio.voiceTrack, loaded->audio);
    MIX_SetTrackLoops(audio.voiceTrack, 0);
    MIX_SetTrackPlaybackPosition(audio.voiceTrack, 0);
    MIX_PlayTrack(audio.voiceTrack, 0);

    audio.voiceSound = sound;
    audio.voiceVolume = Clamp01(volume);
    audio.voiceRequested = true;
    ++audio.stats.voicesStarted;
    ApplyVolumes();
}

void AudioPlayVoiceAsync(SoundId sound, float volume) {
    AudioState& audio = state();
    // 先把上一句停掉：语义是"现在这一屏该说的是哪句"，不是"再叠一句"。
    AudioStopVoice();
    if (sound == kInvalidSound) {
        return;
    }
    if (AudioIsReady(sound)) {
        AudioPlayVoice(sound, volume);
        return;
    }
    if (AudioLoadStateOf(sound) != AudioLoadState::Loading) {
        return;  // 压根没这个句柄 / 已经失败：不排队
    }
    audio.hasPendingVoice = true;
    audio.pendingVoice = sound;
    audio.pendingVoiceVolume = volume;
}

void AudioStopVoice() {
    AudioState& audio = state();
    audio.hasPendingVoice = false;
    audio.pendingVoice = kInvalidSound;
    if (audio.voiceTrack != nullptr) {
        MIX_StopTrack(audio.voiceTrack, 0);
    }
    audio.voiceSound = kInvalidSound;
    audio.voiceRequested = false;
}

bool AudioIsVoicePlaying() {
    AudioState& audio = state();
    return audio.voiceRequested && audio.voiceTrack != nullptr
        && MIX_TrackPlaying(audio.voiceTrack);
}

float AudioVoicePositionSeconds() {
    AudioState& audio = state();
    if (audio.voiceTrack == nullptr) {
        return 0.0f;
    }
    const Sint64 frames = MIX_GetTrackPlaybackPosition(audio.voiceTrack);
    if (frames < 0) {
        return 0.0f;
    }
    return static_cast<float>(MIX_TrackFramesToMS(audio.voiceTrack, frames)) / 1000.0f;
}

float AudioVoiceVolume() {
    const AudioState& audio = state();
    if (!audio.voiceRequested) {
        return 0.0f;
    }
    return audio.channelVolume[static_cast<int>(AudioChannel::Voice)]
         * audio.voiceVolume;
}

// ---------------------------------------------------------------------------
// 音量
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 外部 PCM 流轨道（A14）：视频音轨 / 语音
// ---------------------------------------------------------------------------

/// 取一条**已经开着**的流（`stream != nullptr`）；否则 nullptr。
PcmStream* OpenPcmStream(PcmStreamId id) {
    AudioState& audio = state();
    if (id == kInvalidPcmStream) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(id) - 1u;
    if (index >= audio.pcmStreams.size()) {
        return nullptr;
    }
    PcmStream& slot = audio.pcmStreams[index];
    return slot.stream != nullptr ? &slot : nullptr;
}

PcmStreamId AudioOpenPcmStream(int sampleRate, int channels) {
    AudioState& audio = state();
    MIX_Mixer* mixer = audio.mixer.load();
    if (mixer == nullptr || sampleRate <= 0 || channels <= 0 || channels > 2) {
        return kInvalidPcmStream;  // 后端不在 / 参数非法：调用方照空转处理
    }
    for (std::size_t i = 0; i < audio.pcmStreams.size(); ++i) {
        PcmStream& slot = audio.pcmStreams[i];
        if (slot.stream != nullptr || slot.track == nullptr) {
            continue;  // 这个槽在用，或者轨道没建起来
        }
        // 目标格式取**混音器的设备格式**：于是 SDL 在 stream 内部就把源格式转成
        // 设备格式（重采样 / 声道转换），推数据的人完全不用操心这件事。
        SDL_AudioSpec device{};
        if (!MIX_GetMixerFormat(mixer, &device)) {
            device.format = SDL_AUDIO_S16;
            device.channels = 2;
            device.freq = 48000;
        }
        SDL_AudioSpec source{};
        source.format = SDL_AUDIO_S16;
        source.channels = channels;
        source.freq = sampleRate;

        SDL_AudioStream* stream = SDL_CreateAudioStream(&source, &device);
        if (stream == nullptr) {
            INK_LOG_ERROR("Audio",
                          std::string("建 PCM 流失败：") + SDL_GetError());
            return kInvalidPcmStream;
        }
        if (!MIX_SetTrackAudioStream(slot.track, stream)) {
            INK_LOG_ERROR("Audio", std::string("把 PCM 流挂上轨道失败：")
                                       + SDL_GetError());
            SDL_DestroyAudioStream(stream);
            return kInvalidPcmStream;
        }
        slot.stream = stream;
        slot.volume = 1.0f;
        slot.sampleRate = sampleRate;
        slot.channels = channels;

        // 起播但**立刻暂停**：等调用方说"播"。数据还没推进来时也不该空转，
        // 而且位置要从 0 开始算（音画同步的主时钟靠它）。
        MIX_SetTrackGain(
            slot.track,
            audio.channelVolume[static_cast<int>(AudioChannel::Voice)]);
        if (MIX_PlayTrack(slot.track, 0)) {
            MIX_PauseTrack(slot.track);
        } else {
            INK_LOG_WARN("Audio",
                         std::string("PCM 流轨道起播失败：") + SDL_GetError());
        }
        slot.playing = false;
        return static_cast<PcmStreamId>(i + 1u);
    }
    INK_LOG_WARN("Audio", "PCM 流池满了（最多 "
                              + std::to_string(kPcmStreamCount) + " 条）");
    return kInvalidPcmStream;
}

std::size_t AudioPushPcm(PcmStreamId id, const void* data, std::size_t bytes) {
    PcmStream* slot = OpenPcmStream(id);
    if (slot == nullptr || data == nullptr || bytes == 0) {
        return bytes;  // 没吃下：调用方攒着下次再推（空转也是这个语义）
    }
    const std::size_t target = PcmQueueTargetBytes(*slot);
    const int queuedRaw = SDL_GetAudioStreamQueued(slot->stream);
    const std::size_t queued =
        queuedRaw > 0 ? static_cast<std::size_t>(queuedRaw) : 0u;
    if (queued >= target) {
        return bytes;  // 水位到了：一点都不收（这就是背压）
    }
    const std::size_t room = target - queued;
    const std::size_t take = bytes < room ? bytes : room;
    if (!SDL_PutAudioStreamData(slot->stream, data, static_cast<int>(take))) {
        return bytes;
    }
    return bytes - take;
}

bool AudioPcmStreamWantsData(PcmStreamId id) {
    PcmStream* slot = OpenPcmStream(id);
    if (slot == nullptr) {
        return false;
    }
    const int queuedRaw = SDL_GetAudioStreamQueued(slot->stream);
    const std::size_t queued =
        queuedRaw > 0 ? static_cast<std::size_t>(queuedRaw) : 0u;
    return queued < PcmQueueTargetBytes(*slot);
}

void AudioSetPcmStreamPlaying(PcmStreamId id, bool playing) {
    PcmStream* slot = OpenPcmStream(id);
    if (slot == nullptr || slot->playing == playing) {
        return;  // 幂等：状态没变就不惊动 mixer
    }
    if (playing) {
        MIX_ResumeTrack(slot->track);
    } else {
        MIX_PauseTrack(slot->track);
    }
    slot->playing = playing;
}

float AudioPcmStreamPositionSeconds(PcmStreamId id) {
    PcmStream* slot = OpenPcmStream(id);
    if (slot == nullptr) {
        return 0.0f;
    }
    // ⚠️ `MIX_GetTrackPlaybackPosition` 给的是**采样帧**，**不是毫秒**——文档原话
    //    "Position is defined in _sample frames_ of decoded audio, not units of
    //    time... use MIX_TrackFramesToMS()"。直接除 1000 会得到一个荒唐的数
    //    （实测：真实过了 200ms，读出来 10.03 秒）。BGM 那边是同一个 API，
    //    要换算的时候照这里来。
    //    暂停 / 停止的轨道报的是"停住那一刻"的位置，所以它能直接当同步主时钟。
    const Sint64 frames = MIX_GetTrackPlaybackPosition(slot->track);
    if (frames <= 0) {
        return 0.0f;
    }
    const Sint64 ms = MIX_TrackFramesToMS(slot->track, frames);
    return ms > 0 ? static_cast<float>(ms) / 1000.0f : 0.0f;
}

void AudioSetPcmStreamVolume(PcmStreamId id, float volume) {
    PcmStream* slot = OpenPcmStream(id);
    if (slot == nullptr) {
        return;
    }
    slot->volume = Clamp01(volume);
    AudioState& audio = state();
    MIX_SetTrackGain(
        slot->track,
        audio.channelVolume[static_cast<int>(AudioChannel::Voice)] * slot->volume);
}

float AudioPcmStreamVolume(PcmStreamId id) {
    PcmStream* slot = OpenPcmStream(id);
    return slot != nullptr ? slot->volume : 0.0f;
}

void AudioClosePcmStream(PcmStreamId id) {
    PcmStream* slot = OpenPcmStream(id);
    if (slot == nullptr) {
        return;  // 幂等
    }
    // **先解绑再销毁**（混音器头文件写明：这个 stream 要有效到"轨道不再需要它"）。
    MIX_SetTrackAudioStream(slot->track, nullptr);
    MIX_StopTrack(slot->track, 0);
    SDL_DestroyAudioStream(slot->stream);
    slot->stream = nullptr;
    slot->volume = 1.0f;
    slot->playing = false;
    slot->sampleRate = 0;
    slot->channels = 0;
}

std::uint32_t AudioPcmStreamCount() {
    AudioState& audio = state();
    std::uint32_t count = 0;
    for (const PcmStream& slot : audio.pcmStreams) {
        if (slot.stream != nullptr) {
            ++count;
        }
    }
    return count;
}

void AudioSetMasterVolume(float volume) {
    state().master = Clamp01(volume);
    ApplyVolumes();
}

float AudioMasterVolume() { return state().master; }

void AudioSetChannelVolume(AudioChannel channel, float volume) {
    const int index = static_cast<int>(channel);
    if (index < 0 || index >= kAudioChannelCount) {
        return;
    }
    state().channelVolume[index] = Clamp01(volume);
    ApplyVolumes();
}

float AudioChannelVolume(AudioChannel channel) {
    const int index = static_cast<int>(channel);
    if (index < 0 || index >= kAudioChannelCount) {
        return 0.0f;
    }
    return state().channelVolume[index];
}

// ---------------------------------------------------------------------------
// 每帧
// ---------------------------------------------------------------------------

void AudioTick(float deltaSeconds) {
    AudioState& audio = state();
    if (audio.mixer.load() == nullptr) {
        return;
    }
    if (deltaSeconds < 0.0f) {
        deltaSeconds = 0.0f;
    }

    // 1. 消费后台加载完成的结果。
    bool volumesDirty = false;
    {
        std::vector<AudioState::Completed> finished;
        {
            std::lock_guard<std::mutex> lock(audio.doneMutex);
            finished.swap(audio.done);
        }
        for (AudioState::Completed& completed : finished) {
            // `inFlight` 由后台线程自减（见 QueueDecode），这里**不能**再减一次。
            ++audio.stats.loadsCompleted;
            audio.stats.decodeMicrosTotal += completed.micros;

            Sound* slot = MutableSound(completed.id);
            if (slot == nullptr || completed.generation != audio.generation.load()) {
                // 迟到（这期间音频层关过 / 句柄没了）：直接销毁，别泄漏。
                if (completed.audio != nullptr) {
                    MIX_DestroyAudio(completed.audio);
                }
                continue;
            }
            if (completed.audio == nullptr) {
                slot->state = AudioLoadState::Failed;
                ++audio.stats.loadFailures;
                INK_LOG_WARN("Audio", "异步加载失败：" + slot->name);
                continue;
            }
            slot->audio = completed.audio;
            slot->durationSeconds = completed.durationSeconds;
            slot->streaming = completed.streaming;
            slot->state = AudioLoadState::Ready;
            ++audio.stats.loads;
        }
    }

    // 2. 把等待队列里的请求投出去（并发上限 2）。
    while (audio.inFlight.load() < kMaxConcurrentLoads && !audio.waiting.empty()) {
        AudioState::Request request = audio.waiting.front();
        audio.waiting.erase(audio.waiting.begin());
        QueueDecode(request.id, request.path, request.mode);
    }

    // 3. 淡入淡出推进（自己算曲线：这样"半途的增益"能被断言）。
    if (audio.bgmFadePerSecond != 0.0f) {
        if (audio.bgmGain < audio.bgmFadeTarget) {
            audio.bgmGain = std::min(audio.bgmFadeTarget,
                                     audio.bgmGain + audio.bgmFadePerSecond * deltaSeconds);
        } else if (audio.bgmGain > audio.bgmFadeTarget) {
            audio.bgmGain = std::max(audio.bgmFadeTarget,
                                     audio.bgmGain - audio.bgmFadePerSecond * deltaSeconds);
        }
        if (audio.bgmGain == audio.bgmFadeTarget) {
            audio.bgmFadePerSecond = 0.0f;
        }
        volumesDirty = true;
    }
    if (audio.fading.active) {
        audio.fading.gain -= audio.fading.fadePerSecond * deltaSeconds;
        if (audio.fading.gain <= 0.0f) {
            audio.fading.gain = 0.0f;
            if (audio.fading.track != nullptr) {
                MIX_StopTrack(audio.fading.track, 0);
            }
            audio.fading.active = false;
        }
        volumesDirty = true;
    }

    // 4. 播完的 SE 回收。
    for (SeVoice& voice : audio.seVoices) {
        if (!voice.active) {
            continue;
        }
        if (voice.track == nullptr || !MIX_TrackPlaying(voice.track)) {
            voice.active = false;
            voice.sound = kInvalidSound;
        }
    }

    // 5. "等就绪再播"的 BGM。
    if (audio.hasPendingBgm) {
        if (AudioIsReady(audio.pendingBgm)) {
            const SoundId id = audio.pendingBgm;
            const float fade = audio.pendingBgmFade;
            const float volume = audio.pendingBgmVolume;
            audio.hasPendingBgm = false;
            audio.pendingBgm = kInvalidSound;
            AudioPlayBgm(id, fade, volume);
        } else if (AudioLoadStateOf(audio.pendingBgm) != AudioLoadState::Loading) {
            audio.hasPendingBgm = false;  // 失败了就别一直等
            audio.pendingBgm = kInvalidSound;
        }
    }

    // 6. "等就绪再播"的语音（B13，理由与 BGM 那条一模一样：这一屏该有的配音
    //    迟到也要补上——它常常还在后台解码时就轮到这一屏了）。
    if (audio.hasPendingVoice) {
        if (AudioIsReady(audio.pendingVoice)) {
            const SoundId id = audio.pendingVoice;
            const float volume = audio.pendingVoiceVolume;
            audio.hasPendingVoice = false;
            audio.pendingVoice = kInvalidSound;
            AudioPlayVoice(id, volume);
        } else if (AudioLoadStateOf(audio.pendingVoice) != AudioLoadState::Loading) {
            audio.hasPendingVoice = false;  // 失败了就别一直等
            audio.pendingVoice = kInvalidSound;
        }
    }

    if (volumesDirty) {
        ApplyVolumes();
    }
}

#else  // 没有 SDL3_mixer：整层空转（每个入口都是安全的空操作）

bool AudioInit() { return false; }

void AudioShutdown() { state().stats = AudioStats{}; }

bool AudioReady() { return false; }

AudioStats AudioStatsSnapshot() { return state().stats; }

std::string AudioDecoderName(int) { return {}; }

SoundId AudioLoad(const std::string&, AudioLoadMode) { return kInvalidSound; }

SoundId AudioLoadAsync(const std::string&, AudioLoadMode) { return kInvalidSound; }

SoundId AudioLoadFromMemory(const void*, std::size_t, const std::string&,
                            AudioLoadMode) {
    return kInvalidSound;
}

void AudioPreload(const std::vector<std::string>&) {}

AudioLoadState AudioLoadStateOf(SoundId) { return AudioLoadState::None; }

bool AudioIsReady(SoundId) { return false; }

float AudioDurationSeconds(SoundId) { return 0.0f; }

void AudioPlayBgm(SoundId, float, float) {}

void AudioPlayBgmAsync(SoundId, float, float) {}

void AudioStopBgm(float) {}

void AudioPauseBgm() {}

void AudioResumeBgm() {}

bool AudioIsBgmPlaying() { return false; }

bool AudioIsBgmPaused() { return false; }

float AudioBgmVolume() { return 0.0f; }

float AudioBgmPositionSeconds() { return 0.0f; }

void AudioPlaySe(SoundId, float) {}

void AudioPlaySeAsync(SoundId, float) {}

std::uint32_t AudioActiveSeCount() { return 0; }

// B13：角色语音——没有后端时全部空转。
void AudioPlayVoice(SoundId, float) {}

void AudioPlayVoiceAsync(SoundId, float) {}

void AudioStopVoice() {}

bool AudioIsVoicePlaying() { return false; }

float AudioVoicePositionSeconds() { return 0.0f; }

float AudioVoiceVolume() { return 0.0f; }

// A14：外部 PCM 流——没有后端时同样是**安全空转**：开流拿不到句柄，
// 推数据原样退回（调用方按"推不进去"处理即可，不需要写"如果没声音"的分支）。
PcmStreamId AudioOpenPcmStream(int, int) { return kInvalidPcmStream; }

std::size_t AudioPushPcm(PcmStreamId, const void*, std::size_t bytes) {
    return bytes;
}

bool AudioPcmStreamWantsData(PcmStreamId) { return false; }

void AudioSetPcmStreamPlaying(PcmStreamId, bool) {}

float AudioPcmStreamPositionSeconds(PcmStreamId) { return 0.0f; }

void AudioSetPcmStreamVolume(PcmStreamId, float) {}

float AudioPcmStreamVolume(PcmStreamId) { return 0.0f; }

void AudioClosePcmStream(PcmStreamId) {}

std::uint32_t AudioPcmStreamCount() { return 0; }

void AudioSetMasterVolume(float volume) {
    const float clamped = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    state().stats.masterVolume = clamped;
}

float AudioMasterVolume() { return state().stats.masterVolume; }

void AudioSetChannelVolume(AudioChannel, float) {}

float AudioChannelVolume(AudioChannel) { return 0.0f; }

void AudioTick(float) {}

#endif  // INK_HAS_SDL3_MIXER

}  // namespace inking
