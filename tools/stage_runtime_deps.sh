#!/usr/bin/env bash
# 把运行期 DLL 闭包拷进一个输出目录，让那个目录**自己可跑**（不依赖 PATH 上有没有工具链）。
#
# 为什么需要（2026-11-02 实测）：`ctest` 的 `add_test` 不设 `PATH`，而 SDL3 的 dll 住在
# 工具链 bin（`SDL3_DIR` = `<msys>/ucrt64/lib/cmake/SDL3`），`build/bin` 里只有
# `libinking.dll`。于是：
#   SDL_ttf 找不到 → 字体打不开 → 自检打印 `[skip] 没有可读字体`
#   → `glyph_atlas_full` 的静默失效自检被跳过 → `silent_failure_list_in_synced` 判红。
# 手动带 PATH 跑是"失败项 0"——所以红的是**环境**，不是代码。
#
# 用法：
#   stage_runtime_deps.sh <输出目录> <工具链 bin 目录> [待解析的 PE 文件 ...]
#
#   不给文件时默认解析 `<输出目录>/libinking.dll` 与该目录下所有 `*.exe`。
#
# 与 `tools/package.sh` 里"运行期 DLL 闭包"那段等价（那边拷进 dist，这边拷进
# build/bin）。**改其中一处要同步另一处**——两边的取舍（按名字取、不用 ldd 报的路径）
# 是同一条理由，见下面注释。
set -u

BIN_DIR="${1:-}"
TOOLCHAIN_BIN="${2:-}"
if [ -z "$BIN_DIR" ] || [ -z "$TOOLCHAIN_BIN" ]; then
    echo "stage_runtime_deps：用法 <输出目录> <工具链 bin 目录> [文件 ...]" >&2
    exit 1
fi
if [ ! -d "$BIN_DIR" ]; then
    echo "stage_runtime_deps：输出目录不存在：$BIN_DIR" >&2
    exit 1
fi
if [ ! -d "$TOOLCHAIN_BIN" ]; then
    echo "stage_runtime_deps：工具链目录不存在：$TOOLCHAIN_BIN" >&2
    exit 1
fi
shift 2

# ldd 在 MSYS2 里是 `/usr/bin/ldd`，不在 ucrt64/bin。按布局推一次，再退回 PATH。
LDD="$(dirname "$TOOLCHAIN_BIN")/../usr/bin/ldd"
if [ ! -x "$LDD" ]; then
    LDD="$(command -v ldd 2>/dev/null || true)"
fi
if [ -z "$LDD" ] || [ ! -x "$LDD" ]; then
    # 不 fail：这是一个"让测试能跑"的便利步骤，缺 ldd 的机器上不该因此编不过。
    # 但要说清楚——不说的话，症状会变成"ctest 莫名其妙红两条"。
    echo "stage_runtime_deps：[WARN] 找不到 ldd，跳过 DLL 拷贝；" \
         "ctest 可能因缺 DLL 而假红（首选手动 export PATH=${TOOLCHAIN_BIN}）" >&2
    exit 0
fi

declare -a TARGETS=()
if [ "$#" -gt 0 ]; then
    TARGETS=("$@")
else
    [ -f "${BIN_DIR}/libinking.dll" ] && TARGETS+=("${BIN_DIR}/libinking.dll")
    for exe in "${BIN_DIR}"/*.exe; do
        [ -f "$exe" ] && TARGETS+=("$exe")
    done
fi
if [ "${#TARGETS[@]}" -eq 0 ]; then
    echo "stage_runtime_deps：[WARN] 没有可解析的 PE 文件（先构建库与示例）" >&2
    exit 0
fi

# **只取 ldd 的 DLL 名字，文件一律从工具链目录拿。**
#
# 为什么不直接用 ldd 解析出来的路径：在带注入 DLL 的环境里（沙箱、调试器、某些安全
# 软件），同名 DLL 会先被从**别处**解析到，ldd 报的是那个路径；按路径过滤既不干净
# （会把注入的 dll 拷进产物）又会漏收（工具链里那份反而没拷）。按名字回工具链取，
# 闭包才稳定——`tools/package.sh` 踩过这个，实测漏了 zlib1 / libiconv / pcre2 等 7 个。
copied=0
kept=0
while IFS= read -r name; do
    [ -n "$name" ] || continue
    src="${TOOLCHAIN_BIN}/${name}"
    [ -f "$src" ] || continue          # 系统 dll（KERNEL32 等）自然跳过
    dst="${BIN_DIR}/${name}"
    if [ -f "$dst" ] && cmp -s "$src" "$dst"; then
        kept=$((kept + 1))             # 已经在、内容相同 ⇒ 不碰 mtime（否则每次构建都"变脏"）
        continue
    fi
    cp -f "$src" "$dst"
    copied=$((copied + 1))
done < <(
    PATH="${TOOLCHAIN_BIN}:${PATH}" "$LDD" "${TARGETS[@]}" 2>/dev/null \
        | sed -n 's/^[[:space:]]*\([^ ]*\.dll\) =>.*/\1/p' \
        | sort -u
)

echo "stage_runtime_deps：拷贝 ${copied} 个 DLL、复用 ${kept} 个 → ${BIN_DIR}"
exit 0
