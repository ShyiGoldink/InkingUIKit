# 变更记录（CHANGELOG）

格式：每个版本一节，分 **新增 / 变更 / 修复 / 文档 / 已知限制** 五类。
测试版本期间的条目**不承诺**跨版本兼容——升级前请逐条对照。

---

## 1.0.0-preview —— 2026-11-04（首个 1.0 预览包：功能面 = 0.9.2）

> **本版不承诺 ABI 冻结。** 1.0 的功能面已经收齐，可以整包交付评估；缺的是
> 「公开面盘点」——哪些符号是承诺、哪些还能随手改，需要一次独立清点
> （立项见 [`plans/2026-10-29-evidence-and-delivery.md`](plans/2026-10-29-evidence-and-delivery.md)
> 的「1.0.0 的 ABI 冻结」一行：明确「不在本批」）。盘点完成、正式 `1.0.0`
> 发出时，公开头里被点到名的符号才进入冻结承诺。**在那之前，升级仍要整包重编。**

### 变更

- **版本号 `0.9.2` → `1.0.0`，阶段 `release` → `preview`**：预编译包名随之变成
  `inking-1.0.0-preview-win64.zip`（只有 `release` 阶段省略后缀）。
- **功能面与 `0.9.2` 相同**：本版只动版本常量与文档，没有任何行为 / 接口变化。
  0.9.2 的升级提示与已知限制原样适用，见下一节。
- **文档页脚统一到 `1.0.0（预览版）`**（25 份 `docs/` 下的页脚），README 顶部
  横幅改成「预览版 ≠ 冻结」，并写明预编译包就是本版。

### 修复

- **`tools/package.sh` 读不出版本号**：0.9 的命名收口把 `kVersion` /
  `kVersionStage` 改成了 `InkingVersion` / `InkingVersionStage`，脚本里的 `sed`
  仍按旧名匹配 ⇒ 在「读不出版本号」那一步直接退出。改按全名匹配，并在打包时
  打印读到的版本 / 阶段。注意 `InkingVersion` 是 `InkingVersionStage` 的**前缀**，
  匹配写宽松了会把阶段名读成版本号。

---

## 0.9.2 —— 2026-11-04（背景板：letterbox 留白可配色 / 可铺底图）

> 起因：确认"库会不会渲染到设计尺寸以外"时，把留白的成因查清了——留白是清屏色
> （实测：非设计比例的窗口里，留白像素 = 清屏色），而清屏色同时是**画布底色**，
> 于是"让黑边跟画布不同色"这件事在旧 API 里做不到。

### 新增

- **`InkingWindow::SetBackdrop(argb, token)` / `GetBackdropColor()` /
  `GetBackdropToken()`**：背景板颜色。画在**清屏之后、场景之前**，覆盖整幅窗口像素
  （含 letterbox 留白），所以**黑边填的就是它**。`0` = 不画（零开销）。
  `token` 是生成期烘下的语义色名，窗口层**每帧**查一次主题表 ⇒ 切主题黑边跟着换。
- **`InkingWindow::SetBackdropImage(path)` / `GetBackdropImage()`**：同一层的底图，
  **cover** 铺满整窗（保持比例、多余裁掉）。解码延迟到第一次绘制，失败打 ERROR 并
  退回背景板颜色（不留黑块），同一路径只报一次。UTF-8 路径走 SDL 文件层。
- **`InkingWindowConfig` 新增 `backdrop` / `backdropToken` / `backdropImage`**：
  对应 `CXXCSS/config.json` 的 `window.backdrop`（支持 `$token`）与
  `window.backdropImage`，`InkingApplyWindowConfig` 逐项回读校验（颜色 / 语义名 /
  路径三条都读回来比）。
- **生成器**：`inkgen` 产出 `kBackdrop` / `kBackdropToken` / `kBackdropImage`，
  `--validate` 的报告里也会打印背景板（颜色 + 语义名 + 底图），不再只有清底色。
- **主题示例**：`CXXCSS/Theme/lavender.json` 加 `letterbox` token，
  `CXXCSS/config.json` 写 `"backdrop": "$letterbox"`。
- **自检 17 条**（离屏 + BMP 回读）：留白是背景板色、画布内不是；`$token` 走主题表
  （烘的蓝被表里的红盖掉）；换一张表黑边跟着换（证明每帧查表、没缓存）；
  底图盖住留白；底图读不到时退回颜色。

### 修复

- **截图 / 离屏渲染其实没有留白**：`Capture()` 与 `RenderToTexture()` 的文档写着
  "截的是整个窗口帧缓冲（含 letterbox 黑边）"，实际 `SDL_RenderReadPixels` 在逻辑
  呈现开着时只回逻辑画布那块（实测 640×640 目标配 1280×720 设计只回 640×360）。
  比例一致的窗口看不出来，比例一不同截图的构图就和眼睛看到的不是一回事。
  现在读取前临时关掉逻辑呈现、读完立即恢复。

### 已知限制

- **底图只做 cover**：`stretch` / `tile` / 九宫格没做（`window.backdropImage` 没有
  "适配方式"字段，写别的会被生成器拒）。要平铺请用素材自己拼一张。
- **底图不进主题表**：主题表仍然只做颜色（`colors` / `fontSizes`），切主题换底图
  需要改配置。加 `images` 表会破掉"主题只做颜色"这条边界，评估后没做。
- **比例锁仍没有**：`SDL_WINDOW_RESIZABLE` 恒开且未调 `SDL_SetWindowAspectRatio`，
  用户自由拖拽窗口时留白必然出现——这是"UI 永不变形"的代价，不是缺陷。

---

## 0.9.1 —— 2026-11-04（发布版：动态面板的滚轮，拉通道接上消费方）

> 明细见 [`plans/2026-11-04-dynamic-wheel.md`](plans/2026-11-04-dynamic-wheel.md)（T-01…T-05）。
> 起因是 `TaskGuide-G.md` §7 登记的那条框架级待办（G-07「动态面板不做滚轮」）。
> 探针把"动态档没有可靠的滚轮读取时机"拆成两半：**逻辑步**读不可靠
> （喂 30.0 只读到 20.0，61 帧里 11 帧为 0），而**每渲染帧一次**的
> `onAnimationTick` 完全可靠（60 帧调用 60 次、3 次喂入全读到、累计 30.0 一格不差）。

### 新增

- **`InkingDynamicPanel` 支持滚轮**（T-03）：此前只有拖动滚动条，滚轮一声不响地
  被丢掉。现在有两条入口、**一份算术**——
  `onPointerWheel`（指针压在面板里的静态子孙上，推通道 · 容器优先）与
  `onAnimationTick`（指针压在面板的空白区，拉通道 · 每渲染帧读一次）。
  归属判定四道门（不能滚 / 指针在窗口外 / 本帧没增量 / **已经有人接了** /
  指针下最上面的动态节点不是本面板），所以同一帧只会滚一次。
- **`InkingScene::GetPointerWheelTarget()`**（T-01）：这一帧的滚轮**实际交给了谁**
  （没人接时为 `nullptr`）。与 `_wheelDispatched` 的差别就是它存在的理由——后者
  只表示"进过派发流程"，指针在空白处时同样为真，判不出有没有人接。
- **`InkingScene::GetTopmostDynamicAt(x, y)`**（T-01）：指针下最上面的动态节点
  （先排序再从绘制列表尾部往前找；不排序就是拿旧 z 判新层级）。
- **`InkingApplyWheelToScroll(spec, dx, dy, offsetX&, offsetY&)`**（T-02）：
  滚轮增量 → 滚动量的**唯一**一份换算（`dataStruct/InkingScroll.h`，纯函数、
  不夹紧——夹紧只在 `SetScroll` 里做一次）。
- **`inking_demo` 右侧多了一张可滚的卡片**（12 项 / 内容 772 高 / 视口 300）：
  滚轮这件事自检只能证明数值与归属，证明不了手感。三个要试的点写在
  `examples/demo/main.cpp` 的 `AddScrollCard()` 注释里（空白区滚 / 按钮上滚 /
  滚到底）。

### 变更

- **`InkingDynamicPanel::HandlesSubtreeWheel()` 现在返回 `scroll.enabled`**
  （0.9.0 那份注释写着"对动态面板没有意义"，已经过时并重写）：它是"指针压在
  静态子孙上"那一档的唯一入口，不重写就是"滚到有按钮的地方就停"。
- `InkingStaticPanel::onPointerWheel` 改为调 `InkingApplyWheelToScroll`：
  **数值逐字不变**（一格 48 设计像素、方向不变），只是把算术挪到两档共用的地方。

### 修复（勾选件 0.9.0 遗留：单选组 `Add` 只建半边链接）

- **三个单选能同时选中**（`examples/showcase` 的"小 / 中 / 大"）：
  `RadioGroup::Add` 只把成员 `push_back` 进列表，**不回写 `radio->_group`**；而
  `Radio::onPointerClick` 正是靠 `GetGroup()` 判断"要不要走组"，于是走的是
  "没挂组"那条分支——各自 `SetChecked(true)`，互斥形同不存在。展示页用的就是
  `_radios.Add(node)`，所以每点一个就多一个选中的。
- 修法：`Add` 改为转调 `Radio::SetGroup(this)`（链接怎么建只留一份实现：退旧组 →
  置指针 → 反手调回 `Add`，第二次进来只补列表）。**`Remove` 一并把成员的
  `GetGroup()` 清回 `nullptr`**——同族隐患：直接 `Remove` 之后成员以为自己还在组里，
  点击走 `Select`、组里却查不到它，静默失效（不报错、也不选中）。
