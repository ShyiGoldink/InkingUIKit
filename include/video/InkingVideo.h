#pragma once

#include <widget/InkingStaticWidget.h>
#include <event/InkingEvent.h>
#include <video/VideoData.h>

#include <cstdint>
#include <memory>

namespace inking {

/**
 * @brief 一个**视频**组件（A14）：把一段视频（含音轨）播在一个形状框里。
 *
 * 定位：**静态档**（几何构造即定型、进命中表、照常参与命中）——和图片一样，
 * 画面上的实体不该把点击漏给底下。运行期变的是**内容**，不是几何。
 *
 * ---------------------------------------------------------------------------
 * 四条设计决定（都可以推翻，写在这里免得以后猜）
 *
 *   1. **公开头不碰 FFmpeg**：解码器只出现在 `.cpp` 里（pimpl）。FFmpeg 是
 *      **可选**依赖，公开头一旦 include 它的头，"没装 FFmpeg 的机器连包含这个头
 *      都编译不过"——那就把可选依赖变成了硬依赖（`AGENTS.md` §3 第 2 条讲的是
 *      SDL，同一条理由）。
 *   2. **每帧推进挂在 `onRender` 上**：静态档收不到动态档的每帧 `Tick`
 *      （`AGENTS.md` §6 第 37 条），而 `onRender` 是唯一每帧都会来的钩子。
 *      副作用是**白拿的**：被裁剪 / 不可见时不再解码（省 CPU），回到可见时
 *      按 pts 追帧（丢帧数有观测口）。
 *   3. **画面走形状层**（`detail::fillShapeTextured`）：形状裁剪、UV、跟随容器
 *      变换、圆角羽化全部复用 B1 那份实现。于是"视频贴进圆角框、跟着容器动"
 *      不需要这里写一行几何代码；也和图片共用同一条纹理路径。
 *   4. **不认外部显示变换**（A13 那条通道默认关）：第一版不做"拖动视频"。
 *      要支持就在 `AcceptsExternalTransform()` 上返回 true、并让命中与包络跟着
 *      走——照 `InkingStaticImage` 抄。
 *
 * ---------------------------------------------------------------------------
 * 音画同步（A14 的重点）
 *
 *   音轨挂在音频层的**外部 PCM 流轨道**上（`AudioOpenPcmStream`，见
 *   `include/audio/InkingAudio.h`）。于是音量 / 暂停 / 位置全归音频层那套
 *   （`AudioChannel::Voice`），而**音频位置就是主时钟**：每帧拿
 *   `AudioPcmStreamPositionSeconds()` 跟视频自己的 pts 比——超前就等，落后就
 *   丢帧。没接音频后端时退回**真实时间**当主时钟：精度差一档，但不会不动。
 *
 * 解码在**后台线程**（照 `AGENTS.md` §6 第 82 条那条分工：worker 只做"打开 +
 * 解码"，结果入互斥队列，主线程消费），所以主线程一帧都不阻塞。
 */
class InkingStaticVideo : public InkingStaticWidget {
public:
    /**
     * @param parent 上级锚点；非拥有。
     * @param data   配置数据（构造时会 `Normalize` 一次，并按 `source` 打开视频）。
     */
    InkingStaticVideo(InkingWidget* parent, const VideoData& data);

    /// 析构要先**停掉解码线程**（生命周期比对象长的那类坑，见 §6 第 67 / 82 条）。
    ~InkingStaticVideo() override;

    InkingStaticVideo(const InkingStaticVideo&) = delete;
    InkingStaticVideo& operator=(const InkingStaticVideo&) = delete;

    // ---- 画什么 / 点得到吗（与图片同一套语义） ----

    /// 指针在这个位置上吗（世界坐标）。**照常参与命中**：视频不是标签。
    bool HitTest(float worldX, float worldY) const override;

    const ShapeSpec& GetShape() const noexcept;

    /// 没有画面时用的兜底色（`normal.color`）。
    std::uint32_t GetDisplayColor() const noexcept;

    TransformSpec GetDisplayTransform() const noexcept;
    TransformSpec GetSubtreeTransform() const noexcept override;

    /// 命中索引用的包络（按形状与那一态的变换算，保守外扩由框架加）。
    ShapeBounds GetHitEnvelope() const override;

