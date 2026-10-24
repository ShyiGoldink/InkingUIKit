# 任务书（TaskGuide）

> ## ⚠ 本文对应版本 0.4.0（0.x 发布版）
>
> 版本号 `0.4.0`（`inking::kVersion` / `inking::kVersionStage`）。
>
> - **本文是重建稿**：原任务书未随仓库搬入，正文按代码注释里的引用反推，逐节标了证据。
> - **只对编号负责**：它回答"哪个任务、落在哪、做到什么程度"，不替代 `docs/zh/api/*.md`。
> - **不要用于生产**：`0.4.0` 是内部联调版本，任务状态会随接口调整变化。

## 0 这份文档是什么 / 重建说明

本文件是**按注释引用重建**的任务书。源项目里 `TaskGuide.md` 是分层重构的任务清单：
每个编号（`T-1` / `A3` / `B7` / `C2` / `D-1` / `r2`）对应一个**可独立验收的原子改动**，
代码注释按编号回指它。本仓库搬家时这份文档没带进来，于是约 25 处引用全部落空
（`docs/zh/SKILL.md` §0 的缺口表当时记为"完全缺失"，现已随本文重建而关闭）。

重建只有三条依据，按可信度排序：

1. **头文件注释**：写有"`TaskGuide.md` 的 X"的地方，引用点前后通常已复述该任务的约束、理由乃至"原话"。
2. **代码行为**：能落地的编号，其"关键 API"与"验收判据"都能指到 `include/` 里的具体符号；
   落不了地的编号，也能从头文件里读到"为什么没做"。
3. **`docs/zh/api/*.md`**：组件与预制件的边界在那里更完整，用来交叉核对（与头文件冲突时以头文件为准）。

**`†` 的含义**：标 `†` 的小节，其**目标**或**背景**是依据代码行为与引用上下文复原的，
**原始任务书的表述不可考**。标 `†` 不等于内容错，只表示没有原文可对照；
结论仍按该节的"落点"与"验收"去核。未标 `†` 的小节至少有引用点原文或可核对符号支撑。

**边界**：`docs/InputDesign.md` 同样缺失，T-4 里的"§6 / §10 / §11"条号**无法核对**，本文只保留注释已复述的结论；
`docs/zh/SKILL.md` 是规范，不重复其条款；本轮只新增本文件，`docs/zh/API.md` §3 的地图尚未收录它。

引用点核对（改本文之前先跑一遍）：
`grep -rn "TaskGuide" include src docs --include=*.h --include=*.cpp --include=*.md`

## 1 任务编号体系

### 1.1 六个系列

| 系列 | 含义 | 编号形态 | 主要落点 |
| --- | --- | --- | --- |
| **A** | **原子能力**：输入 / 布局 / 文字 / 画布 / 音频这一层"单个组件也能用"的能力 | `A1`…`A14`（正文收 12 条；`A5`/`A9`/`A10` 见 §8.2） | `input/`、`widget/`、`dataStruct/`、`canvas/`、`audio/` |
| **B** | **基础组件**：单个组件内部做扎实的事 | `B4`、`B7`、`B12` | `render/`、`text/`、`panel/` |
| **C** | **预制件**：用现成组件组装出的复合件，不新增底层能力 | `C1`…`C5` | `prefab/` |
| **D** | **动画与过渡**：状态之间的插值规则 | `D-1` | `button/ButtonLook.h`、`panel/PanelLook.h` |
| **T** | **纹理 / 文字层与命中**：把"字符串 → 像素"和"点 → 谁"两件事收口 | `T-1`…`T-4`（外延 `T-5`…`T-7`） | `render/`、`scene/`、`dataStruct/` |
| **r2** | **任务纪律**：一次运行最多完成一个任务 | `r2` | 全仓库（工作方式，不是代码） |

### 1.2 编号怎么用

- **一个编号 = 一个可独立验收的单元**，必须能回答"改哪里、怎么算做完了"；做不到就拆号（如 A8 → A8a/A8b）。
- **注释里只写编号，不重复任务内容**——复述一遍就是两份真相，改了一处另一处失真。
- **编号只增不改**：补做用新编号，不复用旧号；废止的编号保留并注明"未落地"。
- **未落地的编号也保留**：A2、A8b 这类"编号在、能力不在"比删掉更有用，它解释了下一个人为什么找不到实现。

### 1.3 为什么编号要稳定

1. **注释是跨文件的索引**。`grep TaskGuide` 是读代码的入口；改号等于把索引打断，
   而断了不会报错，只会让人把"文档缺失"误判成"代码有问题"。
2. **验收判据挂在编号上**。"T-1 的坑"指一类具体的漏命中，不是一个抽象原则。
3. **已知偏离需要归属**。那些"知道该抽、这次不抽"的决定必须挂在编号上，
   否则下一个人会把它当成疏漏顺手"修"掉。

反例就在代码里：`ButtonLook` 与 `PanelLook` 的过渡推进逻辑重复，`A1` 有意没抽
`TransitionTrack`，理由写在 `panel/PanelLook.h:14-22`（没有回归网时不动已验证组件；
`r2` 一次只做一件事）。**A1 若改号，这段解释就悬空了。**

## 2 A 系列：原子能力

共同判据是**组件侧零改动**：能力接上之后，已有组件不该为此各写一份；状态见 §8.1，下面只写边界。

### A1 指针钩子的对称派发

- **目标**：指针事件按**位置**派发，但派发路径上**不做类型查询**。场景只认 `InkingPointerHandler` 的
  `Push*`，组件只重写 `onPointer*`——早年 `static_cast<InkingStaticButton*>` 是 UB 的来源。
- **落点**：`input/InkingPointerHandler.h`、`scene/InkingScene.h`（`DispatchPointer` / `GetDraggingNode`）。
- **API**：`PushPointerHover(bool)` / `PushPointerPress(bool)` / `PushPointerWheel(float,float)` /
  `PushPointerDrag(float,float,float,float)` → 同名 `onPointer*` 钩子。
- **验收**：① 新增一个只继承 `InkingWidget` 的可命中组件，指针扫过不得崩、不得 UB；
  ② 进入/离开、按下/抬起各只有一条 `bool` 入口（不拆两个函数）；③ 五个钩子必须在头里给空实现
  ——只声明不定义会让基类 vtable 引用不存在的符号，任何派生类链接期报 `undefined reference`。
