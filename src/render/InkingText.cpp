#include <render/InkingGlyphAtlas.h>
#include <render/InkingText.h>

#include <render/InkingBatch.h>
#include <render/InkingDraw.h>
#include <detail/InkLog.h>

#include <render/InkingSvg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(INK_HAS_SDL3_TTF)
#include <SDL3_ttf/SDL_ttf.h>
#endif

#if defined(INK_HAS_SDL3_IMAGE)
#include <SDL3_image/SDL_image.h>
#endif

// 文字 / 图片纹理层的实现。四件事：**字体表**、**纹理表**、**退化画法**、
// 以及"纹理绘制是批次边界"（T-6 的提交层）。
//
// 字体表：按（路径，字号）开一次、留着复用。SDL3_ttf 的字体与渲染器无关，
// 所以它可以跨渲染器活着——只有 ReleaseTextCache 才会关掉。
//
// 纹理表：**纹理属于渲染器**，所以表里记着"这批纹理是哪台渲染器建的"。
// 渲染器换人（自检里反复建 / 销毁）时**只丢记账、不去销毁**那些句柄——
// 它们已经随渲染器死掉了，再 Destroy 就是 use-after-free。

namespace inking {

namespace {

/// 纹理表里的一项。width/height 是**设备像素**（栅格化出来就是这么大）。
struct TextureEntry {
    SDL_Texture* texture = nullptr;
    float width = 0.0f;
    float height = 0.0f;
    /// 这张纹理带不带 alpha 通道。**在创建时从 surface 的格式记下来**——
    /// 不能事后问 `SDL_GetTextureProperties`：`SDL_CreateTextureFromSurface`
    /// 建出来的纹理拿不到格式属性（返回 UNKNOWN，于是"有 alpha"被误判成"没有"，
    /// 矩形图片又走回几何批那条路，透明区重新变黑）。
    bool hasAlpha = false;
    /**
     * 这张纹理是**图片**还是**字形**（0.6.4 T-12）。
     *
     * 文字与图片共用一张纹理表，而采样模式开关只该作用于图片：不区分的话，
     * 像素项目一开 `Nearest`，所有文字都跟着变成带锯齿的（画面上只表现为
     * "字好像糙了一点"，没人会想到是图片开关干的）。
     */
    bool isImage = false;
};

/// 这一层的全部状态：函数内静态量，避开静态初始化顺序问题；UI 线程使用，不加锁。
struct TextLayer {
    SDL_Renderer* renderer = nullptr;  ///< 纹理表里的纹理属于它
    std::unordered_map<std::string, TextureEntry> textures;

    /**
     * 图片的**固有尺寸**缓存（`ProbeImageSize`，0.6.1 T-05）。
     *
     * 与 `textures` 有意分开：这里的值是纯 CPU 数据、不属于任何渲染器，
     * 所以 `ReleaseTextCache()` **不清它**（换了渲染器，图还是那么大）。
     */
    std::unordered_map<std::string, std::pair<int, int>> imageSizes;
    std::unordered_map<std::string, void*> fonts;  ///< 键 = 路径 + 字号
    /**
     * 字体链签名 → 实际命中的那条路径（`""` = 整条链全失败）。
     *
     * 键**不含字号**：一条链能不能打开与字号无关，而"全失败只报一条 WARN"
     * 要求"量一次、画一次"共用同一个结论（见 `ResolveFontChain`）。
     */
    std::unordered_map<std::string, std::string> chainHits;
    /// 回退链里"退到第 2 条及以后"才命中的次数（T-13 的观测口）。
    std::uint64_t fontFallbacks = 0;
    std::string defaultFontPath;
    bool defaultFontResolved = false;
    bool ttfInitialized = false;
    /// `TTF_Init` 失败只报一次（它每次调用都会重试，不记这笔就会每帧一条）。
    bool ttfFailureReported = false;
    std::uint64_t texturesCreated = 0;
    std::uint64_t cacheHits = 0;

    // --- SVG 纹理（0.5.1）--------------------------------------------------
    /// 真光栅化了几次（缓存命中不算）。`AGENTS.md` §6 第 40 条：能合并的地方
    /// 都要有可断言的计数，否则"缓存生效了没有"在画面上看不出来。
    std::uint64_t svgRasters = 0;
    std::uint64_t svgSecondPasses = 0;
    std::uint64_t svgCacheHits = 0;
    std::uint64_t svgClamps = 0;
    std::uint64_t svgEvictions = 0;
    std::uint64_t svgFailures = 0;
    std::uint64_t svgUnsupportedElements = 0;
    /// 每张图最多留几档（比例变化 = 新档）。3 档 × 23 MB 是可接受的上限。
    std::unordered_map<std::string, std::vector<std::string>> svgKeysByName;
    /// 失败 / 不支持元素只报一次（键 = 图名）。
    std::unordered_set<std::string> svgFailureReported;
    std::unordered_set<std::string> svgUnsupportedReported;

    /// 上一次烘字用的"1 逻辑单位 = 多少设备像素"。
    ///
    /// 比例一变（窗口缩放 / 进全屏 / 换到另一块 DPI 不同的显示器），旧字形
    /// 都是按旧比例烘的：缓存键里含设备字号，所以它们**不会命中**，只会一直
    /// 占着图集。所以比例一 crossed 就丢掉图集，让它按新比例重贴。
    float rasterRatio = 1.0f;

