#include <field/InkingFieldProjection.h>

#include <cmath>
#include <limits>

// 纯函数层：**只依赖 <cmath> 与 <limits>**，不碰 SDL、不读全局配置。
// 这一份文件能不能脱离渲染器编过，是 `ctest -R field_pure_headers_have_no_sdl`
// 与 §9 那条 `-fsyntax-only` 在盯的事。

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

/// 这个纵深是不是已经贴在消失线上（`s` 趋 0 或翻负）。
constexpr float kMinScale = 1e-6f;

/// `ln_1p` 之类的精度不必求：这里只要"能不能算"和"算出来是不是正的"。
[[nodiscard]] bool FitsPositive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

/// 格子模式锁死的那两个参数（见文件头）。
[[nodiscard]] float EffectiveSkew(const InkingFieldProjection& projection) noexcept {
    return projection.mode == InkingFieldMode::Grid ? 0.0f : projection.skew;
}

/**
 * @brief 透视系数 `h = (1/depthScaleFar − 1) / farD`；`0` 表示**平行投影**。
 *
 * 格子模式锁 0，`depthScaleFar >= 1` 也归 0（那是"没有透视"，不是"反向透视"）。
 * 把这三条收敛到一个函数里，`Project` / `ScaleAt` / `DepthAt` / `DepthLimit`
 * 才不会各判各的——四处各写一份时，漏掉一处就是"画的和点的不一样"。
 *
 * `farD` 从这里**统一取 `InkingFieldEffectiveFarD`**：地平线覆盖（1.5）改的正是
 * 这个量，四处必须一起看见，否则天空带按覆盖、地面按作者值 ⇒ 地面从天空里穿出来。
 */
[[nodiscard]] float PerspectiveH(const InkingFieldProjection& projection) noexcept {
    if (projection.mode == InkingFieldMode::Grid) {
        return 0.0f;
    }
    if (!(projection.depthScaleFar > 0.0f) || projection.depthScaleFar >= 1.0f) {
        return 0.0f;
    }
    const float farD = InkingFieldEffectiveFarD(projection);
    if (!(farD > 0.0f)) {
        return 0.0f;
    }
    return (1.0f / projection.depthScaleFar - 1.0f) / farD;
}

/// `s = 1 / (1 + h·ld)`：让世界直线保持是直线的那个形式。
[[nodiscard]] float DepthScale(float h, float ld) noexcept {
    if (h == 0.0f) {
        return 1.0f;
    }
    const float denominator = 1.0f + h * ld;
    if (std::fabs(denominator) < kMinScale) {
        return 0.0f;
    }
    return 1.0f / denominator;
}

/// 浮点 → 格索引：夹进 `int32` 有效区间，避免"世界坐标一飘就算出 UB"。
[[nodiscard]] std::int32_t ClampIndex(float value) noexcept {
    constexpr float kMaxIndex = 2147483000.0f;
    constexpr float kMinIndex = -2147483000.0f;
    if (!(value == value)) {  // NaN
        return 0;
    }
    if (value > kMaxIndex) {
        return 2147483000;
    }
    if (value < kMinIndex) {
        return -2147483000;
    }
    return static_cast<std::int32_t>(value);
}

}  // namespace

float InkingFieldEffectiveFarD(const InkingFieldProjection& projection) noexcept {
    if (!(projection.horizonRatio >= 0.0f)) {
        return projection.farD;  // 缺省 `-1`（含 NaN）：不覆盖，逐位返回作者值
    }
    if (projection.mode == InkingFieldMode::Grid) {
        return projection.farD;  // 格子模式没有天空带，覆盖无从谈起
    }
    if (!(projection.depthScaleFar > 0.0f) || projection.depthScaleFar >= 1.0f) {
        return projection.farD;  // 平行投影：没有透视，也就没有"多远缩到最远"
    }

    // `H − cos·zoom/h = ratio·H`、`h = (1/dsf − 1)/farD`
    //   ⇒ farD = (1/dsf − 1)·(1 − ratio)·H / (cos·zoom)
    //
    // 注意 `h` 里 `(1/dsf − 1)` 与这里的同一因子约掉：所以同一个比例下**换
    // `depthScaleFar` 不会挪地平线**（它只改远处缩到多小），这正是"反解的是
    // farD 而不是把透视强度当变量"那句话的算术形式。
    const float cosSkew = std::cos(EffectiveSkew(projection) * kDegToRad);
    const float denominator = cosSkew * projection.camera.zoom;
    if (!(denominator > kMinScale)) {
        return projection.farD;  // 退化（skew→90° / zoom→0）：校验会挡，这里只兜底
    }
    const float resolved = (1.0f / projection.depthScaleFar - 1.0f)
                         * (1.0f - projection.horizonRatio) * projection.designHeight
                         / denominator;
    if (!(resolved > 0.0f) || !std::isfinite(resolved)) {
        // `ratio >= 1` 之类（绕过了校验）会落到这里：宁可回到作者值，也不给出
        // 一个负的"多远"——那会让 `h < 0`，整张地图翻过来。
        return projection.farD;
    }
    return resolved;
}