- **偏离**：拖动捕获状态住在场景（`GetDraggingNode`），没有"把捕获转交给另一节点"的 API。

### A2 配置驱动的 Panel 生成 †

- **目标**：让 Panel 成为第二个"配置驱动"的组件——`CXXCSS/Panel/<名字>.json` 经生成器展开成类，
  构造时按名字取 `PanelData`。†（按 `panel/InkingStaticPanel.h:36-44` 的引用还原，原文不可考。）
- **落点**：`panel/InkingStaticPanel.h`、`panel/PanelData.h`。
- **API**：目前**只有** `InkingStaticPanel(InkingWidget* parent, const PanelData& data);`
  没有按名字构造的重载，也没有 `PanelLibrary`。
- **验收**（尚未满足）：存在按名字取数据的入口与"取不到"的计数观测口，形态对照 `InkingStaticButton::FindData` / `UnknownNameCount`。
- **偏离**：**整条未落地**。原话是"生成器目前硬绑 Button，所以这一轮面板的配置在代码里手写"。
  别把 `PanelData` 的手写构造当成 A2 完成。

### A3 键盘焦点与键盘事件两条路径

- **目标**：键盘**状态**与**事件**两条路都给，且事件按**焦点**派发（与指针按位置派发对称）。
  状态给"按住方向键就移动"，事件给"按下 Enter 就提交"；边沿检测由框架做。
- **落点**：`input/KeyboardInput.h`、`input/InkingKeyboardTarget.h`、`scene/InkingScene.h:385-438`。
- **API**：`bool WantsKeyboardFocus() const noexcept`（默认 false）、`SetKeyboardFocus(InkingWidget*)`、
  `DispatchKeyboard()`、`GetKeyboardState()`；事件侧 `PushKeyDown/PushKeyUp/PushTextInput`。
- **验收**：① 只有 `WantsKeyboardFocus()` 为真的节点会在按下时拿到焦点；② 三条焦点规则成立
  ——点击获得、点别处就换、不可见/被摘除就释放；③ `DispatchKeyboard()` 必须排在 `DispatchPointer()`
  **之后**，否则"点进输入框并立刻打字"第一下会丢；④ 窗口失焦清空按住的键。
- **偏离**：没有 Tab 焦点遍历（`text/InkingTextInput.h:62` 明写"没做"）。

### A4 滚轮

- **目标**：滚轮是**一次性量**而非跨帧状态——一帧内累加、派发一次、帧末必清；同时允许容器
  声明"子树里的滚轮归我"，否则指针停在子按钮上时容器滚不动。
- **落点**：`scene/InkingScene.h:312-327`、`input/InkingPointerHandler.h:32-43`、
  `widget/InkingWidget.h:449-453`。
- **API**：`SetPointerWheel(float,float)`（一帧多次 = 累加）、
  `virtual bool HandlesSubtreeWheel() const noexcept`、`FindSubtreeWheelOwner()`（沿父链找最近声明者）。
- **验收**：① 一帧多个滚轮事件合并成一次 `onPointerWheel`；② 指针停在滚动容器的子按钮上，
  滚轮仍归容器；③ 没人消费也必须在帧末丢掉，不得攒到下一帧（否则会"切场景后莫名滚一下"）。
- **偏离**：动态组件不进派发，只能在 `Tick` 里读 `GetPointerWheelX/Y`——"静态推 / 动态拉"
  是两档输入方式的必然结果。

### A6 布局通道 / `MeasureLayout`

- **目标**：布局是**一条通道**，不是几何写入口。规则（`LayoutSpec`）住在父容器，孩子只写意愿
  （`LayoutItem`）；解出的矩形写进运行期几何，于是绘制 / 命中 / AABB / 子树变换都不知道"有布局"。
- **落点**：`dataStruct/InkingLayout.h`、`layout/InkingLayoutSolver.h`、`widget/InkingWidget.h:632-679`。
- **API**：`virtual LayoutSize MeasureLayout(const LayoutConstraints&) const`（**纯函数**）、
  `Relayout()`（幂等）、`RelayoutParent()`、`detail::layout::Solve(spec, containerSize, items)`。
- **验收**：① 重复调用 `MeasureLayout` 结果相同且不标任何脏；② `Relayout()` 在结果与当前几何
  一致时**一个脏都不标**（用 `GetStructureGeneration()` 钉）；③ 标签换长文字后父容器重排、兄弟让位。
- **偏离**：**框架不做隐式重排**。内容变了只标重绘脏，必须由组件自己喊 `RelayoutParent()`；
  代价是可能忘喊，收益是"什么时候重排"始终可见。溢出时求解器一律按 Start 排，交给裁剪 / 滚动。

### A7 文字排版

- **目标**：把一段文本排成若干行并算好占多大——换行、省略、行位置在同一层完成。**不含 SDL**，
  宽度靠测量回调，所以能用"每码点固定宽"的假测量做数值自检。
- **落点**：`dataStruct/InkingTextLayout.h`。
- **API**：`TextLayoutResult LayoutText(const TextLayoutRequest&, const TextMeasureFn&)`（纯函数）、
  `float LineOffsetX(float lineWidth, float availableWidth, TextAlignH align)`。
- **验收**：① 显式 `\n` 永远断行、`Word` 不切开英文整词、超宽单词退化成 `Anywhere`、行首空白丢掉；
  ② `Ellipsis` 是"先回退字符再加 `…`"，加完仍超宽即不通过；③ 空串返回 `empty = true` 且尺寸只剩 padding。
- **偏离**：不为了塞进 `maxHeight` 缩字号；不做基线对齐（行高只由 `lineSpacing` 与单行测量高度决定）。

### A8a 子孙不进命中表

- **目标**：容器声明 `OwnsSubtreeHit()` 为真后，子孙**不再进全局命中表**——它们的绝对包络会随容器
  变换过期。改由容器登记**一条**盖住整棵子树的包络，查询落到容器上后再"深挖一层"。
- **落点**：`widget/InkingWidget.h:389-425`、`panel/InkingStaticPanel.h:148-166`、
  `src/scene/InkingScene.cpp:rebuildHitTable`。
