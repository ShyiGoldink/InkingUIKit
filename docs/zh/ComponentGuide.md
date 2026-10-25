# 组件开发指南（ComponentGuide）

> ## ⚠ 本文对应版本 0.5.2（0.x 发布版）
>
> 接口在 `1.0.0` 之前不承诺稳定；本文讲的是**写法**，具体签名以头文件与
> `docs/zh/api/` 为准。示例代码与真实实现同源，但删减了日志与边界分支。

**重建说明**：本文件在本仓库中原本缺失，现按头文件注释中的引用点与
`InkingStaticPanel` / `InkingStaticButton` / `InkingButtonLike` 的实际代码复原。
标 `†` 的小节是**推断内容**（原始文档的章节结构与统计不可考）；文中一切
"多少行 / 多少项 / 多少倍"的描述都只是量级，**数值请以自己的实测为准**。

---

## 0. 这份文档讲什么

这不是 API 索引。API 索引回答"这个类有什么函数"；本文回答**"新组件该按什么顺序写、
每一段为什么必须存在、漏掉会怎样"**。

样板只有一个：`InkingStaticPanel`（`include/panel/InkingStaticPanel.h`）。它同时是
"整块在动的容器"和"容器该怎么写"的参考实现——抽屉、下拉、弹窗、滚动区都从它派生，
`OptionBar` / `ListView` / `Select` / `FilePicker` 没有再写一行底层代码。

### 0.1 组件的八段式 †

| # | 段落 | 样板里的落点 | 一句话判据 |
| --- | --- | --- | --- |
| 1 | **数据面** | `PanelData` / `PanelAppearance` | 纯数据、可 `Normalize`、不碰 SDL |
| 2 | **构造搬字段** | `InkingStaticPanel::applyData` | 每个字段显式搬一次，漏一处就"配置不生效" |
| 3 | **观测口** | `GetState` / `GetDisplayColor` / `GetData` | 只读、`const noexcept`、不推脏 |
| 4 | **写入口** | `SetOpen` / `Toggle` | `[final]`、幂等、内部标脏 |
| 5 | **钩子** | `onAnimationTick` / `onRender` / `onPointer*` | `on` 开头、可重写、**不标脏** |
| 6 | **标脏** | `MarkRepaintDirty` / `MarkDirty` | 重绘脏与命中脏是两条通道 |
| 7 | **命中** | `HitTest` / `HitTestNode` / `GetHitEnvelope` | 绘制与命中同源，包络覆盖所有状态 |
| 8 | **布局** | `MeasureLayout` / `Relayout` | 测量是纯函数；重排显式且幂等 |

> 这八段的**先后顺序**是本文为了讲解排的（†），不是框架强制的生命周期。
> 框架强制的只有两条：构造期把数据搬完；写入口标脏、钩子不标脏。

---

## 1. 样板逐段拆解：`InkingStaticPanel`

### 1.1 数据面：纯数据 + 一次 `Normalize`

`PanelData` 只有两个具名状态 `closed` / `open`，不是数组：

```cpp
struct PanelData {
    std::string name;
    int width = 0, height = 0;
    PanelAppearance closed{}, open{};   // 两态平铺成具名字段，不是数组
    ShapeSpec shape; AnchorPoint selfAnchor, traceAnchor;
    float offsetX = 0.0f, offsetY = 0.0f;
    LayoutSpec layout; LayoutItem layoutItem;
    int zIndex = 0; bool visible = true;
    bool startOpen = false; bool clipChildren = false;
    ScrollSpec scroll;
    void Normalize();                   // 补齐"省略即继承"，幂等
};
```

三条规矩：数据 struct 不带行为，只允许"补齐默认值"这类收尾（`Normalize`）；
`Normalize` 必须幂等，构造时调一次，之后读取方不再各写一遍"省略即继承"；
字段名与配置一一对应，不在此处改名——名字对不上的排查成本，比"更漂亮的命名"值钱。

### 1.2 构造：每个字段显式搬一次

