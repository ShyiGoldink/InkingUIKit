// 设计画布尺寸。这是**唯一**决定 letterbox 逻辑坐标系的地方。
//
// 两个概念要分清：
//   - `kDefaultDesignWidth` / `kDefaultDesignHeight`：**编译期默认值**，
//     没有配置时就用它；
//   - `DesignWidth()` / `DesignHeight()`：**运行期实际值**，库内部一律走这两个。
//
// 为什么需要运行期这一层（2026-10-17 补）：本库以**预编译 DLL** 分发，
// 消费方在 `CXXCSS/config.json` 里改设计尺寸，改的是**他自己的构建产物**，
// 碰不到 DLL 里烘死的常量。所以配置必须落到一条运行期入口——
// inkgen 生成的 `inking::cxxcss::ApplyDesignConfig()` 最终就是调
// `SetDesignSize()`。少了这一层的后果是"配置写对了、窗口纹丝不动"，
// 而这类静默失效在本仓库吃过不止一次亏（`INK_HAS_SDL3_TTF` 那次）。
//
// 定型的时机：设计尺寸在**第一份按它算的几何**产生时被钉住——
// 也就是第一个窗口 / 场景构造的时候。之后 `SetDesignSize` 会**拒绝**改动并
// 报错，而不是让"一半场景是旧尺寸、一半是新尺寸"这种半生效状态存在。
#pragma once

namespace inking {

// 设计画布默认 = 720p。素材（背景 / CG）本身就是 1280×720，1:1 对应最清楚；
// 窗口怎么拉都不改设计坐标，只改展示倍率（letterbox 等比 + 黑边）。
inline constexpr int kDefaultDesignWidth  = 1280;
inline constexpr int kDefaultDesignHeight = 720;

// 旧名字，**只为源码兼容保留**：它们永远是上面那两个默认值，
// 不随 `SetDesignSize` 变化。要读"这一次实际用的尺寸"请用 `DesignWidth()`。
inline constexpr int kDesignWidth  = kDefaultDesignWidth;
inline constexpr int kDesignHeight = kDefaultDesignHeight;

/// 设计画布尺寸上限。超过它的配置按错误处理：那是输入法把数字按错的量级。
inline constexpr int kMaxDesignSize = 16384;

/// 实际设计宽度（设计坐标）。
int DesignWidth() noexcept;

/// 实际设计高度（设计坐标）。
int DesignHeight() noexcept;

/// 一次取两个（省得调两遍，也让"同一帧读到的一对"天然一致）。
void GetDesignSize(int& width, int& height) noexcept;

/**
 * @brief 设定设计画布尺寸（`CXXCSS/config.json` 的 `design.width/height`）。
 *
 * **必须在构造任何场景 / 窗口之前调用**——设计尺寸在那一刻就被烘进
 * 根锚点、letterbox 逻辑尺寸与缩放档位里，晚了改不动。生成代码里的
 * `inking::cxxcss::ApplyDesignConfig()` 应当放在 `main()` 的第一行。
 *
 * 三条拒绝规则（返回 false + 一条 ERROR 日志，**不静默夹取**）：
 *   1. 宽或高不在 `1..kMaxDesignSize`；
 *   2. 已经有人按旧尺寸定型了（窗口 / 场景已构造）而新值不同；
 *   3. 上一轮已经失败过（避免"改一半"的状态被后续调用当成成功）。
 *
 * 传当前值（幂等调用）返回 true，不算失败。
 */
bool SetDesignSize(int width, int height);

namespace detail {

/**
 * 记一笔"已经有人按当前设计尺寸定型了"。
 *
 * 由**唯一几个真的按设计尺寸算几何的地方**调用：`InkingWindow` 的根锚点、
 * `InkingScene` 的根锚点。这不是给业务用的入口，是给 `SetDesignSize`
 * 判断"还来不来得及"用的。
 */
void NoteDesignSizeTaken() noexcept;

/// 已经定型了吗（自检与 `SetDesignSize` 用）。
bool DesignSizeTaken() noexcept;

}  // namespace detail

}  // namespace inking
