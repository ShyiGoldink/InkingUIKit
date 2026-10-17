# CXXCSS 配置格式

CXXCSS 是本项目的**界面描述文件**：在 `CXXCSS/` 下按组件类型建目录、一个组件一个 `.json`，
生成器 `inkgen`（源码在 `tools/inkgen/`）在构建期读它们、校验后在 `build/` 下生成 C++：
**每个名字一个类**（按钮 / 面板 / 标签 / 图片）+ 一份**语言表**的静态数据 + 启动期就位的
登记对象；另读一张**主题表**（`CXXCSS/Theme/*.json`），把配置里的 `"$panelSurface"` /
`"$title"` 这类**语义名**在生成期换成具体值（§3.9，产物里一个 `$` 都不剩）。手写组件那条路
（`ButtonData` / `PanelData` / `LabelData` / `ImageData`）仍然可用，两条路落到同一份数据结构上。

**怎么用生成的结果**：构建脚本调 `ink_add_generated()`，代码里
`#include <inkgen/<你的集合名>/button_service.h>`，直接用生成的类
（`inking::cxxcss::NormalButton`；字符串查找键是 `::kName`）。

> **本仓库没有消费示例**：源项目的 `examples/panel_demo/` 没有随搬家带入，`examples/`
> 现在只有 `demo/`（手写数据那条路）。生成结果仍会被**构建期编译验证**——
> `inkgen_compile_check_generated` 把 `button_service_register.cpp` 编一遍，
> 语法/签名不对就构建失败。要看生成的东西长什么样，直接读
> `build/inkgen/all/button_service.h`。

---

## 1. 目录与命名

```text
CXXCSS/
├─ CXXCSS.md              ← 本文档
├─ Button/   button.example.json（静态按钮示例）、svgPath.example.json（SVG path 形状）
├─ Panel/    drawerPanel.json（抽屉）、clipWindow.json（裁剪）、columnPanel.json（布局）、
│            labelBoard.json（装标签）、scrollPanel.json（滚动）、panel.example.json
├─ Label/    menuTitle.json、hintBody.json、ellipsisNote.json、langBadge.json
├─ Image/    cardArt.json（运行期读图）
├─ Text/     ui.json（语言表）
└─ Theme/    dark.json（主题表）
```

> **没有 `Scene/`**：场景手写、不生成（§7）。原先的 `CXXCSS/Scene/<场景名>.json` 撤掉了（§7.2）。

三条**必须**遵守的命名规则：

1. **目录名 = 组件类型**（`Button` / `Panel` / `Label` / `Text` / `Theme` / `Image`）：认不出的
   目录名**报错**、不会猜；写法大小写不敏感、单复数都认（`Button` / `buttons` 同理）。
   **加一个组件类型** = 生成器里加一条目录名映射 + 一个 Loader + 发射器里一段（要改哪几处
   写在 `tools/inkgen/Emit.h` 顶上）。⚠️ `Text` 是**语言表**（§3.8）、不是"文字组件"（文字
   组件叫 `Label`）；`Theme` 是**主题表**（§3.9），是**生成器的输入**、不是组件；
2. **文件名 = `name` 字段**：两者不一致时报错（类名与查找键都取自这个名字）；
3. 以 `.example.json` 结尾的文件只作参考，**不参与生成**。

名字必须唯一且稳定：两个文件认领同一个 `name` 时，生成器在**生成期**报"已经被 xxx 用了"，
而且是**跨组件类型**的（一个按钮和一个面板同名同样是错——名字是运行期的全局查找键）；
运行期的库也会驳回后来者，但那只是静默的错。

**生成器做到哪一步**：每个独立名字一个独立的类（`normalButton` → `inking::cxxcss::NormalButton`），
颜色 / 尺寸 / 形状 / 锚点全部烘成字面量，构造时不查任何表。还没做的是 §6 第 3 条（按布局生成
专用查找）与"专属命中结构"这类特化。`name` 必须是**合法的 C++ 标识符**（`normalButton` 行，
`button.example` 不行）；名字里有 `.` 或 `-` 时只是没有类——数据照样进 `RegisterAllButtons()`，
只能按字符串名字用。生成的类放在**由构建脚本指定**的命名空间里（本项目示例用 `inking::cxxcss`）。

> **`width` / `height` 两种写法**：数字和"数字字符串"都收（历史遗留，见 §2.1），新配置用数字。

**六种目录类型各生成什么**（`Text` / `Theme` 与其它几种形态不同，见 §3.8 / §3.9 / §3.10）：

| 目录 | 生成物 | 用起来 |
| --- | --- | --- |
| `Button/` | 一个按钮类（可选动态档） | `inking::cxxcss::NormalButton btn{parent};` |
| `Panel/` | 一个面板类 | `inking::cxxcss::DrawerPanel drawer{parent};` |
| `Label/` | 一个标签类 | `inking::cxxcss::MenuTitle title{parent};` |
| `Image/` | 一个图片类（**没有 onClicked 的按钮**） | `inking::cxxcss::CardArt card{parent};` |
| `Text/` | **语言表的静态数据 + 安装函数**（不是类） | 启动期自动装；业务侧只用 `SetTextLanguage()` |
| `Theme/` | **什么都不生成**（它是生成器的输入） | 组件配置里写 `"$token"`；换主题要重新生成 |

---

## 2. 公共骨架

任何组件配置的超集都长这样（以 Button 为例，实际见 `Button/button.example.json`）：

```jsonc
{
    "$comment": ["……"],        // 注释，生成器不读
    "name": "button.example",   // 必须与文件名一致
    "width": "200", "height": "100",              // 设计坐标
    "normal":    { "type": "color", "source": "0|0|0|0.75",
                   "transition": { "duration": 0.3 } },   // 进这个状态用多久（可选）
    "hover":     { "type": "color", "source": "0|0|0|0.85",
                   "transform": { "translateY": -6, "scale": 1.05 } },  // 平移/旋转/缩放（可选）
    "onclicked": { "type": "color", "source": "0|0|0|0.75" },
    "selfAnchorX": 0.5, "selfAnchorY": 0.5,       // 自身锚点
    "traceAnchorX": 0,  "traceAnchorY": 0,        // 上级锚点
    "shape": { "type": "roundedRect", "radius": 10 }
}
```

### 2.1 书写约定

| 约定 | 说明 |
| --- | --- |
| 编码 | **UTF-8**（无 BOM） |
| 注释 | 用 `"$comment"` 字段，字符串或字符串数组都行；生成器跳过它 |
| 键名 | 统一 **camelCase**（`selfAnchorX`），别用下划线 |
| 未知字段 | 生成器**报错**而不是静默忽略——拼错的字段比缺字段更难查 |
| 数值类型 | `width` / `height` 收字符串与数字两种（历史遗留）；**新字段一律用数字** |

### 2.2 锚点

