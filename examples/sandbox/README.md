# 2.5D 小沙盒 · 静态骨架（SBOX-01）

一片**有限斜投影（`InkingFieldMode::Oblique`）瓦片地图** + 草 / 沙 / 水 / 石砖四个分区
+ 物体面一棵树 + 单独一格用 `tint` 的 alpha 做的简易水面。纯展示，**能编译能跑**。

明确不在范围内：交互、角色、autotile、动画、摄像机移动、碰撞、网络。
地基是库自带的 `field/InkingField.h`，**没有另造渲染层或投影层**。

---

## 1. 文件与职责

| 文件 | 职责 |
| - | - |
| `CMakeLists.txt` | 挂进根构建，产出 `inking_sandbox.exe`（落 `build/bin`） |
| `main.cpp` | 程序生成图集 → 建窗口 / 场景 → 配 Oblique 投影 → 填 tiles / objects → 主循环；另带 `--probe` 离屏自检 |
| `SandboxPalette.h` | **唯一**的颜色常量处（`0xAARRGGBB`），与 `docs/art-palette.md` §8 逐条对齐 |
| `README.md` | 本文 |
| `docs/art-palette.md` | 美术侧配色规格（SBOX-02 产出，非本任务文件） |

运行期会在 exe 旁的 `assets/` 下生成一张 `sandbox_atlas.png`（程序生成，不是外置美术资源）；
`--probe` 还会在当前工作目录写一张 `sandbox_probe.png` 截图。这两个文件都是**构建产物**，
不是源码，删掉会在下次启动时自动重建。

---

## 2. Build

在仓库根目录（`D:/InkingUIKit`，工具链 MSYS2 UCRT64）：

```bash
cmake --build build
```

`examples/CMakeLists.txt` 里追加了 `add_subdirectory(sandbox)`；Ninja 检测到
CMakeLists 变化会自动重跑 configure，**不需要手工 `cmake -S . -B build`**。
若从干净构建树开始：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="C:/msys64/ucrt64"
cmake --build build
```

产物：`build/bin/inking_sandbox.exe`。根 CMake 会把 `libinking.dll` 与 SDL3 / SDL3_image /
SDL3_ttf / SDL3_mixer 的运行期 DLL 闭包拷到同一个 `bin/`，**不用手动设 PATH**。

只编这一个目标（快）：

```bash
cmake --build build --target inking_sandbox
```

单文件语法检查（工程约定）：

```bash
/c/msys64/ucrt64/bin/g++ -std=c++20 -fsyntax-only \
  -DINK_ISLOG=1 -DINK_ISDEBUG=1 -Iinclude examples/sandbox/main.cpp