- **API**：`virtual bool OwnsSubtreeHit() const noexcept`、`virtual InkingWidget* HitTestNode(float,float)`、
  `virtual ShapeBounds GetHitEnvelope() const`。
- **验收**：① 容器开合时**命中表条目数一条都不变**；② 容器里的按钮/输入框仍收得到 hover、点击、
  滚轮，也拿得到键盘焦点；③ 子树里只有 `PassThrough` 命中时返回 `nullptr`，让调用方保留"容器自己"；
  ④ 包络必须覆盖**所有可能状态**（抽屉开着和关着差一个身位）。
- **偏离**：`HitTestNode` 非 `const`（返回的指针要拿去派发）；层级深了是逐层问，没有缓存。

### A8b 局部层级（已做：树序 + 兄弟内 z）

- **目标**：z 序从"全局一条"变成"从根向下的 z 序列按字典序比较"，让**容器里的东西永远在
  容器自己之上**（面板底色不再盖住自己的标题和按钮）。
- **落点**：`src/widget/InkingWidget.cpp` 的 `IsAbove`（唯一改动点，四个调用方一处没动）。
- **做法**：等价规则三条——① 父先于子；② `zIndex` 只在兄弟之间生效；③ 同 z 看注册序号。
  实现是"对齐深度 → 上溯到分叉点 → 比兄弟"，不拼序列、不分配内存，结果仍是**严格全序**。
- **验收**（`inking_demo_selftest` 的「层级」段，19 项全绿）：父 z=20 / 子 z=1 时子在上；
  兄弟按 z；card 的子孙 z=100 也盖不过 z=20 那一支；绘制列表顺序与 `IsAbove` 同源；
  中心点命中子面板；**真渲染一帧后中心像素是子面板的颜色**。
- **未做（真正的堆叠上下文）**：**子节点逃出父级**——子 z 再大也盖不过父级的兄弟。
  要做到这一点需要"提升到根层"的 portal 层，那时 §10.5 规则 4 的**跨簇条目仲裁**
  才需要补回来（现在层级是全序，任何两个条目都可比，所以不需要）。
- **连带变化**：`Select` 展开仍然抬高**自己**（`expandedZIndex = 200`），这在局部层级下
  依旧是正确写法；`MenuBar` / `OptionBar` 里给子节点写 `父z + 1` 从此是冗余的（无害）。

### A11 图片 = 一个没有 `onClicked` 的按钮

- **目标**：img 的逻辑就是"没有写入 `onClicked` 事件的 button"——形状、外观、描边阴影、锚点、
  命中全都要，只有三态与回调不要。所以不新增一套绘制，**复用按钮那条**。
- **落点**：`image/ImageData.h`、`image/InkingStaticImage.h`、`button/ButtonPaint.h`
  （`detail::PaintButtonAppearance`）。
- **API**：`struct ImageData : public ButtonData { bool runtimeAsset = false; };`
  `InkingStaticImage(InkingWidget*, const ImageData&)`。
- **验收**：① 相同形状/描边/阴影下图片与按钮**像素一致**；② 只有 `normal` 一态有意义，
  配置里出现 `hover` / `onclicked` / `text` 时生成器报错；③ 照常参与命中但没有任何回调；
  ④ 图读不到时保留上一次的图（首次退回按颜色填），不得把画面变成洞。
- **偏离**：`docs/zh/api/InkingComponents.md` 写它继承 `InkingButtonLike`，而头文件是
  `class InkingStaticImage : public InkingStaticWidget`（自带 `GetDisplayColor` / `GetDisplayTransform`）。
  **以头文件为准**。

### A12 画布自己的内容视口

- **目标**：画布在语义上就是**视口**——绘制器按本地设计坐标画，越界看不见；内容由注入的绘制器决定，
  框架不认识"你要画什么"。这也是"把绘制入口提升成公开 API"的落点：加一种画法不用框架长字段。
- **落点**：`canvas/CanvasData.h`（`clipContent`）、`canvas/InkingStaticCanvas.h`、
  `canvas/CanvasContext.h`、`canvas/InkingCanvasPainter.h`。
- **API**：`bool clipContent = true`、`SetClipContent(bool)`、`PushClip(x,y,w,h)`（与画布边界、
  祖先容器取交集）、`Invalidate()`（内容变了才重调绘制器）。
- **验收**：① 裁剪区是**本体外接矩形**，可再叠一层 `PushClip` 做更窄视口；② 绘制器**不是每帧被调**
  ——只有 `Invalidate()` 后那次重绘才调，其余帧回放命令表（`GetPaintCount` / `GetReplayCount` 可断言）；
  ③ 命中只判形状本体，**内容不参与命中**。
- **偏离**：圆形画布的四角仍画得出内容（要按形状剪内容得先离屏渲染，第二版）；内容热区同样第二版。

### A13 外部显示变换

- **目标**：给"运行期把整块东西挪走/旋转/缩放"一条**不走几何**的写入口。最终显示 =
  外部变换 ∘ 组件自己那份（三态过渡 / 开合动画照旧，外面的拖动叠在它之上）。
- **落点**：`widget/InkingWidget.h:507-569`、`dataStruct/InkingTransform.h`。
- **API**：`virtual bool AcceptsExternalTransform() const noexcept`（默认 false）、
  `SetDisplayTransform(const TransformSpec&)`、`ClearDisplayTransform()`、
  `CombineWithExternal(const TransformSpec& own)`。
- **验收**：① 没声明的组件调用被**拒绝并计数**（`GetDisplayTransformRejectedCount`），不静默；
  ② 同一份变换重复设置返回 false 且不标脏（`GetDisplayTransformRevision`）；③ 视觉挪到哪、
  点就得到哪（正逆严格互逆）；④ 本地几何一个字节都不动。
- **偏离**：这是框架里**唯一**一条会动命中表的显示通道——任意拖动的包络并集算不出来，每次真改都要
  标结构脏、下一帧整表重烘。代价是**不改变占位**：兄弟不让位，布局与滚动范围都不认它。
  `InkingStaticVideo` 未声明支持（`video/InkingVideo.h:33`）。

### A14 外部 PCM 流

