# 第三方组件与许可（THIRD-PARTY NOTICES）

本包 `bin/` 下除 `libinking.dll` / `inkgen.exe` / 两个示例程序外，还带有
**运行期 DLL 闭包**，使示例在未安装 MSYS2 的机器上也能直接运行。
这些 DLL 的版权归各自作者，许可见 `licenses/<组件>/`。

## 随包分发的运行期组件

| 组件 | 版本（MSYS2 UCRT64） | 许可 | 本包内文件 |
| --- | --- | --- | --- |
| SDL3 | 3.4.16 | zlib | `SDL3.dll` |
| SDL3_image | 3.4.6 | zlib | `SDL3_image.dll` |
| SDL3_ttf | 3.2.2 | zlib | `SDL3_ttf.dll` |
| SDL3_mixer | 3.2.4 | zlib | `SDL3_mixer.dll` |
| FreeType | 2.14.3 | FTL（FreeType License） | `libfreetype-6.dll` |
| HarfBuzz | 14.5.0 | MIT | `libharfbuzz-0.dll` |
| GLib | 2.90.0 | LGPL-2.1-or-later | `libglib-2.0-0.dll` |
| PCRE2 | 10.48 | BSD-3-Clause | `libpcre2-8-0.dll` |
| Brotli | 1.2.0 | MIT | `libbrotlidec.dll` / `libbrotlicommon.dll` |
| bzip2 | 1.0.8 | bzip2-1.0.6（BSD 型） | `libbz2-1.dll` |
| zlib | 1.3.2 | zlib | `zlib1.dll` |
| libpng | 1.6.58 | libpng-2.0 | `libpng16-16.dll` |
| libiconv | 1.19 | LGPL-2.1-or-later | `libiconv-2.dll` |
| gettext-runtime | 0.2x（MSYS2） | LGPL-2.1-or-later | `libintl-8.dll` |
| Graphite2 | 1.3.15 | LGPL-2.1 / MPL-2.0 | `libgraphite2.dll` |
| GCC 运行库 | 16.2.0 | GPL-3.0 + GCC Runtime Library Exception | `libstdc++-6.dll` / `libgcc_s_seh-1.dll` / `libwinpthread-1.dll` |

> LGPL 组件的重链接权利：以上 LGPL 库均为**动态链接**（独立 DLL），
> 替换 DLL 即可换用修改版。源码与构建脚本见 MSYS2 包仓库
> <https://packages.msys2.org/>（搜索上表包名），或各项目上游站点。

## 运行期依赖（随包分发）

| 组件 | 用途 | 说明 |
| --- | --- | --- |
| FFmpeg（libavcodec / avformat / avutil / swresample / swscale） | 视频解码 | 0.9.0 起**默认开启**：CMake 探测到开发包就定义 `INK_HAS_FFMPEG` 并私有链接，`bin/` 内带齐 `av*.dll`（含 `dav1d` / `x264` 等传递依赖，以 `ldd` 实得为准）。**动态链接**：符合 LGPL-2.1+ 的要求，替换 DLL 即可换构建。探测不到时不链接、`bin/` 内无 FFmpeg DLL、二进制不引用其符号。 |
| CMake / Ninja / GCC | 构建 | 不随包分发。 |

### FFmpeg 的传递闭包（1.0.0-preview 起共 101 个 DLL）

上表只列**直接依赖**。`bin/` 里实际是 `ldd` 的传递闭包——FFmpeg 又链了
x264 / x265 / dav1d / gnutls / pango / cairo / librsvg …，少一个的表现是
**静默退出**（0xC0000135），所以整包收齐。逐条清单见包根 `BUILD.txt`，
`licenses/` 下按 MSYS2 包名分目录。

**口径提醒**：MSYS2 的 FFmpeg 是 **GPL 构建**，闭包里因此含 GPL 组件
（`libx264-165` / `libx265-217` 等）。那些 DLL **不适用本仓库的 MIT**：

- LGPL 组件（GLib / libiconv / gettext / Graphite2 / FFmpeg 本体按 LGPL 口径使用）
  均为动态链接，替换 DLL 即可换构建 —— 满足 LGPL 2.1 §6 的"可替换"要求。
- GPL 组件按 GPL 分发：再分发时需提供**对应源码**的获取方式。源码与构建脚本见
  MSYS2 包仓库 <https://packages.msys2.org/>，或上游
  <https://ffmpeg.org/legal.html> / <https://www.videolan.org/developers/x264.html>。
- **本次（1.0.0-preview）的已知缺口**：这台机器的 MSYS2 没有把 ffmpeg / x264 /
  x265 的许可正文装进 `share/licenses`，所以 `licenses/` 下**缺这三份 GPL 文本**，
  只有上表 16 个包的正本。内部评估无碍，**对外分发前要补齐或改发 LGPL 构建的
  FFmpeg**。

## 本仓库自身

`libinking.dll`、`inkgen.exe`、`include/`、`docs/`、示例代码：**MIT**，
全文见包根 `LICENSE`。

## 再分发时请注意

1. 保留本文件与 `licenses/` 下全部版权/许可文本。
2. 若你**替换**或**新增**了 DLL，需要同步更新本节清单与许可文件。
3. 若改为静态链接 FFmpeg（LGPL），须提供可重链接的目标文件；
   GPL 配置的 FFmpeg 会让衍生作品同样受 GPL 约束。
