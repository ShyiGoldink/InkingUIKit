# 变更记录（CHANGELOG）

格式：每个版本一节，分 **新增 / 变更 / 修复 / 文档 / 已知限制** 五类。
测试版本期间的条目**不承诺**跨版本兼容——升级前请逐条对照。

---

## 0.5.3 —— 2026-10-26（**当前：发布版**：FilePicker 按钮栏脱离 MenuBar + 内容高量准）

> 来源：GoldingCore 侧 appmanager 插件的两条实测反馈（全屏后「添加 / 取消」消失；
> 文件选择器首次没有滑块，进子目录再退回才有）。核实与取舍见
> `docs/zh/plans/2026-10-26-0.5.3-picker-buttons-and-scroll.md`。

### 变更

- **`FilePickerStyle::buttons`（`MenuBarStyle`）→ `buttonBar`（`PanelData`）**，
  `FilePicker::Buttons()` 的返回类型随之改为 `InkingStaticPanel&`。底部按钮栏不再
  复用 `prefab::MenuBar`：那是"贴着窗口的横条"，数据面捆着 `hideInFullscreen` /
  `zoomWheel`（两者默认都是 **true**）以及 `draggable` / `captionButtons`，实现里
  还订阅窗口的 `fullscreenChanged`、绑快捷键表。对话框的一行按钮只要"横向排两个
  按钮"，这些策略一条都不该继承——**布局该复用，策略不该复用**。排列口径不变
  （`Stack` + `Horizontal` + `justify=End` + `align=Center` + `gap 8` + `padding {0,4,8,4}`）。
- 新增观测口 `FilePicker::GetButtonCount()` / `IsButtonBarVisible()` / `GetButton(index)`
  ——自检靠它们断言"窗口策略没有被继承回来"。

### 修复

- **全屏后「确定 / 取消」消失**：按钮栏是 `MenuBar`，而 `MenuBarStyle::hideInFullscreen`
  默认 true ⇒ 构造时查一次 `IsFullscreen()`、之后订阅 `fullscreenChanged` 自行
  `SetVisible(false)`。换成普通面板后，全屏与它无关（启动即全屏、运行期切全屏、
  全屏状态下新建选择器三条路都验过）。
- **滚轮落在按钮行会缩放窗口**（同一条根因，未被上报）：`MenuBarStyle::zoomWheel`
  默认 true，`MenuBar::onPointerWheel` 直接调 `InkingWindow::ZoomBy`；按钮栏占选择器
  底部 40px，指针落在那一带滚轮 = 窗口跳档、列表不动。
- **首次填充没有滚动条**：`InkingStaticPanel::UpdateScrollRange()` 原先按
  `child->IsVisibleInTree()` 统计内容尺寸 ⇒ 在**隐藏的弹窗**里 `SetDirectory()` 建好的
  列表内容高恒为 0，而弹窗转可见之后没有任何人重量（`ListView::rebuildVisible` 在
  可见区间没变时就早退）⇒ 滑块不出现，只有换目录触发一次全量重建才量到。
  现在内容尺寸**不看可见性**（几何与布局本来就不看，"`SetVisible(false)` 的项仍占位"
  是既有规矩），隐藏期填的目录一样算得准。
- **内容高恒定少算面板自身的偏移**：`UpdateScrollRange()` 把**已是父级相对**的子项
  坐标又减了一次面板自己的 `GetX()/GetY()` ⇒ 列表在 FilePicker 里 y=70 时，37 项目录
  真值 962 量到 892、`rangeY` 592 而非 662，末尾那 70px 永远滚不到（0.5.2 的盘符栏
  把这个偏移推到 104）。现在直接取子项并集。

### 文档

- `docs/zh/api/InkingPrefab.md` §5 的"按钮栏（`MenuBar`）"改为普通横排容器，并补一句
  "不参与全屏隐藏 / 滚轮缩放"；`FilePickerStyle` 字段表同步改。
- `CHANGELOG` / `README` / `API.md` 版本横幅 0.5.2 → 0.5.3。

### 已知限制

- `FilePickerStyle::buttonBar` 只表达"这条多高、什么底、怎么排"，**不再**借道
  `MenuBarStyle`：需要标题栏式行为（拖动窗口 / 自绘最小化）的地方请直接用 `MenuBar`。
- 静态档几何仍在构造期定型：`buttonBar` 改尺寸要重建选择器。

---

## 0.5.2 —— 2026-10-25（发布版：C5 增补盘符栏）

### 新增

- **`FilePicker` 盘符栏**：快捷位置下面多一行窄按钮（"C:" "D:" …），点一下就把
  当前目录切到那个盘的根。新增 `FilePicker::EnumerateDrives()` /
  `RefreshDrives()` / `GetDriveCount()` / `GetCurrentDrive()` / `Drives()`；
  样式面加 `FilePickerStyle::drives`（一份 `OptionBarStyle`）、`showDrives`、
  `driveBarHeight`。落点 `include/prefab/FilePickerData.h`、`FilePicker.h`、
  `src/prefab/FilePicker.cpp`。
- **自检 `CheckFilePickerDrives()`**：盘符数 = 枚举数、id 是 `drive:<完整根路径>`、
  点盘符切目录、`GetCurrentDrive()` 形如 `C:`、`RefreshDrives()` 幂等（六条断言）。
  顺带补上 `FilePicker` 此前**零自检、零示例**的缺口。

### 变更

- 盘符**单独成块**，不复用 `shortcuts`：选项栏每项同宽（`itemLook.width`），
  96 宽 × 3 项再塞盘符会顶出 640；盘符项 52 宽，一行放得下 11 个。
- 非 Windows 上 `EnumerateDrives()` 返回空 ⇒ 盘符栏**整条不建**（不占高度），
  而不是画一条空栏。
- `MakeDefault()` 仍是 640×480：五块内容合计 448，底部留白由 66 px 变 32 px。

### 文档

- `docs/zh/api/InkingPrefab.md` §5 改成"五块"并补「盘符栏（换盘符）」一节；
  `TaskGuide` C5 扩写、状态表标注增补；`UsageReport` C5 节同步。

### 已知限制

