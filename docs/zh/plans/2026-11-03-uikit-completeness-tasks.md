# 任务书：UIKit 齐全度补齐（自身规划）

> 状态：**全部完成**（a~f 六批 / 10 个任务；本文件按任务逐条跟踪，完成规则见 §1）
> 日期：2026-11-03
> 来源：Shyi 2026-11-03「检查检查这个库，看看作为一个简单的 UI 库是否齐全」；
> 盘点结论见 `docs/zh/plans/2026-11-03-uikit-completeness-audit.md`，核实结论见 §2
> 目标版本：`0.9.0`（六批：a 视频 → b 计量 → c 勾选 → d 浮层 → e 导航 → f 系统集成）
> 涉及层：`video/`、`prefab/`、`widget/`、`window/`、`scene/`、`CMakeLists.txt`、`tools/`

---

## 0. 结论（TL;DR）

审计提了 13 项缺口（1 个「写完没接线」+ 9 个控件 + 3 项系统集成），拆成 **10 个任务 / 6 批**。

核实阶段修正了两处判断：

- **视频不是「没有」**：`src/video/InkingVideo.cpp` 37 KB 完整实现，只是 `INK_HAS_FFMPEG`
  从未被任何 CMake 文件定义。本次实测 `g++ -fsyntax-only -DINK_HAS_FFMPEG` **零错误**，
  所以这是「两行 CMake」的事，不是「写一个视频组件」的事（§2 第 1 条）。
- **九个控件里没有一个需要新能力**：`InkingFixedPopup` 已经带了「吃掉点击 / 点位 / 夹紧 /
  外点关闭 / 焦点」五件事，`OptionBar` 已带「项布局 / 激活 / 层级」。缺的是「拿来就能用」
  那一层（§2 第 3 条）。

| 编号 | 任务 | 批次 | 对应来源 | 落点 | 代价 |
| - | - | - | - | - | - |
| T-01 | 开启 FFmpeg 并让视频真出画 | a | 审计 §1 | `CMakeLists.txt`、`tools/package.sh`、三方许可 | 中 |
| T-02 | 加 `Slider` 滑块 | b | 审计 §3 第 4 | `include/prefab/Slider.h` + `src/prefab/Slider.cpp` | 中 |
| T-03 | 加 `ProgressBar` 进度条 | b | 审计 §3 第 5 | 同上（`ProgressBar`） | 低 |
| T-04 | 加勾选三件套（Checkbox / Radio / Switch） | c | 审计 §3 第 1~3 | `prefab/ToggleLike.h` + 三个子类 | 中 |
| T-05 | 加 `Tooltip` 悬浮提示 | d | 审计 §3 第 8 | `prefab/Tooltip.h` | 中 |
| T-06 | 加 `Dialog` 弹窗 | d | 审计 §3 第 7 | `prefab/Dialog.h` | 中 |
| T-07 | 加 `Toast` 通知 | d | 审计 §3 第 9 | `prefab/ToastHost.h` | 中 |
| T-08 | 加 `TabBar` 标签页 | e | 审计 §3 第 6 | `prefab/TabBar.h` | 中 |
| T-09 | 加鼠标光标形状 | f | 审计 §4 第 1 | `widget/InkingWidget.h` + `window/` | 中 |
| T-10 | 加文件拖放 | f | 审计 §4 第 2 | `scene/InkingScene.h` + `window/` | 中 |

三条设计取向（后续取舍按它们判）：

1. **新控件一律进 `prefab/`，一律不进 CXXCSS。** 判据：这些控件的配置量都很小
   （一个滑块四个值），而让生成器认一套新组件类型要同时改 `Fields.h` / `Cxxcss.cpp` /
   `Emit.cpp` 三处并重跑 13 条自检——那是给「配置量大、要在外部编辑」的东西准备的路。
   `MenuBar` / `OptionBar` / `Select` / `ListView` / `FilePicker` 已经这么办了。
