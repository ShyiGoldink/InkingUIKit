// 定点浮窗落位算术的实现（`include/detail/InkingPopupPlacement.h`）。
//
// 这里只有加减乘除和两次夹取：任何一个判断写错，自检的 `check_popup_placement`
// 会立刻报出来（四组数字覆盖"翻左翻上 / 原地不动 / 贴边 / 比范围还大"）。

#include <detail/InkingPopupPlacement.h>

namespace inking {
namespace detail {
namespace {

/// 一轴的解（内部形态，最后拼成 `PopupPlacement`）。
struct AxisSolution {
    float position = 0.0f;
    float anchor = 0.0f;
    int side = 1;
    bool clamped = false;
};

bool Fits(float position, float size, float lo, float hi) noexcept {
    // 用 >= / <= 而不是严格不等：正好贴边算"放得下"，这样"窗口宽 1280、
    // 浮窗右边缘刚好 1280"不会被误判成越界然后夹紧（少一次翻转）。
    return position >= lo && (position + size) <= hi;
}

AxisSolution SolveAxis(float target, float size, float lo, float hi,
                       float preferred) noexcept {
    AxisSolution out;
    out.anchor = preferred;
    out.position = target - size * preferred;

    // ① 按期望方向放得下
    if (Fits(out.position, size, lo, hi)) {
        return out;
    }

    // ② 翻到对侧
    const float flipped = 1.0f - preferred;
    const float alt = target - size * flipped;
    if (flipped != preferred && Fits(alt, size, lo, hi)) {
        out.anchor = flipped;
        out.position = alt;
        out.side = -1;
        return out;
    }

    // ③ 夹紧。用翻转后的候选（它指向定点那一侧，视觉上"菜单从指针往回收"），
    //    翻转无意义时用原候选。
    out.clamped = true;
    out.side = 0;
    const float candidate = (flipped != preferred) ? alt : out.position;
    if (flipped != preferred) {
        out.anchor = flipped;
    }

    const float maxPosition = hi - size;
    if (size > (hi - lo)) {
        // 比范围还大：贴住起始边，允许另一侧溢出（用户说的"真的过大"）。
        out.position = lo;
        return out;
    }
    out.position = candidate < lo ? lo : (candidate > maxPosition ? maxPosition
                                                                : candidate);
    return out;
}

}  // namespace

PopupPlacement SolvePopupPlacement(float targetX, float targetY, int width,
                                   int height, const ShapeBounds& bounds,
                                   const AnchorPoint& preferred) {
    PopupPlacement out;
    out.selfAnchor = preferred;

    // 退化：尺寸或范围无效时"就放在定点上"，不翻转不夹紧。
    if (width <= 0 || height <= 0 || bounds.width <= 0.0f
        || bounds.height <= 0.0f) {
        out.x = targetX;
        out.y = targetY;
        return out;
    }

    const float loX = bounds.x;
    const float hiX = bounds.x + bounds.width;
    const float loY = bounds.y;
    const float hiY = bounds.y + bounds.height;

    const AxisSolution axisX = SolveAxis(targetX, static_cast<float>(width), loX,
                                         hiX, preferred.x);
    const AxisSolution axisY = SolveAxis(targetY, static_cast<float>(height),
                                         loY, hiY, preferred.y);

    out.x = axisX.position;
    out.y = axisY.position;
    out.sideX = axisX.side;
    out.sideY = axisY.side;
    out.clampedX = axisX.clamped;
    out.clampedY = axisY.clamped;

    // 两轴可以翻成不同的方向（右下角的小菜单：x 翻左、y 仍向下）。
    out.selfAnchor = AnchorPoint{axisX.anchor, axisY.anchor};
    return out;
}

}  // namespace detail
}  // namespace inking
