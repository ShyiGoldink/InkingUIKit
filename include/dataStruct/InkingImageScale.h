#pragma once

// **素材该多大 / 纵深该选哪一档 / 采样比干不干净**——三条静态规则的纯函数版。
//
// 来源：`docs/zh/plans/2026-10-29-scaling-and-device-pixels.md` §3（0.7.0 T-02）。
// 这份头把那份文档里的三段散文变成可调用、可断言的东西，理由只有一个：
// **写在 README 里的公式没人验，也没人在改了组件尺寸之后重算一遍。**
//
// 三条规则（全部是"素材/使用者的责任"，不是渲染器的）：
//
//   1. 素材按**屏幕像素**出，不按"设计像素的倍数"出：
//        `素材像素 = 组件设计尺寸 × 纵深 × R_max`
//      推论：素材的绝对像素只取决于"这个组件最大占多少屏幕"，**与设计尺寸无关**。
//      所以"设计 640 会不会糊"这个问题本身是错的——糊的原因是素材按设计倍数出。
//
//   2. 纵深必须落在 `d = m / k` 上（m = 素材倍率 = 素材像素 ÷ 设计尺寸，k = 正整数）。
//      不做 mip、不做运行期重采样 ⇒ σ 必须正好是整数（=1 最清，=k 是干净的 1/k 缩小）。
//      非整数 σ 会有整列纹素被跳过，细节在运动时游走 / 闪烁——**比"缩得多"更难看**。
//
//   3. 边缘模糊有四个成因，本库只解决其中两个（素材不足 → 这条函数给阈值，
//      `INK_ISDEBUG` 下会 WARN；非整数 σ → `IsCleanRatio` 可判）。抗锯齿带按设备
//      像素改造推迟到 `1.x.x`，与素材分辨率**无关**（羽化在顶点 alpha 上）。
//
// ---------------------------------------------------------------------------
// 为什么是**纯头 + 纯函数**、而不是组件上的 setter
//
// 与 `InkingImageFitSpec`（`InkingImageFit.h`）同一形态：纯几何、可单测、绘制层只消费
// 结果。这条先例让"加一条规则"的评审成本接近零。
//
// 而"让组件接管纵深"（`SetDepth()` 自动算尺寸）**没有做**：那会把"纵深"这个概念
// 引进组件层，而纵深是**场景语义**（这个角色离镜头多远），组件只该知道"我占多大"。
// 真做纵深系统时再评估，不在这一版里预埋。
//
// 两者语义正交，所以**不并进 `InkingImageFitSpec`**：那个结构管"图在框里怎么摆"，
// 这个管"素材该多大"。合进去会让一个已经稳定的结构体承担两种职责。
// ---------------------------------------------------------------------------

#include <cmath>

/**
 * @brief 一次"这个组件要多大素材"的计算（纯数据 + 纯函数，不碰渲染器）。
 *
 * 用法（美术导出前 / 装载期自检 / `INK_ISDEBUG` 告警）：
 *
 * ```cpp
 * InkingImageScaleSpec spec;
 * spec.designWidth = 320;   // 组件在设计坐标里的宽度
 * spec.depth       = 1.0f;  // 纵深（没有纵深系统就填 1）
 * spec.ratioMax    = 2.0f;  // 最大目标屏 ÷ 设计尺寸；不知道就填 2
 * const int need = spec.RequiredTextureWidth();   // → 640
 * const float sigma = spec.SamplingRatio(textureWidth, currentRatio);
 * ```
 *
 * **单位**：`designWidth` 是设计坐标；`textureWidth` 是纹理像素；`ratio`（R）是
 * "1 设计单位 = 多少设备像素"的 letterbox 倍率（`min(屏宽/设计宽, 屏高/设计高)`）。
 */
struct InkingImageScaleSpec {
    int designWidth = 0;    ///< 组件设计宽度（设计坐标；高同理，按宽的算法类推）
    float depth = 1.0f;     ///< 纵深：1 = 贴着镜头那一层；0.5 = 退后一半
    float ratioMax = 0.0f;  ///< 最大目标屏 ÷ 设计尺寸（R_max）

