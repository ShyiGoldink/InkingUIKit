# 最简单窗口教程

本项目是一个用 SDF 形状 + 锚点定位写 UI 的 C++20 前端框架，以**预编译动态库**的形式提供。

把我们的项目导入到第三方库之中。作为特色，强烈建议你使用我们的 CXXCSS；考虑到你可能不需要，
所以接下来的教程会**两个方向并行**：

- **CXXCSS 路线**：外观写在 json 里，代码生成器把它烘成 C++ 类，代码里一行挂上去；
- **API 路线**：不用配置文件，直接构造组件、直接传数据。

两条路线可以混用，只有一处例外（见 §1 末尾）。

---

## 1. 确定设计大小

本项目为了保证所见即所得，使用的是 **LetterBox** 式的设计：UI 全部写在一个固定的**设计画布**
坐标系里，窗口怎么拉都不改设计坐标，只改展示倍率，多出来的空间留黑边。

如果不做调整，默认设计大小是 **1280 × 720**。

### 1.1 使用 CXXCSS

**第一步：创建 `CXXCSS` 文件夹。**

你可以直接把 release 包里的 `CXXCSS` 文件夹复制到项目根目录，也可以自己建一个同名文件夹。
文件夹里有很多内容，我们暂时先不管。

**第二步：在 `CXXCSS` 下创建 `config.json`**，它决定设计大小。完整内容如下（`$comment` 只是给人看的）：

```json
{
  "$comment": "设计画布 = letterbox 的逻辑坐标系",
  "design": {
    "width": 1600,
    "height": 900
  }
}
```

`width` / `height` 必须是 **1..16384 的整数**——设计坐标按整像素对位，写 `1280.5` 会在构建期
报错（"设计坐标按整像素对位"）。

**第三步：在 `main()` 的第一行把它交给运行期。**

```cpp
#include <inkgen/button_service.h>   // 生成的头文件

int main() {
    if (!inking::cxxcss::ApplyConfig()) {
        return 1;                    // 有字段没生效，日志里写着是哪一项
    }
    // …… 建场景、开窗口
}
```

> `ApplyConfig()` 生成出来，文件本身**不会自动生效**。漏掉这一行的现象是"配置改对了、窗口
> 纹丝不动"——设计尺寸是**运行期**值，预编译好的 DLL 里的编译期常量改不到它。

### 1.2 使用 API

在 `main()` 的第一行直接调用 `SetDesignSize(宽, 高)`：

```cpp
#include <config/window_config.h>
#include <window/InkingWindow.h>

int main() {
    if (!inking::SetDesignSize(1600, 900)) {
        // 没生效：参数不合法（允许 1..16384），
        // 或者场景 / 窗口已经按旧尺寸构造过了
        return 1;
    }

    MyScene scene;
    inking::InkingWindow::Instance().Show("main");
}
```

**这个调用必须早于任何场景 / 窗口的构造。** 设计尺寸在那一刻被写进根锚点、letterbox 的逻辑
坐标系和缩放档位；过了这个点就改不动了——它会返回 `false` 并打一条错误日志，不会出现
"一半旧尺寸、一半新尺寸"的半生效状态。

不调用就是 1280×720（等价于 `SetDesignSize(1280, 720)`）。读回当前值用
`inking::DesignWidth()` / `inking::DesignHeight()`；**不要**用 `inking::kDesignWidth`，
那是默认值，不随 `SetDesignSize` 变化。

> ⚠️ 两条路线不要混着设：用了 CXXCSS，`ApplyConfig()` 已经替你调过 `SetDesignSize`，
> 再手写一个不同的值会被拒绝。幂等设同一个值才会返回 `true`。

---

## 2. 新建您的第一个场景

创建一个头文件来放场景的声明：

```cpp
// include/MainScene.h
#pragma once
#include <scene/InkingScene.h>

class MainScene : public inking::InkingScene {
public:
    MainScene();
};
```

**必须实现构造函数**，场景才会被登入到场景管理器里（"构造即登记"）：

```cpp
// src/MainScene.cpp
#include "MainScene.h"

MainScene::MainScene() : inking::InkingScene("main") {}
```

