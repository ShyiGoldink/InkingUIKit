# 组件

**回到API目录：**
[API index](../API.md)

"组件"= 能直接 `new` 出来放进场景的东西。本页按族过一遍，每个组件给**数据面 → 构造 →
关键 API → 边界**四段。公共行为（标脏、命中、输入、布局）在
[InkingWidget.md](InkingWidget.md)，不在这里重复。

| 组件 | 数据 | 档位 | 头文件 |
| --- | --- | --- | --- |
| 静态按钮 | `ButtonData` | 静态 | `button/InkingStaticButton.h` |
| 动态按钮 | `ButtonData` | 动态 | `button/InkingDynamicButton.h` |
| 面板（抽屉 / 弹窗 / 滚动容器） | `PanelData` | 静态 | `panel/InkingStaticPanel.h` |
| 标签 | `LabelData` | 静态 | `text/InkingStaticLabel.h` |
| 输入框 | `TextInputData : LabelData` | 静态 | `text/InkingTextInput.h` |
| 图片 | `ImageData : ButtonData` | 静态 | `image/InkingStaticImage.h` |
| 画布 | `CanvasData` | 静态 | `canvas/InkingStaticCanvas.h` |
| 视频 | `VideoData : ButtonData` | 静态 | `video/InkingVideo.h` |
| 音频（不是组件，是全局函数族） | — | — | `audio/InkingAudio.h` |

**为什么只有按钮有动态档**：只有它需要"运行期改几何"（按下撑开、内容撑高）。
其余组件的几何构造即定型，改尺寸/改内容只标重绘脏——这正是静态档的定义。

## 一、按钮族

### 分四层，各管一件事

```
ButtonData            数据：三态外观 / 每态尺寸 / 文字 / 形状 / 描边阴影
ButtonLook            状态机：三态 + 过渡插值（纯值类型，可单测）
InkingButtonLike      契约：clicked / stateChanged 事件 + 三态 + [√] 钩子（带实现，不是纯接口）
detail::ButtonPaint   绘制：形状 / 贴图 / 描边阴影 / 文字
      ↓
InkingStaticButton    接线：形状、文字、命中、登记（输入由场景推）
InkingDynamicButton   接线：同上（输入由场景在指针变化时通知，它自己读状态）
```

**为什么不抽成纯接口**：`InkingButtonLike` 必须**带状态**（`ButtonLook` 那十几个字段、
过渡的计时与起点、`clicked` 槽）。纯接口之后每个使用者都要把状态机与二十来行转发
再写一遍——正是这个文件要消除的东西。

**为什么它不继承 `InkingPointerHandler`**：`InkingWidget` 已经继承了它，再继承一次就是
**菱形**（两个子对象，`PushPointerHover` 直接歧义，而且未必编译报错）。所以它只提供
`MouseHover` / `MousePress` / `MouseRelease` / `TriggerClick` 四个入口，组件在自己的
`onPointer*` 里转发一行。

### ButtonData

```C++
struct ButtonData {
    std::string name;              // 与 json 文件名、代码里的名字三处一致
    int width = 0, height = 0;     // 设计坐标

    ButtonAppearance normal{}, hover{}, onclicked{};
    bool hoverInheritsNormal = true;      // 省略 hover 就继承 normal
    bool onclickedInheritsNormal = true;

    ButtonTextData text{};
    std::string label;             // text.content.normal 的运行时形态
    std::uint32_t textColor = 0xFFFFFFFFu;

    AnchorPoint selfAnchor{}, traceAnchor{};
    float offsetX = 0.0f, offsetY = 0.0f;

    LayoutSpec layout;             // 我是容器时怎么排孩子
    LayoutItem layoutItem;         // 我在父容器里的意愿

    int zIndex = 0;
    bool visible = true;

    ShapeSpec shape = ShapeSpec::Rect();  // 形状层
    StrokeSpec stroke;             // 描边（与形状平级的两笔装饰）
    ShadowSpec shadow;

    void Normalize();              // 补齐"省略即继承"，幂等
    bool HasLabel() const noexcept;
};
```