- 自检补 ③b 八条（`CheckToggleLike`）：`Add` 后 `GetGroup()` 指向新组、换组是搬走
  不是复制、组侧加的两个成员照样互斥、`Add` 幂等、`Remove` 清指针、摘掉之后点击
  回到"单用"语义。**③ 只用了 `SetGroup`（成员侧），这条缺口从没被覆盖**。
  实测：把 `Add` 临时还原成旧实现，③b 红 5 条；修完 8 条全绿，`ctest` 8/8。
- 行为影响面：只有"从组侧加人"那条路从单向变双向；用 `SetGroup` 的既有代码一个
  字节都不变（`SetGroup` 原本已经是完整的）。

### 已知限制

- **嵌套两层滚动容器只有外层生效**：`FindSubtreeWheelOwner` 给的是"最近的声明者"，
  要内层优先得按指针路径逐层仲裁（每层都要知道"子层消费了吗"）。本轮只有一层时
  是对的，两层时外层生效。
- 滚轮不做惯性 / 平滑滚动（那是一段动画，不是一次位移）。

---

## 0.9.0 —— 2026-11-04（发布版：几何分档收口 + 命名空间收口 + UIKit 齐全度 a/b 批 + 块叠加组）

> 本节已随 `InkingVersion` 一起定稿（`0.9.0` / `release`）。明细见
> `docs/zh/TaskGuide-G.md`（G-00…G-08）、`docs/zh/TaskGuide-N.md`（N-01…N-04）、
> `docs/zh/plans/2026-11-03-uikit-completeness-tasks.md`（UIKit 齐全度 a~f），
> 以及 `docs/zh/plans/2026-11-04-sprite-group-and-animate-group.md`（T-01…T-08，
> 块叠加组与帧事件地基）。

### 新增

- **四个组件的动态档**（G 系列）：`InkingDynamicImage` / `InkingDynamicSprite` /
  `InkingDynamicLabel` / `InkingDynamicPanel` 补齐，`ButtonLike` 三件套与滚动、占位
  等能力随档落地；生成器四种组件都认 `"dynamic": true`（判据是
  `inkgen_compile_check` 里的 `static_assert(is_base_of_v<…>)` 八条）。

### 变更（**破坏性**：所有消费方必须整包重编）

- **去掉 `namespace inking` 外壳，全部公开符号改为全局命名空间 + `Inking` 前缀**
  （N 系列）：类型 126 个、自由函数 119 个、常量 33 个统一改名
  （`inking::ButtonData` → `InkingButtonData`、`inking::SetDesignSize` →
  `InkingSetDesignSize`、`inking::kVersion` → `InkingVersion`）；
  子命名空间 `inking::detail` / `inking::prefab` / `inking::json` 改为
  `inking_detail` / `inking_prefab` / `inking_json`；
  生成代码命名空间默认 `inking::cxxcss` → `cxxcss`（`--namespace` 可改）。
  **头文件路径与 CMake 目标名不变**（`#include <button/ButtonData.h>`、
  `inking::inking` 照旧）。升级方法：把 `inking::X` 换成 `X`，再按编译报错把
  没带前缀的类型 / 函数 / 常量补上 `Inking`。
- **`InkingSpriteGroup` 改名为 `InkingAnimateGroup`**（0.9.0 T-02）：旧名让给了
  "一个 sprite 由多块拼成"的新组件。"一个角色的多个动作"这个类本身**一行逻辑都没改**
  ——纯重命名，`SwitchTo` / `animationChanged` / `keepPhase` / `resize` / 组内尺寸
  跟着动作改，语义全不变。连带 `InkingSpriteGroupData` → `InkingAnimateGroupData`、
  `InkingSpriteGroupModuleName` → `InkingAnimateGroupModuleName`、自检
  `CheckSpriteGroup` → `CheckAnimateGroup`。**动态库导出符号全换**（0.x 阶段允许）。
- **`InkingSpriteLike::Hooks` 的两个回调换了签名**（0.9.0 T-03）：
  `setSourceRect(const InkingShapeBounds&)` → `setSourceRects(const InkingShapeBounds*,
  std::size_t)`；`LoadedFrame::sourceRect` → `sourceRects`（矩形表）。任何自己派生
  `InkingSpriteLike` 并填 `Hooks` 的代码要整包重编——仓里没有这样的代码。
- **`CMakeLists.txt` 的 `project(VERSION)` 从 `0.7.1` 升到 `0.9.0`**：它从来没跟上
  `kVersion`（`TaskGuide-Dynamic.md` §已知限制里登记过），于是已发布包的
  `inkingConfigVersion.cmake` 写的是 `PACKAGE_VERSION "0.7.1"`，下游
  `find_package(inking 0.8.5)` 会直接失败。这次一并修掉。

### 新增（UIKit 齐全度 · a/b 批：视频接线 + 计量两件套）

> 来源：任务书 [`plans/2026-11-03-uikit-completeness-tasks.md`](plans/2026-11-03-uikit-completeness-tasks.md)
> T-01 / T-02 / T-03；估值见 [`plans/2026-11-03-uikit-completeness-audit.md`](plans/2026-11-03-uikit-completeness-audit.md)。

- **`inking_prefab::Slider`**（`include/prefab/Slider.h`）：轨道 / 已填充 / 圆盘三个
  可独立换外观的部件，`GetParts()` 把绘制与命中的几何合成一份；事件只有两个槽
  `changed(now, before)` + `released()`（写入口 `SetValue` 夹取 + 按 `step` 量化，
  **值没变就不发事件**）；拖动用"相对按下点的累计位移"，点轨道当场跳过去；
  要键盘焦点（←/→/Home/End）。默认外观取 `PrefabTheme`（紫主色 + 白盘）。
- **`inking_prefab::ProgressBar`**（`include/prefab/ProgressBar.h`）：静态档 + 确定
  进度，只读（没有事件槽、不吃点击）；`SetProgress` 夹到 `[0,1]`、同值返回 false
  （每帧喂同一个值 ⇒ 零额外重绘）；进度 0 时**不画**填充块。填充宽与 `Slider`
  共用同一个纯函数（`prefab/PrefabParts.h` 的 `FillBoundsInTrack`）。
- **视频真的出画**：`INK_HAS_FFMPEG` 现在由 CMake **探测**决定（见下"变更"），
  素材 `examples/demo/assets/probe.mp4`（320×240 / 30fps / 1 秒）与自检
  `video_decode_probe` 一起落地，断言"解出帧数 > 0、画面 320×240、丢帧 0、
  画面内部不止一种颜色且看不到兜底色"。

### 新增（UIKit 齐全度 · c 批：勾选三件套）

> 来源：同任务书 T-04。

- **`inking_prefab::ToggleLike` + `Checkbox` / `Radio` / `Switch`**
  （`include/prefab/ToggleLike.h`）：三者共用一个基类——选中态、`clicked` /
  `cancelled` 两个槽、`SetChecked` 的幂等规则、`GetParts()` 的两块矩形都只有一份
  实现，子类只覆盖"mark 摆在哪 / 长什么样"。勾 / 点 / 滑块都是可换的形状
  （`markShape` 默认 `Rect()` 是"用内置的"哨兵）。
- **`RadioGroup`**（显式对象，不拥有成员）：`Select()` 先清其它、再选目标，
  所以不会有一帧"两个同时选中"；一次换人发 **1 条 `clicked` + N 条 `cancelled`**。
  `Radio` 析构自动退组，组比成员先死也安全。
- **`Switch` 的 0.15 秒过渡**挂 `onAnimationTick`（每渲染帧），`SetAnimated(false)`
  之后立刻到位且布局一个像素不变。
- **`prefab/PrefabParts.h` 新增 `FillPart` / `StrokePart`**：三个调用方
  （Slider / ProgressBar / ToggleLike）共用同一份"设计坐标 → 设备像素"换算，
  此前 `Slider.cpp` 与 `ProgressBar.cpp` 各抄了一份。
- 自检 `toggle_like_probe`：翻转 / 幂等 / 组内互斥 / `mark ⊆ base` / 过渡到位 /
  **勾的方向**（读像素：左下与右上各有勾色、左上角为空——画倒了会当场红）。

### 新增（UIKit 齐全度 · d 批：浮层三件套）

> 来源：同任务书 T-05 / T-06 / T-07。

- **`inking_prefab::Tooltip`**（`prefab/Tooltip.h`）：停留 `delay` 秒后弹出，位置复用
  `inking_detail::SolvePopupPlacement`（下方放不下自动翻到上方再夹紧）；计挂在
  `onAnimationTick`（每渲染帧、带 delta、非 const），所以探针能按帧精确喂时间。
  **对指针完全透明**：`HitTest` 恒 false + `HitTestNode` 恒 nullptr——提示盖在按钮上，
  点下去还是那个按钮。
- **`inking_prefab::Dialog`**（`prefab/Dialog.h`）：模态 = **自己就是那层全屏遮罩**
  （静态面板、几何 = 设计画布、进命中表），中间那块框是它的子面板。三个槽
  `accepted` / `cancelled` / `action(int)`；Esc 取消、Enter 确认并带出
  `SetDefaultAction()` 的索引；点遮罩**默认不关**。事件顺序固定"先关、再发"。
  焦点：打开即夺取，**每次点面板被清掉后下一帧自己收回**，关掉即释放。