```cpp
InkingStaticPanel::InkingStaticPanel(InkingWidget* parent, const PanelData& data)
    : InkingStaticWidget(parent, data.name) {   // 基类只拿到名字
    applyData(data);
}

void InkingStaticPanel::applyData(const PanelData& data) {
    _data = data;
    _data.Normalize();
    _look.Reset(_data);              // 两态外观 / 状态机 / 过渡一起复位

    _width = _data.width > 0 ? _data.width : 0;
    _height = _data.height > 0 ? _data.height : 0;
    _selfAnchor = _data.selfAnchor;
    _traceAnchor = _data.traceAnchor;
    _offsetX = _data.offsetX;  _offsetY = _data.offsetY;
    _layout = _data.layout;    _layoutItem = _data.layoutItem;

    SetVisible(_data.visible);       // 骨架写入口：自己夹取、自己判"变没变"
    ChangeZIndex(_data.zIndex);
    ClearDirty();                    // 几何到此定型
    NotifySceneStructureChanged();   // 绘制列表里的绝对坐标快照要重算
}
```

**为什么必须逐字段搬**：基类不存整份 `XxxData`。`InkingWidget` 的构造函数只收
`AnchorData` 或一个名字，它不认识 `PanelData`、`ButtonData`——这是分层纪律。
代价很具体：**漏搬一处，就是"配置里写了、运行期不生效"，而且不报错**。
新增会影响几何 / 布局 / 可见性的字段时，必须在这里补一行。

两个细节：几何字段直接写基类的受保护成员，因为静态档没有 `Resize` 之类写入口
（这是构造期一次性写入，不是运行期变化）；可见性与层级走现成写入口
（`SetVisible` / `ChangeZIndex`），它们会夹取、会判"真的变了没有"。
- 末尾的 `ClearDirty()` + `NotifySceneStructureChanged()` 不能省：前者表示
  "构造期的改动已经结算"，后者让场景重算绘制列表里的绝对坐标快照。

### 1.3 观测口：只回答，不推脏

`InkingStaticPanel` 的观测口分成四组：

| 组 | 例子 | 语义 |
| --- | --- | --- |
| 状态 | `IsOpen()` / `GetState()` | **目标态**；过渡进行中也是目标态 |
| 显示值 | `GetDisplayColor()` / `GetDisplayTransform()` | **这一刻实际画出来的值**（过渡中是插值） |
| 几何 | `GetShape()` / `GetWidth()` / `GetHeight()` | 绘制 / 命中 / 包络的共同来源 |
| 配置 | `GetData()` | 构造时那份**已经 `Normalize` 过**的副本 |

规矩只有一条：**观测口不推脏、无副作用**。`GetDisplayColor()` 如果顺手标一个
重绘脏，那么每帧都有人读它，脏标记就永远清不掉。`InkingStaticLabel::GetTextLayout()`
的注释把这条写死了："它按当前宽度排一次并缓存，宽度没变就直接给上一次的结果"——
缓存是允许的，推脏不允许。

`GetDisplayWidth()` / `GetDisplayHeight()` 这个名字在按钮契约里是刻意的：
`GetWidth()` / `GetHeight()` 已经是锚点层的虚函数，组件必须重写它们
（`return GetDisplayWidth();`）。同名会造成多继承下的查找歧义。

### 1.4 写入口：`[final]`、幂等、内部标脏

```cpp
bool SetOpen(bool open) noexcept;   // 状态真的变了才 true
bool Toggle() noexcept;
bool Open() noexcept  { return SetOpen(true);  }
bool Close() noexcept { return SetOpen(false); }
```

三条硬约束：

1. **不加 `virtual`**，也就是 `[final]`。子类不能重写写入口，否则"标脏由写入口
   统一完成"这条保证就破了。
2. **幂等**。同一个值写两次返回 `false`、一个脏都不标，正在跑的过渡也不会被打断
   重起（重起会让动画抖一下）。
3. **标脏在写入口内部完成**。业务代码只管调 `SetOpen`，不需要自己维护命中相关的
   标记。

`SetOpen` 只标**重绘脏 + 命中脏**，**不标结构脏**：

```cpp
if (!_look.SetOpen(open)) return false;
syncBaseColor();
MarkRepaintDirty();   // 画面变了（颜色 / 变换）
MarkDirty();          // 命中可能变了（同一块搬走了）
```