- **只重建项、不重排布局**：静态档几何在构造期算死，"运行期从 0 个盘符变成 1 个"
  不会把这一块撑出来；要那种效果请重新构造选择器。
- 盘符是**会话内枚举**的：U 盘插拔后要手动 `RefreshDrives()`，不挂系统通知
  （托盘轮询不是本组件的活）。
- 仍不做：原生对话框、书签 / 最近位置、多列 / 图标视图。

---

## 0.5.1 —— 2026-10-24（**发布版**）

> 来源：用户需求「支持原本的 svg 设计」；API 专页按用户裁定**后置**（实现完成后再写）。
> 方案、核实与实测数字见 `docs/zh/plans/2026-10-24-0.5.1-svg-design.md`（探针
> `build/probe_svg*.cpp`）。

### 新增

- **图片层支持 `type:"svg"`**：整张界面设计稿（多图元 / `g` / `transform` / 渐变 /
  描边 / `fill-rule="evenodd"`）在运行期按 `组件设计尺寸 × 输出设备比例` 光栅化成纹理，
  再按形状裁剪铺进框。同一张图最多 3 档缓存、单张上限 16M 像素；1600×900 实测首次
  光栅化 **14 ms**（3200×1800 为 63 ms）。落点：`include/render/InkingSvg.h`、
  `src/render/InkingSvg.cpp`、`InkingText.cpp` 的 SVG 档。
- **`hitPassThrough`（Image 顶层字段）**：整张稿当背景时置 true，空白点击被判成
  `PassThrough` 继续往下找；默认 false = 图片照旧挡住点击（0.5.0 行为不变）。
- **不支持元素的 WARN**：`text` / `use` / `<image>` / `clipPath` / `mask` / `filter`
  在加载时扫描并打一条汇总 WARN（每张图一次），同时进
  `TextCacheStats::svgUnsupportedElements` 计数——以前这些元素**静默消失**。
- **示例**：`CXXCSS/Image/design.json` + `examples/demo/assets/design.svg`，
  demo 里铺满背景、上层控件照常可点。

### 变更

- **`Image` 的 `type:"svg"` 落成 `AppearanceType::Image`**（纹理路），不是按钮的
  `AppearanceType::Svg`（生成期转形状、单图形）。两条路同名不同义，
  `CXXCSS.md` §4.1 有对照表。
- 按钮 `type:"svg"` 子集**不变**（单图形、单填充、几何等于组件框）。
- `include/render/InkingSvg.h` 是新的"render 层内部头"（只前向声明 `SDL_Surface`）。

### 文档

- `CXXCSS.md` §3.10 加 `type:"svg"` 的元素矩阵、目标像素与 `hitPassThrough`；
  §4.1 加"两种 svg"对照表；§9 字段总表随 `--dump-fields` 重生成。
- `README.md` 已知限制 3 拆成两条（两种 svg 的边界 + 不渲染元素），版本横幅改
  0.5.1 发布版，预编译包 `dist/inking-0.5.1-win64.zip`。
- `include/button/ButtonData.h` 与 `src/image/InkingStaticImage.cpp` 里"没有画法"的
  过期注释已改。
- **API 专页（`docs/zh/api/`）不在本版**：用户裁定实现完成后再写。

### 已知限制

- 元素级交互（把整张稿拆成每个 `path` 一个控件）不做，0.6 再谈；0.5.1 是
  "一张纹理 + 可穿透"。
- `text` / `use` / `<image>` / `clipPath` / `mask` / `filter` 不渲染（nanosvg 边界），
  只 WARN；导出时请转曲 / 展开。
- 二次解码只补一次：框与稿比例差得极远时，短边仍可能略糊（另有 16M 像素上限）。

---

## 0.5.0 —— 2026-10-24（发布版）

> 来源：用户要求"补齐右键输入，触摸屏长按自动映射为右键"。方案与核实见
> `docs/zh/plans/2026-10-24-right-button-longpress.md`。
> **这一版有破坏性接口改动**：指针钩子签名变了，迁移方法见"变更"第 1 条。

### 新增

- **两种输入状态 `Primary` / `Secondary`**（`input/PointerButton.h`）：鼠标左 / 右键
  一一对应，**触摸轻点 = Primary、长按 = Secondary**。组件层从此只认这两个语义位，
  **永远不需要知道输入设备**——这是本库的跨平台立场，不是对触摸的补偿。
- **次键点击事件**（`InkingButtonLike::contextClicked`）：`InkingEvent<float, float>`，
  参数是**按下点**。`SetOnContextClicked` 是它的便利写法。
- **触摸长按**（`InkingScene`）：按住 0.5 秒（`_longPressSeconds`）且移动不超过
  8 设计像素（`_longPressSlop`）即转义成一次完整的次键交互（press → release → click）。
  两个参数是 `protected` 成员，派生场景可改。
- **场景观测口**：`GetPointerDown()` / `IsSecondaryPointerDown()` /
  `GetSecondaryPressedNode()` / `GetPressOrigin(button)`。
- **`MouseState::source`**：窗口层记录这次按下来自鼠标还是触摸（场景只在按下那一刻读）。

### 变更

- **【破坏】指针钩子带按钮维度**：
  ```C++
  // 旧
  void onPointerPress(bool press) override;
  void onPointerClick() override;
  // 新
  void onPointerPress(PointerButton button, bool press) override;
  void onPointerClick(PointerButton button, float designX, float designY) override;
  ```
  迁移样板（不关心右键的组件）：
  ```C++
  void MyWidget::onPointerPress(PointerButton button, bool press) {
      if (button != PointerButton::Primary) { return; }
      /* ...原来的逻辑... */
  }
  ```
  不保留旧签名的理由：两套入口会让"重写了哪个、另一个还调不调"没有答案，
  表现不是双触发就是静默不生效。
- **捕获按按钮分槽**：`_pressedNode` 变成数组。左右同按互不覆盖——共用一份状态时
  右键按下会顶掉左键槽，抬起时左键的 `press(false)` 永远不来，按钮永久停在 Pressed 色。