InkingFieldPrecomputed InkingFieldPrecompute(
    const InkingFieldProjection& projection) noexcept {
    const float skew = EffectiveSkew(projection) * kDegToRad;
    InkingFieldPrecomputed pre;
    pre.sinSkew = std::sin(skew);
    pre.cosSkew = std::cos(skew);
    pre.h = PerspectiveH(projection);
    pre.zoom = projection.camera.zoom;
    pre.centerX = projection.designWidth * 0.5f;
    pre.groundY = projection.designHeight;
    return pre;
}

InkingDesignPos InkingFieldProjectPrecomputed(const InkingFieldProjection& projection,
                                              const InkingFieldPrecomputed& pre,
                                              InkingFieldPos pos) noexcept {
    const float ld = pos.d - projection.camera.d;
    const float s = DepthScale(pre.h, ld);
    const float k = pre.zoom * s;  // 1 世界单位 = k 设计像素

    InkingDesignPos out;
    out.x = pre.centerX + (pos.x - projection.camera.x + pre.sinSkew * ld) * k;
    // 地面在屏幕底边：`ld = 0` 时 `sy = H`，纵深越大越往上（`cos` 为负贡献）。
    out.y = pre.groundY - pre.cosSkew * ld * k - pos.y * k;
    return out;
}

InkingDesignPos InkingFieldProject(const InkingFieldProjection& projection,
                                   InkingFieldPos pos) noexcept {
    return InkingFieldProjectPrecomputed(projection, InkingFieldPrecompute(projection), pos);
}

float InkingFieldScaleAt(const InkingFieldProjection& projection,
                         float depth) noexcept {
    const float h = PerspectiveH(projection);
    const float ld = depth - projection.camera.d;
    return projection.camera.zoom * DepthScale(h, ld);
}

float InkingFieldDepthAt(const InkingFieldProjection& projection,
                         float designY) noexcept {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float zoom = projection.camera.zoom;
    if (!(zoom > 0.0f) || !std::isfinite(designY)) {
        return nan;
    }
    const float cosSkew = std::cos(EffectiveSkew(projection) * kDegToRad);
    if (std::fabs(cosSkew) < kMinScale) {
        return nan;  // 深度线水平：屏幕上一个 y 对应无穷多个纵深
    }

    // `(H − py)/zoom = cos·ld·s`，而 `ld·s = ld/(1+h·ld)` —— 解一次一元方程。
    const float k = (projection.designHeight - designY) / (zoom * cosSkew);
    const float h = PerspectiveH(projection);
    if (h == 0.0f) {
        return k;
    }
    const float denominator = 1.0f - k * h;
    if (!(denominator > kMinScale)) {
        return nan;  // 地平线及以上
    }
    return k / denominator;
}

bool InkingFieldGroundHit(const InkingFieldProjection& projection,
                          InkingDesignPos design, InkingFieldPos* out) noexcept {
    const float ld = InkingFieldDepthAt(projection, design.y);
    if (!std::isfinite(ld)) {
        return false;
    }
    const float h = PerspectiveH(projection);
    const float s = DepthScale(h, ld);
    const float k = projection.camera.zoom * s;
    if (!(k > kMinScale)) {
        return false;  // 已经压在消失线上：屏幕点对应不到有限的地面点
    }
    const float skew = EffectiveSkew(projection) * kDegToRad;
    const float cx = projection.designWidth * 0.5f;

    InkingFieldPos resolved;
    resolved.x = projection.camera.x + (design.x - cx) / k - std::sin(skew) * ld;
    resolved.d = projection.camera.d + ld;
    resolved.y = 0.0f;
    if (!std::isfinite(resolved.x) || !std::isfinite(resolved.d)) {
        return false;
    }
    if (out != nullptr) {
        *out = resolved;
    }
    return true;
}

