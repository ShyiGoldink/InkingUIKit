# 组件

**回到API目录：**
[API index](../API.md)

"组件"= 能直接 `new` 出来放进场景的东西。本页按族过一遍，每个组件给**数据面 → 构造 →
关键 API → 边界**四段。公共行为（标脏、命中、输入、布局）在
[InkingWidget.md](InkingWidget.md)，不在这里重复。

| 组件 | 数据 | 档位 | 头文件 |
| --- | --- | --- | --- |
| 静态按钮 | `InkingButtonData` | 静态 | `button/InkingStaticButton.h` |
| 动态按钮 | `InkingButtonData` | 动态 | `button/InkingDynamicButton.h` |
| 面板（抽屉 / 弹窗 / 滚动容器） | `InkingPanelData` | 静态 | `panel/InkingStaticPanel.h` |
| 动态面板（几何可变 / 可滚动） | `InkingPanelData` | 动态 | `panel/InkingDynamicPanel.h` |
| 标签 | `InkingLabelData` | 静态 | `text/InkingStaticLabel.h` |
| 动态标签 | `InkingLabelData` | 动态 | `text/InkingDynamicLabel.h` |
| 输入框 | `InkingTextInputData : InkingLabelData` | 静态 | `text/InkingTextInput.h` |
| 图片 | `InkingImageData : InkingButtonData` | 静态 | `image/InkingStaticImage.h` |
| 动态图片 | `InkingImageData : InkingButtonData` | 动态 | `image/InkingDynamicImage.h` |
| 帧动画 | `InkingSpriteData : InkingImageData` | 静态 | `image/InkingSprite.h` |
| 动态精灵 | `InkingSpriteData : InkingImageData` | 动态 | `image/InkingDynamicSprite.h` |
| 画布 | `InkingCanvasData` | 静态 | `canvas/InkingStaticCanvas.h` |
| 视频 | `InkingVideoData : InkingButtonData` | 静态 | `video/InkingVideo.h` |
| 音频（不是组件，是全局函数族） | — | — | `audio/InkingAudio.h` |

**动态档不再只有按钮**：判据只有一条——**几何会不会在构造之后自己变**。按钮要它
（按下撑开、内容撑高），图片也要（实体层每帧走位），精灵也要（每帧走位 + 换动作时改
尺寸），标签也要（运行期按内容改尺寸 / 改占位），面板也要（改占位 + 滚动）；
`TaskGuide-G.md` 把这条判据逐个落到 Image / Sprite / Label / Panel 上，四类**都已补齐**。
没列进动态档的组件（画布 / 视频）仍是构造即定型：改内容只标重绘脏，几何一个字节都不动。

## 一、按钮族

### 分四层，各管一件事

```
InkingButtonData            数据：三态外观 / 每态尺寸 / 文字 / 形状 / 描边阴影
InkingButtonLook            状态机：三态 + 过渡插值（纯值类型，可单测）
InkingButtonLike      契约：clicked / stateChanged 事件 + 三态 + [√] 钩子（带实现，不是纯接口）
inking_detail::ButtonPaint   绘制：形状 / 贴图 / 描边阴影 / 文字
      ↓
InkingStaticButton    接线：形状、文字、命中、登记（输入由场景推）
InkingDynamicButton   接线：同上（输入由场景在指针变化时通知，它自己读状态）
```

**为什么不抽成纯接口**：`InkingButtonLike` 必须**带状态**（`InkingButtonLook` 那十几个字段、
过渡的计时与起点、`clicked` 槽）。纯接口之后每个使用者都要把状态机与二十来行转发
再写一遍——正是这个文件要消除的东西。

**为什么它不继承 `InkingPointerHandler`**：`InkingWidget` 已经继承了它，再继承一次就是
**菱形**（两个子对象，`PushPointerHover` 直接歧义，而且未必编译报错）。所以它只提供
`MouseHover` / `MousePress` / `MouseRelease` / `TriggerClick` 四个入口，组件在自己的
`onPointer*` 里转发一行。

### InkingButtonData

```C++
struct InkingButtonData {
    std::string name;              // 与 json 文件名、代码里的名字三处一致
    int width = 0, height = 0;     // 设计坐标

    InkingButtonAppearance normal{}, hover{}, onclicked{};
    bool hoverInheritsNormal = true;      // 省略 hover 就继承 normal
    bool onclickedInheritsNormal = true;

    InkingButtonTextData text{};
    std::string label;             // text.content.normal 的运行时形态
    std::uint32_t textColor = 0xFFFFFFFFu;
    const char* textColorToken = nullptr;   // 文字色的主题 token（换肤用）；nullptr = 纯颜色

    InkingAnchorPoint selfAnchor{}, traceAnchor{};
    float offsetX = 0.0f, offsetY = 0.0f;

    InkingLayoutSpec layout;             // 我是容器时怎么排孩子
    InkingLayoutItem layoutItem;         // 我在父容器里的意愿

    int zIndex = 0;
    bool visible = true;

    InkingShapeSpec shape = InkingShapeSpec::Rect();  // 形状层
    InkingStrokeSpec stroke;             // 描边（与形状平级的两笔装饰）
    InkingShadowSpec shadow;

    void Normalize();              // 补齐"省略即继承"，幂等
    bool HasLabel() const noexcept;
};
```

>**InkingButtonAppearance**

```C++
struct InkingButtonAppearance {
    InkingAppearanceType type = InkingAppearanceType::Color;  // Color / Image / Svg
    std::uint32_t color = 0xFF000000u;
    std::string source;                 // 原样的配置串
    const std::uint8_t* imageBytes = nullptr;  // 借用：生成代码里的静态数组
    std::size_t imageSize = 0;
    InkingImageFit imageFit = InkingImageFit::Fill;
    InkingShapeBounds srcRect{};              // 只画纹理的哪一块（全零 = 整张，图集切格用）
    float transitionSeconds = 0.0f;     // 进入这个状态的过渡时长
    InkingEasing easing = InkingEasing::Linear;
    InkingTransformSpec transform{};          // 这个状态的目标变换（不改几何、不改命中表）
    float width = 0.0f, height = 0.0f;  // 这个状态的尺寸（0 = 用 InkingButtonData 的）

    static InkingButtonAppearance InkingFromUnitRgba(float r, float g, float b, float a = 1.0f);
    bool IsColor() const noexcept;
};
```

四条约定：

1. **过渡只插值颜色 / 透明度 / 变换 / 尺寸，绝不碰锚点与偏移**。前三样不标命中脏，
   所以静态按钮能一直待在命中表那一档。
2. **每态尺寸 `width` / `height` 是从左上角生长的**（锚点与偏移不动）。
   要"以中心变大"用 `transform.scale`。
3. **每态尺寸不进任何写入口**：它来自构造期烘好的有限集合（三态各一份），由状态机切换。
   命中表登记的是**三态包络的并集**，任何中间尺寸都落在里面。
4. `InkingAppearanceType::Image` / `Svg` 的**数据面已留、纹理路已接**（`imageBytes` +
   `imageFit`），但生成器那边还在落地中。
5. **`srcRect` 只决定"取哪块"，不决定"画多大"**（0.6.1）：单位是**纹理像素**，
   全零 = 整张（既有图片的行为一个字节都不变）；画多大仍是 `width`/`height` +
   `imageFit` 的事，而且等比的基准是**这块的宽高**，不是整张纹理。
   图集就是"同一张纹理 + 换这块矩形"——换帧零解码、零新建纹理。

>**InkingButtonTextData**

```C++
struct InkingButtonTextData {
    float fontSize = 0.0f;
    float leftSpace = 0.0f;   // 距左侧（align == Left 时用）
    float topSpace = 0.0f;    // 距顶部（alignV == Top 时用）
    std::string path;         // 空 = 机器默认字体
    InkingTextAlignH align = InkingTextAlignH::Left;
    InkingTextAlignV alignV = InkingTextAlignV::Top;
};
```

`Center` / `Right` / `Middle` / `Bottom` 会**先量文字块的真实尺寸**再算偏移——
"菜单项文字居中"这种最基本的需求不该靠手填估算值（比例字体下必然歪）。

### InkingStaticButton

```C++
InkingStaticButton(InkingWidget* parent, const InkingButtonData& data);   // 手写数据
InkingStaticButton(InkingWidget* parent, const std::string& name);  // 按名字从 InkingButtonLibrary 取

static const InkingButtonData* FindData(const std::string& name);
static int UnknownNameCount() noexcept;    // 取不到配置的次数（自检断言用）

// 下面这些都从 InkingButtonLike 继承得来，本类一个都不转发：
//   clicked / contextClicked / stateChanged
//   GetState / GetAppearance / GetDisplayColor / GetDisplayTransform
//   GetDisplayWidth / GetDisplayHeight / MouseHover / MousePress / MouseRelease
//   TriggerClick / TriggerContextClick / SetOnClicked / SetOnContextClicked
//   HasOnClicked / HasOnContextClicked / IsTransitioning

bool HitTest(float worldX, float worldY) const override;
const InkingShapeSpec& GetShape() const noexcept;
InkingShapeBounds GetHitEnvelope() const override;
int GetWidth() const noexcept override;      // 当前显示尺寸（三态插值后）
int GetHeight() const noexcept override;
bool HasLayoutAnimation() const override;
InkingTransformSpec GetSubtreeTransform() const override;
const InkingButtonData& GetData() const noexcept;
const std::string& GetText() const noexcept;
void SetText(const std::string& text);
void SetTextColor(std::uint32_t color);
```