锚点用**百分比**：(0,0) 是左上角、(1,1) 是右下角。`selfAnchorX/Y` 是**自身**以内部哪个点去
对应，`traceAnchorX/Y` 是那个点对上**父级**的哪个位置；组件在用锚点时一律被视作矩形，
位置不作为字段存在、由锚点推导（`docs/API.md`「InkingAnchor」）。允许任意浮点数，但**建议
保持在 `[0,1]`**——超出范围的行为未定义。旧示例里 `traceAnchorY` 曾拼成 `trancAnchorY`；
生成器会把拼错的键当未知字段报错，这类错不会静默通过。

---

## 3. 形状层（shape）

`shape` **同时决定**渲染（SDF）、命中（`contains`）与索引（AABB），且**三者由同一份定义
派生，不允许各写一遍**——否则会出现"画的是圆角、点到的是直角"：两边单独看都对，合起来才错。

### 3.1 写法：对象 + 参数，**不要把参数编进字符串**

```jsonc
"shape": { "type": "roundedRect", "radius": 10 }    // ✅ 推荐
"shape": "rounded-rect-10"                          // ❌ 不要
```

生成器要按 `type` 查形状表做参数校验；把参数塞进字符串就得再写一个迷你解析器，
也没法给出"radius 必须为正"这种具体报错。

### 3.2 形状可以省略

不写 `shape`（或写 `{}`）等价于 `{"type":"rect"}`——由 `width` / `height` 定义的直角矩形，
所以老配置不加字段也能继续工作。

### 3.3 形状表

**第一档：形状词汇的地基**

| `type` | 参数 | 说明 |
| --- | --- | --- |
| `rect` | 无（尺寸来自 `width`/`height`） | 直角矩形，默认值 |
| `roundedRect` | `radius` | 圆角矩形。`radius` 会被夹到 `min(width,height)/2` |
| `circle` | 无 | 正圆，直径取 `min(width,height)`，居中 |
| `ellipse` | 无 | 椭圆，用满 `width`×`height` |

**第二档：UI 高频（几何 / 渲染 / 生成器都已落地）**

| `type` | 参数 | 说明 |
| --- | --- | --- |
| `capsule` | 无 | 胶囊：用满 `width`×`height`，两端是半径 `min(w,h)/2` 的半圆，**沿长轴**放（竖框得到竖胶囊） |
| `ring` | `thickness` | 圆环：**居中**、外径取 `min(w,h)`，`thickness` 是环宽（设计坐标，必须 > 0），从外径往**里**长 |
| `line` | `thickness`、`cap`（可选） | 水平线段、**纵向居中**（分割线就是它）：`thickness` 是线宽（必须 > 0）；`cap` 取 `butt`（默认）/ `round` / `square`，语义照 CSS 的 `stroke-linecap` |
| `polygon` | `sides`、`rotation`（可选） | 正多边形：**居中**、外接圆直径取 `min(w,h)`；`sides` ≥ 3 且必须是整数；`rotation` 是**起始角**，单位**度**（跟 `transform.rotate` 统一），省略 = 0（第一个顶点在正右方），顺时针为正 |

其中两条：`line` 只画一条**水平**线段（要斜线/竖线由组件自己的 `transform.rotate` 给，形状层
不再开一组"从哪儿画到哪儿"的参数——那会变成第二个几何真相）；`ring` 的 `thickness` 超过
`min(w,h)/2` 时**夹到上限**（那时环没有孔了，退化成实心圆），跟 `radius` 的上限夹取同一类。
`line` 的 `round` / `square` 端点会让画出来的东西**超出 `width`**（两端各 `thickness/2`），
包围盒与命中区域跟着一起超出——这是有意的（两者同源），不是 bug。

**第三档：B5 已全部落地**

| `type` | 参数 | 说明 |
| --- | --- | --- |
| `chamferRect` | `chamfer` | 切角矩形：四条边照旧，四个角用 45° 直线切掉 `chamfer` 长。超过短边一半时**夹住**（退化成矩形） |
| `star` | `points`、`innerRatio`（可选，0.5）、`rotation`（可选） | 星形：**居中**、外接圆直径取 `min(w,h)`；`points` ≥ 3 且必须是整数；`innerRatio` 是内/外半径之比（0~1，越小越尖）；`rotation` 是第一个**外角**的方向（度） |
| `superellipse` | `exponent` | 超椭圆（squircle）：用满 `width`×`height`；`exponent` > 0，`2` 就是椭圆、越大越接近矩形（上限夹到 16） |
| `arc` | `thickness`、`sweepAngle`、`startAngle`（可选） | 圆弧：**居中**、外径取 `min(w,h)`，`thickness` 是环宽（必须 > 0）；从 `startAngle`（度，缺省 0 = 正右方）起**顺时针**扫 `sweepAngle` 度（必须非 0、在 -360~360 之间）；两端**径向切平**。整圈请直接用 `ring` |

**任意多边形（B2 的形状层）：`polygonPath`**

| `type` | 参数 | 说明 |
| --- | --- | --- |
| `polygonPath` | `vertices` | 顶点表：`[x0,y0, x1,y1, …]`（**设计坐标**，相对组件左上角），至少 3 个点、必须是**偶数个**数字。可以**非凸**，甚至可以不是星形域（U 形那种，中心落在凹口里）——填充走 ear clipping，凹口不会被填实 |

```jsonc
// U 形：上部横带 + 两条腿，中间那块是凹口
"shape": { "type": "polygonPath",
           "vertices": [20,20, 180,20, 180,180, 140,180, 140,60, 60,60, 60,180, 20,180] }
```

它是**折线**（顶点之间走直线）；SVG 的 `path` 由生成器在**生成期**折线化成同一个形状
（B2 已落地，认 `M L H V Z C S Q T A`，见 §4.1）。描边（`stroke`）对它同样有效——顶点偏移走
角平分线（miter），尖角处夹到 2×宽度。

**明确不支持**（别指望 `shape` 能表达）：

| 需求 | 为什么不行 | 该走什么 |
| --- | --- | --- |
| 虚线边框 | 不是单一距离场 | 专门的 dash 逻辑 |
| 形状层直接收任意贝塞尔曲线 | 需要数值最近点求解，代价高 | 先折线化成 `polygonPath`（SVG `path` 就是这么做的） |
| 文字 | 字形是另一条路 | `text` 段（见 §5） |
| 纹理填充 | 形状只管"点是否在内" | 纹理层 + 形状裁剪（`detail::fillShapeTextured`） |

### 3.4 关于符号约定（一处与旧文档的偏差）

SDF 的**通用约定**是：`f < 0` 在内部、`f = 0` 在边界、`f > 0` 在外部。本项目原先在
`docs/AGENTS.md` 里写的是「裁剪 = `f >= 0`」，方向是反的；**形状层采用通用约定（负 = 内）**，
裁剪的语义相应写成「保留 `f <= 0` 的区域」（`docs/AGENTS.md` 已同步）——这样才能照抄公开的
SDF 参考实现，抗锯齿也就是标准写法 `smoothstep(-aa, aa, d)`。

### 3.5 形状与语义：形状 ≠ 填充

