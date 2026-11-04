// 块叠加组的数据面实现（0.9.0 T-04）。
//
// 全部是纯函数：几何算术、查名字、校验。没有状态、没有日志之外的副作用——
// 日志只在 `Validate()` 之外不出现（它只返回字符串，由调用方决定记不记）。

#include <image/SpriteGroupData.h>

#include <cstddef>
#include <string>

std::size_t InkingSpriteGroupData::FindIndex(
    const std::string& animationName) const noexcept {
    // 线性查：动作数只有几个（待机 / 走 / 跑 / 攻击），哈希的常数开销更贵。
    for (std::size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == animationName) {
            return i;
        }
    }
    // **不是 0**：0 是第一个动作，"找不到"与"第一个"混起来会让配置写错静默生效。
    return kNotFound;
}

int InkingSpriteGroupData::ResolveCount() const noexcept {
    if (columns <= 0 || rows <= 0) {
        return 0;
    }
    return columns * rows;
}

InkingShapeBounds InkingSpriteGroupData::CellRect(int cell) const noexcept {
    if (frameWidth <= 0 || frameHeight <= 0 || columns <= 0) {
        return InkingShapeBounds{};
    }
    if (cell < 0) {
        return InkingShapeBounds{};
    }
    const int column = cell % columns;
    const int row = cell / columns;
    if (rows > 0 && row >= rows) {
        return InkingShapeBounds{};
    }
    return InkingShapeBounds{
        static_cast<float>(margin + column * (frameWidth + spacing)),
        static_cast<float>(margin + row * (frameHeight + spacing)),
        static_cast<float>(frameWidth), static_cast<float>(frameHeight)};
}

int InkingSpriteGroupData::SheetWidth() const noexcept {
    // 与 `InkingSpriteData::SpriteSheet::SheetWidth()` 同一个公式。两份公式并存是
    // 有意的：那一个是"图集切格"，这一段是"块叠加组的网格"，两者若哪天分头演进
    // （比如这里加上 per-column 的偏移），改一处不会悄悄影响另一处。
    if (columns <= 0 || frameWidth <= 0) {
        return 0;
    }
    return frameWidth * columns + spacing * (columns - 1) + 2 * margin;
}

int InkingSpriteGroupData::SheetHeight() const noexcept {
    if (rows <= 0 || frameHeight <= 0) {
        return 0;
    }
    return frameHeight * rows + spacing * (rows - 1) + 2 * margin;
}

void InkingSpriteGroupData::Normalize() {
    // 空实现见头文件里的理由：这个类型没有"能修正"的字段。写成一行注释而不是
    // 删掉方法，是为了让 T-05 的构造体保持"Normalize → Validate → 取初始动作"
    // 这个与 `InkingSpriteData` 对称的顺序。
}

std::string InkingSpriteGroupData::Validate() const {
    // ① 图集路径。本类型**不认**"整张纹理"：`sourceRects` 表恒非空，没有路径就
    //    没有纹理可切。
    if (source.empty()) {
        return "块叠加组的 source 为空：必须写图集路径（本类型不认整张纹理）";
    }

    // ② 网格字段。宽高与行列必须 > 0（否则 `CellRect` 算不出）；margin / spacing
    //    不许为负（会把格子推到图外，而画面上只表现为"图偏了一点"）。
    if (frameWidth <= 0 || frameHeight <= 0) {
        return "块叠加组的 frameWidth / frameHeight 必须 > 0（当前 "
               + std::to_string(frameWidth) + "×" + std::to_string(frameHeight) + "）";
    }
    if (columns <= 0 || rows <= 0) {
        return "块叠加组的 columns / rows 必须 > 0（当前 " + std::to_string(columns)
               + "×" + std::to_string(rows) + "）";
    }
    if (margin < 0 || spacing < 0) {
        return "块叠加组的 margin / spacing 不能为负（当前 margin="
               + std::to_string(margin) + " spacing=" + std::to_string(spacing) + "）";
    }

    // ③ 动画表。
    if (animations.empty()) {
        return "块叠加组没有任何动画";
    }

    const int cellCount = ResolveCount();
    for (std::size_t a = 0; a < animations.size(); ++a) {
        const InkingSpriteGroupAnimation& animation = animations[a];

        // ④ 动作名。查出问题时**带上下标与上下文**：一个组里几个动作，只报
        //    "动作名为空"没法定位是哪一个。
        if (animation.name.empty()) {
            return "第 " + std::to_string(a) + " 个动画的名字为空";
        }

        // ⑤ 重名。`FindIndex` 只命中第一个，第二条会被**静默忽略**——
        //    "某个动作永远播不出来"比报错难查得多。
        if (FindIndex(animation.name) != a) {
            return "动画名重复：\"" + animation.name + "\"（第 "
                   + std::to_string(FindIndex(animation.name)) + " 条与第 "
                   + std::to_string(a) + " 条）";
        }

        // ⑥ 帧率。`!(fps > 0.0)` 而不是 `fps <= 0.0`：NaN 也要落到这条上。
        if (!(animation.fps > 0.0)) {
            return "动画 \"" + animation.name + "\" 的 fps 必须 > 0（当前 "
                   + std::to_string(animation.fps) + "）";
        }

        // ⑦ 帧表。整个动画没帧 / 某一帧没有格号，都算"没东西可画"。
        if (animation.frames.empty()) {
            return "动画 \"" + animation.name + "\" 没有任何帧";
        }
        for (std::size_t f = 0; f < animation.frames.size(); ++f) {
            const std::vector<int>& cells = animation.frames[f];
            if (cells.empty()) {
                return "动画 \"" + animation.name + "\" 的第 " + std::to_string(f)
                       + " 帧没有格号";
            }

            // ⑧ 格号越界。报"哪个动画的第几帧第几块"——三个数少一个都定位不了。
            for (std::size_t c = 0; c < cells.size(); ++c) {
                const int cell = cells[c];
                if (cell < 0 || cell >= cellCount) {
                    return "动画 \"" + animation.name + "\" 的第 " + std::to_string(f)
                           + " 帧第 " + std::to_string(c) + " 块的格号 "
                           + std::to_string(cell) + " 越界（共 "
                           + std::to_string(cellCount) + " 格）";
                }
            }
        }

        // ⑨ 事件帧。判据用本动画的帧数。
        for (std::size_t e = 0; e < animation.events.size(); ++e) {
            const InkingSpriteData::SpriteEvent& event = animation.events[e];
            if (event.frame >= animation.frames.size()) {
                return "动画 \"" + animation.name + "\" 的第 " + std::to_string(e)
                       + " 个事件指向第 " + std::to_string(event.frame)
                       + " 帧，但只有 " + std::to_string(animation.frames.size())
                       + " 帧";
            }
            if (event.name.empty()) {
                return "动画 \"" + animation.name + "\" 的第 " + std::to_string(e)
                       + " 个事件没有名字";
            }
        }
    }

    return std::string();
}