- **目标**：视频音轨是"解码器边解边给的 PCM"，不是文件，走不了 `AudioLoad`。给它一条流轨道：
  解码侧推 PCM，混音器按设备格式重采样并混音，音量/暂停/位置全归音频层；音画同步主时钟就是它的位置。
- **落点**：`audio/InkingAudio.h:243-304`。
- **API**：`AudioOpenPcmStream(int sampleRate, int channels)`、
  `AudioPushPcm(PcmStreamId, const void*, std::size_t)`（返回**没吃下**的字节数）、
  `AudioPcmStreamWantsData(PcmStreamId)`、`AudioPcmStreamPositionSeconds(PcmStreamId)`。
- **验收**：① 返回未吃下的字节数形成**背压**——缓冲满了就留着等 `WantsData` 再推；② 暂停时位置冻住
  （与 BGM 同语义）；③ 无效句柄一律照空转处理，调用方不必写分支；④ 全部在主线程调。
- **偏离**：格式固定 **S16 交错**，不做浮点 / 平面格式；没有"worker 直接推"的加锁路径。

## 3 B 系列：基础组件

B 系列是"单个组件内部"的深度：不是新能力，而是把某一件事做到可验收。
其余编号（B1–B3、B5–B6、B9–B10、B13）见 §8.2，这里只收被本文件点名的三条。

### B4 批次与「带纹理提交次数」计数

- **目标**：把"每个节点 immediate 提交"改成**一帧成批提交**——形状顶点先追加进当前批，连续的同种
  提交合成一次调用；批按**纹理句柄**分（纯色是 `nullptr`），纹理变了就刷上一批。后半是字形图集打包。
- **落点**：`render/InkingBatch.h`、`render/InkingGlyphAtlas.h`。
- **API**：`BatchStats { geometrySubmissions, textureDraws, batchFlushes, ... }`、
  `AppendTexturedGeometry(SDL_Texture*, const SDL_Vertex*, size_t, const int*, size_t, float, float)`、
  `GlyphAtlas::Insert(...) -> Slot { valid }`。
- **验收**：① 同一张纹理**连续**画 N 笔，`textureDraws` 只 +1；② 顺序与调用顺序一致，只合并连续的
  同种提交、绝不重排（重排会造出"画的是一个、点的是另一个"）；③ 图集放不下返回 `valid == false`，
  打包器不做淘汰策略（那是缓存层的事）。
- **偏离**：文字走 `SDL_RenderTextureRotated`，它是**批次边界**而不是批次内容；顶点缓存与渲染器无关
  （只有纹理才要跟着渲染器销毁）。

### B7 单行文本框

- **目标**：可编辑文本框——光标、选区、键盘编辑、剪贴板、IME 提交。它**继承 `InkingStaticLabel`**：
  排版（A7）、撑高（A6）、文字绘制、多语言查表全是现成的，只加编辑状态与键盘接线。
- **落点**：`text/InkingTextInput.h`、`input/TextEditState.h`。
- **API**：`struct TextInputData : public LabelData`、`TextEditState& Edit()`、
  `bool HandleKey(const KeyEvent&)`、`void HandleText(const std::string&)`（手动喂 IME 结果）、
  `static void SetClipboardHandlers(ClipboardGet, ClipboardSet)`。
- **验收**：① `WantsKeyboardFocus()` 为 true（不重写就永远拿不到焦点，而且**不报错**）；
  ② 构造里显式 `SetHitPassThrough(false)`——输入框要点击定位光标，标签默认穿透；
  ③ IME 提交走 `onTextInput`，自检覆盖这条路；④ 每次编辑后 `syncToLabel()` ⇒ 重排 + 重绘。
- **偏离**：**单行**。`wrap` 被强制 `NoWrap`，粘贴时换行丢掉；不做多行、撤销/重做、Tab 遍历、候选窗定位。

### B12 滚动容器

- **目标**：给容器一套**纯几何**的滚动——范围、夹取、轨道、滑块、点轨道跳过去都在纯函数里；
  面板只负责把指针事件接上去，滚轮/拖滑块/点轨道共用同一份几何。
- **落点**：`dataStruct/InkingScroll.h`、`panel/InkingStaticPanel.h:78-260`、`panel/PanelData.h`。
- **API**：`ScrollSpec scroll`、`UpdateScrollRange()`、`WheelScroll(float,float)`、
  `ScrollBarMetrics GetBarMetrics(bool vertical)`、`PressScrollAt(float,float)`。
- **验收**：① 滚动不改变内容的本地几何，只改显示/裁剪用的位移；② 裁剪窗口是**容器本体**、不含滚动位移
  （否则内容滑出去还看得见）；③ 配了滚动的容器 `HandlesSubtreeWheel()` 为 true；
  ④ 滚动条那一条要从内容视口里扣掉。
- **偏离**：滚动位移进入的是"容器到子节点"的复合变换，所以**子孙绝对包络不能进命中表**——
  这与 A8a 是同一条约束（`detail/InkingGeometry.h:49` 记着这个坑）。

## 4 C 系列：预制件

预制件 = **用现成组件组装出来的复合件**，**不新增任何底层能力**：没有一处新的绘制/命中/布局代码，
全白拿自 Panel、布局通道、事件槽。存在理由只有一个：`CXXCSS` 只描述**单层**组件，
"容器 + 子项"一直没有归处。

### 4.1 C 系列的三个通用约定

原文在 `prefab/OptionBar.h:55-59`，三条对 C1–C5 全部适用：

1. **数据驱动**：项列表变了就 `SetItems`，内部 `ClearChildren()` + 重建；没变就**什么都不做**
   （幂等：节点不重建、脏不标）。
2. **外观可配置**：默认吃 json（如 `CXXCSS/Button/optionItem.json`）或调用方自己那份 `*Style`；
   样式里的 `panel.layout` 是**唯一**的排列真相，不在别处再抄 axis / gap / padding。
3. **不引入浮层、不引入输入消费**：显示/隐藏就是 `SetVisible(false)`；"点外部关闭"由调用方约定
   （别处一点就隐藏），预制件不接管全局输入。

### C1 选项栏（`OptionBar`）

- **目标**：`layout` 排列的按钮组——一个 Panel 容器 + 每个选项一个按钮。"加一个选项" =
  往 `std::vector<OptionItem>` 里加一个对象。