不调 `MarkStructureChanged()` 的理由：布局一个字节都没动，子节点的绝对坐标快照
仍然有效，命中表也不用重建。抽屉来回拉，命中表条目数一条都不变。

对比 `SetVisible`：后者是骨架变化（不画了，但结构还在），它会连带影响绘制顺序，
所以走 `MarkStructureChanged()`。**"内容状态"和"骨架状态"不要混**。

### 1.5 钩子：`on*` 开头、可重写、不标脏

`InkingStaticPanel` 重写了这些钩子：

| 钩子 | 触发时机 | 本类做的事 |
| --- | --- | --- |
| `onRender(pixelX, pixelY)` | 场景提交本节点时 | 画自己那一笔 + 两条滚动条 |
| `onAnimationTick(delta)` | **每渲染帧** | 推进开合过渡，按返回值标脏 |
| `onPointerWheel(dx, dy)` | 滚轮派发到本节点 | 改滚动量 |
| `onPointerPress(press)` | 按下 / 抬起 | 决定"拖滑块"还是"点轨道跳过去" |
| `onPointerDrag(x, y, dx, dy)` | 拖动 | 把滑块中心拖到指针处 |

规矩：

- 重写**必须写 `override`**。漏了就变成"隐藏"，而框架是拿基类指针调的——
  静默不生效，不报错。
- 钩子**不负责标脏**，也不拦截写入口。标脏是写入口的活；钩子只挂附加逻辑。
- `onAnimationTick` 两档都收（颜色 / 变换动画与几何无关），`onTick` 只有动态档收
  （固定逻辑步，默认 50Hz；**它管几何 / 计时，不管输入**）。`onPointerStateChanged`
  也只有动态档收——指针真的变了才来一次（0.4.1 T-02）。三条通道分开是有意的。

### 1.6 标脏：重绘脏与命中脏是两条通道

| | 命中脏 | 重绘脏 |
| --- | --- | --- |
| 标记 | `MarkDirty()` / `IsDirty()` | `MarkRepaintDirty()` / `IsRepaintDirty()` |
| 触发 | 几何、可见性、层级、父子 | 颜色、文字、变换、几何 |
| 消费点 | 帧首（重建绘制列表、必要时重烘表） | 帧末（记账 + 计时清零） |
| 不处理的后果 | 点到的地方和画的地方对不上 | 画面停在上一帧 |
| 安静帧数 | `kHitQuietFrames = 3` | `kRepaintQuietFrames = 5` |

过渡推进的返回值把两件事分开：

```cpp
const PanelLook::AdvanceResult advanced = _look.Advance(deltaSeconds);
if (advanced.repaint) { syncBaseColor(); MarkRepaintDirty(); }
if (advanced.hit)     { MarkDirty(); }
```

- 只换颜色 → 只标重绘脏。**颜色每帧变却标命中脏，就会每帧白重建一次绘制列表**
  （排序 + 重算所有静态节点的坐标快照）。
- 变换变了 → 命中可能变，但要标命中脏；它仍**不改表**（面板的包络覆盖所有状态，
  子孙不进表）。

还有一个容易漏的角落：**显示值在 `Advance` 之外被当场换掉**（例如没配过渡的
状态切换）。`ButtonLook` / `PanelLook` 会先记一笔 `_pendingRepaint` /
`_pendingHit`，下一次 `Advance` 再报出来。漏了这笔账，"没配过渡的开关"就永远
标不上重绘脏——画面看着没问题（每帧全量重画），协议上却漏了一条"画面变了"。

### 1.7 命中：`HitTest` / `HitTestNode` / `GetHitEnvelope`

> 命名提示：注释里偶尔把容器的包络写成"子树包络"，**代码里的真名是
> `GetHitEnvelope()`**，不存在 `GetSubtreeHitEnvelope()`。容器版的区别是
> 它的实现把整棵子树算进包络。

三个函数分工：

| 函数 | 回答什么 | 面板的实现要点 |
| --- | --- | --- |
| `HitTest(x, y)` | 几何上在不在我（含子树） | 先自己本体，再**用世界坐标原样**递归问子节点 |
| `HitTestNode(x, y)` | 子树里**最上面**该收事件的是谁 | **子树优先**，按 `IsAbove` 取最上面的 `Block` |
| `GetHitEnvelope()` | 命中索引用的保守包络 | 布局空间子树并集 × **两个状态**的变换并集 |

