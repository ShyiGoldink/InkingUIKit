#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 打"图集帧动画测试"绿色包：dist/atlas-frames-win64/
#
# 与 `package.sh` 的分工：那个打的是 **SDK 包**（头 / 库 / 文档 / 示例），
# 这个只打**一个可双击运行的小程序**——exe + 图集 + 运行期 DLL 闭包 +
# 许可文本，解压即跑，不需要 MSYS2、不需要 PATH。
#
# 用法：
#   tools/package_atlas_frames.sh [图集路径]
#   不给参数就拷 `assets/plant/sunflower_idle/atlas.png`（本机素材）。
#
# 前提：`cmake --build build-release --target inking_atlas_frames` 已经跑过。
# ---------------------------------------------------------------------------
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build-release"
NAME="atlas-frames-win64"
OUT="dist/${NAME}"
ATLAS="${1:-D:/Pvzadventrue/assets/plant/sunflower_idle/atlas.png}"

EXE="${BUILD_DIR}/bin/inking_atlas_frames.exe"
DLL="${BUILD_DIR}/bin/libinking.dll"

if [ ! -f "$EXE" ]; then
    echo "缺 $EXE——先构建：cmake --build ${BUILD_DIR} --target inking_atlas_frames" >&2
    exit 1
fi
if [ ! -f "$ATLAS" ]; then
    echo "找不到图集：$ATLAS" >&2
    exit 1
fi

echo "== 打包 ${NAME} =="
rm -rf "$OUT"
mkdir -p "$OUT"

cp "$EXE" "$OUT/"
cp "$ATLAS" "$OUT/atlas.png"

# ---------------------------------------------------------------------------
# 运行期 DLL 闭包
#
# 只收 `ldd` 的**传递**结果（libinking.dll → SDL3 → libpng / freetype →
# harfbuzz → glib …），而且**文件一律从工具链目录按名字取**：带注入 DLL 的
# 环境里 ldd 解析到的路径可能在别处，按路径过滤会漏收，漏收只在使用方机器上
# 表现为静默退出（0xC0000135）。
# ---------------------------------------------------------------------------
CXX_PATH="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "${BUILD_DIR}/CMakeCache.txt" | head -1)"
if [ -z "$CXX_PATH" ]; then
    echo "读不到 CMAKE_CXX_COMPILER（${BUILD_DIR}/CMakeCache.txt）" >&2
    exit 1
fi
TOOLCHAIN_BIN="$(dirname "$CXX_PATH")"

declare -a RUNTIME_DLLS=()
while IFS= read -r name; do
    [ -n "$name" ] || continue
    if [ -f "${TOOLCHAIN_BIN}/${name}" ]; then
        RUNTIME_DLLS+=("${TOOLCHAIN_BIN}/${name}")
    fi
done < <(
    PATH="${TOOLCHAIN_BIN}:${PATH}" ldd "$EXE" "$DLL" \
        | sed -n 's/^[[:space:]]*\([^ ]*\.dll\) =>.*/\1/p' \
        | sort -u
)
if [ "${#RUNTIME_DLLS[@]}" -eq 0 ]; then
    echo "一个运行期 DLL 都没收到——ldd 没解析出任何依赖" >&2
    exit 1
fi
for dll in "${RUNTIME_DLLS[@]}"; do
    cp -f "$dll" "$OUT/"
done
cp -f "$DLL" "$OUT/"
echo "   收到 ${#RUNTIME_DLLS[@]} 个 DLL（含 libinking.dll 一共 $(( ${#RUNTIME_DLLS[@]} + 1 )) 个）"

# 第三方许可：分发二进制必须带（SDL 是 zlib，FreeType 是 FTL，GLib 系是 LGPL）
LICENSE_SRC="$(dirname "$TOOLCHAIN_BIN")/share/licenses"
if [ -d "$LICENSE_SRC" ]; then
    mkdir -p "$OUT/licenses"
    declare -A LICENSE_PKGS=(
        [SDL3]=SDL3
        [SDL3_image]=SDL3_image
        [SDL3_ttf]=SDL3_ttf
        [FreeType]=freetype
        [HarfBuzz]=harfbuzz
        [GLib]=glib2
        [PCRE2]=pcre2
        [Brotli]=brotli
        [bzip2]=bzip2
        [zlib]=zlib
        [libpng]=libpng
        [libiconv]=libiconv
        [gettext-runtime]=gettext-runtime
        [Graphite2]=graphite2
        [GCC-Runtime]=gcc
    )
    for key in "${!LICENSE_PKGS[@]}"; do
        pkg="${LICENSE_PKGS[$key]}"
        if [ -d "${LICENSE_SRC}/${pkg}" ]; then
            cp -r "${LICENSE_SRC}/${pkg}" "$OUT/licenses/${key}"
        fi
    done