形状只描述**几何**，颜色/描边是另一层的事：一段配置里 `"shape"` 与 `"normal"` 平级。
描边（`stroke`）与阴影（`shadow`）同样**与 `shape` 平级**——**不要把描边宽度塞进 `shape`
的参数里**：那会让同一个圆角矩形出两套参数，而且宽度一变，形状的身份（顶点缓存键、
命中包络）就跟着变，那是两件不相干的事。

```jsonc
"shape":  { "type": "roundedRect", "radius": 10 },
"stroke": { "width": 2, "color": "1|1|1|0.35" },
"shadow": { "offsetX": 0, "offsetY": 4, "color": "0|0|0|0.45" }
```

| 字段 | 子字段 | 说明 |
| --- | --- | --- |
| `stroke` | `width`（必填）、`color`（可选，默认黑） | 沿形状**边界**画一圈：一半在形状外、一半在内（照 CSS 的描边语义）。圆角 / 圆 / 星形都跟着自己的形状走。`width` 必须 > 0（不想描边就**整段省略**，别写 0） |
| `shadow` | `offsetX` / `offsetY`（可选，默认 0）、`color`（可选，默认半透明黑） | 形状的**偏移实心副本**——**没有模糊**（所以**不认 `blur`**，写了报错）。偏移量是设计坐标 |

两条要记住的：**两者都不参与命中**（命中判的是形状本体 `ShapeContains`，描边画在边界内外
各一半、阴影整体偏在形状之上/之下，都不算"点得中"）；**绘制顺序是"阴影 → 填充/图片 →
描边"**（顺序反了就会被自己盖住），描边**不支持** `ring` / `arc`（它们本身就是一条细带，
写 `stroke` 也不画、不报错）。

### 3.6 不合法参数的处理

**必须是"报错停下"，不是"夹一下就过"。** 生成器阶段的问题最容易查，放进运行时就是
"跑起来了但样子不对"。例如：

- `radius` 为负、或不是数字 → 报错；
- `type` 不认识 → 报错，并列出支持的取值；
- `polygon` 的 `sides < 3`、或 `sides` 不是整数 → 报错；
- `ring` / `line` 缺 `thickness`、或 `thickness <= 0` → 报错（不从高度猜）；
- `line` 的 `cap` 不是 `butt` / `round` / `square` → 报错，并列出这三个取值；
- `chamferRect` 缺 `chamfer`、或它为负 → 报错；`star` 缺 `points`、`points < 3`
  或不是整数、`innerRatio` 超出 0~1 → 报错；`superellipse` 缺 `exponent`
  或它 ≤ 0 → 报错；`arc` 缺 `thickness` / `sweepAngle`、或 `sweepAngle`
  为 0 / 超出 ±360 → 报错；`polygonPath` 缺 `vertices`、`vertices` 不是数组、
  数字个数是奇数或少于 6（3 个点）→ 报错。

唯一允许自动修正的是**语义上无歧义的夹取**：`radius` 超过 `min(width,height)/2`、
`ring` 的 `thickness` 超过 `min(w,h)/2`、`chamferRect` 的 `chamfer` 超过短边一半、
`superellipse` 的 `exponent` 超过 16、`arc` 的 `sweepAngle` 到 ±360（退化成整环）
——都是形状本身的数学要求，不是配置错误。

---

### 3.7 布局（`layout` / `layoutItem`，A6）

**规则写在容器上，意愿写在孩子上**——和 CSS 的 `display:flex` 与 `flex-grow` 同一个分工：

```jsonc
// 容器（例如一个面板）：我怎么排孩子
"layout": {
    "kind": "stack",          // stack | grid（省略 = 不排，孩子还是各自锚点定位）
    "axis": "vertical",       // vertical | horizontal（only stack）
    "gap": 12,                // 主轴间距
    "padding": 20,            // 一个数字 = 四边相同；也可 {"left":20,"top":20,...}
    "justify": "start",       // start | center | end | spaceBetween | spaceAround | spaceEvenly
    "align": "stretch",       // 交叉轴：start | center | end | stretch（默认 stretch）
    "columns": 2,             // grid：列数（≥1）
    "rowGap": 6, "columnGap": 10
}

// 孩子：我在父容器布局里的意愿
"layoutItem": {
    "margin": 8,              // 一个数字 = 四边相同；也可四边对象
    "fillMain": true,         // 主轴吃剩余空间（多个就均分）
    "minWidth": 0, "minHeight": 0,
    "maxWidth": 0, "maxHeight": 0,   // 0 = 不限
    "selfAlign": "center"     // 覆盖容器的 align
}
```

几条要知道的：

- **min / max 永远最后夹**（它是约束，不是建议）；`fillMain` 被 `max` 夹住之后**不做二次
  分配**，剩下的留白就空着；
- 内容超出容器时**一律从头排**，不缩不挤——溢出的那截交给容器的 `clipChildren`（§4）；
- **`layout` 只烘规则、不烘结果**：位置与尺寸在运行期算出来写进子节点的运行期几何，然后
  标结构脏让**命中表重烘**（点得到的地方和画出来的地方永远一致）；
- **布局容器里的子节点，锚点与 `offset` 不参与定位**（写了也不影响结果）——想让某一个偏
  一点，用 `layoutItem.margin`；
- **重排是显式的**：改完内容（文字变长、换语言、列表增删）或改了容器尺寸之后，在组件里调
  一次 `Relayout()`；它**幂等**（算出来一样就一个脏都不标）。框架不做隐式重排，因为那会让
  "什么时候重排"变得看不见。

参与生成的真实配置：`CXXCSS/Panel/columnPanel.json`（一列内容 + 间距 12 + 内缩 20）。

---

### 3.8 标签与语言表（`Label` / `Text`，A7）

这两个目录类型不是一对：`Label` 是**组件**（一段只读文字），`Text` 是**语言表**（占位符 →
各语言的串）。标签的 `text.content` 写占位符，运行期由语言表查出当前语言那一份。

#### 标签：`CXXCSS/Label/<名字>.json`

```jsonc
{
    "name": "menuTitle",             // 必须与文件名一致，且**必须是合法 C++ 标识符**
    "width": 368,                    // **必填**：换行的依据（设计坐标）
    "height": 250,                   // 可省：省略 = 0 = **完全由内容撑高**

    "text": {                        // 内容 + 排版样式（整块收在一个段里）
        "content": "menu.title",     // 字面量，或**占位符**（语言表里的 key）
        "fontSize": 24,              // 字号（设计坐标；默认 16）
        "lineSpacing": 1.2,          // 行距**倍数**（1.0 = 行与行紧贴；默认 1.25）
        "path": "",                  // 字体绝对路径；空 = 机器默认字体（CJK 优先）
        "align": "center",           // 行内水平：left（默认）/ center / right
        "alignV": "middle",          // 整块垂直：top（默认）/ middle / bottom
        "padding": 8,                // 文字与文本框四边的留白（数字或四边对象）
        "color": "1|1|1|1",          // 文字色，`r|g|b|a`（0~1）
        "wrap": "word",              // noWrap / word（默认）/ anywhere
        "overflow": "ellipsis",      // clip（默认）/ ellipsis（末尾补 `…`）
        "maxLines": 2                // 0（默认）= 不限
    },

    "selfAnchorX": 0.5, "selfAnchorY": 0.5,
    "traceAnchorX": 0.5, "traceAnchorY": 0.5,
    "offsetX": 0, "offsetY": 0, "zIndex": 0, "visible": true,
    "layoutItem": { "margin": 8 }    // 它在父容器布局里的意愿（§3.7）
}
```