    // --- 字形图集（B4 后半）------------------------------------------------
    /// 图集纹理：**普通 STATIC 纹理**（由 CPU 侧的 surface 上传）。
    SDL_Texture* glyphAtlas = nullptr;
    /// CPU 侧图集：字形先 `SDL_BlitSurface` 进这里，再按矩形上传到纹理。
    SDL_Surface* glyphAtlasSurface = nullptr;
    /// 打包器：只算坐标（见 `InkingGlyphAtlas.h`）。
    GlyphAtlas glyphPacker{1024};
    /// 键 = 字体路径 + 字号 + 字形 → 它在图集里的落点与前进量。
    std::unordered_map<std::string, detail::GlyphBitmap> glyphs;
    detail::GlyphAtlasStats glyphStats{};
};

TextLayer& layer() {
    static TextLayer instance;
    return instance;
}

/**
 * 图片采样模式的**进程唯一槽位**（0.6.4 T-12）。
 *
 * 必须是 `.cpp` 里的函数内静态量（`AGENTS.md` §2 第 1 条）：写成头内 inline
 * 静态量时 DLL 与 exe 各持一份 ⇒ "库侧切换、宿主侧读"会静默不一致，
 * 而画面上只表现为"开关时灵时不灵"。
 */
detail::ImageSampling& SamplingSlot() {
    static detail::ImageSampling mode = detail::ImageSampling::Linear;
    return mode;
}

/// 采样模式 → SDL 的枚举（只有两档，别在别处再写一份三元表达式）。
SDL_ScaleMode ScaleModeOf(detail::ImageSampling mode) {
    return mode == detail::ImageSampling::Nearest ? SDL_SCALEMODE_NEAREST
                                                  : SDL_SCALEMODE_LINEAR;
}

/// UTF-8 码点数（数"非续字节"就够——只用来估算退化画法的宽度）。
std::size_t CodePointCount(const std::string& text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

/// 字号到缓存键：按"百分之一"取整，避开浮点键的精度抖动。
std::string SizeKey(float pixelSize) {
    return std::to_string(
        static_cast<long long>(std::lround(pixelSize * 100.0f)));
}

// ---------------------------------------------------------------------------
// 高清（E 需求）：文字按"设备像素"烘，按"设计单位"画
// ---------------------------------------------------------------------------
//
// 问题的根：绘制调用发生在**逻辑坐标系**里（窗口层设了
// `SDL_SetRenderLogicalPresentation(LETTERBOX)`）。同一段文字，如果按设计字号
// 烘成位图再交给渲染器放大，放大比例就是重采样比例——屏幕上必然是糊的。
//
// 做法：按 `fontSize × 比例` 设备像素烘，画的时候再除回去（画出来仍是设计尺寸）。
// 屏幕占的物理像素数不变，但纹理与像素 1:1，于是不再重采样。
//
// 比例从**渲染器自己**问（输出尺寸 / 逻辑尺寸），不新增全局槽位：
// 它和窗口层那个 `letterboxMagnification` 恒等（同一个 min），但取值点在绘制
// 现场——渲染器换了、窗口换了、进全屏了都自动跟得上，而 `render/` 依旧不认识
// `window/`（分层不倒退）。
// ---------------------------------------------------------------------------

/// 上限：3 倍以上继续加清晰度收益递减，而纹理面积按 `ratio²` 涨。
constexpr float kMaxTextRasterRatio = 3.0f;

/// SVG 单张 surface 的像素上限（16M ≈ 4096×4096）。一张 3200×1800 的稿是
/// 5.76M px（23 MB ARGB），已经够大；再往上就是拿内存换肉眼看不出的锐度。
constexpr int kMaxSvgRasterPixels = 16 * 1024 * 1024;

/// 同一张 SVG 最多保留几档（比例变化 = 新档；旧档留着只会吃内存）。
constexpr std::size_t kMaxSvgVariants = 3;

/// 渲染器当前"1 逻辑单位 = 多少设备像素"。拿不到时按 1（等于改动之前的行为）。
float RendererDeviceRatio(SDL_Renderer* renderer) {
    if (renderer == nullptr) {
        return 1.0f;
    }
    int logicalWidth = 0;
    int logicalHeight = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    if (!SDL_GetRenderLogicalPresentation(renderer, &logicalWidth, &logicalHeight,
                                          &mode)
        || logicalWidth <= 0 || logicalHeight <= 0) {
        return 1.0f;  // 没开逻辑呈现：坐标本来就是设备像素
    }
    int outputWidth = 0;
    int outputHeight = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)
        || outputWidth <= 0 || outputHeight <= 0) {
        return 1.0f;
    }
    const float scaleX =
        static_cast<float>(outputWidth) / static_cast<float>(logicalWidth);
    const float scaleY =
        static_cast<float>(outputHeight) / static_cast<float>(logicalHeight);
    const float ratio = scaleX < scaleY ? scaleX : scaleY;
    return ratio < 1.0f ? 1.0f : ratio;  // 缩小窗口时不"降清"，避免越缩越糊
}

/// 取当前烘字比例，并在比例变化时丢掉过期的字形图集。
float currentRasterRatio(SDL_Renderer* renderer) {
    const float raw = RendererDeviceRatio(renderer);
    const float ratio = raw > kMaxTextRasterRatio ? kMaxTextRasterRatio : raw;
    TextLayer& state = layer();
    if (std::fabs(ratio - state.rasterRatio) > 0.01f) {
        // 只丢图集，**不**丢纹理表：那张表里还有图片纹理（`PaintImage`），
        // 它们与比例无关，重解码一遍纯属浪费。整段文字的纹理按设备字号
        // 自己分档，多留几档的内存可以接受。
        detail::ReleaseGlyphAtlas();
        state.rasterRatio = ratio;
    }
    return ratio;
}

#if defined(INK_HAS_SDL3_TTF)

/// 找不到字体时按顺序试这几份。优先 CJK（本项目是中文项目，没有汉字的字体
/// 测不出真正关心的东西——和源项目 tests/test_ttf.cpp 用同一张表，该套件未搬入）。
const char* const kFontCandidates[] = {
    "C:/Windows/Fonts/msyh.ttc",      // 微软雅黑（含 CJK）
    "C:/Windows/Fonts/msyhbd.ttc",
    "C:/Windows/Fonts/simhei.ttf",    // 黑体
    "C:/Windows/Fonts/arial.ttf",     // 兜底：只有拉丁字符
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
};

/// 惰性 TTF_Init（成功一次就够；ReleaseTextCache 里配对 TTF_Quit）。
bool EnsureTtf() {
    TextLayer& state = layer();
    if (state.ttfInitialized) {
        return true;
    }
    if (!TTF_Init()) {
        return false;
    }
    state.ttfInitialized = true;
    return true;
}

void* OpenFontRaw(const std::string& path, float pixelSize) {
    if (path.empty() || pixelSize <= 0.0f) {
        return nullptr;
    }
    // SDL3_ttf 的 DPI 默认是 72，所以 ptsize 就是像素（不用乘 96/72）。
    return TTF_OpenFont(path.c_str(), pixelSize);
}

bool MeasureWithFontRaw(void* font, const std::string& text, int* width,
                        int* height) {
    return TTF_GetStringSize(static_cast<TTF_Font*>(font), text.c_str(),
                             text.size(), width, height);
}

SDL_Surface* RenderWithFontRaw(void* font, const std::string& text) {
    // 白字：颜色留给纹理调制（见头文件里"颜色不进缓存键"那段）。
    const SDL_Color white{255, 255, 255, 255};
    return TTF_RenderText_Blended(static_cast<TTF_Font*>(font), text.c_str(),
                                  text.size(), white);
}

void CloseFontRaw(void* font) { TTF_CloseFont(static_cast<TTF_Font*>(font)); }

#else  // !INK_HAS_SDL3_TTF：这一层只剩退化画法

bool EnsureTtf() { return false; }
void* OpenFontRaw(const std::string&, float) { return nullptr; }
bool MeasureWithFontRaw(void*, const std::string&, int*, int*) { return false; }
SDL_Surface* RenderWithFontRaw(void*, const std::string&) { return nullptr; }
void CloseFontRaw(void*) {}

#endif

/// 机器默认字体：按候选表找一份（只找一次），空串 = 这台机器没有。
///
/// **只处理"一条字体都没写"这一条**（T-13 之后配置来的路径一律走链解析，见
/// `ResolveFontChain`）。它只问"文件在不在"，能不能打开由链解析试开确认——
/// 那一步漏了的话，"文件在、TTF 打不开"会静默变成占位方块。
std::string MachineDefaultFontPath() {
    TextLayer& state = layer();
    if (state.defaultFontResolved) {
        return state.defaultFontPath;
    }
    state.defaultFontResolved = true;
#if defined(INK_HAS_SDL3_TTF)
    for (const char* candidate : kFontCandidates) {
        if (SDL_GetPathInfo(candidate, nullptr)) {
            state.defaultFontPath = candidate;
            break;
        }
    }
#endif
    if (state.defaultFontPath.empty()) {
        // 候选表全落空（或这台机器压根没编译进 SDL3_ttf）：接下来所有文字都会
        // 退化成占位方块。**必须说出来**——只说"字怎么变成方块了"，谁也没法
        // 往"字体没找到"上想。只报一次：`defaultFontResolved` 已经保证不重复。
        INK_LOG_WARN("InkingText",
                     "没找到任何机器默认字体（候选表全部落空，或未编译进 "
                     "SDL3_ttf）：文字将退化为占位方块。请在 CXXCSS 里显式写 "
                     "fontPath / text.path");
    }
    return state.defaultFontPath;
}

/**
 * @brief 拿一个字体（开过就复用）。拿不到返回 nullptr，调用方走退化画法。
 *
 * **不报错**（0.3.0 T-13 改）：回退链会拿它逐条试，故障日志由
 * `ResolveFontChain` 汇总成一条——每个候选各报一条，等于把日志刷成墙。
 * 失败也占一格缓存（"空"那一格），所以不会每帧重试开字体。
 */
void* AcquireFont(const std::string& path, float pixelSize) {
    if (pixelSize <= 0.0f) {
        return nullptr;
    }
    if (path.empty()) {
        // 空路径 = 一条字体都没有（机器默认也没找到），报错在链解析那边。
        return nullptr;
    }
    if (!EnsureTtf()) {
        TextLayer& state = layer();
        if (!state.ttfFailureReported) {
            state.ttfFailureReported = true;
            INK_LOG_WARN("InkingText",
                         std::string("TTF_Init 失败，文字将退化为占位方块：")
                             + SDL_GetError());
        }
        return nullptr;
    }

    const std::string key = path + "|" + SizeKey(pixelSize);
    TextLayer& state = layer();
    const auto found = state.fonts.find(key);
    if (found != state.fonts.end()) {
        return found->second;
    }

    void* font = OpenFontRaw(path, pixelSize);
    // 失败也记一格：免得每次绘制都去试开一次（那才是真的慢）。
    state.fonts.emplace(key, font);
    return font;
}

/// 链签名：`a\x1Fa\x1F`。**空串不可能**（链里只放非空路径），所以不会与
/// "没有链"混淆。用 `\x1F`（单元分隔符）而不是 `|`：路径里可能有 `|`，
/// 但不会有控制字符。
std::string ChainSignature(const std::vector<std::string>& chain) {
    std::string key;
    for (const std::string& path : chain) {
        key += path;
        key += '\x1F';
    }
    return key;
}

/**
 * @brief 把"请求里的字体"解析成**一条真的能打开的路径**（T-13）。
 *
 * 链 = `fontPath`（非空时排第一）+ `fontPaths`（去重、跳过空串）。**没写任何字体**
 * 时链 = 机器默认那一条——它也要**真的试开一次**：候选表只问"文件在不在"，
 * 而"在却打不开"（权限 / 被占用 / 沙箱）会静默退化成占位方块。
 *
 * 三个决定：
 *   1. **按链缓存**（`TextLayer::chainHits`），键**不含字号**：一条链能不能打开
 *      与字号无关，而"整条链全失败只报一条 WARN"要求跨字号共用同一个结论
 *      （量在 `fontSize`、画在 `deviceSize` 是两次调用，各报一条就是两条）。
 *      缓存里也存"全失败"这个结论（空串），否则每帧重试 → 每帧一条 WARN；
 *   2. **不静默换成机器默认字体**：写了字体却全打不开时，宁可画占位方块。
 *      换一份字体是"看起来正常、其实不是你要的字体"，比方块更难发现；
 *      而且那样一来 T-04 的"打不开要发声"在机器默认可用的机器上会变成
 *      0 条 WARN——测试与实现互相打架，说明这条路本来就不该走；
 *   3. `TTF_Init` 失败**不缓存**：它每次调用都会重试，缓存成"这条链没戏"
 *      会让后来 init 成功的进程永远画方块。
 */
std::string ResolveFontChain(const TextRequest& request, float pixelSize) {
    TextLayer& state = layer();
    const bool explicitFont =
        !request.fontPath.empty() || !request.fontPaths.empty();

    // 单条路径是绝大多数调用：不进循环、只拼一次签名。
    std::vector<std::string> chain;
    if (request.fontPaths.empty()) {
        if (!request.fontPath.empty()) {
            chain.push_back(request.fontPath);
        }
    } else {
        chain.reserve(request.fontPaths.size() + 1);
        if (!request.fontPath.empty()) {
            chain.push_back(request.fontPath);
        }
        for (const std::string& path : request.fontPaths) {
            if (path.empty()) {
                continue;
            }
            if (std::find(chain.begin(), chain.end(), path) != chain.end()) {
                continue;  // 重复的候选没有意义（同一份文件试两遍）
            }
            chain.push_back(path);
        }
    }

    if (chain.empty()) {
        // 没写字体：机器默认。候选表全落空时 `MachineDefaultFontPath` 已报过，
        // 这里直接放弃（否则会为"根本不存在的一条路径"再报一条）。
        const std::string machineDefault = MachineDefaultFontPath();
        if (machineDefault.empty()) {
            return std::string();
        }
        chain.push_back(machineDefault);
    }

    const std::string signature = ChainSignature(chain);
    if (const auto found = state.chainHits.find(signature);
        found != state.chainHits.end()) {
        return found->second;
    }

    if (!EnsureTtf()) {
        // 故障日志在 `AcquireFont` 里（`ttfFailureReported` 只报一次）。
        // **不写缓存**：理由见上面第 3 条。
        return std::string();
    }

    for (std::size_t index = 0; index < chain.size(); ++index) {
        if (AcquireFont(chain[index], pixelSize) != nullptr) {
            state.chainHits.emplace(signature, chain[index]);
            if (index > 0) {
                ++state.fontFallbacks;
            }
            return chain[index];
        }
    }

    state.chainHits.emplace(signature, std::string());

    char sizeText[32] = {};
    std::snprintf(sizeText, sizeof(sizeText), "%.1f",
                  static_cast<double>(pixelSize));
    std::string tried;
    for (const std::string& path : chain) {
        if (!tried.empty()) {
            tried += "、";
        }
        tried += "\"" + path + "\"";
    }
    // 一条汇总 WARN：**带全部候选路径**（可归因）、字号、以及最后一条失败原因。
    if (explicitFont) {
        INK_LOG_WARN("InkingText",
                     "字体打不开，该处文字将退化为占位方块：回退链上 " +
                         std::to_string(chain.size()) + " 条路径全部失败（"
                         + tried + "），字号 " + sizeText + "，最后一条 SDL 说："
                         + SDL_GetError());
    } else {
        // 没写字体、机器默认那一条也打不开：候选表只问"文件在不在"，
        // "在却打不开"（权限 / 被占用 / 沙箱）必须在这里说出来，
        // 否则整屏文字变方块而日志一片安静。
        INK_LOG_WARN("InkingText",
                     "机器默认字体打不开（文件存在，但 TTF 开不了），文字将退化为"
                     "占位方块：路径 " + tried + "，字号 " + sizeText
                         + "，SDL 说：" + SDL_GetError()
                         + "。请在 CXXCSS 里显式写 fontPath / text.path");
    }
    return std::string();
}

/// 渲染器换人：把纹理记账整个丢掉（**不**销毁句柄，它们已经随旧渲染器死了）。
void EnsureRenderer(SDL_Renderer* renderer) {
    TextLayer& state = layer();
    if (state.renderer == renderer) {
        return;
    }
    state.textures.clear();
    state.svgKeysByName.clear();
    // **字形图集也必须丢**：它的纹理同样绑在旧渲染器上。留着复用等于拿一张
    // 别的渲染器的纹理去画——SDL 不报错、图集统计也照旧正常（字形确实烘好了），
    // 只是**画不出来**：文字整段消失。而且它只在"换过渲染器、且烘字比例恰好
    // 没变"的那一次发生——比例一变就会走 `currentRasterRatio` 里的重建，
    // 看起来反而没事，所以这个 bug 很难靠肉眼归因。
    // 自检里踩到的正是这一条：A 渲染器上复用了前一组测试留下的图集。
    detail::ReleaseGlyphAtlas();
    state.renderer = renderer;
}

/**
 * 把一张纹理画到"组件本地 (offsetX, offsetY) 处、画出来是 drawWidth × drawHeight"
 * 的位置上，并过一次**变换通道**（平移 / 旋转 / 缩放，绕组件中心）。
 *
 * 文字与图片**共用这一段**：它的正负号与旋转中心已经错过一次
 * （`AGENTS.md` §6 第 45 条：SDL_RenderTextureRotated 的 center 是相对 dst 的），
 * 抄第二遍必然再错一次。
 */
void DrawTextureTransformed(SDL_Renderer* renderer, SDL_Texture* texture,
                            float offsetX, float offsetY, float drawWidth,
                            float drawHeight, int width, int height, float mag,
                            float pixelX, float pixelY,
                            const TransformSpec& transform,
                            const SDL_FRect* source = nullptr) {
    const float deviceW = static_cast<float>(width) * mag;
    const float deviceH = static_cast<float>(height) * mag;
    const float centerX = pixelX + deviceW * 0.5f;
    const float centerY = pixelY + deviceH * 0.5f;
    const float translateX = transform.translateX * mag;
    const float translateY = transform.translateY * mag;
    const float scale = transform.scale;

    const float localLeft = offsetX * mag - deviceW * 0.5f;
    const float localTop = offsetY * mag - deviceH * 0.5f;

    SDL_FRect destination;
    destination.w = drawWidth * scale;
    destination.h = drawHeight * scale;
    destination.x = centerX + localLeft * scale + translateX;
    destination.y = centerY + localTop * scale + translateY;

    // 祖先子树变换（T-7）：容器动了 / 转了，文字与图片也要跟着。
    // 相似仿射作用在"一张已经摆好、可能还转了角度的矩形"上，结果仍是相似形：
    // 尺寸乘上缩放、角度加上旋转、中心过一遍仿射——三样一起改就等价了。
    float angle = transform.rotate;
    float pivotX = -localLeft * scale;
    float pivotY = -localTop * scale;
    const Affine ancestor = detail::currentAncestorAffine();
    if (!ancestor.IsIdentity()) {
        const float factor =
            std::sqrt(ancestor.a * ancestor.a + ancestor.b * ancestor.b);
        const Point2 moved = ApplyAffine(
            ancestor, destination.x + destination.w * 0.5f,
            destination.y + destination.h * 0.5f);
        destination.w *= factor;
        destination.h *= factor;
        destination.x = moved.x - destination.w * 0.5f;
        destination.y = moved.y - destination.h * 0.5f;
        angle += std::atan2(ancestor.b, ancestor.a)
               / detail::kTransformDegToRad;
        pivotX *= factor;
        pivotY *= factor;
    }

    const SDL_FPoint pivot{pivotX, pivotY};
    // `source` 为空 = 画整张纹理（整段文字那条路）；非空 = 只取图集里这一格
    // （逐字形那条路）。两条路共用同一套变换与祖先子树仿射，位置才对得上。
    SDL_RenderTextureRotated(renderer, texture, source, &destination,
                             angle, &pivot, SDL_FLIP_NONE);
}

/// 把一张 surface 变成纹理并记进表里（文字与图片共用）。失败返回 nullptr。
///
/// @param isImage 这张纹理是图片吗（0.6.4 T-12）。决定两件事：建纹理时按当前
///        采样模式设（图片）/ 恒 `LINEAR`（字形），以及
///        `SetImageSampling` 切换时要不要重设它。
TextureEntry* InsertTexture(SDL_Renderer* renderer, const std::string& key,
                            SDL_Surface* surface, bool isImage) {
    TextureEntry entry;
    entry.width = static_cast<float>(surface->w);
    entry.height = static_cast<float>(surface->h);
    entry.isImage = isImage;
    const SDL_PixelFormatDetails* surfaceDetails =
        SDL_GetPixelFormatDetails(surface->format);
    entry.hasAlpha =
        surfaceDetails != nullptr && surfaceDetails->Amask != 0u;
    entry.texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (entry.texture == nullptr) {
        return nullptr;
    }
    SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
    // **在入表之前设完**：读者不必担心"表里有一张还没设过采样模式的纹理"。
    SDL_SetTextureScaleMode(
        entry.texture,
        isImage ? ScaleModeOf(SamplingSlot()) : SDL_SCALEMODE_LINEAR);

    TextLayer& state = layer();
    ++state.texturesCreated;
    auto inserted = state.textures.emplace(key, entry);
    return &inserted.first->second;
}

/**
 * 取（或烘）一段文字的纹理。
 *
 * 键 = 字体路径 + 设备像素字号 + 文本。**颜色不在键里**：纹理烘成白字，
 * 画的时候用颜色调制上色。返回 nullptr 表示"这台机器没有可用字体"。
 */
TextureEntry* AcquireTexture(SDL_Renderer* renderer, const TextRequest& request,
                             const std::string& fontPath, float deviceSize) {
    void* font = AcquireFont(fontPath, deviceSize);
    if (font == nullptr) {
        return nullptr;
    }

    const std::string key =
        fontPath + "|" + SizeKey(deviceSize) + "|" + request.text;
    TextLayer& state = layer();
    auto found = state.textures.find(key);
    if (found != state.textures.end()) {
        ++state.cacheHits;
        return &found->second;
    }

    SDL_Surface* surface = RenderWithFontRaw(font, request.text);
    if (surface == nullptr) {
        return nullptr;
    }
    TextureEntry* entry = InsertTexture(renderer, key, surface, false);
    SDL_DestroySurface(surface);
    return entry;
}

#if defined(INK_HAS_SDL3_IMAGE)
SDL_Surface* DecodeImageRaw(const ImageBytes& image) {
    // 只读：SDL 不会改这块内存，const_cast 只是为了对上 SDL_IOFromMem 的签名。
    SDL_IOStream* stream =
        SDL_IOFromMem(const_cast<std::uint8_t*>(image.data), image.size);
    if (stream == nullptr) {
        return nullptr;
    }
    return IMG_Load_IO(stream, true);  // true = 读完顺手把流关掉
}

/**
 * 把"贴着不透明区域的那几圈透明像素"的颜色**渗出去**（RGB 借邻近像素的颜色，
 * alpha 一个字节都不改）。
 *
 * 为什么需要：透明底的 PNG 在"图外"那圈像素常常是 **RGB=0、A=0**（黑且透明）。
 * 直通 alpha 的纹理交给硬件做双线性采样时，会把那圈黑按权重混进边缘像素——于是
 * 每个立绘都描出一圈黑边（实测：真窗口里"每个立绘都有黑边，顶部还有一条"；
 * 而 `--shot` 走的 software 后端是逐像素算的，看不出来，出图一直是干净的）。
 *
 * 为什么不是"预乘 alpha"：预乘要连混合模式一起换成 `BLEND_PREMULTIPLIED`，
 * 那会改动整条纹理路径的语义（自检里的像素对账当场就红了）。只把**看不见的**
 * 透明像素的 RGB 换个值，肉眼与像素对账都不受影响，还顺手把黑边治了。
 *
 * 返回 nullptr = 这张图本来就没有 alpha（不透明），不需要处理。
 */
SDL_Surface* BleedTransparentEdges(SDL_Surface* source) {
    const SDL_PixelFormatDetails* sourceDetails =
        source != nullptr ? SDL_GetPixelFormatDetails(source->format) : nullptr;
    if (source == nullptr || sourceDetails == nullptr
        || sourceDetails->Amask == 0u) {
        return nullptr;
    }
    SDL_Surface* copy = SDL_ConvertSurface(source, SDL_PIXELFORMAT_ARGB8888);
    if (copy == nullptr) {
        return nullptr;
    }
    const SDL_PixelFormatDetails* details =
        SDL_GetPixelFormatDetails(copy->format);
    if (details == nullptr || !SDL_LockSurface(copy)) {
        SDL_DestroySurface(copy);
        return nullptr;
    }
    const int width = copy->w;
    const int height = copy->h;
    auto* pixels = static_cast<std::uint32_t*>(copy->pixels);
    std::vector<std::uint32_t> front(pixels, pixels + width * height);
    std::vector<std::uint32_t> back = front;

    // 两轮 3×3 传播：双线性采样最多看到边界外一个像素，两圈足够。
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int index = y * width + x;
                std::uint8_t r = 0;
                std::uint8_t g = 0;
                std::uint8_t b = 0;
                std::uint8_t a = 0;
                SDL_GetRGBA(front[static_cast<std::size_t>(index)], details,
                            nullptr, &r, &g, &b, &a);
                if (a != 0u) {
                    continue;  // 自己有颜色（或本身不透明）：不动
                }
                int sumR = 0;
                int sumG = 0;
                int sumB = 0;
                int count = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                            continue;
                        }
                        std::uint8_t nr = 0;
                        std::uint8_t ng = 0;
                        std::uint8_t nb = 0;
                        std::uint8_t na = 0;
                        SDL_GetRGBA(front[static_cast<std::size_t>(ny * width + nx)],
                                    details, nullptr, &nr, &ng, &nb, &na);
                        if (na == 0u) {
                            continue;  // 邻居也还没颜色
                        }
                        sumR += nr;
                        sumG += ng;
                        sumB += nb;
                        ++count;
                    }
                }
                if (count == 0) {
                    back[static_cast<std::size_t>(index)] =
                        front[static_cast<std::size_t>(index)];
                    continue;
                }
                // 颜色取邻居平均，**alpha 仍然是 0**：像素依然完全透明，
                // 只是它旁边那圈不再是黑的。
                back[static_cast<std::size_t>(index)] = SDL_MapRGBA(
                    details, nullptr, static_cast<std::uint8_t>(sumR / count),
                    static_cast<std::uint8_t>(sumG / count),
                    static_cast<std::uint8_t>(sumB / count), 0u);
            }
        }
        front.swap(back);
    }

    for (int i = 0; i < width * height; ++i) {
        pixels[i] = front[static_cast<std::size_t>(i)];
    }
    SDL_UnlockSurface(copy);
    return copy;
}
#else
SDL_Surface* DecodeImageRaw(const ImageBytes&) { return nullptr; }