InkingFieldCell InkingFieldCellOf(const InkingFieldProjection& projection,
                                  InkingFieldPos pos) noexcept {
    InkingFieldCell cell;
    const float cellWidth = projection.grid.cellWidth;
    const float cellDepth = projection.grid.cellDepth;
    if (!(cellWidth > 0.0f) || !(cellDepth > 0.0f)) {
        return cell;  // 参数坏：返回 0 号格，由 `InkingFieldValidate` 负责发声
    }
    // **floor 不是 trunc**：负坐标是无限格的常态。
    cell.i = ClampIndex(std::floor(pos.x / cellWidth));
    cell.j = ClampIndex(std::floor(pos.d / cellDepth));
    return cell;
}

InkingDesignPos InkingFieldCellCenter(const InkingFieldProjection& projection,
                                      InkingFieldCell cell) noexcept {
    InkingFieldPos world;
    world.x = (static_cast<float>(cell.i) + 0.5f) * projection.grid.cellWidth;
    world.d = (static_cast<float>(cell.j) + 0.5f) * projection.grid.cellDepth;
    world.y = 0.0f;
    return InkingFieldProject(projection, world);
}

InkingDesignPos InkingFieldCellQuad(const InkingFieldProjection& projection,
                                    InkingFieldCell cell,
                                    InkingDesignPos out[4]) noexcept {
    const float cellWidth = projection.grid.cellWidth;
    const float cellDepth = projection.grid.cellDepth;
    const float x0 = static_cast<float>(cell.i) * cellWidth;
    const float d0 = static_cast<float>(cell.j) * cellDepth;
    const float x1 = x0 + cellWidth;
    const float d1 = d0 + cellDepth;

    // 绕序固定：(min x, min d) → (+x) → (+x, +d) → (+d)。提交三角化与自检
    // 都按这个顺序，改顺序等于改 UV 角，症状是"瓦片转了 90°"。
    if (out != nullptr) {
        out[0] = InkingFieldProject(projection, InkingFieldPos{x0, d0, 0.0f});
        out[1] = InkingFieldProject(projection, InkingFieldPos{x1, d0, 0.0f});
        out[2] = InkingFieldProject(projection, InkingFieldPos{x1, d1, 0.0f});
        out[3] = InkingFieldProject(projection, InkingFieldPos{x0, d1, 0.0f});
    }
    return InkingFieldCellCenter(projection, cell);
}

float InkingFieldHorizonY(const InkingFieldProjection& projection) noexcept {
    const float h = PerspectiveH(projection);
    if (!(h > 0.0f)) {
        // 平行投影：深度线永不相交 ⇒ **没有天空**，整屏都是地面。
        // 语义取"地平线在屏幕顶边"：天空带高度 0、地面带铺满。
        return 0.0f;
    }
    // `ld → 1/h` 时 `s → 0`，而 `ld·s → 1/h`：所以极限 `sy = H − cos(skew)·zoom/h`。
    const float cosSkew = std::cos(EffectiveSkew(projection) * kDegToRad);
    return projection.designHeight - cosSkew * projection.camera.zoom / h;
}

float InkingFieldDepthLimitForPixel(const InkingFieldProjection& projection) noexcept {
    const float infinity = std::numeric_limits<float>::infinity();
    const float h = PerspectiveH(projection);
    if (!(h > 0.0f)) {
        // 平行投影：格子永远不小于 1px ⇒ 不需要裁。这句就是"无限格子"的数学依据。
        return infinity;
    }
    const float smaller = projection.grid.cellWidth < projection.grid.cellDepth
                              ? projection.grid.cellWidth
                              : projection.grid.cellDepth;
    const float needed = smaller * projection.camera.zoom;
    if (!(needed > 1.0f)) {
        return 0.0f;  // 近处就已经不足 1px：这个配置本身没有可画的格子
    }
    return (needed - 1.0f) / h;
}