- **落点**：`prefab/OptionBar.h`、`prefab/OptionBarData.h`。
- **API**：`SetItems(std::vector<OptionItem>)`（幂等）、`GetBuiltItemCount()`、`Activate(std::size_t)`、
  `InkingEvent<const std::string&, std::size_t> optionClicked`。
- **验收**：① 重复喂同一份列表时 `GetBuiltItemCount()` 不变、脏不增；② 项数/顺序/每项矩形可用
  `GetItemRect` 直接钉住；③ 禁用项**不发**回调；④ 选项栏可互相嵌套——由调用方在 `optionClicked` 里
  再建一个实现，框架不知道"嵌套"。
- **偏离**：几百项时 `SetItems` 是 O(n²)（`AddChild` 每次查重），该改用 C3。不做浮层定位、
  选项栏栈、项的键盘导航、子栏自动回退。"禁用"没有第四态：只落成"回调不发 + 文字调暗"。

### C2 菜单栏（`MenuBar`）

- **目标**：窗口顶部一条横向容器，替掉系统标题栏/菜单。可放按钮、图片和**任意 `InkingWidget`**，
  所以核心入口只有 `AddItem`。另一半是自绘标题栏：按住空白拖窗口 + 右上角最小化/关闭。
- **落点**：`prefab/MenuBar.h`、`prefab/MenuBarData.h`。
- **API**：`AddItem(std::unique_ptr<InkingWidget>)`、`AddButton(look, text, onActivate, shortcut)`、
  `AddCaptionButtons()`、`HandlesSubtreeWheel() override`。
- **验收**：① "内容偏左 + 窗口按钮贴右上角"同时成立——内部靠一根 `layoutItem.fillMain = true`
  的弹簧吃掉主轴剩余空间，不新增布局词汇；② 滚轮归菜单栏（窗口缩放），指针停在菜单按钮上也不被吃掉；
  ③ 全屏隐藏只记"被这条规则藏起来了"，退出只恢复那一次，**不覆盖**业务自己调的 `SetVisible(false)`。
- **偏离**：不做无边框下自绘改尺寸、双击最大化、系统级吸附。第 4 参把菜单项绑到全局快捷键，
  命中后那一下按键被 `ConsumeFrameEvent` 吃掉，不再推给焦点组件。

### C3 列表（`ListView` / `ListRow`）

- **目标**：`ScrollView`（B12 的 `scroll` 段）+ 布局 + 动态项组装；窗口化时**只建可见行**。
- **落点**：`prefab/ListView.h`、`prefab/ListRow.h`、`prefab/ListViewData.h`。
- **API**：`SetItems(std::vector<ListItem>)`、`SyncVisibleWindow()`、`GetBuiltRowCount()`、
  `GetRowNode(int)`、`ScrollToItem(int)`、`InkingEvent<const std::string&, int> itemActivated`。
- **验收**：① **硬判据**：1000 项时 `GetBuiltRowCount()` 只有十几二十个，`GetItemCount()` 才是 1000；
  ② 可见行落在正确位置、滚动范围仍是全部项——靠前后两个占位块分别"顶下去"和"补总高"；
  ③ `rowHeight > 0`（窗口化靠它算位置）；④ 滚动与裁剪由预制件强制打开，"滚出去的行点不到"白拿。
- **偏离**：不做拖拽重排、行内编辑、多列/排序、树形缩进。`ListRow` 的选中态是**一层覆盖**
  （换外观那一笔的颜色），不是按钮的第四态。

### C4 下拉（`Select`）

- **目标**：收起态一个按钮（当前值 + 占位 + 展开指示），展开态**就地**显示一个 C1 选项栏
  ——不引入浮层、不引入输入消费。
- **落点**：`prefab/Select.h`、`prefab/SelectData.h`。
- **API**：`SetOptions(std::vector<OptionItem>)`、`SetSelected(int)`、`SetExpanded(bool)`、
  `GetVisibleOptionCount()`、`SetFilterText(const std::string&)`、`RawIndexForVisible(int)`。
- **验收**：① 展开/收起只改可见性（骨架变化 ⇒ 命中表重烘），"展开的那一列点得到、收起后点不到"；
  ② 过滤后 `RawIndexForVisible` 仍能映射回原索引；③ `Select` 自己**不能** `clipChildren`。
- **偏离**：点外部自动关不做（只提供 `SetExpanded(false)`）；展开容器盖不住容器外的兄弟（A8b 暂缓），
  用 `expandedZIndex = 200` 绕开；过滤是"每渲染帧比一次文本"的轮询，因为 `InkingTextInput`
  没有"文本变了"的事件槽。

### C5 文件选择（`FilePicker`）

- **目标**：四块拼起来——路径框（B7）→ 快捷位置（C1）→ 目录内容（C3）→ 按钮栏（C2）。
  路线是**组装自绘**：不动框架、跨平台一致、可断言。
- **落点**：`prefab/FilePicker.h`、`prefab/FilePickerData.h`。
- **API**：`bool SetDirectory(const std::filesystem::path&)`、`GoUp()`、`EnterDirectory(int)`、
  `bool Accept()`（按 mode 决定要什么）、`std::vector<std::string> GetSelectedPaths()`（UTF-8）。
- **验收**（三条必须一起守）：① **路径全程 `std::filesystem::path`**（Windows 上就是宽字符）
  ——窄串构造 `path` 在中文目录里会**抛异常**，且发生在构造那一步，`error_code` 重载救不了；
  往界面/事件里递的字符串先过内部工具 `PathToUtf8`；② 枚举带 `error_code`，权限不足/路径消失不得崩；
  ③ `Accept()` 分清三种模式：开文件/存文件要**选中的文件**，选目录要**当前目录**。
- **偏离**：不做原生对话框、驱动器列表、书签/最近位置、文件名与目录分开输入、多列/图标视图。

### C6 定点浮窗（`InkingFixedPopup`）

- **目标**：一个**固定几何、自己定位**的动态面板——`OpenAt(点)` 在定点弹出，靠近边缘自动翻到
  对侧，比窗口还大时贴边并允许溢出。点击**吃掉**（不透到下面），点外部默认关闭。