SDL_Surface* BleedTransparentEdges(SDL_Surface*) { return nullptr; }
#endif

/// 取（或解码）一张图片的纹理。返回 nullptr 表示"这台机器解不出来"；
/// SVG 那条路的失败原因写进 `error`（调用方要发 WARN）。
TextureEntry* AcquireImageTexture(SDL_Renderer* renderer,
                                  const ImageBytes& image, int boxWidth,
                                  int boxHeight, bool svg, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    TextLayer& state = layer();

    if (svg) {
        // 键 = 名字 + 字节数 + 目标框边长：**同一个文件被多个组件引用**时共用，
        // 比例变了就换一档（旧档留在表里，最多 `kMaxSvgVariants` 档）。
        const int box = std::max(boxWidth, boxHeight);
        const std::string key = "svg|" + image.name + "|"
            + std::to_string(image.size) + "|" + std::to_string(box);
        if (const auto found = state.textures.find(key);
            found != state.textures.end()) {
            ++state.svgCacheHits;
            ++state.cacheHits;
            return &found->second;
        }

        detail::SvgRasterResult raster = detail::RasterizeSvg(
            image.data, image.size, boxWidth, boxHeight, kMaxSvgRasterPixels);
        if (raster.surface == nullptr) {
            if (error != nullptr) {
                *error = raster.error;
            }
            ++state.svgFailures;
            if (state.svgFailureReported.insert(image.name).second) {
                INK_LOG_WARN("InkingText",
                             "SVG 解码失败：" + image.name + "（"
                                 + std::to_string(image.size) + " 字节），SDL 说："
                                 + raster.error + "；这张图退回按颜色填");
            }
            return nullptr;
        }
        ++state.svgRasters;
        state.svgSecondPasses +=
            static_cast<std::uint64_t>(raster.secondPasses);
        if (raster.clamped != 0) {
            ++state.svgClamps;
        }
        state.svgUnsupportedElements +=
            static_cast<std::uint64_t>(raster.unsupportedCount);
        if (!raster.unsupportedList.empty()
            && state.svgUnsupportedReported.insert(image.name).second) {
            INK_LOG_WARN("InkingText",
                         "SVG 里这些标签 / 属性不会被渲染："
                             + raster.unsupportedList + "（" + image.name
                             + "）；导出时请把文字转曲、展开 use");
        }

        // 档位淘汰：同一张图最多 `kMaxSvgVariants` 档（比例变化 = 新档）。
        auto& keys = state.svgKeysByName[image.name];
        if (keys.size() >= kMaxSvgVariants) {
            const std::string victim = keys.front();
            keys.erase(keys.begin());
            if (const auto it = state.textures.find(victim);
                it != state.textures.end()) {
                if (it->second.texture != nullptr) {
                    SDL_DestroyTexture(it->second.texture);
                }
                state.textures.erase(it);
                ++state.svgEvictions;
            }
        }

        // 矢量稿不做 `BleedTransparentEdges`：抗锯齿边缘通常不是 RGB=0，
        // 而两轮全表面 3×3 遍历对 3200×1800 比光栅化本身还贵（先量再改）。
        TextureEntry* entry =
            InsertTexture(renderer, key, raster.surface, true);
        SDL_DestroySurface(raster.surface);
        if (entry == nullptr) {
            return nullptr;
        }
        keys.push_back(key);
        return entry;
    }

    // 位图老路：键 / 解码 / 尺寸与 0.5.0 完全一致。
    // 键用（路径 + 字节数）：同一个文件被多个按钮引用时共用一张纹理。
    const std::string key =
        "img|" + image.name + "|" + std::to_string(image.size);
    auto found = state.textures.find(key);
    if (found != state.textures.end()) {
        ++state.cacheHits;
        return &found->second;
    }

    SDL_Surface* surface = DecodeImageRaw(image);
    if (surface == nullptr) {
        return nullptr;
    }
    // 把"贴着不透明区域的那圈透明像素"的颜色渗出去，免得硬件采样时描出黑边。
    SDL_Surface* bled = BleedTransparentEdges(surface);
    TextureEntry* entry = InsertTexture(
        renderer, key, bled != nullptr ? bled : surface, true);
    if (bled != nullptr) {
        SDL_DestroySurface(bled);
    }
    SDL_DestroySurface(surface);
    return entry;
}

