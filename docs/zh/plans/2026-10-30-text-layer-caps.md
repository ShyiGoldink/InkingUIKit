# 任务书：文字层的上限与静默（缺陷排查结论落地）

> 状态：**已完成**（5 个任务全绿，见 §6.2；细目已按 §1.2 压缩，设计理由在代码注释与
> `CHANGELOG.md` 的 0.8.3~0.8.5 一节里）
> 日期：2026-10-30
> 来源：本日缺陷排查（`-Wall -Wextra` 全量编译 42 条告警 / GCC `-fanalyzer` 66 个 TU 0 条 / 7 个探针实测）；核实结论见 §2
> 目标版本：`0.8.3`（裁行与不重烘）/ `0.8.4`（发声与预算）/ `0.8.5`（告警门）
> 涉及层：`src/render/InkingText.cpp`、`src/text/InkingStaticLabel.cpp`、`src/dataStruct/InkingTextLayout.cpp`、`src/scene/SceneLibrary.cpp`、`CMakeLists.txt`、`docs/`

---

## 0. 结论（TL;DR）

一条排查结论：**文字层有三条路径没有上限、且全部静默**。拆成 **5 个任务、3 个批次**；批次顺序按"能立刻断言的先做、定策略的后做"排：

| 编号 | 任务 | 批次 | 对应条目 | 落点 | 代价 |
| - | - | - | - | - | - |
| T-01 | 文字按框高裁行 | 0.8.3 | E3 | `src/dataStruct/InkingTextLayout.cpp:314`、`src/text/InkingStaticLabel.cpp:153` | 低 |
| T-02 | 图集满后不再逐帧重烘字形 | 0.8.3 | E2 | `src/render/InkingText.cpp:1157` | 低 |
| T-03 | 图集满发声并进静默清单 | 0.8.4 | E2 | `src/render/InkingText.cpp:1157`、`docs/AGENTS.md` §7 | 低 |
| T-04 | 给文字纹理加字节预算与 LRU 淘汰 | 0.8.4 | E1 | `src/render/InkingText.cpp`（`TextLayer` / 新增 `BeginTextFrame`） | 中 |
| T-05 | 清掉严格告警并把它挂进 ctest | 0.8.5 | E4 | `CMakeLists.txt`、8 个源文件 | 低 |

两条设计取向（本次的取舍底线）：

1. **先止血、再定预算**。T-01 / T-02 各一条断言就能收口；T-04 的阈值要等"每行一张纹理"这条路被压下来之后再定——否则会照着一个虚高的峰值（43.7 MB）去定预算。
2. **淘汰必须可关**。`SetTextureBudget(0)` 等价于 0.8.2 的行为，任何"文字量本来就小"的项目不该被这条改动绑住。

---

## 1. 任务维护规则（本文件的写法约定）

### 1.1 每个任务必须写三段

| 段 | 要求 |
| - | - |
| **准备怎么改** | 决策与选型：做什么、走哪条路、为什么；**放弃的候选要写出来** |
| **大致改动逻辑** | 代码级顺序：改哪个文件、加哪个函数、谁调谁、哪一步不能反。不贴大段代码 |
| **与改动前对比** | 表格；**至少一行"改动后仍不变"的边界**，否则是过度承诺 |

另附 **落点**、**验收**（可断言）、**风险与回退**（至少各一条）。

### 1.2 完成即压缩（核心规则）

任务完成后，把该任务整段压缩成一行：

```
- T-01 <标题>-已完成（<YYYY-MM-DD>，<一句可验证的结论>）
```

删除「准备怎么改 / 大致改动逻辑 / 与改动前对比 / 验收 / 风险」五个细目，并把 §6 进度表的状态改为 `已完成`。**两处必须同改。**

细目不留在本文件里——它在 git 历史中，回看用 `git log --grep=T-01`。

### 1.3 四种状态

| 情况 | 写法 |
| - | - |
| 完成 | `- T-01 <标题>-已完成（日期，结论）` |
| 放弃 | `- T-01 <标题>-已放弃（原因；替代方案）` |
| 拆分 | `- T-01 <标题>-已拆分为 T-01a / T-01b`，各自走完整三段 |
| 阻塞 | `状态：阻塞（原因）`，保留全部细目，不压缩 |

