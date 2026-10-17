# InkingFrontendDeveloper

**一个用 SDF 形状 + 锚点定位写 UI 的 C++20 前端框架（动态链接库）。**

> ## ⚠ 当前状态：**测试版本**（`0.1.0-test`）
>
> - 接口 / ABI / 行为在 `1.0.0` 之前**不承诺稳定**，升级请对照
>   [`docs/zh/CHANGELOG.md`](docs/zh/CHANGELOG.md)。
> - 只在 **Windows + MSYS2 UCRT64 + Ninja** 上构建过；Linux / macOS 未验证。
> - **请勿使用现版本进行生产。** 正式 `1.0.0` 预计 2026 年 11 月底前推出。

---

## 这是什么

`InkingFrontendDeveloper` 把 UI 拆成四条互不越界的层：

| 层 | 回答的问题 | 关键类型 |
| --- | --- | --- |
| **定位** | 我在哪儿 | `InkingAnchor`（锚点公式 + 父链 + 可见性 + 倍率） |
| **数据** | 我长什么样 | `AnchorData` / `ButtonData` / `PanelData` / `ShapeSpec` / `LayoutSpec` |
| **树** | 我在哪棵树里、谁点得到我 | `InkingWidget` + 静态 / 动态两档 |
| **档位** | 几何会不会自己变 | `InkingStaticWidget` / `InkingDynamicWidget` |
| **输入契约** | 我收不收指针 / 键盘 | `InkingPointerHandler` / `InkingKeyboardTarget` |

四条设计原则（读代码前先知道）：

1. **位置不存，由锚点推导**。窗口左上角是 (0,0)，向右 x 增大、向下 y 增大；
   自身锚点与上级锚点都是 0~1 的百分比。设计坐标严格等比缩放到窗口（letterbox）。
2. **形状同时决定渲染 / 命中 / AABB**。三者从一份 SDF 定义派生，
   所以不会出现"画的是圆角、点到的是直角"。
3. **写入口不可重写，钩子不标脏**。`SetVisible` / `Resize` 是 `[final]`；
   `onSizeChanged` / `onTick` 是 `[√]`。名字上就能分开。
4. **标脏只有两档**：命中脏（几何 / 可见性 / 层级 / 父子）与重绘脏（颜色 / 文字 / 变换）。
   动画改颜色**绝不能**标命中脏，否则每帧白重建一次命中表。

---

## 快速开始

### 1. 依赖

Windows + MSYS2（UCRT64 环境）：

```bash
pacman -S --needed \
    mingw-w64-ucrt-x86_64-{cmake,ninja,gcc,pkgconf} \
    mingw-w64-ucrt-x86_64-{sdl3,sdl3-image,sdl3-ttf,sdl3-mixer,ffmpeg}
```

| 依赖 | 用途 | 查找方式 |
| --- | --- | --- |
| CMake ≥ 3.20 | 构建 | — |
| GCC ≥ 13（C++20）或 MSVC | 编译器 | — |
| Ninja | 生成器（可选但推荐） | — |
| SDL3 / SDL3_image / SDL3_ttf / SDL3_mixer | 窗口、纹理、文字、音频 | CMake config（`SDL3::SDL3` 一族） |
| FFmpeg（libavcodec / avformat / avutil / swresample / swscale） | 视频与音频解码 | `pkg-config` |

### 2. 构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 3. 跑起来

```bash
./build/bin/inking_demo.exe            # 真窗口演示（ESC 退出，F11 全屏）
./build/bin/inking_demo_selftest.exe   # 无头自检：dummy 视频驱动 + 软件渲染器
```

自检的输出末行是结论：

```
== 失败项：0 ==
```

**看这一行，不要只看退出码。** 当前 `0.1.0-test` 实测 **73 项通过 / 0 项失败**
（退出码也等于失败项数，可以直接用）。

```bash
ctest --test-dir build --output-on-failure   # 若有 CXXCSS 配置，还会跑生成链路的编译自检
```

### 4. 构建选项

| 选项 | 默认 | 作用 |
| --- | --- | --- |
| `ISMESSAGE` | `ON` | 消息队列参与编译（宏 `INK_ISMESSAGE`）；关掉时退化成空实现 |
| `INK_BUILD_EXAMPLES` | 顶层构建时 `ON` | 构建 `examples/demo`（演示 + 自检） |
| `INK_BUILD_TESTS` | 顶层构建时 `ON` | 注册 ctest（含 `inkgen --validate` 与生成链路编译自检） |
| `CMAKE_BUILD_TYPE` | `Release` | — |

`INK_ISLOG` / `INK_ISDEBUG` 恒为 `PUBLIC` 定义（会改公开头里 inline 函数的内容，
消费者必须与库看到同一个值，否则是 ODR 违例）。