    /// 三个字段都在合理范围里吗（绘制层用它拦掉"没有意义的计算"）。
    bool Valid() const noexcept {
        return designWidth > 0 && depth > 0.0f && ratioMax > 0.0f;
    }

    /// 这个组件在**最大目标屏**上最多占多少设备像素（`设计尺寸 × 纵深 × R_max`）。
    float ScreenWidthMax() const noexcept {
        return static_cast<float>(designWidth) * depth * ratioMax;
    }

    /**
     * @brief **规则一**：素材该出多宽（纹理像素）。
     *
     * `lround(设计尺寸 × 纵深 × R_max)`。取整而不是向上取：多出半个像素的素材
     * 换不来可见清晰度，却会让"按这个数导出"的图每边多一条透明边。
     */
    int RequiredTextureWidth() const noexcept {
        if (!Valid()) {
            return 0;
        }
        return static_cast<int>(std::lround(ScreenWidthMax()));
    }

    /**
     * @brief 采样比 σ = **素材像素 ÷ 它实际占的屏幕像素**。
     *
     * @param textureWidth 素材（这一帧）的像素宽
     * @param ratio        当前的 R（不是 R_max——这里量的是**这一次**的显示）
     *
     * 三个字段非法时返回 0（调用方据此跳过判定，而不是拿到一个假的 1）。
     *
     * 读法：`=1` 最清；`整数且 >1` 是干净的 1/k 缩小（信息只剩 1/k²）；
     * `非整数` 有纹素被跳过（脏）；`<1` 是放大（软）。
     */
    float SamplingRatio(int textureWidth, float ratio) const noexcept {
        const float screen =
            static_cast<float>(designWidth) * depth * ratio;
        if (textureWidth <= 0 || screen <= 0.0f) {
            return 0.0f;
        }
        return static_cast<float>(textureWidth) / screen;
    }

    /// 按 `ratioMax` 算的 σ——"在最坏的那块屏上还够不够"，装载期该问的就是它。
    float SamplingRatioAtMax(int textureWidth) const noexcept {
        return SamplingRatio(textureWidth, ratioMax);
    }

    /**
     * @brief **规则二**：σ 干不干净（是不是整数）。
     *
     * 带一个容差是因为 σ 是浮点算出来的：`3840 / (1280 × 1 × 3)` 未必正好是 1.0f，
     * 而"差一个 ulp 就判脏"会让这条规则在实际使用中永远为假，等于没有。
     *
     * 注意 `sigma = 0.5` **判脏**：0.5 是"放大一倍"，不是整数倍缩小。干净档位
     * 集合是 `{1, 2, 3, …}`，不含分数。
     */
    static bool IsCleanRatio(float sigma, float tolerance = 0.02f) noexcept {
        if (!(sigma > 0.0f)) {  // 顺带挡掉 NaN
            return false;
        }
        return std::fabs(sigma - std::round(sigma)) <= tolerance;
    }

    /**
     * @brief **规则二**的另一半：给定素材倍率，第 k 档干净纵深是多少。
     *
     * `m = 素材像素 ÷ 设计尺寸`（= 这份素材"最多能撑几倍"），干净纵深 = `m / k`。
     * 于是"选素材倍率"和"定纵深档位"是**同一件事**——这正是那张档位表的生成式：
     *
     * | m | k=1 | k=2 | k=3 | k=4 | k=5 |
     * | - | - | - | - | - | - |
     * | 1× | 1.0 | 0.5 | 0.333 | 0.25 | 0.2 |
     * | 2× | 1.0 | 0.667 | 0.5 | 0.4 | 0.333 |
     * | 0.5× | 0.5 | 0.25 | 0.167 | — | — |
     *
     * @param textureMultiple 素材倍率 m（≤0 或 k≤0 时返回 0）
     * @param divisor         k（正整数；σ 就等于 k）
     */
    static float CleanDepth(float textureMultiple, int divisor) noexcept {
        if (!(textureMultiple > 0.0f) || divisor <= 0) {
            return 0.0f;
        }
        return textureMultiple / static_cast<float>(divisor);
    }
};