```

---

## 3. Run

```bash
./build/bin/inking_sandbox.exe          # 真窗口，关窗 / ESC 退出
./build/bin/inking_sandbox.exe --probe  # dummy 驱动离屏跑 2 帧，打印 GetStats()
```

`SANDBOX_PROBE=1` 环境变量等价于 `--probe`。`--probe` 不依赖显示器，适合 CI / 远程桌面。
退出码：`0` = 自检判定"活的"；`1` = 场景登记 / 场地搭建失败；`2` = 建不出渲染器；
`3` = `GetStats()` 里出现静默失效（见 §5）。

---

## 4. 调参

全部集中在 `main.cpp` 顶部匿名 namespace 的常量块，改完重新 build 即可：

| 常量 | 当前值 | 作用 / 注意 |
| - | - | - |
| `kSkewDeg` | `26.57f` | 深度线偏离垂直的角度（度），`atan(0.5)` ≈ 2 竖:1 横的等距角。**有效区间 `[0, 80]`**，越界只留一条 WARN 并保留上一次参数 |
| `kZoom` | `1.30f` | 1 世界单位 = 多少设计像素；必须 `> 0` |
| `kCellWidth` / `kCellDepth` | `64` / `32` | 一格的世界尺寸 = 瓦片的像素尺寸（`src` 就是 64×32） |
| `kCols` / `kRows` | `14` / `24` | **有限地图大小**。Oblique 下必须都 `> 0`，否则投影被拒、整块不画 |
| `kDepthScaleFar` | `0.72f` | 最远处尺寸比，`(0, 1]`；`< 1` 才有透视与天空带 |
| `kHorizonRatio` | `0.28f` | 天空占画布高度比例，`[0, 1)`；库会**反解 `farD`**（`kFarD` 会被覆盖） |
| `kCameraX` | `448` | 地图横向中心。设 0 会让地图从画布中心往右铺、左半屏全空 |
| `kSkewDeg` 改大要注意 | — | `cos(skew) → 0` 时地面反解无解；`80°` 是硬上限 |
| `kSurfaceCellI/J` | `10 / 16` | 单独那一格半透明水面的位置（目前落在水分区中间） |
| `kTreeCellI/J` | `3 / 4` | 树所在格（目前落在草分区中间） |

配色**不要**在 `main.cpp` 里改：只改 `SandboxPalette.h`。该文件末尾的三条
`static_assert` 会挡住"换色后四档明度差 < 30"这类回归（美术规格 §6 / §8）。

想换分区布局：改 `SetupTiles()` 里的 `zone` 计算与 `kZoneTint[]` 映射（那是
**tint 表**，不是纹理格表——四种地表共用同一个 `src`）。
想换地表明暗 / 图案：改 `PaintRelief()`（只有白 / 灰阶，**不要在这里写地表色**）。
想换树 / 水面的形状：改 `PaintTree()` / `PaintSurfaceCell()`。图集是一次性生成的，
不是每帧逐像素光栅化。

---

## 5. 怎么验它是活的（最重要）

这一层的失效**全是静默的**：远处少画、地形停在上一帧、瓦片被截、整块因祖先变换不画，
画面上都只是"少了点东西"。唯一可靠的判据是 `InkingField::GetStats()`。

```bash
./build/bin/inking_sandbox.exe --probe
```

输出（2026-11-12 实测，`14×24` / `zoom 1.30`）：

```
-- sandbox probe / GetStats()（上一帧口径）--
   projectionValid      = 1  (期望 1)
   transformBlocked     = 0  (期望 0)
   tileCount            = 336  (给了几块瓦片)
   objectCount          = 1  (给了几个物体)
   objectsDrawn         = 1  (期望 1：那棵树)
   visibleCells         = 336  (期望 > 0)
   terrainBatches       = 1  (期望 1..8)
   objectBatches        = 1
   truncatedTiles       = 0  (期望 0)
   rejectedBatchesTotal = 0  (期望 0，累计口径)
   culledSubPixel       = 0
   skyQuads             = 0
   结论                 = 活的