---

## 产物

| 文件 | 说明 |
| --- | --- |
| `build/bin/libinking.dll` | **动态库本体**（Release 约 1.2 MB） |
| `build/lib/libinking.dll.a` | 导入库（链接用） |
| `build/bin/inkgen.exe` | `CXXCSS/*.json` → C++ 的构建期生成器（只依赖标准库） |
| `build/bin/inking_demo.exe` | 演示程序 |
| `build/bin/inking_demo_selftest.exe` | 无头自检 |
| `build/inkgen/all/button_service.h` | 生成结果（看得见就能审） |

导出方式：`WINDOWS_EXPORT_ALL_SYMBOLS`（CMake 生成 `.def`），当前导出 **1943** 个符号。
**不**逐类标 `__declspec(dllexport)`，也**不**加 `-fvisibility=hidden`——两套机制混用是
自相矛盾的（MinGW 上会让 `.def` 收不到符号）。

### 发布包（`dist/`）

最小的可分发包（**测试版本，不要用于生产**）：

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --clean-first -j
# 组装：见下表的目录约定；包根放 BUILD.txt 记录工具链 / 校验和 / 验证结果
```

```
inking-0.1.0-test-win64/
├─ BUILD.txt            版本阶段、工具链、依赖版本、SHA-256、验证记录、已知限制、许可
├─ README.md            本文件
├─ bin/                 libinking.dll · inking_demo.exe · inking_demo_selftest.exe · inkgen.exe
├─ lib/                 libinking.dll.a（导入库）
├─ include/             82 个公开头（`#include <widget/InkingWidget.h>` 的搜索根）
├─ docs/                API 文档（`API.md` + `api/`）、配置格式、变更记录
└─ examples/CXXCSS/     配置样例（喂给 `inkgen.exe`）
```

包内**不含** SDL3 / SDL3_ttf / SDL3_image / SDL3_mixer / FFmpeg 的 DLL——它们是运行期
依赖，请由使用方自行提供（MSYS2 UCRT64 的 `mingw-w64-ucrt-x86_64-*` 包即可）。
包内 `bin/inking_demo_selftest.exe` 可直接跑，`失败项：0` 即说明这份 DLL 是好的。

### 在自己的工程里用

两种拿法：**源码内嵌**（作子目录）或**预编译包**（解压即 SDK）。后者是发布用的主路径，下面详述。

```cmake
# 源码内嵌（最省事，拿得到 inking::inking 别名）
add_subdirectory(third_party/InkingUIKit)
target_link_libraries(my_app PRIVATE inking::inking)
```

---

## 用预编译包（不编译源码）

把 `inking-0.1.0-test-win64.zip` 解到任意目录，那个目录就是 **SDK 根**（下文写 `<SDK>`）：

```
<SDK>/
├─ bin/                 libinking.dll · inkgen.exe
├─ include/             82 个公开头（#include <widget/InkingWidget.h> 的搜索根）
├─ lib/                 libinking.dll.a · cmake/inking/（find_package 用）
├─ docs/                API 文档
├─ examples/CXXCSS/     配置样例（17 个 json + CXXCSS.md）
└─ BUILD.txt · README.md · LICENSE
```

前提：装好 MSYS2 UCRT64 的

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{cmake,ninja,gcc,pkgconf} \
    mingw-w64-ucrt-x86_64-{sdl3,sdl3-image,sdl3-ttf,sdl3-mixer,ffmpeg}
```

> ⚠ **不能用 MSVC 链接这个包**：它是 MinGW(GCC) 编译的，`.dll.a` 导入库与 MSVC 的
> `.lib` 互不兼容，C++ ABI 也不同。在 Visual Studio 里请用"打开文件夹"（CMake 模式）
> 并选 MinGW 工具链，别用 `.vcxproj`。

### 路线 A：手写组件（最快上手）

工程只要三个文件。

**`CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApp LANGUAGES CXX)

find_package(inking CONFIG REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE inking::inking)

# 把 libinking.dll 拷到 exe 旁边 —— 不拷会静默退出（0xC0000135）
add_custom_command(TARGET MyApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:inking::inking>" "$<TARGET_FILE_DIR:MyApp>")
```

**`CMakePresets.json`**（把 `<SDK>` 换成你的解压路径，斜杠用 `/`）

