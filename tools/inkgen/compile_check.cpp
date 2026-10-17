// inkgen 生成链路的**编译自检**：这里没有任何业务逻辑。
//
// 它的唯一作用是让「CXXCSS/*.json → inkgen 生成 .h/.cpp → 编译 → 链接」这条
// 链路在每次构建时真的走一遍。生成出来的 `*_register.cpp` 会 include 生成头，
// 所以生成的头文件也会被编译器校验，而不是只看 inkgen 自己的退出码。
//
// 想看产物：构建后到 `build/inkgen/` 下找（只落构建目录，不进源码树，
// 见 docs/AGENTS.md §3 第 9 条）。
#include <cstdio>

int main() {
    std::printf("inkgen compile check ok\n");
    return 0;
}