```

> `skyQuads = 0` 是**正常的**：本示例只用 `SetSkyColor` 纯色天空，没有 `SetSkyTexture`；
> 天空带是否出现看画面顶部那条 `kSkyColor`，不看 `skyQuads`。

| 字段 | 坏了长什么样 | 先查什么 |
| - | - | - |
| `projectionValid = 0` | 整块停在上一次的参数 | `cols/rows > 0`、`skew ∈ [0,80]`、`depthScaleFar ∈ (0,1]`、`farD > 0`、`horizonRatio ∈ {-1} ∪ [0,1)` |
| `transformBlocked = 1` | **整块不画** | 场地不是场景直接子节点 / 父链上有平移缩放 |
| `objectsDrawn = 0` | 树不见了 | 纹理句柄无效、`tint` alpha 为 0、被 1px 规则剔掉 |
| `visibleCells = 0` | 没有地可画 | 相机 / zoom / 地图边界 |
| `terrainBatches = 0` 或 `> 8` | 地形没提交 / 被层池截断 | 层池 8 批 × 4096 四边形 |
| `truncatedTiles > 0` | 远处缺瓦片 | 可见格超过 `8 × 4096` |
| `rejectedBatchesTotal > 0` | 地形整块停在上一帧 | 地形批顶点超限 / 纹理解析失败（**累计**口径，出过一次就一直是 1） |
| `culledSubPixel` | 远物体被剔 | 只在投影缓存重建那一帧非 0，**别当累计量读** |

`--probe` 用 `SDL_HINT_VIDEO_DRIVER=dummy` + 软件渲染器，走的是和窗口层同一条
`InkingSceneLibrary::RenderScene(renderer)` 提交链；它还会 `SDL_RenderReadPixels`
写一张 `sandbox_probe.png`，可以直接肉眼确认四分区、树、水面确实画出来了。

---

## 6. 已知限制（如实列出）

1. **瓦片是平行四边形，不是四角透明的 64×32 菱形。** `InkingField::Oblique` 的
   `+x` 轴恒水平、只把纵深轴按 `skew` 倾斜，格四边形必然是平行四边形；
   美术规格 §5 假设的"菱形四顶点 + 四角透明"需要另一套投影（或旋转整块场地），
   本任务禁止另造投影层，所以没有实现。取 `skew=26.57°` + 格 `64×32` 是为了让
   那条斜边落在 2 竖:1 横的等距角上，尽量靠近菱形观感。
2. **单格水面是替换而不是叠加。** `SetTiles` 的"格 → 瓦片"索引是后者赢，
   一个格只能有一块瓦片，所以简易水面那一格用白色 relief + `kWaterSurface`
   （alpha `0x99`）直接染出半透明水面层，**底下并没有再铺一块水瓦片**。
   它放在地形面（瓦片），不是物体面：物体在屏幕上是正矩形、不贴合格子
   （美术规格 §7-E 选项 ①）。
3. **乘性 tint 的亮度上限：`kXxxLit` / 绝对色 `kTileOutline` 在这条路径上不可达。**
   补充对齐项 C 已落地：一张共用的白 / 灰阶 relief + `tint = kXxxBase`。
   但 `SDL_Vertex.color` 是**乘性调制、只能变暗**：
   - 顶面纯白 ⇒ `tint × 1.0` = `kXxxBase`，四档明度阶梯 202/147/112/79 逐位精确；
   - 上沿受光棱也只能取纯白（乘法的最大值），实现为"**未被压暗的 Base**"，
     比 Base 更亮的 `kXxxLit` 做不到；明暗方向由下沿灰阶压暗承担；
   - 外轮廓改用 `kTileOutlineSoft`（半透明中性黑）靠 BLEND 叠在 `kGroundColor` 上，
     四种 tint 下都收敛到接近 `kTileOutline` 的深色，而不是恒等那个绝对色。
   `kXxxLit` / `kXxxShade` / `kTileOutline` 仍按规格 §8 留在 `SandboxPalette.h`，
   供"BLEND 不可用 → 退回实色"和以后换渲染路径时使用。
   **§2.2 的四种不同地表图案**随"共用一张图案纹理"一起让位给统一的中性 1px 点；
   规格 §2.2 标注图案为"非必需但强烈建议"，§6 的可访问性由**精确的 Base 明度阶梯**
   独立承载，ΔY 最小 33 仍达标。
4. **有限地图到不了地平线。** `horizonRatio=0.28` 时天空带在 `y≈202`，
   而 24 行地图的远边在更下方，两者之间会露出一条 `kGroundColor` 的深色兜底带。
   这是有限地图 + 透视的必然结果，不是瓦片漏画；想填满请加大 `kRows`。
5. **`SetTiles` / `SetObjects` 是只读视图**：库不拷数组。两个 `std::vector` 存在
   `SandboxScene` 的成员里，活到场地析构；改内容必须重新 `Set*`，否则画面停在上一份。
6. **不做交互**：`ClickAt` / `SetOnFieldClick` 未接线，树的 `hitEnabled=false`。
   水面那一格的 alpha 是 `0x99` 而不是 `0`（alpha 0 会被 `HitObject` 判成不可见），
   这样以后接交互不会踩雷（规格 §3.1）。
7. **`kTileOutlineSoft` 已启用、`kWaterGlint` 未用。** 前者是外轮廓 1px 的半透明
   中性黑（依赖 `SDL_BLENDMODE_BLEND`，见 §7-B）；后者是规格标注的可选增强，
   水面波纹改用 relief 里的两条灰阶亮线（`kReliefGlint`）实现。
   `kTileOutline`（实色）保留为"BLEND 不可用"时的回退常量。
8. **运行期会写文件**：`assets/sandbox_atlas.png`（每次启动覆盖）与
   `sandbox_probe.png`（仅 `--probe`）。库没有"从内存像素直接建纹理句柄"的公开入口，
   `InkingLoadTexture` 只认资源根下的相对路径，所以走了"生成像素 → 存 PNG → Load"
   这一条；编码只在初始化做一次，之后每帧仍走库的正常提交链。
9. **Windows 中文路径**：`InkingAssetRoot()` 返回 UTF-8，SDL3 在 Windows 上按
   UTF-8 收路径，本示例路径全是 ASCII；若把整个工程搬到中文目录，`std::filesystem::path`
   从窄串构造那一处需要换成 `u8path` / `char8_t` 路径。

---

## 7. 与美术规格的对齐结论

- **§7-A（P0）**：最终 `Mode::Oblique`（skew 26.57° > 0）+ `depthScaleFar=0.72`（h>0）
  + `horizonRatio=0.28`，天空带真实存在，`SetSkyColor(kSkyColor)` 生效；
  **没有**走 `Grid` + `SetBackdrop` 兜底，画面顶部不是黑的。
- **§7-B（P0）**：库在建 pin 纹理时已显式 `SDL_SetTextureBlendMode(..., SDL_BLENDMODE_BLEND)`
  （`src/render/InkingText.cpp:739`）——**BLEND 已开启**，无需示例再开。
  因此实际走的是：`kWaterSurface`（alpha `0x99`）半透明混合 + `kTileOutlineSoft`
  半透明叠压描边。**回退路径**（若 BLEND 不可用）：描边换 `kTileOutline` 实色、
  水面 tint 的 alpha 提到 `0xFF`（退回不透明）。
- **§7-C（P0）✓ 已落地**：一张共用的白 / 灰阶 relief（同一个 `src`）+ 四个
  `InkingFieldTile::tint = kXxxBase`，纹理里**没有任何地表色**，地表色只由 tint 给。
  乘性调制带来的两处不可达见已知限制 #3。
- **§7-D/E/F/G/H/I**：一次性生成 RGBA 缓冲后上传句柄、每帧走 `SetTiles`/`SetObjects`
  （D ✓）、水面放地形面瓦片而非物体（E ✓）、树与水面都给了有效纹理句柄（F ✓）、
  完全不用 flip（G ✓）、显式 `SetSkyColor`/`SetGroundColor` 覆盖库默认黑色（H ✓）、
  常量集中在 `SandboxPalette.h` 且 `kPascalCase` + 8 位 `0xAARRGGBB`（I ✓）。
- **§7-J（P2）**：`SandboxPalette.h` 三条 `static_assert` 守住
  `Y(沙)-Y(草)=55 / Y(草)-Y(石)=35 / Y(石)-Y(水)=33`，全部 ≥30；relief 顶面纯白
  使渲染出的顶面颜色**逐位等于** `kXxxBase`，阶梯没有被调制削弱。

### 7.1 补充中转要求 · 四问四答

| 问题 | 答复 |
| - | - |
| 最终投影模式？ | `InkingFieldMode::Oblique`，skew=26.57°、zoom=1.30、格 64×32、cols/rows=14/24、depthScaleFar=0.72、horizonRatio=0.28（`farD` 由库反解）、camera.x=448。Grid 被排除（锁 skew=0 出不来斜格）。 |
| 天空色走 `SetSkyColor` 还是 `SetBackdrop`？ | **`SetSkyColor(kSkyColor)`**。因为 depthScaleFar<1 且 horizonRatio≥0 ⇒ h>0 ⇒ 天空带高 >0，纯色天空真的画出来；`SetBackdrop` 完全没调用。 |
| 水面做成瓦片还是物体？ | **地形面瓦片** `InkingFieldTile`（格 10,16），`src` 指向白 relief，`tint=kWaterSurface`（alpha 0x99）。不是 `InkingFieldObject`——物体是正矩形不贴合格子。 |
| 混合模式是否开启？ | **是**，`SDL_BLENDMODE_BLEND` 由库在 `ResolveTextureHandle` 建纹理时设置；半透明水面与半透明描边都靠它，截图已证实生效。 |

规格 §8 常量表已逐条落地：`kSkyColor` / `kGroundColor` / 四组
`kXxxBase·Lit·Shade` / `kTileOutline` / `kTileOutlineSoft` / 树 5 个 /
`kWaterSurface` / `kWaterGlint` —— 名字与 8 位 `0xAARRGGBB` 数值与
`docs/art-palette.md` §8 完全一致（另有 6 个工程侧中性量 `kOpaque` /
`kReliefWhite` / `kReliefShade` / `kReliefPattern` / `kReliefGlint` / `kTransparent`）。

---

## 8. 验证记录（2026-11-12 实测，非预期值）

环境：Windows / MSYS2 UCRT64 / GCC C++20 / SDL 3.4.16 / Ninja Release，
工作目录 `D:/InkingUIKit`。以下均为**实际执行并捕获的输出末几行**。

**① 单文件语法检查**

```bash
/c/msys64/ucrt64/bin/g++ -std=c++20 -fsyntax-only \
  -DINK_ISLOG=1 -DINK_ISDEBUG=1 -Iinclude examples/sandbox/main.cpp
