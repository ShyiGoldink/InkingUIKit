#pragma once

// 静态命中表：**簇（粗块）→ 簇内均匀格子 → CSR 候选**。
//
// 形态照 `docs/InputDesign.md`：
//   §10.2 格子索引 + CSR 候选数组，条目按 zindex 降序，是否命中仍由节点自己判；
//   §10.3 聚类——只有含 UI 的粗块才建簇，每个簇用自己的 cellSize。
//
// 三条设计约束，都直接来自设计稿，改实现时别丢：
//
//   1. **格子只负责缩小范围**。命中与否仍由节点自己的 `HitTest` 回答，
//      所以格子算粗了只是候选多几个，**不会误命中**（§10.2）。
//   2. **候选按 z 降序**填，"第一个真正包含该点的"就是最上面那个（§10.2）。
//      全局只有一条 z 序规则（`InkingWidget::IsAbove`），和渲染顺序同源。
//   3. **建表正比于 UI 数量**，不再正比于画布面积（§10.3）：空白区没有簇，
//      簇内格子数由该簇自己的 cellSize 决定。
//
// 这个类**不认识场景**：它只收"条目（节点 + 包络 + 让不让过）"和"动态洞的包络"，
// 由 `InkingScene` 负责在帧首重建、在派发时查询。这样表可以单独被自检对账。

#include <dataStruct/InkingShapeSpec.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace inking {

class InkingWidget;

/// 命中的三态（`docs/InputDesign.md` §6）。
enum class HitKind {
    Block,        ///< 事件到此为止（半透明遮罩也属于这一类，遮挡 ≠ 未命中）
    PassThrough,  ///< 有东西，但它让过（继续往下找）
    Miss,         ///< 这里没东西
};

/// 一次 query 的答案：**三态 + 目标**（§6：投递需要目标，所以两样一起给）。
struct HitResult {
    HitKind kind = HitKind::Miss;
    InkingWidget* target = nullptr;

    bool IsHit() const noexcept {
        return kind != HitKind::Miss && target != nullptr;
    }
};

/**
 * 静态命中表。生命周期归调用方（场景持有一个，脏了重建）。
 *
 * 用法：`Clear()` → 逐条 `AddEntry(...)` / `AddHole(...)` → `Build()` → 反复 `Query()`。
 * 重建的成本正比于**条目数 × 覆盖格数**，与画布面积无关。
 */
class HitTable {
public:
    /// 一条静态条目。
    struct Entry {
        InkingWidget* node = nullptr;

        /// **保守包络**（设计坐标、绝对）。为什么不是"当前 AABB"：
        /// 静态组件也能有变换（平移 / 旋转 / 缩放，见 `InkingTransform.h`），
        /// 而变换一变 AABB 就变，格子登记就得跟着改 —— 那就等于每帧重烘表，
        /// 违背 §11「变换通道不改表」。所以这里存的是**组件上报的包络**
        /// （= 它所有可能变换的并集），表因此真正冻结；
        /// 候选的**精筛**与最终判定用的是那一刻的当前值。
        ShapeBounds envelope;

        bool passThrough = false;  ///< 命中后让不让过（三态里的 PassThrough）
    };

    /// 一个簇：覆盖一个粗块，簇内所有条目共用一个 `cellSize`。
    struct Cluster {
        int blockX = 0;  ///< 粗块下标（不是像素坐标）
        int blockY = 0;
        float originX = 0.0f;  ///< 簇的左下角… 上角，设计坐标
        float originY = 0.0f;
        float cellSize = 64.0f;
        int columns = 0;
        int rows = 0;
        std::uint32_t cellBegin = 0;  ///< 本簇第一个格子在 `_cells` 里的下标
    };

    /** 清空表（重建前调）。簇、格子、候选、条目、洞全部清掉。 */
    void Clear() noexcept;

    /**
     * 加一条静态条目。`envelope` 是**绝对**包络（调用方已经把节点左上角加进去了）。
     *
     * 可以按任意顺序加：`Build()` 里会按 z 降序重排。
     */
    void AddEntry(InkingWidget* node, const ShapeBounds& envelope,
                  bool passThrough) noexcept;

    /**
     * 加一个**动态洞**的包络（绝对）。
     *
     * 动态组件不进表（§10.5 规则 3），但它的占地要记下来：点不落在任何洞里，
     * 就不必再去问动态层。注意这**只是性能开关**——正确性由"静态结果与
     * 动态结果按 z 序合并"保证，所以洞漏登也只是多问一次，不会点错。
     */
    void AddHole(const ShapeBounds& envelope) noexcept;

    /// 建簇、切格子、填 CSR 候选。条目为空时是空操作。
    void Build() noexcept;

    /**
     * 查一次：返回命中的最上面那个。
     *
     * @param holeHit 可选出参：点是否落在某个动态洞里（为真时调用方还要问动态层）。
     */
    HitResult Query(float x, float y, bool* holeHit = nullptr) const noexcept;

    /// 点是否落在某个动态洞的包络里。
    bool IsInDynamicHole(float x, float y) const noexcept;

    // ---------------- 观测（自检与预算估算用） ----------------
    std::size_t GetEntryCount() const noexcept { return _entries.size(); }
    std::size_t GetClusterCount() const noexcept { return _clusters.size(); }
    std::size_t GetCellCount() const noexcept { return _cells.size(); }
    std::size_t GetCandidateCount() const noexcept { return _candidates.size(); }
    std::size_t GetHoleCount() const noexcept { return _holes.size(); }

    /// 一次查询最多扫过多少候选（上界，用来判断格子尺寸是不是定得太粗）。
    std::size_t GetMaxCellCandidates() const noexcept { return _maxCellCandidates; }

private:
    /// CSR 的一段：`_candidates[begin, end)`。
    struct CellRange {
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
    };

    std::vector<Entry> _entries;        ///< 按 z 降序（Build 之后）
    std::vector<ShapeBounds> _holes;    ///< 动态洞（线性扫：动态组件通常很少）
    std::vector<Cluster> _clusters;     ///< 按 (blockY, blockX) 升序（二分定位）
    std::vector<CellRange> _cells;      ///< 各簇的格子，簇内按行优先
    std::vector<std::uint32_t> _candidates;  ///< 格子里存的条目下标

    std::size_t _maxCellCandidates = 0;
};

}  // namespace inking
