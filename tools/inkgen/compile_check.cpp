// inkgen 生成链路的**编译自检**：这里没有任何业务逻辑。
//
// 它的唯一作用是让「CXXCSS/*.json → inkgen 生成 .h/.cpp → 编译 → 链接」这条
// 链路在每次构建时真的走一遍。生成出来的 `*_register.cpp` 会 include 生成头，
// 所以生成的头文件也会被编译器校验，而不是只看 inkgen 自己的退出码。
//
// 想看产物：构建后到 `build/inkgen/` 下找（只落构建目录，不进源码树，
// 见 docs/AGENTS.md §3 第 9 条）。

// G-08：生成的类**真的**继承了动态档——这是 `"dynamic": true` 唯一的客观判据。
//
// 为什么必须在这里断（而不是只断配置读得懂）：`dynamic` 只影响"生成的类继承谁",
// 它不产生任何运行期数据，所以配置通过校验 ≠ 产物换了基类。而这个错误的表现是
// **生成的类几何改不动**（编译期就会红在"调不到 Resize"那行，但那时离真正的原因
// 已经很远了）。
//
// 为什么帧动画那份必须单独断：图片与帧动画**共用同一段 emit 代码**、只差一个
// `isSprite` 分支，只断图片那一侧等于没断帧动画那一侧。
#include <inkgen/all/button_service.h>

#include <cstdio>
#include <type_traits>

static_assert(std::is_base_of_v<InkingDynamicImage,
                                cxxcss::DynamicWalker>,
              "CXXCSS/Image/dynamicWalker.json 应生成动态图片");
static_assert(std::is_base_of_v<InkingDynamicSprite,
                                cxxcss::DynamicRunner>,
              "CXXCSS/Image/dynamicRunner.json 应生成动态帧动画");
static_assert(std::is_base_of_v<InkingDynamicLabel,
                                cxxcss::DynamicTitle>,
              "CXXCSS/Label/dynamicTitle.json 应生成动态标签");
static_assert(std::is_base_of_v<InkingDynamicPanel,
                                cxxcss::DynamicDrawer>,
              "CXXCSS/Panel/dynamicDrawer.json 应生成动态面板");

// 反向：没写 `dynamic` 的配置必须**仍在**静态档——否则"加了开关"就变成了
// "把默认值改了"，而那种改动在产物 diff 里只表现为一行基类名。
static_assert(!std::is_base_of_v<InkingDynamicImage,
                                 cxxcss::CardArt>,
              "没写 dynamic 的图片不该变成动态档");
static_assert(!std::is_base_of_v<InkingDynamicSprite,
                                 cxxcss::Walker>,
              "没写 dynamic 的帧动画不该变成动态档");
static_assert(!std::is_base_of_v<InkingDynamicLabel,
                                 cxxcss::MenuTitle>,
              "没写 dynamic 的标签不该变成动态档");
static_assert(!std::is_base_of_v<InkingDynamicPanel,
                                 cxxcss::DrawerPanel>,
              "没写 dynamic 的面板不该变成动态档");

int main() {
    std::printf("inkgen compile check ok\n");
    return 0;
}