三个必须理解的细节：

1. **递归时不要预变换坐标**。子节点的 `AncestorAffine` 已含本层变换，它自己会撤
   干净；父级先换算到本地空间再递归，子节点会**多撤一次**。
2. **`HitTest` 与 `HitTestNode` 的遍历顺序相反**：前者本体重命中就短路，后者必须
   子树优先（子节点画在容器之上）。规则只有一份 `IsAbove`，渲染与命中共用。
3. **包络要覆盖所有可能的状态**。抽屉开着和关着差一个身位；只按当前状态登记，
   拉出来之后表里就找不到它了。面板用 `detail::PanelHitEnvelope` 把两态取并集。

容器还要声明 `OwnsSubtreeHit() == true`：子孙**不进全局命中表**，改由容器递归
解答。好处是命中表条目数一条都不变（抽屉来回拉也不重烘表）；代价是容器内的
子控件要靠场景再问一次 `HitTestNode` 才收得到事件。

裁剪与命中必须同源：`HasSubtreeClip()` 为真时，`HitTest` 在递归前先判
"点在不在裁剪区里"，不在就整棵子树都不命中；绘制侧用的是同一个矩形。

### 1.8 布局：测量是纯函数，重排是显式动作

```cpp
virtual LayoutSize MeasureLayout(const LayoutConstraints&) const;  // [√] 纯函数
void Relayout() noexcept;                                          // [final] 从我这棵子树排
void RelayoutParent() noexcept;                                    // [final] 让上游容器排
```

- `MeasureLayout` **不许改几何、不许标脏、不许有副作用**。布局可能问好几遍，
  有副作用就会"问一次变一次"。
- `Relayout` / `RelayoutParent` **显式、幂等**。框架**不做隐式扫描**：不在结构脏
  消费里替所有容器跑一遍。理由写在 `InkingWidget.h`：否则"什么时候重算"看不见，
  嵌套容器容易重入。代价是调用方要记得喊一声，所以它必须幂等——算出来跟当前
  几何一模一样时一个脏都不标，多喊几次不白干。
- 内容驱动尺寸的组件（标签换长文字）调 `RelayoutParent()`：自己变高、兄弟要让位，
  那是父容器的活。
- `ApplyLayoutRect()` 是布局层唯一的几何写入口，**只有
  `detail::layout::ArrangeSubtree` 是 friend**；业务代码调它是编译错误。

面板的 `UpdateScrollRange()` 遵循同一条纪律：显式、幂等，不靠框架每帧扫描子节点。
"内容变大了"要自己喊一声，和 `Relayout()` 一样。

---

## 2. 静态档 vs 动态档：只有一条判据

**几何会不会在构造之后自己变。**

| | 静态档 `InkingStaticWidget` | 动态档 `InkingDynamicWidget` |
| --- | --- | --- |
| 几何写入口 | **没有**（构造即定型） | `Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset` |
| 命中 | **进全局命中表** | **不进表**，自己判；占地记成"洞" |
| 输入 | 场景**推结果**（`onPointer*`） | 场景在指针变化时推**通知**，自己拉状态（`onPointerStateChanged`） |
| 每帧 Tick | 不收 `onTick` | 收 `onTick`（固定逻辑步，默认 50Hz；只管几何 / 计时） |
| 颜色动画 | 收 `onAnimationTick` | 收 `onAnimationTick` |

选择顺序：

1. 三态只换颜色 / 变换、按下"下沉"只是绘制偏移 → **静态**。按钮就是这条路。
2. 按下要改尺寸、撑开兄弟、内容驱动高度 → **动态**。动态按钮就是这条路。
3. "运行期挪一下"但不想改占位 → 用**外部显示变换**（A13），不要为此选动态档。
   代价是它会动命中表（任意拖动无界，整表重烘），且不改变布局占位。

静态档不是"不能动"：它能做颜色 / 透明度 / 变换过渡，因为那些不标命中脏。
`src/inking.cpp` 里有编译期断言钉着"静态组件不该有 `Resize`"等四条；给静态组件
加几何写入口，构建会当场变红。