### 1.4 完成的定义（DoD）

四条全中才算完成，缺一条保持 `未开始` / `进行中`：

1. 编译通过；
2. 验收断言在自检里跑绿（不是临时探针）；
3. 涉及文档同步改；
4. 「与改动前对比」每行都能对上实测。

---

## 2. 基线事实（核实结论）

探针都在 `build/`（已被 gitignore），可原样复跑：

> **复跑前先重编**：这些 exe 是手工编的，而本轮改了 `TextCacheStats` / `GlyphAtlasStats`
> 的**字段**（按值返回）——旧 exe 与新的 `libinking.dll` 一起跑会崩（退出码 127、
> 输出在第一行就断）。重编命令：
> `g++ -std=c++20 -O1 -I../include probe_text_xxx.cpp -o bin/probe_text_xxx.exe -Lbin -linking -L/c/msys64/ucrt64/lib -lSDL3`。
>
> 另外：**T-01 之后 `probe_text_cost` 的"行数"会变小**（框高开始真的裁行了），
> `probe_text_caps` 的 ① 也不再用"同一段重复字串"把图集灌满（那样只有十几个不同字形）。
> 所以下面这些数字是**改动前**的基线，不是现在的读数；现在的判据在自检里
> （`text_clip_by_height` / `glyph_atlas_full` / `T-04` 那三条）。

```bash
cd build/bin && PATH="/c/Windows/system32:/c/Windows:/c/msys64/ucrt64/bin" ./probe_text_cost.exe
```

| 探针 | 问的是什么 |
| - | - |
| `probe_text_growth` | 换文字是否新增纹理、缓存键是否生效、`ReleaseTextCache` 是否唯一的回收口 |
| `probe_text_caps` | 图集填满的代价（重烘次数 / WARN 条数）、新串堆纹理的速度、超长文本 |
| `probe_text_layout` | 极窄宽 / 0 宽 / 负宽 / 纯换行 / 超长词会不会挂死 |
| `probe_text_clip` | 不可见的行会不会也被栅格化（裁剪剔除） |
| `probe_text_long` | 一次上 2 万字，标签高 40 / 400 / 不限 三档的纹理张数与像素 |
| `probe_text_overflow` | 文字是否画到组件框外（读回像素计数） |
| `probe_text_cost` | 同字数不同纹理数 → 首帧耗时与内存的因果（E1 的主证据） |