InkingFieldCellRange InkingFieldVisibleCells(const InkingFieldProjection& projection) noexcept {
    InkingFieldCellRange range;
    const float cellWidth = projection.grid.cellWidth;
    const float cellDepth = projection.grid.cellDepth;
    if (!(cellWidth > 0.0f) || !(cellDepth > 0.0f) || !(projection.camera.zoom > 0.0f)
        || !(projection.designWidth > 0.0f) || !(projection.designHeight > 0.0f)) {
        return range;
    }

    // ① 视口上下沿 ⇒ 深度区间。地面深度只跟屏幕 y 有关（同一 d 上 x 只是平移）。
    float ldNear = InkingFieldDepthAt(projection, projection.designHeight);
    if (!std::isfinite(ldNear)) {
        return range;
    }
    float ldFar = InkingFieldDepthAt(projection, 0.0f);

    // ② 地平线在屏幕里（或之上）时远端无解 ⇒ 由 1px 规则兜底；平行投影下
    //    `ldFar` 本来就有解（视口上沿 = 有限纵深）。
    if (!std::isfinite(ldFar)) {
        const float limit = InkingFieldDepthLimitForPixel(projection);
        ldFar = std::isfinite(limit) ? limit : ldNear + 1.0f;
    } else {
        const float limit = InkingFieldDepthLimitForPixel(projection);
        if (std::isfinite(limit) && ldFar > limit) {
            ldFar = limit;
        }
    }

    // ③ 有限地图：夹到 `[0, rows·cellDepth]`。
    if (projection.rows > 0) {
        const float mapFar = static_cast<float>(projection.rows) * cellDepth;
        if (ldFar > mapFar - projection.camera.d) {
            ldFar = mapFar - projection.camera.d;
        }
        if (ldNear < -projection.camera.d) {
            ldNear = -projection.camera.d;
        }
    }
    if (ldFar < ldNear) {
        // 相机在斜投影下已经跑出地图（或参数退化）：这一屏没有格可画。
        return range;
    }

    // ④ 横向范围。`x(ld)` 对 `ld` 是**线性**的（1/(zoom·s) 与 sin(skew)·ld 都线性），
    //    所以极值必在两端 —— 取四个角就够，不必采样，也不会漏。
    const float h = PerspectiveH(projection);
    const float skew = EffectiveSkew(projection) * kDegToRad;
    const float sinSkew = std::sin(skew);
    const float cx = projection.designWidth * 0.5f;

    float minX = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    const float depths[2] = {ldNear, ldFar};
    const float edges[2] = {0.0f, projection.designWidth};
    for (float ld : depths) {
        const float k = projection.camera.zoom * DepthScale(h, ld);
        if (!(k > kMinScale)) {
            return range;  // 远端已经压在消失线上
        }
        for (float designX : edges) {
            const float worldX =
                projection.camera.x + (designX - cx) / k - sinSkew * ld;
            minX = worldX < minX ? worldX : minX;
            maxX = worldX > maxX ? worldX : maxX;
        }
    }

    // 上界用 `ceil(...) − 1` 而不是 `floor(...)`：格是**半开区间**
    // `[i·cw, (i+1)·cw)`，所以"和 [minX, maxX) 有交集的最大格"是
    // `ceil(maxX/cw) − 1`。用 `floor` 会在 maxX 正好落在格边界上时多算一整行 /
    // 一整列（1280 / 8 = 160 时多出第 160 列），那一片永远没有东西可画，
    // 而且让"可见格数"变成一个没人能预判的奇数。
    float i0 = std::floor(minX / cellWidth);
    float i1 = std::ceil(maxX / cellWidth) - 1.0f;
    const float dNear = projection.camera.d + ldNear;
    const float dFar = projection.camera.d + ldFar;
    float j0 = std::floor(dNear / cellDepth);
    float j1 = std::ceil(dFar / cellDepth) - 1.0f;

    if (projection.cols > 0) {
        const float upper = static_cast<float>(projection.cols - 1);
        i0 = i0 < 0.0f ? 0.0f : i0;
        i1 = i1 > upper ? upper : i1;
    }
    if (projection.rows > 0) {
        const float upper = static_cast<float>(projection.rows - 1);
        j0 = j0 < 0.0f ? 0.0f : j0;
        j1 = j1 > upper ? upper : j1;
    }
    if (i1 < i0 || j1 < j0) {
        return range;
    }

    range.i0 = ClampIndex(i0);
    range.i1 = ClampIndex(i1);
    range.j0 = ClampIndex(j0);
    range.j1 = ClampIndex(j1);

    // 防御：参数被填成"每格 0.001 像素"时这里会是个天文数字。宁可整屏不画并
    // 返回 invalid（调用方看得见），也不要让一次枚举吃掉几 GB。
    const double cells = (static_cast<double>(range.i1) - range.i0 + 1.0)
                       * (static_cast<double>(range.j1) - range.j0 + 1.0);
    if (cells > 4.0e6) {
        return InkingFieldCellRange{};
    }
    range.valid = true;
    return range;
}