- **落点**：`panel/InkingFixedPopup.h` + `src/panel/InkingFixedPopup.cpp`；落位算术在
  `detail/InkingPopupPlacement.h`（纯函数，可单测）。
- **API**：`OpenAt(x, y)` / `SetTarget` / `Close` / `IsOpen` / `AddItem` /
  `SetCloseOnOutsideClick` / `SetTakeFocusOnOpen` / `SetClampBounds` / `GetResolvedSideX/Y` /
  `IsClampedX/Y`；事件 `opened` / `closed`；扩展点 `onPopupOpened` / `onPopupClosed` /
  `onOutsidePressed`。
- **验收**：① 落位四方向 + 两轴独立 + 超宽夹紧（`check_popup_placement`，7 条数字断言）；
  ② 打开后 `GetX/GetY` 等于算出来的位置；③ 内部静态按钮能被 `QueryHit` 命中（靠 `QueryHit`
  对动态容器深挖）；④ 空白区 `kind == Block`；⑤ 外部按下后 `IsOpen() == false`。
- **偏离**：**这是 C 系列里第一个引入浮层与输入消费的件**（4.1 的第 3 条到 C5 为止适用）。
  它不提供穿透开关（"点透传到下层"未排期）；不做开合动画，也不做 portal——父级的兄弟仍盖不住
  （`InputDesign.md` §15 第 3 条）。

### C7 菜单浮窗（`MenuPopup`）

- **目标**：基于 C6 的竖排菜单——`AddButton` 加项（可绑**局部快捷键**）、`AddItem` 挂任意件、
  `OpenAt(点)` 打开。定位 / 吃掉 / 点外关闭全部继承自 C6，本类只补布局、项表与快捷键。
- **落点**：`prefab/MenuPopup.h`、`prefab/MenuPopupData.h`、`src/prefab/MenuPopup.cpp`。
- **API**：`AddButton(look, text, action, shortcut)` / `AddItem`（含工厂版）/
  `BindLocalShortcut` / `GetItemCount` / `GetItemRect` / `GetItemNode` / `GetActivatedCount`；
  扩展点 `onMenuActivated`（项被激活后、关闭之前）。
- **验收**：① 相邻项 y 差 = `itemHeight + gap`；② 容器宽 = `itemWidth + padding×2`；
  ③ `TriggerClick` 触发一次回调并自动关闭；④ 局部快捷键命中一次并关闭；⑤ 焦点被夺走后
  下一次指针变化就取回；⑥ 重复绑定、与全局表冲突都被拒（WARN，不静默）。
- **偏离**：局部快捷键是**焦点模态**——菜单开着才存在，且打开期间吃焦点。这与 `MenuBar`
  的全局表**同名不同义**，别混用。不做子菜单、方向键导航、开合动画。

## 5 D 系列：动画与过渡

### D-1 过场段各自带时长；动画绝不改本地几何

- **目标**：两条硬约束。**① 过场段各自带时长**：`normal→hover` 用 `hover` 那份
  `transitionSeconds`（及其 `easing`），`hover→normal` 用 `normal` 那份——时长写在**目标状态自己身上**。
  **② 动画绝不改本地几何**：只插值**颜色**与**变换**两条通道，形状与尺寸一个字节都不动。
  这是静态档的前提：几何不变 ⇒ 命中表不用重烘 ⇒ 静态组件能一直待在表里。
- **落点**：`button/ButtonData.h:78-99`、`button/ButtonLook.h:51-70`、`panel/PanelData.h:46`、
  `panel/PanelLook.h:43`。
- **API**：`ButtonAppearance::transitionSeconds` / `easing` / `transform`；
  `ButtonLook::AdvanceResult Advance(float deltaSeconds) noexcept`。
- **验收**：① 同一段 0.15s 过渡，换曲线不改时长；② 过渡中的 `_width`/`_height`/锚点/偏移与过渡前
  逐位相同；③ 中途再切三态**不跳**——`BeginTransition` 每次把**当前显示值**当起点，所以打断天然平滑
  （"不做打断过渡"= 不为它加机制，不是"打断时会跳"）。
- **偏离**：`T-5` 之后按钮多了"每态尺寸"，走**显示尺寸**通道（`displayWidth`/`displayHeight`），
  本地几何仍不动；代价是显示尺寸变化会标**命中脏**，由 `GetHitEnvelope` 上报"三态包络的并集"兜住。
  面板比按钮多一层收益：变换作用于**整棵子树**（`GetSubtreeTransform`），子节点一个字都不用改。

## 6 T 系列：纹理与命中

T 系列收口两件事：**"字符串 → 像素"**（T-1/T-2/T-3）与**"点 → 谁"**（T-4）。两条链路都要求
"只有一份定义"，否则会出现"画的是圆角、点到的是直角"这类两边单独看都对、合起来才错的 bug。

### T-1 形状登记不全就是漏命中

- **目标**：形状层（`ShapeKind`）是渲染、命中、包围盒**三者共同的唯一定义**。命中表按**包围盒**登记格子，
  所以包围盒必须覆盖实心区域；少算一块，那一块就永远点不到——不报错，只是"偶尔点不上"。
- **落点**：`dataStruct/InkingShapeSpec.h:828-944`（`ShapeContains` / `GetShapeBounds`）、
  `detail/InkingHit.h`。
- **API**：`bool ShapeContains(const ShapeSpec&, float x, float y, int width, int height)`、
  `ShapeBounds GetShapeBounds(const ShapeSpec&, int width, int height)`。
  典型坑是 `Line`：线段本身高度为 0，包围盒必须按 `thickness` 撑开，圆头/方头还要按 `cap`
  外扩 `thickness / 2`（`InkingShapeSpec.h:864-873`）。
- **验收**：① 13 种 `ShapeKind` 都有归属——`Rect`/`RoundedRect`/`Ellipse`/`Capsule`/`ChamferRect`/`Superellipse` 共用"满矩形"分支，其余七种各算各的；② 轮廓采样与 `ShapeContains` 同源——自检 `ReconcileShapeMesh()` 对 8 种形状逐格对账，分歧必须为 0；③ 新增形状必须同时补 `GetShapeBounds` 分支，否则落进 `default` 用整框（保守、不漏，但也没紧包络）。
- **偏离**：`Ring`/`Arc`/`Star` 一律按外接圆登记（保守）；`PolygonPath` 按顶点实算。
  包围盒偏大只多几个候选，**不会误命中**。