几条要知道的：

- **没有 `shape`**：标签只画文字、不画底（写 `shape` / `stroke` / `shadow` 会报"不认识"
  ——与其收下之后不生效，不如当场说清）。要"带底的文字"，把标签放进面板或者按钮里；
- **`height` 省略就是"按内容撑高"**：容器里两个不同长度的标签会被自动推开（`MeasureLayout`
  报的是排版算出来的高度，见 §3.7）；写了 `height` 则它是"还没有内容时"的兜底；
- **`wrap` 与 `overflow` 是两件事**：前者决定"要不要换行"，后者决定"放不下时怎么办"；
  最常见的组合是 `noWrap` + `ellipsis`（单行超长截断）；
- **默认不吃点击**（标签常压在按钮 / 图片上，吃掉点击就变成"看得见、点不动"）；
- 占位符**查不到时原样显示**：没进语言表的字面量照样能用，不会变成空白。

#### 语言表：`CXXCSS/Text/<名字>.json`

```jsonc
{
    "name": "ui",                    // 必须与文件名一致
    "languages": ["zh", "en"],       // 有哪几门语言（**顺序就是下面每条串的顺序**）
    "default": "zh",                 // 可省：启动时装哪门；省略 = 第一门
    "entries": {
        "menu.title": ["星轨之下", "Under the Stars"],   // 一条一个占位符
        "menu.quit":  ["退出", "Quit"]
    }
}
```

- **只允许一份**：`InstallTextTable` 的语义就是"替换整张表"，两份只有一份生效——所以生成期
  直接报错（报错里会指出已经在的那份文件）；
- `entries` 里每条的串数**必须等于** `languages` 的数量（第 i 个串就是第 i 门语言的译文）；
  某一门暂时没译文就写空串 `""`，运行期会退回显示 key；
- 它是**全局表，不是组件**：没有尺寸 / 锚点 / 形状，也不进命中表与绘制列表；生成物是一份
  静态数据 + 一个安装函数，而且**启动期自动装一次**（和 `RegisterAllButtons()` 共用同一个
  启动对象，业务代码不用动手）；
- 换语言只要一句 `ink::SetTextLanguage("en")`，**不需要通知任何组件**——语言版本号一变，
  标签的排版缓存自然失效，下一帧自己重查、重排；不过行数与高度可能跟着变，所以换完之后
  **喊一次所在容器的 `Relayout()`**（重排是显式的，见 §3.7）。

参考配置：四个标签 `Label/menuTitle.json` / `hintBody.json` / `ellipsisNote.json` /
`langBadge.json`，语言表 `Text/ui.json`，承载容器 `Panel/labelBoard.json`（`examples/panel_demo`）。

---

### 3.9 主题表（`Theme`，B8）

**语义色 / 语义字号 → 具体值**。组件配置里写 `"$panelSurface"` / `"$title"`，生成器在
**生成期**换成主题里的值、照旧烘成字面量：

```jsonc
// CXXCSS/Theme/dark.json —— 它不是组件，是**生成器的输入**
{
    "name": "dark",
    "colors": {
        "panelSurface": "0.08|0.11|0.15|0.94",   // 键写名字（不带 $）
        "textPrimary": "1|1|1|1"
    },
    "fontSizes": { "title": 24, "body": 15 }
}
```

```jsonc
// 任意组件配置里：颜色与字号都能用 token
"closed": { "type": "color", "source": "$panelSurface" },
"text":   { "content": "menu.title", "fontSize": "$title", "color": "$textPrimary" }
```

规矩：

- **`$` 只出现在"用它"的地方**：主题里的键写名字（`surface`），不是 `$surface`；
- 主题里的值**必须是字面量**（颜色 `"r|g|b|a"`、字号是数字）；写 `"$other"` 会报错
  ——token 指向 token 迟早绕成环；
- **只允许一份**（两份主题只有一份生效，与其让人猜哪份赢，不如生成期报错）；
- **用到主题里没有的 token 会报错**，并把主题里有哪些列出来；
- **忘了传主题表也会报错**（不是悄悄用兜底色）：构建脚本里 `ink_add_generated(...)` 的
  `SOURCES` 要把 `CXXCSS/Theme/*.json` 一起传进来。

两条边界：**换主题要重新生成（重编）**——语义值是在生成期落进产物的（产物里一个 `$` 都
不剩），"改主题 json → 重新构建"就换肤了，这解决的正是"配色散在每个组件 json 里、改一处
要翻一遍"的问题；**运行期换肤没做**（第二版的事：数据里保留 token + 按版本号重查颜色，
语言表那套是现成的样板），现在产物里没有主题类、也没有"运行期切主题"的接口。

参考配置：`Theme/dark.json`（10 个颜色 token + 3 个字号 token）。

---

### 3.10 图片（`Image`，A11）

**静态图片 = 没有 onClicked 的 button**：形状、外观、描边阴影、锚点、命中全都要，只要三态
与回调不要。数据上它就是 `ink::ImageData : ink::ButtonData`。

```jsonc
// CXXCSS/Image/cardArt.json
{
    "name": "cardArt",
    "width": 200, "height": 120,
    "runtime": true,                                   // ← 图从运行期来
    "normal": { "type": "img", "source": "card.png", "fit": "cover" },
    "shape": { "type": "roundedRect", "radius": 12 }
}
```

**认的字段**：`name` / `width` / `height` / `normal` / `runtime` / `shape` / `stroke` /
`shadow` / 锚点四件 / `offsetX` / `offsetY` / `layoutItem` / `zIndex` / `visible`。
`normal` 里**只认 `type`、`source`、`fit`**；写了 `hover` / `onclicked` / `text` /
`transition` / `dynamic` / `layout` / `svg` 一律**报错**（图片没有三态、没有文字、没有过渡
——静默吃掉会让人以为"悬停能换图"）。

**两种"图从哪来"**：

| 写法 | 字节从哪来 | 产物里有什么 |
| --- | --- | --- |
| 省略 `runtime`（或 `false`） | **生成期**读文件，烘成静态数组 | `const unsigned char k…[] = {…}` + 一个视图 |
| `"runtime": true` | **运行期**从资源根读（`ink::AssetLoad`，资源根默认 exe 旁的 `assets/`） | **只有那个路径字符串**——一个字节都不烘 |

`runtime: true` 时 `source` 必须是**相对资源根**的路径；写绝对路径会报错（两条路的写法不能
混）。图片的 `name` 必须是合法标识符（它没有"按名字查"的库，生成不出类就没有用法）。
参考配置：`Image/cardArt.json`（`runtime: true`）+ `examples/panel_demo`（构建脚本把
`assets/card.png` 拷到 exe 旁边）——**换图不用重编**就是这条路的效果。