- **`detail::PointerProbe::Update(hit, down)`**：第二个参数从 `bool` 变成
  `const PointerDownState&`；结果按 `primary` / `secondary` 分槽，
  顶层 `downEdge` 改名为 `anyDownEdge`。
- **窗口层关掉触摸→鼠标合成**（`SDL_HINT_TOUCH_MOUSE_EVENTS=0`）：SDL3 默认开启，
  会让一次触摸同时产生 `FINGER_*` 与 `MOUSE_*` 两套事件，来源抖动、长按永远不成立。
- **`inside` 判据补 `SDL_WINDOW_INPUT_FOCUS` 与 `IsTouchActive()`**：关掉合成后
  `SDL_WINDOW_MOUSE_FOCUS` 在纯触摸下可能不置位，只留原来那一条会让触摸**整条失效**。

### 修复

- **定点浮窗的"点外部关闭"对右键失灵**：判据从 `downEdge` 改成 `anyDownEdge`。
- **右键会误结束左键拖动**：`InkingStaticImage` / `InkingStaticPanel` / `MenuBar` /
  `InkingTextInput` 都加了主键守卫——否则"右键看着没反应，拖动却断了"。

### 已知限制

- 中键仍只记状态、不派发。要开的话：`PointerButton` 加一个枚举值 +
  `kPointerButtonCount = 3`，`static_assert` 会先把需要同步的地方报出来。
- 右键不产生 `onPointerDrag`（桌面右键拖拽是少见手势，且要和菜单关闭语义仲裁）。
- 触摸仍然只认第一根手指（多指针的结论不变）。
- 长按阈值 / slop 是通用值，真机手感留待有触摸设备时再调。

### 文档

- `api/InkingPointerHandler.md` 新增"两种输入状态"一节，钩子签名与归属表同步。
- `plans/2026-10-24-right-button-longpress.md` 是本版的方案与验收依据。

---

## 0.4.1 —— 2026-10-24（0.4.0 的增补）

> 来源：定点浮窗 + 菜单浮窗（用户口头更新计划）。逐条核实、取舍与实测见
> `docs/zh/plans/2026-10-24-0.4.1-fixed-popup-menu.md`。
> 这一版是 0.4.0 的**增补**：新增两个组件，另有两条行为变更（都在"变更"里写明）。

### 新增

- **定点浮窗**（`panel/InkingFixedPopup.h`）：动态档容器。`OpenAt(x, y)` 在定点弹出，
  靠近窗口边缘时**自动翻到对侧**；比范围还大时贴边并允许另一侧溢出（`IsClampedX/Y()`
  可断言）。点击**吃掉**（不透到下面），点外部默认关闭，打开时夺取键盘焦点。
- **落位算术**（`detail/InkingPopupPlacement.h`）：每轴独立的"默认 → 翻转 → 夹紧"。
  纯函数，自检拿 7 条数字断言钉住（四方向 / 两轴独立 / 超宽 / 非零原点 / 尺寸 0 退化）。
- **菜单浮窗**（`prefab/MenuPopup.h`）：竖排菜单。`AddButton` 可顺手绑**局部快捷键**，
  `AddItem` 可挂任意自定义件，`onMenuActivated` 是继承扩展点。
- **`detail::PointerProbe`**（`detail/InkingPointerProbe.h`）：动态组件的边沿 / 捕获 / 点击
  判定。动态按钮与定点浮窗共用一份——以前这类逻辑各写各的。

### 变更

- **动态组件的输入采样改为"指针变化时"**：场景在指针真的变了的那一帧推一次
  `onPointerStateChanged`，**鼠标不动就不推**。以前是"每个逻辑步（50 Hz）自己拉"：
  既白算（没动也判）也晚判（短于 20 ms 的点击直接漏掉）。`onTick` 仍在，但它只管
  几何 / 计时。代价：至多**一帧错位**。
- **`QueryHit` 的容器深挖统一到静态 / 动态两档**：以前只写在静态那一侧，于是
  `OwnsSubtreeHit()` 的动态容器里的按钮"看得见、点不到"。

### 修复

- **动态容器里的静态子控件收不到点击**：0.4.0 的 A1 只覆盖了"静态面板 → 动态节点 →
  按钮"，**动态节点自己声明容器**那一段没接上。现在两档走同一个 `normalize`。
- **两档差异表里"三态过渡按逻辑步推进"**是错的（动态档也走 `onAnimationTick`），已改正。

### 文档

- `InputDesign.md` 新增 §17「输入归属」，§6 / §7 / §15 / §16 同步改口径；
  `ComponentGuide.md` 加 §2.1（动态档的对照样板）；`TaskGuide.md` 追加 C6 / C7；
  `api/InkingFixedPopup.md` 新建。

### 已知限制

1. **浮窗打开期间是焦点模态的**（同场景的输入框拿不到焦点）。`SetTakeFocusOnOpen(false)` 可关。
2. **局部快捷键与全局表冲突时全局赢**：窗口层先跑 `ShortcutRegistry`，冲突的组合在
   `BindLocalShortcut` 里就被拒并打 WARN，不会静默失效。
3. **跨静态 / 动态的点击透传未实现**：浮窗固定吃掉（不提供穿透开关）。要做"点了透到下层"
   得同时改 `QueryHit` 与 `DispatchPointer`（后者现在完全不看 `HitKind`）。
4. **没有右键通道**：`MouseInput` 记了右键状态，但没有派发到组件。示例用按钮定点打开菜单；
   真要做右键菜单得先给窗口层加一条通道。
5. **菜单无子菜单 / 方向键导航 / 开合动画**。

---

## 0.4.0 —— 2026-10-24（上一版）

> 来源：`D:/GoldingCore/docs/inking-requirements.md`（A 组树动态性 / B 组布局表达力 /
> C 组绘制 / D 组 test 能力）。逐条核实与取舍见
> `docs/zh/plans/2026-10-22-goldingcore-requirements.md`。
> **A 组五条是同一个根因**（收集表建树时一次性生成、之后不跟随），这一版一并修掉；
> C 组三条暂缓（理由写在任务书 §4）。

### 修复

