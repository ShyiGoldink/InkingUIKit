#pragma once

#include <button/ButtonData.h>

#include <cstdint>
#include <string>

namespace inking {

/// 这个模块的名字（日志里用；和别的模块一个约定）。
inline constexpr const char* kVideoModuleName = "InkingStaticVideo";

/// 画面怎么摆进框里（沿用图片那套 `ImageFit` 的语义与取值）。
enum class VideoFit : std::uint8_t {
    Contain,  ///< 完整放进框里（可能留白）
    Cover,    ///< 铺满框（超出的部分剪掉）
    Fill,     ///< 拉伸填满（不保持比例）
};

/// 播放选项（构造时给一次；运行期用组件上的写入口改）。
struct VideoOptions {
    bool autoplay = true;  ///< 上屏就播（第一帧画出来之后开始计时）
    bool loop = false;     ///< 播完重头再来
    bool muted = false;    ///< 不要声音（只要画面）
    float volume = 1.0f;   ///< 这一笔的音量（再乘 `Voice` 通道与主音量）
};

/**
 * @brief 视频组件的数据（A14）：**图片那份数据 + "放哪个文件、怎么播"**。
 *
 * 为什么也继承 `ButtonData`（和 `ImageData` 一样）：形状 / 描边阴影 / 锚点 /
 * 命中那一整套与图片完全同构，只有"画面从哪来"不同——图片来自一张静态文件，
 * 视频来自**解码器每帧产出的画面**。数据同构的好处照旧：往 `AnchorData` 加字段
 * 时这里不用再同步一次（`AGENTS.md` §6 第 83 条记的就是漏一处那种坑）。
 *
 * 三条与图片不同的约定：
 *   - `source` 是**视频文件**（相对资源根），`normal.source` 不参与视频读取；
 *   - `normal.color` 是**没有画面时的兜底色**（后端缺失 / 素材没打开 / 还没解出
 *     第一帧）——把画面变成一个洞比"缺视频"更难查；
 *   - 视频自带音轨，所以多一份 `options`（音量走音频层的 `Voice` 通道）。
 */
struct VideoData : public ButtonData {
    /// 视频文件路径：**相对资源根**（默认 exe 旁的 `assets/`），如 `"op.mp4"`。
    std::string source;

    VideoFit fit = VideoFit::Contain;

    VideoOptions options{};
};

}  // namespace inking