    /// 构造时用的那份配置（已经 Normalize 过）。
    const VideoData& GetData() const noexcept;

    // ---- 播控（没接后端 / 没打开时都是**安全的空操作**） ----

    void Play();
    void Pause();

    /// 停下并回到开头（位置归零）。想恢复播放要显式 `Play()`。
    void Stop();

    /// 跳到某一秒（夹在 `[0, 时长]`）。没打开时忽略。
    void Seek(float seconds);

    void SetLoop(bool loop) noexcept;
    void SetVolume(float volume) noexcept;
    void SetMuted(bool muted) noexcept;

    // ---- 状态（"到底在不在播"看这些，不要靠猜画面） ----

    bool IsPlaying() const noexcept;

    /// 非循环模式下播到末尾了吗。
    bool IsFinished() const noexcept;

    /// 视频**打开成功**了吗（素材在 + 后端在）。空转时恒为 false。
    bool IsOpened() const noexcept;

    /// 已播位置（秒）。没打开时是 0。
    float GetPositionSeconds() const noexcept;

    /// 总时长（秒）。未知（还没打开 / 流式未知）时是 0。
    float GetDurationSeconds() const noexcept;

    int GetVideoWidth() const noexcept;
    int GetVideoHeight() const noexcept;

    /// 这条视频有音轨吗（没音轨 / 没音频后端都是 false）。
    bool HasAudioTrack() const noexcept;

    /**
     * 素材文件**在不在**（相对资源根解析之后）。
     *
     * 与 `IsOpened()` 是**两个口**：一个是"文件没找到"，一个是"找到了但打不开 /
     * 没接后端"。混成一句话时，"视频不动"就只能靠猜——而这层信息排错时最有用。
     */
    bool AssetExists() const noexcept;

    /// 解析出来的素材**绝对路径**（空 = 没解析到）。日志与排错用。
    const std::string& AssetPath() const noexcept;

    /// 解码线程读到文件末尾了吗（观测口）。排"为什么没播完"时先看它。
    bool IsAtEnd() const noexcept;

    // ---- 观测口（自检用；没有它们"丢帧追赶"这种事只能靠眼睛） ----

    std::uint32_t GetDecodedFrameCount() const noexcept;
    std::uint32_t GetPresentedFrameCount() const noexcept;
    std::uint32_t GetDroppedFrameCount() const noexcept;

    // ---- 事件 ----

    /**
     * 解码与播放状态：**定义在 `.cpp` 里**，这里只是个名字。
     *
     * 公开它不是为了给业务用——它是个**不完整类型**：既 new 不出来、也访问不到
     * 任何字段（那正是"公开头不拖 FFmpeg 进来"这条的实现方式）。公开的**唯一**
     * 理由是 `.cpp` 里那些解码辅助函数（打开 / 解码循环 / 每帧推进）需要写出这个
     * 类型名；放在 private 会让它们全部编译不过（`Impl is private within this
     * context`——实测踩过）。
     */
    struct Impl;

    /// 打开完成（拿到时长 / 尺寸）。空转时**不会**发。
    InkingEvent<> opened;

    /// 播完了（非循环）。循环播放时不会发。
    ///
    /// `mutable` 的理由：每帧的推进挂在 `onRender` 上（静态档唯一的每帧钩子，
    /// 见类注释第 2 条），而它是 `const`。而"发个事件通知一声"属于**逻辑常量性**
    /// 里的合法写入——它不是组件的可观察状态，是通知。
    mutable InkingEvent<> finished;

protected:
    /**
     * 画这一笔：有画面就按形状贴上去，没有就退回兜底色。
     *
     * 每帧的推进（解码取帧 / 上传纹理 / 按主时钟丢帧）也挂在这里——静态档
     * 只有这一个每帧钩子（见类注释第 2 条）。
     */
    void onRender(float pixelX, float pixelY) const override;

private:
    /// 把 data 搬进自己 + 基类；构造函数用。
    void applyData(const VideoData& data);

    std::unique_ptr<Impl> _impl;

    VideoData _data{};
    ButtonAppearance _renderAppearance{};

    /// "播完已经通知过了"：防重复发。也必须是 `mutable`（发它在 `onRender` 里）。
    mutable bool _finishedNotified = false;
};

}  // namespace inking