/// 退化画法：一块"字号 × 码点数"的实心方块（改动之前的占位块）。
/// 注意它**不跟变换走**——这条路只在没接 TTF / 没字体时出现，和以前一样。
void PaintPlaceholder(SDL_Renderer* renderer, const TextRequest& request,
                      float deviceSize, float pixelX, float pixelY,
                      float magnification) {
    const float width =
        deviceSize * static_cast<float>(CodePointCount(request.text));
    const float height = deviceSize;
    const SDL_FRect rect{pixelX + request.offsetX * magnification,
                         pixelY + request.offsetY * magnification, width,
                         height};
    // 这是**直接绘制**（走 SDL_RenderFillRect，绕开几何批）：必须先刷批，
    // 否则这一笔会被后面刷出去的批次盖住（实测踩过一次）。
    detail::NoteImmediateDraw();
    detail::setDrawColor(
        renderer,
        ColorAlpha(request.color) == 0u ? 0xFFFFFFFFu : request.color);
    SDL_RenderFillRect(renderer, &rect);
}

}  // namespace

TextMetrics MeasureText(const TextRequest& request) {
    TextMetrics metrics;
    if (request.Empty() || request.fontSize <= 0.0f) {
        return metrics;
    }

    const std::string fontPath = ResolveFontChain(request, request.fontSize);
    if (void* font = AcquireFont(fontPath, request.fontSize)) {
        int width = 0;
        int height = 0;
        if (MeasureWithFontRaw(font, request.text, &width, &height)) {
            metrics.width = static_cast<float>(width);
            metrics.height = static_cast<float>(height);
            return metrics;
        }
    }

    // 没字体：给出与退化画法一致的估算（宽 = 字号 × 码点数）。
    metrics.width =
        request.fontSize * static_cast<float>(CodePointCount(request.text));
    metrics.height = request.fontSize;
    return metrics;
}