### T-2 缓动只改变进度

- **目标**：缓动只把"线性进度"映射成"实际画出来的进度"，**时长、起点、终点都不动**；端点必须精确
  ——`k <= 0` 给 0、`k >= 1` 给 1，不能指望调用方补偿。
- **落点**：`dataStruct/InkingEasing.h`、`button/ButtonLook.h:313-317`。
- **API**：`enum class Easing { Linear, EaseIn, EaseOut, EaseInOut }`、
  `float ApplyEasing(Easing easing, float k)`。
- **验收**：① `ApplyEasing(e, 0) == 0`、`ApplyEasing(e, 1) == 1`（精确相等）；② 换曲线不改时长；
  ③ 自检拿 `displayColor` / `displayTransform` 验"缓动真的生效"，而不是拿进度验。
- **偏离**：不做自定义 `cubic-bezier`（要解析四个控制点，还要写进文档）；用的是**二次**曲线，
  与 CSS 的 `ease` / `ease-in` **不是同一组数**，别指望逐帧对上。曲线取**目标状态**那份，与时长的来源相同。

### T-3 纹理层落地

- **目标**：把一段文字变成一张纹理并缓存——渲染器只认纹理；"文字 → 像素"烘一次、之后复用；
  缓存与失效收在一处，才不会"某条路径忘了失效"。同时给出**帧末标脏 / 重绘信号**。
- **落点**：`render/InkingText.h`、`button/ButtonLook.h:262-347`、`scene/InkingScene.h:209-239`。
- **API**：`MeasureText(const TextRequest&)`、`PaintText(renderer, request, width, height,
  magnification, pixelX, pixelY, transform)`、`struct AdvanceResult { bool repaint; bool hit; }`、
  `ConsumeFrameDirty()`。
- **验收**：① 缓存真的生效——`TextCacheStatsSnapshot()` 的 `cacheHits`/`texturesCreated` 可断言
  （画出来一模一样，只有计数抓得住白干活）；② 纹理按 `fontSize × magnification` 的**设备像素**栅格化：
  比例 2 下墨迹宽高约为比例 1 的 2 倍，而**布局原点与设计字号占位不变**；③ 换色**不重烘纹理**
  （颜色走 `SDL_SetTextureColorMod`），但像素结果必须对；④ 渲染器销毁前先 `ReleaseTextCache()`。
- **范围修订**：`TextBox` **只留契约、不做实现**（`InkingText.h:148-168`）。理由是文本框以后要接
  **多语言**（占位符 → 各语言的串 → 切换时机），现在把数据面定死等于给那一轮预埋一次推翻。
  留给那一轮的四条契约：以 `TextRequest` 为数据面、多语言只换上游、切语言 = 缓存未命中重烘、默认不吃点击。
- **偏离**：SDL3_ttf 是**可选**依赖；没接上或找不到字体时退化成"占位方块"，关掉 TTF 也能编、也能跑。

### T-4 命中：烘焙表 + 三态，同一帧最后统一消费

- **目标**：静态层查**烘焙好的表**（簇 + 格子 + CSR 候选），动态层自判（不进表），两者按**同一套
  层级规则**（`IsAbove`：树序 + 兄弟内 z，A8b）合并成三态；脏标记在**帧末统一消费**，不是"谁改谁立刻处理"。
- **落点**：`scene/HitTable.h`、`scene/InkingScene.h:209-282`、`detail/InkingHit.h`。
- **API**：`enum class HitKind { Block, PassThrough, Miss }`、
  `struct HitResult { HitKind kind; InkingWidget* target; }`、`HitResult QueryHit(float, float)`、
  `void ConsumeFrameDirty() noexcept`。
- **验收**：① 三态齐全：`Block`（到此为止，遮挡 ≠ 未命中）/ `PassThrough`（有东西但让过）/ `Miss`；
  ② 表按**保守包络**登记，格子只缩小范围，精判仍由节点自己的 `HitTest` 回答（格子粗了只多候选，
  **不会误命中**）；③ 隐藏节点**既不进表、也不留洞**；④ 容器开合时 `GetHitTableEntryCount()` 不变；
  ⑤ 一帧连改三次几何只在该帧末处理一次。
- **为什么帧末统一消费**：一帧里可能连着 `Resize` 三次、`ChangeZIndex` 两次，立刻处理就是重烘五次表。
  三个消费点各管一段：**命中脏 → 帧首**（重建列表/重烘表）、**指针脏 → 帧末**（只清标记，不重烘表）、
  **重绘脏 → 帧末**（记账 + 计时清零）。
- **偏离**：**没有溢出桶**（`§10.5 规则 2`）——组件构造即登记进绘制列表，"新增"表现为"表脏了"；
  **没有跨簇条目仲裁**（`§10.5 规则 4`）——那是给"存在不可比层级"的模型准备的；我们的局部层级
  （树序 + 兄弟内 z）仍是全序，任何两个条目都可比，所以不需要。
  动态组件不进表、只留"洞"作性能开关：洞漏登也只是多问一次，**不会点错**。

## 7 任务纪律（r2 与"一次只做一件事"）

**条文**：**一次运行最多完成一个任务。** 一个任务 = 一个编号 = 一条可独立验收的改动。
做完、验完、记完，再开下一个；不在同一轮顺手改第二个组件、第二个能力。

**代码里的证据**：`panel/PanelLook.h:14-22` 最清楚——`ButtonLook` 与 `PanelLook` 的过渡推进逻辑重复，
本该抽 `TransitionTrack`，但 A1 **没有抽**：① 抽它要动**唯一已验证的组件**（Button），而测试在 A1 之前
被整块移除，没有回归网时动它是拿"已经对的东西"换"更整齐的东西"；② `r2` 一次只做一件事。
这不是"忘了"，而是把重复记成**已知债务**并写明触发条件："第三个需要过渡的组件出现时，就该抽出来
（那时有两条调用方 + 自检）"。

**它约束什么**：

