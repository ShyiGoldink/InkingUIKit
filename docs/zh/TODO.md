# 待办备忘录

> 这份文件只做一件事：**记下"之后要做"的事，防止想法丢失**。不是设计方案，不是任务书。
>
> - 每条独立编号（`T-1`、`T-2`…），**只追加、不重排**；做完在条目里写 `状态：已完成（日期 + 落点）`，不删条目。
> - 条目写清四件事：**要做什么 / 为什么（触发点）/ 落在哪（文件 + 锚点）/ 怎么算做完**。
>   缺了"落在哪"，这条以后就得重新调研一遍。
> - 需要展开成多步执行的，另立 `plans/YYYY-MM-DD-*.md`，这里只留一行指针。

---

## T-1 形状属性如何框定形状范围

**状态**：待办

**要做什么**

写 API 文档时，对 `InkingShapeSpec` 支持的**每一种形状**，逐一说明它的**每一个属性**在"框定形状范围"上起什么作用。

**为什么（触发点）**

`InkingShapeSpec` 是"假反射"结构体：13 种 `InkingShapeKind` 共用一个结构体，十几个字段里**任何时刻只有几个有意义**，其余被静默忽略。默认值本身合法 ⇒ 写错属性名、给错形状配错字段、抄了别的形状的配置，**都不报错**，只在画面上差一点。文档是唯一能挡住这类错的地方，而现在缺这一层。

**落在哪**

- 目标文档：`docs/zh/api/InkingShapeSpec.md`
  - 现有 `## 属性` 一节只列字段，没有"每种形状 × 每个属性 → 怎么决定范围"的对位说明。
  - 可参考的语义来源：`include/dataStruct/InkingShapeSpec.h` 顶部注释（坐标系与形状语义）、各 `static` 工厂函数的注释、`inking_detail` 里的夹取函数。
- 已有一份口径素材：`docs/zh/introductions/InkingShapeSpec.md`（含"假反射"取舍与角度约定），**属介绍层，别把 API 层的内容挪过去**。

**内容要求**

1. **覆盖全部 13 种 kind**：`Rect`、`RoundedRect`、`Circle`、`Ellipse`、`Capsule`、`Ring`、`Line`、`Polygon`、`ChamferRect`、`Star`、`Superellipse`、`Arc`、`PolygonPath`。
2. 每种形状分两栏写清属性作用：
   - **占位类**（决定形状能长到多大、要不要居中留白）：`width`/`height` 是否被用满，还是只取 `min(w,h)` 且居中。例：`Circle`/`Ring`/`Star`/`Arc` 取 `min(w,h)` 居中，`Rect`/`Ellipse`/`Capsule`/`ChamferRect`/`Superellipse` 用满 `w × h`。
   - **形态类**（决定边界在哪）：`radius`、`thickness`、`cap`、`sides`、`rotation`、`points`、`innerRatio`、`chamfer`、`exponent`、`startAngle`、`sweepAngle`、`pathPoints`/`pathPointCount`。
3. **标出"对本形状无效"的字段**，并明确写"无意义的值被忽略、不报错" —— 这正是需要读者警惕的静默面。
4. **标出夹取规则**（写文档时直接对着实现写，别自己算）：
   - `radius` → `ClampRadius`：上限 `min(w,h)/2`；负值断言。
   - `thickness`（`Ring`/`Arc`）→ 顶到外半径就停，退化为实心圆。
   - `chamfer` → 上限短边一半。
   - `exponent` → 夹到 `[0.05, 16]`。
   - `innerRatio` → 夹到 `[0, 1]`。
   - `sides` ≥ 3、`points` ≥ 3、`pathPointCount` ≥ 3。
   - `sweepAngle` → 取绝对值夹到 `(0, 360]`，≥360 退化成整圈 `Ring`。
5. 三方一致：**渲染 / 命中 / AABB 从同一份定义派生**（`InkingSignedDistance` → `InkingShapeContains` → `InkingGetShapeBounds`）。写属性作用时，若某属性会影响包围盒（如 `Line` 的 `thickness` 与 `cap` 外扩、`Circle` 居中留白），要单独点出来 —— 包围盒登记不全就是命中表漏命中。

**怎么算做完**

- 13 种 kind × 各自有意义的属性两两覆盖，无遗漏；无效属性的忽略行为有明确表述。
- 每条夹取规则能对上头文件里的实现（函数名或断言一一对应）。
- 生成器 JSON 侧写法（`{"type":"roundedRect","radius":10}`）与字段名一致，不出现头文件里没有的键。