>**ButtonAppearance**

```C++
struct ButtonAppearance {
    AppearanceType type = AppearanceType::Color;  // Color / Image / Svg
    std::uint32_t color = 0xFF000000u;
    std::string source;                 // 原样的配置串
    const std::uint8_t* imageBytes = nullptr;  // 借用：生成代码里的静态数组
    std::size_t imageSize = 0;
    ImageFit imageFit = ImageFit::Fill;
    float transitionSeconds = 0.0f;     // 进入这个状态的过渡时长
    Easing easing = Easing::Linear;
    TransformSpec transform{};          // 这个状态的目标变换（不改几何、不改命中表）
    float width = 0.0f, height = 0.0f;  // 这个状态的尺寸（0 = 用 ButtonData 的）

    static ButtonAppearance FromUnitRgba(float r, float g, float b, float a = 1.0f);
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
4. `AppearanceType::Image` / `Svg` 的**数据面已留、纹理路已接**（`imageBytes` +
   `imageFit`），但生成器那边还在落地中。

>**ButtonTextData**

```C++
struct ButtonTextData {
    float fontSize = 0.0f;
    float leftSpace = 0.0f;   // 距左侧（align == Left 时用）
    float topSpace = 0.0f;    // 距顶部（alignV == Top 时用）
    std::string path;         // 空 = 机器默认字体
    TextAlignH align = TextAlignH::Left;
    TextAlignV alignV = TextAlignV::Top;
};
```

`Center` / `Right` / `Middle` / `Bottom` 会**先量文字块的真实尺寸**再算偏移——
"菜单项文字居中"这种最基本的需求不该靠手填估算值（比例字体下必然歪）。

### InkingStaticButton

```C++
InkingStaticButton(InkingWidget* parent, const ButtonData& data);   // 手写数据
InkingStaticButton(InkingWidget* parent, const std::string& name);  // 按名字从 ButtonLibrary 取

static const ButtonData* FindData(const std::string& name);
static int UnknownNameCount() noexcept;    // 取不到配置的次数（自检断言用）

// 下面这些都从 InkingButtonLike 继承得来，本类一个都不转发：
//   clicked / contextClicked / stateChanged
//   GetState / GetAppearance / GetDisplayColor / GetDisplayTransform
//   GetDisplayWidth / GetDisplayHeight / MouseHover / MousePress / MouseRelease
//   TriggerClick / TriggerContextClick / SetOnClicked / SetOnContextClicked
//   HasOnClicked / HasOnContextClicked / IsTransitioning

bool HitTest(float worldX, float worldY) const override;
const ShapeSpec& GetShape() const noexcept;
ShapeBounds GetHitEnvelope() const override;
int GetWidth() const noexcept override;      // 当前显示尺寸（三态插值后）
int GetHeight() const noexcept override;
bool HasLayoutAnimation() const override;
TransformSpec GetSubtreeTransform() const override;
const ButtonData& GetData() const noexcept;
const std::string& GetText() const noexcept;
void SetText(const std::string& text);
void SetTextColor(std::uint32_t color);
```

**为什么是静态档**：三态只换颜色（颜色不标脏）；按下要"下沉"就是绘制偏移
（`onRender` 里把 y 挪几像素），不是几何变化。真要"按下改尺寸、撑开布局"就用动态版。
**不要在这里加几何写入口**——`src/inking.cpp` 有编译期断言钉着。

### InkingDynamicButton

```C++
InkingDynamicButton(InkingWidget* parent, const ButtonData& data);
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

    ButtonState GetState() const noexcept;                     // Normal / Hover / Pressed
    const ButtonAppearance& GetAppearance() const noexcept;
    const ButtonAppearance& GetAppearance(ButtonState) const noexcept;
    std::uint32_t GetDisplayColor() const noexcept;
    const TransformSpec& GetDisplayTransform() const noexcept;
    int GetDisplayWidth() const noexcept;                      // 注意不叫 GetWidth
    int GetDisplayHeight() const noexcept;
    bool IsTransitioning() const noexcept;
    float GetTransitionProgress() const noexcept;
    bool HasButtonLayoutAnimation() const noexcept;

    bool HasOnClicked() const noexcept;
    void SetOnClicked(ButtonClickCallback callback);           // std::function<void()>
    bool TriggerClick();

    // 次键（右键 / 触摸长按）：**带按下点**，典型用法是 OpenAt(x, y)
    bool HasOnContextClicked() const noexcept;
    void SetOnContextClicked(ButtonContextClickCallback callback);  // void(float, float)
    bool TriggerContextClick(float designX, float designY);

    bool MouseHover(bool hover) noexcept;
    bool MousePress(bool press) noexcept;
    bool MouseRelease() noexcept;
    ButtonLook::AdvanceResult AdvanceButtonLook(float deltaSeconds) noexcept;