**为什么是静态档**：三态只换颜色（颜色不标脏）；按下要"下沉"就是绘制偏移
（`onRender` 里把 y 挪几像素），不是几何变化。真要"按下改尺寸、撑开布局"就用动态版。
**不要在这里加几何写入口**——`src/inking.cpp` 有编译期断言钉着。

### InkingDynamicButton

```C++
InkingDynamicButton(InkingWidget* parent, const InkingButtonData& data);
InkingDynamicButton(InkingWidget* parent, const std::string& name);

bool JudgePointer() noexcept;      // 自己从场景拉指针状态并推三态
bool IsPointerOver() const noexcept;
bool IsPressCaptured() const noexcept;
```

输入方式与静态版**根本不同**：

| | 静态 | 动态 |
| --- | --- | --- |
| 谁判命中 | 场景（查表 + 线性扫） | **自己**（`JudgePointer` 读场景指针状态） |
| 几何遮挡 | 表 + `IsAbove` | **同源**（`QueryHit` 把动态答案与静态答案按 `IsAbove` 合并） |
| 输入投递 | 场景推**结果** | 场景在指针变化时推一次**通知**，它自己拉状态 |
| 采样时机 | 指针变化的那一帧 | **同一帧**（0.4.1 T-02，两档不再差一个逻辑步） |

代价要说清楚：动态组件若在通知里改了几何，本帧命中仍按变化前的列表算——至多一帧错位。

### InkingButtonLike（继承来的公开面）

```C++
class InkingButtonLike {
public:
    virtual ~InkingButtonLike() = default;

    InkingButtonState GetState() const noexcept;                     // Normal / Hover / Pressed
    const InkingButtonAppearance& GetAppearance() const noexcept;
    const InkingButtonAppearance& GetAppearance(InkingButtonState) const noexcept;
    std::uint32_t GetDisplayColor() const noexcept;
    const InkingTransformSpec& GetDisplayTransform() const noexcept;
    int GetDisplayWidth() const noexcept;                      // 注意不叫 GetWidth
    int GetDisplayHeight() const noexcept;
    bool IsTransitioning() const noexcept;
    float GetTransitionProgress() const noexcept;
    bool HasButtonLayoutAnimation() const noexcept;

    bool HasOnClicked() const noexcept;
    void SetOnClicked(InkingButtonClickCallback callback);           // std::function<void()>
    bool TriggerClick();

    // 次键（右键 / 触摸长按）：**带按下点**，典型用法是 OpenAt(x, y)
    bool HasOnContextClicked() const noexcept;
    void SetOnContextClicked(InkingButtonContextClickCallback callback);  // void(float, float)
    bool TriggerContextClick(float designX, float designY);

    bool MouseHover(bool hover) noexcept;
    bool MousePress(bool press) noexcept;
    bool MouseRelease() noexcept;
    InkingButtonLook::AdvanceResult AdvanceButtonLook(float deltaSeconds) noexcept;

protected:
    void ResetButtonLook(const InkingButtonData& data) noexcept;
    virtual void onButtonStateChanged(InkingButtonState from, InkingButtonState to);
    virtual void onButtonDisplayAdvanced(bool repaintDirty, bool hitDirty);
};
```

**两个点击事件，语义不同**（0.5.0）：

| 事件 | 参数 | 谁触发 | 语义 |
| --- | --- | --- | --- |
| `clicked` | 无 | 左键 / 触摸轻点 | "这个按钮被激活了"——位置没有意义 |
| `contextClicked` | `(float x, float y)` | 右键 / 触摸长按 | "在**这一点**打开上下文"——位置就是语义 |

坐标是**按下点**（不是抬起点）。这个不对称是故意的：给 `clicked` 加参数会波及所有
既有订阅者，而它们一个都用不上那两个 float。触摸屏上长按按钮会走到
`contextClicked`——按钮**不需要知道**那是手指（见 `InkingPointerHandler` 的
"两种输入状态"）。

**尺寸观测口叫 `GetDisplayWidth/Height`**：`GetWidth/GetHeight` 已经是"锚点推导的尺寸"，
再叫同一个名字会造成歧义。

**本类一个字都不标脏**：它只回答"状态变了没有""显示值变了没有"，然后调钩子**报告**，
由组件决定标哪个脏——因为"标脏是写入口的活"，而写入口在组件那边。

> 想给别的组件（例如图片）加三态：继承 `InkingButtonLike` 就够了，
> **不要**去继承 `InkingStaticButton`（那会把形状、文字、命中、登记全带上）。

### InkingButtonLibrary

```C++
class InkingButtonLibrary {
public:
    enum class RegisterResult { Ok, NameTaken, Invalid };  // Invalid = 名字为空
    static RegisterResult Register(const InkingButtonData& data);
    static const InkingButtonData* Find(const std::string& name);
    static bool IsNameTaken(const std::string& name);
    static std::vector<std::string> GetAllNames();
    static std::size_t Count();

    static void BeginRegistrationBatch();   // 批量注册（生成代码用）
    static void EndRegistrationBatch();
    static void ClearForTest();
};
```

生成器产出的注册代码在静态初始化期调 `Register`。同名以**先到先得**为准，
后来的 `Register` 返回 `NameTaken`。
`Find` 返回的指针由库持有、**内容只读**，可以长期保存——不要试图改它。

> **"配置写出来、类由生成器烘出来"那条路**（`cxxcss::OptionItem` 这类具名子类、
> `Data()` / `Register()` / `RegisterAllButtons()`、语言表安装）见
> [InkingCxxcss.md](InkingCxxcss.md)。生成类只是把 `Data()` 递上去的薄壳，
> 行为全在本页这些基类里。

## 二、面板：整块在动

`InkingStaticPanel` 是"抽屉 / 下拉 / 弹窗"这一档，也是"容器该怎么写"的样板。
它示范三件事：

1. **变换作用于整棵子树**：只重写 `GetSubtreeTransform()` 就够了，子节点一个字都不用改。
2. **命中不进表，由容器递归解答**：`OwnsSubtreeHit()` 返回 true 之后，子孙不再进全局
   命中表（它们的绝对包络会随容器变换过期），改由 `HitTest()` 递归问子树。
   **命中表的条目数一条都不变**——抽屉来回拉也不会重烘表。
3. **容器不解释指针**：开合是内容状态，由业务调 `SetOpen` / `Toggle` 决定。

```C++
struct InkingPanelAppearance {
    std::uint32_t color = 0xFF000000u;
    float transitionSeconds = 0.0f;
    InkingEasing easing = InkingEasing::Linear;
    InkingTransformSpec transform{};   // "抽屉拉出来多少"的唯一表达
    static InkingPanelAppearance InkingFromUnitRgba(r, g, b, a = 1.0f);
};

struct InkingPanelData {
    std::string name;
    int width = 0, height = 0;
    InkingPanelAppearance closed{}, open{};
    bool openInheritsClosed = true;
    InkingShapeSpec shape = InkingShapeSpec::Rect();
    InkingAnchorPoint selfAnchor{}, traceAnchor{};
    float offsetX = 0.0f, offsetY = 0.0f;
    InkingLayoutSpec layout;
    InkingLayoutItem layoutItem;
    int zIndex = 0;
    bool visible = true;
    bool startOpen = false;      // 构造时就是开着的（不先起一段过渡）
    bool clipChildren = false;   // 剪子孙（默认关：面板常装故意溢出的阴影）
    InkingScrollSpec scroll;           // 配了就能滚（B12），没配就是普通容器
    void Normalize();
};
```

主要 API：

```C++
bool SetOpen(bool open) noexcept;     // 幂等：状态没变返回 false
bool Toggle() noexcept;
bool Open() noexcept;  bool Close() noexcept;
bool IsOpen() const noexcept;
InkingPanelState GetState() const noexcept;
std::uint32_t GetDisplayColor() const noexcept;
const InkingTransformSpec& GetDisplayTransform() const noexcept;
bool IsTransitioning() const noexcept;
float TransitionProgress() const noexcept;

// 滚动（B12）
void UpdateScrollRange();
void SetScroll(float x, float y);
void WheelScroll(float deltaX, float deltaY);
InkingScrollBarMetrics GetBarMetrics(bool vertical) const noexcept;
void PressScrollAt(float designX, float designY);
void DragScrollTo(float designX, float designY);
int DraggingAxis() const noexcept;
float GetScrollX() / GetScrollY() const noexcept;
float GetScrollRangeX() / GetScrollRangeY() const noexcept;
float GetContentWidth() / GetContentHeight() const noexcept;
```

滚动的**几何规则**全在 `dataStruct/InkingScroll.h` 的纯函数里（`InkingScrollRange` /
`InkingClampScroll` / 轨道 / 滑块 / 点轨道跳过去），面板只负责把指针事件接上去。