- **`inking_prefab::ToastHost`**（`prefab/ToastHost.h`）：连发多条从下往上堆、
  到期自消失；`Show()` 返回自增 id（`dismissed` 带 id 而不是下标——下标会前移）；
  `Clear()` 不发 `dismissed`。到期处理是"**先收集 → 循环外统一删 → 再重排**"，
  不在遍历中途删子节点。同样对指针透明。
- 自检 `tooltip_delay_probe` / `dialog_modal_probe` / `toast_lifetime_probe`。

### 新增（UIKit 齐全度 · e 批：导航）

> 来源：同任务书 T-08。

- **`inking_prefab::TabBar`**（`prefab/TabBar.h`）：标签条（顶 / 底 / 左三档）+ 内容区，
  同一时刻只显示一页。切页靠 `SetVisible`（骨架变化 ⇒ 命中表重烘，"切走的页点不到"
  是白拿的），顺序**先藏旧再显新**（否则中间帧两页同显）。`pageChanged(now, before)`
  只在真换页时发——**首页自动选中不发**（初始状态不是"换了页"）。
  选中指示是装饰遍里画的下划线（`onRenderDecoration`，在子孙之上）。
- 自检 `tabbar_switch_probe`：三页、切页后同一位置 `QueryHit` 落到新页、同页再切零事件、
  越界拒绝、点标签走同一个 `Select` 出口。

### 新增（UIKit 齐全度 · f 批：系统集成）

> 来源：同任务书 T-09 / T-10。

- **光标形状**（`input/CursorShape.h` + `InkingWidget::GetCursorShape()`）：
  组件声明"指针停在我身上时该长什么样"，默认 `Default`（箭头）。按钮 / 滑块 /
  三件套 = `Hand`，输入框 = `Text`。窗口每帧读**一次**当前悬停节点，
  **形状变了才** `SDL_SetCursor`（每帧无条件调在 Windows 上会闪）；
  每种形状一个缓存的 `SDL_Cursor*`，窗口收尾时统一 `SDL_DestroyCursor`。
  观测口 `InkingWindow::GetCursorUpdateCount()` 记的是"判定"次数（无头下没有真光标）。
- **文件拖放**（`InkingScene::filesDropped`）：拖放是**第四类输入**，不进指针通道。
  窗口在事件循环里只收集，**帧末一次性派发**给活跃场景——一次拖 3 个文件是
  **一条**事件带 3 个路径，不是三次回调；订阅者因此可以在回调里安全切场景。
  没有活跃场景时丢弃并计数（`GetDroppedWithoutSceneCount()`），不静默。
- 自检 `cursor_shape_probe` + `drop_file_probe`：悬停按钮换一次、悬停不动不重复、
  移开换回；拖 3 个文件一条派发、无场景时丢弃 +1 且不派发。

### 新增（开发工具 · 单控件探针模板）

`examples/probe_template/main.cpp` + 同名目标 `inking_probe_template`（`ctest` 里是
`probe_template`，约 1.7 s 跑完）：

- **给"一个控件的细账"用的模板**。这类断言别再往 `examples/demo/selftest.cpp`
  （已 ~9800 行）里堆——那里验的是"库作为 DLL、消费者视角能不能用"。
- 复制一个文件、改顶部标出的**五处**（场景名 / 建组件 / 状态 / 事件 / 像素）即可。
  样例本身是一个真跑的滑块探针：夹取与量化、`changed` 只发真变化且参数顺序是
  `(现在, 之前)`、左右两半像素分别是填充色与轨道色。
- 约定照旧：**退出码 = 失败项数**；建不出渲染器 `return 77`（ctest 显示 Skipped）；
  默认 `dummy` 后端，`INKING_PROBE_DRIVER=windows` 换真后端。
- 带一条**防假绿**的断言：左右两半颜色必须不同——否则配色改成同色时，
  "左半 = 填充色 / 右半 = 轨道色"这两条仍会过。

### 新增（示例 · 控件总览）

`examples/showcase/main.cpp`（目标 `inking_showcase`）——**一页看全预制件**：

- 五个分区：按钮 / 文本输入、计量（滑块 ×2 + 进度条）、选择（复选 / 开关 / 单选组）、
  浮层（通知 / 对话框 / 悬浮提示）、标签页（三页各有内容）。
- 每个交互都写回右上角状态行，点下去有没有反应一眼可见；三个浮层都挂在场景上
  （不在卡片里），层级按"提示 → 对话框 → 通知"排。
- 自带**离屏截图**：`INKING_SHOWCASE_SNAPSHOT=<png>` 走 dummy 后端 + 读回，
  不抓屏、不受 DWM / 远程桌面影响；`INKING_SHOWCASE_OVERLAY=1` 连通知与对话框
  一起摆出来。改完布局先自己看一眼，别让人肉当第一道检查。
- **外观页能真的换肤**：装两张主题表（`light` / `dark`），点色板 → 版本 +1 →
  所有带 token 的组件下一帧重查。截图模式另有 `INKING_SHOWCASE_THEME=dark`
  （启动即深色）与 `INKING_SHOWCASE_SWITCH_TO=dark`（**先跑一帧再切**，
  验证的是"运行中换肤"那条路径）。
- 示例里两处写法值得抄：单行标签的垂直居中用「顶留白 = (高 − 字号)/2」而不是
  `alignV = Middle`（后者按文字块算，字形会偏上 5~6px）；输入框要自己垫一块
  浅色底（它继承标签，本身没有背景那层）。

### 修复（帧事件派发：回调里换配置，0.9.0 T-01）

- **帧事件回调里调 `SetSpriteData` 原来是未定义行为**：`fireEventsBetween` 边遍历
  `_sprite.events` 边调回调，而回调里换配置会把整个 `_sprite` 替换掉——正在遍历的
  容器当场悬垂，外层还会拿旧动画的帧号再发一次 `frameChanged`。
  **自检实测（改前）**：4 帧配置、事件挂 `0/1/3`，一次 `TickFrame(0.25)` 跨 `0→3`，
  在 `one@1` 回调里换第二份配置，事件流变成 `one@1,swapTwo@2`——本该发的 `three@3`
  丢了，**新配置的 `swapTwo@2` 被提前扫进来**（半新半旧），外加一个报旧帧号的
  `frameChanged`。
- 两处改法：**快照派发**（派发前把"名字 + 帧号"拷一份，循环只读拷贝，与
  `InkingEvent::Emit` 同一条规矩）+ **切换延后**（派发期间 `SetSpriteData` 只登记，
  本帧落帧结束后执行）。改后同一场景事件流是 `one@1,three@3`、切换落在第 0 帧、
  `frameChanged` 不发。
- **语义边界（写进文档，别当 bug）**：同一帧里切两次是**后设覆盖先设**，前一次
  静默丢弃——不排队。UI 语义是"这一帧算一句台词"。
- `nm` 差分只多两个符号（`InkingSpriteLike::SetSpriteDataNow` /
  `applyPendingSpriteData`）；三个既有精灵自检（图集 / 事件 / 换配置）**一条断言
  不改**就过。

### 新增（合批断言扩到 K=1/2/4 + demo 叠块示例，0.9.0 T-07）

- `CheckSpriteBatching` 末尾加一段：块叠加组 **K=1 / 2 / 4** 三档各渲一帧，
  `textureDraws` 与 `geometrySubmissions` 的增量**都是 1**。`CheckSpriteGroup` 已经
  比过 K=2 vs K=1；这里把 K 拉到 4——"每块各提一次"那种实现会在 K=4 上直接露头
  （K=1 时根本看不出来）。实测 `几何 1/1/1，纹理批 1/1/1`。
- `examples/demo`：菜单加「叠块」（F3），用现成的 `walk.png`（4 格 48×48）演示
  同一个节点的两个动作——`idle`（每帧 **1 块**）与 `ghost`（每帧 **2 块**，相邻两格
  叠一起 ⇒ 重影）。**没有引入第三方素材**（`tools/THIRD-PARTY-NOTICES.md` 的规矩）。
- 数据面补四个摆放字段（`InkingSpriteGroupData` 的 `selfAnchor` / `traceAnchor` /
  `offsetX` / `offsetY`，0.9.0 T-05 收尾）：静态档**几何构造即定型**（`InkingAnchor`
  没有公开写入口，那是编译期保证），没有这几个字段组只能躺在父容器左上角，除非
  父容器声明布局——而"组是个布局项"与"精灵按锚点摆"是两件事。字段名与语义照抄
  `InkingSpriteData`。

### 新增（块叠加组的动态档 `InkingDynamicSpriteGroup`，0.9.0 T-06）

- **`include/image/InkingDynamicSpriteGroup.h`**：`InkingDynamicWidget` +
  `InkingImageLike` + `InkingSpriteLike`。与静态档的差别只有"几何能不能改"与
  "进不进命中表"——帧机、绘制、动作切换**逐字相同**。
- **先复制、不抽 Like**（任务书 T-06 方案 A）：第二档刚出现就抽，抽出什么全凭猜。
  判据不是"看起来像"，而是自检里那条「两档同场景跑 100 帧，帧号 / 换帧 / 跳过
  计数逐一相等」——真分家了它先红。**唯一的重复点是 `onRender` 那十几行**（按
  `_frameRects` 逐块 `PaintImage`），先接受。