namespace detail {

void SetImageSampling(ImageSampling mode) noexcept {
    SamplingSlot() = mode;
    const SDL_ScaleMode scale = ScaleModeOf(mode);
    for (auto& pair : layer().textures) {
        TextureEntry& entry = pair.second;
        // 只碰图片：字形纹理跟着改会变成带锯齿的字，而画面上只表现为"字糙了"。
        if (!entry.isImage || entry.texture == nullptr) {
            continue;
        }
        SDL_SetTextureScaleMode(entry.texture, scale);
    }
}

ImageSampling GetImageSampling() noexcept { return SamplingSlot(); }

TextCacheStats TextCacheStatsSnapshot() noexcept {
    const TextLayer& state = layer();
    TextCacheStats stats;
    stats.texturesCreated = state.texturesCreated;
    stats.cacheHits = state.cacheHits;
    stats.fontFallbacks = state.fontFallbacks;
    stats.svgRasters = state.svgRasters;
    stats.svgSecondPasses = state.svgSecondPasses;
    stats.svgCacheHits = state.svgCacheHits;
    stats.svgClamps = state.svgClamps;
    stats.svgEvictions = state.svgEvictions;
    stats.svgFailures = state.svgFailures;
    stats.svgUnsupportedElements = state.svgUnsupportedElements;
    return stats;
}

// ---------------------------------------------------------------------------
// 字形图集（B4 后半）：字符 → 图集里的一格 + 前进量
// ---------------------------------------------------------------------------

GlyphBitmap AcquireGlyph(SDL_Renderer* renderer, const std::string& fontPath,
                         float deviceSize, std::string_view glyph) {
    GlyphBitmap result;
    if (renderer == nullptr || glyph.empty() || deviceSize < 1.0f) {
        return result;
    }
    EnsureRenderer(renderer);
    TextLayer& state = layer();

    // `fontPath` 是**已经解析过的实际路径**（`PaintText` 解析一次、整段复用）。
    // 这里不再解析链：既省一遍签名拼接，也保证"图集键里的字体 = 真的用来烘字
    // 的那份字体"——键里放配置路径的话，改了链顺序还会从旧图集里取到旧字形。
    const std::string& resolvedPath = fontPath;
    if (resolvedPath.empty()) {
        ++state.glyphStats.failed;  // 没字体：调用方退回整段渲染（那边画方块）
        return result;
    }

    // 键 = 字体 + 字号 + 字形：**同一个字在不同字符串里只栅格化一次**，
    // 这正是"整段一张纹理"换成"逐字形"要省下来的东西。
    std::string key = resolvedPath;
    key += '|';
    key += std::to_string(static_cast<int>(deviceSize + 0.5f));
    key += '|';
    key.append(glyph);

    if (const auto found = state.glyphs.find(key);
        found != state.glyphs.end()) {
        ++state.glyphStats.reused;
        return found->second;
    }

    void* font = AcquireFont(resolvedPath, deviceSize);
    if (font == nullptr) {
        ++state.glyphStats.failed;  // 没字体：调用方退回整段渲染
        return result;
    }

    const std::string single(glyph);
    int textWidth = 0;
    int textHeight = 0;
    if (!MeasureWithFontRaw(font, single, &textWidth, &textHeight)) {
        ++state.glyphStats.failed;
        return result;
    }
    SDL_Surface* surface = RenderWithFontRaw(font, single);
    if (surface == nullptr) {
        ++state.glyphStats.failed;
        return result;
    }
    ++state.glyphStats.rasterized;

    // 图集：**CPU 侧 surface + 一张普通纹理**。
    //
    // 为什么不用 `SDL_TEXTUREACCESS_TARGET` 直接往上画（那样代码更短）：实测在
    // software 后端里"TARGET 纹理当源"**画不出东西**——图集内容是对的（读回那一格
    // 有像素），但 `SDL_RenderTextureRotated` 取它就是空，文字整段消失。改成
    // "CPU blit 进 surface + 按矩形 `SDL_UpdateTexture` 上传"，走的是最普通的
    // 那条纹理路径，稳；顺带没有"切完渲染目标忘了恢复"那一整类问题。
    if (state.glyphAtlasSurface == nullptr) {
        state.glyphAtlasSurface = SDL_CreateSurface(
            state.glyphPacker.Size(), state.glyphPacker.Size(),
            SDL_PIXELFORMAT_ARGB8888);
        if (state.glyphAtlasSurface == nullptr) {
            SDL_DestroySurface(surface);
            ++state.glyphStats.failed;
            return result;
        }
        // 底子必须是"什么都没有"：残留像素会顺着双线性采样渗进字形边缘。
        SDL_FillSurfaceRect(state.glyphAtlasSurface, nullptr, 0);
    }

    const GlyphAtlas::Slot slot =
        state.glyphPacker.Insert(surface->w, surface->h);
    if (!slot.valid) {
        // 图集满了：**不硬塞**，让调用方退回整段渲染——字不能消失。
        SDL_DestroySurface(surface);
        ++state.glyphStats.failed;
        return result;
    }

    // 贴进图集：CPU blit + 按矩形上传（见上面那段注释）。
    SDL_Rect cell{slot.x, slot.y, slot.width, slot.height};
    bool uploaded = false;
    if (SDL_BlitSurface(surface, nullptr, state.glyphAtlasSurface, &cell)) {
        if (state.glyphAtlas == nullptr) {
            state.glyphAtlas =
                SDL_CreateTextureFromSurface(renderer, state.glyphAtlasSurface);
            if (state.glyphAtlas != nullptr) {
                SDL_SetTextureBlendMode(state.glyphAtlas, SDL_BLENDMODE_BLEND);
                uploaded = true;
            }
        } else {
            // 只更新这一格：整张 1024² 每贴一个字就传 4MB 太贵。
            const int pitch = state.glyphAtlasSurface->pitch;
            const auto* base = static_cast<const std::uint8_t*>(
                state.glyphAtlasSurface->pixels);
            uploaded = SDL_UpdateTexture(
                state.glyphAtlas, &cell,
                base + static_cast<std::size_t>(cell.y) * pitch
                    + static_cast<std::size_t>(cell.x) * 4u,
                pitch);
        }
    }
    SDL_DestroySurface(surface);
    if (!uploaded) {
        ++state.glyphStats.failed;
        return result;
    }
    ++state.glyphStats.uploaded;

    result.valid = true;
    result.atlasX = slot.x;
    result.atlasY = slot.y;
    result.width = slot.width;
    result.height = slot.height;
    // 前进量：**单个字形**的宽度就是它的前进量（一个字形没有字距可调）。
    // 单位是设备像素（字体是按 `deviceSize` 打开的）。
    result.advance = static_cast<float>(textWidth);

    state.glyphs.emplace(std::move(key), result);
    return result;
}

GlyphAtlasStats GlyphAtlasSnapshot() {
    GlyphAtlasStats stats = layer().glyphStats;
    stats.slots = layer().glyphPacker.SlotCount();
    return stats;
}

SDL_Texture* GlyphAtlasTexture() { return layer().glyphAtlas; }

/// 逐字形那条路的开关（默认开；见头文件里"存在的理由是对账"那段）。
static bool g_glyphAtlasEnabled = true;

void SetGlyphAtlasEnabled(bool enabled) { g_glyphAtlasEnabled = enabled; }

bool IsGlyphAtlasEnabled() { return g_glyphAtlasEnabled; }

void ReleaseGlyphAtlas() {
    TextLayer& state = layer();
    if (state.glyphAtlas != nullptr) {
        SDL_DestroyTexture(state.glyphAtlas);
        state.glyphAtlas = nullptr;
    }
    if (state.glyphAtlasSurface != nullptr) {
        SDL_DestroySurface(state.glyphAtlasSurface);
        state.glyphAtlasSurface = nullptr;
    }
    state.glyphPacker.Reset();
    state.glyphs.clear();
    state.glyphStats = GlyphAtlasStats{};
}

/// 按 `\n` 切行（`\r` 直接丢掉，于是 `\r\n` 也认）：逐字形那条路要自己处理换行
/// （整段渲染是 `TTF_RenderText_Blended` 自己换的）。
std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (const char ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    lines.push_back(current);
    return lines;
}

/**
 * 逐字形画（B4 后半）：一行行排、一个字一个字从**图集**里取。
 *
 * 与整段那条路的取舍：整段是"这段文字一张纹理"（同一个字在不同字符串里重复烘），
 * 这里是"每个字形一份位图、跨字符串复用"（图集）。**取不到任何一个字形就返回
 * false**，由调用方落回整段渲染——半张脸比慢一点糟得多。
 */
bool PaintTextGlyphByGlyph(SDL_Renderer* renderer, const TextRequest& request,
                           const std::string& fontPath, float deviceSize,
                           float rasterScale, float ratio, int width,
                           int height, float mag, float pixelX, float pixelY,
                           const TransformSpec& transform) {
    void* font = AcquireFont(fontPath, deviceSize);
    if (font == nullptr) {
        return false;  // 没字体：交给整段那条路（它会走退化画法）
    }

    // 行高：量一个字的**行高**。单字符渲染出来的位图就是整行高（自带基线以上的
    // 空白），所以逐字形不需要再算 ascent——这也是两条路行距一致的原因。
    int probeWidth = 0;
    int lineHeight = 0;
    if (!MeasureWithFontRaw(font, "M", &probeWidth, &lineHeight)
        || lineHeight <= 0) {
        return false;
    }

    const GlyphMeasure measure = [font](std::string_view glyph) {
        int w = 0;
        int h = 0;
        if (!MeasureWithFontRaw(font, std::string(glyph), &w, &h)) {
            return 0.0f;
        }
        return static_cast<float>(w);
    };

    const std::vector<std::string> lines = SplitLines(request.text);
    struct Cell {
        GlyphBitmap bitmap;
        float x = 0.0f;
        float y = 0.0f;
    };
    std::vector<Cell> cells;
    cells.reserve(request.text.size());

    // ① **先把所有字形取齐**：`AcquireGlyph` 可能临时把渲染目标切到图集纹理，
    //    那件事必须在开始画之前做完——画到一半切目标会把正在缓冲的笔画搞乱。
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const float lineTop =
            static_cast<float>(index) * static_cast<float>(lineHeight);
        const std::vector<GlyphPlacement> placements =
            LayOutGlyphs(lines[index], 0.0f, measure);
        for (const GlyphPlacement& placement : placements) {
            const GlyphBitmap bitmap = AcquireGlyph(
                renderer, fontPath, deviceSize,
                std::string_view(lines[index])
                    .substr(placement.offset, placement.length));
            if (!bitmap.valid) {
                return false;  // 有一个取不到就整段回退
            }
            cells.push_back(Cell{bitmap, placement.x, lineTop});
        }
    }
    if (cells.empty()) {
        return true;  // 空文本：算"处理过了"，不用再走整段那条路
    }

