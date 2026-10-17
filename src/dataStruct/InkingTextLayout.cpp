#include <dataStruct/InkingTextLayout.h>

#include <algorithm>
#include <string_view>

// 文本排版的实现（A7）。三处关键取向：
//
//   1. **按"单元"贪心，每次量整行**：单元 = 一个英文词 / 一个 CJK 字 / 一段空白。
//      累加单元时每加一个就问一次"这一行现在多宽"——比"假设宽度可加"准
//      （字距、连字、混排下宽度本来就不等于逐字相加），代价是每行几十次测量，
//      而真正的测量有纹理层缓存兜着；
//   2. **一个词自己就超宽时退化成硬切**：宁可切开，也不要它溢出去——
//      溢出是"看不见的错"，切开是"看得见但能用"；
//   3. **省略号先回退再补**：`放得下吗` 这个判断把 `…` 的宽度也算进去，
//      否则"加了省略号反而超出"会在窄框上稳定复现。

namespace inking {

namespace {

/// 一个 UTF-8 码点。
struct Utf8Step {
    std::uint32_t code = 0;
    std::size_t size = 1;
};

Utf8Step DecodeUtf8(std::string_view text, std::size_t pos) {
    const auto byte = [&](std::size_t index) {
        return static_cast<std::uint32_t>(
            static_cast<unsigned char>(text[index]));
    };
    const std::uint32_t first = byte(pos);
    if (first < 0x80u) {
        return {first, 1};
    }
    if ((first & 0xE0u) == 0xC0u && pos + 1 < text.size()) {
        return {((first & 0x1Fu) << 6) | (byte(pos + 1) & 0x3Fu), 2};
    }
    if ((first & 0xF0u) == 0xE0u && pos + 2 < text.size()) {
        return {((first & 0x0Fu) << 12) | ((byte(pos + 1) & 0x3Fu) << 6)
                    | (byte(pos + 2) & 0x3Fu),
                3};
    }
    if ((first & 0xF8u) == 0xF0u && pos + 3 < text.size()) {
        return {((first & 0x07u) << 18) | ((byte(pos + 1) & 0x3Fu) << 12)
                    | ((byte(pos + 2) & 0x3Fu) << 6) | (byte(pos + 3) & 0x3Fu),
                4};
    }
    // 非法字节：当单字节处理。宁可画出一个怪字符，也不要在这里崩或者死循环。
    return {first, 1};
}

bool IsSpace(std::uint32_t code) {
    return code == 0x20u || code == 0x09u;
}

/// CJK 之类"字与字之间随便断"的码点。
bool IsBreakableEverywhere(std::uint32_t code) {
    return (code >= 0x1100u && code <= 0x11FFu)   // 韩文字母
        || (code >= 0x2E80u && code <= 0x303Fu)   // CJK 部首 / 标点
        || (code >= 0x3040u && code <= 0x30FFu)   // 平假名 / 片假名
        || (code >= 0x3400u && code <= 0x4DBFu)   // 扩展 A
        || (code >= 0x4E00u && code <= 0x9FFFu)   // 基本区
        || (code >= 0xAC00u && code <= 0xD7AFu)   // 韩文音节
        || (code >= 0xF900u && code <= 0xFAFFu)   // 兼容表意
        || (code >= 0xFF00u && code <= 0xFF60u);  // 全角形式
}

/// 一个"断点单元"：源串里的一段字节，换行时整段搬走或者整段留下。
struct Unit {
    std::size_t begin = 0;
    std::size_t end = 0;
};

/**
 * 把一段（不含 `\n` 的）文本切成断点单元。
 *
 * 规则：空白各自成单元（断点）；CJK 一个字一个单元（处处可断）；
 * 其余连续字符（英文单词 / 数字 / 标点）合成一个单元（整词不切）。
 */
std::vector<Unit> SplitUnits(std::string_view text, std::size_t begin,
                             std::size_t end) {
    std::vector<Unit> units;
    std::size_t pos = begin;
    while (pos < end) {
        const Utf8Step step = DecodeUtf8(text, pos);
        const std::size_t stop = std::min(pos + step.size, end);
        if (IsSpace(step.code) || IsBreakableEverywhere(step.code)) {
            units.push_back(Unit{pos, stop});
            pos = stop;
            continue;
        }
        std::size_t run = stop;
        while (run < end) {
            const Utf8Step next = DecodeUtf8(text, run);
            if (IsSpace(next.code) || IsBreakableEverywhere(next.code)) {
                break;
            }
            run = std::min(run + next.size, end);
        }
        units.push_back(Unit{pos, run});
        pos = run;
    }
    return units;
}

float MeasureWidth(const TextMeasureFn& measure, std::string_view text,
                   std::size_t begin, std::size_t end, const TextStyle& style) {
    if (begin >= end) {
        return 0.0f;
    }
    return measure(std::string(text.substr(begin, end - begin)), style).width;
}

float MeasureHeight(const TextMeasureFn& measure, std::string_view text,
                    std::size_t begin, std::size_t end, const TextStyle& style,
                    float fallback) {
    if (begin >= end) {
        return fallback;
    }
    const TextExtent extent =
        measure(std::string(text.substr(begin, end - begin)), style);
    return extent.height > 0.0f ? extent.height : fallback;
}

/// 行首 / 行尾的空白不算进这一行（换行留下的空格不该顶在行首）。
void TrimSpaces(std::string_view text, std::size_t& begin, std::size_t& end) {
    while (begin < end) {
        const Utf8Step step = DecodeUtf8(text, begin);
        if (!IsSpace(step.code)) {
            break;
        }
        begin = std::min(begin + step.size, end);
    }
    while (end > begin) {
        // 从后往前找一个码点起点：往前退到不是续字节的位置。
        std::size_t back = end - 1;
        while (back > begin
               && (static_cast<unsigned char>(text[back]) & 0xC0u) == 0x80u) {
            --back;
        }
        const Utf8Step step = DecodeUtf8(text, back);
        if (!IsSpace(step.code)) {
            break;
        }
        end = back;
    }
}

/// 硬切：把 `[begin,end)` 里"放得下的最长前缀"切出来（`Anywhere` 与超宽词用）。
struct HardCut {
    std::size_t end = 0;   ///< 真的放得下的终点
    bool fits = false;     ///< 至少放下了一个码点
};

HardCut CutToWidth(const TextMeasureFn& measure, std::string_view text,
                   std::size_t begin, std::size_t end, float maxWidth,
                   const TextStyle& style) {
    HardCut cut;
    cut.end = begin;
    std::size_t pos = begin;
    while (pos < end) {
        const Utf8Step step = DecodeUtf8(text, pos);
        const std::size_t stop = std::min(pos + step.size, end);
        const float width = MeasureWidth(measure, text, begin, stop, style);
        if (maxWidth > 0.0f && width > maxWidth && cut.fits) {
            break;  // 再加一个就超了：停在能放下的地方
        }
        cut.end = stop;
        cut.fits = stop > begin;
        pos = stop;
    }
    return cut;
}

}  // namespace

float LineOffsetX(float lineWidth, float availableWidth, TextAlignH align) {
    switch (align) {
        case TextAlignH::Center:
            return (availableWidth - lineWidth) * 0.5f;
        case TextAlignH::Right:
            return availableWidth - lineWidth;
        case TextAlignH::Left:
        default:
            return 0.0f;
    }
}

TextLayoutResult LayoutText(const TextLayoutRequest& request,
                            const TextMeasureFn& measure) {
    TextLayoutResult result;
    const TextStyle& style = request.style;
    const LayoutSpacing& padding = style.padding;
    const float available =
        request.maxWidth > 0.0f
            ? std::max(0.0f, request.maxWidth - padding.left - padding.right)
            : 0.0f;

    const std::string_view text(request.text);
    if (text.empty()) {
        result.empty = true;
        result.contentWidth = 0.0f;
        result.contentHeight = 0.0f;
        result.width = padding.left + padding.right;
        result.height = padding.top + padding.bottom;
        return result;
    }

    const float fallbackHeight = style.fontSize > 0.0f ? style.fontSize : 1.0f;

    // 显式换行先切段落：`\r\n` 与 `\n` 都当断行（Windows 编辑器存的文本常见）。
    std::vector<std::pair<std::size_t, std::size_t>> paragraphs;
    {
        std::size_t start = 0;
        for (std::size_t pos = 0; pos < text.size();) {
            const char c = text[pos];
            if (c == '\n') {
                paragraphs.emplace_back(start, pos);
                start = pos + 1;
                ++pos;
                continue;
            }
            if (c == '\r') {
                paragraphs.emplace_back(start, pos);
                start = (pos + 1 < text.size() && text[pos + 1] == '\n')
                            ? pos + 2
                            : pos + 1;
                pos = start;
                continue;
            }
            const Utf8Step step = DecodeUtf8(text, pos);
            pos = std::min(pos + step.size, text.size());
        }
        paragraphs.emplace_back(start, text.size());
    }

    // 每一行：源串区间 + 宽度 + 行高。行数上限在最后统一处理（要先知道后面还有没有）。
    struct RawLine {
        std::size_t begin = 0;
        std::size_t end = 0;
        float width = 0.0f;
        float height = 0.0f;
    };
    std::vector<RawLine> raw;
    bool truncated = false;
    const bool wrapEnabled = request.wrap != TextWrap::NoWrap && available > 0.0f;

    for (const auto& paragraph : paragraphs) {
        std::size_t cursor = paragraph.first;
        const std::size_t stop = paragraph.second;
        if (cursor == stop) {
            raw.push_back(RawLine{cursor, stop, 0.0f, fallbackHeight});
            continue;
        }
        while (cursor < stop) {
            std::size_t lineBegin = cursor;
            std::size_t lineEnd = cursor;
            float width = 0.0f;
            if (!wrapEnabled) {
                lineEnd = stop;
                width = MeasureWidth(measure, text, lineBegin, lineEnd, style);
            } else {
                const std::vector<Unit> units = SplitUnits(text, cursor, stop);
                std::size_t fitEnd = cursor;
                float fitWidth = 0.0f;
                for (const Unit& unit : units) {
                    const float candidate =
                        MeasureWidth(measure, text, cursor, unit.end, style);
                    if (candidate > available && fitEnd > cursor) {
                        break;  // 这个单元放不下，换行
                    }
                    if (candidate > available && fitEnd == cursor) {
                        // 第一个单元就超宽：词太长（或框太窄）→ 硬切它。
                        const HardCut cut = CutToWidth(measure, text, cursor,
                                                       unit.end, available,
                                                       style);
                        fitEnd = cut.fits ? cut.end : unit.end;
                        fitWidth = MeasureWidth(measure, text, cursor, fitEnd,
                                                style);
                        break;
                    }
                    fitEnd = unit.end;
                    fitWidth = candidate;
                }
                lineEnd = fitEnd > cursor ? fitEnd : stop;
                width = fitEnd > cursor
                            ? fitWidth
                            : MeasureWidth(measure, text, cursor, stop, style);
                cursor = lineEnd;
            }
            if (!wrapEnabled) {
                cursor = stop;
            }

            std::size_t trimmedBegin = lineBegin;
            std::size_t trimmedEnd = lineEnd;
            TrimSpaces(text, trimmedBegin, trimmedEnd);
            const bool blankLine = trimmedBegin >= trimmedEnd;
            const float lineHeight =
                MeasureHeight(measure, text, trimmedBegin, trimmedEnd, style,
                              fallbackHeight)
                * (style.lineSpacing > 0.0f ? style.lineSpacing : 1.0f);
            raw.push_back(RawLine{blankLine ? lineBegin : trimmedBegin,
                                  blankLine ? lineBegin : trimmedEnd,
                                  blankLine ? 0.0f : width, lineHeight});
            if (lineEnd <= lineBegin) {
                break;  // 保险：不许死循环（宽度为 0 之类）
            }
        }
    }

    // 行数上限 / 省略号。
    const int maxLines = request.maxLines > 0 ? request.maxLines : 0;
    const bool overLines =
        maxLines > 0 && static_cast<int>(raw.size()) > maxLines;
    if (overLines) {
        truncated = true;
        raw.resize(static_cast<std::size_t>(maxLines));
    }
    bool lastLineEllipsis = false;
    const bool lastLineTooWide =
        !raw.empty() && available > 0.0f && raw.back().width > available;
    if (request.overflow == TextOverflow::Ellipsis
        && !raw.empty()
        && (overLines || lastLineTooWide)) {
        RawLine& last = raw.back();
        const std::size_t originalEnd = last.end;
        const float hintWidth = measure(std::string(kTextEllipsis), style).width;
        if (available > 0.0f) {
            // 从末尾回退，直到"这一行 + …"放得下（把省略号自己的宽度算进去）。
            std::size_t begin = last.begin;
            std::size_t end = last.end;
            while (end > begin) {
                const float width =
                    MeasureWidth(measure, text, begin, end, style) + hintWidth;
                if (width <= available) {
                    break;
                }
                std::size_t back = end - 1;
                while (back > begin
                       && (static_cast<unsigned char>(text[back]) & 0xC0u)
                              == 0x80u) {
                    --back;
                }
                end = back;
            }
            last.end = end;
            last.width = MeasureWidth(measure, text, begin, end, style)
                       + hintWidth;
            lastLineEllipsis = true;
            if (end < originalEnd) {
                truncated = true;
            }
        } else {
            // 没有宽度约束（`maxWidth == 0`）时只剩行数限制：直接补个省略号，
            // 表示"后面的没画"。
            last.width += hintWidth;
            lastLineEllipsis = true;
        }
    }

    // 水平 / 垂直对齐 + 总尺寸。
    float contentWidth = 0.0f;
    for (const RawLine& line : raw) {
        contentWidth = std::max(contentWidth, line.width);
    }
    const float alignWidth = available > 0.0f ? available : contentWidth;
    float contentHeight = 0.0f;
    for (const RawLine& line : raw) {
        contentHeight += line.height;
    }

    float verticalOffset = 0.0f;
    if (request.maxHeight > 0.0f) {
        const float inner = std::max(0.0f, request.maxHeight - padding.top
                                                - padding.bottom);
        switch (style.alignV) {
            case TextAlignV::Middle:
                verticalOffset = (inner - contentHeight) * 0.5f;
                break;
            case TextAlignV::Bottom:
                verticalOffset = inner - contentHeight;
                break;
            case TextAlignV::Top:
            default:
                break;
        }
    }

    float y = padding.top + verticalOffset;
    result.lines.reserve(raw.size());
    for (const RawLine& line : raw) {
        TextLine out;
        out.begin = line.begin;
        out.end = line.end;
        out.width = line.width;
        out.x = padding.left + LineOffsetX(line.width, alignWidth, style.align);
        out.y = y;
        out.height = line.height;
        out.hasEllipsis = lastLineEllipsis && (&line == &raw.back());
        y += line.height;
        result.lines.push_back(out);
    }

    result.contentWidth = contentWidth;
    result.contentHeight = contentHeight;
    result.width = contentWidth + padding.left + padding.right;
    result.height = contentHeight + padding.top + padding.bottom;
    if (request.maxWidth > 0.0f) {
        result.width = std::min(result.width, request.maxWidth);
    }
    result.truncated = result.truncated || truncated;
    result.empty = result.lines.size() <= 1 && contentWidth <= 0.0f;
    return result;
}

}  // namespace inking
