#pragma once

// 形状之外的**装饰**：描边（stroke）与阴影（shadow）。
//
// 为什么和 `shape` 平级、而不是塞进形状参数里（`CXXCSS.md` §3.5 早就定了这条）：
// 形状只描述**几何**（`{ "type": "roundedRect", "radius": 10 }`），描边与阴影是
// 画在那个几何**之上/之下**的两笔。塞进形状参数的话，同一个圆角矩形会出两套
// 半径（"形状的"和"描边的"）；而且描边宽度一变，形状的身份（缓存键、命中）
// 就跟着变——那是两件互不相干的事被绑在一起。
//
// 命中**不受这两样影响**：命中判定的是形状本体（`ShapeContains`），描边画在
// 边界内外各一半、阴影整体偏在形状之外，两者都不参与命中（B3 的判据之一）。

#include <dataStruct/InkingColor.h>

#include <cstdint>

namespace inking {

/**
 * @brief 描边：沿形状边界画一圈，**宽度一半在外、一半在内**（照 CSS 的描边语义）。
 *
 * `width <= 0` 或颜色全透明 = 不画（生成器会拦下 `width <= 0` 的配置，
 * 这里只是运行期的兜底）。
 */
struct StrokeSpec {
    float width = 0.0f;                 ///< 描边宽度（设计坐标）；0 = 不画
    std::uint32_t color = 0xFF000000u;  ///< 0xAARRGGBB

    bool Visible() const noexcept {
        return width > 0.0f && ColorAlpha(color) > 0u;
    }
};

/**
 * @brief 阴影：形状的**偏移实心副本**——**没有模糊**。
 *
 * 模糊要离屏目标 + 卷积（或 SDF 软化），那是另一条渲染路径的事，先不假装有。
 * 现在这个是"偏移的实心块"，最常用于"卡片压在地上"那种硬阴影；
 * 要柔边就得等真做模糊那一轮（`UsageReport.md` 的"不成立"表里记着）。
 *
 * 偏移量是**设计坐标**（跟 `width` / `height` 同一套），绘制时乘展示倍率。
 */
struct ShadowSpec {
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    /**
     * 阴影色，`0xAARRGGBB`。
     *
     * ⚠️ **缺省必须是全透明（= 不画）**，不能是"半透明黑"那种"好看的默认值"：
     * `Visible()` 只看 alpha，`ButtonData` / `AnchorData` 又都拿结构体默认值构造，
     * 而生成器在配置里没写 `shadow` 段时**一个字都不输出**。默认给半透明黑的后果是
     * ——**每个按钮都白拿一层"偏移为 0 的阴影"**（正好盖在按钮自己身上），
     * 于是整个画面被压暗一层：实测纯白背景（`255,255,255`）读回来是
     * `150,150,150`，看起来就像"全局蒙了一层暗色遮罩"，而所有形状 / 颜色 / 混合同步
     * 都是对的（2026-10-08 实测，见 `docs/AGENTS.md` §6 第 112 条）。
     *
     * 判据与 `StrokeSpec` 同源：**没配置就是不可见**（那边靠 `width > 0`，
     * 这边靠 alpha == 0）。要阴影就在配置里显式写 `shadow` 段。
     */
    std::uint32_t color = 0x00000000u;

    bool Visible() const noexcept { return ColorAlpha(color) > 0u; }
};

}  // namespace inking