```

无任何输出，退出码 `0`。

**② 全量构建**（`examples/CMakeLists.txt` 变更后 Ninja 自动重跑 configure）

```bash
cmake --build build
```

```
[0/2] Re-checking globbed directories...
[1/2] Re-running CMake...
-- Inking：视频已开启（FFmpeg libavcodec 63.1.102）
-- Configuring done (2.3s)
-- Generating done (0.5s)
-- Build files have been written to: D:/InkingUIKit/build
[0/4] Re-checking globbed directories...
[1/19] 拷贝示例资源（assets/）到 bin/assets
[2/19] Building CXX object examples/sandbox/CMakeFiles/inking_sandbox.dir/main.cpp.obj
...
[13/19] Linking CXX executable bin\inking_sandbox.exe
[16/19] Linking CXX executable bin\inking_sharp.exe; 拷贝验收素材（assets/）到 D:/InkingUIKit/build/bin
EXIT=0
```

产物：`build/bin/inking_sandbox.exe`。调参后重编（增量）输出末几行：

```
[2/4] Building CXX object examples/sandbox/CMakeFiles/inking_sandbox.dir/main.cpp.obj
[3/4] Linking CXX executable bin\inking_sandbox.exe
BUILD_EXIT=0
```

**③ 离屏自检**

```bash
cd build/bin && ./inking_sandbox.exe --probe
```

```
[sandbox] Inking 1.5.0-preview / SDL3 3.4.16
[sandbox] 投影校验 = 通过；地图 14x24，skew 26.57°，zoom 1.30
   projectionValid      = 1
   transformBlocked     = 0
   tileCount            = 336
   objectCount          = 1
   objectsDrawn         = 1
   visibleCells         = 336
   terrainBatches       = 1
   objectBatches        = 1
   truncatedTiles       = 0
   rejectedBatchesTotal = 0
   culledSubPixel       = 0
   结论                 = 活的
   截图                 = sandbox_probe.png
