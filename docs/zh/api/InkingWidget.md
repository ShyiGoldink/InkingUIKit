# 节点（Widget）

**回到API目录：**
[API index](../API.md)

`InkingWidget` 是"能进渲染树的东西"。它站在定位层（`InkingAnchor`）之上，回答定位层
不回答的全部问题：我在哪棵树里、谁是我父级、我属于哪个场景、谁在我上面、我要不要重画、
谁点得到我、我收到什么输入、我怎么排我的孩子、我画成什么样。

**它不是组件基类，而是所有组件的公共祖先；它自己也不能直接实例化**——每个组件声明时
必须选一种档位（静态 / 动态）。

你可以使用 `#include <widget/InkingWidget.h>` 引入核心头；
两个档位分别在 `<widget/InkingStaticWidget.h>` / `<widget/InkingDynamicWidget.h>`。

## 三条继承线，各管一件事

```cpp
class InkingWidget : public InkingAnchor,             // 定位：锚点公式、父链、可见性、倍率
                     public InkingPointerHandler,     // 指针契约：5 钩子 + Push + 滚轮声明
                     public InkingKeyboardTarget      // 键盘契约：5 钩子 + Push + 焦点意愿
```

**输入是能力，不是第二套继承**。所以任何 Widget 天然带这两份契约，重写钩子就能收输入，
没重写的拿默认空实现，什么都不会发生。