    // ② 图集纹理 + 颜色调制：整段同色，设置一次就够（和整段那条路一致）。
    SDL_Texture* atlas = layer().glyphAtlas;
    if (atlas == nullptr) {
        return false;
    }
    SDL_SetTextureColorMod(atlas, ColorRed(request.color),
                           ColorGreen(request.color), ColorBlue(request.color));
    SDL_SetTextureAlphaMod(atlas, ColorAlpha(request.color));

    // 纹理绘制是**批次边界**（T-6 / B4 前半）：和整段那条路一样先刷几何。
    NoteImmediateDraw();

    for (const Cell& cell : cells) {
        const SDL_FRect source{static_cast<float>(cell.bitmap.atlasX),
                               static_cast<float>(cell.bitmap.atlasY),
                               static_cast<float>(cell.bitmap.width),
                               static_cast<float>(cell.bitmap.height)};
        // 字形落点是**设备像素**（排版是按 deviceSize 量的），而
        // `DrawTextureTransformed` 收的是设计坐标偏移（它内部再乘倍率）⇒ 除回去。
        // 除的道具是 `rasterScale`（= mag × 设备比例），不是 mag：
        // 高清之后位图比设计尺寸大 `ratio` 倍，落点也跟着大 `ratio` 倍，
        // 而画出来的尺寸要**除回** `ratio`（见下面两个参数）。
        DrawTextureTransformed(renderer, atlas,
                               request.offsetX + cell.x / rasterScale,
                               request.offsetY + cell.y / rasterScale,
                               static_cast<float>(cell.bitmap.width) / ratio,
                               static_cast<float>(cell.bitmap.height) / ratio,
                               width, height, mag, pixelX, pixelY, transform,
                               &source);
    }
    return true;
}