protected:
    void ResetButtonLook(const ButtonData& data) noexcept;
    virtual void onButtonStateChanged(ButtonState from, ButtonState to);
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

### ButtonLibrary

```C++
class ButtonLibrary {
public:
    enum class RegisterResult { Ok, NameTaken, Invalid };  // Invalid = 名字为空
    static RegisterResult Register(const ButtonData& data);
    static const ButtonData* Find(const std::string& name);
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

> **"配置写出来、类由生成器烘出来"那条路**（`inking::cxxcss::OptionItem` 这类具名子类、
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
struct PanelAppearance {
    std::uint32_t color = 0xFF000000u;
    float transitionSeconds = 0.0f;
    Easing easing = Easing::Linear;
    TransformSpec transform{};   // "抽屉拉出来多少"的唯一表达
    static PanelAppearance FromUnitRgba(r, g, b, a = 1.0f);
};

struct PanelData {
    std::string name;
    int width = 0, height = 0;
    PanelAppearance closed{}, open{};
    bool openInheritsClosed = true;
    ShapeSpec shape = ShapeSpec::Rect();
    AnchorPoint selfAnchor{}, traceAnchor{};
    float offsetX = 0.0f, offsetY = 0.0f;
    LayoutSpec layout;
    LayoutItem layoutItem;
    int zIndex = 0;
    bool visible = true;
    bool startOpen = false;      // 构造时就是开着的（不先起一段过渡）
    bool clipChildren = false;   // 剪子孙（默认关：面板常装故意溢出的阴影）
    ScrollSpec scroll;           // 配了就能滚（B12），没配就是普通容器
    void Normalize();
};
```

主要 API：

```C++
bool SetOpen(bool open) noexcept;     // 幂等：状态没变返回 false
bool Toggle() noexcept;
bool Open() noexcept;  bool Close() noexcept;
bool IsOpen() const noexcept;
PanelState GetState() const noexcept;
std::uint32_t GetDisplayColor() const noexcept;
const TransformSpec& GetDisplayTransform() const noexcept;
bool IsTransitioning() const noexcept;
float TransitionProgress() const noexcept;

// 滚动（B12）
void UpdateScrollRange();
void SetScroll(float x, float y);
void WheelScroll(float deltaX, float deltaY);
ScrollBarMetrics GetBarMetrics(bool vertical) const noexcept;
void PressScrollAt(float designX, float designY);
void DragScrollTo(float designX, float designY);
int DraggingAxis() const noexcept;
float GetScrollX() / GetScrollY() const noexcept;
float GetScrollRangeX() / GetScrollRangeY() const noexcept;
float GetContentWidth() / GetContentHeight() const noexcept;
```

滚动的**几何规则**全在 `dataStruct/InkingScroll.h` 的纯函数里（`ScrollRange` /
`ClampScroll` / 轨道 / 滑块 / 点轨道跳过去），面板只负责把指针事件接上去。

> **滚动容器必须重写 `HandlesSubtreeWheel()`**（面板已重写）：否则指针停在容器里的子按钮上
> 时，滚轮派给了按钮、容器毫无反应。`MenuBar` 同理。

## 三、标签