- **渲染收集表改为"按树重建"**（A2 / A3 / A5）：`InkingScene::sortDrawList()` 在内容脏时
  从场景根做一次先序 DFS，扫到谁就收谁（顺带补登记 `_scene`）。以前是**增量**登记——
  "构造时父级还没进场景"的子树（先建面板、后 `AddChild`）永远进不了列表，症状是
  **节点在、`IsVisibleInTree()` 为真、就是不画**，且不报错。树的层级本来就是唯一真相源，
  列表只是派生索引（`InkingScene.h` 顶上那段话），这一版把它真正做成派生。
- **动态容器的子树可以命中**（A1）：`InkingWidget::HitTestNode()` 的默认实现从
  "命中即自己"改成**子树优先的递归**。以前只有 `InkingStaticPanel` 重写了一份，
  于是"动态容器里的静态按钮"永远收不到点击（命中表里没有它、递归又断在容器上）。
  现在"声明自己是容器"只需要 `OwnsSubtreeHit()` 返回 true。
- **`AddChild` / `RemoveChild` / `ClearChildren` / `Resize` 会重排布局**（A4 / B1）：
  以前布局只在构造期跑过一趟，于是"先建容器、后挂孩子"（抽屉 / 列表 / 动态面板的通用
  写法）里 `fillMain` 不收敛、`Stretch` 不拉伸、`width:0` 解出来就是 0 宽——**不画也不
  报错**。核为此在每个容器上写死 `1224` / `1080` / `200` 这些魔数。
- **按钮的显示尺寸跟随布局结果**（A4 的真因）：`GetWidth()` 走 `ButtonLook::DisplayWidth()`，
  而布局把新尺寸写进组件的 `_width` 之后那一份不跟着变 ⇒ 布局算对了、观测口还是旧值。
  新增 `ButtonLook::SyncBaseSize()`，两档按钮在 `onSizeChanged()` 里同步。
- **截图 / 离屏渲染的落盘不再依赖 SDL 的文件层**：编码先进内存（`SDL_IOStreamInterface`
  实现一块可写内存流），落盘走 `std::fopen`、失败再退回 SDL 路径 API。实测受限环境里
  `SDL_SavePNG(surface, path)` 会把文件名截断（写出 `probe_o` 却返回 true）。
- **背景 alpha 不再是"看起来支持、其实无效"的通道**：不透明窗口上 `SDL_RenderClear`
  直接写帧缓冲、不参与混合，帧缓冲的 alpha 又没人采信 ⇒ `background` 的 `a` 写 0 也透
  不出桌面，而且一个字都不报。现在 `stepFrame` 会打一条 WARN（每状态一次），并进了
  静默失效清单第 9 条。要真透明请开 `window.transparent`。

### 新增

- **`InkingScene::RebuildRenderList()` / `RebuildHitTable()`**（A5 / D6）：显式重建收集表与
  命中表，不必等帧。`QueryHit` 本来就会先推一次重建，所以"窗口 Show 之前验证某点能点到谁"
  现在直接可用。
- **`InkingWidget::SetName()`**（B2）：名字以前只有构造期那一份，动态组件设不了 ⇒
  `findByName` 走不到它、自检只能按子节点索引定位（索引一变断言全错）。
- **`InkingWidget::Activate()` / `InkingScene::Activate(名字)`**（D7）：语义点击，调用方
  **不需要知道控件类型**（以前自检必须 `dynamic_cast<InkingStaticButton*>`）。
- **`InkingScene::FindByName()`**：深度优先、先序、返回第一个同名节点。
- **`InkingWindow::RequestClose(int exitCode)` + `GetExitCode()` + `Show()` 返回退出码**（D5）：
  自检插件要能"报失败并让进程以非 0 退出"。
- **`InkingWindow::Open()` / `PumpOnce()` / `RunFrames(n)`**（D1）：建窗口与跑循环拆开，
  跑完就返回。`Show()` 现在就是 `Open()` + `while (PumpOnce())`，两条路共用 `stepFrame`。
- **`InkingWindow::PostFrameTask()` / `RunAfterFrames(n, fn)`**（D3）：帧末任务队列，位置在
  场景脏消费之后、待销毁场景提交之前（读到的是这一帧定稿的值，场景还活着）。
- **`InkingWindow::SetFixedDelta()` / `GetFixedDelta()`**（D4）：固定每帧 delta，且**不节流**。
  动画 / 主题过渡的截图在 CI 里因此逐像素稳定。
- **`InkingWindow::RenderToTexture(path, w, h, sceneName)`**（D2）：离屏渲染，不建窗口、
  不需要 dummy 视频驱动。走的是**同一条** `SceneLibrary::RenderScene`，不是另画一份。
- **`MenuBar::GetCurrentHeight()` / `IsBarVisible()`**（B3）：顶栏在全屏时整条消失，内容区
  高度必须按"窗口高 − 顶栏**当前**高"算（写死 `窗口高 − 40` 就是全屏留一条白）。
- **透明窗口**：`InkingWindow::SetTransparent()` / `IsTransparentRequested()` /
  `IsTransparentApplied()`，配置侧对应 `window.transparent`。建窗时带上
  `SDL_WINDOW_TRANSPARENT`，清屏色的 alpha 从此**真的**能透出桌面。`Applied` 读的是
  "标志进没进窗口"，不是"合成器照没照做"——后者没有回读口。

### 变更

- **默认主题换成「现代浅紫」**：`CXXCSS/Theme/dark.json` → `Theme/lavender.json`，10 个
  颜色 token 全部重配。色源是用户那份 Godot（`D:\Lya`）的 **"White Purple"** 预设，
  与 `include/prefab/PrefabTheme.h` 同一张调色板：
  场景底 `#EBE6F2` / 卡片 `#FFFFFF` / 次级面 `#FAF8FC` / 承载面 `#F1EDF7` /
  文字 `#33303C` · `#5A5468` · `#7C7590` / 强调 `#6E5AAE`。
  窗口清屏色（`config.json` 的 `window.background`）与 `PrefabTheme::kSceneBackground`
  一起改成 `#EBE6F2`——两处必须是同一个数，否则"窗口底"和"面板底"会差一档灰。
  两点取值理由：`textMuted` 比 `kTextDisabled` 深（它承担正文弱化，不是禁用态）；
  `$accent` 取主色的**深版** `#6E5AAE`（这个 token 只被当文字色用，`#9380C2` 在白底
  只有 3.5:1），实底紫仍是 `#9380C2`，由组件配置自己写。
  示例配置（`button.example.json` / `panel.example.json`）同步成同一套，免得"照示例
  复制"又拿回一份深色。