### 2.1 动态档的对照样板：`InkingFixedPopup`

前面通篇拿 `InkingStaticPanel` 当样板（静态档）。动态档从 0.4.1 起有一份等价的：
`panel/InkingFixedPopup.h`（定点浮窗）。八段式在它那里的对应关系：

| 段 | 落点 |
| - | - |
| 数据面 | 复用 `PanelData`，只取 `closed` 那份（`open` / `startOpen` / `scroll` 忽略） |
| 构造搬字段 | `ToAnchorData()` 搬定位字段；`OpenAt` 里再落位 |
| 观测口 | `IsOpen` / `GetResolvedSideX/Y` / `IsClampedX/Y` / `GetShape` |
| 写入口 | `OpenAt` / `SetTarget` / `Close`（内部走 `ChangeSelfAnchor` + `ChangeOffset`） |
| 钩子 | `onRender`（填充 + 阴影）/ `onRenderDecoration`（描边）/ `onPointerStateChanged` |
| 命中 | `HitTest` + `OwnsSubtreeHit() = true` + `PanelHitEnvelope`（整棵子树并集） |
| 输入 | `onPointerStateChanged`（指针变化通知）+ `detail::PointerProbe` |

与静态面板**唯一的结构差别**是档位：几何会变 ⇒ 不进命中表、命中靠 `QueryHit` 对容器深挖。
除此之外的每一段都值得照抄——尤其是"填充与阴影在绘制趟、描边在装饰趟"那条（§1.7 的同一条理由）。

---

## 3. `InkingButtonLike`：契约 + 实现的混入基类

`InkingButtonLike` 不是纯接口，是**带状态的混入基类**（header-only，没有 `.cpp`）：

```cpp
class MyButton : public InkingStaticWidget,
                 public InkingButtonLike { /* ... */ };
```

它持有 `ButtonLook _look`（三态外观、过渡计时、`clicked` 事件槽），并给出
`GetState` / `GetAppearance` / `GetDisplayColor` / `GetDisplayTransform` /
`GetDisplayWidth/Height` / `TriggerClick` / `MouseHover` / `MousePress` /
`MouseRelease` 等公开面。组件继承即得，一个都不用转发。

### 3.1 为什么不做成纯接口

它**必须带状态**：`ButtonLook` 那十几个字段、过渡的起点与计时、`clicked` 槽，
全是实现而不是契约。抽成纯接口之后，每个使用者都要把状态机与二十来行转发
再写一遍——正是这个文件要消除的东西。而且本仓库不做类型查询（"钩子即能力
发现"），`IInkingButtonLike*` 这个多态手柄没有消费者；继承来的
`InkingButtonLike*` 本身就已经是"能指向任何一个按钮"的手柄。

### 3.2 绝不与 `InkingPointerHandler` 同时继承

`InkingWidget` 已经继承了 `InkingPointerHandler`。如果混入基类再继承一次，
就是**菱形**：两个 `InkingPointerHandler` 子对象，`PushPointerHover` 直接歧义，
而且**未必编译报错**——可能静默推到另一个子对象上，然后"钩子看起来没生效"。

所以 `InkingButtonLike` **不继承任何东西**，只提供四个入口
（`MouseHover` / `MousePress` / `MouseRelease` / `TriggerClick`）。组件在自己的
`onPointer*` 里转发一行：

```cpp
void MyButton::onPointerHover(bool hover) { MouseHover(hover); }
void MyButton::onPointerPress(bool press) { MousePress(press); }
void MyButton::onPointerClick()           { TriggerClick();  }
```

### 3.3 它一个字都不标脏

它只回答"状态变了没有""显示值变了没有"，然后调 `[√]` 钩子
`onButtonStateChanged` / `onButtonDisplayAdvanced` **报告**；标脏由组件决定。
颜色与变换不属于会标脏的四件事，所以"只换颜色"这条路上不该有标脏调用。

### 3.4 图片为什么不继承它

`InkingStaticImage : public InkingStaticWidget`——**没有**继承 `InkingButtonLike`。
"长得像"不等于"是"：

