#pragma once

// 音频层（B10）：**BGM + 一次性 SE + 淡入淡出 + 暂停 + 三级音量**。
//
// 后端是 **SDL3_mixer**（mp3 / ogg / flac / wav + 官方流式播放 + 官方多轨）。
// 它不在时整层退化成"无声空转"：所有调用都是安全的空操作，`AudioInit()`
// 返回 false——调用方**不需要**到处写"如果没声音"的分支。
//
// 四条设计取向（都可以推翻，写在这里免得以后猜）：
//
//   1. **多轨**：mixer 3.x 的 `MIX_Track` 是"调音台上一路推子"，**每路可以同时响**。
//      所以 BGM 交叉淡入淡出 = 两条轨道各写各的音量，不需要"先停再放"；
//   2. **加载与播放分开**：`AudioLoad*` 拿数据（可以同步、可以**异步**），
//      `AudioPlay*` 用它。异步加载走框架现成的 `TaskQueue`（复用 4 条管家线程 +
//      音频任务并发上限），**主线程一帧都不阻塞**——这是这一层最要紧的一条；
//   3. **流式与整段两档**：`AudioLoadMode::Auto` 按时长自己挑（长的流式、短的整段），
//      也可以显式指定。SE 与短 BGM 走整段（快、可反复触发），长 BGM 走流式
//      （驻留内存不随时长涨）；
//   4. **音量三级相乘**：`主音量 × 通道音量(BGM/SE) × 这一笔的音量`。
//      淡入淡出也是这一套：每帧算出增益写进轨道，所以**曲线能被自检断言**。
//
// 公开头**不拖 SDL 进来**（`AGENTS.md` §3 第 2 条）：这里只有 ID、枚举、普通函数。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inking {

/**
 * @brief 一段**已经加载好**的声音的句柄；`kInvalidSound` 表示无效。
 *
 * 为什么用整数而不是指针：句柄能安全地存进配置、跨场景传递，"还在加载 / 失败"
 * 也能用一个值说清楚。真正的数据在音频层内部，释放由 `AudioShutdown` 统一做。
 *
 * 异步加载时这个句柄**立刻**就有效——只是状态还是 `AudioLoadState::Loading`。
 */
using SoundId = std::uint32_t;

inline constexpr SoundId kInvalidSound = 0u;

/**
 * 加载模式。
 *
 *   - `Auto`      按时长挑（超过 `kStreamingThresholdSeconds` 就流式）；
 *   - `InMemory`  整段解码进内存（SE、短 BGM、要反复触发的音效）；
 *   - `Streaming` 边播边解码（长 BGM；驻留内存与时长无关）。
 */
enum class AudioLoadMode : std::uint8_t { Auto, InMemory, Streaming };

/// `Auto` 的分界线：长于这个秒数走流式。
inline constexpr float kStreamingThresholdSeconds = 10.0f;

/// 播放通道：只用来分组调音量（BGM 一路、SE 一路、**外部 PCM 流一路**）。
///
/// `Voice` 是 A14 随"外部 PCM 流轨道"加的：视频音轨、将来的角色语音都归它——
/// 这样"主 × 通道 × 单笔"那三级音量能一起管住它们，不必另开一套音量体系。
enum class AudioChannel : std::uint8_t { Bgm = 0, Se = 1, Voice = 2 };

inline constexpr int kAudioChannelCount = 3;

/// 一个句柄的加载状态。
enum class AudioLoadState : std::uint8_t {
    None,     ///< 没有这个句柄
    Loading,  ///< 后台正在解码（异步加载）
    Ready,    ///< 可以播
    Failed,   ///< 文件不在 / 格式不认识 / 解码器缺失
};

/**
 * @brief 音频层的观察口（自检与调试用）。
 *
 * "加载是不是真在后台"这件事，靠 `loadsPending` + `decodeMicrosTotal` 断言：
 * 调用 `AudioLoadAsync` 立刻返回、`loadsPending` 变 1，而这一段主线程的
 * `AudioTick` 照常推进。
 */