2. **先接线，再加控件。** T-01 排第一不是因为它重要，是因为它牵的是依赖与许可，
   而其余九个任务都只碰新增文件——两者的风险面不重叠，混在一批里出事时分不清是谁的。
3. **对外只有三种样子：一对事件槽、一份 `InkingButtonAppearance`、一个 `GetParts()`。**
   九个控件的 API 形状全在这一层统一，定义与证据见 **§2.5**——读任务段落之前先读它。

---

## 1. 任务维护规则（本文件的写法约定）

### 1.1 每个任务必须写三段

| 段 | 要求 |
| - | - |
| **准备怎么改** | 决策与选型：做什么、走哪条路、为什么；**放弃的候选要写出来** |
| **大致改动逻辑** | 代码级顺序：改哪个文件、加哪个函数、谁调谁、**哪一步不能反** |
| **与改动前对比** | 表格；**至少一行「改动后仍不变」的边界** |

另附 **落点**、**验收**（可断言的断言）、**风险与回退**。

### 1.2 完成即压缩（核心规则）

任务完成后把整段压成一行，并同改进度表（§9.2）：

```
- T-01 <标题>-已完成（<YYYY-MM-DD>，<一句可验证的结论>）
```

删除五个细目。细目在 git 历史里，回看用 `git log --grep=T-01`。

### 1.3 四种状态

完成 / 放弃（写原因 + 替代）/ 拆分（`T-01a` + `T-01b`，各自走完整三段）/
阻塞（保留全部细目，不压缩）。

### 1.4 完成的定义（DoD）

四条全中才算完成：① 编译通过（`-Werror` 门也过）；
② 验收断言进了 `examples/demo/selftest.cpp` 并跑绿（**不是临时探针**）；
③ 文档同步（`CXXCSS.md` / `docs/zh/api/` / `CHANGELOG.md` 该改的改）；
④ §「与改动前对比」每行能对上实测。

---

## 2. 基线事实（核实结论）

| # | 条目 | 判定 | 证据 |
| - | - | - | - |
| 1 | 视频「没有」 | **部分成立** | 有代码无开关。`include/video/InkingVideo.h` 180 行 + `src/video/InkingVideo.cpp` 37 KB；`CMakeLists.txt:174-177` 只有三个能力宏，无 `INK_HAS_FFMPEG`；`dist/inking-0.8.5-win64/bin` 与 `build/bin` 均无 `av*.dll` |
| 1b | 「开了就能编」 | **成立**（本次实测） | `g++ -std=c++20 -fsyntax-only -DINK_ISLOG=1 -DINK_ISDEBUG=1 -Iinclude -DINK_HAS_FFMPEG -IC:/msys64/ucrt64/include src/video/InkingVideo.cpp` → **exit 0，零错误**；`pkg-config --cflags` 只给一个 `-IC:/msys64/ucrt64/include` |
| 1c | 「没有测试素材」 | **成立** | `examples/` 下 `*.mp4/*.webm/*.avi/*.mkv/*.mov` 零命中；但 `C:/msys64/ucrt64/bin/ffmpeg.exe` 在场，可用命令行造样本 |
| 2 | 缺 Checkbox / Radio / Switch | **成立** | `grep -r "CheckBox\|Checkbox\|RadioButton\|Switch"` 在 `include/` + `src/` 零命中（只命中 `SDL_StartTextInput` 的无关行） |
| 3 | 缺 Slider / ProgressBar | **部分成立** | `Slider` 只在 `include/event/InkingEvent.h:45` 作为**注释示例**出现；`ProgressBar` 零命中。能力不缺：`InkingEvent<...>` 的事件槽语义与 `fillShape` 都在 |
| 4 | 缺 TabBar | **成立** | 零命中。但底座已有：`OptionBar`（`include/prefab/OptionBar.h:86-155`：`SetItems` / `GetItemRect` / `Activate` / `SetLayerZ`） |
| 5 | 缺 Tooltip / Dialog / Toast | **成立** | 三个都零命中（`Toast` 只在 `InkingWidget.h:136`、`InkingStaticButton.h:176` 当「将来」的例子） |
| 5b | 「浮窗底座够用」 | **成立** | `InkingFixedPopup`（`include/panel/InkingFixedPopup.h:66-184`）已带 `OpenAt` / `SetTarget` / `Close` / `SetCloseOnOutsideClick` / `SetTakeFocusOnOpen` / `SetClampBounds` / `AddItem` / `OwnsSubtreeHit`；模态「吃掉」语义由 `docs/zh/plans/2026-10-24-0.4.1-fixed-popup-menu.md` 的探针 P3 验证过 |
| 6 | 缺光标形状 | **成立** | `SDL_SetCursor` 全仓库零命中 |
| 7 | 缺文件拖放 | **成立** | `SDL_DropEvent` / `DROPFILE` 零命中 |
| 8 | 缺手柄 / 无障碍 / 多窗口 | **成立，本次不做** | `Gamepad` / `Joystick` / `Accessib` 零命中；`InkingWindow::Instance()` 单例（`InkingWindow.h:36`）。三条都进 §10 |
| 9 | 主题与新控件的一致性有落点 | **成立** | `include/prefab/PrefabTheme.h` 已是完整常量表：13 个颜色 + 3 个语义色 + 5 档圆角（`kRadiusPanel` 16 / `kRadiusCard` 18 / `kRadiusItem` 12 / `kRadiusButton` 10 / `kRadiusCaption` 8） |
| 10 | 自检的注册方式 | **成立** | 新增断言写进 `examples/demo/selftest.cpp`（412 KB，单文件），编成 `inking_demo_selftest`；ctest 名 `selftest`（无头）+ `selftest_windows`（真后端），见 `CMakeLists.txt:452-469`。**退出码会挂，读 stdout 末行「失败项：N」** |