- `Resize` 之后**不需要**重算格矩形：等格是结构保证，矩形只与图集有关、与组件框
  无关。自检钉住"Resize 之后帧数一个都不变"。
- 自检 `CheckDynamicSpriteGroup()`：`Resize(120,90)` 后 `HitTest` 新旧点一假一真、
  尺寸真的变了、帧数不变；两档 100 帧后 `帧号=1 换帧=100 跳过=0` **逐一相等**、
  `GetOwnedChildCount()` 都为 0；`SwitchTo` 与非法配置与静态档同一个出口。

### 新增（块叠加组的静态档 `InkingSpriteGroup`，0.9.0 T-05）

- **`include/image/InkingSpriteGroup.h`**：`InkingStaticImage` + `InkingSpriteLike`，
  与 `InkingSprite` **平级**（不派生它）。派生 `InkingSprite` 表面上白拿帧机，实际
  要在构造期编一份**假配置**去骗过 `InkingSpriteData::Validate()`——那是"为了绕过
  校验而编数据"，代价比复刻 30 行接线大得多。
- `InkingSpriteLike` 补三个 protected 入口：`replaceFrameTable(每帧一组矩形, keepPhase)`
  （语义与 `SetSpriteData` 的落帧那半逐条一致：第一次落帧不发 `frameChanged`、
  清"播完了"、时间基准重对）、`setTiming(...)`（只装时间参数）、`setAutoplay(bool)`
  （构造期设播放标志、**不动时间基准**——构造可能早于进场景）、`setConfigError(...)`
  （让 `GetConfigError()` / `IsValid()` 对组也有意义）。
- 组的数据面补两个字段（`InkingSpriteGroupData::imageBytes` / `imageSize`）：**生成期
  烘进来的图集字节**优先、不读盘；为空才走"运行期从资源根读"。少了它们，生成器烘的
  图集用不上——那是能力缺口，不是文档没写。
- `onRender` 对每块调一次 `PaintImage`（同纹理、不同格矩形），提交层按纹理分批 ⇒
  **2 块一帧只有 1 次 `SDL_RenderGeometry`**。不调 `PaintButtonAppearance`：那会把
  描边 / 阴影按 K 份各画一遍。
- 组的错误走 `GetGroupError()`，同时镜像进 `GetConfigError()`（一个来源、两个名字，
  不想让调用方记两个错误口）。`SetSpriteData` 被 `using` 降为 private——组的不变量是
  "一张图集 + 格号表"，不接受"换整份 `InkingSpriteData`"。
- 自检 `CheckSpriteGroup()`：结构（2 动作 / 3 帧 / 每帧 2 块）、`SwitchTo` 的三条
  语义（成功 / 切到当前是空操作 / 不存在 false + 恰好 1 条 ERROR）、非法配置构造期
  拦下且不装帧表、**合批断言**（2 块与 1 块 `textureDraws` / `geometrySubmissions`
  增量都 = 1）、像素断言（2 块与 1 块的读回 400/400 点全不同、上层覆盖顺序对）。
- 一处与任务书 T-05 验收 ④ 不同：任务是"2 块各自落在同一矩形内的预期像素上"，但
  K 块画在**同一个框**里（叠图）、BMP 不透明，读回只能看到最上面那块。改成
  「2 块 vs 1 块两次读回不同」——实现若忽略多块，两次会一模一样，判据照样红。

### 新增（块叠加组的数据面，0.9.0 T-04）

- **`InkingSpriteGroupData` + `InkingSpriteGroupAnimation`**
  （`include/image/SpriteGroupData.h`）：**一张图集 + 多个动画 + 每帧一组格号**。
  `frames[i]` 是第 `i` 帧要画的格号列表，顺序即叠加顺序。
- **等大是结构保证，不是运行期校验**：只给网格（`frameWidth/Height` +
  `columns/rows` + `margin/spacing`），不给矩形表——"同一帧里两块大小不同"这种事
  根本配不出来。这跟 `InkingAtlasData`（帧是矩形表、宽高可不等）**正好相反**，
  两个概念并存，分工写在文件头注释里。
- 三条定死的语义：**格号允许重复**（同一帧同一格出现两次合法，故意叠两层做发光，
  不查重）；**每帧块数允许不同**（不强制槽位固定）；**格号越界归 `Validate()`**，
  不夹取（"改到哪一格"猜不出来）。
- `Validate()` 九条：`source` 空 / 网格字段非法 / 动画表空 / 动作名为空 / 重名 /
  `fps <= 0` / 动画没有帧（或某帧没有格号）/ 格号越界（报"哪个动画的第几帧第几块"）/
  事件帧越界。
- `CellRect(cell)` 是"格号 → 像素矩形"的**唯一**算术点（行优先），`SheetWidth()` /
  `SheetHeight()` 与 `SpriteSheet` 同公式，供构造期与真实图尺寸比对。
- `Normalize()` 是**幂等的空操作**：这个类型没有"能修正"的字段（等格是结构保证、
  越界只能报错、`fps` 猜 12 就是静默兜底）。留着是为了与 `InkingSpriteData` 的
  构造流程对称。
- 自检 `CheckSpriteGroupDataValidate()`：合法配置 + 幂等 + 格号重复合法 + 九条
  非法各断关键子串 + `FindIndex` 的 `kNotFound` + `CellRect` 的 margin / spacing /
  行优先 / 越界四组坐标断言。

### 变更（帧机泛化：每帧一组矩形，0.9.0 T-03）

- `InkingSpriteLike::LoadedFrame` 的 `sourceRect`（一个矩形）→ `sourceRects`
  （**矩形表**，空 = 不设源矩形，序列帧用）。`Hooks::setSourceRect(const&)` →
  `setSourceRects(const InkingShapeBounds*, std::size_t)`。理由只有一个：块叠加组
  的一帧要画 K 块，而帧事件那套语义（快照派发、切换延后、区间扫描、回绕拆段）
  宁可只写一遍，也不给新组件抄第二份。
- **破坏性（ABI）**：`Hooks` 布局变了，任何自己派生 `InkingSpriteLike` 并填 `Hooks`
  的代码要整包重编。仓里没有这样的代码（两个档的 `hooksFor` 是同仓库的）。
- **单块退化成 `size() == 1`**：现有三档产出的表恒为一个元素，值与改动前逐字节相同。
  判据是行为零变化——`CheckSpriteSheet` / `CheckSpriteSheetFirstFrame` /
  `CheckSpriteUnevenFrames` / `CheckSpriteEvents` / `CheckSpriteBatching` /
  `CheckRuntimeSwap` **一条断言不改**就过（`CheckSpriteSheet` 本来就是离屏逐像素
  比对，不是"看起来对"）。
- 单块档收到多块源矩形（数据层漏校验才会出现）时：**画第一块 + 一条 WARN**，
  不整帧丢弃——丢弃会让"配错了"表现成"东西没了"，更难查。
- `nm -g libinking.dll` 差分（对比 T-02 之后）：**+2 / −1**，全是模板实例与 COMDAT
  存在性（`std::string::replace` 的实例化、`InkingButtonData::Normalize` 的有无），
  **没有公开 API 符号消失**。只记录，本任务不要求"零变化"。

### 修复（UIKit 齐全度 f 批：换肤接线 / 通知条高 / 自检护栏）

- **运行期换主题时"有的地方不跟着变"**（三处独立的接线缺口，症状都是一句
  "点了没反应"）：
  1. `prefab/TabBar` 把 `background` / `content` 转成内部面板时**只搬了 `color`、
     丢了 `themeColor`** —— token 到这一层就断了。改成整份外观传进去；
  2. `prefab/Slider` / `prefab/ProgressBar` 的绘制只读 `appearance.color`，
     从不查 `themeColor`：换肤后轨道 / 填充仍是旧色。改走 `InkingResolveThemeColor`；
  3. **按钮文字色没有 token 通道**（`InkingButtonData::textColor` 是纯颜色）。
     深色主题下"浅底按钮 + 深色文字"变成"深底 + 深字"，一个字都看不见。
     新增 `textColorToken`（与 `themeColor` 同一条约定：指向静态生命周期串）。
- **`Selftest`**：T-09 的悬停断言改成**结果导向**（先推一次 motion，落到按钮才验），
  真后端再跳过"不重复设置 / 移开 +1"两条——那是窗口焦点抖动，不是代码对错。

- **通知条的文字被裁掉大半**（`prefab/ToastHost`）：堆叠时用 `InkingMeasureText`
  量文字高，但请求里**没放文案**——空串量出来是 0，于是条高 = 上下留白 = 16，
  14 号字放不进去。行高只跟字号 / 字体有关，给一个固定样本串即可。
  自检当时只断言了"不重叠"和 y 坐标，没断言高度，所以没抓到。
- **`selftest.cpp` 的 T-09 光标探针**加了一次命中查询预热：`InkingWindow::Open()`
  之后、第一次指针派发之前，命中表可能还没烘焙（现象是 `hovered` 恒空、光标计数
  却不加，看起来像"光标没接上"）。**这条是绕过不是修好**，根因尚未定位，
  已记进工作日志待查。

- **视频自检把丢帧判据写成了"必须为 0"**：主时钟是真实时间，机器一忙（编译 / 并行测试）
  一次 sleep 打盹就丢一帧，同一份代码跑出过 28/0 与 28/1 两种结果。改成**比例判据**
  （丢帧 ≤ 两成）——历史那个 bug 的形态是"出一帧丢一大半"，比例判据照样能抓住。