> **滚动容器必须重写 `HandlesSubtreeWheel()`**（面板已重写）：否则指针停在容器里的子按钮上
> 时，滚轮派给了按钮、容器毫无反应。`MenuBar` 同理。

### 动态面板 `InkingDynamicPanel`（G-07）

```C++
class InkingDynamicPanel : public InkingDynamicWidget {
public:
    InkingDynamicPanel(InkingWidget* parent, const InkingPanelData& data);

    // 继承来的四个几何写入口：Resize / ChangeSelfAnchor / ChangeTraceAnchor / ChangeOffset

    bool SetOpen(bool open) / Toggle() / IsOpen() / GetState();
    bool IsTransitioning() const noexcept;
    float TransitionProgress() const noexcept;

    const InkingShapeSpec& GetShape() const noexcept;
    const InkingPanelAppearance& GetAppearance() const noexcept;   // **目标态**那份
    std::uint32_t GetDisplayColor() const noexcept;          // 插值中的显示值
    const InkingTransformSpec& GetDisplayTransform() const noexcept;
    const InkingStrokeSpec& GetDisplayStroke() const noexcept;
    const InkingShadowSpec& GetDisplayShadow() const noexcept;

    void UpdateScrollRange();
    void SetScroll(float x, float y);
    InkingScrollBarMetrics GetBarMetrics(bool vertical) const noexcept;
    void PressScrollAt(float designX, float designY);   // 世界（设计）坐标
    void DragScrollTo(float designX, float designY);
    int DraggingAxis() const noexcept;
    float GetScrollX() / GetScrollY() const noexcept;
    float GetScrollRangeX() / GetScrollRangeY() const noexcept;
    float ContentViewportWidth() / ContentViewportHeight() const noexcept;

    std::uint64_t GetHitEnvelopeCallCount() const noexcept;   // 包络成本观测口
    void ResetHitEnvelopeCallCount() noexcept;
};
```

两档**共用**的是 `InkingPanelLook`（两态外观 / 过渡）与 `src/panel/PanelPaint.cpp` 的三笔绘制
（填充 / 阴影 / 描边）+ `PanelHitEnvelope`；**各自实现**的是滚动、裁剪、容器命中。
本类**不继承** `InkingStaticPanel`（那条链上没有几何写入口）。

四条要知道的：

1. **滚动条拖动从 `onPointerStateChanged()` 自判**：动态档收不到 `PushPointer*`，这是唯一入口。
   `PointerProbe` 拿 `pressed` / `released` / 仍按着三态，等价静态版的 `onPointerPress` + `onPointerDrag`。
2. **不做滚轮**：滚轮按渲染帧累加 / 清零，而动态组件只能在逻辑步读（一个渲染帧可能跑 0 次或 2 次
   ⇒ 丢了 / 滚双倍）。这是**框架**少一条"每帧一次且必达"的通知，要修得单独立项；
   `HandlesSubtreeWheel()` 对动态面板也就没有意义。
3. **`SetScroll` 只标重绘脏**：本类的包络按滚动范围扩过（洞与滚动量无关），所以滚动**不该**
   触发包络重算——谁把那扩范围的代码删了，症状是"滚过去点不动"且不报错。
4. **`HitTestNode()` 不用重写**：基类那份已经是"子树优先"的容器语义（静态版抄的那一份是
   0.4.0 之前基类还没有它时留下的）。

## 三、标签

```C++
struct InkingLabelData {
    std::string name;
    int width = 0, height = 0;      // 参与布局时会被布局改写
    InkingAnchorPoint selfAnchor{}, traceAnchor{};
    float offsetX = 0.0f, offsetY = 0.0f;
    int zIndex = 0;
    bool visible = true;
    InkingLayoutSpec layout{};
    InkingLayoutItem layoutItem{};

    std::string text;                    // UTF-8
    InkingTextStyle style{};                   // 字号 / 行距 / 字体 / 对齐 / 内缩
    std::uint32_t color = 0xFFFFFFFFu;
    InkingTextWrap wrap = InkingTextWrap::Word;
    InkingTextOverflow overflow = InkingTextOverflow::Clip;
    int maxLines = 0;                    // 0 = 不限
    bool Empty() const noexcept;
};
```

```C++
class InkingStaticLabel : public InkingStaticWidget, public InkingLabelLike {
public:
    InkingStaticLabel(InkingWidget* parent, const InkingLabelData& data);
    InkingStaticLabel(InkingWidget* parent, const std::string& name);

    const InkingLabelData& GetData() const noexcept;
    bool SetText(std::string text) noexcept;        // 变了才 true
    bool SetTextColor(std::uint32_t color) noexcept;
    const InkingTextLayoutResult& GetTextLayout() const;  // 最近一次排版结果

    InkingLayoutSize MeasureLayout(const InkingLayoutConstraints&) const override;  // 按文字撑高
    bool AcceptsExternalTransform() const noexcept override { return true; }
};
```

`SetText` 会调 `RelayoutParent()`：文字变长 → 自己变高 → **兄弟要让位**，
那是父容器的活儿。框架不做隐式重排，所以这一步必须由组件自己喊。

### 动态标签 `InkingDynamicLabel`（G-05）

```C++
class InkingDynamicLabel : public InkingDynamicWidget, public InkingLabelLike {
public:
    InkingDynamicLabel(InkingWidget* parent, const InkingLabelData& data);

    // 继承来的四个几何写入口：Resize / ChangeSelfAnchor / ChangeTraceAnchor / ChangeOffset

    const InkingLabelData& GetData() const noexcept;
    bool SetText(std::string text) noexcept;        // 只换内容，**不改几何**
    bool SetTextColor(std::uint32_t color) noexcept;
    const InkingTextLayoutResult& GetTextLayout() const;

    InkingLayoutSize MeasureLayout(const InkingLayoutConstraints&) const override;  // 按文字撑高
    bool FitToContent();                            // ← 唯一会改几何的入口
    bool IsManagedByLayout() const noexcept;        // 我被布局管着吗（FitToContent 的失败原因）

    bool AcceptsExternalTransform() const noexcept override { return true; }
    InkingShapeBounds GetHitEnvelope() const override;
};
```

两档的差别只有**几何**与**改尺寸的时机**，排版、取色、`MeasureLayout`、绘制那一笔
（`inking_detail::PaintText`）与静态版逐字相同：

1. **`SetText` 不自动撑高**。静态版靠 `RelayoutParent()` + 自测量隐式撑高；动态版把这件
   事故意做成显式——要跟着内容改尺寸就调 `FitToContent()`。忘了调，多出来的行会被
   `InkingTextOverflow::Clip` 裁掉（那是既定语义，不是新错）。
2. **`FitToContent()` 会连宽度一起收到内容宽**。配置里的 `width` 只当第一趟的换行基准；
   要保住那个框宽，就改成读 `MeasureLayout().height` 自己 `Resize(width, h)`。
3. **在带 `InkingLayoutSpec` 的容器里 `FitToContent()` 不生效**：`ApplyLayoutRect` 下一次
   `Relayout()` 就会把尺寸整体重写，写了等于没写。所以它**返回 false 并记一条 WARN**，
   不写一个下一帧就消失的尺寸（`IsManagedByLayout()` 可以提前问）。
4. 标签仍**不吃点击**（`PassThrough`，`HitTest` 恒 false），"A13 保留"与其余动态档同一条。

> **两档的排版在同一处**（G-06，纯重构）：`InkingLabelLike`（`text/InkingLabelLike.h`）承载 `InkingLabelData`、
> 排版缓存（`LayoutCache`）、`LayoutForWidth` / `CacheMatches` / `EffectiveTextColor`、
> `MeasureLabel`、`paintLabelText`（画文字那一笔）与观测口 `GetTextLayout`。
> 它**一个虚函数都没有**（连析构都是受保护非虚）——给次基类一张 vtable 会让两档的析构
> 多出 thunk；于是"画字要的水平滚动量"走**参数**（`paintLabelText(..., scrollX)`），
> 由组件的 `onRender` 把自己那份 `TextScrollX()` 传进去（输入框覆盖它，标签恒 0）。
> `SetText` / `SetTextColor` 只把**数据手术**（`assignText` / `assignTextColor`）放进共用层，
> "之后要不要重排 / 标脏"仍是两档各自的一行——静态档要 `RelayoutParent()`，动态档只标重绘脏。
> 逐行对照记录在 `TaskGuide-G.md` G-06。

### 多语言：InkingTextTable

```C++
struct InkingTextEntry { std::string key; /* 各语言的串 */ };
struct InkingTextTable {
    /* languages + entries */
    int LanguageIndex(const std::string& language) const noexcept;
    const InkingTextEntry* Find(const std::string& key) const noexcept;
    bool Empty() const noexcept;
};

bool InkingInstallTextTable(InkingTextTable table, const std::string& language = {});
bool InkingSetTextLanguage(const std::string& language);
const std::string& InkingCurrentTextLanguage() noexcept;
std::size_t InkingCurrentTextLanguageIndex() noexcept;
std::uint64_t InkingTextLanguageVersion() noexcept;   // 变了 +1，用来判"要不要重查"
std::string InkingResolveText(const std::string& key);
InkingTextTableStats InkingTextTableStatsSnapshot() noexcept;   // { hits, misses, version }
void InkingClearTextTable() noexcept;
```

