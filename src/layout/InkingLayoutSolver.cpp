#include <layout/InkingLayoutSolver.h>

#include <algorithm>
#include <cmath>
#include <vector>

// 布局求解器（A6）的实现。规则写在头文件里，这里只说三处实现取向：
//
//   1. **一切按 float 算，最后才取整**：取整放在"写回组件几何"那一步（那边是
//      int），求解器只管算术——不然每步取整的误差会累积成"排着排着差了 1px"。
//   2. **`0` 表示"这一项没设"**（min / max / 约束都是），这样默认值就是
//      "不限制"，配置里也就不用写 `-1` 那种哨兵。
//   3. **溢出不做特殊处理**：内容超出容器时按 Start 排、让它露出去，由容器的
//      裁剪（A5）去剪。求解器"自己缩一缩"那种聪明做法，只会让布局在边界上
//      出现没法解释的结果。

namespace inking {
namespace detail {
namespace layout {

namespace {

float ClampSize(float value, float minValue, float maxValue) {
    float result = value;
    if (minValue > 0.0f && result < minValue) {
        result = minValue;
    }
    if (maxValue > 0.0f && result > maxValue) {
        result = maxValue;
    }
    return result;
}

/// 主轴方向的两个量（把"竖着排 / 横着排"的差异收在这一层）。
struct AxisAccessor {
    bool vertical = true;

    float Main(const LayoutSize& size) const {
        return vertical ? size.height : size.width;
    }
    float Cross(const LayoutSize& size) const {
        return vertical ? size.width : size.height;
    }
    float MainSpacing(const LayoutSpacing& spacing) const {
        return vertical ? spacing.top + spacing.bottom
                        : spacing.left + spacing.right;
    }
    float MainLeading(const LayoutSpacing& spacing) const {
        return vertical ? spacing.top : spacing.left;
    }
    float MainTrailing(const LayoutSpacing& spacing) const {
        return vertical ? spacing.bottom : spacing.right;
    }
    float CrossLeading(const LayoutSpacing& spacing) const {
        return vertical ? spacing.left : spacing.top;
    }
    float CrossTrailing(const LayoutSpacing& spacing) const {
        return vertical ? spacing.right : spacing.bottom;
    }
    float CrossSpacing(const LayoutSpacing& spacing) const {
        return CrossLeading(spacing) + CrossTrailing(spacing);
    }
};

/// 把"主轴位置 + 交叉轴位置 + 尺寸"组装回 `LayoutRect`。
LayoutRect MakeRect(const AxisAccessor& axis, float mainPos, float crossPos,
                    float mainSize, float crossSize) {
    LayoutRect rect{};
    if (axis.vertical) {
        rect.x = crossPos;
        rect.y = mainPos;
        rect.width = crossSize;
        rect.height = mainSize;
    } else {
        rect.x = mainPos;
        rect.y = crossPos;
        rect.width = mainSize;
        rect.height = crossSize;
    }
    return rect;
}

/// 交叉轴上的摆放：给"项占的交叉轴长度"和"可用交叉轴长度"，算起点偏移。
float CrossOffset(LayoutAlign align, float available, float used) {
    switch (align) {
        case LayoutAlign::Center:
            return (available - used) * 0.5f;
        case LayoutAlign::End:
            return available - used;
        case LayoutAlign::Start:
        case LayoutAlign::Stretch:
        default:
            return 0.0f;
    }
}

LayoutAlign AlignOf(const LayoutItem& item, LayoutAlign containerAlign) {
    return item.hasSelfAlign ? item.selfAlign : containerAlign;
}

/// 一列 / 一行的解。
void SolveStack(const LayoutSpec& spec, const LayoutSize& containerSize,
                const std::vector<LayoutInput>& items,
                std::vector<LayoutOutput>& out) {
    const AxisAccessor axis{spec.axis == LayoutAxis::Vertical};
    const float contentMain = axis.Main(containerSize)
                            - axis.MainSpacing(spec.padding);
    const float contentCross = axis.Cross(containerSize)
                             - axis.CrossSpacing(spec.padding);
    const float startMain = axis.MainLeading(spec.padding);
    const float startCross = axis.CrossLeading(spec.padding);

    const std::size_t count = items.size();

    // 1. 每项的主轴尺寸（先不看 fill：fill 吃的是"剩余空间"）。
    std::vector<float> mainSizes(count, 0.0f);
    std::vector<float> crossSizes(count, 0.0f);
    float usedMain = 0.0f;
    int fillCount = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const LayoutInput& input = items[i];
        mainSizes[i] = ClampSize(axis.Main(input.desired), 
                                 axis.vertical ? input.item.minHeight
                                               : input.item.minWidth,
                                 axis.vertical ? input.item.maxHeight
                                               : input.item.maxWidth);
        // 交叉轴：Stretch 先按"拉满"记，等剩余空间定了再回填。
        const LayoutAlign align = AlignOf(input.item, spec.align);
        crossSizes[i] = align == LayoutAlign::Stretch
                            ? 0.0f
                            : ClampSize(axis.Cross(input.desired),
                                        axis.vertical ? input.item.minWidth
                                                      : input.item.minHeight,
                                        axis.vertical ? input.item.maxWidth
                                                      : input.item.maxHeight);
        usedMain += mainSizes[i] + axis.MainSpacing(input.item.margin);
        if (input.item.fillMain) {
            ++fillCount;
        }
    }
    if (count > 1) {
        usedMain += spec.gap * static_cast<float>(count - 1);
    }