bool InkingFieldCellInMap(const InkingFieldProjection& projection,
                          InkingFieldCell cell) noexcept {
    if (projection.cols > 0 && (cell.i < 0 || cell.i >= projection.cols)) {
        return false;
    }
    if (projection.rows > 0 && (cell.j < 0 || cell.j >= projection.rows)) {
        return false;
    }
    return true;
}

bool InkingFieldValidate(const InkingFieldProjection& projection,
                         const char** why) noexcept {
    const auto fail = [why](const char* message) noexcept {
        if (why != nullptr) {
            *why = message;
        }
        return false;
    };

    if (!FitsPositive(projection.grid.cellWidth)) {
        return fail("grid.cellWidth 必须 > 0（一格的横向世界尺寸，就是瓦片像素宽）");
    }
    if (!FitsPositive(projection.grid.cellDepth)) {
        return fail("grid.cellDepth 必须 > 0（一格的纵深世界尺寸）");
    }
    if (!FitsPositive(projection.camera.zoom)) {
        return fail("camera.zoom 必须 > 0");
    }
    if (!FitsPositive(projection.designWidth) || !FitsPositive(projection.designHeight)) {
        return fail("designWidth / designHeight 必须 > 0");
    }
    if (projection.skyFit != InkingSkyFit::Cover && projection.skyFit != InkingSkyFit::Stretch
        && projection.skyFit != InkingSkyFit::Tile) {
        return fail("skyFit 取值 0..2（Cover / Stretch / Tile）");
    }

    if (projection.mode == InkingFieldMode::Oblique) {
        if (!std::isfinite(projection.skew) || projection.skew < 0.0f
            || projection.skew > 80.0f) {
            return fail("skew 有效区间 [0, 80]（度）；80 以上 cos 趋 0，地面反解无解");
        }
        if (!(projection.depthScaleFar > 0.0f) || projection.depthScaleFar > 1.0f) {
            return fail("depthScaleFar 取值 (0, 1]；1 = 无透视（> 1 是反向透视）");
        }
        if (projection.depthScaleFar < 1.0f && !FitsPositive(projection.farD)) {
            return fail("depthScaleFar < 1 时 farD 必须 > 0（多远缩到最远）");
        }

        // 地平线位置（1.5）：`-1` = 不覆盖，`[0, 1)` = 覆盖。**不夹取**——
        // `1.0` 意思是"地平线在屏幕底边、一格地面都不剩"，那是参数写错了，不是
        // 想贴着边。所以只报错，不留一个"看起来能用"的近似值。
        const bool noOverride = projection.horizonRatio == -1.0f;
        const bool override_ = projection.horizonRatio >= 0.0f && projection.horizonRatio < 1.0f;
        if (!noOverride && !override_) {
            return fail("horizonRatio 只能取 -1（不覆盖）或 [0, 1)（天空带占画布高度的比例）");
        }
        if (override_) {
            if (!(projection.depthScaleFar < 1.0f)) {
                return fail("horizonRatio 需要透视：depthScaleFar 必须 < 1"
                            "（无透视时本来就没有天空带，覆盖无意义）");
            }
            // 直接验**不变量本身**（而不是验中间量）：反解只要让地平线落在
            // `ratio × designHeight`，覆盖就是成立的。
            const float target = projection.horizonRatio * projection.designHeight;
            const float horizon = InkingFieldHorizonY(projection);
            if (!std::isfinite(horizon) || std::fabs(horizon - target) > 0.5f) {
                return fail("horizonRatio 反解出的 farD <= 0（cos(skew)·zoom 退化，换不出地平线）");
            }
        }

        if (projection.cols <= 0 || projection.rows <= 0) {
            return fail("倾斜模式必须是有界地图（cols / rows 都 > 0）");
        }
    }

    if (why != nullptr) {
        *why = nullptr;
    }
    return true;
}
