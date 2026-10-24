# 最小示例（只依赖预编译 SDK）

一个场景、一个标签、一个按钮，用来确认 SDK 装对了。
它**不读源码树**，只通过 `find_package(inking CONFIG)` 链接。

## 构建

```bash
# <SDK> = 本包解压后的目录（含 bin/ include/ lib/）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="<SDK>;C:/msys64/ucrt64"
cmake --build build
```

## 运行

```bash
./build/hello.exe        # 点「退出」或按 ESC 关闭；返回码 0
```

构建脚本会把 `<SDK>/bin/*.dll` 拷到 `build/` 下，所以**不需要**改 PATH。
如果你自己新建工程，记得做同样的事——漏拷 DLL 的表现是**静默退出**
（`0xC0000135`），不会弹任何错误框。

## 前提

- Windows x64
- MSYS2 UCRT64 的 MinGW GCC（本 SDK 是 MinGW ABI，**不能用 MSVC 链接**）
- SDL3 / SDL3_image / SDL3_ttf / SDL3_mixer / FFmpeg 的**开发包**（`find_package`
  会继续找它们；只有运行期不需要它们单独安装——DLL 已在包内）