---

## 2.5 统一的三个 API 约定（T-02~T-08 全部适用）

九个控件是九个类，但**对外只长三种样子**。这一节先定死，下面的任务段落不再重复。

### 2.5.1 约定一：事件只有「一对槽」，不给 `SetXxx` 便利

二态控件（Checkbox / Radio / Switch）统一两个槽，名字就是动词：

| 槽 | 什么时候发 |
| - | - |
| `clicked` | 变成选中 / 开 |
| `cancelled` | 变成未选中 / 关 |

三个控件的差异只在**触发时机**，不在名字：

| 控件 | `clicked` 由谁触发 | `cancelled` 由谁触发 |
| - | - | - |
| Checkbox | 用户点它 | 用户再点它 |
| Radio | 用户点它 | **同组另一个被选中**（不是用户点它） |
| Switch | 用户拨开 | 用户拨关 |

其余控件的事件：

| 控件 | 事件 |
| - | - |
| Slider | `changed(float now, float before)` + `released()` |
| ProgressBar | **无**（只读） |
| TabBar | `pageChanged(int now, int before)` |
| Dialog | `accepted` / `cancelled` |
| Tooltip / Toast | 无（Toast 可选 `dismissed`） |

**不给 `SetOnClicked` 那种便利写法的理由**：`InkingButtonLike.h:222-224` 自己写着它的语义是
「替换」——「别指望它和 `Subscribe` 叠加」。新控件不再重复这个坑；只有事件槽时，
「订阅可以有多个」这件事是看得见的。

**为什么不是 `toggled(bool, bool)`**：一个 bool 把「用户点了它」和「它被别人顶掉」混成同一件事，
而业务上这两件事的处理完全不同（保存设置 / 更新互斥显示）。分成两个槽，订阅者不用自己判。

### 2.5.2 约定二：外观复用 `InkingButtonAppearance`，不新造类型

`InkingButtonAppearance` 名字里带 Button 是历史包袱，语义早就是「一层外观：颜色 / 图 / SVG /
纹理矩形 / 过渡」。它现在已经被用在五处：