```C++
struct LabelData {
    std::string name;
    int width = 0, height = 0;      // 参与布局时会被布局改写
    AnchorPoint selfAnchor{}, traceAnchor{};
    float offsetX = 0.0f, offsetY = 0.0f;
    int zIndex = 0;
    bool visible = true;
    LayoutSpec layout{};
    LayoutItem layoutItem{};

    std::string text;                    // UTF-8
    TextStyle style{};                   // 字号 / 行距 / 字体 / 对齐 / 内缩
    std::uint32_t color = 0xFFFFFFFFu;
    TextWrap wrap = TextWrap::Word;
    TextOverflow overflow = TextOverflow::Clip;
    int maxLines = 0;                    // 0 = 不限
    bool Empty() const noexcept;
};
```

```C++
class InkingStaticLabel : public InkingStaticWidget {
public:
    InkingStaticLabel(InkingWidget* parent, const LabelData& data);
    InkingStaticLabel(InkingWidget* parent, const std::string& name);

    const LabelData& GetData() const noexcept;
    bool SetText(std::string text) noexcept;        // 变了才 true
    bool SetTextColor(std::uint32_t color) noexcept;
    const TextLayoutResult& GetTextLayout() const;  // 最近一次排版结果

    LayoutSize MeasureLayout(const LayoutConstraints&) const override;  // 按文字撑高
    bool AcceptsExternalTransform() const noexcept override { return true; }
};
```

`SetText` 会调 `RelayoutParent()`：文字变长 → 自己变高 → **兄弟要让位**，
那是父容器的活儿。框架不做隐式重排，所以这一步必须由组件自己喊。

### 多语言：TextTable

```C++
struct TextEntry { std::string key; /* 各语言的串 */ };
struct TextTable {
    /* languages + entries */
    int LanguageIndex(const std::string& language) const noexcept;
    const TextEntry* Find(const std::string& key) const noexcept;
    bool Empty() const noexcept;
};

bool InstallTextTable(TextTable table, const std::string& language = {});
bool SetTextLanguage(const std::string& language);
const std::string& CurrentTextLanguage() noexcept;
std::size_t CurrentTextLanguageIndex() noexcept;
std::uint64_t TextLanguageVersion() noexcept;   // 变了 +1，用来判"要不要重查"
std::string ResolveText(const std::string& key);
TextTableStats TextTableStatsSnapshot() noexcept;   // { hits, misses, version }
void ClearTextTable() noexcept;
```

语言切换 = 换一串 `text` ⇒ 文字纹理缓存自然未命中（键里有文本）⇒ 重烘一次。

## 四、输入框（单行）

```C++
struct TextInputData : public LabelData {
    std::uint32_t caretColor = 0xFFEAF2FFu;
    std::uint32_t selectionColor = 0x663E7BD8u;
    float caretBlinkSeconds = 0.6f;   // 0 = 不闪
    float caretWidth = 1.5f;
};
```

```C++
class InkingTextInput : public InkingStaticLabel {
public:
    InkingTextInput(InkingWidget* parent, const TextInputData& data);

    bool WantsKeyboardFocus() const noexcept override { return true; }  // ← 关键
    bool HitTest(float worldX, float worldY) const override;

    const std::string& GetEditText() const noexcept;
    void SetEditText(const std::string& text);
    TextEditState& Edit() noexcept;

    bool IsFocused() const noexcept;
    void SetFocused(bool focused);

    bool HandleKey(const KeyEvent& key);       // 手动喂按键（自检用）
    void HandleText(const std::string& text);  // 手动喂文本（IME 结果）
    void PressAt(float designX, float designY);
    void DragTo(float designX, float designY);

    void TickCaretBlink(float deltaSeconds);
    TextInputRect CaretBox() const;
    std::vector<TextInputRect> SelectionBoxes() const;
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

1. **编辑状态是独立的一份**（`TextEditState`，纯逻辑可单测）；每次改动之后 `syncToLabel()`
   交给基类 ⇒ 重排 + 重绘。
2. **吃点击**（构造里显式 `SetHitPassThrough(false)`）：输入框要点击定位光标，
   而标签默认是"穿透"的。**别指望默认值。**
3. **快捷键认 `ctrl`**（A / C / X / V），**IME 提交走 `onTextInput`**
   （`SDL_EVENT_TEXT_INPUT`，输入法组合好的结果从这里进来）。

**这一版是单行**：`wrap` 会被强制成 `NoWrap`，粘贴时换行被丢掉。
**没做**：多行、撤销 / 重做、Tab 焦点遍历、候选窗位置。

## 五、图片

```C++
struct ImageData : public ButtonData {
    // 多出来的只有"图从哪来"：继承 ButtonData 是为了形状 / 描边 / 锚点 / 命中同构
};
```

```C++
class InkingStaticImage : public InkingStaticWidget, public InkingButtonLike {
public:
    InkingStaticImage(InkingWidget* parent, const ImageData& data);