- **`Relayout()` 的调用时机多了一类**：框架现在会在"容器增删孩子"与"动态容器改尺寸"之后
  自动重排。`InkingWidget.h` 里原来写着"框架不做隐式重排"，那句话对**内容变化**仍然成立
  （文字改长仍要自己喊），对**骨架变化**不再成立——骨架变了而布局不动，等于让调用方
  记得一件必然忘的事。
- **`InkingWidget::HitTestNode()` 基类语义变了**（见上）。派生类重写它时仍按原契约，
  但"忘了重写"不再是静默失效——基类会替你递归。
- **`Show()` 的返回类型 `void` → `int`**：源码兼容（忽略返回值的调用照旧编译）。

### 文档

- `docs/zh/api/InkingScene.md`、`api/InkingWidget.md`、`api/InkingWindow.md` 补上本节
  新增的每个入口与"什么时候用哪个"的判据。
- 任务书 `docs/zh/plans/2026-10-22-goldingcore-requirements.md`：逐条核实结论、
  已落地项（压缩成一行）、暂缓项（附原因与前置条件）。
- **发布包自带运行期 DLL 闭包**（19 个）+ `licenses/` 许可原文 + `examples/minimal/`
  最小工程：上一版必须由使用方装 MSYS2 并配 PATH，漏配只表现为静默退出
  （`0xC0000135`）。版本号同步收口为 `kVersion = "0.4.0"` / `kVersionStage = "release"`
  与 CMake `VERSION 0.4.0`；`tools/package.sh` 改为"按 DLL 名字回工具链取文件"，
  避免带注入 DLL 的环境里 `ldd` 解析到别处而漏收（实测漏 7 个）。

### 已知限制

- **透明窗口只在部分后端成立**：SDL 只在 Windows 的 D3D11 后端补过透明 swapchain，
  D3D12 明确不行、Vulkan 多数环境不行，混合 GPU 也可能失效。`IsTransparentApplied()`
  证明的是"`SDL_WINDOW_TRANSPARENT` 进了窗口标志"，**不是"桌面真的透了"**——合成结果
  读不回来。本机实测（`direct3d11`）：清屏 alpha=0 / 128 都能从后缓冲原样读回，但读回的
  RGB 是**未预乘**的 straight 值，所以带 alpha 的描边 / 阴影可能**偏亮 / 过饱和**，
  要按真机校色。
- **C 组暂缓**：`0x00000000` 仍兜底成品红（T-03 的护栏）、按钮文字仍居中不可调、缺字形
  仍画方框。三条都要新的配置字段 / 文本排版能力，属于下一批（详见任务书 §4）。
- `RebuildRenderList()` 是 **O(N)** 的全表重建；它不该出现在每帧路径上。正常路径由
  `sortDrawList()` 在结构脏时自动跑一次。
- 增量登记时代的 `AddDrawNode()` 仍然保留（构造期用），但收集表已经以树为准——
  它是"标记脏"的通知，不再决定列表内容。

---

## 0.3.0 —— 并入 0.4.0（字体回退链与静默失效巡检）

### 新增

- **字体回退链**（`TextStyle::fontPaths` / `TextRequest::fontPaths` /
  `ButtonTextData::paths`）：`fontPath` 之后按顺序尝试，先能打开的胜出；CXXCSS 里
  `text.path` 现在也认**字符串数组**（首个元素进主字段、其余进链尾）。缓存键（字体、
  字形图集、整段纹理）用的是**实际命中的那条路径**，所以改链顺序会真的换字体。
- **回退命中计数**（`detail::TextCacheStats::fontFallbacks`）：链上"退到第二条及以后"
  才命中的次数。字形差异没法断言，结论落成计数才验得动。
- **静默失效清单**（`docs/AGENTS.md` §7 + 自检内建的同一张表）：每条"会静默变空白 /
  不生效"的路径绑定一个可断言的自检名，并新增元自检 `silent_failure_list_synced`
  ——"表里有、测试里没有"会红。配套 ctest `silent_failure_list_in_sync` 比对文档表与
  自检源码。
- 自检补三条：`theme_token_missing`（表里没有的 token 退回兜底色并记一次 miss）、
  `scroll_range_stale`（内容超出视口后 `UpdateScrollRange` 给出的范围、`SetScroll`
  按它夹住）、`text_negative_width`（可排宽度为负时不换行；负宽度配置被夹到 0 再按
  内容补，不会变成"什么都不画"）。

### 变更

- **字体打不开的告警从"每个候选一条"改成"每条链汇总一条"**：链上多条候选时，逐条报
  会把日志刷成墙。这条同时守住了 0.3.0 的门槛（字体缺失场景只产生 1 条 WARN）。
- `detail::AcquireGlyph` 的参数语义收窄：传入的必须是**已解析**的实际路径（解析只在
  `PaintText` 里做一次），这样图集键里的字体与实际烘字用的字体永远是同一份。

### 文档

- `CXXCSS.md` §3.8 新增「字体回退链（`text.path` 写成数组）」；§5 按钮 `text` 段加了
  一行指向它。两处都写明"文件级回退、不是字形级混排"这条边界。
- `docs/AGENTS.md` 新增 §7「静默失效清单」。

### 已知限制

- 回退链是**文件级**的：整块文字只会用一份字体文件，"中文雅黑 + 数字等宽"同屏混排
  **仍然不支持**（SDL_ttf 没有按族名 / 覆盖范围选字体的入口）。
- **整条链全打不开时不退回机器默认字体**，而是画占位方块 + 一条 WARN（与任务书原稿
  的差别，理由见 `docs/zh/plans/2026-10-22-feedback-upgrade-tasks.md` §5 的偏离说明）。

---

## 局部层级（A8b）—— 并入 0.4.0

