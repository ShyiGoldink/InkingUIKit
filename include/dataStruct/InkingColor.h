#pragma once

// 颜色：本项目内部一律用 **0xAARRGGBB 的 32 位整数**表示。
//
// 为什么不是浮点 RGBA 结构体：渲染器最终要的就是 8 位通道（SDL_SetRenderDrawColor
// 收 Uint8），锚点基类里 `AnchorData::color` 也已经是这个形式。多存一份浮点表示
// 只会多一个"两份真相要同步"的地方。CXXCSS 配置里的 `"0|0|0|0.75"`
// （0~1 浮点、`|` 分隔）是**文件格式**，由生成器在编译期转成这里的整数，
// 运行期不再解析字符串。
//
// 通道语义固定为：A 在高 8 位，R 次之，最后 B。这样 `0xFF569CD6` 一眼能读出
// "不透明的 #569CD6"，十六进制字面量可以直接写进代码。

#include <algorithm>
#include <cstdint>

namespace inking {

/// 颜色通道：8 位，0 是最暗 / 全透明，255 是最亮 / 不透明。
struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

/// 颜色通道：浮点，0.0 ~ 1.0。只用于"从配置里的 0~1 写法换算过来"。
struct RgbaFloat {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

/// 0~1 的浮点通道 → 0~255 的整数通道。先夹到 [0,1] 再乘，
/// 免得配置里手滑写成 1.2 时溢出成一个奇怪的颜色。
inline std::uint8_t unitChannelToByte(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
}

/// 0~255 的整数通道 → 0~1 的浮点通道。
inline float byteChannelToUnit(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

/// 打包成 0xAARRGGBB。
inline constexpr std::uint32_t MakeColor(std::uint8_t r, std::uint8_t g,
                                         std::uint8_t b, std::uint8_t a) {
    return (static_cast<std::uint32_t>(a) << 24)
         | (static_cast<std::uint32_t>(r) << 16)
         | (static_cast<std::uint32_t>(g) << 8)
         | static_cast<std::uint32_t>(b);
}

/// CXXCSS 里的 `"0|0|0|0.75"` 那种写法（0~1 浮点）→ 0xAARRGGBB。
inline std::uint32_t FromUnitRgba(float r, float g, float b, float a = 1.0f) {
    return MakeColor(unitChannelToByte(r), unitChannelToByte(g),
                     unitChannelToByte(b), unitChannelToByte(a));
}

/// 0xAARRGGBB 的四个通道各是谁。
inline constexpr std::uint8_t ColorAlpha(std::uint32_t color) {
    return static_cast<std::uint8_t>((color >> 24) & 0xFFu);
}
inline constexpr std::uint8_t ColorRed(std::uint32_t color) {
    return static_cast<std::uint8_t>((color >> 16) & 0xFFu);
}
inline constexpr std::uint8_t ColorGreen(std::uint32_t color) {
    return static_cast<std::uint8_t>((color >> 8) & 0xFFu);
}
inline constexpr std::uint8_t ColorBlue(std::uint32_t color) {
    return static_cast<std::uint8_t>(color & 0xFFu);
}

/// 0xAARRGGBB → 0~1 浮点通道。反向换算，主要给自检和日志用。
inline RgbaFloat ToUnitRgba(std::uint32_t color) {
    return RgbaFloat{byteChannelToUnit(ColorRed(color)),
                     byteChannelToUnit(ColorGreen(color)),
                     byteChannelToUnit(ColorBlue(color)),
                     byteChannelToUnit(ColorAlpha(color))};
}

/**
 * @brief 两个 0xAARRGGBB 之间**逐通道**（含 alpha）线性插值。
 *
 * @param t 0 返回 `from`，1 返回 `to`，中间按比例混；超出会被夹到 [0,1]。
 *
 * 三态过渡（`ButtonLook::Advance`）就靠它。两个刻意的选择：
 *
 * 1. **逐通道**，不是"先混 rgb 再混 alpha"：三态之间往往只差透明度，
 *    逐通道插值对那种情况就是纯 alpha 渐变，不会顺手改动色相。
 * 2. **在 sRGB 空间直接插**（不做 gamma 校正）：CSS 的默认过渡也是这个口径，
 *    而按钮三态本来就在同一套色里挪 alpha / 亮度，观感上没有差别；
 *    要做"感知均匀"就得换颜色空间，那是样式层的事。
 */
inline std::uint32_t MixColor(std::uint32_t from, std::uint32_t to, float t) {
    const float k = std::clamp(t, 0.0f, 1.0f);
    const auto mix = [k](std::uint8_t a, std::uint8_t b) -> std::uint8_t {
        // 结果一定落在 [a,b] 里，+0.5 是四舍五入，不会溢出。
        return static_cast<std::uint8_t>(
            static_cast<float>(a)
            + (static_cast<float>(b) - static_cast<float>(a)) * k + 0.5f);
    };
    return MakeColor(mix(ColorRed(from), ColorRed(to)),
                     mix(ColorGreen(from), ColorGreen(to)),
                     mix(ColorBlue(from), ColorBlue(to)),
                     mix(ColorAlpha(from), ColorAlpha(to)));
}

}  // namespace inking