| 用在哪 | 证据 |
| - | - |
| 按钮三态 | `ButtonData.h:241-243` |
| 图片的单层外观 | `ImageLike.h:71` |
| 帧动画的每一帧 | `SpriteData.h:186` |
| 视频没画面时的兜底 | `InkingVideo.h:175` |
| 所有 prefab 的 itemLook | `MenuBar.cpp:57`、`OptionBar.cpp:74`、`Select.cpp:113` |

`SpriteData.h:182-183` 把这条决定写死了：「用 `InkingButtonAppearance` 而不是新造一个
FrameAppearance：帧就是'一张图 + 它自己的字节'，那正是它在 `type == Image` 时的全部内容」。

**所以新控件的 Data 里不放裸 `std::uint32_t` 色值，一律放 `InkingButtonAppearance`。**
好处是白拿的：`type` 能用 `Image`（贴图）、`Svg`（矢量）、`themeColor` 留 token 名给运行期换肤、
`srcRect` 给图集取块。

### 2.5.3 约定三：部件（Parts）必须可查询

每个控件把「自己由哪几个面组成」暴露成一个结构，**绘制与命中读同一份**：

```cpp
struct SliderParts {           // 形状随控件而变
    InkingShapeBounds track;   // 轨道
    InkingShapeBounds fill;    // 已填充段
    InkingShapeBounds thumb;   // 滑块圆盘
};
const SliderParts& GetParts() const noexcept;
```

为什么要这一层：`AGENTS.md` §6 第 94 条「同一件事写两遍就会分家」。自定义绘制若拿不到
部件矩形，就只能自己算一遍——于是「画的和点的是两个位置」，两边单独看都对，合起来才错。

### 2.5.4 外观替换的三档（能换什么 / 换不了什么）

| 档 | 能换什么 | 怎么换 | 命中跟不跟 |
| - | - | - | - |
| **1 数据** | 颜色 / 图 / SVG / 圆角 / 尺寸 / 描边 / 阴影 | 改 `XxxData` 里那一份 `InkingButtonAppearance` | 跟（形状参与命中） |
| **2 部件** | 每个面单独换 | 换哪个面的 appearance 就是换哪个面；面与面的矩形来自 `GetParts()` | 跟 |
| **3 绘制器** | 整块自己画 | `SetPainter`，照 `InkingCanvasPainter` / `InkingCanvasPaintFn` | **不跟** |

第三档是既有机制（`include/canvas/InkingCanvasPainter.h`），不用新造。但边界必须写明：
`InkingStaticCanvas.h:18-20` 写着「内容不参与命中——这是 A12 定下的边界」。
**自定义绘制能换「画什么」，换不了「点哪里」。** 要让自定义画的东西可点，得走第二档——
部件矩形进命中，画的东西不进。

### 2.5.5 各部件的表（这就是「哪部分能替换」的具体答案）

| 控件 | 可独立换外观的部件 |
| - | - |
| Checkbox | 框底、勾 |
| Radio | 圆底、内点 |
| Switch | 轨道、滑块 |
| Slider | 轨道、已填充段、滑块圆盘 |
| ProgressBar | 轨道、填充块 |
| TabBar | 标签条（复用 `OptionBar`）、内容区（复用 Panel） |
| Dialog | 遮罩、面板（复用 Panel）、按钮（复用 Button） |
| Tooltip / Toast | 面板 |

---

## 3. 第一批 a：`0.9.0`（视频接线）

> 本批已完成，按 §1.2 压成一行（细目在 `git log --grep=T-01`）：

- T-01 开启 FFmpeg 并让视频真出画-已完成（2026-11-03，`pkg_check_modules` 去掉
  `REQUIRED` 加 `QUIET`，命中才 `PRIVATE` 定义 `INK_HAS_FFMPEG` 并链接、未命中打
  WARNING；`video_decode_probe` 绿：解码 28 帧 / 上屏 28 帧 / 丢 0 / 320×240 /
  画面内部 9 种颜色且无兜底色）