struct AudioStats {
    bool ready = false;              ///< 设备 / 混音器开起来了吗
    std::uint32_t decoderCount = 0;  ///< 后端报告的解码器数量（mp3/ogg/flac…）
    std::uint64_t loads = 0;         ///< 成功加载的声音数（累计）
    std::uint64_t loadFailures = 0;  ///< 加载失败次数（累计）
    std::uint32_t loadsPending = 0;  ///< 后台还在解码的数量
    std::uint64_t loadsCompleted = 0;///< 后台完成的次数（成功失败都算）
    std::uint64_t decodeMicrosTotal = 0;  ///< 后台解码累计耗时（微秒）
    std::uint32_t soundsResident = 0;     ///< 当前持有的声音数
    std::uint32_t tracksActive = 0;       ///< 正在响的轨道数（含 BGM 与 SE）
    std::uint64_t voicesStarted = 0;      ///< 起播次数（累计）
    float masterVolume = 1.0f;            ///< 主音量（读回用）
};

/// 打开音频设备与混音器。**可以重复调**；失败（没声卡 / 后端缺失）返回 false。
bool AudioInit();

/// 关掉并**释放全部已加载的声音**。幂等；之后再 `AudioInit` 可以重来。
void AudioShutdown();

bool AudioReady();

AudioStats AudioStatsSnapshot();

/// 后端的第 `index` 个解码器名字（`index` 越界返回空串）。自检用来断言"mp3 在"。
std::string AudioDecoderName(int index);

// ---------------------------------------------------------------------------
// 加载：同步 / 异步 / 预加载
// ---------------------------------------------------------------------------

/**
 * 从文件加载（**同步**，会阻塞到解码完）。小资源、自检、离线工具用它。
 *
 * 同一个路径**只解码一次**：重复调用返回同一个句柄。
 *
 * @return 句柄；文件不在 / 格式不认识 / 后端不可用时返回 `kInvalidSound`。
 */
SoundId AudioLoad(const std::string& path,
                  AudioLoadMode mode = AudioLoadMode::Auto);

/**
 * 从文件加载（**异步**）：立刻返回句柄（状态 `Loading`），解码在后台线程做。
 *
 * 这是运行期正确的那一条：进场景时请求资源，主线程继续跑；`AudioTick` 里
 * 把已经完成的结果挂上（同一个句柄的状态变成 `Ready`）。
 *
 * `Auto` 的目标时长探测**也在后台**做（探测要打开文件读头部，同样不该卡主线程）。
 */
SoundId AudioLoadAsync(const std::string& path,
                       AudioLoadMode mode = AudioLoadMode::Auto);

/**
 * 从内存字节加载（同步）。`name` 只做缓存键与日志。
 *
 * 字节会被**复制一份**由音频层持有（后端是零拷贝接过去的，所以那份副本要活到
 * 声音销毁）——调用方传完就可以扔掉自己的缓冲。
 */
SoundId AudioLoadFromMemory(const void* data, std::size_t size,
                            const std::string& name,
                            AudioLoadMode mode = AudioLoadMode::Auto);

/// 批量预加载（等价于对每个路径调一次 `AudioLoadAsync`）。
void AudioPreload(const std::vector<std::string>& paths);

/// 句柄的加载状态。
AudioLoadState AudioLoadStateOf(SoundId id);

/// 能播了吗（`Ready`）。
bool AudioIsReady(SoundId id);

/// 时长（秒）；未知 / 流式未知长度 / 无效句柄返回 0。
float AudioDurationSeconds(SoundId id);

// ---------------------------------------------------------------------------
// BGM（同一时刻一条；换歌 = 旧的淡出 + 新的淡入，**两条轨道同时存在**）
// ---------------------------------------------------------------------------

void AudioPlayBgm(SoundId sound, float fadeInSeconds = 0.0f,
                  float volume = 1.0f);