> 别拿这两条继承线做 `dynamic_cast` 去"发现能力"。场景是**推**事件的，
> 类型查询会把早年那个 `static_cast<InkingStaticButton*>` 的 UB 装回来
> （见 [InkingPointerHandler.md](InkingPointerHandler.md#两条设计约束)）。

## 三档：基类不可实例化

| 档位 | 类 | 几何写入口 | 进命中表 | 每帧 Tick |
| --- | --- | --- | --- | --- |
| 基类 | `InkingWidget` | 无（构造 `protected`） | — | — |
| **静态** | `InkingStaticWidget` | **没有**（构造即定型） | **进**（静态层） | 不收 `onTick` |
| **动态** | `InkingDynamicWidget` | `Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset` | **不进**（自己判） | 收 `onTick` |

判据只有一条：**几何会不会在构造之后自己变**。分流不靠运行期标记——构造时就传进去
（`InkingWidget(parent, data, /*dynamic=*/bool)`），登记那一刻已经明确，
所以查询与合并时不需要再问一次"你是不是动态的"。

两档共用的东西全在基类：只读查询、层级、父级、可见性、倍率、标脏、`IsAbove`。
**"静态"少的只有几何写入口。**

> 颜色动画（三态过渡）不该因为"几何不变"就没法推进，所以 `onAnimationTick` 两档都收；
> 而 `onTick` 只有动态档收。两条通道分开是有意的。

## 两个"脏"，别混

| | 命中脏 | 重绘脏 |
| --- | --- | --- |
| 触发 | 几何 / 可见性 / 层级 / 父子 | 颜色 / 文字 / 变换 / 几何 |
| 消费点 | **帧首**（重建绘制列表 + 重烘命中表） | **帧末**（记账 + 计时清零） |
| 不处理的后果 | 点到的地方和画的地方对不上 | 画面停在上一帧的样子 |
| 安静帧数 | `kHitQuietFrames = 3` | `kRepaintQuietFrames = 5` |

**动画每帧改颜色时如果错标成命中脏，就会每帧白重建一次绘制列表**（排序 + 重算所有静态
节点的坐标快照）。这条分开就是为了不付那个代价。

标脏的入口有两个，都由框架调：`MarkDirty()`（写入口内部调，子类不要自己调）与
`MarkRepaintDirty()`（写入口与 `onAnimationTick` 里调）。

## 树与所有权：借用 vs 拥有

| | 怎么来 | 谁销毁 | 在哪露面 |
| --- | --- | --- | --- |
| **借用型** | 构造时传 `parent` 的成员、栈对象 | **调用方** | `GetChildren()` |
| **拥有型** | 运行期 `AddChild(std::unique_ptr)` | **本节点**（`RemoveChild` / `ClearChildren` / 析构） | `GetChildren()` 与 `GetOwnedChildCount()` |

- `GetChildren()` 是"所有子节点"的**视图**（两种都含），顺序按注册先后。
- `GetParent()` 与基类的 `GetAnchorParent()` **同源**（同一条 `_parent` 指针），
  一份存储、两个视图；区别只是类型（`InkingWidget*` vs `InkingAnchor*`）。
  **树不进定位层**——别加第二个父指针。
- 子节点先销毁时自己会摘掉；**父节点先销毁时子节点的 `_parent` 会指向一个已经没了的对象**
  （与父链一直以来的行为一致）。这条不是 bug，是"借用"的代价。

## 命中：三态与"深挖一层"

| 概念 | 函数 | 谁回答 |
| --- | --- | --- |
| 几何上在不在我身上 | `HitTest(worldX, worldY)` | 组件（按 SDF 形状） |
| 我子树里最上面该收事件的是谁 | `HitTestNode(worldX, worldY)` | 容器（默认返回自己） |
| 命中索引用的保守包络 | `GetHitEnvelope()` | 组件（默认整块矩形） |
| 命中之后让不让过 | `SetHitPassThrough(bool)` / `IsHitPassThrough()` | 策略，不是几何 |

三态：**Block**（默认，事件到此为止）/ **PassThrough**（我在这儿但继续往下找）/ **Miss**。

**谁负责 z 序：调用方**。`HitTest` 只回答"我在不在这个点下面"，不回答"是不是我最上面"
——后者按绘制列表从高到低问，规则只有一份（`InkingWidget::IsAbove`：z 优先、同 z 看注册序号）。
**渲染顺序和命中顺序必须共用这一个比较器**，否则就是"画的是 A、点到的是 B"。

容器（`OwnsSubtreeHit() == true`）的子孙**不进全局命中表**，所以场景查到容器之后还要再问
一次 `HitTestNode` 才能把事件发到容器里的按钮上——否则容器里的控件收不到 hover / 点击，
也**拿不到键盘焦点**。

## 子树变换与裁剪

| 钩子 | 默认 | 用途 |
| --- | --- | --- |
| `GetSubtreeTransform()` | 单位变换 | 抽屉 / Toast / 下拉：变换作用于**整棵子树**（提交层负责复合） |
| `OwnsSubtreeHit()` | `false` | 本节点自己递归解答子树命中（子节点不进表） |
| `HasSubtreeClip()` | `false` | 把**子孙**超出裁剪区的部分剪掉（容器自己那一笔不剪） |
| `GetSubtreeClipBounds()` | 本体整块矩形 | 裁剪区（本节点布局空间，相对左上角） |
| `GetClipTransform()` | `GetSubtreeTransform()` | 裁剪窗口跟哪一份变换走 |

`GetClipTransform()` 存在的理由很具体（踩过）：**滚动容器**把滚动位移放进了
`GetSubtreeTransform()`（内容要跟着滚），而裁剪区也读那一份 ⇒ 裁剪窗口跟着内容一起跑了。
正确分工是**内容用子树变换、裁剪窗口用 `GetClipTransform()`**（容器本体，不含滚动）。

绘制与命中**必须同源**：绘制按 `GetSubtreeClipBounds()` 设裁剪矩形，命中的容器在递归之前
先问"这个点在不在我的裁剪区里"。

## 运行期显示变换（A13）

`SetDisplayTransform` / `ClearDisplayTransform` 是**变换通道**在运行期的写入口：
只改"画在哪儿 / 画多大"，本地几何一个字节都不动。

三条要记住：

1. **默认关**：组件类型要重写 `AcceptsExternalTransform()` 返回 true 才认，
   否则调用被**拒绝并计数**（`GetDisplayTransformRejectedCount()`）——不静默。
2. **会动命中表**：表里登记的是"所有可能变换的并集"，而任意拖动是**无界**的
   ⇒ 每次真的改了变换都标**结构脏**，下一帧整表重烘。这是框架里**唯一**一条会动表的显示通道。
3. **幂等**：同一份变换重复设置返回 false、一个脏都不标
   （观测口 `GetDisplayTransformRevision()`）。

与组件自己那份变换是**复合**关系：`GetDisplayTransform()` 里照抄
`return CombineWithExternal(内部那份);`，两者绕同一个中心（组件中心），所以复合是闭式的。

## 布局（A6）

规则（`LayoutSpec`）住在**父容器**的数据里，孩子只写自己的**意愿**（`LayoutItem`）。
算出来的尺寸与位置写进**运行期几何**（`_width` / `_height` / `_offsetX` / `_offsetY`），
所以位置推导一条路都没变——绘制 / 命中 / AABB / 子树变换都不知道"有布局这回事"。

- `MeasureLayout(constraints)`：`[√]` 钩子，**纯函数**（不许改几何、不许标脏、不许有副作用）。
- `Relayout()` / `RelayoutParent()`：`[final]`，**显式**请求重排，幂等。
  **框架不做隐式重排**——凡"排布输入变了"（文字改长、列表增删、容器自己改尺寸）调用方要喊一声。
- `ApplyLayoutRect()`：`protected`，**只有 `detail::layout::ArrangeSubtree` 是 friend**
  ——业务代码想调是**编译错误**，不是文档约定。

详见 [InkingLayout.md](InkingLayout.md) 与 [InkingLayoutSolver.md](InkingLayoutSolver.md)。

## 用到的数据结构

`InkingWidget` 不定义新的公开数据结构，构造参数与字段来源是：

> **AnchorData**（`dataStruct/InkingAnchorData.h`，详见 [InkingAnchor.md](InkingAnchor.md)）

构造时**拆存**成自己的字段（基类不存整份 data）：`selfAnchor` / `traceAnchor` /
`offsetX/Y` / `width` / `height` / `zIndex` / `visible` / `color` / `layout` / `layoutItem`。

> **TransformSpec**（[InkingTransform.md](InkingTransform.md)）
> **LayoutSpec / LayoutItem / LayoutRect / LayoutSize / LayoutConstraints**（[InkingLayout.md](InkingLayout.md)）
> **ShapeBounds**（[InkingShapeSpec.md](InkingShapeSpec.md)）

## APIS

### Public

#### 身份与父子

>**GetName()**

```C++
const std::string& GetName() const noexcept;
```

节点名（配置驱动那条路与 CXXCSS / `ButtonLibrary` 里的名字一致）。

>**GetParent()**

```C++
InkingWidget* GetParent() const noexcept;
```

父节点，**非拥有**。隐藏基类的 `GetAnchorParent()`：那一条是定位链（`InkingAnchor*`），
这一条是树语义（`InkingWidget*`），同一条指针的两个视图。根（窗口 / 场景）返回 `nullptr`。

>**GetChildren()**

```C++
const std::vector<InkingWidget*>& GetChildren() const noexcept;
```

直接子节点（**非拥有**的裸指针视图，含拥有型子节点）。顺序按注册先后（同 z 时
"后构造的在上"，与绘制列表同源）。**不负责生命周期**。

#### 尺寸与位置

>**GetWidth() / GetHeight()**

```C++
int GetWidth() const noexcept override;
int GetHeight() const noexcept override;
```

**画出来 / 判命中的那一份**尺寸（设计坐标）。基类返回推导用的 `_width`；
"每态尺寸"那一档（静态按钮三态各一份）重写它返回插值后的显示尺寸，
于是绘制与命中天然同一个答案。**重写要写 `override`**。

>**GetAbsX() / GetAbsY()**

```C++
float GetAbsX() const noexcept;
float GetAbsY() const noexcept;
```

相对**根**的绝对位置（设计坐标），**O(深度)**。绘制、命中、包络、裁剪四处都问它——
它们是"我画在哪儿"这**一个**答案的共同来源，必须与提交给渲染器的左上角同源。

>**GetDeviceWidth() / GetDeviceHeight()**

```C++
float GetDeviceWidth() const noexcept;
float GetDeviceHeight() const noexcept;
```

设计单位 → 设备像素（窗口缩放 × 系统 DPI）。letterbox 下 XY 同倍率。

>**HasLayoutAnimation()**

```C++
virtual bool HasLayoutAnimation() const noexcept { return false; }
```

"我的尺寸会随状态变吗"。只影响一件事：位置 / 尺寸快照还准不准。
**它不是"是不是动态档"**——这里仍然是构造期烘好的有限集合（三态各一份）+ 状态机切换。

#### 层级与展示

>**GetZIndex() / GetRegisterOrder()**

```C++
int GetZIndex() const noexcept;
int GetRegisterOrder() const noexcept;
```

z 越大越靠上；`_registerOrder` 只在 z 相同时决定先后（构造时自动赋）。

>**GetColor()**

```C++
std::uint32_t GetColor() const noexcept;
```

占位填充色 `0xAARRGGBB`。**颜色不标脏。**

>**IsAbove(a, b)**

```C++
static bool IsAbove(const InkingWidget& a, const InkingWidget& b) noexcept;
```

a 是否盖在 b 上面：**按树序 + 兄弟内 z** 比较（局部层级）。

规则三条：① 父容器先于自己的子孙画（底色永远在下）；② `zIndex` 只在**兄弟之间**生效；
③ 同 z 时晚注册的在上。等价于"从根到自己的 (z, 注册序号) 序列按字典序比较"，
前缀短者（祖先）在前。它仍是**一个严格全序**，所以绘制、命中表、面板子树仲裁、
动静合并四处可以继续共用它。

**渲染顺序和命中顺序必须共用这一个比较器。**

边界：子节点的 z 再大也盖不过**父级的兄弟**（真正的逃逸要 portal，尚未提供）——
"展开的下拉要压住别的内容"应该抬高**容器自己**，而不是抬孙子。容器自绘只有一笔
（底色+描边一起提交），整体在子孙之下。

#### 标脏

>**IsDirty() / ClearDirty()**

```C++
bool IsDirty() const noexcept;
void ClearDirty() noexcept;
```

命中脏。一般不用手工清：帧末的 `ConsumeDirtyFrame` 会在连续 `kHitQuietFrames` 帧无新脏时
自己清。手工调只有一种正当用途——"几何到此定型"（静态按钮构造末尾就这么做）。

>**IsRepaintDirty() / ClearRepaintDirty()**

```C++
bool IsRepaintDirty() const noexcept;
void ClearRepaintDirty() noexcept;
```

重绘脏：画面变了但命中表**不需要**重烘。

>**ConsumeDirtyFrame()**

```C++
void ConsumeDirtyFrame() noexcept;
```

帧末统一消费里的一步：推进安静帧计数，够数就清脏。**同一帧里标了多少次脏都只走这一回**。

>**IsSceneRoot() / IsDynamic() / GetSceneAncestor() / GetScene()**

```C++
bool IsSceneRoot() const noexcept;
bool IsDynamic() const noexcept;
InkingScene* GetSceneAncestor() noexcept;
InkingScene* GetScene() const noexcept;
```

`IsSceneRoot()`：自己就是某棵渲染树的根（场景 / 窗口），没有场景祖先可登记。
`IsDynamic()`：构造时就定死（静态 / 动态是两个类型），不是运行期猜的。
`GetScene()`：所属场景（非拥有），没有时为 `nullptr`。

#### 运行期增删子节点

>**AddChild(child)**

```C++
InkingWidget* AddChild(std::unique_ptr<InkingWidget> child);
```

挂一个**由本节点拥有**的子节点。返回裸指针只是便利——所有权已经进了本节点的表，
**别**再塞进别处的 `unique_ptr`。若它原来挂在别处，先被从旧父级摘干净（跨场景也行）。

>**RemoveChild(child)**

```C++
std::unique_ptr<InkingWidget> RemoveChild(InkingWidget* child);
```

摘下并**把所有权交回调用方**。`nullptr` = 它不在拥有表里（借用型子节点走这条路会拿到
`nullptr`，要摘用 `SetParent(nullptr)`）。摘下之后脱离渲染树但**还活着**。

>**ClearChildren() / GetOwnedChildCount()**

```C++
std::size_t ClearChildren() noexcept;
std::size_t GetOwnedChildCount() const noexcept;
```

清空**拥有型**子节点（借用型不受影响）；返回销毁了几个。/ 拥有几个
（`GetChildren().size()` 是"所有子节点"，可能更多）。

#### 命中

>**HitTest(worldX, worldY)**

```C++
virtual bool HitTest(float worldX, float worldY) const;
```

指针在这个位置上吗（**世界坐标 / 设计空间**）。默认 `false`：基类没有形状概念，
"怎么算在里面"是组件的知识（按钮问自己的 SDF，`ShapeContains`）。
它只回答"几何上在不在"，**不回答"要不要让过"**。

>**HitTestNode(worldX, worldY)**

```C++
virtual InkingWidget* HitTestNode(float worldX, float worldY) noexcept;
```

节点版：这个点落在我的子树里，"最上面那个该收事件"的是谁。默认：命中就是我自己。
容器重写时按三条契约来（返回最上面的 `Block` 命中者 / 子树里只有 `PassThrough` 时返回
`nullptr` / 什么都没命中返回 `nullptr`）。

>**GetHitEnvelope()**

```C++
virtual ShapeBounds GetHitEnvelope() const;
```

命中索引用的**保守包络**（相对本节点左上角，设计坐标）。默认整块矩形——给大了只是候选多
几个，**不会误命中**。**变换会在运行期变时必须 override**（包络要覆盖所有可能变换，
否则表里登记不全 → 漏命中）。

>**SetHitPassThrough(passThrough) / IsHitPassThrough()**

```C++
void SetHitPassThrough(bool passThrough) noexcept;
bool IsHitPassThrough() const noexcept;
```

命中之后让不让过。默认 `false` = **Block**。**不标脏**：让不让过是策略不是几何
（代价是当前帧仍用旧策略，最迟下一帧生效）。

#### 输入归属

>**FindSubtreeWheelOwner()**

```C++
InkingWidget* FindSubtreeWheelOwner() noexcept;
```

沿父链找最近的"要滚轮"的容器（含自己）；没人要就 `nullptr`。
**只有它留在本层**——接口给的是"我要不要滚轮"（`HandlesSubtreeWheel`），
而"沿父链找"要用树结构。

>**HasKeyboardFocus() / MarkKeyboardFocus(focused)**

```C++
bool HasKeyboardFocus() const noexcept;
void MarkKeyboardFocus(bool focused) noexcept;   // 框架调
```

焦点是**场景级**的唯一归属，字段在本层（接口是零状态的）。改它要跟场景打交道，
`InkingKeyboardTarget` 不该知道场景。业务只读，不要自己写。

#### 骨架写入口（`[final]`）

>**SetVisible(visible)**

```C++
bool SetVisible(bool visible) noexcept;
```

不画了，但结构还在（数据、子节点、命中条目都保留）。连带影响绘制顺序，按骨架变化处理。

>**ChangeZIndex(zIndex)**

```C++
bool ChangeZIndex(int zIndex) noexcept;
```

层级变化是要标脏的四件事之一。

>**SetParent(parent)**

```C++
bool SetParent(InkingWidget* parent) noexcept;
```

换父级（跨场景也行）。摘旧挂新 + 标脏 + 通知场景重排。

>**SetMagnification(magnification)**

```C++
void SetMagnification(float magnification) noexcept;
```

窗口层写，**不标脏**。

三者都返回 `bool`：这次调用**真的改了**才为 true；没变就不标脏、不触发钩子。

#### 场景登记（框架内部）

>**RebindScene(scene) / RegisterToScene(scene) / UnregisterFromScene() / DetachFromScene()**

```C++
void RebindScene(InkingScene* scene) noexcept;
void RegisterToScene(InkingScene* scene) noexcept;
void UnregisterFromScene() noexcept;
void DetachFromScene() noexcept;
```

只由框架调。`RebindScene` 补的是一处构造顺序问题：场景"构造完成才盖章成根"，
所以**场景构造函数体里建出来的子节点**沿父链找不到它，不补登记既不渲染也点不到
（而且不报错）。
`DetachFromScene` 只置空 `_scene`、**不碰列表**——给"场景正在析构"那条路径用。

>**NotifyStructureChanged() / MarkStructureChanged()**

```C++
void NotifySceneStructureChanged() noexcept;
void MarkStructureChanged() noexcept;   // protected
```

`MarkStructureChanged` 是骨架变化的统一出口（命中脏 + 重绘脏 + 通知场景重排，一次做完）。
集中在一条路上的两个好处：不会漏；将来"把标脏送到最近的表边界"只改这一处。

>**SubmitToRenderer(renderer, absX, absY)**

```C++
void SubmitToRenderer(SDL_Renderer* renderer, float absX, float absY) const;
```

由场景的渲染入口按 z 序逐个调用；传的是**设计坐标**，这里换算成设备像素再交给 `onRender`。

>**TickAnimation(deltaSeconds)**

```C++
void TickAnimation(float deltaSeconds) noexcept;
```

每渲染帧一次：推进本节点自己的动画（颜色 / 透明度过渡）。**两档都会收到**。

### Protected

>**构造函数（两种，都带 `dynamic`）**

```C++
InkingWidget(InkingWidget* parent, const AnchorData& data, bool dynamic);
InkingWidget(InkingWidget* parent, const std::string& name, bool dynamic);
```

只给两个派生类调。给 `parent` + 名字的那条走 CXXCSS 配置（怎么读由子类实现）；
给完整数据结构的走静态初始化。根（窗口 / 场景）传 `nullptr` 当 parent。

>**~InkingWidget()**

```C++
~InkingWidget() override;
```

**必须写在 .cpp 里**：它要反注册自己并释放拥有的子节点，不能 `= default`。
拷贝 / 移动四个全 delete（持有父指针 + 登记进绘制列表，复制出的第二份关系会让两边
指向同一个对象）。

>**onRender(pixelX, pixelY)**

```C++
virtual void onRender(float pixelX, float pixelY) const;
```

本节点怎么画。入参是**设备像素**的绝对左上角（倍率已换算）。
基类实现只是"按 color 填一个矩形"（`detail::FillDefaultRect`，矩形就是 `ShapeSpec::Rect()`，
走同一套形状填充，所以抗锯齿 / 倍率换算不会"按钮一条路、默认矩形另一条路"）。

>**onAnimationTick(deltaSeconds)**

```C++
virtual void onAnimationTick(float deltaSeconds);
```

本节点自己的动画，每渲染帧一次（真实 delta）。基类什么都不做。
**两档都收**——颜色过渡与几何无关，所以不归动态档那条 `Tick`。

>**几何 / 骨架钩子**

```C++
virtual void onSizeChanged();
virtual void onSelfAnchorChanged();
virtual void onTraceAnchorChanged();
virtual void onOffsetChanged();
virtual void onZIndexChanged(int zIndex);
virtual void onParentChanged(InkingWidget* parent);
virtual void onVisibleChanged(bool visible);
virtual void onDirty();
```

钩子只挂附加逻辑：**不负责标脏**（写入口已经标了），也不拦截写入口。

>**MeasureLayout(constraints)**

```C++
virtual LayoutSize MeasureLayout(const LayoutConstraints& constraints) const;
```

`[√]` 测量钩子：**我在这份约束下想要多大**。契约是**纯函数**——不许改几何、不许标脏、
不许有副作用（布局可能问好几遍）。默认返回"我当前的尺寸"。

>**Relayout() / RelayoutParent()**

```C++
void Relayout() noexcept;
void RelayoutParent() noexcept;
```

`[final]`。`Relayout` 从**我这棵子树**开始同步排一次；`RelayoutParent` 往父链找第一个
声明了布局的祖先让它排一次（给"内容驱动的尺寸变了"用——标签换了更长的文字，
自己的高度变了而**兄弟要让位**，那是父容器的活儿）。两只都**幂等**。

>**ApplyLayoutRect(rect)**

```C++
bool ApplyLayoutRect(const LayoutRect& rect) noexcept;
```

布局层专用的几何写入口。**只有 `detail::layout::ArrangeSubtree` 是它的 friend**——
业务代码想调它是**编译错误**，不是文档约定。名字带 `Layout`、参数是布局层的 `LayoutRect`，
不会被误认成 resize；幂等（矩形没变返回 false、一个脏都不标）。

>**MarkAsSceneRoot(scene) / DetachSceneFromSubtree()**

```C++
void MarkAsSceneRoot(InkingScene* scene) noexcept;
void DetachSceneFromSubtree() noexcept;
```

场景专用：构造末尾盖章成渲染树的根 / 析构前把子树里指向自己的 `_scene` 摘成 `nullptr`。

## 两个档位

>**InkingStaticWidget**

```C++
class InkingStaticWidget : public InkingWidget {
public:
    InkingStaticWidget(InkingWidget* parent, const AnchorData& data);
    InkingStaticWidget(InkingWidget* parent, const std::string& name);
protected:
    void onRender(float pixelX, float pixelY) const override;
};
```

构造即定型，**没有几何写入口**，进命中表。`src/inking.cpp` 里有编译期断言钉着
"静态档不能有 `Resize`"这条。

>**InkingDynamicWidget**

```C++
class InkingDynamicWidget : public InkingWidget {
public:
    InkingDynamicWidget(InkingWidget* parent, const AnchorData& data);
    InkingDynamicWidget(InkingWidget* parent, const std::string& name);

    bool Resize(int width, int height) noexcept;
    bool ChangeSelfAnchor(const AnchorPoint& anchor) noexcept;
    bool ChangeTraceAnchor(const AnchorPoint& anchor) noexcept;
    bool ChangeOffset(float offsetX, float offsetY) noexcept;

    void Tick(float deltaSeconds) noexcept { onTick(deltaSeconds); }
protected:
    virtual void onTick(float deltaSeconds);
    void onRender(float pixelX, float pixelY) const override;
};
```

四个几何写入口都是 `[final]`、都幂等（没变返回 false）。**不进命中表**，
输入由它自己在 `Tick` 里从场景**拉**（`GetPointerX/Y` / `IsPointerDown` / `IsPointerInside`）。

`Tick` 走**固定逻辑步**（默认 50Hz），而指针状态每渲染帧更新 ⇒ 短于 20ms 的点击
看不出来（人手点击 80ms 起步，够用）。

## 常见坑

1. **构造忘了传 parent**：沿父链找不到场景 ⇒ 不渲染、不命中，**不报错**。
2. **场景构造函数体里建子节点**：那一瞬间场景还没盖章，靠 `rebindEarlyChildren` 兜住；
   自己写容器时若在"根还没成为根"的时刻建节点，也要想到这一条。
3. **重写钩子漏 `override`**：变成隐藏，基类指针调过来静默不生效。
4. **颜色动画标了命中脏**：每帧白重建一次绘制列表（排序 + 重算所有静态坐标）。
5. **以为 `GetAbsX()` 很便宜**：它是 O(深度)。每帧对大量节点调它之前先想清楚。
6. **给静态组件加几何写入口**：`src/inking.cpp` 的编译期断言会当场拦住你。
7. **想用 `dynamic_cast` 发现"这组件有没有三态"**：回到"钩子即能力发现"那条路。

---

> 本文档对应版本：**0.1.0-test（测试版本）**。