`"main"` 就是场景名，后面 `Show("main")` 按这个名字找它。

现在一个空场景就建好了。此时场景里还没有任何内容，所以接下来——

> 两个小提醒：公开头文件**都带一级目录**（`<scene/InkingScene.h>`，不是 `<InkingScene.h>`）；
> 场景名不能重复，同名时后建的那个会被驳回（`IsRegistered()` 返回 `false`）。

---

## 3. 新建您的第一个 UI 组件

以最简单的 **Image（图片组件）** 为例，两条路线各来一遍。

### 3.1 使用 CXXCSS

确保 `CXXCSS` 下有一个 `Image` 文件夹，在里面新建 `nina.json`：

```json
{
    "$comment": ["1024×1024 的立绘，居中显示，圆角 12",
                 "type=img 表示一般图片，source 填图片文件路径"],
    "name": "nina",
    "width": 1024,
    "height": 1024,
    "normal": { "type": "img", "source": "D:\\art\\nina.png", "fit": "cover" },
    "selfAnchorX": 0.5, "selfAnchorY": 0.5,
    "traceAnchorX": 0.5, "traceAnchorY": 0.5,
    "offsetX": 0, "offsetY": 0,
    "zIndex": 1,
    "visible": true,
    "shape": { "type": "roundedRect", "radius": 12 }
}
```

> `source` 请换成**你自己机器上那张图**的路径（上面是占位的 `D:\art\nina.png`）。
> 路径里要用两个反斜杠 `\\`（JSON 转义）。没写 `"runtime": true` 时，生成器会在
> **构建期**把这个文件读进来、把字节烘进 exe——所以文件不存在会在构建期就报错，
> 而不是等运行期画出一片空白。

| 字段 | 含义 |
| --- | --- |
| `name` | 组件名，**必须与文件名一致**（`nina.json` → `"nina"`） |
| `width` / `height` | 设计坐标下的尺寸 |
| `normal.source` | 图片文件路径。没写 `"runtime": true` 时，生成器会把**字节烘进 exe** |
| `normal.fit` | `fill`（拉伸）/ `contain`（完整放进框）/ `cover`（铺满并裁掉超出） |
| `selfAnchorX/Y` | 自己的哪个点（0~1 百分比）——详见进阶教程的锚点一节 |
| `traceAnchorX/Y` | 对上父级的哪个点。两者都是 0.5 = 居中 |
| `offsetX/Y` | 在锚点对齐之后再位移多少 |
| `shape` | 形状，渲染 / 命中 / 包围盒三者都从它派生 |

> ⚠️ **json 里不能写 `//` 注释**（解析器的错误是"对象的键必须是字符串"，容易误导）。
> 要写说明请用 `$comment` 字段。

配置好了，代码里挂上去。**推荐用成员变量**：

```cpp
// include/MainScene.h
#pragma once
#include <inkgen/button_service.h>   // 生成类 Nina 的声明
#include <scene/InkingScene.h>

class MainScene : public inking::InkingScene {
public:
    MainScene();

private:
    inking::cxxcss::Nina _nina;
};
```

```cpp
// src/MainScene.cpp
#include "MainScene.h"

MainScene::MainScene() : inking::InkingScene("main"), _nina(this) {}
```

三个要点：

- json 里 `name` 写**小写**且与文件名一致；生成器把首字母大写得到类名 `Nina`。自己把 json 的
  `name` 改成大写会报"name 与文件名不一致"。驼峰要你自己写对（`ninaFace` → `NinaFace`）。
- 生成的类**没有默认构造**，参数就是父级，所以**必须写在初始化列表里**（写在函数体里不行）。
- 传 `this` 就够了——组件构造时会沿父链找到场景并登记进绘制列表，**不需要**再 `AddChild`。
  成员变量方案还很安全：析构顺序是「派生析构体 → 成员 → 基类」，`_nina` 销毁时场景还活着，
  能正常把自己从绘制列表里摘掉。

如果你更习惯"显式挂载"，也有第二种写法：