/**
 * 异步版：句柄还没就绪时**记住这次请求**，等它 `Ready` 的那一帧自动起播。
 *
 * 为什么它和 SE 的策略相反：BGM 是"这个场景该有的音乐"，迟到也要放；
 * 而 SE 迟到就没意义了（见 `AudioPlaySeAsync`）。
 */
void AudioPlayBgmAsync(SoundId sound, float fadeInSeconds = 0.0f,
                       float volume = 1.0f);

/// 停止 BGM（`fadeOutSeconds` 内音量降到 0，然后释放这条轨道）。
void AudioStopBgm(float fadeOutSeconds = 0.0f);

void AudioPauseBgm();
void AudioResumeBgm();
bool AudioIsBgmPlaying();
bool AudioIsBgmPaused();

/// BGM 当前的**实际**增益（通道音量与淡入淡出都算进去了，不含主音量）。
float AudioBgmVolume();

/// BGM 已播到的位置（秒）——"到底在不在响"最直接的观测口。
float AudioBgmPositionSeconds();

// ---------------------------------------------------------------------------
// SE（一次性，可叠加）
// ---------------------------------------------------------------------------

/// 放一声 SE（句柄没就绪就**什么也不做**——迟到的音效不如不响）。
void AudioPlaySe(SoundId sound, float volume = 1.0f);

/// 异步上下文里用的同一个入口（语义 = 未就绪丢弃，存在只是为了读起来清楚）。
void AudioPlaySeAsync(SoundId sound, float volume = 1.0f);

/// 当前在响的 SE 条数。
std::uint32_t AudioActiveSeCount();

// ---------------------------------------------------------------------------
// 角色语音（B13：一句话一条，**新的打断旧的**，走 `Voice` 通道）
// ---------------------------------------------------------------------------
//
// 为什么单开一条路而不是复用 SE / BGM：
//
//   1. **同一时刻只该有一句语音**：两句叠着响 = 两个人在同时说话。SE 池是"可叠加"
//      的设计，拿它播语音就会出现"连点几下，四五句台词一起念"；
//   2. **音量要能单独关**：玩家要的是"语音大一点 / BGM 小一点"，而语音归
//      `AudioChannel::Voice`（和视频音轨同一路），与 SE 那条互不影响；
//   3. **未就绪的策略与 BGM 一致（等就绪再播）**：语音文件是运行期从磁盘读的，
//      推进到这一屏时它常常还在后台解码——"这一句该有的配音"迟到也该补上，
//      不像 SE 那样迟到就没意义了。
//
// 语义只有三条：**新的打断旧的**、**空的/未就绪的丢掉请求**、**播完自动静音**
// （轨道还在，`AudioIsVoicePlaying()` 变假）。

/// 放一句语音（**打断上一句**）；句柄没就绪就什么也不做（要等就绪用下面的异步版）。
void AudioPlayVoice(SoundId sound, float volume = 1.0f);

/**
 * 异步版：**先停掉上一句**，句柄没就绪就记住这次请求，等它 `Ready` 的那一帧起播。
 *
 * 后一次请求会覆盖前一次的待播（"这一屏该说的那句话"永远只有一句）。
 */
void AudioPlayVoiceAsync(SoundId sound, float volume = 1.0f);

/// 停掉当前语音，并撤掉还没起播的待播请求。幂等。
void AudioStopVoice();

bool AudioIsVoicePlaying();

/// 这一句已经念到哪儿了（秒）。没在响返回 0。
float AudioVoicePositionSeconds();

/// 语音的**实际**增益（通道音量与这一笔都算进去了，不含主音量）。观测口。
float AudioVoiceVolume();

// ---------------------------------------------------------------------------
// 音量（三级相乘：主 × 通道 × 单笔）
// ---------------------------------------------------------------------------

void AudioSetMasterVolume(float volume);
float AudioMasterVolume();

void AudioSetChannelVolume(AudioChannel channel, float volume);
float AudioChannelVolume(AudioChannel channel);