void PaintText(SDL_Renderer* renderer, const TextRequest& request, int width,
               int height, float magnification, float pixelX, float pixelY,
               const TransformSpec& transform) {
    if (renderer == nullptr || request.Empty() || width <= 0 || height <= 0) {
        return;
    }
    const float mag = magnification > 0.0f ? magnification : 1.0f;
    EnsureRenderer(renderer);

    // 高清（E）：**烘字用设备像素，落点用设计单位**。
    //   ratio       —— 渲染器自己报的"1 逻辑单位 = 多少设备像素"；
    //   rasterScale —— 位图相对设计尺寸的倍数（= mag × ratio）。
    // 于是 `deviceSize` 是位图实际要烘多大，而画出来的尺寸要**除回 ratio**
    // （见下面几处 `/ ratio`）——屏幕上占的物理像素一样，只是不再重采样。
    const float ratio = currentRasterRatio(renderer);
    const float rasterScale = mag * ratio;
    const float deviceSize = request.fontSize * rasterScale;
    if (deviceSize < 1.0f) {
        return;  // 小到看不清，不画
    }

    // 字体只解析一次（T-13）：逐字形那条路与整段那条路共用**同一个实际路径**，
    // 于是"图集键 / 纹理键 / 真正开的那份字体"永远是同一份。
    const std::string fontPath = ResolveFontChain(request, deviceSize);

    // B4 后半：优先走**逐字形**那条路（图集里一格一个字形，同一个字跨字符串复用）。
    // 取不到字形（没字体 / 图集满）时它返回 false，落到下面那条整段渲染上。
    if (g_glyphAtlasEnabled
        && PaintTextGlyphByGlyph(renderer, request, fontPath, deviceSize,
                                 rasterScale, ratio, width, height, mag, pixelX,
                                 pixelY, transform)) {
        return;
    }

    TextureEntry* entry = AcquireTexture(renderer, request, fontPath, deviceSize);
    if (entry == nullptr) {
        // 退化方块按**设计尺寸**画（它不烘纹理，跟比例无关）。
        PaintPlaceholder(renderer, request, request.fontSize * mag, pixelX, pixelY,
                         mag);
        return;
    }
    // 兜底：渲染器换代（而调用方又忘了 Release）时，句柄可能已经无效。
    float probeWidth = 0.0f;
    float probeHeight = 0.0f;
    if (!SDL_GetTextureSize(entry->texture, &probeWidth, &probeHeight)) {
        return;
    }

    // 颜色调制：纹理是白字，这里上色。alpha 也走调制（过渡期间文字跟着淡）。
    SDL_SetTextureColorMod(entry->texture, ColorRed(request.color),
                           ColorGreen(request.color), ColorBlue(request.color));
    SDL_SetTextureAlphaMod(entry->texture, ColorAlpha(request.color));

    // 纹理绘制是**批次边界**：先把待提交的几何刷出去，顺序才不会乱（T-6）。
    NoteImmediateDraw();
    // 纹理是 `deviceSize` 大（比设计字号大 ratio 倍），画出来要除回 ratio——
    // 这一步就是"高清但不改变布局"的全部秘密。
    DrawTextureTransformed(renderer, entry->texture, request.offsetX,
                           request.offsetY, entry->width / ratio,
                           entry->height / ratio, width, height, mag, pixelX,
                           pixelY, transform);
}

