#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 打 Windows x64 预编译包：dist/inking-<版本>[-<阶段>]-win64/
#
# 只做机械活：**搬文件 → 收运行期 DLL → 算校验和 → 压 zip**。
#
# `BUILD.txt`（包内的构建说明）与 `licenses/THIRD-PARTY-NOTICES.md` 是
# **手写文档**——里面是这一轮的验证记录、依赖清单与已知边界。母本在
# `tools/`，脚本只负责拷进包里：写在输出目录里会被开头的 `rm -rf` 删掉
# （第一版就丢过）。
#
# 流程：构建 → 更新 `tools/BUILD.txt` → 跑本脚本 → 在包内验收。
#
# 0.4.0 起多了一件事：**收运行期 DLL 闭包**。上一版包内只有 exe，使用方
# 必须先装 MSYS2 并把 `ucrt64\bin` 加进 PATH 才能双击运行；漏配的表现是
# **静默退出**（0xC0000135），没有任何提示。现在 bin/ 自带闭包，示例
# 解压即跑；编译自己的工程仍需要 MSYS2（MinGW ABI 的硬前提）。
# ---------------------------------------------------------------------------
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build-release"

version_of() {
    # 从公开头里读，避免版本号出现第二份真相。
    sed -n 's/.*kVersion[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' \
        include/inking.h | head -1
}
stage_of() {
    sed -n 's/.*kVersionStage[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' \
        include/inking.h | head -1
}

VER="$(version_of)"
STAGE="$(stage_of)"
if [ -z "$VER" ] || [ -z "$STAGE" ]; then
    echo "读不出版本号（include/inking.h 的 kVersion / kVersionStage）" >&2
    exit 1
fi

# 正式发布版省略阶段后缀：inking-0.4.0-win64。
# 其它阶段照旧拼进去（inking-0.1.0-test-win64）——测试包与发布包不该同名。
if [ "$STAGE" = "release" ]; then
    NAME="inking-${VER}-win64"
else
    NAME="inking-${VER}-${STAGE}-win64"
fi
OUT="dist/${NAME}"

if [ ! -f "${BUILD_DIR}/bin/libinking.dll" ]; then
    echo "没有 ${BUILD_DIR}/bin/libinking.dll——先构建（ninja -C ${BUILD_DIR}）" >&2
    exit 1
fi

echo "== 打包 ${NAME} =="
rm -rf "$OUT"

# 库 / 导入库 / 公开头 / cmake 包配置 / inkgen.exe 全部走 `cmake --install`：
#   - 这里**不要手抄**这些文件。上一次手工复制就漏了 `lib/cmake/inking/`，
#     于是"包里能链但 find_package 找不到"（CMakeLists.txt 第 365 行起）；
#   - install 的目录布局（bin / lib / include）正好就是包的布局。
cmake --install "${BUILD_DIR}" --prefix "$(pwd)/${OUT}" >/dev/null

# 示例程序不在 install 里（它们是 demo，不是 SDK 的一部分），单独拷。
mkdir -p "$OUT/bin"
cp "${BUILD_DIR}/bin/inking_demo.exe" \
   "${BUILD_DIR}/bin/inking_demo_selftest.exe" "$OUT/bin/"

# ---------------------------------------------------------------------------
# 运行期 DLL 闭包
#
# 直接抄 `ldd` 的**传递**结果，而不是只抄 `libinking.dll` 的导入表：
# SDL3_image → libpng / libfreetype → libharfbuzz → libglib / libpcre2 …
# 只收第一层的话，换台机器启动时才会缺 DLL，而且照旧是静默退出。
#
# 工具链位置从 CMakeCache 读（不在脚本里写死 C:/msys64）：换了前缀也不用改
# 脚本。`ldd` 只解析**这台机器上**的路径，所以必须在工具链 bin 的 PATH 下跑。
# ---------------------------------------------------------------------------
CXX_PATH="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' \
    "${BUILD_DIR}/CMakeCache.txt" | head -1)"
if [ -z "$CXX_PATH" ]; then
    echo "读不到 CMAKE_CXX_COMPILER（${BUILD_DIR}/CMakeCache.txt）" >&2
    exit 1
fi
TOOLCHAIN_BIN="$(dirname "$CXX_PATH")"
if [ ! -d "$TOOLCHAIN_BIN" ]; then
    echo "工具链目录不存在：${TOOLCHAIN_BIN}" >&2
    exit 1
fi

echo "== 收集运行期 DLL（${TOOLCHAIN_BIN}）=="
# **只取 ldd 的 DLL 名字，文件一律从工具链目录拿**。
#
# 为什么不直接用 ldd 解析出来的路径：在带注入 DLL 的环境里（沙箱、调试器、
# 某些安全软件），同名 DLL 会先被从**别处**解析到，ldd 报的是那个路径；
# 按路径过滤就会漏收（实测漏了 zlib1 / libiconv / pcre2 等 7 个，而漏收
# 只在使用方机器上表现为 0xC0000135）。按名字回工具链取，闭包才稳定。
declare -a RUNTIME_DLLS=()
while IFS= read -r name; do
    [ -n "$name" ] || continue
    if [ -f "${TOOLCHAIN_BIN}/${name}" ]; then
        RUNTIME_DLLS+=("${TOOLCHAIN_BIN}/${name}")
    fi
