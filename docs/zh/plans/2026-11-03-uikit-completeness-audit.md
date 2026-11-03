# 审计：作为「简单 UI 库」，它齐不齐

> 日期：2026-11-03
> 方法：`include/` 24 个子系统逐个过 + 关键词 `grep`（零命中即为缺失）+ 发行包二进制核对
> 版本：0.8.5（`include/inking.h:20`）
> 性质：**审计，不是任务书**。补什么、先补什么，等看完再定

---

## 0. 结论

**能力面齐，预制件缺九个，系统集成缺三样。视频不是没做，是开关没开。**

| 层 | 判定 | 说明 |
| - | - | - |
| 渲染 / 布局 / 命中 / 场景 / 输入 | **齐** | 比一般 UI 库深：两档脏、烘焙命中表、布局求解器、输入统一成 `Primary`/`Secondary` |
| 基础组件 | **齐** | 按钮（三态+图+SVG）、标签（排版/省略/多语言）、图片（静态/动态/帧动画/图集）、面板（滚动/裁剪/开合）、输入框、画布 |
| 预制件（控件） | **缺九个** | 见 §3 |
| 系统集成 | **窗口很全，缺三样** | 见 §4 |
| 音频 / 主题 / 多语言 / 线程 / JSON / 日志 | **齐** | 音频含流式 + 异步加载 + 三级音量 |
| 视频 | **写完了，没接线** | 见 §1 |

一句话：**它缺的不是「能力」，是「预制件」。** 那九个控件全部能用现成的
基类 + 面板 + 布局搓出来（`include/event/InkingEvent.h:45` 自己就写着一个
`class Slider : public InkingStaticWidget` 的示例），缺的是「拿来就能用」的那一层。

---

## 1. `InkingVideo`：写完了，但**从来没被编进去过**

你记得没错——但准确说法是「有代码、没开关」。

| 项 | 现状 |
| - | - |
| 公开头 | `include/video/InkingVideo.h`，180 行，`class InkingStaticVideo` |
| 实现 | `src/video/InkingVideo.cpp`，**37 KB**，含音画同步（音频位置当主时钟）、后台线程解码、pts 追帧、丢帧计数 |
| 设计 | 完整：四条设计决定写死在头注释里，连「为什么走 `onRender` 而不走 `Tick`」都记了 |
| 编译开关 | `INK_HAS_FFMPEG` |
| 开关状态 | **从未被任何 CMake 文件定义** |

证据链：

```
CMakeLists.txt:138-147   pkg_check_modules(FFMPEG REQUIRED ...) + PkgConfig::FFMPEG  ← FFmpeg 是 REQUIRED 且链接了
CMakeLists.txt:174-177   target_compile_definitions(inking PRIVATE
                             INK_HAS_SDL3_TTF
                             INK_HAS_SDL3_IMAGE
                             INK_HAS_SDL3_MIXER)      ← 只有三个，没有 INK_HAS_FFMPEG
```

于是 `InkingVideo.cpp` 每一处 `#if defined(INK_HAS_FFMPEG)` 都走 `#else`
（`:23` / `:36` / `:134` / `:172` / `:609` / `:621` / `:702` / `:808` / `:882` / `:907`）。

**这跟 `INK_HAS_SDL3_MIXER` 那次是同一个坑，`CMakeLists.txt:169-173` 自己写着那段教训**：
「宏没有 ⇒ 编进去的是空实现，于是 `AudioInit()` 恒 false、BGM / SE / 语音全部静默无声，
而 CMake 仍然 find_package + 链接 —— 能编能链把这件事完全盖住了」。视频现在就在这个状态。

而且这件事**已经被诚实登记过**，只是没被修：

| 位置 | 原文 |
| - | - |
| `tools/THIRD-PARTY-NOTICES.md:36` | 「`src/video/` 走 `INK_HAS_FFMPEG` 分支，本版未开启，二进制**不引用**其符号」 |
| `tools/BUILD.txt:244` | 「**从未被任何 CMake 文件定义过**」 |
| `README.md:446` | 「本包**不含** FFmpeg DLL」 |
| `docs/zh/plans/2026-10-30-text-layer-caps.md:176-181` | 「`g++ -fsyntax-only -DINK_HAS_FFMPEG` 编它**零错误**」 |

实测核对：`dist/inking-0.8.5-win64/bin` 里没有任何 `av*.dll` / `libsw*.dll`，
`build/bin` 的 20 个 DLL 里也没有。二进制确实不引用。

> **所以开视频的工作量 ≈ 加一个 option + 一个 `target_compile_definitions`，
> 再加一个「没装 FFmpeg 时怎么退」的分支。** 代码和自检都写好了，
> 差的是那两行 CMake 与一次真机验证。

---

## 2. 有的一张表（免得补的时候重复造）

**组件**：`InkingStaticButton` / `InkingDynamicButton` · `InkingStaticLabel` / `InkingDynamicLabel` ·
`InkingStaticImage` / `InkingDynamicImage` / `InkingSprite` / `InkingDynamicSprite` / `InkingSpriteGroup` ·
`InkingStaticPanel` / `InkingDynamicPanel` / `InkingFixedPopup` · `InkingTextInput` · `InkingStaticCanvas` ·
`InkingStaticVideo`（空壳）

**预制件**（`prefab/`，10 个实现）：`MenuBar` · `MenuPopup` · `OptionBar` · `Select`（下拉/选择器，
带过滤）· `ListView` · `ListRow` · `FilePicker`