---

## 4. Button

完整字段见 `Button/button.example.json` 里的 `$comment`。要点：

| 字段 | 说明 |
| --- | --- |
| `name` | 与文件名、代码里使用的名字三处一致 |
| `width` / `height` | 设计坐标尺寸 |
| `dynamic` | 可选，布尔。`true` → 生成的类继承 `ink::InkingDynamicButton`（几何可在运行期改、不进命中表、指针由它自己在 `Tick` 里拉）；不写或 `false` → `ink::InkingStaticButton`。见 4.3 |
| `normal` / `hover` / `onclicked` | 三态外观，结构相同 |
| `text` | 可选。**不建议用**，见 §5 |
| `selfAnchorX/Y`、`traceAnchorX/Y` | 锚点 |
| `shape` | 形状层 |
| `stroke` / `shadow` | 与 `shape` 平级的两笔装饰（描边 / 阴影，B3） |

### 4.1 三态外观

`normal`（通常）/ `hover`（悬停）/ `onclicked`（按下），三者结构一致：

```jsonc
{ "type": "color" | "img" | "svg", "source": "……" }
```

| `type` | `source` 写什么 |
| --- | --- |
| `color` | RGBA，用 `|` 分隔：`"0|0|0|0.75"`（0~1 的浮点） |
| `img` | 图像**绝对路径**。**生成期**把文件读成字节烘进生成代码（上限 **2 MB**，超了报错、文件不在也报错），运行期首次绘制时解码一次、进纹理表；画的时候**按组件形状裁剪**，图在框里怎么摆放由 `fit` 决定 |
| `svg` | 直接写 SVG 代码。**生成期**解析并转成"形状 + 填充色"（见下面的子集边界） |

**`fit`：图片适配模式**（可选，只对 `img` 有意义；B1）。写法示例：
`"normal": { "type": "img", "source": "D:/assets/card.png", "fit": "cover" }`

| 值 | 效果 |
| --- | --- |
| `fill` | **缺省**。拉伸铺满整块框，不保持宽高比（B1 之前唯一的行为，所以老配置一个字节都不用改） |
| `contain` | 保持宽高比，整张图放进框里、居中；**留白处不画**（那一圈露的是底下的东西） |
| `cover` | 保持宽高比铺满框、居中；**超出框的一边被裁掉**（像 CSS 的 `object-fit: cover`） |

三种模式都**按形状裁剪**（形状外的一律不画，边缘与形状同一套抗锯齿）——实现是"形状网格 +
纹理坐标"（`detail::fillShapeTextured`），不是先画矩形再遮盖，所以形状与图片永远同源。
`contain` 的留白边界是**轴对齐**的（用一次裁剪矩形实现），容器**旋转**时那一圈留白不会跟着
转（已知近似，见 `UsageReport.md` §7.4）。

**`svg` 的支持子集**：

| 认 | 说明 |
| --- | --- |
| `rect` | `x`/`y` 必须是 0，`width`/`height` 必须正好是组件尺寸；带 `rx`/`ry` 就是圆角 |
| `circle` | `cx`/`cy` 必须居中、`r` 必须等于 `min(宽,高)/2` |
| `ellipse` | `cx`/`cy` 居中、`rx`/`ry` 正好是半个组件宽高 |
| `path` | **B2**：`d` 属性被**折线化**成一张顶点表（就是 `polygonPath` 形状）。认 `M/m L/l H/h V/v Z/z C/c S/s Q/q T/t A/a`（大小写 = 绝对 / 相对），曲线在**生成期**离散（每段不超过 4 个设计单位、段数夹在 4~64）。`path` **不要求**与组件框一致——它就是任意形状 |
| `fill` | `#RGB` / `#RRGGBB` / `#RRGGBBAA` / `black` / `white`；不写就是黑（与 SVG 一致） |

`path` 的边界（这一轮）：**只认单条子路径**（第二个 `M` 会报"多图形 / 孔洞还没做"——形状层
现在只有一条点列，硬接会把两块用一条直线连起来；`fill-rule`（nonzero / evenodd）也还没有
意义）；**不认** `g` / `line` / `polygon` / `use`、变换、描边、渐变、`viewBox` 缩放；一个 svg
里只能放**一个**图形元素。不认的标签会明确报"**不支持的标签**"（说清是没实现、不是你写错了）。

> 几何必须与组件框一致、三个状态的 svg 几何必须相同、显式写了 `shape` 时还要与它一致
> ——三条都是**生成期**检查，错了就红。旧示例把键写成了 `"type:"`（多一个冒号），那是笔误。

### 4.1.1 `transition`：三态之间的过渡（可选）

写在**状态自己**身上，表示「**进入这个状态时**用多久过渡」：

```jsonc
"hover": { "type": "color", "source": "1|1|1|0.85",
           "transition": { "duration": 0.15, "easing": "easeOut" } }
```

| 字段 | 说明 |
| --- | --- |
| `duration` | 秒，**必须 > 0**。不想要过渡就整个 `transition` 都别写 |
| `easing` | 可选，缓动曲线：`linear`（缺省）/ `easeIn` / `easeOut` / `easeInOut`。**写了 `easing` 就必须写 `duration`**——没有时长就没有过渡，那条曲线不会生效，所以生成期直接报错 |

于是"过场段各自带一份"天然成立：`normal` 写 0.3 + `easeInOut`、`hover` 写 0.15 +
`easeOut`，就是**进悬停快而收得住、退回慢且起落柔和**（`Button/button.example.json` 就是这么
配的）；按下（`onclicked`）通常**不配**——按下要的是即时反馈。三条要记住的约定：

1. **颜色与变换都按这一段时长走**：同一段过渡里两者一起插值，不需要各配一个时长。
2. **缓动只改变进度，不改变时长、起点、终点**：`f(0) = 0`、`f(1) = 1` 是精确的（收尾那一帧
   另外直接落目标值，双保险）。算式是**二次**那组，定义在
   `include/ink/dataStruct/InkingEasing.h`；**与 CSS 默认的 `ease-in` / `ease` 不是同一组数**
   （那要解三次贝塞尔方程），别指望逐帧对上。
3. **过渡由渲染帧推进**（不是逻辑步），用真实 delta；**中断时从头算**——走到一半切状态，
   起点取**当前显示值**，新的一段曲线从 0 重新开始（不做"补偿已经走过的曲线"）。

### 4.1.2 `transform`：平移 / 旋转 / 缩放（可选）

四个字段都可以不写（不写就是单位变换，等于不动）：

| 字段 | 含义 |
| --- | --- |
| `translateX` / `translateY` | 平移，**设计坐标**（不是百分比） |
| `rotate` | 旋转，**度**、顺时针为正 |
| `scale` | 均匀缩放，**不能为 0**（0 会在生成期报错——那会把形状压成一个点） |

