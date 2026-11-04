# InkingFrontendDeveloper API 文档

> ## 当前为 **0.x 发布版**（Release Build）
>
> 版本号 `0.5.3`（`InkingVersion` / `InkingVersionStage`）。
>
> - **接口仍在变动**：本文档描述的类、函数、字段在 `1.0.0` 之前不承诺 ABI / API 稳定，
>   升级请按 `docs/zh/CHANGELOG.md` 逐条对照。
> - **行为已验证的范围**：示例工程 `inking_demo_selftest` 覆盖的项（见 §7）；
>   未覆盖的能力一律按"可用但未验收"看待。
> - **适用范围**：`0.5.3` 是 1.0 前的发布版，文档、示例与预编译包齐备；
>   生产集成前请先跑一遍自检，并注意尚未覆盖跨平台验证。

本文件是**索引**：先读 §0 跑起来，再按 §3 的模块地图查专页。
每个专页的写法是固定的四段：**定位说明 → 用到的数据结构 → 属性 → APIS**，
最后加一节"常见坑"。查一个符号时直接看 §8 的头文件索引。

---

## 0. 五分钟上手

```cpp
#include <window/InkingWindow.h>
#include <scene/InkingScene.h>
#include <button/InkingStaticButton.h>

// 1) 场景：必须派生（InkingScene 是基类，构造即登记进场景库）
class DemoScene : public InkingScene {
public:
    explicit DemoScene(const std::string& name) : InkingScene(name) {
        // 手写数据这条路由 InkingButtonData 直接给（配置驱动那条路是传名字）
        InkingButtonData data;
        data.name   = "demo_button";
        data.width  = 160;
        data.height = 48;
        data.normal.color = 0xFF569CD6u;
        data.Normalize();                       // 补齐"省略即继承"
        _button = std::make_unique<InkingStaticButton>(this, data);
        _button->SetOnClicked([] { /* ... */ });
    }
private:
    std::unique_ptr<InkingStaticButton> _button;
};

int main() {
    DemoScene scene{"demo"};                       // 名字同期唯一
    InkingWindow::Instance().Show("demo"); // 进主循环，退出后返回
    return 0;
}
```

四条"第一次必踩"的规矩：

1. **`InkingScene` / `InkingWindow` 都不可直接实例化**：场景派生、窗口是私有构造单例
   （`InkingWindow::Instance()`）。
2. **构造时传 `parent`**：除根以外所有组件都要 parent，否则沿父链找不到场景，
   既不渲染也点不到（而且**不报错**）。
3. **静态档没有几何写入口**：`Resize` / `ChangeOffset` 只挂在 `InkingDynamicWidget` 上。
   要运行期改尺寸就选动态档，代价是不进命中表。
4. **走配置驱动时名字三处一致**：`CXXCSS/<组件>/<名字>.json`、代码里传的字符串、
   生成出来的类名。名字查不到时 `FindData` 返回 `nullptr`（组件退化成一块空壳，
   靠 `UnknownNameCount()` 才看得见）——所以手写数据这条路更不容易出错。

构建与运行见仓库根 `README.md`。

---

## 1. 关键词

**| 可配置内容**
是指设计常量（编译期定型，无 setter）。
你不需要关心它在代码的哪里，只需要在文件夹 `CXXCSS` 按照需求写好配置文件即可；
代码生成器（`inkgen`）会在编译期把这些常量展开成 C++。
**格式以 [../../CXXCSS/CXXCSS.md](../../CXXCSS/CXXCSS.md) 为准**——那份写了目录与命名、
公共骨架、形状层词汇表、Button 的完整字段，以及"哪些是纸面约定、哪些已落地"。
也可以直接读 `CXXCSS/*.json` 实例，或让 `inkgen --validate` 报错告诉你怎么写。

**| [final] 写入口**
指的是不可重写的写入口：结构性变化（`ChangeZIndex` / `SetParent`）两版共用，
几何变化（`Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset`）只有动态版有。
一律不加 `virtual`，子类不可重写。
标脏由框架在写入口内部统一完成，你不需要自己维护命中相关的脏标记。