    bool HitTest(float worldX, float worldY) const override;
    const ShapeSpec& GetShape() const noexcept;
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
};
```

**它继承 `InkingButtonLike`，但一个 `onPointer*` 都不重写**（除拖拽那一路）——
于是没人喂它、三态永远停在 Normal，**正好就是它要的**。所以不必为"可点 / 不可点"
拆成两个基类：差别只是"重不重写那几个钩子"。

拖拽走的是**运行期显示变换**（`SetDisplayTransform`）＋ `Resize` 的仿射表示，
不会撑开兄弟（不改变占位）。

## 六、画布

```C++
struct CanvasData {
    std::string name;
    int width = 0, height = 0;
    ShapeSpec shape = ShapeSpec::Rect();
    std::uint32_t color = 0u;      // 默认透明：只画内容的透明层是最常见用法
    StrokeSpec stroke{};
    AnchorPoint selfAnchor{}, traceAnchor{};
    float offsetX = 0.0f, offsetY = 0.0f;
    LayoutSpec layout;
    LayoutItem layoutItem;
    int zIndex = 0;
    bool visible = true;
    bool clipContent = true;       // 画布 = 视口，默认剪掉越界内容
};
```

```C++
class ICanvasPainter {
public:
    virtual void Paint(CanvasContext& canvas) = 0;
    virtual ~ICanvasPainter() = default;
};
using CanvasPaintFn = std::function<void(CanvasContext&)>;

class InkingStaticCanvas : public InkingStaticWidget {
public:
    InkingStaticCanvas(InkingWidget* parent, const CanvasData& data);
    ~InkingStaticCanvas() override;