### 变更

- **`zIndex` 的语义从"全场景一条"改为"兄弟之间"**（局部层级）。`InkingWidget::IsAbove`
  现在按"树序 + 兄弟内 z"比较：① 父容器先于自己的子孙画；② `zIndex` 只在同一父下生效；
  ③ 同 z 仍看注册序号。
- **修复一类静默失效**：以前"面板 `zIndex:20`、里面的标签 `zIndex:1`"会让面板底色
  盖住整块内容，且不报任何错（外部使用方实测：面板区最大亮度 38，调完 z 才回到 255）。
  现在子的 z 再小也在父之上。
- 仍是**一个严格全序**，所以绘制、命中表、面板子树仲裁、动静合并四处共用同一个比较器，
  "画的是 A、点到的是 B"这条不变量没变。
- **日志改为纯文本事实源**（与 InkingBackendFramework 的 `ShineLog` 同步）：
  - 运行期只写 `LOG/<日期>/run-<时分秒>-<pid>.log`，一次运行一个文件，`LOG/latest.txt`
    指向最近一次；**不再写 `Log.html`**，热路径上不再有 CSS/JS 模板。
  - 每行 7 段自带定位：`时间 | 级别 | 模块:阶段 | 序号 | 线程 | 文件:行 | 消息`；
    第 3 段固定为 `模块:--`（前端无阶段概念，保留该列以便与框架端共用解析/报告工具）。
  - 连续相同记录折叠成一行 `FOLD`（带重复次数与起止时间）；同一个键每秒最多 5 条；
    `ERROR` 不折叠，一条一行。
  - 浏览器视图改由 `InkLog::report()` 按需把 `.log` 渲染成自包含 HTML（双击即开）。
  - `InkLog::logFilePath()` 现在返回**当前 `.log` 路径**（首次调用会创建文件与目录）；
    新增 `logDirectory()` / `tail()` / `grep()` / `statistics()` / `setLevel()` /
    `parseLevel()` / `mark()`，以及 `INK_LOG_MARK` 宏（伪级别 MARK，不受阈值影响）。
  - `INK_LOG_*` 宏改为把 `std::source_location::current()` 传下去，日志里的"文件:行"
    指向业务代码而不是 `InkLog.cpp`。

### 新增

- **运行期主题（颜色）**：`theme/InkingTheme.h`（`InstallThemeTable` / `SetActiveTheme` /
  `ThemeVersion` / `ResolveThemeColor` / `ThemeStatsSnapshot` / `ClearThemeTables`）。
  生成期仍按**烘焙主题**把 `$token` 换成字面量（= 默认色），同时把 token 名留在
  `ButtonAppearance::themeColor` / `PanelAppearance::themeColor` / `LabelData::colorToken`
  里，运行期按当前主题重查 ⇒ `SetActiveTheme("light")` 一句话换肤。
- **多份主题**：`CXXCSS/Theme/*.json` 可以有多份，`"default": true` 指定烘焙主题
  （都没标则按文件名排序取第一份，并打一条警告）；生成物多出
  `InstallGeneratedThemes()` / `kDefaultThemeName` / `kThemeCount`，启动期自动安装。
- 生成期新增**跨表校验**：每份主题都必须覆盖组件用到的全部颜色 token——少一个的表现是
  "切过去那一处还是旧色"，在构建期就报错。
- 自检新增「运行期主题」18 项：装两张表 / 切表 / 切失败不动版本 / **真渲染像素**在
  蓝→红→蓝之间切换 / 缺 token 用兜底色并记 miss / 清空回到字面量。
- `inking_demo_selftest` 的「层级」段（19 项）：父子 / 兄弟 / 跨子树三种关系、绘制列表
  顺序与 `IsAbove` 同源、中心点命中子面板、**真渲染一帧后读回中心像素**。
- **截图能力（P2-6）**：`InkingWindow::RequestCapture(path)` / `Capture(path)` /
  `HasPendingCapture()`。`.png` 走 `SDL_SavePNG`（SDL ≥ 3.4）、其它走 `SDL_SaveBMP`，
  **不新增 SDL3_image 依赖**。`RequestCapture` 排队到本帧末（`Present` 前）执行，
  所以在按钮回调里也能用；`Capture` 立即读当前渲染目标。
- **环境变量 `INK_CAPTURE=<路径>`**：启动后第一帧零代码落一张图。排错时"先看一眼画面"
  比加日志快——外部使用方当初为此自写了 `SDL_RenderReadPixels` + 纯 Python 的 BMP→PNG。
- 自检新增「截图：失败路径」4 项：空路径 / 无渲染器都必须返回 false，
  且**不许留下永远不落盘的排队请求**。

### 修复

- **标签 `height` 写 `0` 被判"必须大于 0"**：`CXXCSS.md` 两处都写着"可省：省略 = 0 = 完全由
  内容撑高"，但生成器的 `ReadSize` 对 `< 1` 一刀切报错——照文档写 `0` 的人在构建期被拒
  （外部使用方正是这么踩的）。现在标签的 `height` 收 `0`（与省略完全等价，产物里都不写
  这一行）；**宽度仍然必填且 > 0**（它是换行的依据），负数的文案改成"不能是负数"。

### 文档

- `CXXCSS.md` 新增 §2.3「层级」：`zIndex` 只在同级生效、要压住别的内容就抬容器自己。
- `InputDesign.md` §1/§10.5、`TaskGuide.md` A8b/T-4、`UsageReport.md`、
  `README.md`「已知限制」、`api/InkingWidget.md` / `InkingScene.md` / `InkingAnchor.md`
  同步改口径。
- `api/InkingRuntime.md` §七「日志：InkLog」按新实现重写：输出位置、7 段行格式、
  折叠/限流规则、查看方式（`.log` / `tail` / `grep` / `report()`）、编译期开关的实际取值
  （原先写的 `-DISLOG=ON` 选项并不存在，实际情况是 `CMakeLists.txt` 里直接打开）。

### 已知限制