- **`Dialog` 的键盘焦点会被点面板清掉**：场景的规则是"点到的组件不想要焦点就别留着"，
  于是"点一下对话框"之后 Esc / Enter 就没有落点（现象是"快捷键突然没反应"）。
  现在对话框在 `onAnimationTick` 里把焦点收回子树——**只补不抢**：焦点已经在子树里
  （例如点进了内容里的输入框）就一个字节都不动。

### 变更（依赖面：FFmpeg 从"从未开启"变成"探测到就开启"）

- `pkg_check_modules(FFMPEG ...)` 去掉 `REQUIRED`、加 `QUIET`；命中则
  `target_compile_definitions(inking PRIVATE INK_HAS_FFMPEG)` + 私有链接，
  未命中打一条 `WARNING` 并编成空实现。判据跟着**探测结果**走，不跟人手开关走
  ——没有"忘了开"这个状态（理由与 `INK_HAS_SDL3_MIXER` 漏定义那次同源）。
- **FFmpeg 从 PUBLIC 改 PRIVATE**：公开头只做 pimpl、不 include 它，传播只会让
  "只要 UI、不要视频"的工程被迫装开发包。**注意**：`bin/` 从此带 `av*.dll`
  （`ldd` 驱动的 `stage_runtime_deps.sh` 自动收齐，包括 `dav1d` / `x264`），
  按 LGPL-2.1+ 动态链接分发。
- `examples/demo/CMakeLists.txt`：资源拷贝从"每个 exe 各挂一条 POST_BUILD"改成
  一个 `inking_demo_assets` 目标拷一次。原写法在并行链接时两个 `copy_directory`
  同时写 `bin/assets/`，Windows 上偶发 `Error copying directory ... Permission
  denied`，整条 `cmake --build` 随机失败。

### 修复

- **视频画面被兜底色乘成黑色**：`fillShapeTextured` 的顶点色是"纹理 × 顶点色"，
  RGB 也参与乘法，而 `InkingVideoData::normal.color` 的语义是"**没有画面时的
  兜底色**"、默认值为不透明黑。于是默认配置下视频"解码在走、帧数在涨、
  一个像素都不出"。现在有画面时只借它的 **alpha** 做淡入淡出、RGB 归白
  （自检先跑出"解码 28 帧 / 上屏 28 帧 / 抽到 1 种颜色"，修完是 9 种颜色）。
- **滑块点不到**：`InkingWidget::HitTest` 基类恒返回 `false`，新控件不重写就是
  "按下去什么都不发生、也不报错"。`Slider` 现在按整块组件框答（`HitTestLocalRect`，
  与绘制同一套坐标）。

### 文档

- `README.md` / `tools/THIRD-PARTY-NOTICES.md` / `tools/BUILD.txt` 三处口径改成
  "0.9.0 起默认开启 + 动态链接分发"（`BUILD.txt` 是 0.8.5 的发布清单，仍保留
  当时的记录，只加"0.9.0 已解决"的标注，不改哈希行）。

### 已知限制（本批）

- 新控件**不进 CXXCSS**（配置量小；扩张生成器要改三处 + 13 条自检）。
- `Slider` / `ProgressBar` / 三件套 **都没有 `SetPainter` 第三档**：`InkingCanvasContext`
  的构造是 `private`（`friend` 只有 `InkingStaticCanvas`，必须带一个 `CanvasRecorder`），
  叶组件挂不了绘制器。要整块自绘请把 `InkingStaticCanvas` 叠在上面。
- 部件级**贴图**（`InkingButtonAppearance::type == Image`）没接：部件会按各自
  `color` 画成纯色，`Slider` / `ProgressBar` 构造时会打一条 `WARN`（不静默）。
- `Slider` 只做横向、不做双滑块（区间选择）；三件套**没有键盘空格切换**、不做三态
  Checkbox（半勾）。
- `Switch` 的状态**只由滑块位置表达**（轨道颜色不随状态变）；要变色得换 `base` 外观，
  而这一版没有 `SetData` ⇒ 意味着重建组件。
- `Tooltip` **不换行**：`maxWidth` 只夹面板宽，超长文本会溢出（多行要动文字层）；
  落位只算一次（锚点在延迟期间移动不会跟随）。
- `Dialog` 的遮罩面板必须**晚于**它要挡住的内容创建（同级按注册序号 + z 比较）。
  需要压住"更晚创建"的东西时，对 Dialog 自己调 `ChangeZIndex`。
- `Dialog` 的 `Close()` 不发事件（程序关掉不算"用户取消"）——想区分请自己记账。

### 修复（发布后补丁：勾选件的 mark 不看选中态）

- **Checkbox / Radio 点一下"状态没变"**（`examples/showcase` 报的现象，0.9.0 发布后
  修）：`ToggleLike::onRender` 画 `mark` 时**从不读 `_checked`**，于是勾 / 圆点常驻。
  事件照发、`IsChecked()` 照翻，"只有画面不动"——看起来就是"点了没反应，但操作记了账"。
  自带的 T-04 自检只验了"选中时有勾"，验不到反方向，所以没抓到。
  现在 `onRender` 按 `_checked` 决定画不画，并把这条开关拆成
  `ToggleLike::PaintsMarkWhenUnchecked()`（`protected` 虚函数，默认 `false`）——
  `Switch` 覆写成 `true`：轨道上的圆滑块关着也得看得见，它是"位置表达状态"，
  不是"有 = 选中"的记号。
- 自检补 ⑧ 三条（`CheckToggleLike`）：未选中 Checkbox 的勾像素 = 0、未选中 Radio
  的圆点像素 = 0、**反证**同一块区域选中时 > 0（否则"永远不画"也能过），外加
  "关着的 Switch 仍画滑块"——一刀切下去会把这个例外删掉，而画面只是少一个圆。
  实测：修前勾 20/20、圆点 27/27（两个状态一模一样），修后 20/0、27/0。

---

## 0.8.5（`0.8.3` / `0.8.4` **未单独发版**：文字层的上限与静默）

> 来源：任务书 [`plans/2026-10-30-text-layer-caps.md`](plans/2026-10-30-text-layer-caps.md)
> （本日缺陷排查：`-Wall -Wextra` 全量编译的严格告警 / `-fanalyzer` 66 个 TU 0 条 /
> 7 个探针实测）。
> 一条结论拆成 5 个任务：**文字层有三条路径没有上限、且全部静默**。
> 三个批次（0.8.3 止血 / 0.8.4 发声与预算 / 0.8.5 告警门）在同一次发版里交付，
> 所以版本号直接是 **0.8.5**——与 0.7.0 未单发、只发 0.7.1 是同一个惯例。

### 修复

- **文字会画到组件框外**（T-01，0.8.3）：`TextOverflow::Clip` 是默认值、头文件里写着
  "多余的行不画"，但**从来没有实现过**——`maxHeight` 只参与垂直对齐。实测 300×40 的
  标签 + 100 个汉字：7 行全画，**框外 10,030 个亮像素**。现在排版层按"累计行高 ≤
  `maxHeight − 上下 padding`"丢尾行并置 `truncated`；命令行改完是 **1 行 / 框外 0**。
  两条边界：**至少留一行**（框比一行还矮也画，不让标签静默变空白）、**不缩字号**。
  想让长文案溢出的把 `height` 设 0 即恢复旧行为（不需要改库）。
- **图集满之后每帧白烘一遍字形**（T-02，0.8.3）：1024² 图集灌满后，文字一个字都没变
  也每帧把整段重新栅格化再失败（实测 19 次/帧、60 帧多烘 1140 次），最后照样退回整段
  渲染。新增 `TextLayer::glyphAtlasFull`：满了整条逐字形路直接短路，行为与之前**一致**，
  只是不再白烧 CPU。复位只在 `ReleaseGlyphAtlas()`（换比例 / 换字号档 /
  `ReleaseTextCache`）。

### 新增

- **图集满会发声**（T-03，0.8.4）：第一次满时一条 WARN（已用格数 / 上限 / 后果），
  计数进 `GlyphAtlasStats::full`。静默失效清单新增第 13 条 `glyph_atlas_full`
  （`AGENTS.md` §7 + `kSilentFailures` + `NoteSilentFailure` 三处同改）。
- **文字纹理的字节预算与 LRU 淘汰**（T-04，0.8.4）：`detail::SetTextureBudget(bytes)`，
  **默认 0 = 不淘汰**（与 0.8.3 逐字节相同）。`SceneLibrary::RenderScene` 每帧先调
  `detail::BeginTextFrame` 推进帧号；超预算就从最久没用过的条目丢起，丢之前先
  `FlushGeometry()`（批里可能还引用着那张纹理）。四条边界写进
  `api/InkingRender.md` §4.1：瞬时上界是"预算 + 本帧新增"、字形图集不参与淘汰、
  预算比图集还小时放弃淘汰、字节数按 RGBA8 上界算（偏保守）。
  实测：预算 0 时 402 张长串 ⇒ 12.8 MB 只涨不落；预算 8 MB ⇒ 8.43 MB
  （≤ 8 MB + 单张 61,776），淘汰 271 次，被丢的串重画能重建。