写法与 `transition` 同一个状态段里：`"transform": { "translateY": -6, "scale": 1.05 }`。
两条约定：**原点固定是组件中心**（等价 CSS 的 `transform-origin: 50% 50%`；形状都按左上角
定义，绕左上角转不是直觉里的样子）；它属于 **InputDesign.md §11 的"变换通道"**——**不改本地
几何、也不改命中表**（表按本地形状烘，查询时把点**反变换**回本地空间）：

| 通道 | 例子 | 命中表要不要重烘 |
| --- | --- | --- |
| **变换**（这个字段） | `translateX/Y`、`rotate`、`scale` | **不用**——表冻结，查询时反变换 |
| **布局**（还没有字段） | `width` / `height` / 会让兄弟重排的定位 | **要**重烘受影响的子树 |

推论：**静态组件也能配 `transform`，而且仍然进命中表**；绘制时顶点过正变换、命中时点过
逆变换，两者严格互逆（`InkingTransform.h` 里两份函数挨着写，就是为了这个）。示例见
`examples/game/buttons/menuOne.json`（悬停上移 5 + 放大 6%）与
`examples/game/buttons/cardTwo.json`（悬停转 45° + 放大 6%）。**文字也跟着变换走**（文字是一张
纹理，绘制时和形状走同一套变换——按钮转了、字跟着转）。

### 4.1.3 布局通道：状态自己的尺寸（T-5，可选）

写在状态里、由状态机切换，所以**组件仍然是静态档**（没有几何写入口，`src/ink.cpp` 的编译期
断言继续成立）。写法就是在状态段里加 `"width": 320, "height": 140`（还可以带 `transition`）。
三条约定：

1. **从左上角生长**：锚点与偏移一个字节都不动，尺寸变了就从左上角往外长。想"以中心变大"
   用 `transform.scale`（变换通道）；想真的改本地几何（命中区域跟着变）才用这里的
   `width`/`height`。
2. **与颜色 / 变换同一个进度**：`transition` 那段时长里三者一起插值，推到一半就是两端尺寸
   的中间。
3. **不做"每态一份表"**：命中表登记的是**三态包络**（三个状态的形状 + 变换 + 尺寸并起来），
   中间尺寸的 AABB 一定落在并集里；精判用**当前显示尺寸**。于是尺寸动画不需要重烘表
   （自检里"推到一半对账 0 不一致"钉的就是它）。

**不影响兄弟**：这一档只改自己的尺寸；会牵动兄弟重排的定位是另一件事（`InputDesign.md` §11
说的"受影响范围由生成器算好"），还没做。

### 4.2 `hover` / `onclicked` 可省略

省略时继承 `normal` 的外观（三个都写只是为了示例完整）。继承是**整段**继承：`hover` 省略时
它的 `transition` 与 `transform` 也跟着 `normal` 走；想只要颜色继承、其余另配，就得把
`hover` 显式写全。

### 4.3 `dynamic`：这块按钮是静态档还是动态档

判据只有一条（`include/ink/basic/InkingAnchor.h` 末尾那段）：**几何会不会在构造之后自己变**。

| | 静态（不写 `dynamic`） | 动态（`"dynamic": true`） |
| --- | --- | --- |
| 生成出来的基类 | `ink::InkingStaticButton` | `ink::InkingDynamicButton` |
| 几何写入口 | **没有**（类型上就调不到） | `Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset` |
| 命中 | 由场景的命中表回答 | 不进表，自己判 |
| 输入 | 场景派发**推**过来（`MouseHover` / `MousePress` / `MouseRelease`） | 自己在 `Tick` 里**拉**（`JudgePointer`，读场景的指针状态） |
| 三态外观 / 画出来什么样 | 两份**共用**（`include/button/ButtonLook.h`、`src/button/ButtonPaint.cpp`） | 同左 |

**它不是"另一个组件"，是同一个组件的另一档**：颜色、形状、文字、点击回调的用法完全一样，
差别只在"几何能不能在运行期改"和"输入从哪来"。所以两块按钮要各自一份 json（名字也必须各自
唯一，见 §1）——要消掉重复得另加机制（例如 json 之间的 `extends`），**未决定**。`dynamic`
是**生成期**开关，不进运行期数据结构（`ButtonData` 里没有这个字段）：类型本身已经说明它属于
哪一档。参考配置：静态档 `examples/game/buttons/menuOne.json`、动态档
`examples/galgame/buttons/CastLuolisi.json`。

---

### 4.4 Panel：容器组件（关 / 开两态）

面板**不是控件，是容器**——抽屉 / 下拉 / 弹窗那一档：开合是**内容状态**（业务调 `SetOpen` /
`Toggle`），不是指针三态；它的变换**作用于整棵子树**（面板一动，里面的子节点**绘制与命中
一起动**，子节点自己一个字都不用知道）；子孙**不进命中表**（容器只登记一条盖住"整棵子树 ×
两态变换"的包络，命中由容器递归解答），所以抽屉来回拉，**命中表条目数一条不变**。

```jsonc
{
    "$comment": ["……"],
    "name": "drawerPanel",     // 必须与文件名一致，且**必须是合法 C++ 标识符**
    "width": 400, "height": 900,   // 设计坐标
    "closed": {                // 关着时的外观（必填）
        "type": "color",
        "source": "0.106|0.141|0.188|1",
        "transition": { "duration": 0.32, "easing": "easeInOut" },  // 进这个状态用多久
        "transform": { "translateX": -420 }                          // 常见写法：整块挪出去
    },
    "open": {                  // 开着时的外观（省略 = 颜色继承 closed）
        "type": "color",
        "source": "0.141|0.188|0.25|1",
        "transition": { "duration": 0.18, "easing": "easeOut" }
    },
    "startOpen": false,        // 构造时就是开着的吗；省略 = false
    "clipChildren": true,      // 剪掉子孙超出本体的部分（A5）；省略 = false
    "selfAnchorX": 0, "selfAnchorY": 0, "traceAnchorX": 0, "traceAnchorY": 0,
    "offsetX": 0, "offsetY": 90, "zIndex": 0, "visible": true,
    "shape": { "type": "roundedRect", "radius": 20 }
}
```

与 Button 同名同义的字段（`width` / `height` / 锚点 / `offsetX/Y` / `zIndex` / `visible` /
`shape`）含义完全一样；面板特有的四个：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `closed` / `open` | `closed` 必填 | 两个**具名状态**的外观。`open` 省略时**颜色**继承 `closed`（`openInheritsClosed`），但**过渡与变换不继承**——`open` 的 `transform` 就是"开到位的目标"，继承关着那份等于没开 |
| `startOpen` | 否 | 构造时就是开着的吗，默认 `false`。放进配置而不是"构造完再调一次 `SetOpen`"，是为了不让初始状态先起一段过渡再被打断 |
| `clipChildren` | 否 | **剪掉子孙超出本体的部分**（A5），默认 `false`。裁剪区就是本体矩形、只剪子孙（面板自己那一笔不受影响），而且**画出来被剪、点起来也被剪**（两边同源）。默认不剪是因为面板也常装故意溢出的东西（阴影 / tooltip）。参考 `Panel/clipWindow.json` |
| `type` | 是（每个状态里） | 现在**只支持 `color`**。写 `img` / `svg` 会明确报"面板还不支持"——面板不贴图、不开洞，它是容器 |

