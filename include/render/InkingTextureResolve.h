#pragma once

// 句柄 → 纹理的解析口（dev010 T-01）：**框架内部头**（含 SDL），不进公开面。
//
// 为什么单独一个文件：它是 `asset/`（句柄注册表，零 SDL）与 `render/`（纹理表，
// 要 SDL）之间唯一的那根线。放进 `InkingBatch.h` 会让提交层知道资源层的存在，
// 放进 `InkingText.h` 会让纹理层知道句柄的存在——两个方向都不该有。
//
// 消费者只有一个：`image/InkingGeometryLayer.cpp` 在 `onRender` 里把
// `InkingQuad::texture`（整数 id）换成可以提交的 `SDL_Texture*`。

#include <cstdint>

struct SDL_Texture;
struct SDL_Renderer;

namespace inking_detail {

/**
 * 把公开句柄解析成当前渲染器上的纹理。
 *
 * 三条行为：
 *   - id 不在注册表里（已 Release / 换过渲染器）⇒ 返回 nullptr 并记一次
 *     `staleSubmits`，**不报错**：调用方跳过这一笔就够，一层里一个坏句柄不该
 *     让整屏黑掉；
 *   - 纹理还没建（Load 之后第一次提交）⇒ 就地建出来并钉住；
 *   - 建不出来（解码失败）⇒ nullptr，`InkingTextureStats::loadFailures` 记账。
 *
 * @param width,height 出参（像素）；传 nullptr 表示不需要。
 */
SDL_Texture* ResolveTextureHandle(std::uint32_t id, SDL_Renderer* renderer,
                                  int* width, int* height);

}  // namespace inking_detail
