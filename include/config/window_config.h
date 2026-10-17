// 设计常量。此文件将来由 inkgen 依据 UI 描述文件生成，请勿手改。
// 这些值是编译期常量：没有 setter，运行时只读。
#pragma once

namespace inking {

// 设计画布 = 720p。素材（背景 / CG）本身就是 1280×720，1:1 对应最清楚；
// 窗口怎么拉都不改设计坐标，只改展示倍率（letterbox 等比 + 黑边）。
inline constexpr int kDesignWidth  = 1280;
inline constexpr int kDesignHeight = 720;
// 扩展位：letterbox 模式、DPI 策略、渲染后端……

}  // namespace inking
