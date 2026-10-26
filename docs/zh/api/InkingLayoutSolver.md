# 布局求解器

**回到API目录：**
[API index](../API.md)

求解器是布局的**算术核**：给一份规则、容器尺寸、每个孩子的期望尺寸，算出每个孩子的矩形。
不碰组件、不碰场景、不标脏，所以可以单独用数值断言验。

为什么单拎出来：布局的坑几乎全在**算术**上（间距、剩余空间怎么分、min/max 什么时候夹、
溢出怎么办），而那些都能用数字钉死。真正的组件接线（把结果写回去 + 重烘命中表）
只是薄薄一层——那一层在 `detail::layout::ArrangeSubtree`
（声明在 `include/widget/InkingWidget.h`、实现在 `src/widget/InkingWidget.cpp`）。

**规则的数据面在 [InkingLayout.md](InkingLayout.md)**：`LayoutSpec` / `LayoutItem` /
`LayoutRect` / `LayoutSize` / `LayoutConstraints`。本页只讲怎么解。

`#include <layout/InkingLayoutSolver.h>`（`namespace inking::detail::layout`）。

## 用到的数据结构

>**LayoutInput**

```C++
struct LayoutInput {
    LayoutSize desired{};   // Measure 出来的期望尺寸（内容驱动：文字多高、图片多大）
    LayoutItem item{};      // 这个孩子的意愿：margin / fillMain / min-max / selfAlign
};
```

>**LayoutOutput**

```C++
struct LayoutOutput {
    LayoutRect rect{};      // 最终位置与尺寸，相对**父级左上角**
    LayoutSize size{};      // 最终尺寸（与 rect 的 width/height 一致，方便取用）
};
```

## APIS

>**Solve(spec, containerSize, items)**

```C++
std::vector<LayoutOutput> Solve(const LayoutSpec& spec,
                                const LayoutSize& containerSize,
                                const std::vector<LayoutInput>& items);
```

`items` 的**顺序 = 排布顺序**；返回与之等长的结果。
`spec.kind == None` 时原样返回期望尺寸、位置全 0（调用方本该先判 `Valid()`，
这里兜底不崩）。

规则（与 `InkingLayout.h` 的字段一一对应）：

1. 先在容器里扣掉 `padding`，剩下的叫**内容区**；
2. 主轴上：每项的主轴尺寸 = `desired` 夹到 `[min, max]`；
   再把 `Σ(主轴尺寸 + 主轴方向 margin) + gap × (n−1)` 求和；
3. **剩余空间**（内容区 − 上面的和）：
   - `> 0` 且有人声明 `fillMain` → **均分给它们**（均分后再夹一次 max）；
   - `> 0` 且没人 `fillMain` → 交给 `justify` 分配（center / end / space-between /
     around / evenly）；
   - `<= 0`（内容超出）→ **一律按 `Start` 排**，溢出交给容器自己的裁剪 / 滚动，
     求解器**不挤、不缩**；
4. 交叉轴上：`Stretch` 拉满内容区（扣掉两侧 margin），否则用 `desired` 的交叉轴尺寸，
   再按 `align`（孩子可用 `selfAlign` 覆盖）定位；
5. **`min/max` 永远最后夹**——它是约束，不是建议；
6. `Grid`：按 `columns` 等分列宽（`columnGap` 分隔），行高 = 该行里**最高**的那一项
   （含其 margin），单元格内按 `align` 摆放。

## 三个"就这样"的决定

| 情形 | 求解器的答案 | 为什么 |
| --- | --- | --- |
| 内容超出（剩余 < 0） | 按 Start 排，溢出不管 | 挤 / 缩会把"内容放不下"这个事实藏起来；裁剪 / 滚动是容器的职责 |
| 多个 `fillMain` | 均分，再各自夹 max | 剩余空间是"大家一起吃"；夹完还有剩就留在那里（不再二次分配，避免迭代） |
| `maxWidth/maxHeight` 为 0 | 视为**不限** | "0 = 不限"与 `LayoutConstraints` / `maxWidth` 的约定统一 |

## 与上层的接线（数据流）

```
容器 LayoutSpec ──┐
                  ├─► detail::layout::ArrangeSubtree(InkingWidget& container)
孩子 MeasureLayout ┘            │  读 spec + 每个孩子的 desired
                                ▼
                        layout::Solve(...)  →  LayoutOutput[]
                                │
                                ▼
                  InkingWidget::ApplyLayoutRect(rect)     // protected，只有 ArrangeSubtree 是 friend
                                │  反解成锚点公式里的量：锚点归零、尺寸取整、偏移写入
                                ▼
                     _width / _height / _offsetX / _offsetY
                                │
                                ▼
                     MarkStructureChanged()  →  帧首重烘命中表
```

三条与上层有关的约束（细节见 [InkingWidget.md](InkingWidget.md)）：

- **测量是纯函数**：`MeasureLayout` 不许改几何、不许标脏、不许有副作用——它可能被问好几遍。
- **写回是唯一入口**：`ApplyLayoutRect` 只有 `ArrangeSubtree` 是 friend，业务代码想调是
  **编译错误**，不是文档约定。
- **重排是显式的**：`Relayout()` / `RelayoutParent()` 由调用方在"排布输入变了"之后喊。
  框架不做隐式重排，代价是调用方要记得喊——所以两条都是幂等的。

## 常见坑

1. **指望求解器帮忙处理溢出**：它不挤不缩。要么裁剪（`clipChildren`），要么滚动（`scroll`）。
2. **在 `MeasureLayout` 里改自己的尺寸**：那会让"问一遍"变成"改一遍"，
   嵌套重排时结果不可预测。
3. **`columns <= 0`**：`LayoutSpec::columns` 至少 1（`Valid()` 只看 `kind`，
   这个值靠生成期 / 调用方把关）。
4. **用 `Solve` 的结果直接当绝对坐标**：它相对**父级左上角**，不是屏幕。

---

> 本文档对应版本：**0.5.3（0.x 发布版）**。