// ---------------------------------------------------------------------------
// 外部 PCM 流轨道（A14）：视频音轨 / 将来的角色语音走它
// ---------------------------------------------------------------------------
//
// 为什么需要它：视频的音轨是**解码器边解边给**的 PCM，不是"一个文件"，
// 所以走不了 `AudioLoad` 那条路。而 mixer 3.x 原生支持把一条轨道的输入设成
// `SDL_AudioStream`（`MIX_SetTrackAudioStream`），于是：
//
//     解码侧解出 PCM → 推给这条流 → mixer 按**设备格式**重采样并混音输出。
//
// 好处是**音量 / 暂停 / 位置全归音频层**：三级音量里的 `AudioChannel::Voice`
// 那一档直接管住它，而音画同步的主时钟就是 `AudioPcmStreamPositionSeconds()`
// （底下是 `MIX_GetTrackPlaybackPosition`，与 BGM 那条同源）。
//
// 线程前提（`SKILL.md` §8 要求写死）：**全部在主线程调**——和 BGM / SE 一样。
// 解码 worker 只把 PCM 块交回主线程、由主线程推（`AGENTS.md` §6 第 82 条那条
// 分工）。将来要让 worker 直接推，再给这条通道加锁——现在不加锁，是为了和
// 这一层既有的"只有跨线程的 done/inFlight 才加锁"保持一致。

/// 一条外部 PCM 流的句柄；`kInvalidPcmStream` 表示无效。
using PcmStreamId = std::uint32_t;

inline constexpr PcmStreamId kInvalidPcmStream = 0u;

/**
 * 打开一条外部 PCM 流轨道。
 *
 * @param sampleRate 源采样率（Hz，必须 > 0）。和设备不一致也没关系，SDL 会转。
 * @param channels   源声道数（1 = 单声道，2 = 立体声）。
 * @return 句柄；后端不可用（没声卡 / 没编 SDL3_mixer）、参数非法、池满时返回
 *         `kInvalidPcmStream`——调用方**不需要**写分支，照空转处理即可。
 */
PcmStreamId AudioOpenPcmStream(int sampleRate, int channels);

/**
 * 推一段 PCM 进去（格式固定 **S16 交错**，字节数按源格式算）。
 *
 * @return 这次**没吃下**的字节数：0 = 全吃下了；> 0 = 缓冲满了（把剩下的留着，
 *         等 `AudioPcmStreamWantsData` 变真再推）。这就是解码侧要的**背压**。
 */
std::size_t AudioPushPcm(PcmStreamId stream, const void* data,
                         std::size_t bytes);

/// 现在还想不想要数据（排队量低于阈值）。解码侧靠它决定"要不要继续解"。
bool AudioPcmStreamWantsData(PcmStreamId stream);

/// 播放 / 暂停；暂停时位置**冻住**（和 BGM 那条一样的语义）。
void AudioSetPcmStreamPlaying(PcmStreamId stream, bool playing);

/// 已播位置（秒）——**音画同步的主时钟**。无效句柄返回 0。
float AudioPcmStreamPositionSeconds(PcmStreamId stream);

/// 这一笔的音量（再乘 `AudioChannel::Voice` 与主音量）。
void AudioSetPcmStreamVolume(PcmStreamId stream, float volume);

float AudioPcmStreamVolume(PcmStreamId stream);

/// 关掉一条流（幂等）：解绑 → 销毁这条流的缓冲。轨道随混音器一起回收。
void AudioClosePcmStream(PcmStreamId stream);

/// 现在开着几条流（观测口，自检用）。
std::uint32_t AudioPcmStreamCount();

// ---------------------------------------------------------------------------
// 每帧
// ---------------------------------------------------------------------------

/**
 * 每帧推进：淡入淡出、把增益写进轨道、消费后台加载完成的结果、
 * 清理播完的 SE、推进"等就绪再播"的 BGM 请求。
 *
 * **由窗口层的帧循环调**（`InkingWindow` 已经替你调了）。`deltaSeconds` 用真实
 * 流逝时间——声音不该跟着帧率走（`AGENTS.md` §6 第 25 条同一条理由）。
 */
void AudioTick(float deltaSeconds);

}  // namespace inking