- **portal 未做**：子节点的 z 再大也盖不过**父级的兄弟**。"展开的下拉要压住同一容器
  之外的内容"目前靠抬高**容器自己**（`Select` 的 `expandedZIndex` 就是这么用的）。
  引入 portal 之后，`§10.5 规则 4` 的跨簇条目仲裁才需要补回来。
- **运行期换肤只覆盖颜色，且只覆盖三处**：按钮三态、面板两态、标签文字色。
  `image` / `canvas` / 滚动条的颜色，以及 `stroke` / `shadow` 的颜色 token，仍在生成期定。
- **字号 token 是编译期的**：那是几何（重排 + 重烘命中表 + 重烘字图集），改字号要重新生成。

---

## 0.1.1 —— 并入 0.4.0（外部反馈第一批，静默失效清零）

来源：外部使用方对 `0.1.0-test` 的 12 条反馈；逐条核实与任务拆分见
`docs/zh/plans/2026-10-22-feedback-upgrade-tasks.md`。主题不是某一条 bug，而是
**"出错的时候不发声"**。

### 新增

- **`InkLog::CountOf(Level)` / `InkLog::ResetStats()`**：按级别数"真的调用了多少次"
  （折叠、限流、阈值挡掉的都不算），于是"这里应当恰好产生 1 条 WARN"变成可断言的东西。
  `ResetStats()` 明确标注**只给自检用**。
- **`InkingWidget::RelayoutSelfIfContentSized()`**：内容驱动组件在没有布局容器时自己量一次。
- **`InkingWidget::WarnIfZeroSize(module)`**：0 尺寸的统一护栏（标签 / 面板 / 两档按钮共用一套文案）。
- 面板 / 按钮两态（三态）全透明兜底时**补一条 WARN**，并带组件名。
- 字体打不开时**补 WARN**（路径 + 字号 + `SDL_GetError`）；机器默认字体候选全落空时另有一条。
- `CXXCSS/Panel/inputBox.json` + `CXXCSS.md` §3.8.1「输入框配方」：`InkingTextInput` 不画底，
  正确用法是"面板当底 + 输入框当字"两层。

### 变更

- **`INK_ISDEBUG` 从"恒为 PUBLIC"改为 `option(INKING_DEBUG_LOG)`，默认 `OFF`**。
  噪声的根子是这条：发布包的消费方**关不掉**调试流（948 条日志里有一条重复 528 次，
  而那一条本来就是 DEBUG 级）。仍是 PUBLIC —— 公开头里有 `kDebugEnabled` 这类
  `inline constexpr`，库与调用方取值不同就是 ODR 违例。
- **DEBUG 有独立的折叠槽**：别的级别的记录插进来不再打断它的累计。原来的共用槽只对
  "连续相同"有效，而真实噪声正是"每帧一条 DEBUG、中间夹着别的日志"。
- **WARN / ERROR 不再参与折叠**（诊断凭据要逐条看得见）；仍受"同一键每秒最多 5 条"限流。
- 窗口圆角的"请求了但系统忽略"从 DEBUG 升 **WARN**（配置未生效，发布包里以前看不见）。
- 兜底色 `0xFF000000` → **`0xFFFF00FF` 品红**：深色界面上纯黑与正常底色无法区分，兜底等于没兜。

### 修复

- **内容驱动组件在无布局容器里尺寸为 0、一个像素不画、零日志**（P0）。根因是
  `RelayoutParent()` 找不到布局祖先就静默返回。现在改成"找不到就自己量"，且
  **只补没配置的那一维**（`width: 200` 不会被内容的 76 压掉）。标签构造末尾补一次
  （构造期没有 `SetText` 触发）。
- 标签漏接 0 尺寸护栏（按钮一直有，标签没有）；现在统一到 Widget 层，四个组件共用一处。

### 文档

- `CXXCSS.md` §3.8：`height` 省略的口径改成"有 `layout` 由布局撑高；没有则自测量"，
  原文是无条件表述，在没有布局容器时不成立（文档过承诺）。
- `README.md`：新增"路线 0：先跑 `inkgen --validate`"（配置校验的报错常被误读成编译错误），
  构建选项表补 `INKING_DEBUG_LOG`。
- `API.md` / `api/InkingScene.md`：观测口与"怎么读这些数"的入口指向 `UsageReport.md §3`。
- `api/InkingWidget.md`：`Relayout / RelayoutParent` 补"**哪些 setter 会自动重排**"一张表
  （框架不做隐式重排这条没变）。
- `api/InkingRuntime.md` §七：10-21 同步时写的降噪口径（"连续相同折叠 / ERROR 不折叠 /
  两个开关都直接打开"）已被本次改动作废，按新规则重写；接口清单补 `CountOf` / `ResetStats`。
- **`InkLog.h` 与 `ShineLog.h` 各写明"子集关系"**，不再声称"保持同一套设计"——两边级别集合
  本来就不同（框架端 `Trace…Fatal` 六档，前端 `Debug/Info/Pass/Warn/Error` 五档），
  前端还缺 `init/LogOptions`、`snapshot`、`command`、控制台镜像。现在两个头里都写了
  **哪些要同步改、哪些各管各的**，判据不靠记忆。

### 已知限制

- 自测量只发生在**没有布局祖先**时；布局容器内仍由布局给尺寸（自己量会和布局打架，
  也会在父容器尚未构造完时量出偏小的值）。
- 显示尺寸为 0 的**纯容器**刻意不告警：0 尺寸分组节点是常见用法，告警会变噪声。
- 字体**族名**解析仍不做（`SDL_ttf` 只有按路径打开）；回退链 `fontPaths` 属第二批。

---

## 0.1.0-test —— 2026-10-17（历史）

首个"可构建、可运行、文档齐"的测试版本。功能面来自此前的分层重构与
T/A/B/C 系列任务；本次的主要工作是**文档补齐 + 最小发布准备**。

### 新增

- **完整 API 文档**（`docs/zh/api/` 新增 10 页）：
  `InkingWidget` / `InkingScene` / `InkingWindow` / `InkingInput` /
  `InkingRender` / `InkingComponents` / `InkingPrefab` / `InkingRuntime` /
  `InkingLayoutSolver` / **`InkingCxxcss`（CXXCSS 生成物：生成类形态、`Data()` /
  `Register()` / `RegisterAllButtons()`、语言表安装、`inkgen` 命令行、落地边界）**。
  原有 6 页（锚点 / 形状 / 变换 / 布局 / 指针 / 键盘）保留。