语言切换 = 换一串 `text` ⇒ 文字纹理缓存自然未命中（键里有文本）⇒ 重烘一次。

## 四、输入框（单行）

```C++
struct InkingTextInputData : public InkingLabelData {
    std::uint32_t caretColor = 0xFFEAF2FFu;
    std::uint32_t selectionColor = 0x663E7BD8u;
    float caretBlinkSeconds = 0.6f;   // 0 = 不闪
    float caretWidth = 1.5f;
};
```

```C++
class InkingTextInput : public InkingStaticLabel {
public:
    InkingTextInput(InkingWidget* parent, const InkingTextInputData& data);

    bool WantsKeyboardFocus() const noexcept override { return true; }  // ← 关键
    bool HitTest(float worldX, float worldY) const override;

    const std::string& GetEditText() const noexcept;
    void SetEditText(const std::string& text);
    InkingTextEditState& Edit() noexcept;

    bool IsFocused() const noexcept;
    void SetFocused(bool focused);

    bool HandleKey(const InkingKeyEvent& key);       // 手动喂按键（自检用）
    void HandleText(const std::string& text);  // 手动喂文本（IME 结果）
    void PressAt(float designX, float designY);
    void DragTo(float designX, float designY);

    void TickCaretBlink(float deltaSeconds);
    InkingTextInputRect CaretBox() const;
    std::vector<InkingTextInputRect> SelectionBoxes() const;
    std::size_t OffsetAtPoint(float designX, float designY) const;

    int TextInputCount() / SubmitCount() / CopyCount() / PasteCount() const noexcept;
    bool IsCaretVisible() const noexcept;

    using ClipboardGet = std::function<std::string()>;
    using ClipboardSet = std::function<void(const std::string&)>;
    static void SetClipboardHandlers(ClipboardGet get, ClipboardSet set);
    static void ResetClipboardHandlers();
};
```

三条约定：

1. **编辑状态是独立的一份**（`InkingTextEditState`，纯逻辑可单测）；每次改动之后 `syncToLabel()`
   交给基类 ⇒ 重排 + 重绘。
2. **吃点击**（构造里显式 `SetHitPassThrough(false)`）：输入框要点击定位光标，
   而标签默认是"穿透"的。**别指望默认值。**
3. **快捷键认 `ctrl`**（A / C / X / V），**IME 提交走 `onTextInput`**
   （`SDL_EVENT_TEXT_INPUT`，输入法组合好的结果从这里进来）。

**这一版是单行**：`wrap` 会被强制成 `NoWrap`，粘贴时换行被丢掉。
**没做**：多行、撤销 / 重做、Tab 焦点遍历、候选窗位置。

## 五、图片

```C++
struct InkingImageData : public InkingButtonData {
    // 多出来的只有"图从哪来"：继承 InkingButtonData 是为了形状 / 描边 / 锚点 / 命中同构
};
```

```C++
class InkingStaticImage : public InkingStaticWidget, public InkingButtonLike {
public:
    InkingStaticImage(InkingWidget* parent, const InkingImageData& data);

    bool HitTest(float worldX, float worldY) const override;
    const InkingShapeSpec& GetShape() const noexcept;
    const std::string& ImageName() const noexcept;
    std::size_t AssetByteCount() const noexcept;
    bool UsingAssetBytes() const noexcept;
    int AssetLoadCount() const noexcept;
    bool LoadAsset(const std::string& relative);

    // 拖拽 / 缩放（交互式画布那一档）
    void SetDraggable(bool draggable) noexcept;
    void SetResizable(bool resizable) noexcept;
    bool IsDraggable() / IsResizable() / IsDragging() / IsScalingDrag() const noexcept;

    bool AcceptsExternalTransform() const noexcept override { return true; }

protected:
    // 只画纹理的哪一块（图集切格，0.6.1 T-04）。全零 = 整张；
    // 只标重绘脏、不标命中脏（画的东西变了、几何没变）
    void SetRenderSourceRect(const InkingShapeBounds& srcRect);
    const InkingShapeBounds& GetRenderSourceRect() const noexcept;
};
```

**它继承 `InkingButtonLike`，但一个 `onPointer*` 都不重写**（除拖拽那一路）——
于是没人喂它、三态永远停在 Normal，**正好就是它要的**。所以不必为"可点 / 不可点"
拆成两个基类：差别只是"重不重写那几个钩子"。

拖拽走的是**运行期显示变换**（`SetDisplayTransform`）＋ `Resize` 的仿射表示，
不会撑开兄弟（不改变占位）。

### 动态图片 `InkingDynamicImage`（G-01）

```C++
class InkingDynamicImage : public InkingDynamicWidget {
public:
    InkingDynamicImage(InkingWidget* parent, const InkingImageData& data);

    // 继承来的四个几何写入口：Resize / ChangeSelfAnchor / ChangeTraceAnchor / ChangeOffset

    void SetDraggable(bool on) noexcept;   // 拖 = ChangeOffset（写**几何**）
    void SetResizable(bool on) noexcept;   // 缩 = Resize，绕左上角，夹 [0.2, 5]
    bool IsDragging() / IsScalingDrag() / IsDraggable() / IsResizable() const noexcept;

    // 与静态版同名同义的观测口
    bool HitTest(float worldX, float worldY) const override;
    const InkingShapeSpec& GetShape() const noexcept;
    const InkingButtonAppearance& GetAppearance() const noexcept;
    std::uint32_t GetDisplayColor() const noexcept;
    InkingTransformSpec GetDisplayTransform() const noexcept;
    InkingTransformSpec GetSubtreeTransform() const noexcept override;
    InkingShapeBounds GetHitEnvelope() const override;
    bool AcceptsExternalTransform() const noexcept override { return true; }
    const InkingImageData& GetData() const noexcept;
    const std::string& ImageName() const noexcept;
    std::size_t AssetByteCount() const noexcept;
    bool UsingAssetBytes() const noexcept;
    int AssetLoadCount() const noexcept;
    bool LoadAsset(const std::string& relative);
    void SetImageBytes(const std::uint8_t* data, std::size_t size,
                       const std::string& name);
};
```

**和静态版只有两条差别**：几何能改、输入靠拉。绘制（`PaintButtonAppearance`）、
图片加载、命中形状（`HitTestShape`）逐字同源——抄第二份就会出现"按钮和图片画得
不一样"。

**内置拖动 / 缩放写的是几何，不是 A13**（这是最容易写反的一条）：

| | 静态版 | 动态版 |
| --- | --- | --- |
| 拖动落在哪 | 外部显示变换（A13） | `ChangeOffset`（几何，占位跟着走） |
| 缩放落在哪 | A13 的 scale + 平移补偿 | `Resize`（夹 `[0.2, 5]`，绕左上角） |
| 外部变换还能用吗 | 能（它就是那条通道） | **能**（`AcceptsExternalTransform()` 恒 true） |

A13 与几何是**正交**的两件事（屏幕抖动 / 整体缩放 vs 占位变化），在绘制与命中处
天然复合——`GetDisplayTransform()` 就是"外部变换 ∘ 配置那份"，`HitTest` 读的是同一个
结果。风险从来不在"两者并存"，而在"同一个拖动功能同时写两边"。

**代价**：`GetOffset()`（几何）与 `GetDisplayTransform()`（显示）回答的是**不同的问题**，
调用方别混用。

**命中表**：不进表，占地记成"洞"（与 `InkingFixedPopup` 同档）。它是叶子，所以移动时
走 `T-19` 的快路——**只刷洞、不重建整张表**（与静态版靠 A13 拖动要每帧全量重建不同）。

### 帧动画的数据面（0.6.0 T-02）

```C++
struct InkingSpriteData : public InkingImageData {
    struct SpriteSheet {
        std::string source;                  // 空 = 沿用 normal.source
        int frameWidth, frameHeight, columns, rows;
        int count = 0;                       // 0 = columns × rows
        int firstFrame = 0;                  // 起始**格号**（0.6.4）：每行一个方向时用
        int margin = 0, spacing = 0;
        std::vector<InkingShapeBounds> frameRects; // 帧表（0.6.5）：非空时优先，与网格互斥
        bool IsConfigured() const noexcept;
        int  ResolveCount() const noexcept;   // count 缺省 = columns × rows
        int  SheetWidth() / SheetHeight() const noexcept;   // 整张图应该是多大
        InkingShapeBounds CellRect(std::size_t i) const noexcept; // 第 i 格的像素矩形
    };

    struct SpriteEvent { std::size_t frame; std::string name; };  // 帧事件（0.6.3）

    std::vector<InkingButtonAppearance> frames;    // 序列帧（每项是一张图）
    SpriteSheet sheet;                       // 图集（frames 为空时才看它）
    std::vector<SpriteEvent> events;         // 播到第 N 帧发一个名字
    double fps = 12.0;
    bool loop = true;
    bool autoplay = true;
    std::size_t startFrame = 0;
    double startOffsetSeconds = 0.0;

    bool UsesSheet() const noexcept;
    std::size_t FrameCount() const noexcept;
    void Normalize();                        // 补默认 + 夹取，幂等
    std::string Validate() const;            // 空串 = 通过，否则是错误说明
};
```