---

## 4. 第二批 b：`0.9.0`（计量两件套）

> 本批已完成，按 §1.2 压成一行（细目在 `git log --grep=T-02` / `T-03`）：

- T-02 加 `Slider` 滑块-已完成（2026-11-03，`prefab/Slider.h` + `.cpp`；
  `slider_drag_probe` 绿：点 25% → 25.0、拖 75% → 75.0、`changed` 恰好 2 次、
  `released` 1 次、按 → 值 +1 step）

- T-03 加 `ProgressBar` 进度条-已完成（2026-11-03，`prefab/ProgressBar.h` + `.cpp`；
  `progress_bar_probe` 绿：`fill = track × progress`、同值返回 false、像素上
  "进度 0.5 时左右两半不同色、进度 0 时同色"）

---

## 5. 第三批 c：`0.9.0`（勾选三件套）

> 本批已完成，按 §1.2 压成一行（细目在 `git log --grep=T-04`）：

- T-04 加勾选三件套（Checkbox / Radio / Switch）-已完成（2026-11-03，
  `prefab/ToggleLike.h` + `.cpp`；`toggle_like_probe` 绿：三件的翻转与幂等、组内
  "只有一个为真 / 点已选中的不翻转 / 换人发 1+1 条"、`mark ⊆ base`、过渡 0.05s 时
  knob=0.67、**勾的方向靠读像素钉住**（左下 12 / 右上 7 / 左上角 0））

---

## 6. 第四批 d：`0.9.0`（浮层三件套）

> 本批已完成，按 §1.2 压成一行（细目在 `git log --grep=T-05` / `T-06` / `T-07`）：

- T-05 加 `Tooltip` 悬浮提示-已完成（2026-11-03，`prefab/Tooltip.h` + `.cpp`；
  `tooltip_delay_probe` 绿：2 帧不弹 / 7 帧弹出 / `Hide()` 复位 / 提示正中点击落到
  底层按钮（穿透）/ 落位在锚点下方且水平居中）

- T-06 加 `Dialog` 弹窗-已完成（2026-11-03，`prefab/Dialog.h` + `.cpp`；
  `dialog_modal_probe` 绿：打开后按钮位置的 `QueryHit` 落到遮罩、点遮罩默认不关、
  点面板后焦点仍归它、Esc 只发 `cancelled`、Enter 发 `accepted` + `action(2)`）

- T-07 加 `Toast` 通知-已完成（2026-11-03，`prefab/ToastHost.h` + `.cpp`；
  `toast_lifetime_probe` 绿：三条堆叠不重叠、0.5s 后第一条自消失且 id 对得上、
  同批到期一次清空、`Clear()` 不发事件、通知盖住按钮仍点到按钮）

---

## 7. 第五批 e：`0.9.0`（导航）

> 本批已完成，按 §1.2 压成一行（细目在 `git log --grep=T-08`）：

- T-08 加 `TabBar` 标签页-已完成（2026-11-03，`prefab/TabBar.h` + `.cpp`；
  `tabbar_switch_probe` 绿：三页 / 切页后同一位置 `QueryHit` 落到新页 / 同页再切零事件 /
  越界拒绝 / 点标签走同一个 `Select` 出口）

---

## 8. 第六批 f：`0.9.0`（系统集成）

> 本批已完成，按 §1.2 压成一行（细目在 `git log --grep=T-09` / `T-10`）：

- T-09 加鼠标光标形状-已完成（2026-11-03，`input/CursorShape.h` +
  `InkingWidget::GetCursorShape()` + `InkingWindow` 的 `applyCursor`；
  `cursor_shape_probe` 绿：悬停按钮换 1 次、悬停不动跑 5 帧不再换、移开换回箭头）