**| [√] 钩子**
指的是可以重写的钩子，例如 `onSizeChanged` / `onSelfAnchorChanged` / `onTraceAnchorChanged` /
`onOffsetChanged` / `onZIndexChanged` / `onParentChanged` / `onVisibleChanged` / `onDirty` /
`onAnimationTick` / `onTick`（只有动态组件收得到 tick）。
钩子只用来挂附加逻辑，不负责标脏，也不拦截写入口。

**| 标脏 / isDirty**
指的是「命中结果可能已经过期」这个标记。会标脏的只有四件事：几何（position / size）、
可见性（hide / show）、层级（zindex）、鼠标移动。颜色、文字、透明度、展示倍率、不影响命中的
装饰动效都不标脏。
重绘不等于标脏——判断在写入口里完成，不在重绘时做。

后续补充的一个概念（见 `api/InkingWidget.md`）：**重绘脏**（`MarkRepaintDirty`）与**命中脏**
是两条通道。颜色 / 文字 / 变换每帧变，只标重绘脏；只有骨架变化才标命中脏。

**| X/x**
指的是屏幕横向的位置。通常以窗口的左上角为 0，向右线性增加。

**| Y/y**
指的是屏幕纵向的位置。通常以窗口的左上角为 0，向下线性增加。

**| 自身锚点**
指的是组件内部的锚点位置，以百分比作为参考。以左上角为 (0,0)、右下角为 (1,1) 为标准参考系。
任何组件在使用锚点时都会被视作"矩形"。自身锚点决定了自身以哪里作为与上级对应的方式。

**| 上级锚点**
指的是组件上级的锚点位置，以百分比作为参考。以左上角为 (0,0)、右下角为 (1,1) 为标准参考系。
任何组件在使用锚点时都会被视作"矩形"。上级锚点决定了自身的锚点与上级的哪里作为对应。

**| 设计坐标 / 设备像素**
本文档里凡是写"设计坐标"的，都是 `InkingLegacyDesignWidth × InkingLegacyDesignHeight`（默认 1280×720）这套
与分辨率无关的坐标；凡是写"设备像素"的，都是真实窗口像素。换算只有一个系数：
`GetMagnification()`（letterbox 下 XY 同倍率）。**公开 API 的入参一律设计坐标**，
只有 `onRender(pixelX, pixelY)` 那一层收设备像素。

---

## 2. 0.x 发布版的边界（"能用"与"验收过"）

| 项 | 状态 | 依据 |
| --- | --- | --- |
| 库编译 / 链接 / 导出符号 | **已验收** | `cmake --build` 通过，产物 `libinking.dll` + `libinking.dll.a` |
| 无头自检（`inking_demo_selftest`） | **已验收**（失败项 0） | 见 §7 |
| 真窗口渲染、文字栅格化、视频解码 | **可用但未逐项验收** | 沙箱内 `SDL_IOFromFile` 失败，字体打不开（见 §7 注） |
| 跨平台（Linux / macOS） | **未验证** | 只在 Windows + MSYS2 UCRT64 上构建过 |
| ABI 稳定性 | **不承诺** | 0.x 发布版，见顶部横幅 |