    // 2. 剩余空间：有人声明 `fillMain` 就**均分给它们**（均分后再夹一次 max；
    //    被 max 夹住而没吃满的那部分就空着——v1 不做"二次再分配"，
    //    那会让"谁最终多宽"变成一件要盯着看才知道的事）。
    //    没人 fill 才轮到 `justify` 去分配剩余空间。
    float remaining = contentMain - usedMain;
    if (remaining > 0.0f && fillCount > 0) {
        const float share = remaining / static_cast<float>(fillCount);
        for (std::size_t i = 0; i < count; ++i) {
            if (!items[i].item.fillMain) {
                continue;
            }
            const float maxMain = axis.vertical ? items[i].item.maxHeight
                                                : items[i].item.maxWidth;
            mainSizes[i] = ClampSize(mainSizes[i] + share, 0.0f, maxMain);
        }
        remaining = 0.0f;  // 剩余空间已经归它们了，justify 不再参与
    }

    // 3. justify：剩余空间怎么变成"起点偏移 + 间隙增量"。
    float cursor = startMain;
    float extraGap = 0.0f;
    if (remaining > 0.0f && fillCount == 0) {
        switch (spec.justify) {
            case LayoutJustify::Center:
                cursor += remaining * 0.5f;
                break;
            case LayoutJustify::End:
                cursor += remaining;
                break;
            case LayoutJustify::SpaceBetween:
                if (count > 1) {
                    extraGap = remaining / static_cast<float>(count - 1);
                } else {
                    cursor += remaining * 0.5f;  // 只有一个：居中更好看
                }
                break;
            case LayoutJustify::SpaceAround:
                cursor += remaining / (2.0f * static_cast<float>(count));
                extraGap = remaining / static_cast<float>(count);
                break;
            case LayoutJustify::SpaceEvenly:
                cursor += remaining / (static_cast<float>(count) + 1.0f);
                extraGap = remaining / (static_cast<float>(count) + 1.0f);
                break;
            case LayoutJustify::Start:
            default:
                break;
        }
    }