- **严格告警门 `strict_warnings`**（T-05，0.8.5）：`ctest` 从 6 条变 7 条。用同一批源
  （`src/` + `tools/inkgen/`）、另一套 flags（`-Wall -Wextra -Wshadow -Wcast-qual
  -Wuseless-cast -Werror`）编一个 `OBJECT` 库，目标 `EXCLUDE_FROM_ALL`（不进 `all`，
  日常构建耗时不变）。改前严格 flags 下 25 条真告警，已逐条清掉；`-Woverloaded-virtual`
  指向 `InkingWidget::Activate` 被隐藏的那一批**这一轮不改**（改名是公开面变更），
  门里显式 `-Wno-` 并注明原因。门本身**人工验过红绿**（临时加一个 unused 变量 ⇒ 报
  `-Werror=unused-variable`；删掉恢复）。

### 变更

- 删掉三个"没人读的参数"：`detail::fillShapeTextured` 的 `hasAlpha`（0.6.2 矩形图片
  并进几何批之后两条路逐位相同）、`HitsThumb` 的 `vertical`（`ScrollBarMetrics` 里
  已经按轴算好了）、`AudioLoadFromMemory` 的 `AudioLoadMode`（从内存加载只有
  "整段解进内存"一种，传 `Streaming` 是假的）。**0.x 不承诺 ABI**，但这三处会让
  旧代码编译不过——按 0.x 的惯例，改签名就是改签名。
- `InkingStaticLabel::LayoutForWidth` / `CacheMatches` 多一个 `availableHeight`
  参数（private，外部看不到）；排版缓存键加高度（漏了就是"改了框高、行数没变"）。

### 文档

- `api/InkingRender.md` §4.1 新增"字节预算与 LRU 淘汰"；§五 更正"图集满了会整张重建"
  这句错话（真实行为是退回整段渲染 + 一条 WARN）；文本排版那一节补"框高即上限"。
- `API.md` §2 补"文字与框高"一段；§7 的沙箱注补一条实测陷阱：**手工跑自检要把
  msys64 的 `ucrt64/bin` 放进 PATH**，否则 `SDL_IOFromFile` 失败、字体打不开，
  自检会打印"没有可读字体"并跳过像素断言（那是环境，不是缺陷）。
- `TaskGuide.md` A7 补记"`Clip` 之前没实现"这件事与本轮的落点。
- `AGENTS.md` §6 追加第 113~115 条（告警门必须共用宏 / `-Werror` 与工具链耦合 /
  删死参数之前先想它是不是历史遗留的决定）。

### 已知限制

- **帧内峰值不受预算约束**：淘汰发生在帧首，本帧刚烘出来的纹理不能丢（丢了当场缺字），
  所以一帧里烘出 40 MB，那一帧照样是 40 MB。
- **预算小于字形图集（1024² ≈ 4 MB）时不做淘汰**：否则每帧清空重烘，比不设预算更糟。
- 图集"满了"是**粗粒度**判断：它只说明"这一格塞不进去"，更小的字形其实还塞得下。
  按字形做 negative cache 能多塞几个小字，但要多一张表与失效逻辑，这一轮不做。

---

## 0.7.1 之后（**未单独发版**：证据链 / 交付路径 / 内存账本）

> 来源：任务书 [`plans/2026-10-29-evidence-and-delivery.md`](plans/2026-10-29-evidence-and-delivery.md)
> （「这个项目还需要什么」的五条建议，逐条核实后的 5 个任务、3 个批次）。
> **不改任何渲染行为**：只把"已经对了"变成"随时可验证"，再加一本内存账。
> 版本号仍是 **0.7.1**——`dist/inking-0.7.1-win64` 这份预编译包**已经包含**本节全部
> 改动（见包内 `BUILD.txt` 验证记录），统一 bump 到 0.8.0 留给下一次发版。

### 新增

- **行为自检进了 `ctest`**（T-01）：`ctest` 从 4 条变 5 条，多出来的那条跑的是
  565+ 项产品行为断言。此前"`ctest` 全绿"证明不了产品能用。
  `-DINK_BUILD_EXAMPLES=OFF` 的构建里没有自检 exe，靠 `if(TARGET …)` 守卫少注册一条
  ——有意为之，不是漏配。
- **真后端一档**（T-02）：环境变量 `INKING_SELFTEST_DRIVER=windows` 让自检跑
  direct3d11（`ctest` 里是第二条测试 `selftest_windows`，仅 Windows 注册）。
  默认仍是 `dummy`（"任何机器都能跑"的唯一保证）。真后端建不出窗口时打印
  `[skip] 真后端不可用` 并 `return 77`，`ctest` 显示 **Skipped** 而不是 Failed。
  动机：0.6.4 那次"采样模式在软件后端几何批上不生效"正是 GPU 与 software 行为
  不同的一类，只验一种后端永远抓不到。
- **纹理字节账本**（T-05）：`TextCacheStats` 加 `textureBytes`（当前）/
  `textureBytesPeak`（峰值）/ `textureCount`（条数）。四个建纹理的入口（字形图集 /
  整段文字 / 图片 / SVG）在**拿到有效句柄之后**记账，四条释放路径（SVG 档位淘汰 /
  换渲染器 / `ReleaseTextCache` / `ReleaseGlyphAtlas`）同步销账。
  口径是 **RGBA8 上界**（`宽×高×4`），**不含渲染器内部缓冲**、也**不做淘汰决策**。
- **静默失效清单新增 `texture_bytes_leak`**（`AGENTS.md` §7 + `kSilentFailures` +
  `NoteSilentFailure` 三处同改）：账本只建不销账时，内存只涨不回落、画面上看不出来。
- **示例素材随包分发**：包里多了 `bin/assets/design.svg` 与 `walk.png`（1.3 KB + 894 B）。
  不带它们时，包里那个 demo 照样起窗、退出码 0，但背景是黑的、图集那块是空的——
  "双击看效果"这条路径等于没走通，而且不报任何错。

### 变更

- `tools/package.sh` 加**一致性护栏**（T-04）：`include/inking.h`（版本号的唯一真相）
  比 `build-release/bin/libinking.dll` 新时直接失败并提示先重建；缺
  `inking_demo_selftest.exe` 也失败。此前两者互不比对，忘了重建会**静默**打出
  "标着新版本、装着旧代码"的包。判据用 mtime 而不是"DLL 里搜版本串"——实测 DLL 里
  搜不到任何一个版本号（`kVersion` 是 inline constexpr，未必被引用）。
- `tools/BUILD.txt` 补"重新打包（维护者）"三步，写明护栏挡住什么、挡不住什么。
- `examples/minimal/CMakeLists.txt` 注释不再写死 `inking-0.5.2-win64`（写死就是下一次
  的过期文档）。
- **`dist/` 从 0.5.3 追到 0.7.1**（T-03）：`find_package(inking CONFIG)`、安装布局、
  DLL 闭包（19 个）这条路径 5 个版本没被走过，本轮实测走通；`examples/minimal`
  用该包 configure + build + 运行全通过。**旧包保留不删**（可能有人钉着它）。

### 文档

- `docs/zh/API.md` §7：删掉"不要看退出码"那段（退出码就是失败项数，`0.1.1` 起
  已可直接当判据），补 ctest 6 条与 `INKING_SELFTEST_DRIVER` 的用法。
- `docs/zh/api/InkingRender.md`：`TextCacheStats` 补三个字段与**四条口径边界**
  （偏高 / 不含渲染器内部缓冲 / 只统计这一层 / 只记账不决策）。
- `docs/AGENTS.md` 第 29 条：补"示例自检一直不在列表里"这件事的结局与正确落点。

### 已知限制

- **`textureBytes` 偏高**：不透明图真实分配可能是 RGB888、贴图可能有对齐填充；
  精确值的路（`SDL_GetTextureProperties`）已被 0.5.1 实测否掉（返回 UNKNOWN）。
- **账本不含渲染器自己的内部缓冲**，所以它不是"进程显存占用"。
- **一致性护栏抓不住"改了代码但忘了 bump 版本号"**：那属于版本纪律；mtime 只回答
  "这份二进制比这份头新吗"。另外 git checkout / 解压会刷 mtime，可能误报一次
  （提示语里写了"先重建一次"）。
- **`examples/pet` 与 `examples/sharp` 不在包里**：它们是仓库内的验收用例，不是
  运行期依赖（`sharp` 那张 5.97 MB 素材因此不进包，源码在仓库里）。

---

## 0.7.1 —— 2026-10-29（发布版：缩放护栏 / 素材分辨率提示）

> 来源：关于「设计像素 / 素材像素 / 纵深缩放 / 边缘模糊」的四轮讨论。任务书
> [`plans/2026-10-29-scaling-and-device-pixels.md`](plans/2026-10-29-scaling-and-device-pixels.md)
> 把结论收敛成**三条静态规则 + 两个小护栏**（0.7.0 规则 / 0.7.1 护栏）。
> **0.7.0 没有单独发版**，两个批次在这一版一起收口。
>
> 两个明确的"不做"，避免以后再讨论：**不做运行期重采样、不做按目标尺寸的缓存表**
> （本库取向是"内存换效率"）；"任意尺寸不失真"这条**放弃**，清晰度改由静态规则
> 保证。抗锯齿带按设备像素改造推迟到 `1.x.x`（它与素材分辨率无关）。

### 新增

- **`SetTextRasterCap` / `GetTextRasterCap`**（`render/InkingText.h`，T-03）：文字光栅
  比例上限从常量变成运行期可配（默认仍是 3，夹在 `[1, 8]`）。
  "设计 640 + 4K 屏"（R = 6）那种配置可以提到 6×，代价是单字纹理面积 ×4。
