// 布局求解器与组件树的**接线**（A6 的后半截）。
//
// 它单独一个文件，是为了让 `InkingLayoutSolver.cpp` 保持**纯算术**：
// `Solve` / `SolveStack` / `SolveGrid` 只依赖 `dataStruct/InkingLayout.h`，
// 可以不起任何 Widget 就单测；而 `ArrangeSubtree` 必须知道组件树——
// 要读容器的 `LayoutSpec`、问每个孩子的 `MeasureLayout`、再把解出来的矩形
// 经 `ApplyLayoutRect` 写回去。
//
// 那条写入口是 Widget 层的 private + `friend`：**只有本文件的这个函数调得到**
// （见 `InkingWidget.h` 里 `ApplyLayoutRect` 的注释）——所以"业务代码改不了
// 静态组件的几何"是编译期保证，不是文档约定。也正因为这条 friend 关系，
// 本文件的位置可以随布局层走，但函数签名不能动。

#include <layout/InkingLayoutSolver.h>
#include <widget/InkingWidget.h>

#include <vector>

namespace inking {
namespace detail {
namespace layout {

void ArrangeSubtree(InkingWidget& container) {
    // 先把**这一层**排好（孩子的尺寸由此确定），再递归进"本身也是容器"的孩子——
    // 顺序反过来的话，子容器会用它**旧的**尺寸去排自己的内容，位置差一截。
    const LayoutSpec& spec = container.GetLayoutSpec();
    const std::vector<InkingWidget*>& children = container.GetChildren();

    if (spec.Valid() && !children.empty()) {
        const LayoutSize containerSize{static_cast<float>(container.GetWidth()),
                                       static_cast<float>(container.GetHeight())};
        const LayoutConstraints constraints{containerSize.width,
                                           containerSize.height};

        std::vector<LayoutInput> inputs;
        inputs.reserve(children.size());
        for (const InkingWidget* child : children) {
            LayoutInput input;
            input.item = child->GetLayoutItem();  // 孩子的意愿（margin / fill / min-max）
            input.desired = child->MeasureLayout(constraints);
            inputs.push_back(input);
        }

        const std::vector<LayoutOutput> outputs =
            Solve(spec, containerSize, inputs);
        for (std::size_t i = 0; i < children.size() && i < outputs.size(); ++i) {
            // 这条写入口是 private + friend：只有这里调得到（A6 定案）。
            children[i]->ApplyLayoutRect(outputs[i].rect);
        }
    }

    // 递归：孩子自己声明了布局的，继续往下排（样式表里的嵌套容器就是这么来的）。
    for (InkingWidget* child : children) {
        if (child->GetLayoutSpec().Valid()) {
            ArrangeSubtree(*child);
        }
    }
}
}  // namespace layout
}  // namespace detail
}  // namespace inking