- T-10 加文件拖放-已完成（2026-11-03，`InkingScene::filesDropped` +
  窗口事件循环收集 / 帧末派发；`drop_file_probe` 绿：一次拖 3 个文件 = 一条事件带
  3 个路径、路径原样保留、无场景时丢弃 +1 且不派发）

---

## 9. 批次门槛与进度

### 9.1 门槛

| 批次 | 发布前必须满足 |
| - | - |
| a | ① `dist/bin` 里 `avcodec` 等 DLL 齐，`ldd libinking.dll` 逐个对上；② 最小 PATH 解压跑通；③ `video_decode_probe` 绿；④ 没装 FFmpeg 的构建打印 WARNING 且 `libinking.dll` 导入表无 `av*` |
| b | `slider_drag_probe` + `progress_bar_probe` 绿；两者共用同一个「按比例算填充矩形」函数（grep 只有一份定义） |
| c | `toggle_like_probe` 绿；三件套**只发 `clicked` / `cancelled` 两个槽**、不引入 `toggled`（§2.5.1 已论证：一个 bool 把"用户点了它"和"它被别人顶掉"混成同一件事）；`mark ⊆ base`；一次换人 = 1 + N 条事件 |
| d | `tooltip_delay_probe` + `dialog_modal_probe` + `toast_lifetime_probe` 绿；Tooltip / Toast 在 `QueryHit` 里**不可见**（盖在按钮上仍点到按钮）；开着的 Dialog 让底下按钮的 `QueryHit` 落到遮罩、且**点面板后焦点仍归它** |
| e | `tabbar_switch_probe` 绿；切页后旧页 `QueryHit` 不可达 |
| f | `cursor_shape_probe` + `drop_file_probe` 绿；无头自检下两者都是安全空操作；两点都挂在 **`stepFrame`**（不是 `runLoop`），否则 `PumpOnce()` 那条路验不到 |

### 9.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 开启 FFmpeg 并让视频真出画 | a | **已完成**（2026-11-03） |
| T-02 | 加 `Slider` 滑块 | b | **已完成**（2026-11-03） |
| T-03 | 加 `ProgressBar` 进度条 | b | **已完成**（2026-11-03） |
| T-04 | 加勾选三件套（Checkbox / Radio / Switch） | c | **已完成**（2026-11-03） |
| T-05 | 加 `Tooltip` 悬浮提示 | d | **已完成**（2026-11-03） |
| T-06 | 加 `Dialog` 弹窗 | d | **已完成**（2026-11-03） |
| T-07 | 加 `Toast` 通知 | d | **已完成**（2026-11-03） |
| T-08 | 加 `TabBar` 标签页 | e | **已完成**（2026-11-03） |
| T-09 | 加鼠标光标形状 | f | **已完成**（2026-11-03） |
| T-10 | 加文件拖放 | f | **已完成**（2026-11-03） |

完成一个任务后，本节状态改 `已完成`，对应段落按 §1.2 压缩成一行——**两处必须同时改**。

### 9.3 批次 a~f 执行记录（与任务书字面不同的地方）

写在这里是因为 §1.2 把细目压掉了，而这几条是**实测后改的口径**，不是遗漏：