```cpp
// include/MainScene.h
class MainScene : public inking::InkingScene {
public:
    MainScene();

private:
    inking::cxxcss::Nina* _nina = nullptr;   // 裸指针：不拥有
};

// src/MainScene.cpp
#include "MainScene.h"
#include <memory>

MainScene::MainScene() : inking::InkingScene("main") {
    _nina = static_cast<inking::cxxcss::Nina*>(
        AddChild(std::make_unique<inking::cxxcss::Nina>(this)));
}
```

`AddChild(std::unique_ptr<T>)` 的语义：**所有权转移给父级**，返回裸指针只是方便你接着配字段。

- 不要对一个栈上 / 成员对象用 `AddChild(std::unique_ptr<T>(&obj))`——场景会 `delete` 它；
- `_nina` 是裸指针，不要自己 `delete`，也不要再交给第二个 `unique_ptr`；
- 运行期加节点 / 换父都支持，下一帧生效；但**不要**在 `onRender` 里加（提交期改结构）。

> 两种写法不混用：成员变量路线**不要**再 `AddChild` 一个新对象，否则就是两个组件叠在一起。

### 3.2 使用 API

不用 CXXCSS 时，图片数据要自己造。头文件：

```cpp
// include/MainScene.h
#pragma once

#include <image/ImageData.h>
#include <image/InkingStaticImage.h>
#include <scene/InkingScene.h>

class MainScene : public inking::InkingScene {
public:
    MainScene();

private:
    inking::InkingStaticImage _nina;   // 借用型成员：生命周期 = 场景
};
```

实现文件：

```cpp
// src/MainScene.cpp
#include "MainScene.h"

namespace {

// 成员初始化列表里写不了多行语句，所以数据用自由函数造。
inking::ImageData MakeNinaData() {
    inking::ImageData data;
    data.name = "nina";
    data.width = 1024;
    data.height = 1024;

    // 手写路线没有"生成期烘字节"这一步，所以必须走运行期读图。
    data.runtimeAsset = true;
    data.normal.type = inking::AppearanceType::Image;
    data.normal.source = "nina/nina.png";   // 相对 exe 旁的 assets/
    data.normal.imageFit = inking::ImageFit::Cover;

    data.selfAnchor = inking::InkingChangeAnchor::Center;
    data.traceAnchor = inking::InkingChangeAnchor::Center;
    data.shape = inking::ShapeSpec::RoundedRect(12.0f);
    return data;
}

}  // namespace

MainScene::MainScene() : inking::InkingScene("main"), _nina(this, MakeNinaData()) {}
```

图片要放在 **exe 所在目录下的 `assets/`**（资源根可以改，默认就是这里）：

```
MyWindow/
  assets/
    nina/nina.png
```

> ⚠️ 这里最坑的一条：`runtimeAsset = false` 是"字节已经在配置里了"，而手写路线没有字节，
> 结果是**一块纯色圆角矩形**，而且**连日志都不打**。手写路线必须 `runtimeAsset = true` +
> 相对路径；只有 `true` 且读图失败时才有一条 ERROR 日志。

---

## 4. 组织您的 CMake 文件

先把目录摆成这样（目录名随您，路径要和 `CMakeLists.txt` 对上）：

```
MyWindow/
  CMakeLists.txt
  main.cpp                  ← 也可以挪进 src/
  include/                  ← 自己写的头文件（MainScene.h）
  src/                      ← 自己写的实现（MainScene.cpp）
  CXXCSS/                   ← 只有 CXXCSS 路线需要
    config.json
    Image/nina.json
  assets/                   ← 只有 API 路线需要（运行期读图）
    nina/nina.png
  third_party/InkingUIKit/  ← 解压好的发布包（不动它）
```

`CMakeLists.txt` 全文（逐段注释，可以照抄；**只有 CXXCSS 路线才需要第 ③ 段**）：

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyWindow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ① 找到预编译 SDK：包配置在 third_party/InkingUIKit/lib/cmake/inking
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/InkingUIKit")
find_package(inking CONFIG REQUIRED)