> **文字与框高**（0.8.3 起）：标签的 `height` 就是文字的上限——累计行高超过
> `height − 上下 padding` 时，装不下的行整行不画（`InkingTextOverflow::Clip` 的默认语义
> 之前一直没实现，文字会画到框外），并置 `InkingTextLayoutResult::truncated`。**至少留
> 一行**、**不缩字号**。想让长文案溢出的，把 `height` 设 0 即恢复旧行为。
> 细节见 [api/InkingRender.md](api/InkingRender.md#六文本排版纯函数)。

---

## 3. 文档地图

### 3.1 数据与算术（纯数据 / 纯函数，header-only，无 SDL）

| 页面 | 覆盖的头文件 |
| --- | --- |
| [api/InkingAnchor.md](api/InkingAnchor.md#锚点系统) | `anchor/InkingAnchor.h`、`dataStruct/InkingAnchorData.h` |
| [api/InkingShapeSpec.md](api/InkingShapeSpec.md#形状层) | `dataStruct/InkingShapeSpec.h` |
| [api/InkingTransform.md](api/InkingTransform.md#变换通道) | `dataStruct/InkingTransform.h`、`dataStruct/InkingAffine.h` |
| [api/InkingLayout.md](api/InkingLayout.md#布局数据层) | `dataStruct/InkingLayout.h` |
| [api/InkingLayoutSolver.md](api/InkingLayoutSolver.md) | `layout/InkingLayoutSolver.h` |
| [§9 素材该多大](#9-素材该多大缩放规则) | `dataStruct/InkingImageScale.h`（0.7.0：素材尺寸 / σ / 干净档位） |

### 3.2 节点与树

| 页面 | 覆盖的头文件 |
| --- | --- |
| [api/InkingWidget.md](api/InkingWidget.md) | `widget/InkingWidget.h`、`widget/InkingStaticWidget.h`、`widget/InkingDynamicWidget.h`、`detail/InkingGeometry.h` |

### 3.3 输入

| 页面 | 覆盖的头文件 |
| --- | --- |
| [api/InkingPointerHandler.md](api/InkingPointerHandler.md#指针输入契约) | `input/InkingPointerHandler.h` |
| [api/InkingKeyboardTarget.md](api/InkingKeyboardTarget.md#键盘输入契约) | `input/InkingKeyboardTarget.h` |
| [api/InkingInput.md](api/InkingInput.md) | `input/InkingKeyboardInput.h`、`input/InkingMouseInput.h`、`input/InkingShortcut.h`、`input/InkingShortcutRegistry.h`、`input/InkingTextEditState.h` |

### 3.4 场景 / 窗口 / 渲染

| 页面 | 覆盖的头文件 |
| --- | --- |
| [api/InkingScene.md](api/InkingScene.md) | `scene/InkingScene.h`、`scene/InkingSceneLibrary.h`、`scene/SceneRegisterToken.h`、`scene/InkingHitTable.h` |
| [api/InkingWindow.md](api/InkingWindow.md) | `window/InkingWindow.h`、`config/window_config.h` |
| [api/InkingRender.md](api/InkingRender.md) | `render/*.h`、`dataStruct/InkingTextRequest.h`、`dataStruct/InkingTextLayout.h` |

### 3.5 组件与预制件

| 页面 | 覆盖的头文件 |
| --- | --- |
| [api/InkingComponents.md](api/InkingComponents.md) | `button/*`、`panel/*`、`image/*`、`text/*`、`canvas/*`、`video/*`、`audio/*` |
| [api/InkingFixedPopup.md](api/InkingFixedPopup.md) | `panel/InkingFixedPopup.h`（定点浮窗，0.4.1） |
| [api/InkingPrefab.md](api/InkingPrefab.md) | `prefab/*` |
| [api/InkingCxxcss.md](api/InkingCxxcss.md) | **没有头文件**：`CXXCSS/*.json` 编译期生成的那些组件类（`cxxcss::OptionItem` 之类）、`RegisterAllButtons()`、语言表安装、`inkgen` 命令行 |

### 3.6 工具件（`util/`）

| 页面 | 覆盖的头文件 |
| --- | --- |
| [api/InkingJson.md](api/InkingJson.md) | `util/InkingJson.h`（解析 / 取值 / 转义；只依赖标准库，0.6.5） |

> **观测口（"它到底跑了几次 / 缓存生效了没"）**：`GetDrawItemCount()` /
> `GetHitTableEntryCount()` / `GetRepaintingNodeCount()` / `GetPaintCount()` /
> `GetReplayCount()` / `BatchStats*` 这一族的**口径与读法**在
> [UsageReport.md §3「观测口与读数口径」](UsageReport.md)。三条最容易踩的：累计值必须
> 取前后差、绘制列表条目数**含隐藏节点**、命中表条目数**不含容器子孙**。
>
> **布局重排的契约**（哪些 setter 会自动重排、哪些不会）在
> [api/InkingWidget.md「Relayout / RelayoutParent」](api/InkingWidget.md)——一句话版：
> **框架不做隐式重排**，内容变了由组件自己喊；`InkingStaticLabel::SetText` 是唯一
> 自动喊的那一处（它一变就影响兄弟让位）。

### 3.6 运行支撑

| 页面 | 覆盖的头文件 |
| --- | --- |
| [api/InkingRuntime.md](api/InkingRuntime.md) | `inking.h`、`thread/*`、`timeline/InkingTimeline.h`、`event/InkingEvent.h`、`asset/InkingAsset.h`、`detail/InkLog.h`、`detail/InkingMessageQueue.h`、`detail/InkingHit.h`、`dataStruct/TaskStruct.h`、`dataStruct/MessageStruct.h` |

### 3.7 工程规范与设计文档

| 页面 | 内容 |
| --- | --- |
| [SKILL.md](SKILL.md) | 编码与解耦规范（分层、头文件、类写法、命名、并发、自检） |
| [../AGENTS.md](../AGENTS.md) | 阅读顺序、头文件与类写法 10 条、验证 6 条、**经验条目 1–112** |
| [InputDesign.md](InputDesign.md) | **输入与命中的设计约定**：三种脏的生命周期、命中三态、命中表、变换通道、开放问题 |
| [TaskGuide.md](TaskGuide.md) | 任务编号体系（A / B / C / D / T 系列 + r2）与"编号 → 落点文件"对照 |
| [ComponentGuide.md](ComponentGuide.md) | 组件该怎么写（以 `InkingStaticPanel` 为样板）+ 新增组件 checklist |
| [UsageReport.md](UsageReport.md) | 成本与量级、实测成立 / 不成立清单、预制件与文本框的现状 |
| [../../CXXCSS/CXXCSS.md](../../CXXCSS/CXXCSS.md) | 配置格式（目录与命名、公共骨架、形状层词汇表、Button 字段全表） |
| [plans/](plans/) | 已落地方案的设计记录（窗口 / 菜单栏 / 缩放 / 文字高清 / 帧动画 / **块叠加组**） |

> 标注 `†` 的章节是**重建内容**：原始设计稿 / 实测数据没有随搬家带入，结论按代码行为与
> 引用上下文复原。引用具体数值时以自己实测为准。

---

## 4. 分层与依赖方向（一句话版）

```
config / dataStruct            纯数据、纯算术，连 SDL 都不认识
      ↑
detail（geometry / hit / log / queue）   跨层工具，无 SDL（Batch / Text 例外）
      ↑
anchor → widget（树、标脏、命中、布局、输入派发）
      ↑
组件（button / panel / image / text / canvas / video / audio）
      ↑
scene（场景、场景库、命中表）  →  window（唯一碰 SDL 窗口与主循环的地方）
      ↑
prefab（用上面的东西组装，不写新底层）
```

**单向**：上层可以依赖下层，下层绝不能反过来认识上层。
判断一个文件该放哪个目录，按"它归属哪个子系统"定，**不按"含不含 SDL"**定。

---

## 5. 名字上就能分开的两种契约

| 名字 | 标记 | 谁调 | 能不能重写 | 负不负责标脏 |
| --- | --- | --- | --- | --- |
| `SetVisible` / `Resize` / `SetParent` | `[final]` | 业务 | **不能** | **负责**（内部统一标脏） |
| `onSizeChanged` / `onTick` / `onPointerHover` | `[√]` | 框架 | **可以** | **不负责** |

写入口一律 `PascalCase`，钩子一律 `on` 开头。派生类重写钩子**必须写 `override`**：
漏了就变成"隐藏"，而框架是拿基类指针调的——**静默不生效**（不报错，只是没反应）。

---

## 6. 列出公开面的三条口径

1. **头文件只放声明**，实现进 `.cpp`；只有一行的方法可以留在头里。
2. **公开头不拖 SDL 进来**：`struct SDL_Renderer;` / `union SDL_Event;` 前向声明，
   SDL 类型的声明必须写在 `namespace inking` **之外**。
3. **契约头一律"声明即定义"**（头内 inline 空实现）：只声明的话，任何派生类都在
   **链接期**炸 `undefined reference`。

---

## 7. 自检（0.x 发布版唯一的行为依据）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build                 # 7 条：配置 / 字段表 / 生成链路 / 严格告警门 / 静默清单 /
                                       #       行为自检（software）/ 行为自检（真后端，仅 Windows）
cd build && PATH=.:$PATH ./bin/inking_demo_selftest.exe   # 单跑行为自检
```

**读末行"失败项：N"，也读退出码——它们是同一个数**（`main` 直接 `return g_fail`），
所以 `exit=0` ⇔ 全绿，可以直接当判据，不需要解析 stdout。

> 上一版这里写的是"不要看退出码"。那句话已经过期：它源自 `0.1.0-test`
> 时代"打印完失败项就不返回"的死锁（`InkingTaskQueue` 的函数内静态量在 DLL 卸载时
> join 管家线程），那条在 `0.1.1` 就修掉了（见 `README.md` §已知限制 那条
> "已修掉的历史限制"）。实测连跑多次都是 1 秒内 `exit=0`。

行为自检**已挂进 `ctest`**（0.8.0 T-01）：不再有"`ctest` 全绿但一条产品行为都
没验"的空档。`-DINK_BUILD_EXAMPLES=OFF` 的构建里没有自检 exe，`ctest` 会少这一条
——**这是有意的**，不是漏配。

### 后端：software 与真后端各跑一遍

自检默认用 SDL 的 `dummy` 视频驱动（**软件渲染器**），因为那是"在任何机器上都能
跑"的唯一保证。设环境变量可以换后端：

```bash
INKING_SELFTEST_DRIVER=windows ./bin/inking_demo_selftest.exe   # 本机走 direct3d11
```

`ctest` 里两条各跑一遍：`selftest`（默认 dummy）与 `selftest_windows`
（`ENVIRONMENT "INKING_SELFTEST_DRIVER=windows"`，仅 Windows 注册）。
真后端建不出窗口时自检打印 `[skip] 真后端不可用` 并 `return 77`，`ctest`
把它显示成 **Skipped** 而不是 Failed——"这台机器没 GPU"与"代码坏了"不是同一件事。

为什么值得多跑一遍：0.6.4 的"采样模式在软件后端几何批上不生效"正是
**GPU 与 software 行为不同**的那类缺陷，只验一种后端永远抓不到。

> 沙箱 / CI 注：某些环境里 `SDL_IOFromFile` 会失败，于是 `TTF_OpenFont` 打不开字体、
> 文字无法真栅格化（绘制退回占位方块，自检打印"没有可读字体"并跳过那一段）。
> 自检支持环境变量 `INK_TEST_FONT` 指定可读字体；"没字体"不等于"代码有缺陷"。
>
> **实测过的一个陷阱（2026-10-30）**：同一个 exe、同一份 `libinking.dll`，**进程环境**不同
> 结果不同。不带 msys64 的 `ucrt64/bin` 就直接跑时，`SDL_IOFromFile` 对所有路径都报
> "系统找不到指定的文件"，于是自检打印"没有可读字体"；把它放进 `PATH` 再跑就全好了
> （`ctest` 与 `tools/` 的构建脚本都是这么跑的）。**症状**是"这台机器没有字体"，
> **真实原因**在加载期。最可能的成因：本机 `PATH` 上还有第二份 `SDL3.dll`
> （`C:/SDL3/lib/x64/`，与 msys64 那份版本差一年多），`SDL3_ttf` 与它配不上，
> 文件 IO 就走不通了——**同一份代码换一份 `SDL3.dll` 就不一样**，所以查这条线
> 先数 `PATH` 上有几份 `SDL3*.dll`，别去改代码。

---

## 8. 头文件 → 专页 全量索引

97 个公开头，全部落在上表的某一页里。
**生成物不在 `include/` 里**（`CXXCSS/*.json` 烘出来的 `cxxcss::*` 类落在
`build/inkgen/` 下），见 [api/InkingCxxcss.md](api/InkingCxxcss.md)。

| 目录 | 头文件 | 专页 |
| --- | --- | --- |
| 根 | `inking.h` | Runtime |
| anchor | `InkingAnchor.h` | Anchor |
| asset | `InkingAsset.h` | Runtime |
| audio | `InkingAudio.h` | Components |
| button | `InkingButtonData.h` `InkingButtonLibrary.h` `InkingButtonLook.h` `ButtonPaint.h` `InkingButtonLike.h` `InkingStaticButton.h` `InkingDynamicButton.h` | Components |
| canvas | `InkingCanvasContext.h` `InkingCanvasData.h` `InkingCanvasPainter.h` `InkingStaticCanvas.h` | Components |
| config | `window_config.h` | Window |
| dataStruct | `InkingAffine.h` `InkingTransform.h` `InkingAnchorData.h` `InkingColor.h` `InkingDecoration.h` `InkingEasing.h` `InkingImageFit.h` `InkingImageScale.h` `InkingLayout.h` `InkingScroll.h` `InkingShapeSpec.h` `InkingTextLayout.h` `InkingTextRequest.h` `MessageStruct.h` `TaskStruct.h` | Anchor / InkingShapeSpec / Transform / Layout / Render / Runtime / [§9](#9-素材该多大缩放规则) |
| detail | `InkLog.h` `InkingGeometry.h` `InkingHit.h` `InkingMessageQueue.h` | Runtime / Widget |
| event | `InkingEvent.h` | Runtime |
| image | `InkingImageData.h` `InkingSpriteData.h` `InkingSpriteGroupData.h` `InkingStaticImage.h` `InkingSprite.h` `InkingSpriteGroup.h` `InkingDynamicSpriteGroup.h` `InkingAnimateGroup.h` | Components |
| input | `CursorShape.h` `InkingKeyboardTarget.h` `InkingPointerHandler.h` `InkingPointerButton.h` `InkingKeyboardInput.h` `InkingMouseInput.h` `InkingShortcut.h` `InkingShortcutRegistry.h` `InkingTextEditState.h` | KeyboardTarget / PointerHandler / Input |
| layout | `InkingLayoutSolver.h` | LayoutSolver |
| panel | `InkingStaticPanel.h` `InkingPanelData.h` `InkingPanelLook.h` | Components |
| prefab | `Dialog.h` `FilePicker.h` `FilePickerData.h` `ListRow.h` `ListView.h` `ListViewData.h` `MenuBar.h` `MenuBarData.h` `OptionBar.h` `OptionBarData.h` `PrefabParts.h` `PrefabTheme.h` `ProgressBar.h` `Select.h` `SelectData.h` `Slider.h` `TabBar.h` `ToastHost.h` `ToggleLike.h` `Tooltip.h` | Prefab |
| render | `InkingBatch.h` `InkingDraw.h` `InkingGlyphAtlas.h` `InkingMesh.h` `InkingText.h` | Render |
| scene | `InkingHitTable.h` `InkingScene.h` `InkingSceneLibrary.h` `SceneRegisterToken.h` | Scene |
| text | `InkingStaticLabel.h` `InkingTextInput.h` `InkingTextTable.h` `InkingLabelData.h` | Components |
| thread | `InkingTaskQueue.h` `InkingThreadPool.h` | Runtime |
| timeline | `InkingTimeline.h` | Runtime |
| video | `InkingVideo.h` `InkingVideoData.h` | Components |
| widget | `InkingWidget.h` `InkingStaticWidget.h` `InkingDynamicWidget.h` | Widget |
| window | `InkingWindow.h` | Window |

---

## 9. 素材该多大（缩放规则）

> 规则本身写在代码里：`dataStruct/InkingImageScale.h`（纯头、无 SDL，与
> `InkingImageFitSpec` 同一形态）。这一节是**给使用者与美术读的**版本，公式一字不改，
> 但以头文件为准。设计取舍的完整推演在
> [`plans/2026-10-29-scaling-and-device-pixels.md`](plans/2026-10-29-scaling-and-device-pixels.md)。

### 9.1 四个量

```
R     = min(窗口物理宽 ÷ 设计宽, 窗口物理高 ÷ 设计高)   ← letterbox 倍率
S     = 组件设计尺寸 × 纵深 × R                          ← 它在屏幕上的物理像素
σ     = 素材像素 ÷ S                                     ← 采样比
R_max = min(最大目标屏宽 ÷ 设计宽, 最大目标屏高 ÷ 设计高)
```

### 9.2 规则一：素材按"屏幕像素"出，不按"设计像素的倍数"出

```
素材像素 = 组件设计尺寸 × 纵深 × R_max      // InkingImageScaleSpec::RequiredTextureWidth()
```

关键推论：**素材的绝对像素只取决于"这个组件最大占多少屏幕"，与设计尺寸无关。**

| 设计尺寸 | 全屏 4K 的 R | 按"2× 设计"出图 | σ | 按屏幕像素出图 | σ |
| --- | --- | --- | --- | --- | --- |
| 1920×1080 | 2 | 3840 宽 | 1.0 ✓ | 3840 | 1.0 ✓ |
| 1280×720 | 3 | 2560 宽 | 0.67（放大 1.5×，糊） | 3840 | 1.0 ✓ |
| 640×360 | 6 | 1280 宽 | 0.33（放大 3×，糊） | 3840 | 1.0 ✓ |

所以"设计尺寸小"本身不导致糊——糊的原因是素材按设计倍数出。设计尺寸只是坐标刻度。

### 9.3 规则二：纵深必须落在 `素材倍率 ÷ 整数` 上

不做 mip、不做运行期重采样，就意味着**每个显示尺寸都要靠"素材正好那么大"来保证
σ 为整数**。非整数 σ 会有整列纹素被跳过（表现是细节在运动时游走 / 闪烁），比
"缩得多"更难看。干净档位 = `InkingImageScaleSpec::CleanDepth(素材倍率 m, k) = m / k`：

| 素材倍率 m | 干净纵深档位（k = 1,2,3,4,5） |
| --- | --- |
| 1× | 1.0 / 0.5 / 0.333 / 0.25 / 0.2 |
| 2× | 1.0 / 0.667 / 0.5 / 0.4 / 0.333 |
| 0.5× | 0.5 / 0.25 / 0.167 / — / — |

σ 判定（`InkingImageScaleSpec::IsCleanRatio(sigma)`）：

| σ | 含义 | 观感 |
| --- | --- | --- |
| = 1 | 1:1 | 最清 |
| 整数且 > 1 | 缩小，双线性 ≈ k×k 盒式 | 干净（信息只剩 1/k²） |
| 非整数 | 部分纹素被跳过 | 脏，运动时闪 |
| < 1 | 放大 | 软（linear）或块（nearest） |

### 9.4 规则三：边缘模糊有四个成因，本库只解决两个

| 成因 | 归属 | 本库的做法 |
| --- | --- | --- |
| 抗锯齿带按设计像素（R≠1 即失真） | 库 | **推迟到 `1.x.x`**（羽化在顶点 alpha 上，与素材分辨率无关） |
| 素材不足（σ<1） | 使用者 | `INK_ISDEBUG` 下构造期 WARN（`image_undersampled`） |
| 非整数 σ 的采样不均 | 使用者 | `IsCleanRatio()` 可判 + 本节档位表 |
| 非整数 R 的像素不均 | 选择 | 不适用（进程级 `Linear`） |

### 9.5 怎么用

```cpp
#include <dataStruct/InkingImageScale.h>

InkingImageScaleSpec spec;
spec.designWidth = 320;   // 组件在设计坐标里的宽度
spec.depth       = 1.0f;  // 没有纵深系统就填 1
spec.ratioMax    = 2.0f;  // 最大目标屏 ÷ 设计尺寸；不知道就填 2
const int need   = spec.RequiredTextureWidth();          // 交给美术："出这么宽"

const float sigma = spec.SamplingRatio(纹理宽, 当前 R);   // 装载期自检 / 打日志
const bool clean  = InkingImageScaleSpec::IsCleanRatio(sigma);
```

验收示例：`examples/sharp`（720p 设计尺寸 + 一张 1536×2752 的真实素材，全屏逐档
打印 R 与 σ，人工对照预测表）。

---

> 本文档对应版本：**0.9.0（0.x 发布版）**。发现文档与代码不一致时，
> **以头文件注释为准**，并请把差异记到 `docs/zh/CHANGELOG.md`。