- `docs/zh/API.md` 重写为**索引 + 五分钟上手 + 模块地图 + 头文件全量索引**。
- 仓库根 `README.md`：构建、运行、产物、依赖、已知限制。
- `include/inking.h` 新增 `kVersionStage = "test"`（版本阶段单独成一个常量）。
- 本文件。

### 变更

- `docs/zh/API.md` 的"关键词"章节保留原文并补充**设计坐标 / 设备像素**一条，
  以及"重绘脏 vs 命中脏"的区分。
- 全文档标注**测试版本**（`0.1.0-test`）。
- **运行期也标出测试版本**：`inking_demo` 与 `inking_demo_selftest` 的启动横幅改成
  `版本 0.1.0-test（测试版本）`——别人抓到一份日志时，第一行就该知道它不是正式版。

### 修复

- 无（本次不改运行期行为）。

### 已验证

- Release 构建（Ninja + MinGW UCRT64）：`libinking.dll` 链接成功，导出符号 1943 个。
- 无头自检 `inking_demo_selftest`：**69 项通过 / 0 项失败**，本次运行退出码为 0。

### 文档（第二轮补齐，同日追加）

第一轮把 API 面补到 15 页，但**被注释引用的设计文档**仍然全缺，导致 141 行注释的
「§6 第 N 条」「T-3」「§11」这类引用无法核对。第二轮补的就是这一块：

- **`docs/AGENTS.md`**：12 行 → **重建为完整文档**（§1 阅读顺序 / §2 构建自检 /
  §3 头文件与类写法 10 条 / §4 验证 6 条 / §5 两种脏对照表 / §6 经验条目 **1–112**）。
  41 个被引用条号**全部覆盖**（含 `CMakeLists.txt` 与 `tools/inkgen/` 的引用共 46 处），
  编号连续无空洞；带 `●` 的是按引用处语义回填
  （可核对），带 `†` 的是编号未被引用、按仓库实际约定补入的。
- **`docs/zh/InputDesign.md`（新增，约 470 行）**：§1 每帧只判断一次与全局 z 序 /
  §2 三档与"档不迁移" / §4 四类标脏触发源 / §5 isDirty 生命周期
  （命中脏 3 帧、重绘脏 5 帧、指针脏单列）/ §6 命中三态 / §7 动态档自判 /
  §8 生命周期成对处理 / §9 包络分档 / §10 命中表（§10.2 格子 + CSR、§10.3 聚类、
  §10.5 未落地的两条规则）/ §11 变换通道 / §12–14 键盘、滚动、捕获 / §15 开放问题。
- **`docs/zh/TaskGuide.md`（新增，517 行）**：A / B / C / D / T 五个任务系列 + `r2`
  任务纪律，每个编号给「目标 / 落点 / API / 验收 / 偏离」五段，末尾附**编号与代码
  对照表**。
- **`docs/zh/ComponentGuide.md`（新增，397 行）**：以 `InkingStaticPanel` 为样板的
  八段式组件写法、静态 / 动态选型判据、`InkingButtonLike` 混入基类、
  "为什么没抽 `TransitionTrack`"、新增组件 13 项 checklist。
- **`docs/zh/UsageReport.md`（新增，348 行）**：观测口与读数口径、成本量级、`§6.7`
  绘制成本 8 条、预制件 C1–C5 的限制、B7 文本框现状、**"不成立"表**。
- `docs/zh/SKILL.md` §0 的"文档缺口"表改为**已完成**状态（保留历史对照）。
- `docs/zh/API.md` §1 撤掉"CXXCSS.md 尚未搬入"的错误提示（该文件实际存在，821 行）。

### 已验证（第二轮追加）

- `docs/AGENTS.md` 的条号覆盖：以脚本对 `include/` + `src/`（含 `CMakeLists.txt`、
  `tools/inkgen/`）全量抽取 `§6 第 N 条`，**46 个被引用条号 0 缺失**，编号 1–112 **无空洞**。
  `§3` 被引用条号 2 / 3 / 9 / 10、`§4` 的 4 也各自有对应条目。
- **Release 全量重建**（`--clean-first`，70 个构建步骤全过，0 error）：
  `libinking.dll` 1,265,899 字节，导入库 959,264 字节，导出 **1943** 个符号。
- **自检**：`inking_demo_selftest` **69 项 `[ok]` / 1 项 `[skip]` / 失败项 0**。
  `[skip]` 是"高清文字"路径（本机 `SDL_IOFromFile` 打不开字体，退化成占位画法）。
- **发布包**：`dist/inking-0.1.0-test-win64/`（129 个文件，4.3 MB）与同名 `.zip`
  （1.3 MB）。包内 `bin/inking_demo_selftest.exe` 单独跑过一遍，`失败项：0`。
  包根 `BUILD.txt` 记录工具链、依赖版本、SHA-256、验证记录、已知限制与许可状态。

### 已知限制

1. **`TaskQueue` 退出收尾**：函数内静态量在 DLL 卸载（`DllMain` 持加载器锁）时析构并
   join 管家线程，某些环境下进程退出会看起来"挂住"。与业务逻辑无关，属线程收尾语义。
2. **`docs/zh/TaskGuide.md` / `InputDesign.md` / `UsageReport.md` 是"重建版"**：
   原始设计稿与实测数据没有随搬家带入，结论按代码行为与引用上下文复原，
   标 `†` 的小节是推断内容——引用具体数值时以自己实测为准。
3. **未验证平台**：只在 Windows + MSYS2 UCRT64 构建过；Linux / macOS 未验证。
4. **ABI 不承诺稳定**：测试版本。

---

## 0.0.x —— 2026-10-15 ~ 10-17（历史，无独立记录）

分层重构与窗口能力落地（无边框 / 全屏 / 缩放档位 / 快捷键 / 文字高清）。
详细方案见 `docs/zh/plans/2026-10-17-window-menubar-zoom-hidpi.md`。