fi
if [ -f tools/THIRD-PARTY-NOTICES.md ]; then
    mkdir -p "$OUT/licenses"
    cp tools/THIRD-PARTY-NOTICES.md "$OUT/licenses/THIRD-PARTY-NOTICES.md"
fi

# 说明：解压后第一眼该看到的东西
cat > "$OUT/README.txt" <<'TXT'
图集帧动画测试 —— sunflower_idle
==============================

这是什么
--------
用 InkingUIKit 的**图集模式**（一张大图 + 帧表 / 网格切法）播放向日葵待机动画。
窗口 512×512、无边框、透明、置顶：桌面上只有向日葵本身，没有标题栏和底板。

素材
----
atlas.png   3072×2560，6 列 × 5 行 = 30 帧，单帧 512×512
播放        15 fps，循环，2.00 秒一圈
对齐        共用画布、脚底固定（所以摆动不会变成画布内跳动）

怎么跑
------
双击 inking_atlas_frames.exe。同目录的 atlas.png 会自动找到。
文件夹里的 DLL 是运行期依赖，别删、别单独移动 exe。

操作
----
Space        暂停 / 继续（暂停后逐帧看，帧序对不对一眼就知道）
← / →        上一帧 / 下一帧（会自动暂停）
R            回到第 0 帧重新播
左键拖动      移动窗口
ESC          退出

命令行（可选）
--------------
inking_atlas_frames.exe [图集路径] [fps]
  inking_atlas_frames.exe D:\other\atlas.png 24

排错开关（可选，正常不用）
--------------------------
INK_ATLAS_DEBUG=1     控制台每 15 帧打一行帧号 / 推进次数 / 纹理数
INK_ATLAS_SHOT=x.png  第 2 帧离屏渲一份 PNG（屏幕抓图对 GPU 合成窗口不可靠）
INK_ATLAS_CAPTURE=x.png  抓窗口后缓冲（真 renderer 画出来的东西）
INK_ATLAS_OPAQUE=1    关掉透明，用来分辨"没画"还是"没合成"
INK_ATLAS_BACKEND=direct3d11  指定渲染后端（名字写错会退回自选）

没有画面怎么办
--------------
1. 程序会在同目录下生成 LOG\<日期>\*.log。搜 "SDL_CreateRenderer 失败"：
   有这一条就说明**渲染器没建起来**——窗口会在、但里面全透明。
2. 没有渲染器程序会直接打印一行提示并退出（不再静默留一个空窗口）。
3. 显式指定后端再试：INK_ATLAS_BACKEND=direct3d11
   （注意 SDL3 的驱动名是 direct3d11，不是 SDL2 时代的 d3d11）

已知边界（不是 bug，是这一版的能力边界）
----------------------------------------
1. 透明区仍然吃点击：窗口是矩形的，向日葵旁边的空白处也属于这个窗口。
2. 半透明边缘会略亮：DWM 按预乘 alpha 解释窗口 alpha，而组件画的是
   straight alpha。全不透明的地方不受影响。
3. 只支持 Windows x64 + D3D11 后端：透明 swapchain 只有这条路上有。

技术要点
--------
· 30 帧共用**一张**纹理（换帧只改源矩形），不是 30 张纹理。
· 帧尺寸与格数在起窗前先量一遍 PNG：图宽 ≠ 帧宽 × 列数会立刻警告。
· 播放由场景时钟驱动，同一场景里的多个动画天然同步。
· 渲染后端由 SDL 自选（它会给透明窗口跳过不支持透明的后端）。手写
  "d3d11" 是 SDL2 的名字，SDL3 叫 "direct3d11"；写错时窗口照开、透明照落地，
  但一个字都画不上——这个坑 2026-11-01 踩过一次，所以现在会探测渲染器。
TXT

# 校验和（README 自己不算）
( cd "$OUT" && find . -type f -print0 | sort -z | xargs -0 sha256sum ) \
    > "dist/${NAME}.SHA256SUMS.txt"

# zip：条目名带顶层目录，解压不散一地（这台机器没有 zip，用 Python）
ZIP="dist/${NAME}.zip"
rm -f "$ZIP"
python - "$(cygpath -w "$(pwd)/${ZIP}")" "$(cygpath -w "$(pwd)/${OUT}")" <<'PY'
import os
import sys
import zipfile

zip_path, root = sys.argv[1], sys.argv[2]
base = os.path.dirname(root)
count = 0
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as archive:
    for dirpath, _, files in os.walk(root):
        for name in sorted(files):
            full = os.path.join(dirpath, name)
            archive.write(full, os.path.relpath(full, base))
            count += 1
print(f"   打包文件 {count} 个")
PY

echo "   目录     ${OUT}"
echo "   压缩包   ${ZIP}（$(du -h "$ZIP" | cut -f1)）"
