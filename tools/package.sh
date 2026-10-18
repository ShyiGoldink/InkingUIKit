#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 打 Windows x64 预编译包：dist/inking-<版本>-<阶段>-win64/
#
# 只做三件机械活：**搬文件 → 算校验和 → 压 zip**。
#
# `BUILD.txt`（包内的构建说明）是**手写文档**——里面是这一轮的验证记录与
# 已知边界。它的母本在 `tools/BUILD.txt`，脚本只负责拷进包里：
# 写在输出目录里会被本脚本开头的 `rm -rf` 删掉（第一版就丢了）。
#
# 流程：构建 → 更新 `tools/BUILD.txt` → 跑本脚本。
#
# 为什么不去仓库里找打包脚本的历史：这个包第一次是手工打的（2026-10-17），
# 命令散在对话里；这里把机械部分固化下来，免得每次靠记忆。
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

NAME="inking-${VER}-${STAGE}-win64"
OUT="dist/${NAME}"

if [ ! -f "${BUILD_DIR}/bin/libinking.dll" ]; then
    echo "没有 ${BUILD_DIR}/bin/libinking.dll——先构建（ninja -C ${BUILD_DIR}）" >&2
    exit 1
fi

echo "== 打包 ${NAME} =="
rm -rf "$OUT"

# 库 / 导入库 / 公开头 / cmake 包配置 / inkgen.exe 全部走 `cmake --install`：
#   - 这里**不要手抄**这些文件。上一次手工复制就漏了 `lib/cmake/inking/`，
#     于是"包里能链但 `find_package` 找不到"（CMakeLists.txt 第 359-362 行
#     那段注释记的就是这件事）；
#   - install 的目录布局（bin / lib / include）正好就是包的布局。
cmake --install "${BUILD_DIR}" --prefix "$(pwd)/${OUT}" >/dev/null

# 示例程序不在 install 里（它们是 demo，不是 SDK 的一部分），单独拷。
mkdir -p "$OUT/bin"
cp "${BUILD_DIR}/bin/inking_demo.exe" \
   "${BUILD_DIR}/bin/inking_demo_selftest.exe" "$OUT/bin/"

# 文档：设计文档 + API 专页 + 配置格式（CXXCSS.md 在仓库根，不在 docs/ 下）
mkdir -p "$OUT/docs"
cp -r docs/. "$OUT/docs/"
cp CXXCSS/CXXCSS.md "$OUT/docs/"

# 配置样例（含 CXXCSS.md 与 config.json：使用方 COPY 出去就能改）
mkdir -p "$OUT/examples"
cp -r CXXCSS "$OUT/examples/CXXCSS"

cp README.md LICENSE "$OUT/"

# 构建说明：**母本在 `tools/BUILD.txt`**，脚本只负责拷进去。
#
# 为什么母本放在仓库里而不是"直接写在包内"：脚本第一件事是 `rm -rf` 输出目录，
# 写在包内的那一份会被下一次打包顺手删掉（第一次就这么丢了）。
# 这一份是手写文档（验证记录 / 边界），脚本不生成、不覆盖。
if [ -f tools/BUILD.txt ]; then
    cp tools/BUILD.txt "$OUT/BUILD.txt"
else
    echo "缺 tools/BUILD.txt：包内要有一份构建说明" >&2
    exit 1
fi

# 校验和：包内每个文件一条（BUILD.txt 自己不算——它可能还没写）
#
# `Log.html` 是跑自检时 InkLog 落在 exe 旁边的日志（冒烟测试的副产物），
# 不属于包内容，先删掉——否则"在包内验一次"就会把它带进 zip。
rm -f "$OUT/bin/Log.html"
( cd "$OUT" && find . -type f ! -name BUILD.txt -print0 \
    | sort -z | xargs -0 sha256sum ) > "dist/${NAME}.SHA256SUMS.txt"

# zip：把 BUILD.txt 一起收进去（此时它应该已经写好了）
#
# 这台机器上没有 `zip`（Git Bash 不自带、也没装 7z），所以用 Python 的
# zipfile——它一定在（inkgen 的构建链也依赖 Python 之外的东西，但 Python
# 是本仓库工具链的既有前提）。条目名带顶层目录名，解压不会散一地。
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