- **`TextCacheStats::rasterCapped`**（T-03）：触顶（夹取）次数。夹取不再是静默的——
  第一次触顶报 1 条 WARN（带设计尺寸 / 想要的倍数 / 当前上限），之后只涨计数。
- **素材分辨率 DEBUG 校验**（`detail::WarnIfImageUndersampled`，T-04）：`INK_ISDEBUG`
  下在"字节到手"的三个入口（构造 / `SetImageBytes` / `LoadAsset`）比一次
  "素材够不够它占的屏幕"，`σ < 0.75` 就 WARN 并计数（`image_undersampled`）。
  Release 构建里整段被编译掉——零日志、零解码。
- **静默失效清单新增两条**：`text_raster_capped` / `image_undersampled`
  （`docs/AGENTS.md` §7 + `kSilentFailures` + `NoteSilentFailure` 三处同改）。

### 变更

- 文字层的 `kMaxTextRasterRatio` 常量 → `TextLayer::rasterCap` 字段（`kDefaultTextRasterCap`
  = 3）。**不调 setter 的进程与 0.7.0 逐像素相同。**
- 每次调 `SetTextRasterCap` 都会**重新武装**触顶告警（同值调用也算一次决策）：这是
  为了让"只报一次"能被自检确定性复现。

### 文档

- `docs/zh/api/InkingRender.md`：补 `SetTextRasterCap` / `WarnIfImageUndersampled` 两节。

### 已知限制

- 素材够不够**只在 DEBUG 下检查**：发布包里没有任何提示（零成本换来"发布包不吵"）。
- `WarnIfImageUndersampled` 的 `R_max` 用**主显示器桌面 ÷ 设计画布**近似：多显示器、
  以及"窗口被拖到更大的那块屏"这两件事它都不知道。
- 像素风素材（进程级 `SetImageSampling(Nearest)`）与 SVG **直接跳过**这条检查——
  它们"放大"是正确用法。

---

## 0.7.0 —— 2026-10-29（并入 0.7.1：缩放规则落地）

### 新增

- **`dataStruct/InkingImageScale.h`**（纯头、无 SDL，T-02）：把三条规则做成
  可调用、可断言的纯函数，与 `ImageFitSpec` 同一形态。
  - `RequiredTextureWidth()`：素材宽 = 设计尺寸 × 纵深 × `R_max`；
  - `SamplingRatio(纹理宽, R)` / `SamplingRatioAtMax()`：σ = 素材像素 ÷ 屏幕像素；
  - `IsCleanRatio(σ)`：σ 是不是整数（干净档位）；`CleanDepth(m, k)`：干净纵深 = `m / k`。
- **`examples/sharp`**（T-05）：720p 设计尺寸 + 一张 1536×2752 的真实素材
  （`assets/nailong-art-01.png`，5.97 MB / 5.69 MiB），`1/2/3` 切 Fill / Contain / Cover、
  `F11` 全屏、`ESC` 退出，每次显示状态变化打印设计 / 素材 / 绘制 / 屏幕 / R / σ。
- **自检 `CheckImageScaleRules`**（T-02）：逐字对着任务书 §3 的数字断言。
- **自检 `CheckRasterIsDeviceSpace`**（T-01）：验"栅格化发生在设备分辨率"。

### 确认（T-01 的实测结论）

**成立**（软件后端 + dummy 视频驱动实测）。判据是"纹理里的一条 1px 黑列在 R=2 时
是否仍是唯一的一列纯黑"，读数：

| 情形 | 黑列读数 |
| - | - |
| 基准（无逻辑呈现，64 纹理铺 64 屏幕） | **1 列 @ x=33**，每列 32 像素，灰像素 0 |
| R=2（逻辑 32、屏幕 64） | **1 列 @ x=33**，每列 32 像素，灰像素 0 |
| 对照组（先渲 32×32 再放大，`build/probe_devspace.cpp`） | **0 列**（黑列被丢掉） |

前两行相同 ⇒ 中间没有"先缩到逻辑分辨率再放大"这一步，§3.2 那整条推论（素材按屏幕
像素出才有意义）成立；第三行说明判据**有区分力**（不是"怎么测都是一列"）。
GPU（direct3d11）后端没有实测，留作人工验收项。

### 文档

- `docs/zh/API.md` 新增 **§9 素材该多大（缩放规则）**：四个量、三条规则、干净档位表、
  σ 判读表、`ImageScaleSpec` 用法，全部引用纯函数而不是抄公式。
- `docs/zh/API.md` §3.1 / §8 索引登记新头文件；公开头计数按实测更新。

### 验收（T-05 人工目视，2026-10-29）

自动部分（可核对）：四档 `R` 与 `σ` 与任务书预测表**逐项一致**——Cover
1.20 / 0.80 / 0.60 / 0.40，Contain 3.82 / 2.55 / 1.91 / 1.27。

人工部分（Shyi 执行）：**符合预测**。原话是"看上去是 ok 的"——Cover 越全屏越软、
Contain 越全屏越清，方向与预测一致；**同时记录一条保留意见**：目视本人对"我能看出
多软"这件事没把握，所以这条按**低置信度符合**记，**不据此放宽任何规则**。

> 这一条的可迁移结论只有一个：**这张画风上 2.5× 放大（σ=0.40）不至于难看到不能接受**。
> 它是内容相关的观感，不是"规则可以松"的证据——规则管的是"素材该出多大"，
> 而 σ<1 意味着放大这件事**确实发生了**，只是这张画的容错高。

### 已知限制（本版明确接受）

- **小设计尺寸 + 大屏时边缘略糊**：羽化带仍按设计像素（`kFeatherPixels = 1.0f`），
  R≠1 时那一圈会失真。它与素材分辨率**无关**（羽化在顶点 alpha 上），推迟到 `1.x.x`。
- **`Cover` 全屏一定越看越软**（素材只够它自己的像素数）：`examples/sharp` 的
  `Cover` 列就是这样——那是规则的正确结果，不是缺陷。

---

## 0.6.6 —— 2026-10-28（发布版：换配置 / 不规则图集 / 动画组）

> 来源：用户用自己的素材（CraftPix 史莱姆包）实测后追加三批需求——「修缺口 /
> 不规则图集 + assets / 动画组」。核实、九成以上的取舍见
> `docs/zh/plans/2026-10-25-0.6.x-frame-animation.md` §7~§9。
> **0.6.4 / 0.6.5 两个中间批次没有单独发版**，本版一次收口；下面按批次列，
> 是为了让"哪一条属于哪个目标"仍然查得到。

### 新增

**0.6.4（素材正确性）**

- **图集起始格 `sheet.firstFrame`**：起始**格号**（不是帧数）。"每行一个方向"的
  图集（`Slime1_Idle.png` 那种 6 列 × 4 行）靠它播第 1~3 行——第 r 行 =
  `firstFrame: r × columns` + `count: columns`。与顶层 `startFrame`（帧号）是两件事。
- **图片采样模式**：`detail::ImageSampling{Linear, Nearest}` +
  `SetImageSampling()` / `GetImageSampling()`。像素素材放大后必须是硬边，
  `Linear` 会把它糊成渐变。切换**立即生效**（遍历纹理表重设已建好的**图片**纹理，
  字形纹理不动）。

**0.6.5（换配置与不规则图集）**

- **运行期换配置 `InkingSprite::SetSpriteData(data, keepPhase)`**：节点指针、父子
  关系、**所有订阅全部保留**（替换组件那条路会让它们全部失效）。`keepPhase`
  保留当前帧号（同动作换方向不跳回第一帧）；换配置本身**不发** `frameChanged`。
- **帧表 `SpriteSheet::frameRects`**：每帧一个绝对像素矩形、宽高可以不等——一页
  混装多个动作时用。与网格字段互斥（同给报错），构造期逐帧验"落在图内"。
- **库内的 JSON 小工具 `inking::json`**（`util/InkingJson.h`）：解析 / 取值 /
  转义，**零异常 + 每个值带行号 + 重复键报错**。与生成器共用同一份实现。
- **图集描述 `AtlasData` + `MakeSpriteData()`**（`image/InkingAtlas.h`）：一张图
  里有哪些具名动画、每个动画的帧矩形与播放参数；纯函数转成 `SpriteData`，
  **一个图集可以被多个 sprite 共用**（字节一份、纹理一张）。
- **inkgen 认 `CXXCSS/Atlas/*.json`**：产物是 `const inking::AtlasData&
  AtlasXxx()`。网格切法在**生成期**展开成逐帧矩形（运行期只有帧表一条路）。

**0.6.6（动画组）**

> （**0.9.0 起该名指另一组件**：`InkingSpriteGroup` 改名为 `InkingAnimateGroup`，
> 旧名让给"一个 sprite 由多块拼成"的新组件。下面这条说的仍是当时那个类，不改写。）

- **`InkingSpriteGroup`**（派生 `InkingSprite`）：一个角色多个动作，
  `SwitchTo("run")` 换的是**配置**不是节点 ⇒ 节点数恒 1（没有隐藏节点参与绘制
  遍历与命中）。`animationChanged(新, 旧)` 在换完之后发。
- **`resize` 参数**：组默认跟随新动作的 `width/height`（角色轮廓随动作变是组的
  职责），`resize=false` 关掉。注意这与单个 `InkingSprite::SetSpriteData()`
  **不自动改尺寸**相反——两处语义都在文档里写明了。
- demo 追加一个动画组示例（走 / 跑两个动作，按钮切换）。

### 变更