    // 4. 逐项落位。
    for (std::size_t i = 0; i < count; ++i) {
        const LayoutInput& input = items[i];
        const LayoutAlign align = AlignOf(input.item, spec.align);
        const float mainPos = cursor + axis.MainLeading(input.item.margin);
        const float mainSize = mainSizes[i];

        const float crossAvailable =
            contentCross - axis.CrossSpacing(input.item.margin);
        float crossSize = crossSizes[i];
        if (align == LayoutAlign::Stretch) {
            crossSize = ClampSize(crossAvailable, 0.0f,
                                  axis.vertical ? input.item.maxWidth
                                                : input.item.maxHeight);
        }
        const float crossPos = startCross + axis.CrossLeading(input.item.margin)
                             + CrossOffset(align, crossAvailable, crossSize);

        out[i].rect = MakeRect(axis, mainPos, crossPos, mainSize, crossSize);
        out[i].size = LayoutSize{out[i].rect.width, out[i].rect.height};
        cursor += mainSize + axis.MainTrailing(input.item.margin) + spec.gap
                + extraGap;
    }
}

/// 网格的解：定列数、逐行填；行高取该行最高的一项。
void SolveGrid(const LayoutSpec& spec, const LayoutSize& containerSize,
               const std::vector<LayoutInput>& items,
               std::vector<LayoutOutput>& out) {
    const int columns = spec.columns > 0 ? spec.columns : 1;
    const float contentWidth = containerSize.width
                             - spec.padding.left - spec.padding.right;
    const float columnWidth =
        (contentWidth - spec.columnGap * static_cast<float>(columns - 1))
        / static_cast<float>(columns);

    const std::size_t count = items.size();
    // 行高 = 该行里"期望高度 + 上下 margin"的最大值。
    const auto rowHeight = [&](std::size_t rowStart) {
        float height = 0.0f;
        for (std::size_t i = rowStart;
             i < count && i < rowStart + static_cast<std::size_t>(columns); ++i) {
            const float one = ClampSize(items[i].desired.height,
                                        items[i].item.minHeight,
                                        items[i].item.maxHeight)
                            + items[i].item.margin.top
                            + items[i].item.margin.bottom;
            height = one > height ? one : height;
        }
        return height;
    };

    float rowTop = spec.padding.top;
    for (std::size_t rowStart = 0; rowStart < count;
         rowStart += static_cast<std::size_t>(columns)) {
        const float height = rowHeight(rowStart);
        for (std::size_t i = rowStart;
             i < count && i < rowStart + static_cast<std::size_t>(columns); ++i) {
            const LayoutInput& input = items[i];
            const std::size_t column = i - rowStart;
            const float cellLeft = spec.padding.left
                                 + static_cast<float>(column)
                                       * (columnWidth + spec.columnGap);
            const float cellWidth =
                columnWidth - input.item.margin.left - input.item.margin.right;
            const LayoutAlign align = AlignOf(input.item, spec.align);
            const float desiredWidth = ClampSize(input.desired.width,
                                                 input.item.minWidth,
                                                 input.item.maxWidth);
            const float width = align == LayoutAlign::Stretch
                                    ? std::max(0.0f, cellWidth)
                                    : std::min(desiredWidth,
                                               std::max(0.0f, cellWidth));
            const float cellHeight =
                height - input.item.margin.top - input.item.margin.bottom;
            const float desiredHeight = ClampSize(input.desired.height,
                                                  input.item.minHeight,
                                                  input.item.maxHeight);
            // 单元格里的高度也用同一套 `align`：Stretch = 拉满这一行，
            // 否则用期望高度 + 对齐偏移（这样"一格里内容怎么摆"与栈是同一套语义）。
            const bool stretch = align == LayoutAlign::Stretch;
            const float finalHeight = stretch ? std::max(0.0f, cellHeight)
                                              : std::min(desiredHeight,
                                                         std::max(0.0f, cellHeight));
            const float x = cellLeft + input.item.margin.left
                          + CrossOffset(align, cellWidth, width);
            const float y = rowTop + input.item.margin.top
                          + CrossOffset(stretch ? LayoutAlign::Stretch : align,
                                        cellHeight, finalHeight);
            LayoutRect rect{};
            rect.x = x;
            rect.y = y;
            rect.width = width;
            rect.height = finalHeight;
            out[i].rect = rect;
            out[i].size = LayoutSize{width, finalHeight};
        }
        rowTop += height + spec.rowGap;
    }
}

}  // namespace

std::vector<LayoutOutput> Solve(const LayoutSpec& spec,
                                const LayoutSize& containerSize,
                                const std::vector<LayoutInput>& items) {
    std::vector<LayoutOutput> out(items.size());
    if (items.empty()) {
        return out;
    }

    if (spec.kind == LayoutKind::None) {
        // 不布局：原样返回期望尺寸、位置全 0（调用方本该先判 `Valid()`）。
        for (std::size_t i = 0; i < items.size(); ++i) {
            out[i].size = items[i].desired;
            out[i].rect = LayoutRect{0.0f, 0.0f, items[i].desired.width,
                                     items[i].desired.height};
        }
        return out;
    }

    if (spec.kind == LayoutKind::Grid) {
        SolveGrid(spec, containerSize, items, out);
        return out;
    }

    SolveStack(spec, containerSize, items, out);
    return out;
}
}  // namespace layout
}  // namespace detail
}  // namespace inking
