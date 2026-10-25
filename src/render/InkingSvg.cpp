#include <render/InkingSvg.h>

#if defined(INK_HAS_SDL3_IMAGE)
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace inking::detail {

namespace {

#if defined(INK_HAS_SDL3_IMAGE)
/// 开一个只读流；失败返回 nullptr（错误串留在 SDL_GetError 里）。
SDL_IOStream* OpenBytes(const std::uint8_t* data, std::size_t size) {
    return SDL_IOFromMem(const_cast<std::uint8_t*>(data), size);
}
#endif

bool IsSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

/// 大小写不敏感的"从 at 开始匹配 pattern"，匹配成功返回 true。
bool MatchesAt(const std::uint8_t* data, std::size_t size, std::size_t at,
               const char* pattern) {
    const std::size_t length = std::strlen(pattern);
    if (at + length > size) {
        return false;
    }
    for (std::size_t i = 0; i < length; ++i) {
        char a = static_cast<char>(data[at + i]);
        char b = pattern[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

/// 跳过 `<!-- ... -->`（返回注释结束后的位置；找不到结束标记就返回 size）。
std::size_t SkipComment(const std::uint8_t* data, std::size_t size,
                        std::size_t at) {
    if (!MatchesAt(data, size, at, "<!--")) {
        return at;
    }
    const std::size_t end = at + 4;
    if (end >= size) {
        return size;
    }
    for (std::size_t i = end; i + 2 < size; ++i) {
        if (data[i] == '-' && data[i + 1] == '-' && data[i + 2] == '>') {
            return i + 3;
        }
    }
    return size;
}

struct UnsupportedHit {
    const char* label;
    const char* pattern;
    int count;
};

}  // namespace

bool LooksLikeSvg(const std::uint8_t* data, std::size_t size) {
#if defined(INK_HAS_SDL3_IMAGE)
    if (data == nullptr || size == 0) {
        return false;
    }
    SDL_IOStream* stream = OpenBytes(data, size);
    if (stream == nullptr) {
        return false;
    }
    const bool svg = IMG_isSVG(stream);
    SDL_CloseIO(stream);
    return svg;
#else
    (void)data;
    (void)size;
    return false;
#endif
}

std::string ScanUnsupportedSvgElements(const std::uint8_t* data,
                                       std::size_t size, int* count) {
    if (count != nullptr) {
        *count = 0;
    }
    if (data == nullptr || size == 0) {
        return {};
    }

    // 顺序 = 报出来时的顺序（先标签、后属性）。`<text` 会把 `textPath` 一起算进来：
    // 两者都不渲染，合并计数反而更贴近"少了什么"。
    UnsupportedHit hits[] = {
        {"text", "<text", 0},        {"use", "<use", 0},
        {"image", "<image", 0},      {"clip-path", "clip-path", 0},
        {"mask", "mask=", 0},        {"filter", "filter=", 0},
    };

    for (std::size_t at = 0; at < size;) {
        if (IsSpace(static_cast<char>(data[at]))) {
            ++at;
            continue;
        }
        if (MatchesAt(data, size, at, "<!--")) {
            at = SkipComment(data, size, at);
            continue;
        }
        bool advanced = false;
        for (UnsupportedHit& hit : hits) {
            if (MatchesAt(data, size, at, hit.pattern)) {
                ++hit.count;
                at += std::strlen(hit.pattern);
                advanced = true;
                break;
            }
        }
        if (!advanced) {
            ++at;
        }
    }

    std::string list;
    int total = 0;
    for (const UnsupportedHit& hit : hits) {
        if (hit.count <= 0) {
            continue;
        }
        total += hit.count;
        if (!list.empty()) {
            list += "、";
        }
        list += hit.label;
        list += "\xC3\x97";  // UTF-8 的 "×"
        list += std::to_string(hit.count);
    }
    if (count != nullptr) {
        *count = total;
    }
    return list;
}

SvgRasterResult RasterizeSvg(const std::uint8_t* data, std::size_t size,
                             int boxWidth, int boxHeight, int maxPixels) {
    SvgRasterResult out;
#if !defined(INK_HAS_SDL3_IMAGE)
    (void)boxWidth;
    (void)boxHeight;
    (void)maxPixels;
    out.error = "这个构建没有接 SDL3_image（INK_HAS_SDL3_IMAGE 未定义）";
    return out;
#else
    if (data == nullptr || size == 0) {
        out.error = "SVG 字节为空";
        return out;
    }
#if defined(SDL_IMAGE_VERSION_ATLEAST) && !SDL_IMAGE_VERSION_ATLEAST(3, 0, 0)
    out.error = "SDL3_image 低于 3.0，没有 SVG 入口";
    return out;
#else
    const int targetW = std::max(boxWidth, 1);
    const int targetH = std::max(boxHeight, 1);

    // 正方形框：SDL 保持比例，长边 = 框，所以"框边长"就是"长边像素数"。
    int box = std::max(targetW, targetH);

    // 预算：contain 到正方形后面积 = box² × min(1, aspect) ≤ box²，
    // 所以 box ≤ sqrt(maxPixels) 一定够保守。
    if (maxPixels > 0) {
        const int limit = static_cast<int>(std::floor(std::sqrt(
            static_cast<double>(maxPixels))));
        if (limit > 0 && box > limit) {
            box = limit;
            out.clamped = 1;
        }
    }

    SDL_Surface* surface = nullptr;
    {
        SDL_IOStream* stream = OpenBytes(data, size);
        if (stream == nullptr) {
            out.error = SDL_GetError();
            return out;
        }
        surface = IMG_LoadSizedSVG_IO(stream, box, box);
        SDL_CloseIO(stream);
    }
    if (surface == nullptr) {
        out.error = SDL_GetError();
        return out;
    }

    // 二次解码：稿与框比例差得远时，短边会不足（例如 900×1600 的稿进 900×1800 的框）。
    // 只补一次——再补下去就是拿内存换肉眼看不出的清晰度。
    if (surface->w < targetW || surface->h < targetH) {
        const float need = std::max(
            static_cast<float>(targetW) / static_cast<float>(surface->w),
            static_cast<float>(targetH) / static_cast<float>(surface->h));
        int box2 = static_cast<int>(
            std::ceil(static_cast<float>(box) * need));
        if (maxPixels > 0) {
            const double aspect =
                static_cast<double>(surface->w) / static_cast<double>(surface->h);
            const double shortSide = std::min(1.0, aspect);
            const double limit =
                std::sqrt(static_cast<double>(maxPixels) / shortSide);
            const int cap = static_cast<int>(std::floor(limit));
            if (cap > 0 && box2 > cap) {
                box2 = cap;
            }
        }
        if (box2 > box) {
            SDL_Surface* better = nullptr;
            SDL_IOStream* stream = OpenBytes(data, size);
            if (stream != nullptr) {
                better = IMG_LoadSizedSVG_IO(stream, box2, box2);
                SDL_CloseIO(stream);
            }
            if (better != nullptr) {
                SDL_DestroySurface(surface);
                surface = better;
                out.secondPasses = 1;
            }
        }
    }

    out.surface = surface;
    out.unsupportedList =
        ScanUnsupportedSvgElements(data, size, &out.unsupportedCount);
    return out;
#endif
#endif
}

}  // namespace inking::detail