**两种素材二选一**（不是"一份配置同时用两种"：`frames` 与 `sheet` 都填直接报错，
不替你猜哪个优先；都不填也报错）。

| 判断 | 落在哪 | 行为 |
| --- | --- | --- |
| 能修正的（`startFrame` 越界、`count` 缺省） | `Normalize()` | 就地修 + 一条 WARN（画面还能看见，不是空白） |
| 不能猜的（都填 / 都不填、`fps <= 0`、图集几何自相矛盾） | `Validate() const` | 只报告，不改数据 |

分工的判据是"这个字段能不能被安全地改成某个值"——能就是 `Normalize`，不能就是
`Validate`，两者不重叠。`FrameCount()` 是**唯一**算帧数的地方（组件只认这一个数）。

图集**构造期**还要验一遍"切出来的尺寸与图宽是否相符"——判断是
`SheetWidth()/SheetHeight()`（`frameWidth × columns + spacing × (columns − 1) + 2 × margin`）
与图片真实尺寸比对。**切格算术只在 `CellRect()` 里做一次**：组件每帧拿它设源矩形，
自检直接断言它（验 margin / spacing 不必真画一张图）。

`firstFrame`（0.6.4）是**起始格号**（行优先、绝对），不是帧数：`"每行一个方向"`的图集用
第 r 行 = `firstFrame: r × columns` + `count: columns`。`CellRect()` 的语义**没变**（仍是
绝对格号）——"帧号 → 格号"的 `+ firstFrame` 只在 `InkingSprite::loadFrameBytes()` 里做一次；
写反了就会**静默播到别的行**，所以 `Validate()` 报错时把格号与帧数两个数都打出来。
`firstFrame` 也**不进** `IsConfigured()`（只写它不算"配了图集"）。

`frameRects`（0.6.5 T-14）是**帧表**：每帧一个绝对像素矩形，宽高可以不等（一页里混装
多个动作时必然如此）。三条约定：与网格字段（`columns` / `rows` / `frameWidth` /
`frameHeight` / `margin` / `spacing` / `firstFrame`）**互斥**（同给直接报错，不静默选一套）；
`CellRect(i)` 直接返回表里那项、**不叠加** `firstFrame`；帧数由表长决定（`count` 被
`Normalize()` 覆盖）。图集模式下构造期还会逐帧验"矩形落在图内"，越界文案带帧号与矩形。

> 数据层看不到图片多大，所以这一步要**量一次图**（`inking_detail::ProbeImageSize`，解一次
> 但不建纹理，按 `(名字, 字节数)` 缓存）。量不到（没接 SDL3_image / 字节坏了 / 是 SVG）
> 时给一条 WARN 并跳过校验，不假装通过、也不拦着播。

### 帧动画组件 `InkingSprite`（0.6.0 T-03）

```C++
class InkingSprite : public InkingStaticImage {
public:
    InkingSprite(InkingWidget* parent, const InkingSpriteData& data);

    InkingEvent<std::size_t, std::size_t> frameChanged;   // (新帧号, 旧帧号)
    InkingEvent<const std::string&, std::size_t> frameEvent;  // (事件名, 帧号)
    InkingEvent<> finished;                               // 非循环播完一次

    void Play();                                  // 播完后再 Play = 从头再来
    void Pause();                                 // 时间冻结（不是"停止换帧"）
    void Stop();                                  // 回起始帧且不播
    bool SeekToFrame(std::size_t frame);          // 越界夹到末帧
    void SetSpriteData(const InkingSpriteData& data, bool keepPhase = false);  // 换配置（0.6.5）


    bool IsPlaying() / IsFinished() const noexcept;
    std::size_t GetFrameIndex() / GetFrameCount() / GetLoadedFrameCount() const noexcept;
    std::uint64_t GetFrameAdvanceCount() / GetSkippedFrameCount() const noexcept;
    double GetAnimationTime() const noexcept;     // 已播时长（暂停期间冻结）
    const InkingSpriteData& GetSpriteData() const noexcept;
    const std::string& GetConfigError() const noexcept;
    bool IsValid() const noexcept;
    bool IsSheetMode() const noexcept;            // 实际走的是图集模式吗
};
```

> **两档的帧机在同一处**（G-04，纯重构）：`InkingSpriteLike`（`image/InkingSpriteLike.h`）承载配置 /
> 帧表 / 时间基准 / 三项计数 / 状态机 / 事件派发，`InkingSprite` 与 `InkingDynamicSprite`
> 都继承它。它**一个虚函数都没有**（连析构都是受保护非虚）——给次基类一张 vtable 会让两档
> 的析构多出 thunk；"要组件替它做的事"（换字节 / 读资源 / 换源矩形 / 三种事件）是**构造期
> 注入的回调**（`InkingSpriteLike::Hooks`），调用时机与原来写在 `applyFrame` 里的那几行一致。
> 实现与逐行对照记录都在 `src/image/InkingSprite.cpp` 与 `TaskGuide-G.md` G-04。

**两种素材共用一套帧号**：`frame = floor(t × fps) % count` 与素材形态无关，只在
"怎么把这一帧搬到画面上"分叉（0.6.1 T-05）：

| 素材 | 换帧做什么 | 代价 |
| --- | --- | --- |
| 序列帧 | 换**字节视图**（`SetImageBytes`，KB 级拷贝） | 每帧一次拷贝 |
| 图集 | 换**源矩形**（`SetRenderSourceRect`） | 零拷贝、零 `SDL_CreateTexture`，纹理只建 1 张 |

分叉点只在 `applyFrame()` 一处（每帧带不带 `sourceRects` 表；0.9.0 T-03 从「一个矩形」
泛化成表，单块档恒一个元素），**不写两套状态机**——
否则"序列帧的暂停"和"图集的暂停"迟早不一致，而这种不一致只表现为"某个模式有点怪"。

**时间从场景时钟来**（`InkingScene::AnimationSeconds`，0.6.0 T-01），帧号是时间的纯函数：
`frame = floor(t × fps) % count`。所以：

| 场景 | 结果 |
| --- | --- |
| 12fps × 1 秒 | 恰好换 12 帧（`GetFrameAdvanceCount() == 12`） |
| 一次画了 0.25s（慢帧） | 帧号直接跨 3 帧，`GetSkippedFrameCount() += 2`——**画面不慢放** |
| 同场景两个同 fps 的 sprite | 帧号恒等（只有一个时间源，不各累加 delta） |
| 场景切走 | 冻结（`TickFrame` 只对活跃场景派发），切回来接着走 |

**暂停是"时间冻结"**：`Pause()` 记下暂停起点，`Play()` 把这段时间补进时间轴——
"暂停 3 秒再恢复"从原帧继续，不是跳过 36 帧。

**字节与纹理是两件事**：

- **字节**在构造期一次备齐（运行期换帧零 I/O）。运行期换帧若走 `LoadAsset` 会每帧读盘，
  那不是能接受的代价；
- **纹理**在第一次画到那一帧时由渲染层解码并进缓存（键 = 帧名）。构造期**烘不出**纹理：
  组件构造时窗口 / 渲染器还不存在，而纹理属于渲染器。所以"预热"的准确含义是
  **字节预热 + 首轮播放建纹理**：第二轮起换帧 `texturesCreated` 增量为 0。

**帧事件**（0.6.3）：`events` 由**帧号变化**驱动，不是"时间到点"。

| 情况 | 结果 |
| --- | --- |
| 正常播放到第 3 帧（挂了事件） | `frameEvent("hit", 3)` 恰好一次 |
| 慢帧一次跨 3 帧，中间有事件 | **仍然触发**（扫 `(旧, 新]` 区间，掉帧不吞事件） |
| 回绕 `3 → 0` | 拆成 `(3, 末帧]` + `[0, 0]` 两段扫，第 0 帧的事件照发 |
| `SeekToFrame` 跳过事件帧 | 不发（"跳过去"不是"播过去"） |
| 非循环播完 | `finished` 一次；`Play()` 后再播完再发一次；循环动画永不发 |

`frameEvent` 在 `frameChanged` **之前**发——订阅者收到"换帧"时，这一帧的关键点已经处理完。
事件名是 `const std::string&`，指向组件自己那份配置（生命周期同组件）。

**帧事件回调里可以换配置**（0.9.0 T-01）：`SetSpriteData` 在派发期间调用**不会立即
生效**——本帧的落帧与派发完整走完，切换在本帧末执行。这是"帧事件驱动过渡"的地基。
三条推论：

| 情况 | 结果 |
| --- | --- |
| 回调里换了配置，本批还有别的事件 | **照发**（派发用快照，不是边遍历边换） |
| 回调里换了配置，这一帧还发 `frameChanged` 吗 | **不发**（旧动画的帧号在"已决定换掉它"之后说一遍就是假事件） |
| 同一帧里切两次 | **后设覆盖先设**，前一次静默丢弃（不排队——"这一帧算一句台词"） |