```json
{
  "version": 3,
  "configurePresets": [{
    "name": "mingw",
    "generator": "Ninja",
    "binaryDir": "${sourceDir}/build",
    "cacheVariables": {
      "CMAKE_BUILD_TYPE": "Release",
      "CMAKE_C_COMPILER": "C:/msys64/ucrt64/bin/gcc.exe",
      "CMAKE_CXX_COMPILER": "C:/msys64/ucrt64/bin/g++.exe",
      "CMAKE_MAKE_PROGRAM": "C:/msys64/ucrt64/bin/ninja.exe",
      "CMAKE_PREFIX_PATH": "<SDK>;C:/msys64/ucrt64"
    },
    "environment": { "PATH": "C:/msys64/ucrt64/bin;$penv{PATH}" }
  }],
  "buildPresets": [{ "name": "mingw", "configurePreset": "mingw" }]
}
```

**`main.cpp`**

```cpp
#include <window/InkingWindow.h>
#include <scene/InkingScene.h>
#include <button/InkingStaticButton.h>

class MyScene : public inking::InkingScene {
public:
    MyScene() : inking::InkingScene("main") {   // 名字给 Show() 用
        inking::ButtonData b;
        b.name = "hello";
        b.width = 200;
        b.height = 72;
        b.label = "点我";
        b.normal    = inking::ButtonAppearance(inking::AppearanceType::Color, 0xFF3F7FD6u);
        b.hover     = inking::ButtonAppearance(inking::AppearanceType::Color, 0xFF5A9BF0u);
        b.onclicked = inking::ButtonAppearance(inking::AppearanceType::Color, 0xFF2F63AAu);
        b.hoverInheritsNormal = false;      // 不关这两条，hover/按下会被拉回 normal
        b.onclickedInheritsNormal = false;
        b.selfAnchor = inking::InkingChangeAnchor::Center;
        b.traceAnchor = inking::InkingChangeAnchor::Center;
        b.shape = inking::ShapeSpec::RoundedRect(12.0f);
        AddChild(std::make_unique<inking::InkingStaticButton>(this, b));
    }
};

int main() {
    MyScene scene;
    inking::InkingWindow::Instance().Show("main");   // 同步：跑到关窗为止
}
```

构建运行：

```bash
cmake --preset mingw
cmake --build --preset mingw
./build/MyApp.exe
```

### 路线 B：CXXCSS 配置驱动（外观与逻辑分离）

在路线 A 的基础上加三处。这是 Inking 的核心玩法：**外观写在 json 里，代码里一行挂上去**。

**① 配置放 `CXXCSS/Button/*.json`**（格式见 `<SDK>/examples/CXXCSS/CXXCSS.md`，样例可整套拷过来）

**② `CMakeLists.txt` 追加**

```cmake
include("${INKING_INKGEN_CMAKE}")     # find_package 导出的生成模块

file(GLOB MY_CSS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/Button/*.json")

ink_add_generated(MyApp SOURCES ${MY_CSS})
```

**③ 代码里用生成的类**（构建期产物，落在 `<build>/inkgen/button_service.h`）

```cpp
#include <inkgen/button_service.h>          // 不是手写的文件

class MyScene : public inking::InkingScene {
public:
    MyScene() : inking::InkingScene("main") {
        AddChild(std::make_unique<inking::cxxcss::NormalButton>(this));
    }
};
```

类名 = json 的 `name` 转 PascalCase（`normalButton` → `NormalButton`）。
改外观只改 json、重新构建，**代码一行不动**。

> 生成的头/源带 UTF-8 BOM，Visual Studio / 记事本打开不会乱码；它们只落构建目录，
> 不进源码树。

### 运行期 DLL

`libinking.dll` 由上面 `POST_BUILD` 自动拷到 exe 旁。**SDL3 / FFmpeg 的 DLL 要自己在 PATH 里**：

- 推荐：把 `C:\msys64\ucrt64\bin` 加进系统 PATH（一次性，之后所有程序都能跑）
- 或者：只在调试时临时加

找不到时的表现是**静默退出**（退出码 `0xC0000135`），没有报错窗口 —— 跑不起来先查这个。

### CMake 导出了什么

`find_package(inking CONFIG)` 之后可用：

| 名字 | 内容 |
| --- | --- |
| `inking::inking` | 库目标（传播 include、四个 SDL 目标、FFmpeg、三个编译宏） |
| `INKING_SDK_DIR` | SDK 根目录 |
| `INKING_INKGEN` | `bin/inkgen.exe` 的绝对路径 |
| `INKING_CXXCSS_DIR` | `examples/CXXCSS`（配置样例） |
| `INKING_INKGEN_CMAKE` | 生成模块路径（`include()` 它才有 `ink_add_generated`） |

`inking::inking` 会**传播** include 目录、四个 SDL 目标、FFmpeg 与三个编译宏
（`INK_ISLOG` / `INK_ISDEBUG` / `INK_ISMESSAGE`）——所以消费方不需要自己再配一遍。