bool PaintImage(SDL_Renderer* renderer, const ImageBytes& image,
                const ShapeSpec& shape, int width, int height,
                float magnification, float pixelX, float pixelY,
                const TransformSpec& transform, ImageFit fit,
                std::uint32_t tintColor, const ShapeBounds& srcRect) {
    if (renderer == nullptr || image.Empty() || width <= 0 || height <= 0) {
        return false;
    }
    const float mag = magnification > 0.0f ? magnification : 1.0f;
    EnsureRenderer(renderer);

    // SVG 是一种**图片格式**，不是新外观类型：在这里按字节嗅探一次，
    // 决定"按目标像素光栅化"还是"按原始尺寸解码"。顺序不能反——
    // 先按固有尺寸解码再放大，就是位图放大（糊）。
    const bool svg = LooksLikeSvg(image.data, image.size);
    int boxWidth = 0;
    int boxHeight = 0;
    if (svg) {
        // 与文字高清同源：`mag × 渲染器报的设备比例`。目标框只影响**解码像素**，
        // 不改变绘制矩形（`fit` 三模式照旧）。
        const float ratio = currentRasterRatio(renderer);
        const float scale = mag * ratio;
        boxWidth = std::max(
            1, static_cast<int>(std::lround(static_cast<float>(width) * scale)));
        boxHeight = std::max(
            1, static_cast<int>(std::lround(static_cast<float>(height) * scale)));
    }

    std::string decodeError;
    TextureEntry* entry = AcquireImageTexture(renderer, image, boxWidth,
                                              boxHeight, svg, &decodeError);
    if (entry == nullptr) {
        return false;  // 没接 SDL3_image / 字节坏了：调用方退回按颜色填
    }
    float textureWidth = 0.0f;
    float textureHeight = 0.0f;
    if (!SDL_GetTextureSize(entry->texture, &textureWidth, &textureHeight)) {
        return false;
    }

    // 适配模式那点几何是**纯计算**（`ImageFitSpec`）：框、图、模式 → 绘制区域与 UV。
    // 这里只负责把三样尺寸凑齐（都换算到设备像素）。
    ImageFitSpec spec;
    spec.fit = fit;
    spec.frameWidth = static_cast<float>(width) * mag;
    spec.frameHeight = static_cast<float>(height) * mag;
    spec.imageWidth = textureWidth;
    spec.imageHeight = textureHeight;
    // 图集那一刀（0.6.1 T-04）：**缩放比例仍按整张纹理算**，`srcRect` 只说取哪块。
    // SVG 那条路不切格（纹理是按组件框光栅化的，切开没有意义），所以全零照旧。
    spec.srcRect = srcRect;

    // 真绘制交给形状层：**同一份形状网格**（轮廓 + 羽化）带上 UV 与纹理，
    // 于是"按形状裁剪"这件事不需要纹理层再实现一遍。
    fillShapeTextured(renderer, shape, width, height, mag, pixelX, pixelY,
                      transform, entry->texture, entry->hasAlpha, spec,
                      tintColor);
    return true;
}

bool ProbeImageSize(const ImageBytes& image, int* width, int* height) {
    if (width != nullptr) {
        *width = 0;
    }
    if (height != nullptr) {
        *height = 0;
    }
    if (image.Empty()) {
        return false;
    }

    // 缓存键与纹理表同一套口径（名字 + 字节数）：同一张图集被 N 个 sprite 引用时
    // 只解一次。"构造期量尺寸"这笔账只有在**每张图一次**的时候才划算。
    const std::string key =
        "size|" + image.name + "|" + std::to_string(image.size);
    const auto found = layer().imageSizes.find(key);
    if (found != layer().imageSizes.end()) {
        if (width != nullptr) {
            *width = found->second.first;
        }
        if (height != nullptr) {
            *height = found->second.second;
        }
        return true;
    }

    SDL_Surface* surface = DecodeImageRaw(image);
    if (surface == nullptr) {
        return false;  // 没接 SDL3_image / 字节坏了 / 是 SVG：调用方跳过校验
    }
    const int surfaceWidth = surface->w;
    const int surfaceHeight = surface->h;
    SDL_DestroySurface(surface);
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        return false;
    }

    layer().imageSizes.emplace(key, std::make_pair(surfaceWidth, surfaceHeight));
    if (width != nullptr) {
        *width = surfaceWidth;
    }
    if (height != nullptr) {
        *height = surfaceHeight;
    }
    return true;
}

void ReleaseTextCache() noexcept {    TextLayer& state = layer();
    // 图集与字形表也是"属于渲染器"的东西：**先**放掉（§6 第 46 条）。
    ReleaseGlyphAtlas();
    for (auto& item : state.textures) {
        if (item.second.texture != nullptr) {
            SDL_DestroyTexture(item.second.texture);
        }
    }
    state.textures.clear();
    state.svgKeysByName.clear();
    state.renderer = nullptr;

    for (auto& item : state.fonts) {
        if (item.second != nullptr) {
            CloseFontRaw(item.second);
        }
    }
    state.fonts.clear();

    // 链解析的结论也一起丢：它记的是"哪份字体能开"，而字体句柄刚被全部关掉，
    // 再加上 `TTF_Quit` —— 留着会让"下一轮重新 init 之后再画"沿用旧结论
    // （那条结论可能是在 TTF 还没起来的时候得出的）。
    state.chainHits.clear();

#if defined(INK_HAS_SDL3_TTF)
    if (state.ttfInitialized) {
        TTF_Quit();
        state.ttfInitialized = false;
    }
#endif
}

}  // namespace detail

}  // namespace inking