| # | 任务书写的 | 实际做的 | 为什么 |
| - | - | - | - |
| 1 | T-02「断言 `GetParts().thumb` 落在 `GetParts().track` 之内」 | 断言**水平范围**在内 + **纵向同心** | 盘 18 > 轨 6，垂直必然溢出（任务书自己的「风险与回退」也这么写）。硬裁进去是半个圆 |
| 2 | T-02「命中只认 track，整条可点」 | 热区 = **整块组件框**，x→值 用轨道跨度换算 | 6px 高的轨道条在 1080p 下几乎点不中；"盘不单独进命中"由"没有第二个热区"满足 |
| 3 | T-01 第 4 步「把 av* 加进 `stage_runtime_deps.sh`」 | **不改**：脚本按 `ldd` 走名字回工具链取 | 实测自动收 82 个 DLL（含 `dav1d` / `x264`）；手写清单反而会漏 |
| 4 | T-01 验收「`GetFrameCount()`」 | `GetDecodedFrameCount()` / `GetPresentedFrameCount()` / `GetDroppedFrameCount()` | 库里没有 `GetFrameCount()` 这个名字 |
| 5 | T-01 回退「`-DINK_WITH_FFMPEG=OFF`」 | 方案 A 下没有这个 option；回退 = `PKG_CONFIG_LIBDIR=` 指向空目录重配 | 实测：配置打印 WARNING、`libinking.dll` 导入表 **0 个 `av*`**、`bin/` 少 63 个 DLL |
| 6 | §2.5.4 第三档 `SetPainter`（九个控件都该有） | **T-02/T-03/T-04 都不提供** | `InkingCanvasContext` 构造是 `private`、`friend` 只有 `InkingStaticCanvas`（必须带 `CanvasRecorder`），叶组件挂不了绘制器。要整块自绘就叠一块画布 |
| 7 | T-04 第 4 条「`onPointerClick`：`SetChecked(!IsChecked())`；Radio 覆写成"已选中就直接返回"」 | Radio 挂了组时走 **`RadioGroup::Select(this)`**；没挂组才直接 `SetChecked(true)` | 只写"已选中就返回"的话，点另一个 Radio 会**两个同时选中**——互斥必须由组落地，而点击是最自然的入口 |
| 8 | T-04 第 6 条「`Switch` 的 0.15s 过渡」 | 过渡挂在 `onAnimationTick`（每渲染帧），**只在活跃场景的 `TickFrame` 里推进** | 自检第一版忘了把探针场景设为活跃，滑块一步不动——现象是"过渡没生效"，像组件的问题（已写进探针注释） |
| 9 | T-06「`class Dialog : public InkingFixedPopup`」 | **`class Dialog : public InkingStaticPanel`**，自己就是那层全屏遮罩，中间那块框是子面板 | 两者的几何模型对不上：`FixedPopup` 是**点锚定**的（贴指针展开），对话框是**居中**的；而模态要挡的是**整块画布**，必须有一个铺满的实体去命中。铺满的 `FixedPopup` 等于"对话框是全屏的"，与"居中面板"矛盾；拆成两个同级节点又得各抬一次 z |
| 10 | T-06 第 4 条「`WantsKeyboardFocus()` 为真 + 把焦点收到自己」 | 除了 `Open()` 时夺焦点，还在 `onAnimationTick` 里**每帧补一次**（只补不抢） | 场景的规则是"点到的组件不想要焦点就别留着" ⇒ 点一下面板就把焦点清掉，Esc / Enter 从此没落点（自检第一版正是红在这里） |
| 11 | T-07「`dismissed` 带不带索引由实现定」 | 带的是 `Show()` 返回的**自增 id** | 下标会随着前面的条消失整体前移，订阅者拿不到稳定标识 |
| 12 | T-08「复用 `OptionBar` 做标签条，选中态走它的激活通道」 | 标签条**直接摆 `InkingStaticButton`**，选中指示由 TabBar 在装饰遍画下划线 | `OptionBar` 的项宽冻在构造期（`SetItems` 不回读 style），且 `itemLook.width <= 0` 时每项撑成**容器整宽**（竖向菜单语义）——横向标签条要"宽 / 项数"、项数又随 `AddPage` 变，两条路都比直接摆按钮差。它也没有"选中态"这个第四态，只有 `Activate()` 的动作通道 |
| 13 | T-08 第 2 条「第一页设为可见」 | 首页自动选中**不发 `pageChanged`** | 那是初始状态不是"换了页"；发了的话订阅者每个回调都得先写 `if (before < 0) return;` |
| 14 | T-01 验收「③ 丢帧计数为 0」 | 改成**比例判据**（丢帧 ≤ 两成） | 主时钟是真实时间，机器一忙一次 sleep 打盹就丢一帧：同一份代码实测跑出过 28/0 与 28/1。历史 bug 的形态是"出一帧丢一大半"，比例判据照样抓得住 |
| 15 | T-09 第 2 条「在 **`runLoop` 的帧末**取悬停节点、比形状」 | 挂在 **`stepFrame`**（键盘派发之后） | `runLoop` 只是 `while (stepFrame(...))`，无头自检走的 `PumpOnce()` 完全不经过它——挂错地方会读到"`SDL_SetCursor` 0 次"，看起来像没实现（`stepFrame` 顶上那段注释就是为这个坑写的） |
| 16 | T-09 验收「断言每帧 `SDL_SetCursor` 调用次数」 | 断言 `InkingWindow::GetCursorUpdateCount()`：记的是**判定**次数 | 无头驱动下 `SDL_CreateSystemCursor` 可能返回空，`SDL_SetCursor` 是空操作；按真实调用计数会永远是 0 |
| 17 | T-10 第 3 条「路径转 `std::filesystem::path` 走宽字符」 | 事件里带 **UTF-8 原样串**，转换交给调用方；文档写明"别拿窄串直接构造 path" | 事件类型是 `const std::vector<std::string>&`；在窗口层转成 path 再转回串等于绕一圈，而宽字符那条纪律属于**用**路径的人 |