**面板没有** `hover` / `onclicked`（那是指针三态）、`text`、`img` / `svg`（外观类型）、
`dynamic`（只有静态档：开合只改变换，几何构造即定型）。**名字的限制更严**：面板的 `name`
必须是合法标识符——按钮的名字不合法还能走"按字符串名字查"（数据进 `ButtonLibrary` 兜着），
而面板**没有按名字查的库**，没有类就等于没有任何用法，所以生成期直接报错。

```cpp
#include <inkgen/<你的集合>/panel_service.h>

inking::cxxcss::DrawerPanel drawer{parent};   // 构造只给父级
drawer.Toggle();                           // 或 Open() / Close() / SetOpen(bool)
```

参考配置：`Panel/drawerPanel.json`（`examples/panel_demo` 用的就是它）。组件与 API 见
`docs/UsageReport.md` §6.2；"新组件怎么写"见 `docs/ComponentGuide.md`。

### 4.5 滚动（`scroll`，B12）

**滚动是容器的一种能力**（和 `clipChildren` 同一族），不是新组件类型——配了 `scroll` 段
就启用。几何规则全在 `include/ink/dataStruct/InkingScroll.h` 的几个**纯函数**里
（可滚范围 / 夹取 / 轨道与滑块 / 点轨道跳到哪），所以绘制与自检共用同一份，没有第二个真相。

```jsonc
"clipChildren": true,          // 滚动容器**必须**配：不配的话滑出去的内容会画到框外
"scroll": {
    "axes": "both",            // vertical（默认）| horizontal | both：哪条轴**能滚**
    "wheelStep": 48,           // 一格滚轮滚多少（**设计像素**，默认 48）
    "verticalBar": "auto",     // auto（默认，内容超出才画）| always | never
    "horizontalBar": "auto",   // ↑ 只决定**画不画条**，与能不能滚无关
    "barWidth": 8,             // 条的粗细（默认 8）
    "barInset": 4,             // 条离容器边缘多远（默认 4）
    "minThumbLength": 28,      // 滑块最短（默认 24；内容超长时不至于细如发丝）
    "barRadius": 0             // 0 = 胶囊（取短边的一半，默认观感）；> 0 = 指定圆角半径
}
```

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `axes` | 否 | `vertical`（默认）/ `horizontal` / `both`——哪条轴**能滚**。 |
| `wheelStep` | 否 | 一格滚轮滚多少**设计像素**（默认 `48`）。这一版**没有**"视口比例"那一档。 |
| `verticalBar` / `horizontalBar` | 否 | 那条轴**画不画条**：`auto`（默认，内容超出才画）/ `always` / `never`。与"能不能滚"无关（写 `never` 也照样能靠滚轮 / 点轨道滚）。 |
| `barWidth` / `barInset` | 否 | 条的粗细（默认 `8`）/ 离容器边缘的距离（默认 `4`）。 |
| `minThumbLength` | 否 | 滑块最短（默认 `24`）。 |
| `barRadius` | 否 | `0`（默认）= **胶囊**，取短边的一半；`> 0` = 指定的圆角半径。 |

`scroll` 里**不认识的字段会报错**（构建期就红）：只认上表这八个。**颜色不在配置里**
（轨道 / 滑块 / 悬停 / 条带背景都要手写 `ScrollSpec`：`barColor` / `thumbColor` /
`thumbHoverColor` / `barBackgroundColor`，默认值见 `InkingScroll.h`）。

**这一版不做**：缓动（滚轮与点轨道都**瞬时到位**）、惯性 / 越界回弹 / 吸附对齐、
触摸"拖内容"、虚拟化（B9）、条挤占内容区（条**浮在内容之上**——想让条带干净就手写
`barBackgroundColor`）。点轨道 = **跳过去**（滑块中心落到点击处）；滚轮**容器优先**
（指针在可视区内就容器吃掉）。内容尺寸变了要显式喊一次 `UpdateScrollRange()`（**幂等**）。

参考配置 `Panel/scrollPanel.json`；运行期接口、四条约定与自检见 `docs/UsageReport.md` §6.21。

---

## 5. 文字（text）

`text` 是**贴在按钮上的那一行**（独立文字组件是 `Label`，见 §3.8）。结构：

```jsonc
"text": {
    "fontSize": 16,
    "leftSpace": 12,      // 距左侧（设计坐标）；align == "left" 时用它
    "topSpace": 8,        // 距顶部（设计坐标）；alignV == "top" 时用它
    "align": "center",    // 可选：left（默认）/ center / right —— 文字块的水平摆法
    "alignV": "middle",   // 可选：top（默认）/ middle / bottom —— 垂直摆法
    "path": "",           // 字体绝对路径；空则用机器默认字体
    "content": { "normal": "确定", "hover": "确定", "onClicked": "确定" }
}
```

`align` / `alignV` 是**对齐**（不是"再挪几个像素"）：写了 `center` / `middle` 之后，
框架会**先量一遍文字块的真实尺寸**（`ink::MeasureText`，比例字体也准）再算位置，
`leftSpace` / `topSpace` 在那两个方向上就不参与了；`right` / `bottom` 则把它们当
**边距**用。不写就是老行为（文字左上角落在 `leftSpace` / `topSpace`），既有配置
与产物一个字节都不变。

> ⚠️ **建议：`text` 段只配"外观"，文案留给运行期。** `fontSize` / `leftSpace` / `topSpace` /
> `align` / `alignV` / `path` 是**外观**，烘进生成代码没问题；`content.*` 是**文案**，烘进去
> 就等于把一种语言焊死在二进制里。多语言界面请用 `SetText()` 在运行期设置（颜色用
> `SetTextColor()`）——示例 `examples/game/buttons/*.json` 就是这么配的：有 `text` 段，
> 但**故意不写 content**。

`path` 为空时用机器的默认字体（按候选表找 CJK 优先）；**依赖机器字体意味着跨机器不一致**，
交付版本应显式指定字体路径。文字走纹理层
（[`include/render/InkingText.h`](../include/render/InkingText.h)）：按（文本 + 字号 +
字体）栅格化并缓存，颜色走**调制**（换色不重烘），旋转 / 缩放 / 平移跟形状同一套；没接
`SDL3_ttf`、或机器上一份字体都没有时退化成"占位方块"（老画法），不是不画。**按钮上的文字
只画 `content.normal` 那一份**（hover / onClicked 的换字通道还没做）；可编辑的输入框
（光标 / 选区 / IME）是另一件事，见 `TaskGuide.md` B7。

---

## 6. 生成器会拿这些做什么

理解这一步有助于判断"某个字段该不该拆"：

1. **每个名字生成一个独立类**（`normalButton` → 专用按钮类；`drawerPanel` → 专用面板类；
   `menuTitle` → 专用标签类）；语言表（`Text/`）不生成类——它是全局数据 + 一个安装函数；