```cpp
#include <window/InkingWindow.h>
#include <scene/InkingScene.h>
#include <button/InkingStaticButton.h>
```

---

## 目录结构

```
include/            82 个公开头（按子系统分目录，与 src/ 一一对称）
src/                54 个 .cpp + 3 个内部头
examples/demo/      演示 + 无头自检（两个目标）
tools/inkgen/       CXXCSS → C++ 的生成器
CXXCSS/             UI 配置（Button / Panel / Label / Image / Text / Theme 的 .json）
cmake/              SDL3 查找、inkgen 的构建规则
docs/zh/            API 文档与变更记录
dist/               发布包（`inking-<版本>-win64/` 目录 + 同名 .zip，不入库）
build/              构建产物（不入库）
```

分层与依赖方向（单向，下层不认识上层）：

```
config / dataStruct  →  detail  →  anchor → widget  →  组件  →  scene  →  window  →  prefab
（纯数据/算术）        （工具）    （定位）  （树）    （具体外观） （场景）  （SDL 窗口） （组装）
```

---

## 文档

| 文档 | 内容 |
| --- | --- |
| [`docs/zh/API.md`](docs/zh/API.md) | **从这里读起**：五分钟上手 + 模块地图 + 头文件全量索引 |
| [`docs/zh/api/`](docs/zh/api/) | 16 个专页：Widget / Scene / Window / Input / Render / Components / Prefab / CXXCSS 生成物 / Runtime / LayoutSolver + Anchor / ShapeSpec / Transform / Layout / KeyboardTarget / PointerHandler |
| [`CXXCSS/CXXCSS.md`](CXXCSS/CXXCSS.md) | 配置格式：目录与命名、公共骨架、形状层词汇表、Button 字段全表 |
| [`docs/zh/CHANGELOG.md`](docs/zh/CHANGELOG.md) | 变更记录 |

---

## 已知限制（测试版本）

1. **无显示 / 受限环境里的文字**：某些环境里 `SDL_IOFromFile` 对**所有**路径都失败
   （同路径 `fopen` 正常），于是 `TTF_OpenFont` 打不开字体、文字退化成占位画法。
   自检支持环境变量 `INK_TEST_FONT` 指定可读字体；**"没字体"不是代码缺陷**。
2. **单行输入框**：多行、撤销 / 重做、Tab 焦点遍历、输入法候选窗定位都未做。
3. **`AppearanceType::Svg` 没有画法**：配置里能写，运行期**静默退回纯色 `color`**
   ——不报错、只是看不到图。（`Image` 已落地：`detail::PaintImage` 按形状裁剪后画进按钮。）
4. **未做局部层级（堆叠上下文）**：z 序是全局一条，`Select` 展开靠"临时提到 z=200"。
5. **音频需要显式 `AudioInit()`**：窗口层只在 `Show()` 之后每帧调 `AudioTick`、
   关窗时调 `AudioShutdown`，**不会**替你初始化。不调时整层空转、`AudioReady()` 为 false，
   而且不报错（`AudioInit()` 返回 false 通常意味着构建期漏了 `INK_HAS_SDL3_MIXER`）。
6. **编译警告**：`src/window/InkingWindow.cpp` 里重复 `#define NOMINMAX`
   （C++ 标准库的 `os_defines.h` 已经定义过）。无害，但可以先判 `#ifndef` 再定义。

> **`0.1.0-test` 期间已修掉的两条历史限制**：
> ① `TaskQueue` 退出时 join 管家线程的死锁（以前进程"打印完失败项就不返回"，
> 现在 1 秒退出、退出码 = 失败项数）；
> ② 缺 CMake package config（`find_package(inking CONFIG)` 现在可用）。

---

## 许可

本仓库的**自身代码**按 **MIT** 授权，全文见 [`LICENSE`](LICENSE)。

> **第三方依赖的许可要一并遵守**（分发二进制时尤其要注意）：
>
> | 依赖 | 许可 | 对你的要求 |
> | --- | --- | --- |
> | SDL3 / SDL3_image / SDL3_ttf / SDL3_mixer | zlib | 保留版权声明即可 |
> | FFmpeg | **LGPL-2.1+**（MSYS2 包默认是动态链接的 LGPL 版） | **必须动态链接**，并随分发提供其许可与源码获取方式 |
>
> 若改用**静态链接**或 GPL 配置的 FFmpeg，整个作品的许可约束会跟着变：LGPL 静态
> 链接要求提供可重链接的目标文件，GPL 版则要求衍生作品也采用 GPL。MIT 只覆盖
> 本仓库自己的代码，不改变这些依赖的许可。

---

> 版本 `0.1.0-test`（测试版本） · 最后更新 2026-10-17