    void SetPainter(std::shared_ptr<ICanvasPainter> painter);
    void SetPainter(CanvasPaintFn paint);
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

`CanvasContext`（绘制器能用的画笔）：

```C++
float Width() const noexcept;
float Height() const noexcept;

void FillShape(const ShapeSpec&, float x, float y, float w, float h, color, ...);
void StrokeShape(const ShapeSpec&, float x, float y, float w, float h, color, ...);
void FillRect(float x, float y, float w, float h, std::uint32_t color, ...);
void FillCircle(float centerX, float centerY, float diameter, std::uint32_t color, ...);
void DrawLine(float x0, float y0, float x1, float y1, float thickness, color, ...);
void DrawText(const TextRequest& request);
void DrawText(const std::string& text, float x, float y, float fontSize, color);

void PushClip(float x, float y, float w, float h);
void PopClip();
void ImmediateDraw(std::function<void(SDL_Renderer*)> draw);   // 逃生口
SDL_Renderer* Renderer() const noexcept;
```

坐标是**画布本地设计坐标**。命中只判形状本体，**内容不参与命中**（图表元素热区留给第二版）。

## 七、视频

```C++
enum class VideoFit : std::uint8_t { Contain, Cover, Fill };

struct VideoOptions {
    bool autoplay = true;
    bool loop = false;
    bool muted = false;
    float volume = 1.0f;    // 再乘 Voice 通道与主音量
};

struct VideoData : public ButtonData {
    std::string source;     // 相对资源根，例如 "op.mp4"
    VideoFit fit = VideoFit::Contain;
    VideoOptions options{};
};
```

```C++
class InkingStaticVideo : public InkingStaticWidget {
public:
    InkingStaticVideo(InkingWidget* parent, const VideoData& data);
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
    bool AssetExists() const noexcept;
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

> **用之前必须显式 `AudioInit()`。** 窗口层**不会**替你初始化音频：它只在 `Show()`
> 之后每帧调 `AudioTick`、关窗时调 `AudioShutdown`（这两条已接真实实现）。
> 不调 `AudioInit()` 时整层空转——`AudioReady()` 为 false，所有 `AudioPlayBgm` /
> `AudioPlaySe` 静默无效，**而且不报错**。
>
> `AudioInit()` 返回 false 通常意味着构建期漏了 `INK_HAS_SDL3_MIXER`
> （`src/audio/InkingAudio.cpp` 整份被这个宏包着，缺了它编进去的是空实现分支）。
> 这个失效**编得过、链得过**，只能靠二进制判据抓：`objdump -p libinking.dll`
> 的依赖里必须有 `SDL3_mixer.dll`。

```C++
using SoundId = std::uint32_t;
inline constexpr SoundId kInvalidSound = 0u;

enum class AudioChannel : std::uint8_t { Bgm = 0, Se = 1, Voice = 2 };
enum class AudioLoadMode : std::uint8_t { Auto, InMemory, Streaming };
enum class AudioLoadState : std::uint8_t { /* Idle / Loading / Ready / Failed */ };

bool AudioInit();  void AudioShutdown();  bool AudioReady();
AudioStats AudioStatsSnapshot();
std::string AudioDecoderName(int index);

SoundId AudioLoad(path, mode = Auto);
SoundId AudioLoadAsync(path, mode = Auto);
SoundId AudioLoadFromMemory(const void* data, std::size_t size, ...);
void AudioPreload(const std::vector<std::string>& paths);
AudioLoadState AudioLoadStateOf(SoundId id);
bool AudioIsReady(SoundId id);
float AudioDurationSeconds(SoundId id);

void AudioPlayBgm(SoundId, float fadeIn = 0.0f, ...);
void AudioPlayBgmAsync(SoundId, float fadeIn = 0.0f, ...);
void AudioStopBgm(float fadeOut = 0.0f);
void AudioPauseBgm();  void AudioResumeBgm();
bool AudioIsBgmPlaying();  bool AudioIsBgmPaused();
float AudioBgmVolume();    float AudioBgmPositionSeconds();

void AudioPlaySe(SoundId, float volume = 1.0f);
void AudioPlaySeAsync(SoundId, float volume = 1.0f);
std::uint32_t AudioActiveSeCount();

void AudioPlayVoice(SoundId, float volume = 1.0f);
void AudioStopVoice();
bool AudioIsVoicePlaying();
float AudioVoicePositionSeconds();  float AudioVoiceVolume();

void AudioSetMasterVolume(float);  float AudioMasterVolume();
void AudioSetChannelVolume(AudioChannel, float);  float AudioChannelVolume(AudioChannel);

// 自定义 PCM 流（程序合成的音频）
PcmStreamId AudioOpenPcmStream(int sampleRate, int channels);
std::size_t AudioPushPcm(PcmStreamId, const void* data, ...);
bool AudioPcmStreamWantsData(PcmStreamId);
void AudioSetPcmStreamPlaying(PcmStreamId, bool);
float AudioPcmStreamPositionSeconds(PcmStreamId);
void AudioSetPcmStreamVolume(PcmStreamId, float);
void AudioClosePcmStream(PcmStreamId);
std::uint32_t AudioPcmStreamCount();

void AudioTick(float deltaSeconds);   // 主循环每帧调（窗口层已经接了）
```

`AudioLoadMode::Auto` 会按 `kStreamingThresholdSeconds`（10 秒）决定：
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

> 本文档对应版本：**0.5.0（0.x 发布版）**。