done < <(
    PATH="${TOOLCHAIN_BIN}:${PATH}" ldd \
        "$OUT/bin/libinking.dll" \
        "$OUT/bin/inking_demo.exe" \
        "$OUT/bin/inking_demo_selftest.exe" \
        "$OUT/bin/inkgen.exe" \
        | sed -n 's/^[[:space:]]*\([^ ]*\.dll\) =>.*/\1/p' \
        | sort -u
)
if [ "${#RUNTIME_DLLS[@]}" -eq 0 ]; then
    echo "一个运行期 DLL 都没收到——ldd 没解析出任何依赖" >&2
    exit 1
fi
for dll in "${RUNTIME_DLLS[@]}"; do
    cp -f "$dll" "$OUT/bin/"
done
echo "   收到 ${#RUNTIME_DLLS[@]} 个 DLL"

# ---------------------------------------------------------------------------
# 第三方许可
#
# 分发二进制**必须**带版权与许可文本：SDL 是 zlib（保留声明），FreeType 是
# FTL（要求署名），GLib / libiconv / gettext / Graphite2 是 LGPL（还要说明
# 可替换动态库），GCC 运行库走 Runtime Library Exception。文本从 MSYS2 的
# `share/licenses/` 原样复制——不手抄，免得漏段或改错版本。
# ---------------------------------------------------------------------------
LICENSE_SRC="$(dirname "$TOOLCHAIN_BIN")/share/licenses"
if [ ! -d "$LICENSE_SRC" ]; then
    echo "找不到 MSYS2 许可目录：${LICENSE_SRC}" >&2
    exit 1
fi
mkdir -p "$OUT/licenses"
declare -A LICENSE_PKGS=(
    [SDL3]=SDL3
    [SDL3_image]=SDL3_image
    [SDL3_ttf]=SDL3_ttf
    [SDL3_mixer]=SDL3_mixer
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
for name in "${!LICENSE_PKGS[@]}"; do
    pkg="${LICENSE_PKGS[$name]}"
    if [ -d "${LICENSE_SRC}/${pkg}" ]; then
        cp -r "${LICENSE_SRC}/${pkg}" "$OUT/licenses/${name}"
    else
        echo "缺许可文本：${LICENSE_SRC}/${pkg}" >&2
        exit 1
    fi
done

if [ -f tools/THIRD-PARTY-NOTICES.md ]; then
    cp tools/THIRD-PARTY-NOTICES.md "$OUT/licenses/THIRD-PARTY-NOTICES.md"
else
    echo "缺 tools/THIRD-PARTY-NOTICES.md：包里要有第三方清单" >&2
    exit 1
fi

# 文档：设计文档 + API 专页 + 配置格式（CXXCSS.md 在仓库根，不在 docs/ 下）
mkdir -p "$OUT/docs"
cp -r docs/. "$OUT/docs/"
cp CXXCSS/CXXCSS.md "$OUT/docs/"

# 配置样例（含 CXXCSS.md 与 config.json：使用方 COPY 出去就能改）
mkdir -p "$OUT/examples"
cp -r CXXCSS "$OUT/examples/CXXCSS"

# 最小可运行示例：只依赖本包 + MSYS2，验证 find_package 与运行期 DLL 拷贝。
mkdir -p "$OUT/examples/minimal"
cp examples/minimal/CMakeLists.txt \
   examples/minimal/main.cpp \
   examples/minimal/README.md "$OUT/examples/minimal/"

cp README.md LICENSE "$OUT/"

# 构建说明：**母本在 `tools/BUILD.txt`**，脚本只负责拷进去（理由见文件头）。
if [ -f tools/BUILD.txt ]; then
    cp tools/BUILD.txt "$OUT/BUILD.txt"
else
    echo "缺 tools/BUILD.txt：包内要有一份构建说明" >&2
    exit 1
fi

# 校验和：包内每个文件一条（BUILD.txt 自己不算——它可能还没写）
#
# 跑过自检会在 exe 旁边留 `LOG/` 与 `Log.html`（冒烟测试的副产物），
# 不属于包内容，先删掉——否则"在包内验一次"就会把它们带进 zip。
rm -rf "$OUT/bin/LOG" "$OUT/bin/Log.html"
( cd "$OUT" && find . -type f ! -name BUILD.txt -print0 \
    | sort -z | xargs -0 sha256sum ) > "dist/${NAME}.SHA256SUMS.txt"

# zip：把 BUILD.txt 一起收进去（此时它应该已经写好了）
#
# 这台机器上没有 `zip`（Git Bash 不自带、也没装 7z），所以用 Python 的
# zipfile。条目名带顶层目录名，解压不会散一地。
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

echo "   文件数   $(find "$OUT" -type f | wc -l)"
echo "   压缩包   ${ZIP}（$(du -h "$ZIP" | cut -f1)）"
echo "   校验和   dist/${NAME}.SHA256SUMS.txt"