PROBE_EXIT=0
```

生成 `build/bin/assets/sandbox_atlas.png`（926 B，白/灰阶 relief 版）与
`build/bin/sandbox_probe.png`（20307 B）。截图已肉眼确认：四分区、透明背景的树、
水分区里那一块更亮的半透明水面、顶部天空带、深色地面兜底带、1px 深色描边都在；
图集另经目视确认左格是纯白/灰阶 relief（**没有**绿/黄/蓝/灰的预染地表色）。

> 补充中转项 C 落地后**重新跑过上面 ①–⑤**：`SYNTAX=OK`、
> `BUILD_EXIT=0`、`PROBE_EXIT=0`、GetStats() 十一个字段与下表逐项相同（336/1/1/336/1/1/0/0）、
> `ctest` 仍 10/10。`GetStats()` 不因为"四个分区改共用一个 src"而变化，这一点本身
> 也验证了地形批是按纹理句柄分段、与 `tint` 无关。

**④ 窗口主循环启动**（无显示器环境用 dummy 驱动 + 6 秒上限）

```bash
cd build/bin && SDL_VIDEO_DRIVER=dummy timeout 6 ./inking_sandbox.exe
```

```
[sandbox] Inking 1.5.0-preview / SDL3 3.4.16
[sandbox] 投影校验 = 通过；地图 14x24，skew 26.57°，zoom 1.30
[sandbox] 开窗口：关窗或 ESC 退出；--probe 可离屏打印 GetStats()。
RUN_EXIT=124   # 6 秒到点仍在循环里 = 主循环确实进入，没有启动即崩
```

**⑤ 回归（不破坏既有测试）**

```bash
ctest --test-dir build --output-on-failure
```

```
 1/10 Test  #1: inkgen_validate_cxxcss ...........   Passed    0.10 sec
 6/10 Test  #6: strict_warnings ..................   Passed    2.06 sec
 7/10 Test  #7: selftest .........................   Passed    5.63 sec
 8/10 Test  #8: selftest_windows .................   Passed    3.30 sec
10/10 Test #10: probe_drag .......................   Passed    0.46 sec
100% tests passed out of 10
Total Test time (real) =  12.35 sec
CTEST_EXIT=0
```

**未跑通的项**：无。真窗口的**人工目视验收**（在有显示器的桌面上双击 exe）本次环境
不具备，已用 `--probe` 的 dummy 渲染 + 像素截图替代，见 ④ / ③。