| 条目 | 判定 | 证据 |
| - | - | - |
| E1 文字纹理只建不淘汰、无字节上限 | **成立** | `probe_text_cost`：同样 1334 行，用 3 个不同字时 **1 张 / 4 MB / 221 ms**，2 万个不同字时 **1220 张 / 43.7 MB / 1603 ms**；5 千字现实量级 **220 张 / 11.3 MB / 362 ms**。全库唯一的回收口是 `ReleaseTextCache()`（全清），无任何自动路径 |
| E2 图集满后永久退化且完全静默 | **成立** | `probe_text_caps`：1024² 图集在 20 px 下装 **1728 格**即满；之后连画 60 帧（文字不变）仍**多烘 1140 次 / 多失败 1140 次**（19 次/帧），日志 **WARN 0 条**；只有换设备比例或手动 `ReleaseTextCache()` 才会复位 |
| E3 `TextOverflow::Clip`（默认值）没实现 | **成立** | 头文件写"`Clip` = 多余的行不画"（`InkingTextLayout.h:56`），而 `LayoutForWidth` 只传 `maxWidth`/`maxLines`（`InkingStaticLabel.cpp:164`）、`maxHeight` 只参与垂直对齐（`InkingTextLayout.cpp:375`）、`onRender` 逐行全画（`:228`）。`probe_text_overflow`：300×40 标签 + 7 行 → **框外 10,030 个亮像素**（框内 1,770） |
| E4 42 条严格告警，且不在任何构建门内 | **成立** | `-Wall -Wextra -Wshadow -Wcast-qual -Wuseless-cast` 全量重编 → 42 条：`-Woverloaded-virtual` 15（`Activate` 被 `InkingScene`/`OptionBar` 隐藏）、死参数 4、死变量 2、死函数 2、无用 cast 10、`NOMINMAX` 重定义 1。默认 flags 里一条都没有。逐条：`InkingWindow.cpp:36` NOMINMAX、`InkingDraw.cpp:608` 死参 `hasAlpha`、`InkingScroll.cpp:96` `vertical`、`InkingAudio.cpp:747` `mode`、`OptionBar.cpp:158` `horizontal`、`InkingVideo.cpp:907` `impl`、`InkingScene.cpp:52` `magnify`、`inkgen/Emit.cpp:235` `Indent` |
| 「滚动列表会画到框外」 | **不成立** | `ListView` 默认 `windowed` + `panel.clipChildren = true`（`src/prefab/ListView.cpp:103`），`HasSubtreeClip()` 已贯通绘制与命中（`InkingGeometry.cpp:137`）。这里没问题，别照 E3 的结论去改列表 |
| 「UTF-8 按字节切会劈开汉字」 | **不成立** | `TextEditState::NextCodePoint` 先 `SnapToBoundary` 再前进（`src/input/TextEditState.cpp:70`）；`LayOutGlyphs` 按码点切（`src/render/InkingGlyphAtlas.cpp:124`） |
| 「静态分析能扫出内存缺陷」 | **不成立（0 条）** | `-fanalyzer` 跑遍 66 个 `.cpp`，除 `NOMINMAX` 重定义外无输出。范围仅限单 TU 内的空指针 / 泄漏 / 双重释放，不覆盖跨 TU 与 SDL 所有权 |

---

## 3. 第一批：0.8.3（止血：裁行 + 不重烘）

- T-01 文字按框高裁行-已完成（2026-10-30，`LayoutText` 在 `maxLines` 之后按
  `maxHeight − 上下 padding` 丢尾行并置 `truncated`；300×40 + 100 汉字从"7 行 / 框外
  10,030 亮像素"变成"1 行 / 框外 0"，自检 `text_clip_by_height` 绿）
- T-02 图集满后不再逐帧重烘字形-已完成（2026-10-30，`TextLayer::glyphAtlasFull` 置起后
  逐字形路直接短路、只在 `ReleaseGlyphAtlas()` 复位；灌满后连画 60 帧 `rasterized`
  增量 0，自检 `glyph_atlas_full` 绿，且**对照组**（临时关掉短路）确实 +7 而非 0）

## 4. 第二批：0.8.4（发声 + 预算）

- T-03 图集满发声并进静默清单-已完成（2026-10-30，首次满 `CountOf(Warn) == 1`、再灌
  2000 个新字仍 == 1；`AGENTS.md` §7 第 13 条 + `kSilentFailures` + `NoteSilentFailure`
  三处同改，`silent_failure_list_in_sync` 绿）
- T-04 给文字纹理加字节预算与 LRU 淘汰-已完成（2026-10-30，预算 0 ⇒ 402 张长串 12.8 MB
  只涨不落、淘汰 0 次；预算 8 MB ⇒ 8.43 MB ≤ 8 MB + 单张 61,776、淘汰 271 次，被丢
  那条重画能重建；淘汰在 `SceneLibrary::RenderScene` 里帧首调，丢之前先 `FlushGeometry`）

## 5. 第三批：0.8.5（告警门）

- T-05 清掉严格告警并把它挂进 ctest-已完成（2026-10-30，25 条真告警逐条清掉；`ctest`
  7/7 绿，其中 `strict_warnings` 用同一批源 + 另一套 flags 重编 `src/` 与
  `tools/inkgen/`；门经过"临时加 unused 变量必红、删掉恢复"的人工对拍）

## 6. 批次门槛与进度

### 6.1 门槛