> 0.9.0 之前这里没有约定：回调里换配置会让正在遍历的事件表悬垂，事件流变成
> "半新半旧"（自检实测：`one@1` 之后本该是 `three@3`，实际发出了新配置的
> `swapTwo@2`，`three@3` 丢了），外加一个报旧帧号的 `frameChanged`。

**配置不合法不静默**：`IsValid()` 为假 + 一条 ERROR（例如图集切格与图宽不符、
`frames` 与 `sheet` 都填、事件帧越界）。
组件照常构造——"少一个动画"不该让整个界面起不来。

**运行期换配置**（`SetSpriteData`，0.6.5 T-13）：节点指针、父子关系、**所有订阅全部保留**
——"待机→跑步"是同一个实体的同一个节点。四条语义：

| 情况 | 结果 |
| --- | --- |
| `keepPhase == false`（默认） | 落到第 0 帧 |
| `keepPhase == true` | 保留换之前的帧号（越界夹到末帧）——同动作换方向时不跳回第一帧 |
| `IsPlaying()` | 保持原值；播完（`finished`）的那份状态被清掉 |
| `frameChanged` | 换配置后的第一次落位**不发**（没有"旧帧"这回事） |
| 组件尺寸 | **不动**（改尺寸属于布局层，要自己 `ApplySelfSize`） |
| 旧纹理 | 不删（键不同就各留一张，键相同就复用——多动画共用图集靠这条） |
| 在帧事件回调里调用 | 延后到本帧末执行（见上一节，0.9.0 T-01） |

换到图集配置时会**同步把图集字节交给绘制层**：只改源矩形不动字节的话，帧数、
`IsSheetMode()` 全对而画面还是上一张动画——最像成功的那种错（自检钉住了它）。

### 动态精灵 `InkingDynamicSprite`（G-03）

```C++
class InkingDynamicSprite : public InkingDynamicWidget {
public:
    InkingDynamicSprite(InkingWidget* parent, const InkingSpriteData& data);

    // 继承来的四个几何写入口：Resize / ChangeSelfAnchor / ChangeTraceAnchor / ChangeOffset

    InkingEvent<std::size_t, std::size_t> frameChanged;      // (新帧号, 旧帧号)
    InkingEvent<const std::string&, std::size_t> frameEvent; // (事件名, 帧号)
    InkingEvent<> finished;                                  // 非循环播完一次

    void Play(); void Pause(); void Stop();
    bool SeekToFrame(std::size_t frame);
    void SetSpriteData(const InkingSpriteData& data, bool keepPhase = false);

    bool IsPlaying() / IsFinished() const noexcept;
    std::size_t GetFrameIndex() / GetFrameCount() / GetLoadedFrameCount() const noexcept;
    std::uint64_t GetFrameAdvanceCount() / GetSkippedFrameCount() const noexcept;
    double GetAnimationTime() const noexcept;
    const InkingSpriteData& GetSpriteData() const noexcept;
    const std::string& GetConfigError() const noexcept;
    bool IsValid() / IsSheetMode() const noexcept;

    // 与静态版同名同义的观测口（一个不少，G-04 抽 InkingSpriteLike 时不用回头补）
    bool HitTest(float worldX, float worldY) const override;
    const InkingShapeSpec& GetShape() const noexcept;
    const InkingButtonAppearance& GetAppearance() const noexcept;
    std::uint32_t GetDisplayColor() const noexcept;
    InkingTransformSpec GetDisplayTransform() const noexcept;
    InkingTransformSpec GetSubtreeTransform() const noexcept override;
    InkingShapeBounds GetHitEnvelope() const override;
    bool AcceptsExternalTransform() const noexcept override { return true; }
    const InkingImageData& GetData() const noexcept;
    bool LoadAsset(const std::string& relative);
    void SetImageBytes(const std::uint8_t*, std::size_t, const std::string& name);
};
```

**帧机与静态版逐字相同**：`frame = floor(t × fps) % count`、`applyFrame` 的单点分叉、
暂停＝时间冻结、帧事件按区间扫、`SetSpriteData` 的六条语义——一条都没改。差别只有
两条：**几何能改**、**输入靠拉**（本类没有内置拖动：走位由实体层调几何入口，
不是用户拽着走）。

**`SetSpriteData` 仍不动尺寸**：配置里带 `width/height`，但"换动作顺手改几何"会让一次
切动作变成一次看不见的重排。动态档多出来的能力是**调用方随时可以调 `Resize`**，
不是"换配置时套用配置尺寸"。

**图集模式下几何与切格是两个坐标系统**：`Resize` 改的是"画多大"，源矩形仍是图集里
那块 40×40 的像素。谁把源矩形跟着几何缩放，画面就是"整张图集的左上角被拉大"，
而 `IsValid()` 依然为真——静默错，自检钉住了它。

**命中表**：不进表，占地记成"洞"；它是叶子，几何入口走 `T-19` 的快路（移动/改尺寸
只刷洞、不重建整表）。

### 图集描述 `InkingAtlasData`（0.6.5 T-16）

"一张图里混装多个动画"的**数据面**：一张图集里有哪些具名动画、每个动画的帧矩形与
播放参数。它**不是加载器**——字节由资源层给（`InkingAssetLoad` 或烘好的数组），这一层只回答
"图里的哪块是哪一帧"。

```C++
struct InkingAtlasAnimation {
    std::string name;
    std::string source;              // 相对资源根；同时是纹理缓存键
    std::vector<InkingShapeBounds> frames; // 绝对像素矩形，宽高可以不等
    double fps = 12.0;
    bool loop = true;
    std::vector<InkingSpriteData::SpriteEvent> events;
};

struct InkingAtlasData {
    std::string name;
    std::vector<InkingAtlasAnimation> animations;
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    std::size_t FindIndex(const std::string& animationName) const noexcept;  // 找不到 = kNotFound
    std::string Validate() const;    // 空串 = 通过（重名 / 空帧 / 缺路径 / 矩形非法）
};

// 纯函数：描述 + 字节视图 → 组件吃的配置。不读文件。
InkingSpriteData InkingMakeSpriteData(const InkingAtlasData& atlas, std::size_t animationIndex,
                          const std::uint8_t* imageBytes, std::size_t imageSize);
```

三条约定：**尺寸默认取第一帧的自然宽高**（动画比第一帧宽时调用方自己改 `width/height`）；
**`source` 只写一处**（`normal.source`，`sheet.source` 留空即沿用）；**帧表直接灌进
`sheet.frameRects`**，所以不会同时给出网格字段。下标越界时返回一份非法配置 + 一条 ERROR
（`kNotFound` 是个巨大的 `size_t`，不打日志就只是一片空白）。

重名动画在 `Validate()` 里报错：`FindIndex()` 只命中第一个，第二条会被**静默忽略**——
"某个动作永远播不出来"比报错难查得多。

### 动画组 `InkingAnimateGroup`（0.6.6 T-18）

一个角色有几个动作、切换其中一个。**一个节点**承载全部动作，`SwitchTo()` 换的是
**配置**不是节点（节点数恒 1）。

```C++
struct InkingAnimateGroupData {
    std::vector<std::pair<std::string, InkingSpriteData>> animations;  // 顺序 = 声明顺序
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);
    std::size_t FindIndex(const std::string& name) const noexcept;
    std::string Validate() const;   // 空表 / 动作名为空 / 重名
};

class InkingAnimateGroup : public InkingSprite {
public:
    InkingAnimateGroup(InkingWidget* parent, const InkingAnimateGroupData& data,
                       const std::string& initialAnimation = {});

    InkingEvent<const std::string&, const std::string&> animationChanged;  // (新, 旧)

    bool SwitchTo(const std::string& name, bool keepPhase = false, bool resize = true);
    const std::string& GetCurrentAnimation() const noexcept;
    std::vector<std::string> GetAnimationNames() const;   // 给 UI 做下拉
    const InkingAnimateGroupData& GetGroupData() const noexcept;
    const std::string& GetGroupError() const noexcept;
};
```

**为什么不是"一个容器装 N 个 sprite"**：后者每个动作有独立相位（最自然），但
绘制列表与命中表里会**始终躺着 N−1 个不可见节点**、每个动作各一份时间基准
（切回来时相位停在"上次离开的地方"）、还与 0.6.2 的合并收益相抵。

| 场景 | 组的行为 |
| - | - |
| `SwitchTo("run")` | 换配置；`animationChanged(新, 旧)` 在换完之后发（回调里读到的是新动作） |
| 切到**当前**动作 | 空操作（返回 true、不重建、不发事件）——UI 反复点同一个按钮不该重置相位 |
| 名字不存在 | 返回 false + 一条 ERROR（不静默停在旧动作，否则"切了没反应"像动画坏了） |
| `keepPhase` | 保留当前帧号（同动作换方向）；默认第 0 帧 |
| `resize`（默认 true） | **组件宽高跟着新动作走**；`false` 交给调用方 |
| 初始名字不存在 | 退回第 0 个 + 一条 ERROR |