顺带修掉的三处既有缺陷（都在本批的验证路径上撞见，不修则"绿"是假的）：

1. **视频画面被兜底色乘成黑**：`fillShapeTextured` 的顶点色参与 RGB 乘法，而
   `InkingVideoData::normal.color` 是"没有画面时的兜底色"、默认不透明黑 ⇒
   默认配置下"帧在走、一个像素都不出"。自检第一版就是靠"抽到 1 种颜色"抓到的。
2. **`InkingWidget::HitTest` 基类恒 `false`**：`Slider` 不重写就完全点不到，
   且不报错（自检表现："值一直 0、事件一次都没有"）。
3. **示例资源拷贝并发冲突**：两个 exe 各挂一条 POST_BUILD，并行链接时同时写
   `bin/assets/`，Windows 上偶发 `Error copying directory ... Permission denied`，
   `cmake --build` 随机失败。改成一个 `inking_demo_assets` 目标拷一次。

---

## 10. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| 让新控件进 CXXCSS | 配置量小（一个滑块四个值），而扩张成本是 `Fields.h` / `Cxxcss.cpp` / `Emit.cpp` 三处 + 13 条自检。生成器是给「配置量大、要外部编辑」的东西准备的路 |
| 不确定进度的 `ProgressBar`（跑马灯） | 静态档收不到 `Tick`（§6 第 37 条），要做就得给一个纯展示控件开每帧路径。将来需要时另做 `DynamicProgressBar` |
| 三态 `Checkbox`（半勾） | 两态覆盖了现有需求；半勾要先定义「点它变什么」，那是产品决策不是库决策 |
| `TreeView` / `TableView` | 数据量大时才需要，且要先有「行虚拟化」这个能力。审计 §3 已列为可选 |
| 手柄输入 | 视觉小说在 PC 上以键鼠为主；`Gamepad` 整套（轴 / 按钮映射 / 断连重连）是独立工程 |
| 多窗口 | `InkingWindow::Instance()` 是单例，而 `currentRendererSlot` / `currentAncestorAffineSlot` / `window_config` 那一族全是「进程唯一」槽位。改多窗口等于改架构，不在本次范围 |
| 无障碍（屏幕阅读器 / 焦点环语义） | 游戏 UI 普遍不做；要做需要每个控件暴露语义树，与现有「命中树」是两套结构 |
| 恢复 `CXXCSS/Scene/` 生成 | `CXXCSS.md` §7.2 已评估并回退：收益手写就能拿到，代价固定 |

---

> 本文件按 `task-driven-project-build` 的约定组织。结构校验：
> `python C:\Users\24807\.dsh\skills\task-driven-project-build\scripts\check_task_book.py <本文件>`
