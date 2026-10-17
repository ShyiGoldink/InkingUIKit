// 定位基类的实现（`include/anchor/InkingAnchor.h`）。
//
// 这个文件**故意只有一个函数**。定位层的全部语义（锚点公式、父链上溯、可见性
// 判定）都已经在头文件里 inline 写完了——位置不存，由锚点推导，所以没有状态、
// 没有缓存、没有需要同步的东西。
//
// 那为什么还要有这么个文件？因为 `~InkingAnchor()` 是**非 inline 的虚函数**，
// 也就是这个类的 key function：按 C++ ABI 规则，类的 vtable 与 typeinfo 只在
// key function 的定义所在的那一个翻译单元里生成。没有它，任何构造
// `InkingWidget` 的翻译单元都会在链接期报
// `undefined reference to vtable for inking::InkingAnchor`，
// 且报的是 vtable（不是析构函数本身）。
//
// 两件事因此被这个文件钉住：
//   1. vtable 有且只有一份（weak symbol 合并的成本也省了）；
//   2. 定位层将来若真有了要释放的资源（比如位置缓存），落点已经在这儿。

#include <anchor/InkingAnchor.h>

namespace inking {

// 本类没有拥有任何资源：`_parent` 是非拥有裸指针（那只是定位的输入），
// 其余成员全是 POD。所以析构是平凡的，`= default` 就是完整语义。
InkingAnchor::~InkingAnchor() = default;

}  // namespace inking