> **`resize` 与单组件相反是有意的**：组就是"一个角色"，轮廓随动作变（待机 40 宽、
> 攻击 56 宽）是它的职责；而 `InkingSprite::SetSpriteData()` 换配置时"要不要动布局"
> 只有调用方知道，所以它**不动**尺寸。两处都在文档里写明了，别当成矛盾。

### 块叠加组的数据面 `InkingSpriteGroupData`（0.9.0 T-04）

「**一个 sprite 由多块拼成**」——一张图集、多个动画、**每帧一组格号**。跟上面
`InkingAnimateGroup`（一个角色的多个动作，一帧一块）是两件事，跟 `InkingAtlasData`
（帧是矩形表、宽高可不等）**正好相反**。

```C++
struct InkingSpriteGroupAnimation {
    std::string name;
    std::vector<std::vector<int>> frames;   // 每帧 = 格号列表；顺序 = 叠加顺序
    double fps = 12.0;
    bool loop = true;
    std::vector<InkingSpriteData::SpriteEvent> events;
};

struct InkingSpriteGroupData {
    std::string source;                     // 图集路径（纹理缓存键，必填）
    int frameWidth = 0, frameHeight = 0;    // 一格多大（所有格等大）
    int columns = 0, rows = 0;              // 网格（行优先）
    int margin = 0, spacing = 0;
    std::vector<InkingSpriteGroupAnimation> animations;

    std::size_t FindIndex(const std::string&) const noexcept;   // 找不到给 kNotFound
    int ResolveCount() const noexcept;                          // columns × rows
    InkingShapeBounds CellRect(int cell) const noexcept;        // 越界给全零
    int SheetWidth() const noexcept;
    int SheetHeight() const noexcept;
    void Normalize();
    std::string Validate();
};
```

**等大是结构保证，不是运行期校验**：只给网格、不给矩形表，于是"同一帧里两块大小
不同"根本配不出来。这是刻意的取舍——这个类型代表的"多块拼成一个 sprite"在业务上
就是等格叠图（身体 + 头 + 武器都在同一套网格里）。

三条定死的语义：

| 语义 | 为什么 |
| --- | --- |
| **格号允许重复** | 同一帧同一格出现两次是合法的（故意叠两层做发光）。查重会把这种用法挡掉 |
| **每帧块数允许不同** | 不强制槽位固定；代价是顶点数每帧可变（反正每块各自 `BuildShapeMesh`） |
| **格号越界归 `Validate()`，不夹取** | "改到哪一格"猜不出来 |

`Validate()` 九条，顺序即报告顺序：`source` 空 / 网格字段非法 / 动画表空 /
动作名为空 / 重名 / `fps <= 0` / 动画没有帧（或某帧没有格号）/ 格号越界（报
"哪个动画的第几帧第几块"）/ 事件帧越界。

`Normalize()` 是**幂等的空操作**：这个类型没有"能修正"的字段（等格是结构保证、
越界只能报错、`fps` 猜 12 就是静默兜底）。留着是为了与 `InkingSpriteData` 的构造
流程对称。

> 组件的构造与绘制在 T-05 / T-06 落地；这一节只描述数据面（先写清"能配什么"，
> 再谈"怎么画"）。

### 块叠加组 `InkingSpriteGroup`（0.9.0 T-05）

````C++
class InkingSpriteGroup : public InkingStaticImage, public InkingSpriteLike {
public:
    InkingSpriteGroup(InkingWidget* parent, const InkingSpriteGroupData& data,
                      const std::string& initialAnimation = {});

    InkingEvent<const std::string&, const std::string&> animationChanged;  // (新, 旧)
    InkingEvent<std::size_t, std::size_t> frameChanged;                   // (新帧号, 旧帧号)
    InkingEvent<const std::string&, std::size_t> frameEvent;              // (事件名, 帧号)
    InkingEvent<> finished;

    bool SwitchTo(const std::string& name, bool keepPhase = false);

    const std::string& GetCurrentAnimation() const noexcept;
    std::vector<std::string> GetAnimationNames() const;
    const InkingSpriteGroupData& GetGroupData() const noexcept;
    const std::string& GetGroupError() const noexcept;

    // 帧机的观测口（与 InkingSprite 同名同义）：
    // IsPlaying / IsFinished / GetFrameIndex / GetFrameCount / GetLoadedFrameCount /
    // GetFrameAdvanceCount / GetSkippedFrameCount，外加 Play / Pause / Stop / SeekToFrame
};
````

**一帧 K 块，一次提交**：`onRender` 对每块调一次 `inking_detail::PaintImage`
（同一张纹理、不同的格矩形），提交层按**纹理**分批 ⇒ 2 块一帧只有 **1** 次
`SDL_RenderGeometry`。自检用 `BatchStats` 的差值钉住（2 块与 1 块的
`textureDraws` / `geometrySubmissions` 增量都 = 1）。

| 语义 | 结果 |
| --- | --- |
| 组件框 | **一格**大小（K 块都画在同一个框里，叠图）；不是整张图集 |
| `SwitchTo` 存在的动作 | 换格号表 → 帧数 / 当前动作都跟新动作走；`animationChanged(新, 旧)` 发一次 |
| 切到**当前**动作 | 空操作（返回 true、不重建、不发事件） |
| 名字不存在 | 返回 false + 一条 ERROR（不静默停在旧动作） |
| 换动作与相位 | `keepPhase=false` 落第 0 帧；`true` 保留帧号（越界夹到末帧） |
| 图集几何不符 | **构造期**一条 ERROR（`GetGroupError()` 非空、`IsValid()` 假），不装帧表 ⇒ 不画 |

> **`SetSpriteData` 被降成 private**：组的不变量是"一张图集 + 格号表"，不接受
> "换整份 `InkingSpriteData`"（那可能换文件、换尺寸）。要切动作走 `SwitchTo`。

> **命中不变**：一个矩形。K 块等格同原点，分开命中没有语义（点眼睛那种需求不在
> 本轮）。

### 动态块叠加组 `InkingDynamicSpriteGroup`（0.9.0 T-06）

与静态档**逐字同源**，差别只有两处：

| 场景 | `InkingSpriteGroup`（静态） | `InkingDynamicSpriteGroup`（动态） |
| --- | --- | --- |
| 几何写入口 | 无（编译期 `static_assert` 钉死） | 四个都有：`Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset` |
| 命中 | 进命中表 | **不进表**，占地记「洞」；`Resize` 后洞跟着刷 |
| 每帧推进 | `onAnimationTick` | `onAnimationTick`（**同一条路**） |
| 一帧 K 块一次提交 | 1 次 | **不变**（同一份绘制逻辑） |
| 数据面 / `SwitchTo` / 事件槽 | 见上一节 | **完全一样** |

`Resize` 之后**不需要**重算格矩形：等格是结构保证，矩形只与图集有关、与组件框无关
（自检钉住"Resize 之后帧数一个都不变"）。

**为什么先复制、不抽 `InkingSpriteGroupLike`**：第二档刚出现就抽，抽出什么全凭猜。
判据不是"看起来像"，而是自检里那条「两档同场景跑 100 帧，帧号 / 换帧 / 跳过计数
逐一相等」——同一个场景、同一个动画时钟，`frame = floor(t × fps) % count` 是结构
保证，两边真分家了它就红。**唯一重复的是 `onRender` 那十几行**，先接受；第三档出现
（或它俩真分家）再抽。

````C++
class InkingDynamicSpriteGroup : public InkingDynamicWidget,
                                 public InkingImageLike,
                                 public InkingSpriteLike {
public:
    InkingDynamicSpriteGroup(InkingWidget* parent, const InkingSpriteGroupData& data,
                             const std::string& initialAnimation = {});

    // 继承来的四个几何写入口：Resize / ChangeSelfAnchor / ChangeTraceAnchor / ChangeOffset

    // 事件槽、SwitchTo、观测口、图片面——与静态档同名同义
};
````

## 六、画布

```C++
struct InkingCanvasData {
    std::string name;
    int width = 0, height = 0;
    InkingShapeSpec shape = InkingShapeSpec::Rect();
    std::uint32_t color = 0u;      // 默认透明：只画内容的透明层是最常见用法
    InkingStrokeSpec stroke{};
    InkingAnchorPoint selfAnchor{}, traceAnchor{};
    float offsetX = 0.0f, offsetY = 0.0f;
    InkingLayoutSpec layout;
    InkingLayoutItem layoutItem;
    int zIndex = 0;
    bool visible = true;
    bool clipContent = true;       // 画布 = 视口，默认剪掉越界内容
};
```

