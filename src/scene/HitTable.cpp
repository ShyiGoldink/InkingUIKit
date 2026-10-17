#include <scene/HitTable.h>

#include <widget/InkingWidget.h>

#include <algorithm>
#include <cmath>

namespace inking {
namespace {

/// 粗块边长（设计坐标）。一个粗块 = 一个簇。256 是按"1280×720 的界面大约
/// 20~30 个簇"选的：簇在查询时要二分定位，太多就没意义了。
constexpr float kBlockSize = 256.0f;

/// 簇内格子边长的上下限。太细 → 空白区格子数按面积涨；太粗 → 密集区候选多、
/// 早退失效（§10.3 说的就是这对折中）。
constexpr float kMinCellSize = 16.0f;
constexpr float kMaxCellSize = 128.0f;

/// cellSize 取 8 的倍数：格子边界对齐到整数，README 里报出来的数字也好读。
constexpr float kCellQuantize = 8.0f;

/// 包络统一往外放一点。
///
/// **这不是"差不多就行"，是必须的**：包络是拿浮点算出来的（四个角过变换、
/// 再取 min/max），而精判 `HitTest` 走的是另一条浮点路径。两条路径的误差
/// 在 1e-5 量级，但**恰好落在边界上的点**会因此出现"形状里算命中、包络里算在外"
/// ——实测 24978 个采样点里有 26 个是这样漏掉的（自检的对账抓出来的）。
/// 外扩半像素把它们盖住，代价是候选偶尔多一两个。
constexpr float kEnvelopePadding = 0.5f;

/// 落在哪个粗块 / 格子。用 floor 而不是截断，负数侧才不会挤到 0 号格。
int BlockIndex(float value, float size) noexcept {
    return static_cast<int>(std::floor(value / size));
}

ShapeBounds Padded(const ShapeBounds& bounds) noexcept {
    ShapeBounds padded;
    padded.x = bounds.x - kEnvelopePadding;
    padded.y = bounds.y - kEnvelopePadding;
    padded.width = bounds.width + 2.0f * kEnvelopePadding;
    padded.height = bounds.height + 2.0f * kEnvelopePadding;
    return padded;
}

int LocalCellIndex(float value, float origin, float cellSize) noexcept {
    return static_cast<int>(std::floor((value - origin) / cellSize));
}

float ClampCellSize(float value) noexcept {
    float clamped = value;
    if (clamped < kMinCellSize) {
        clamped = kMinCellSize;
    }
    if (clamped > kMaxCellSize) {
        clamped = kMaxCellSize;
    }
    return std::round(clamped / kCellQuantize) * kCellQuantize;
}

bool EnvelopeContains(const ShapeBounds& bounds, float x, float y) noexcept {
    return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y
        && y < bounds.y + bounds.height;
}

/// 一个粗块里收到的条目（索引指向 `_entries`）。
struct BlockBucket {
    int blockX = 0;
    int blockY = 0;
    std::vector<std::uint32_t> items;
};

}  // namespace

void HitTable::Clear() noexcept {
    _entries.clear();
    _holes.clear();
    _clusters.clear();
    _cells.clear();
    _candidates.clear();
    _maxCellCandidates = 0;
}

void HitTable::AddEntry(InkingWidget* node, const ShapeBounds& envelope,
                        bool passThrough) noexcept {
    if (node == nullptr) {
        return;
    }
    Entry entry;
    entry.node = node;
    entry.envelope = Padded(envelope);
    entry.passThrough = passThrough;
    _entries.push_back(entry);
}

void HitTable::AddHole(const ShapeBounds& envelope) noexcept {
    _holes.push_back(Padded(envelope));
}

bool HitTable::IsInDynamicHole(float x, float y) const noexcept {
    // 线性扫：动态组件通常只有几个。真到了几百个再给它建索引也不迟
    // （那时该先问一句"是不是该把它们收进一个动态容器"）。
    for (const ShapeBounds& hole : _holes) {
        if (EnvelopeContains(hole, x, y)) {
            return true;
        }
    }
    return false;
}

void HitTable::Build() noexcept {
    _clusters.clear();
    _cells.clear();
    _candidates.clear();
    _maxCellCandidates = 0;

    if (_entries.empty()) {
        return;
    }

    // ① 全局按 z 降序。这一步定下了后面所有顺序：格子的候选也是这个顺序，
    //    于是查询时"第一个真正命中的"就是最上面那个（§10.2）。
    //    用 stable_sort：z 相同时保持调用方给的顺序（场景那边已经是注册序号序）。
    std::stable_sort(_entries.begin(), _entries.end(),
                     [](const Entry& a, const Entry& b) {
                         return InkingWidget::IsAbove(*a.node, *b.node);
                     });

    // ② 按粗块分组。**一个条目可以进多个粗块**：大面板横跨几个粗块时，
    //    每个粗块都得收它，否则"从另一侧点进去"就查不到了。
    //    （注意这与 §10.5 规则 4 说的"跨簇条目"不是一回事：那说的是
    //    局部层级下"同一个条目登记在多个簇、谁在上面"的仲裁；我们只有
    //    全局一条 z 序，索引层面重复登记只是为了查得到。）
    std::vector<BlockBucket> buckets;
    const auto findBucket = [&buckets](int blockX, int blockY) -> BlockBucket& {
        for (BlockBucket& bucket : buckets) {
            if (bucket.blockX == blockX && bucket.blockY == blockY) {
                return bucket;
            }
        }
        buckets.push_back(BlockBucket{blockX, blockY, {}});
        return buckets.back();
    };

    for (std::uint32_t i = 0; i < _entries.size(); ++i) {
        const ShapeBounds& env = _entries[i].envelope;

        // 包络为空（宽或高为 0）时也登记：它仍可能被点到（一个 1px 的线）。
        const float right = env.x + (env.width > 0.0f ? env.width : 0.0f);
        const float bottom = env.y + (env.height > 0.0f ? env.height : 0.0f);

        const int firstX = BlockIndex(env.x, kBlockSize);
        const int firstY = BlockIndex(env.y, kBlockSize);
        const int lastX = BlockIndex(right, kBlockSize);
        const int lastY = BlockIndex(bottom, kBlockSize);

        for (int by = firstY; by <= lastY; ++by) {
            for (int bx = firstX; bx <= lastX; ++bx) {
                findBucket(bx, by).items.push_back(i);
            }
        }
    }

    // ③ 每个簇：先定 cellSize，再切格子、填候选。
    for (BlockBucket& bucket : buckets) {
        // cellSize 取该簇条目的**平均短边**：格子边长与条目尺寸同量级时，
        // 每个条目大约覆盖常数个格子——这正是 §10.3 想要的"按 UI 分布聚类"。
        float sum = 0.0f;
        for (const std::uint32_t index : bucket.items) {
            const ShapeBounds& env = _entries[index].envelope;
            const float shorter = env.width < env.height ? env.width : env.height;
            sum += shorter > 0.0f ? shorter : kMinCellSize;
        }
        const float average =
            sum / static_cast<float>(bucket.items.size());
        const float cellSize = ClampCellSize(average);

        Cluster cluster;
        cluster.blockX = bucket.blockX;
        cluster.blockY = bucket.blockY;
        cluster.originX = static_cast<float>(bucket.blockX) * kBlockSize;
        cluster.originY = static_cast<float>(bucket.blockY) * kBlockSize;
        cluster.cellSize = cellSize;
        cluster.columns = static_cast<int>(std::ceil(kBlockSize / cellSize));
        cluster.rows = cluster.columns;
        if (cluster.columns <= 0) {
            cluster.columns = 1;
            cluster.rows = 1;
        }
        if (cluster.cellSize <= 0.0f) {
            cluster.cellSize = kMinCellSize;
        }

        const std::size_t cellCount =
            static_cast<std::size_t>(cluster.columns) * cluster.rows;
        cluster.cellBegin = static_cast<std::uint32_t>(_cells.size());

        // 先把该簇的格子空出来，再往里填候选（两趟：CSR 的标准做法）。
        std::vector<std::vector<std::uint32_t>> perCell(cellCount);

        for (const std::uint32_t index : bucket.items) {
            const ShapeBounds& env = _entries[index].envelope;
            const int firstX =
                LocalCellIndex(env.x, cluster.originX, cluster.cellSize);
            const int firstY =
                LocalCellIndex(env.y, cluster.originY, cluster.cellSize);
            const int lastX = LocalCellIndex(
                env.x + env.width, cluster.originX, cluster.cellSize);
            const int lastY = LocalCellIndex(
                env.y + env.height, cluster.originY, cluster.cellSize);

            for (int cy = firstY; cy <= lastY; ++cy) {
                if (cy < 0 || cy >= cluster.rows) {
                    continue;
                }
                for (int cx = firstX; cx <= lastX; ++cx) {
                    if (cx < 0 || cx >= cluster.columns) {
                        continue;
                    }
                    perCell[static_cast<std::size_t>(cy) * cluster.columns + cx]
                        .push_back(index);
                }
            }
        }

        // 条目下标本身已经是 z 降序（`_entries` 排过了），所以直接顺序 append
        // 得到的候选就是 z 降序——不用再排一次。
        for (std::vector<std::uint32_t>& cell : perCell) {
            CellRange range;
            range.begin = static_cast<std::uint32_t>(_candidates.size());
            for (const std::uint32_t index : cell) {
                _candidates.push_back(index);
            }
            range.end = static_cast<std::uint32_t>(_candidates.size());
            _cells.push_back(range);
            if (cell.size() > _maxCellCandidates) {
                _maxCellCandidates = cell.size();
            }
        }

        _clusters.push_back(cluster);
    }

    // ④ 簇按 (blockY, blockX) 升序，查询时二分。粗块下标是 int，坐标轴方向
    //    和屏幕一致（y 向下），排序只是为了能二分，不表达任何层级。
    std::sort(_clusters.begin(), _clusters.end(),
              [](const Cluster& a, const Cluster& b) {
                  if (a.blockY != b.blockY) {
                      return a.blockY < b.blockY;
                  }
                  return a.blockX < b.blockX;
              });
}

HitResult HitTable::Query(float x, float y, bool* holeHit) const noexcept {
    if (holeHit != nullptr) {
        *holeHit = IsInDynamicHole(x, y);
    }

    if (_clusters.empty()) {
        return HitResult{};
    }

    // ① 定位簇：二分（簇数量少，但二分不比线性麻烦）。
    const int blockX = BlockIndex(x, kBlockSize);
    const int blockY = BlockIndex(y, kBlockSize);

    const auto it = std::lower_bound(
        _clusters.begin(), _clusters.end(), std::pair<int, int>{blockY, blockX},
        [](const Cluster& cluster, const std::pair<int, int>& key) {
            if (cluster.blockY != key.first) {
                return cluster.blockY < key.first;
            }
            return cluster.blockX < key.second;
        });

    if (it == _clusters.end() || it->blockY != blockY || it->blockX != blockX) {
        return HitResult{};  // 这个粗块里没有 UI
    }

    const Cluster& cluster = *it;

    // ② 簇内定位格子。
    const int cellX = LocalCellIndex(x, cluster.originX, cluster.cellSize);
    const int cellY = LocalCellIndex(y, cluster.originY, cluster.cellSize);
    if (cellX < 0 || cellX >= cluster.columns || cellY < 0
        || cellY >= cluster.rows) {
        return HitResult{};
    }

    const std::size_t cellIndex = static_cast<std::size_t>(cluster.cellBegin)
        + static_cast<std::size_t>(cellY) * cluster.columns + cellX;
    if (cellIndex >= _cells.size()) {
        return HitResult{};
    }

    // ③ 扫候选。顺序**已经是 z 降序**，所以第一个真正命中的就是答案；
    //    遇到 PassThrough 就记下（最上面那个）继续往下找 Block。
    InkingWidget* passTarget = nullptr;
    const CellRange& range = _cells[cellIndex];
    for (std::uint32_t i = range.begin; i < range.end; ++i) {
        if (i >= _candidates.size()) {
            break;
        }
        const Entry& entry = _entries[_candidates[i]];

        // 包络粗筛（格子只缩小范围，这里再用包络缩小一次——因为一个条目
        // 登记进了它覆盖的所有格子，而查询点只在其中一个里）。
        if (!EnvelopeContains(entry.envelope, x, y)) {
            continue;
        }
        // 精判交给节点自己：形状是唯一真相（SDF / ShapeContains）。
        if (!entry.node->HitTest(x, y)) {
            continue;
        }

        if (entry.passThrough) {
            if (passTarget == nullptr) {
                passTarget = entry.node;
            }
            continue;
        }
        return HitResult{HitKind::Block, entry.node};
    }

    if (passTarget != nullptr) {
        return HitResult{HitKind::PassThrough, passTarget};
    }
    return HitResult{};
}

}  // namespace inking