**基础设施**：`anchor`（定位）· `layout`（求解器 + 绑定）· `scene`（场景 + 命中表）·
`render`（Mesh / Batch / GlyphAtlas / Svg / Text）· `input`（键盘 / 鼠标 / 指针 / 快捷键 / 文本编辑态）·
`event`（事件槽）· `timeline`（动画时钟）· `theme` · `text`（语言表）· `audio` · `thread`（线程池 + 任务队列）·
`util`（JSON）· `detail`（几何 / 命中 / 日志 / 消息队列 / 指针探针 / 浮窗定位）

**零命中即缺失**（`grep` 全仓库，只命中注释里的示例）：见 §3 / §4。

---

## 3. 缺的控件：九个

按「简单 UI 库」该有的常用控件对：

| # | 控件 | 关键词 `grep` 命中 | 现状 | 手搓要什么 |
| - | - | - | - | - |
| 1 | 复选框 Checkbox | 0 | 缺 | 按钮 + 勾形状 + 一个 bool 状态 |
| 2 | 单选按钮 Radio | 0 | 缺 | 同上，加组内互斥 |
| 3 | 开关 Switch | 0 | 缺 | 面板 + 一块滑动的高亮块 |
| 4 | 滑块 Slider | 只有 `InkingEvent.h:45` 的**示例注释** | 缺 | 按 96 条（相对按下点的累计位移）做 |
| 5 | 进度条 ProgressBar | 0 | 缺 | 面板 + 按比例裁一块 |
| 6 | 标签页 TabBar | 0 | 缺 | `OptionBar` 换一套外观 + 面板切可见性 |
| 7 | 弹窗 Dialog / MessageBox | 0 | 缺 | `InkingFixedPopup`（已存在）+ 按钮 |
| 8 | 悬浮提示 Tooltip | 0 | 缺 | `InkingFixedPopup` + 延迟显示 |
| 9 | 通知 Toast | 只有注释里当「将来」的例子 | 缺 | 面板 + `timeline` |

另外两个**可选**（数据量大了才需要）：`TreeView`、`TableView`。

**它们都不是「新能力」**——`InkingEvent.h:45` 的注释里自己就写着：

```cpp
class Slider : public InkingStaticWidget {
    // ...
};
```

缺的是「预制件」这一层：`prefab/` 已经有 `MenuBar` / `Select` / `ListView` / `FilePicker` 四个
足够复杂的，前九个反而是更简单的那些，一直没人回头补。

---

## 4. 系统集成：窗口很全，缺三样

**窗口层做得相当完整**（`include/window/InkingWindow.h`，40+ 个公开方法）：
无边框 · 置顶 · 透明（含 `IsTransparentApplied` 这种「请求了但没生效」的观测口）· 圆角 ·
全屏 · 缩放阶梯（0.25~4.0，`kMinZoom` / `kMaxZoom`）· 移动 / 最小化 · 截图 / `RenderToTexture` ·
退出确认回调 · 快捷键注册表 · 后台帧任务

缺的三样（`grep` 全仓库零命中）：

| # | 能力 | 关键词 | 对「简单 UI 库」的影响 |
| - | - | - | - |
| 1 | **鼠标光标形状** | `SDL_SetCursor` | 悬停可点区域不变手型，用户少一个「这里能点」的提示。游戏界面里很常见 |
| 2 | **文件拖放** | `SDL_DropEvent` / `DROPFILE` | 拖一张图进来换立绘 —— 视觉小说里很自然的操作，现在做不到 |
| 3 | **手柄输入** | `Gamepad` / `Joystick` | 纯键盘 + 鼠标。视觉小说多在 PC，优先级低 |

另外两条**结构性**的，不算「缺」但要记：

- **单窗口**：`InkingWindow::Instance()`（`InkingWindow.h:36`）是单例，没有多窗口。
  对「简单 UI 库」是合理取舍；要做多窗口得改一大片（`currentRendererSlot` 那一族全是进程唯一槽位）。
- **无无障碍**：没有 `Accessib` 相关。这一条在游戏里普遍不做，记一笔即可。

---

## 5. 判断：够不够

**够，而且比「简单」多。**

理由：它有一个 UI 库最难的几块（布局求解、命中表、两档脏、字体纹理层、主题、多语言），
这些是「补不了」的；而它缺的九个控件是「补得了」的，且每个都只需要几十行。

真正的短板不在控件数量，在**「拿来就能用」的密度**：
`prefab/` 里四个复杂控件都在，最常用的九个反而没有。

---

## 6. 如果要补，我建议的顺序

| 批 | 内容 | 理由 |
| - | - | - |
| 一 | **开 `INK_HAS_FFMPEG`** | 代码和自检全在，差两行 CMake + 一次真机验证。性价比最高 |
| 二 | `Slider` + `ProgressBar` + `Switch`/`Checkbox`/`Radio` | 设置界面 + 视觉小说必备，全都只依赖现成能力 |
| 三 | `Tooltip` + `Dialog` + `Toast` | 三个都是 `InkingFixedPopup` 变体，浮窗那套已经有了 |
| 四 | 光标形状 + 文件拖放 | 体验向，不动核心 |
| 五 | `TabBar`、`TreeView`、`TableView` | 数据量大了再说 |

---

> 本文只做盘点。要动哪一批，按 `task-driven-project-build` 拆任务书；
> 视频那一条单独列，因为它牵的是 CMake 与依赖许可，不是组件代码。