- **四个观测口的实现全不同**：`GetAppearance` / `GetDisplayColor` /
  `GetDisplayTransform` / `GetHitEnvelope` 都自己实现；图片只有 `normal` 一态，
  变换还要复合外部显示变换。套进按钮契约反而到处是"只认 Normal"的特例。
- **没有三态、没有状态机、没有过渡、没有 `clicked`**，也没有吃输入的钩子；
  它要的只是"按钮那条绘制"与"图片从哪来"的两条路。

> 注：`docs/zh/api/InkingComponents.md` 里"图片继承 `InkingButtonLike`"那段
> 已经过时，以头文件为准（`SKILL.md` §0：注释与文档冲突，以头文件为准）。

---

## 4. 为什么 `ButtonLook` 与 `PanelLook` 没抽成 `TransitionTrack`

现状是**已知的重复**：两者的"起一段过渡、按缓动插值、收尾落目标值、把当场改值
记成待报告账"逻辑上是同一件事，各写了一份。A1 没有抽，理由有两条，原文在
`include/panel/PanelLook.h:14-24`：

1. **抽它要动唯一一个已验证的组件**（Button），而回归测试在 A1 之前被整块移除
   ——没有回归网时，动它是拿"已经对的东西"换"更整齐的东西"。
2. **任务纪律 r2：一次运行最多完成一个任务。**

这两条不是"以后再说"的托词，而是有明确触发条件的欠账：**第三个需要过渡的组件
出现时**，就该把它抽出来（那时有两条调用方 + 自检，回归网至少覆盖了调用方）。

### 4.1 抽的时候要带走什么

过渡的起点 / 已用时长 / 总时长 / 缓动曲线；`Advance(delta)` 的 `repaint` / `hit`
两布尔；"当场换掉显示值"的 `_pendingRepaint` / `_pendingHit`；以及收尾直接落
目标值的浮点处理。

### 4.2 抽取的收益与风险 †

收益是消灭约一份状态机代码；风险是**改坏唯一验证过的动画路径**，而且"静态 / 动态
手感不一致"只在两边对照时才看得见。没有逐项耗时 / 行数统计，这里不编数字；
判断标准是"第三个组件出现"，不是"代码看起来重复"。

---

## 5. 新增组件 checklist

按顺序过一遍，任何一项没有具体落点就先别写实现。

| # | 检查项 | 具体做法 |
| --- | --- | --- |
| 1 | 目录 | `include/<组件>/` + `src/<组件>/`；静态类 `InkingStaticXxx`，动态类 `InkingDynamicXxx` |
| 2 | 数据 | 纯数据 `XxxData`；有"省略即继承"就写幂等 `Normalize()` |
| 3 | 档位 | 几何构造后会不会变？会 → 动态；不会 → 静态。不要两者都继承 |
| 4 | 构造 | 基类只给名字；`applyData` 里**逐字段**搬；末尾 `ClearDirty()` + `NotifySceneStructureChanged()` |
| 5 | 观测口 | `const noexcept`；只读；不推脏；显示值与目标值分开命名 |
| 6 | 写入口 | 不加 `virtual`；返回 `bool`；幂等；内部标脏 |
| 7 | 钩子 | `on` 开头；写 `override`；不标脏；`onAnimationTick` 走渲染帧 |
| 8 | 标脏 | 颜色 / 文字 / 变换 → 重绘脏；几何 / 可见 / 层级 / 父子 → 结构脏 |
| 9 | 命中 | `HitTest` 与绘制用同一份形状；容器决定 `OwnsSubtreeHit` / `HitTestNode` |
| 10 | 包络 | 变换会变 → 重写 `GetHitEnvelope()`，覆盖**所有**状态 |
| 11 | 布局 | `MeasureLayout` 纯函数；内容变了显式 `Relayout()` / `RelayoutParent()` |
| 12 | 断言 | 若改了静态 / 动态边界，补 `src/inking.cpp` 的编译期断言 |
| 13 | 自检 | 给可断言的观测口（计数 / 尺寸 / 状态），不要只靠"看着对" |

最后一条尤其重要：本仓库的验收纪律是"能变成数值判据的自己验"。
一个组件如果没有任何计数或几何观测口，它就只能靠肉眼验收——那等于没有验收。

---

> 本文档对应版本：0.5.2（0.x 发布版）。