| 批次 | 发布前必须满足 |
| - | - |
| `0.8.3` | `probe_text_overflow` 框外亮像素 **== 0**；`probe_text_caps` 的"60 帧重烘"增量 **== 0**；两条新自检进 ctest 且全绿；`API.md` / `InkingRender.md` 补"框高即上限"一句 |
| `0.8.4` | 图集满时 WARN **恰好 1 条**、`silent_failure_list_synced` 13 条绿；预算 8 MB 下 `textureBytes <= 8 MB + 单张最大`；`SetTextureBudget(0)` 时与 0.8.3 逐字节一致 |
| `0.8.5` | `ctest -R strict_warnings` 绿；`tools/BUILD.txt` 的验证记录补一条（含"故意加 unused 变量会红"） |

### 6.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 文字按框高裁行 | 0.8.3 | 已完成 |
| T-02 | 图集满后不再逐帧重烘字形 | 0.8.3 | 已完成 |
| T-03 | 图集满发声并进静默清单 | 0.8.4 | 已完成 |
| T-04 | 给文字纹理加字节预算与 LRU 淘汰 | 0.8.4 | 已完成 |
| T-05 | 清掉严格告警并把它挂进 ctest | 0.8.5 | 已完成 |

完成任务后，本节状态改 `已完成`，§3~§5 对应段落按 **§1.2** 压缩成一行——两处必须同时改。

---

## 7. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| **`Activate` 改名（`ActivateByName` 等）** | 公开面变更：`InkingScene::Activate(const std::string&)` 与 `OptionBar::Activate(size_t)` 都把基类虚函数藏了，`-Woverloaded-virtual` 15 条都指向它。现在不出错（调用方要么走具体类型、要么拿不到那个重载），改名要动公开头与迁移文档，留给 1.0 的公开面盘点 |
| **把字形图集扩到 2048²** | 面积 4×、常驻 32 MB。先看 T-04 的淘汰效果；图集是共享资源，扩容只是把"什么时候满"推后 |
| **按帧龄过期（不做预算）** | 没有内存上界：一次 2 万字的那一帧照样冲到 43.7 MB。它只能当 T-04 的补充 |
| **字体子集化 / 跨进程共享字形纹理** | 另一个量级的事（要自己做字体裁剪与文件格式），与本批"先给上界"的目标不同 |
| **默认打开预算** | 第一次做淘汰，默认改变所有人的内存行为不可接受；先让宿主显式设，观察一轮再谈 |
| **给标签加 `TextOverflow::Overflow`（允许溢出）** | 与 `Clip` 的默认语义直接冲突，且会让 E3 的 43.7 MB 变成"合法行为"。要溢出的人把 `height` 设 0 即可 |

### 7.1 本轮顺手发现、但**没有处理**的一件事

`src/video/InkingVideo.cpp` 的 FFmpeg 分支（`#if defined(INK_HAS_FFMPEG)`）**从来没被
编进去过**：`CMakeLists.txt` 与 `cmake/*.cmake` 里**没有任何地方定义这个宏**。于是
`InkingStaticVideo` 编的是 `#else` 那条空实现——能编、能链、功能是空的，与 0.6.x 的
`INK_HAS_SDL3_MIXER` 漏定义完全同型（`AGENTS.md` §2 第 2 条）。

- **证据**：`g++ -fsyntax-only -DINK_HAS_FFMPEG` 编 `src/video/InkingVideo.cpp` **零错误**
  （说明代码本身是能编的，缺的只是那个宏）；不定义它时严格门报 `unused variable 'impl'`
  ——那条告警就是这件事的影子。
- **本轮只清告警**：在 `onRender` 的 `#else` 里 `(void)impl;` 并写下这段记录。
  **没有**在 CMake 里打开它：打开等于第一次让视频解码真的跑起来，而它现在既没有
  视频素材、也没有一条像素断言——"清告警"这个任务不该顺手打开一个从没跑过的子系统。
- **要做的下一件事**：给它一个素材（几帧 mjpeg 的 `.mp4` 就够）+ 一条"解出第一帧、
  像素不是全黑"的自检，然后再定义宏、补进 `strict` 门与 `dist`（FFmpeg 的 DLL 闭包
  与 LGPL 声明也才需要跟着走）。