- **`inkgen` 多一个配置类目 `Atlas`**（目录名决定类型，`Atlas` / `atlases` 都认）。
  产物里没有类：只有一份 `AtlasData` 和一个访问函数。
- **`libinking.dll` 体积 +约 35KB**：JSON 解析器进了库（`util/`）。不 include
  `<util/InkingJson.h>` 的项目零影响（不进任何 TU）。
- **矩形图片并批的羽化代价仍在**（0.6.2 起）：这一版没动它。

### 修复

- **生成期报错级联**：图集描述里某条动画自己错了之后，`AtlasData::Validate()`
  会再报一句"图集里没有任何动画"——那是派生噪音，现在只有前面没报过错时才判。

### 文档

- `CXXCSS.md`：`sheet.firstFrame`、"图集描述（0.6.5）"整节、
  三条新错例；§9 字段表按 `inkgen --dump-fields` 重生成（新增三张 `Atlas` 表）。
- `docs/zh/api/InkingJson.md`（新页面）、`API.md` 加「3.6 工具件」、`InkingComponents.md`
  补 `SetSpriteData` / `frameRects` / `AtlasData` / `InkingSpriteGroup`、
  `InkingRender.md` 补采样开关与**后端差异表**、`AGENTS.md` 第 10 条加 `util/` 分层。
- `README` 版本横幅与本文件同步到 0.6.6。

### 已知限制

- **TexturePacker 导出件不直接吃**：`frames` 是 hash（不是 array）、`rotated`、
  `trimmed` 三种形态明确不接——要用得先写转换脚本。
- **图集描述不能热更**：结构（帧矩形）进 exe，只有图片字节在 `assets/` 里。
  改排版要重编（与"换美术不用重编"的边界一致）。
- **采样模式是进程级的**：同一进程里"像素素材 + 高清图平滑缩放"只能二选一。
- **software 后端的几何批不认采样模式**（`SDL_RenderGeometry` 恒最近邻）：
  像素素材不受影响（它本来就要最近邻）；受影响的只有"软件后端 + 高清图缩放"。
- **组切换会改尺寸**：同一格子里换动作可能让兄弟节点位移——`resize=false` 可关。
- **帧表不支持帧内偏移**（`spriteSourceSize`）：帧被裁过四周透明边时会抖，不做。

---

## 0.6.3 —— 2026-10-28（发布版：帧动画 `InkingSprite`）

> 来源：用户口头需求「基于 timeline 做一个帧动画组件」+ 追加裁定「整个 0.6.x 只做
> 帧动画、内容我全都要、组件名 `InkingSprite`」。核实、十个任务与取舍见
> `docs/zh/plans/2026-10-25-0.6.x-frame-animation.md`。
> 0.6.0 / 0.6.1 / 0.6.2 三个中间批次**没有单独发版**，本版一次收口——下面按批次列，
> 是为了让"哪一条属于哪个目标"仍然查得到。

### 新增

**0.6.0（骨架）**

- **场景动画时钟与频率订阅**：`InkingScene::AnimationSeconds()` /
  `SubscribeAnimation(interval, cb)` / `UnsubscribeAnimation(handle)` /
  `AnimationSubscriberCount()` / `GetAnimationCatchUpDropCount()`，句柄类型
  `AnimationHandle = uint64_t`。语义与 `Timeline::Subscribe` 同构（累加器追赶、
  慢帧补发），但**由场景持有生命周期**——组件拿不到 `Timeline` 实例，也就不会
  在重开窗口后握着失效句柄。单帧补发上限 64 步，撞上就截断并记一次丢账。
- **帧动画组件 `InkingSprite`**（派生 `InkingStaticImage`）：`SpriteData` 描述
  "有多少帧、怎么放"，组件只管"现在第几帧"。时间只来自场景时钟，帧号是时间的
  纯函数（`frame = floor(t × fps) % count`）⇒ 慢帧不慢放、同屏同 fps 的 sprite
  帧号恒等、长跑不丢精度。
- 播放控制：`Play()` / `Pause()`（时间冻结，不是"停止换帧"）/ `Stop()` /
  `SeekToFrame()`；观测口 `IsPlaying()` / `IsFinished()` / `GetFrameIndex()` /
  `GetFrameCount()` / `GetLoadedFrameCount()` / `GetFrameAdvanceCount()` /
  `GetSkippedFrameCount()` / `GetAnimationTime()`。
- `SpriteData`（纯数据头）：序列帧 `frames` 与图集 `sheet` **二选一**，`Normalize()`
  补默认 + 夹取、`Validate()` 只报告不改数据（"能修 / 只能拒"按这一条分）。

**0.6.1（图集与配置）**

- **图集切格**：`ButtonAppearance::srcRect` / `ImageFitSpec::srcRect`——一块纹理
  只画其中一格的像素矩形，全零 = 整张（既有图片零影响）。图集换帧只改源矩形，
  **零解码、零新建纹理**。
- 切格算术收在 `SpriteSheet::CellRect(i)` / `SheetWidth()` / `SheetHeight()`
  （行优先，含 `margin` / `spacing`）。
- **构造期几何校验**：`detail::ProbeImageSize()` 量一次图，与 `SheetWidth/Height`
  比对，不符就 `IsValid() == false` + 一条 ERROR（写明"真实 160×40 vs 应为 200×40"）。
- **inkgen 认帧动画**：`Image` 配置里配了 `frames` 或 `sheet` 就生成
  `InkingSprite` 类 + `SpriteData`；否则产物与以前逐字一致。播放参数
  （`fps` / `loop` / `autoplay` / `startFrame` / `startOffset`）写在组件顶层。

**0.6.2（合批）**

- **矩形 + `Fill` 的图片默认并进几何批**：50 个同图集 sprite 的一帧从 50 次
  `SDL_RenderTexture` 变成 **1 次** `SDL_RenderGeometry`。开关
  `detail::SetImageBatchingEnabled(bool)`（默认开，关掉即回到 0.6.1 行为）。

**0.6.3（帧事件）**

- **帧事件**：`SpriteData::SpriteEvent{frame, name}` + `InkingSprite::frameEvent`
  （配置里 `events`）。由**帧号变化**驱动，慢帧跨帧也不吞事件、回绕照发、
  `SeekToFrame` 不发。
- **`finished`**：非循环播完发一次，`Play()` 之后可再发；循环动画永不发。
- 示例新增两个动画：12fps 序列帧指示器（8 张内联 SVG）+ 图集行走角色
  （`examples/demo/assets/walk.png`，4×1 图集，带 `step` 帧事件）。

### 变更

- **矩形图片现在吃形状网格的 1px 羽化**（并批的代价）：边缘从硬边变成抗锯齿，
  与矩形按钮统一；渐变图上表现为"采样点偏了半个纹素"（实测最大通道差 3，
  纯色图逐像素相等）。要回到旧的硬边行为，把 `SetImageBatchingEnabled(false)`。
- `InkingSprite` 的图集模式**不再被拒绝**（0.6.0 时它明确报错"0.6.1 才支持"）。

### 修复

- **`InkingStaticImage::refreshRenderAppearance()` 的纹理键**：`SetImageBytes(data,
  size, name)` 的 `name` 从没落到 `_renderAppearance.source` 上，而纹理键取的是
  后者 ⇒ **同尺寸换图会一直命中第一张纹理**（帧动画表现成"每帧都画第一帧"，
  不报错）。现在 `source` 跟着当前字节走。
- **矩形图片的 `transform` 被丢弃**：0.2.0 起那条 `SDL_RenderTexture` 快路只用
  `pixelX/pixelY/w/h`，旋转 / 缩放过的矩形图片画出来是**没变换的**。并批后走
  形状网格，变换（以及祖先子树变换）恢复生效。

### 文档

- `CXXCSS.md` §3.10 新增「帧动画（0.6.1）」小节：两种素材的 JSON 例子、字段与默认值、
  生成期会拦的错；§9 字段表按 `inkgen --dump-fields` 重生成（新增 `Image / sheet`、
  `Image / events[]` 两张）。
- `docs/zh/api/InkingComponents.md`：`SpriteData` / `InkingSprite` 全量 API 与
  "帧事件五种情况"对照表；`InkingRender.md`：`PaintImage(srcRect)`、`ProbeImageSize`、
  并批开关与羽化代价；`InkingScene.md`：场景动画时钟。
- `README` 版本横幅与本文件同步到 0.6.3。

### 已知限制

- **图集尺寸只能运行期验**：生成期看不到图片有多大，"格数与图宽不符"要等组件构造
  才报 ERROR（`IsValid()` 为假）。构造期会多解一次图（按 `(名字, 字节数)` 缓存）。
- **并批的羽化代价**见「变更」——这是统一边缘换来的，不是缺陷，但确实与 0.6.1
  的像素不完全一致（带 `transform` 的图差异更大：那是修 bug）。
- **帧动画只有离散换帧**：不做关键帧插值 / 属性动画、不做变速 / 倒放——`fps` 是
  配置（运行期没有 setter），要变速就换一份 `SpriteData` 重建组件，或在业务层用
  `SeekToFrame` 自己算帧号。也没有图集打包工具。
- **音频同步自己挂**：框架只提供 `frameEvent` / `finished`，不解析音频时间轴。
- 播放状态属于组件实例，**不提供跨场景的全局播放器**。

---

## 0.5.3 —— 2026-10-26（发布版：FilePicker 按钮栏脱离 MenuBar + 内容高量准）

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