```C++
class InkingCanvasPainter {
public:
    virtual void Paint(InkingCanvasContext& canvas) = 0;
    virtual ~InkingCanvasPainter() = default;
};
using InkingCanvasPaintFn = std::function<void(InkingCanvasContext&)>;

class InkingStaticCanvas : public InkingStaticWidget {
public:
    InkingStaticCanvas(InkingWidget* parent, const InkingCanvasData& data);
    ~InkingStaticCanvas() override;

    void SetPainter(std::shared_ptr<InkingCanvasPainter> painter);
    void SetPainter(InkingCanvasPaintFn paint);
    void ClearPainter();
    bool HasPainter() const noexcept;

    void Invalidate();                 // 内容变了 → 下次重绘重新调绘制器
    bool IsContentDirty() const noexcept;
    void SetClipContent(bool clip);
    bool ClipsContent() const noexcept;

    std::uint64_t GetPaintCount() const noexcept;    // 真的调了几次绘制器
    std::uint64_t GetReplayCount() const noexcept;   // 回放了几次命令表
    std::size_t GetCommandCount() const noexcept;
};
```

**脏驱动**：绘制器**不是每帧被调用**。画布把一次的绘制结果记成**命令表**，之后每帧只是
回放命令表；只有 `Invalidate()` 之后的那次重绘才会重新调绘制器。于是绘制器里那些
"比重新画更贵"的逻辑每帧只跑一次。

`InkingCanvasContext`（绘制器能用的画笔）：

```C++
float Width() const noexcept;
float Height() const noexcept;

void FillShape(const InkingShapeSpec&, float x, float y, float w, float h, color, ...);
void StrokeShape(const InkingShapeSpec&, float x, float y, float w, float h, color, ...);
void FillRect(float x, float y, float w, float h, std::uint32_t color, ...);
void FillCircle(float centerX, float centerY, float diameter, std::uint32_t color, ...);
void DrawLine(float x0, float y0, float x1, float y1, float thickness, color, ...);
void DrawText(const InkingTextRequest& request);
void DrawText(const std::string& text, float x, float y, float fontSize, color);

void PushClip(float x, float y, float w, float h);
void PopClip();
void ImmediateDraw(std::function<void(SDL_Renderer*)> draw);   // 逃生口
SDL_Renderer* Renderer() const noexcept;
```

坐标是**画布本地设计坐标**。命中只判形状本体，**内容不参与命中**（图表元素热区留给第二版）。

## 七、视频

```C++
enum class InkingVideoFit : std::uint8_t { Contain, Cover, Fill };

struct InkingVideoOptions {
    bool autoplay = true;
    bool loop = false;
    bool muted = false;
    float volume = 1.0f;    // 再乘 Voice 通道与主音量
};

struct InkingVideoData : public InkingButtonData {
    std::string source;     // 相对资源根，例如 "op.mp4"
    InkingVideoFit fit = InkingVideoFit::Contain;
    InkingVideoOptions options{};
};
```

```C++
class InkingStaticVideo : public InkingStaticWidget {
public:
    InkingStaticVideo(InkingWidget* parent, const InkingVideoData& data);
    ~InkingStaticVideo() override;

    void Play();  void Pause();  void Stop();
    void Seek(float seconds);
    void SetLoop(bool loop) noexcept;
    void SetVolume(float volume) noexcept;
    void SetMuted(bool muted) noexcept;

    bool IsPlaying() / IsFinished() / IsOpened() const noexcept;
    float GetPositionSeconds() / GetDurationSeconds() const noexcept;
    int GetVideoWidth() / GetVideoHeight() const noexcept;
    bool HasAudioTrack() const noexcept;
    bool InkingAssetExists() const noexcept;
    const std::string& AssetPath() const noexcept;
    bool IsAtEnd() const noexcept;

    std::uint32_t GetDecodedFrameCount() const noexcept;
    std::uint32_t GetPresentedFrameCount() const noexcept;
    std::uint32_t GetDroppedFrameCount() const noexcept;
};
```

三条与图片不同的约定：

- `source` 是**视频文件**（相对资源根），`normal.source` 不参与视频读取；
- `normal.color` 是**没有画面时的兜底色**（后端缺失 / 素材没打开 / 还没解出第一帧）
  ——把画面变成一个洞比"缺视频"更难查；
- 视频自带音轨，所以多一份 `options`（音量走音频层的 `Voice` 通道）。

## 八、音频（不是组件）

音频是**全局函数族**，不挂在节点上——标题画面没有场景也要有 BGM。

> **用之前必须显式 `InkingAudioInit()`。** 窗口层**不会**替你初始化音频：它只在 `Show()`
> 之后每帧调 `InkingAudioTick`、关窗时调 `InkingAudioShutdown`（这两条已接真实实现）。
> 不调 `InkingAudioInit()` 时整层空转——`InkingAudioReady()` 为 false，所有 `InkingAudioPlayBgm` /
> `InkingAudioPlaySe` 静默无效，**而且不报错**。
>
> `InkingAudioInit()` 返回 false 通常意味着构建期漏了 `INK_HAS_SDL3_MIXER`
> （`src/audio/InkingAudio.cpp` 整份被这个宏包着，缺了它编进去的是空实现分支）。
> 这个失效**编得过、链得过**，只能靠二进制判据抓：`objdump -p libinking.dll`
> 的依赖里必须有 `SDL3_mixer.dll`。

```C++
using InkingSoundId = std::uint32_t;
inline constexpr InkingSoundId InkingInvalidSound = 0u;

enum class InkingAudioChannel : std::uint8_t { Bgm = 0, Se = 1, Voice = 2 };
enum class InkingAudioLoadMode : std::uint8_t { Auto, InMemory, Streaming };
enum class InkingAudioLoadState : std::uint8_t { /* Idle / Loading / Ready / Failed */ };

bool InkingAudioInit();  void InkingAudioShutdown();  bool InkingAudioReady();
InkingAudioStats InkingAudioStatsSnapshot();
std::string InkingAudioDecoderName(int index);

InkingSoundId InkingAudioLoad(path, mode = Auto);
InkingSoundId InkingAudioLoadAsync(path, mode = Auto);
InkingSoundId InkingAudioLoadFromMemory(const void* data, std::size_t size, ...);
void InkingAudioPreload(const std::vector<std::string>& paths);
InkingAudioLoadState InkingAudioLoadStateOf(InkingSoundId id);
bool InkingAudioIsReady(InkingSoundId id);
float InkingAudioDurationSeconds(InkingSoundId id);

void InkingAudioPlayBgm(InkingSoundId, float fadeIn = 0.0f, ...);
void InkingAudioPlayBgmAsync(InkingSoundId, float fadeIn = 0.0f, ...);
void InkingAudioStopBgm(float fadeOut = 0.0f);
void InkingAudioPauseBgm();  void InkingAudioResumeBgm();
bool InkingAudioIsBgmPlaying();  bool InkingAudioIsBgmPaused();
float InkingAudioBgmVolume();    float InkingAudioBgmPositionSeconds();

void InkingAudioPlaySe(InkingSoundId, float volume = 1.0f);
void InkingAudioPlaySeAsync(InkingSoundId, float volume = 1.0f);
std::uint32_t InkingAudioActiveSeCount();

void InkingAudioPlayVoice(InkingSoundId, float volume = 1.0f);
void InkingAudioStopVoice();
bool InkingAudioIsVoicePlaying();
float InkingAudioVoicePositionSeconds();  float InkingAudioVoiceVolume();

void InkingAudioSetMasterVolume(float);  float InkingAudioMasterVolume();
void InkingAudioSetChannelVolume(InkingAudioChannel, float);  float InkingAudioChannelVolume(InkingAudioChannel);

// 自定义 PCM 流（程序合成的音频）
InkingPcmStreamId InkingAudioOpenPcmStream(int sampleRate, int channels);
std::size_t InkingAudioPushPcm(InkingPcmStreamId, const void* data, ...);
bool InkingAudioPcmStreamWantsData(InkingPcmStreamId);
void InkingAudioSetPcmStreamPlaying(InkingPcmStreamId, bool);
float InkingAudioPcmStreamPositionSeconds(InkingPcmStreamId);
void InkingAudioSetPcmStreamVolume(InkingPcmStreamId, float);
void InkingAudioClosePcmStream(InkingPcmStreamId);
std::uint32_t InkingAudioPcmStreamCount();

void InkingAudioTick(float deltaSeconds);   // 主循环每帧调（窗口层已经接了）
```

`InkingAudioLoadMode::Auto` 会按 `InkingStreamingThresholdSeconds`（10 秒）决定：
长的走 `Streaming`（边解边放，省内存）、短的 `InMemory`。

## 常见坑

1. **继承 `InkingStaticButton` 来做"另一个有三态的组件"**：继承 `InkingButtonLike`。
2. **给静态按钮加 `Resize`**：编译期断言会拦。
3. **面板剪了子孙却指望"点得到"**：`clipChildren` 只影响绘制；
   命中的裁剪由容器的 `HitTest` 递归那条路管，两者同源，会一起生效。
4. **滚动容器不重写 `HandlesSubtreeWheel()`**：滚轮被指针下的子按钮吃掉。
5. **标签 `SetText` 之后不重排**：框架不做隐式重排（`SetText` 自己会喊 `RelayoutParent`，
   自己写的组件要记得）。
6. **输入框不重写 `WantsKeyboardFocus()`**：默认 `false`，点了拿不到焦点，**不报错**。
7. **画布每帧调 `Invalidate()`**：命令表失去意义，等于回到"每帧重画"。

---

> 本文档对应版本：**0.9.0（0.x 发布版）**。
