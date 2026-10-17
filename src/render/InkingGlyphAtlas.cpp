#include <render/InkingGlyphAtlas.h>

#include <input/TextEditState.h>  // UTF-8 码点步进（纯函数，三处复用同一份）

#include <algorithm>

namespace inking {

GlyphAtlas::GlyphAtlas(int size) : size_(size > 0 ? size : 1) {}

GlyphAtlas::Slot GlyphAtlas::Insert(int width, int height) {
    Slot slot;
    if (width <= 0 || height <= 0 || width > size_ || height > size_) {
        return slot;  // 尺寸非法 / 比整张图集还大：直接说放不下
    }

    const int needWidth = width + kPadding;
    const int needHeight = height + kPadding;

    // 先找一层放得下的：高度够（这一层能容纳它的高）且右边还有地方。
    // 货架式**不往回收**已经用掉的空间（不做空洞复用）——那是"最优装箱"的活。
    for (Shelf& shelf : shelves_) {
        if (height > shelf.height) {
            continue;
        }
        if (shelf.nextX + needWidth > size_) {
            continue;
        }
        slot.x = shelf.nextX;
        slot.y = shelf.y;
        slot.width = width;
        slot.height = height;
        slot.valid = true;
        shelf.nextX += needWidth;
        slots_.push_back(slot);
        return slot;
    }

    // 开新层：顶边接在最后一层的下面。
    const int nextY = shelves_.empty()
                          ? 0
                          : shelves_.back().y + shelves_.back().height + kPadding;
    if (nextY + needHeight > size_) {
        return slot;  // 高度也排不下了
    }
    Shelf shelf;
    shelf.y = nextY;
    shelf.height = height;
    shelf.nextX = width + kPadding;
    shelves_.push_back(shelf);

    slot.x = 0;
    slot.y = nextY;
    slot.width = width;
    slot.height = height;
    slot.valid = true;
    slots_.push_back(slot);
    return slot;
}

int GlyphAtlas::UsedHeight() const noexcept {
    if (shelves_.empty()) {
        return 0;
    }
    const Shelf& last = shelves_.back();
    return last.y + last.height;
}

double GlyphAtlas::Occupancy() const noexcept {
    if (size_ <= 0) {
        return 0.0;
    }
    std::int64_t used = 0;
    for (const Slot& slot : slots_) {
        used += static_cast<std::int64_t>(slot.width) * slot.height;
    }
    const double total = static_cast<double>(size_) * size_;
    return static_cast<double>(used) / total;
}

bool GlyphAtlas::OverlapsAny(const Slot& slot) const noexcept {
    if (!slot.valid) {
        return false;
    }
    for (const Slot& other : slots_) {
        if (!other.valid) {
            continue;
        }
        // **不做"跳过自己"的特例**：语义就是"这一格和任何已分配的格子重叠吗"。
        // 传进来的槽通常还没被分配（问"这块地方能不能用"），而已经分配过的那份
        // 本来也在表里——要不要排除它，是调用方的事。
        const bool apart = slot.x + slot.width <= other.x
                        || other.x + other.width <= slot.x
                        || slot.y + slot.height <= other.y
                        || other.y + other.height <= slot.y;
        if (!apart) {
            return true;
        }
    }
    return false;
}

void GlyphAtlas::Reset() {
    shelves_.clear();
    slots_.clear();
}

// ---------------------------------------------------------------------------
// 逐字形排版
// ---------------------------------------------------------------------------

std::vector<GlyphPlacement> LayOutGlyphs(std::string_view text, float startX,
                                         const GlyphMeasure& measure) {
    std::vector<GlyphPlacement> placements;
    if (text.empty()) {
        return placements;
    }
    placements.reserve(text.size() / 2 + 1);  // 混排时大约一半是单字节

    float pen = startX;
    std::size_t pos = 0;
    while (pos < text.size()) {
        // 码点边界：**按码点切**，不按字节（中文一个字三字节，按字节切会劈开）。
        const std::size_t next = TextEditState::NextCodePoint(text, pos);
        const std::string_view glyph = text.substr(pos, next - pos);

        GlyphPlacement placement;
        placement.offset = pos;
        placement.length = next - pos;
        placement.x = pen;
        placement.advance = measure ? measure(glyph)
                                    : static_cast<float>(placement.length > 0
                                                             ? glyph.size()
                                                             : 1);
        pen += placement.advance;
        placements.push_back(placement);
        pos = next;
    }
    return placements;
}

}  // namespace inking