2. **形状展开成 SDF + `contains` + AABB**，三份来自同一个 `type` 分支；
3. **按布局类型生成专用查找**——等宽等距的按钮组可以算成 `index = (x - rowX) / (w + gap)`，
   O(1) 且不需要表（`docs/InputDesign.md` §10.4）——**还没做**（命中走的是烘焙表）；
4. **锚点与尺寸变成编译期常量**，运行期不再解析配置。

所以配置里**能让生成器算的，就别留给运行时**：能烘的烘掉，烘不了（多语言）的才留给运行时。
逐项状态见 §8。

---

## 7. 场景：**手写，不生成**

场景不进 CXXCSS——它是**编排**（放哪些组件、按什么条件放），不是数据。

```cpp
class MainScene : public ink::InkingScene {
public:
    static constexpr const char* kName = "mainScene";   // 一行常量
    MainScene() : ink::InkingScene(kName) {}
    // 组件是成员；`{this}` 由默认成员初始化器填父级——少了 `asParent()` 这处样板，
    // 也不可能"忘了传 parent → 组件不登记"。
    inking::cxxcss::NormalButton normalButton{this};
    inking::cxxcss::DynamicTestButton dynamicTestButton{this};
};

MainScene scene;                               // 用户代码就三行
ink::SceneLibrary::SetActiveScene(&scene);
ink::InkingWindow::Instance().Show();
```

组件的位置仍然写在**组件自己的配置**里（Button json 的锚点 / 偏移），所以"换个场景位置跟着
走"这条照样成立——它和场描述在哪儿无关。

### 7.1 为什么手写就够

生成场景本来能拿到两处收益，但**它们都不需要生成器**：

| 收益 | 手写怎么拿到 |
| --- | --- |
| 组件不用传 `parent` | 成员 + 默认成员初始化器 `{this}`（就是上面那几行） |
| 名字有编译期保险 | 组件类型名由**生成的按钮类**提供（`NormalButton` 拼错就是编译错误）；场景名一行 `kName` 常量就够 |

### 7.2 为什么撤掉（2026-10-02 评估）

生成场景曾实现过一版并跑通（`TaskGuide.md` 的 T-7），评估后回退：**收益用手写就能拿到，而
代价是固定的**——一个界面要看两组文件（成员清单在 Scene json、位置在 Button json）、表达力
受结构性限制（不能放非按钮组件、不能覆盖位置、不能嵌套、不能按条件建组件）。它真正的价值
在**将来**：`docs/InputDesign.md` §10.4 那种"按布局生成专用查找"（那时配置里得带 layout
信息），或者窗口层真的开始"按名字切场景"。

---

## 8. 落地状态

诚实地区分"已实现"和"纸面约定"，别让后来的人以为字段已经生效：

| 项 | 状态 |
| --- | --- |
| 本文档描述的格式 | **已定型**（约定） |
| 代码生成器 `inkgen` | ✅ 构建期读 `CXXCSS/`（`ink_add_generated()` 传文件，`CONFIGURE_DEPENDS` 跟进新文件），产物落 `build/`、自动进 include 路径 |
| 校验（`name` 与文件名 / 未知字段 / 参数 / 名字唯一） | ✅ 生成期报错、带文件与行号；名字唯一是**跨组件类型**的。`inkgen_validate_cxxcss` 单测真实配置 |
| 每个名字一个独立类 | ✅ 按钮（`dynamic` → `InkingDynamicButton`）、面板（`InkingStaticPanel`）、标签（`InkingStaticLabel`）；自检 `inkgen_test` / `ink_panel_gen_test` / `ink_label_gen_test` |
| `shape` 几何 + 填充 | ✅ 十三种（§3.3）；填充与命中同源（`detail::fillShape`）；自检 `ink_shape_test` |
| `stroke` / `shadow`（B3） | ✅ 描边 = 轮廓环带（`detail::strokeShape`）、阴影 = 偏移实心副本（**无模糊**，不认 `blur`）；都不参与命中、顺序"阴影 → 填充 → 描边"、`ring` / `arc` 不支持描边；自检 `ink_decor_test` |
| `polygonPath`（B2） | ✅ 顶点表烘静态数组；符号射线法 + ear clipping 填充（非凸可用）+ miter 描边；自检 `ink_path_test`（7543 点对账） |
| SVG `path`（B2） | ✅ 生成期离散成折线（认 `M L H V Z C S Q T A`）；**多子路径 / 孔洞与 `fill-rule` 还没做** |
| 锚点 / 尺寸 / `zIndex` / `visible` / `offsetX` / `offsetY` | ✅ 配置与手写 `AnchorData` 两条路都通 |
| Button 的 `dynamic` / `transition` / `transform` | ✅ 见 §4.3 / §4.1.1 / §4.1.2；`transform` 不改几何也不改命中表，文字也跟着走 |
| Button 的 `img` / `svg` | ✅ `img` 生成期烘字节（≤ 2 MB）+ 运行期解码 + 按形状裁剪 + `fit` 三模式；没接 SDL3_image 时按颜色填；自检 `ink_image_test` |
| `text` 渲染 | ✅ 真文字（纹理层 + 缓存 + 跟变换走）；文案 / 颜色走运行期；独立文字组件是 `Label` |
| `Label`（A7） | ✅ `InkingStaticLabel`：不吃点击、`MeasureLayout` 报内容尺寸、`SetText` 让容器重排；排版是纯函数；自检 `ink_text_layout_test` / `ink_label_test` |
| `Text` + 多语言（A7） | ✅ `InkingTextTable.h`：`ResolveText` **查不到原样返回 key**；**版本号切换**（不用遍历通知） |
| `Label` / `Text` / `Theme` / `Image` 四种生成段 | ✅ 与按钮 / 面板共用同一套校验 / 重名 / 产物规则；`Text` 只允许一份 + 启动期自动装；`Theme` 生成期解析 token；`Image` 两条路各有产物差异断言；自检 `inkgen_test` |
| Panel 的 `clipChildren`（A5） | ✅ 画出来被剪、点起来也被剪；自检 `ink_clip_test`；配置 `Panel/clipWindow.json` |
| 场景配置 | ⛔ **已撤销**（§7.2）；场景名仍是代码里的常量 |
| 布局通道的动画 | ❌ 未实现：必须重烘命中表，还没有配置字段 |

**改 `CXXCSS/<组件类型>/*.json` 现在会真的影响行为**：下次构建时重新生成；**新增**一个 json
之后 Ninja 会自动重新 glob（`CONFIGURE_DEPENDS`），不需要手动重新配置。自检现状以 `ctest`
的实际列表为准（明细见 `docs/UsageReport.md` §2）。

> **给字段定稿的人一句提醒**：第 5 节的 `text` 字段是从原示例注释**推断**的，没有示例文件
> 可对照。它现在已经在 `ButtonData` 与生成器里各固化了一次（`fontSize` / `leftSpace` /
> `topSpace` / `path` / `content`）——要改名或改结构，现在改代价最小。