# ② 自己的源码。glob 省得每加一个文件就改一次；
#    CONFIGURE_DEPENDS 让新增 / 删除文件之后自动重新配置。
file(GLOB MY_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    list(APPEND MY_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
endif()

add_executable(MyWindow ${MY_SOURCES})
# 这一行是 #include "MainScene.h" 能成立的前提
target_include_directories(MyWindow PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(MyWindow PRIVATE inking::inking)

# ③ CXXCSS → C++（纯 API 路线整段删掉）
include("${INKING_INKGEN_CMAKE}")
file(GLOB MY_CXXCSS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/*.json"      # 根下的 config.json
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/*/*.json")   # 各组件目录（Image / Label / ...）
ink_add_generated(MyWindow
    SOURCES ${MY_CXXCSS}
    HEADER  "inkgen/button_service.h")               # 代码里 #include <inkgen/button_service.h>

# ④ 运行期要的东西：把库 DLL 拷到 exe 旁边
add_custom_command(TARGET MyWindow POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:inking::inking>" "$<TARGET_FILE_DIR:MyWindow>")

# ⑤ 资源目录也拷过去（只有 API 路线需要；目录不存在时跳过，免得报错）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET MyWindow POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:MyWindow>/assets")
endif()
```

五个容易漏的点：

- 只写 `add_executable(MyWindow main.cpp)` 会漏掉 `src/` 下的实现，编译能过、**链接**报"未定义符号"；
- 不写 `target_include_directories(... include)`，`#include "MainScene.h"` 就找不到文件；
- `ink_add_generated` 的产物落在**构建目录**（`build/inkgen/`），不进源码树；它已经把那个目录加进
  include 路径，所以代码里直接 `#include <inkgen/button_service.h>`；
- 改了 json 不用手动跑生成器：json 是这条自定义命令的依赖，`cmake --build` 会自动重跑；
- 目标名（`MyWindow`）随您，只要和 `add_executable` / `ink_add_generated` 里的一致。

---

## 5. 编译并运行

第一次：配置 + 构建（`-G Ninja` 可选但推荐；机器上没有 Ninja 就删掉这个参数）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

之后只改代码或 json，不需要再配置，直接：

```bash
cmake --build build -j 8
```

跑起来。`libinking.dll` 会被 §4 的第 ④ 段自动拷到 exe 旁边，但 **SDL3 / FFmpeg 的 DLL 不随包提供**，
要自己让它们出现在 `PATH` 里：

Git Bash：

```bash
PATH=/c/msys64/ucrt64/bin:build:$PATH ./build/MyWindow.exe
```

Windows PowerShell：

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;build;$env:PATH"
.\build\MyWindow.exe
```

> ⚠️ 缺 DLL 的现象是**静默退出**：窗口没出来、一行输出都没有，退出码 `0xC0000135`
> （`STATUS_DLL_NOT_FOUND`）。看到这个先查 `PATH`，别怀疑代码。

---

## 常见问题

| 现象 | 原因 |
| --- | --- |
| 编译报 `No such file or directory: InkingScene.h` | include 路径少了层级，应写 `<scene/InkingScene.h>` |
| 链接报 `undefined reference to inking::...` | 没链接 `inking::inking`，或漏了 `src/` 下的 `.cpp` |
| 窗口没出现、无任何输出、退出码 `0xC0000135` | `PATH` 里缺 SDL3 / FFmpeg 的 DLL |
| 场景建了但窗口一片空白 | 场景名与 `Show("...")` 不一致，或同名场景已被占（`IsRegistered()` 为 false） |
| 图片不显示，只有一块纯色圆角矩形 | 手写路线 `runtimeAsset` 写成了 `false`，或文件不在 `assets/` 下 |
| 改了 `config.json` 但窗口大小没变 | `main()` 里没调 `ApplyConfig()`，或调用发生在场景 / 窗口构造之后（日志会报） |
| `nina.json` 构建期报"对象的键必须是字符串" | json 里写了 `//` 注释，改用 `$comment` |
| 编译期找不到 `Nina` 类型 | json 的 `name` 与文件名不一致，或名字不是合法 C++ 标识符（此时不生成类） |