- **不夹带重构**：修 bug 只修 bug；想重构就单开编号、单独验收。
- **不跨编号改接口**：一个任务只动自己的落点；动了别人的头文件，就按别人的判据重验。
- **先有判据再有改动**：能变成数值判据的自己验（像素/坐标/数量/顺序/计数），需要"看/摸"的交给用户。
- **保留基线**：改渲染路径前先存逐字节基线，改完比对，不靠"看起来一样"。
- **编号与状态同步**：没做就是没做（A2 / A8b 原样保留），不把"部分可用"写成"已完成"。

**代价**：短期看它慢——重复代码会留着（`ButtonLook` / `PanelLook`），相邻的坑会明知而不填。
换来的是每一步都可回退、可归因：出问题时能定位到某一个编号的改动，而不是"上一轮动了很多地方"。
这条纪律与"编号稳定"是一体两面：编号让任务可被指认，`r2` 让指认始终对应一次真实改动。

## 8 编号与代码的对照表

### 8.1 本文件收录的编号

| 编号 | 落点头文件 | 状态 |
| --- | --- | --- |
| A1 | `input/InkingPointerHandler.h`、`scene/InkingScene.h` | 已落地 |
| A2 | `panel/InkingStaticPanel.h`（仅手写 `PanelData`） | **未落地** |
| A3 | `input/KeyboardInput.h`、`input/InkingKeyboardTarget.h` | 已落地 |
| A4 | `input/InkingPointerHandler.h`、`scene/InkingScene.h` | 已落地 |
| A6 | `layout/InkingLayoutSolver.h`、`widget/InkingWidget.h` | 已落地 |
| A7 | `dataStruct/InkingTextLayout.h` | 已落地 |
| A8a | `widget/InkingWidget.h`、`panel/InkingStaticPanel.h` | 已落地 |
| A8b | `scene/HitTable.h`、`widget/InkingWidget.h`（仅注释与绕行） | **未落地**（暂缓） |
| A11 | `image/InkingStaticImage.h`、`image/ImageData.h` | 已落地 |
| A12 | `canvas/InkingStaticCanvas.h`、`canvas/CanvasContext.h` | 已落地 |
| A13 | `widget/InkingWidget.h` | 已落地 |
| A14 | `audio/InkingAudio.h` | 已落地 |
| B4 | `render/InkingBatch.h`、`render/InkingGlyphAtlas.h` | 已落地 |
| B7 | `text/InkingTextInput.h` | 已落地（单行；多行未做） |
| B12 | `dataStruct/InkingScroll.h`、`panel/InkingStaticPanel.h` | 已落地 |
| C1 | `prefab/OptionBar.h` | 已落地 |
| C2 | `prefab/MenuBar.h` | 已落地 |
| C3 | `prefab/ListView.h`、`prefab/ListRow.h` | 已落地 |
| C4 | `prefab/Select.h` | 已落地 |
| C5 | `prefab/FilePicker.h` | 已落地 |
| D-1 | `button/ButtonLook.h`、`panel/PanelLook.h` | 已落地 |
| T-1 | `dataStruct/InkingShapeSpec.h` | 已落地 |
| T-2 | `dataStruct/InkingEasing.h` | 已落地 |
| T-3 | `render/InkingText.h`、`button/ButtonLook.h` | 已落地（`TextBox` 只留契约） |
| T-4 | `scene/HitTable.h`、`scene/InkingScene.h` | 已落地 |
| r2 | `panel/PanelLook.h:21`（引用点） | 纪律，无代码落点 |

### 8.2 只在注释里出现的外延编号

这些编号正文不单列（或不在本次要求的集合里），但代码确实按编号引用它们；列在这里以免再次"引用找不到落点"。

| 编号 | 含义（按引用上下文） | 落点 | 状态 |
| --- | --- | --- | --- |
| A5 | 父子裁剪：画出来被剪、点起来也被剪（同源） | `InkingWidget::HasSubtreeClip`、`panel/InkingStaticPanel.h:196` | 已落地 |
| A9 | 通用事件槽：组件声明事件、使用者挂回调，不必再改底层 | `event/InkingEvent.h` | 已落地 |
| A10 | 运行期增删子节点：拥有型 `AddChild` / 借用型两条路 | `widget/InkingWidget.h:326` | 已落地 |
| B1 | 按形状裁剪的纹理填充 + `fit` 适配模式 | `dataStruct/InkingImageFit.h`、`render/InkingDraw.h` | 已落地 |
| B2 | 任意多边形 / 网格补全（凹多边形三角化） | `render/InkingMesh.h:80-89` | 已落地 |
| B3 | 描边/阴影两笔装饰，不参与命中 | `dataStruct/InkingDecoration.h`、`render/InkingDraw.h` | 已落地 |
| B5 | 形状字段扩展（`chamfer` 等），网格需同步 | `dataStruct/InkingShapeSpec.h`、`render/InkingMesh.h` | 已落地（曾漏字段） |
| B6 | 触摸：归一化坐标 × 窗口像素尺寸，只认第一根手指 | `input/MouseInput.h:66-99` | 已落地 |
| B9 | 只建可见项；后移，由 C3 接管 | `prefab/ListView.h:9` | 未单独落地 |
| B10 | 音频层：BGM + SE + 淡入淡出 + 暂停 + 三级音量 | `audio/InkingAudio.h:3` | 已落地 |
| B13 | 角色语音：一句话一条，新的打断旧的（`Voice` 通道） | `audio/InkingAudio.h:196` | 已落地 |
| T-5 | 每态显示尺寸（走显示通道，不改本地几何） | `button/ButtonData.h:117`、`button/ButtonLook.h:107` | 已落地 |
| T-6 | 一帧成批提交（几何批次） | `render/InkingBatch.h:3` | 已落地 |
| T-7 | 子树变换复合 | `widget/InkingWidget.h:210`、`detail/InkingGeometry.h` | 已落地 |

### 8.3 编号连续性

A 系列 `A1`–`A14` 全部有归属（`A5`/`A9`/`A10` 只在 §8.2 出现）；B 系列在注释里出现的是
`B1`–`B7`、`B9`–`B10`、`B12`–`B13`，`B8` / `B11` **未出现**，是否曾经存在不可考。

---

> 本文档对应版本：0.4.0（0.x 发布版）。
