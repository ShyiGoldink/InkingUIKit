#pragma once

#include <button/ButtonData.h>

namespace inking {

/// 这个模块的名字（日志里用；和别的模块一个约定）。
inline constexpr const char* kImageModuleName = "InkingStaticImage";

/**
 * @brief 静态图片的数据（A11）：**按钮那份数据 + 一个"从 assets 读"的开关**。
 *
 * 为什么是继承而不是"再抄一份字段"：A11 的原话就是"img 的逻辑类似于一个没有
 * 写入 onClicked 事件的 button"。数据同构 ⇒ 生成器那边**逐字复用**按钮的烘法，
 * 而且以后往 `AnchorData` 加字段时不用在第五个地方再同步一次
 * （`AGENTS.md` §6 第 83 条记的就是"漏了一处、配置写了却不生效"那种坑）。
 *
 * 三条约定：
 *   - **只有 `normal` 一态有意义**：图片不悬停、不按下、不点击（`hover` /
 *     `onclicked` / `text` 都不该出现在图片配置里，生成器会报错）；
 *   - `runtimeAsset == true` 时 `normal.source` 是**相对资源根的路径**
 *     （默认 exe 旁的 `assets/`），字节在**运行期**由 `inking::AssetLoad` 读进来
 *     ——这条就是"别把所有图烘进 exe"（A11 的目的）。为 false 时照旧是
 *     "绝对路径 + 生成期把字节烘进源码"；
 *   - 图片**照常参与命中**（它不是标签）：压在别人身上时，指针到它这一格就算
 *     命中它——只是它没有任何回调，`onPointer*` 全走基类的空实现。
 */
struct ImageData : public ButtonData {
    /**
     * true = **运行期**从资源根读（`normal.source` 必须是相对路径）；
     * false = 生成期已经把字节烘进来了（`normal.source` 是绝对路径，只作缓存键）。
     */
    bool runtimeAsset = false;
};

}  // namespace inking
