// ---------------------------------------------------------------------------
// SBOX-01 · 2.5D 小沙盒「静态骨架」
//
// 一片有限斜投影瓦片地图（草 / 沙 / 水 / 石砖四分区）+ 物体面一棵树 +
// 单独一格用 tint 的 alpha 做的简易水面。纯展示：无交互 / 无角色 / 无 autotile /
// 无动画。地基是库自带的 `field/InkingField.h`，**不另造渲染或投影层**。
//
// 地表用**一张共用的白/灰阶 relief 纹理**：四种地表共用同一个 src，颜色只从
// `InkingFieldTile::tint = kXxxBase` 进来（tint 是乘性调制 + alpha 混合）。
// 纹理只在初始化一次性生成，之后每帧走库的正常提交链。
//
// 运行：
//   inking_sandbox                 真窗口 + 主循环（Show("sandbox")，关窗 / ESC 退出）
//   inking_sandbox --probe        dummy 视频驱动离屏跑 2 帧，打印 GetStats() 并写
//                                 sandbox_probe.png（给"它是不是活的"留证据）
//
// 三条最容易静默失效的纪律（docs/zh/api/InkingField.md §7）：
//   1. 场地必须是**场景的直接子节点** —— 用 SetField(field)，父链上不能有变换；
//   2. SetTiles/SetObjects 是**只读视图** —— 数组存在 SandboxScene 的成员里，
//      活到场地析构；改内容必须重新 Set；
//   3. 瓦片/物体没有有效纹理句柄就不可见 —— 图集句柄先生成再填数组。
// ---------------------------------------------------------------------------

#include <inking.h>

#include <asset/InkingAsset.h>
#include <asset/InkingTexture.h>
#include <config/window_config.h>
#include <field/InkingField.h>
#include <field/InkingFieldProjection.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "SandboxPalette.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace pal = sandbox::palette;

// ---------------------------------------------------------------------------
// 调参只改这一块（README「调参」一节逐项对应）
// ---------------------------------------------------------------------------

/// atan(0.5)：深度线 2 竖 : 1 横，等距像素画常用的角度。有效区间 [0, 80]。
constexpr float kSkewDeg = 26.57f;
constexpr float kZoom = 1.30f;

/// 一格的世界尺寸 = 瓦片的像素尺寸。64×32 对应美术规格的 2:1 瓦片。
constexpr float kCellWidth = 64.0f;
constexpr float kCellDepth = 32.0f;

/// **有限地图**：Oblique 模式下 cols / rows 必须都 > 0，否则投影被拒且只留一条 WARN。
constexpr std::int32_t kCols = 14;
constexpr std::int32_t kRows = 24;

/// 透视强度与地平线位置。horizonRatio ∈ [0,1) 时库会反解 farD，
/// kFarD 只是"填个正数"让 InkingFieldValidate 通过（反解值会覆盖它）。
/// depthScaleFar < 1 ⇒ 有天空带 ⇒ SetSkyColor 才真的画得出来（规格 §7-A）。
constexpr float kDepthScaleFar = 0.72f;
constexpr float kHorizonRatio = 0.28f;
constexpr float kFarD = 900.0f;

/// 相机放在地图横向中心：cx = designWidth/2，camera.x = 0 时地图会从画布中心
/// 往右铺、左半屏全空（自检里那条 −80..79 的坑）。
constexpr float kCameraX = kCols * kCellWidth * 0.5f;
constexpr float kCameraD = 0.0f;

// ---------------------------------------------------------------------------
// 图集布局：3 个 64 宽的格子 —— 一张**共用**的白/灰阶 relief、水面层、树。
// 四种地表共用同一个 src，颜色只由 InkingFieldTile::tint 给（补充对齐项 C）。
// ---------------------------------------------------------------------------
constexpr int kTileW = 64;
constexpr int kTileH = 32;
constexpr int kTreeW = 64;
constexpr int kTreeH = 96;
constexpr int kAtlasW = 3 * kTileW;   // 192
constexpr int kAtlasH = kTreeH;       // 96
constexpr int kTreeCellX = 2 * kTileW;

constexpr char kAtlasFile[] = "sandbox_atlas.png";

enum AtlasCell : int {
    kCellRelief = 0,   ///< 白 / 灰阶 relief：四种地表共用这一格
    kCellSurface = 1,  ///< 白 relief + 2 条波纹亮线，专给 kWaterSurface 上色
    kCellTree = 2,     ///< 树（物体，不是地表，用 kTree* 实色画）
};

/// 分区 → **tint**：0 近左草 / 1 近右沙 / 2 远左石 / 3 远右水。
/// 地表色只从这里进 InkingFieldTile::tint，绝不预先染进纹理像素。
constexpr std::uint32_t kZoneTint[4] = {
    pal::kGrassBase, pal::kSandBase, pal::kStoneBase, pal::kWaterBase,
};

/// 单独那一格简易水面（落在水分区中间）。
constexpr std::int32_t kSurfaceCellI = 10;
constexpr std::int32_t kSurfaceCellJ = 16;

/// 树落在草分区中间那格。
constexpr std::int32_t kTreeCellI = 3;
constexpr std::int32_t kTreeCellJ = 4;

// ---------------------------------------------------------------------------
// 程序生成图集：一次性生成 RGBA 像素缓冲 → 落一张 PNG → InkingLoadTexture。
// 之后每帧仍走库的 SetTiles/SetObjects 提交，不做 CPU 逐像素光栅化（规格 §7-D）。
// ---------------------------------------------------------------------------

struct Rgba {
    std::uint8_t r, g, b, a;
};

constexpr Rgba Unpack(std::uint32_t argb) noexcept {
    return Rgba{static_cast<std::uint8_t>((argb >> 16) & 0xFFu),
                static_cast<std::uint8_t>((argb >> 8) & 0xFFu),
                static_cast<std::uint8_t>(argb & 0xFFu),
                static_cast<std::uint8_t>((argb >> 24) & 0xFFu)};
}

/// 直接写 RGBA32 的字节（SDL_PIXELFORMAT_RGBA32 的字节序恒为 R,G,B,A）。
void Put(SDL_Surface* surface, int x, int y, std::uint32_t argb) noexcept {
    if (surface == nullptr || x < 0 || y < 0 || x >= surface->w
        || y >= surface->h) {
        return;
    }
    auto* base = static_cast<std::uint8_t*>(surface->pixels);
    std::uint8_t* pixel = base
                        + static_cast<std::ptrdiff_t>(y) * surface->pitch
                        + static_cast<std::ptrdiff_t>(x) * 4;
    const Rgba c = Unpack(argb);
    pixel[0] = c.r;
    pixel[1] = c.g;
    pixel[2] = c.b;
    pixel[3] = c.a;
}

/// 确定性伪随机：图案固定、不随帧变化（静态骨架不做动画）。
constexpr std::uint32_t Noise(int x, int y, std::uint32_t seed) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 73856093u
                    ^ static_cast<std::uint32_t>(y) * 19349663u
                    ^ seed * 83492791u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h;
}

/**
 * **一张共用**的 64×32 白 / 灰阶 relief（补充对齐项 C / D / I）。
 *
 * 所有像素都是中性灰阶或中性半透明黑，**一个地表色都没有**；四种地表的颜色
 * 只从 `InkingFieldTile::tint = kXxxBase` 进来，四个分区共用同一个 `src`。
 *
 * `SDL_Vertex.color` 是**乘性调制、只能变暗**，所以三条语义要如实落地：
 *   - 顶面纯白 255 ⇒ `tint × 1.0` = `kXxxBase` **逐位精确**，§6/J 的四档明度
 *     阶梯（202/147/112/79）因此不受影响；
 *   - 上沿受光棱也只能取纯白（乘法的亮度上限）＝"未被压暗的 Base"。
 *     `kXxxLit` 这类**比 Base 更亮**的绝对色在单张 relief + 乘性 tint 下不可达，
 *     常量仍按规格 §8 留在 `SandboxPalette.h`，但这条路径不使用；
 *   - 下沿 1px + 内侧 2px 用灰阶压暗，承担"光固定在屏幕左上"的方向；
 *   - 外轮廓 1px 用 `kTileOutlineSoft`（半透明中性黑），靠 `SDL_BLENDMODE_BLEND`
 *     叠在 `kGroundColor` 上，四种 tint 下都收敛到接近 `kTileOutline` 的深色。
 *     **BLEND 不可用时的回退**：把这行换成 `pal::kTileOutline`，并把水面 tint
 *     的 alpha 提到 0xFF（补充对齐项 B）。
 *
 * 注意形状：库的 Oblique 投影把 (i,j) 格投成**平行四边形**（+x 轴恒水平），
 * 所以这里填整块 64×32 矩形，不是四角透明的菱形；见 README「已知限制」#1。
 */
void PaintRelief(SDL_Surface* surface) {
    for (int y = 0; y < kTileH; ++y) {
        for (int x = 0; x < kTileW; ++x) {
            std::uint32_t color = pal::kReliefWhite;   // 顶面 = 纯白 = tint 原色
            const bool outline = x == 0 || y == 0 || x == kTileW - 1
                              || y == kTileH - 1;
            if (outline) {
                color = pal::kTileOutlineSoft;         // 1px 半透明描边（必留）
            } else if (y >= kTileH - 4) {
                color = pal::kReliefShade;             // 下沿暗边 + 2px 厚度
            } else if ((Noise(x, y, 5u) & 127u) == 0u) {
                color = pal::kReliefPattern;           // 中性 1px 点，极简图案
            }
            // y = 1..2 保持纯白 = 上沿受光棱（乘性调制的亮度上限）
            Put(surface, kCellRelief * kTileW + x, y, color);
        }
    }
}

/// 水面层那一格：白 / 灰阶 relief，由 tint = kWaterSurface 染成半透明淡青蓝。
void PaintSurfaceCell(SDL_Surface* surface) {
    const int ox = kCellSurface * kTileW;
    for (int y = 0; y < kTileH; ++y) {
        for (int x = 0; x < kTileW; ++x) {
            std::uint32_t color = pal::kReliefWhite;
            if ((y == 11 || y == 21) && x > 6 && x < 58) {
                color = pal::kReliefGlint;      // 2 条横向亮线 = "水在反光"
            }
            Put(surface, ox + x, y, color);
        }
    }
}

/// 树：64×96，四角透明；树干 + 三团圆树冠，左上受光 / 右下背光。
void PaintTree(SDL_Surface* surface) {
    const auto inCircle = [](int x, int y, float cx, float cy, float r) noexcept {
        const float dx = static_cast<float>(x) - cx;
        const float dy = static_cast<float>(y) - cy;
        return dx * dx + dy * dy <= r * r;
    };
    for (int y = 0; y < kTreeH; ++y) {
        for (int x = 0; x < kTreeW; ++x) {
            std::uint32_t color = pal::kTransparent;
            if (x >= 28 && x <= 35 && y >= 54 && y <= 94) {
                color = (x >= 33) ? pal::kTreeTrunkShade : pal::kTreeTrunk;
            }
            if (inCircle(x, y, 32.0f, 32.0f, 24.0f)
                || inCircle(x, y, 19.0f, 44.0f, 16.0f)
                || inCircle(x, y, 45.0f, 44.0f, 16.0f)) {
                color = pal::kTreeCanopy;
                if (x < 26 && y < 30) {
                    color = pal::kTreeCanopyLit;     // 左上受光，面积 < 1/3
                }
                if (x > 42 || y > 56) {
                    color = pal::kTreeCanopyShade;   // 右下暗部 + 底部轮廓
                }
            }
            Put(surface, kTreeCellX + x, y, color);
        }
    }
}

/**
 * 生成图集并取回一个**跨帧持有**的有效句柄。
 *
 * 为什么落盘再 Load：`InkingLoadTexture` 的入口只有"相对资源根的路径"，库没有
 * 从内存像素直接建句柄的公开面。SDL3_image 的 `IMG_SavePNG` 负责编码（保留 alpha），
 * `InkingLoadTexture` / `ResolveTextureHandle` 负责解码并建纹理 —— 这一步只在
 * 初始化做一次，之后每帧走的还是库自己的提交链。
 */
InkingTextureHandle GenerateAtlas(std::string* writtenPath) {
    SDL_Surface* surface = SDL_CreateSurface(kAtlasW, kAtlasH, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) {
        std::printf("[sandbox] SDL_CreateSurface 失败：%s\n", SDL_GetError());
        return {};
    }
    for (int y = 0; y < kAtlasH; ++y) {
        for (int x = 0; x < kAtlasW; ++x) {
            Put(surface, x, y, pal::kTransparent);
        }
    }
    // 只有一张白 / 灰阶 relief（四种地表共用）+ 水面层 + 树。
    // 地表色一个都没写进像素：全部由 InkingFieldTile::tint 给（补充对齐项 C）。
    PaintRelief(surface);
    PaintSurfaceCell(surface);
    PaintTree(surface);

    const std::string root = InkingAssetRoot();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(root), ec);
    const std::string path = root + "/" + kAtlasFile;
    if (!IMG_SavePNG(surface, path.c_str())) {
        std::printf("[sandbox] IMG_SavePNG 失败：%s（%s）\n", path.c_str(),
                    SDL_GetError());
        SDL_DestroySurface(surface);
        return {};
    }
    SDL_DestroySurface(surface);
    if (writtenPath != nullptr) {
        *writtenPath = path;
    }

    const InkingTextureHandle handle = InkingLoadTexture(kAtlasFile);
    if (!handle.Valid()) {
        std::printf("[sandbox] InkingLoadTexture 取不到有效句柄：%s\n", kAtlasFile);
    }
    return handle;
}

// ---------------------------------------------------------------------------
// 场景：构造即登记（`InkingScene("sandbox")`），Show("sandbox") 按名字激活
// ---------------------------------------------------------------------------
class SandboxScene : public InkingScene {
public:
    SandboxScene() : InkingScene("sandbox") {
        _atlas = GenerateAtlas(&_atlasPath);
        if (!_atlas.Valid()) {
            std::printf("[sandbox] 图集不可用：瓦片与物体都不会显示。\n");
            return;
        }

        // 场地必须是**场景的直接子节点**，父链上不能有任何平移 / 缩放。
        InkingWidget* added =
            AddChild(std::make_unique<InkingField>(this, "field"));
        _field = static_cast<InkingField*>(added);
        if (!SetField(_field)) {
            std::printf("[sandbox] SetField 被拒：场地没占住槽位，整块不会画。\n");
            _field = nullptr;
            return;
        }

        SetupProjection();
        SetupTiles();
        SetupObjects();
    }

    InkingField* Field() const noexcept { return _field; }
    bool Ok() const noexcept { return _field != nullptr && _atlas.Valid(); }
    const std::string& AtlasPath() const noexcept { return _atlasPath; }
    InkingTextureHandle Atlas() const noexcept { return _atlas; }
    std::size_t TileCount() const noexcept { return _tiles.size(); }
    std::size_t ObjectCount() const noexcept { return _objects.size(); }

private:
    void SetupProjection() {
        InkingFieldProjection projection;
        projection.mode = InkingFieldMode::Oblique;  // 有限地图 + skew
        projection.grid = InkingFieldGrid{kCellWidth, kCellDepth};
        projection.camera = InkingFieldCamera{kCameraX, kCameraD, kZoom};
        projection.skew = kSkewDeg;
        projection.depthScaleFar = kDepthScaleFar;
        projection.farD = kFarD;                     // horizonRatio 会反解覆盖
        projection.designWidth = static_cast<float>(InkingDesignWidth());
        projection.designHeight = static_cast<float>(InkingDesignHeight());
        projection.cols = kCols;
        projection.rows = kRows;
        projection.horizonRatio = kHorizonRatio;     // 天空带真的出现
        projection.skyFit = InkingSkyFit::Cover;
        _field->SetProjection(projection);

        // 规格 §4：库默认是 0xFF000000 / 0xFF1E1E1E，必须显式覆盖。
        _field->SetSkyColor(pal::kSkyColor);
        _field->SetGroundColor(pal::kGroundColor);
    }

    void SetupTiles() {
        _tiles.clear();
        _tiles.reserve(static_cast<std::size_t>(kCols) * kRows);
        for (std::int32_t j = 0; j < kRows; ++j) {
            for (std::int32_t i = 0; i < kCols; ++i) {
                InkingFieldTile tile;
                tile.cell = InkingFieldCell{i, j};
                tile.texture = _atlas;

                const int zone = (i >= kCols / 2 ? 1 : 0)
                               + (j >= kRows / 2 ? 2 : 0);
                // 四个分区**共用同一张白 / 灰阶 relief**：src 完全相同，
                // 地表色只从 tint 进来（补充对齐项 C）。
                tile.src = InkingShapeBounds{
                    static_cast<float>(kCellRelief * kTileW), 0.0f,
                    static_cast<float>(kTileW), static_cast<float>(kTileH)};
                tile.tint = kZoneTint[zone];

                // 单独一格「简易水面」：**地形面瓦片**，不是物体（补充对齐项 E：
                // InkingFieldObject 在屏幕上是正矩形、不贴合格子）。
                // 白 relief + kWaterSurface 这个带 alpha 的 tint；alpha=0x99
                // （≈60%，规格 §3.1 主值），既不会被判成 invisible，也不会盖死底层。
                if (i == kSurfaceCellI && j == kSurfaceCellJ) {
                    tile.src = InkingShapeBounds{
                        static_cast<float>(kCellSurface * kTileW), 0.0f,
                        static_cast<float>(kTileW), static_cast<float>(kTileH)};
                    tile.tint = pal::kWaterSurface;
                }
                _tiles.push_back(tile);
            }
        }
        _field->SetTiles(_tiles.data(), _tiles.size());
    }

    void SetupObjects() {
        InkingFieldObject tree;
        tree.x = (static_cast<float>(kTreeCellI) + 0.5f) * kCellWidth;
        tree.d = (static_cast<float>(kTreeCellJ) + 0.5f) * kCellDepth;
        tree.lift = 0.0f;
        tree.width = 48.0f;    // 与 src 64:96 同比例（2:3）
        tree.depth = 24.0f;    // 不参与视觉，只占位
        tree.height = 72.0f;
        tree.texture = _atlas;
        tree.src = InkingShapeBounds{static_cast<float>(kTreeCellX), 0.0f,
                                     static_cast<float>(kTreeW),
                                     static_cast<float>(kTreeH)};
        tree.tint = pal::kOpaque;
        tree.rotation = 0.0f;
        tree.flipX = false;              // 翻 UV 会把光源翻到右下（规格 §5.6）
        tree.followPerspective = true;
        tree.hitEnabled = false;         // 静态骨架不接交互
        tree.anchor = InkingFieldAnchor::Center;

        _objects.clear();
        _objects.push_back(tree);
        _field->SetObjects(_objects.data(), _objects.size());
    }

    InkingTextureHandle _atlas{};
    std::string _atlasPath;
    /// 非拥有：真正的所有权在 AddChild 交进去的 unique_ptr 上。
    InkingField* _field = nullptr;
    /// **只读视图的宿主**：必须活到下一次 SetTiles / SetObjects（或场地析构）之后。
    /// 放在场景成员里而不是 main() 的局部变量，正是为了这条生命周期约束。
    std::vector<InkingFieldTile> _tiles;
    std::vector<InkingFieldObject> _objects;
};

// ---------------------------------------------------------------------------
// 离屏自检：不依赖显示器、不依赖窗口层主循环，只回答"这一帧到底画了没"
// ---------------------------------------------------------------------------
int RunProbe(SandboxScene& scene) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("[sandbox][probe] SDL_Init(VIDEO) 失败：%s\n", SDL_GetError());
        return 2;
    }
    SDL_Window* window =
        SDL_CreateWindow("sandbox-probe", InkingDesignWidth(), InkingDesignHeight(), 0);
    SDL_Renderer* renderer =
        window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (renderer == nullptr) {
        std::printf("[sandbox][probe] 建不出渲染器：%s\n", SDL_GetError());
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
        return 2;
    }
    SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    InkingSceneLibrary::SetActiveScene(&scene);
    scene.TickFrame(1.0 / 60.0);
    for (int frame = 0; frame < 2; ++frame) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        InkingSceneLibrary::RenderScene(renderer);
    }

    const InkingField::Stats s = scene.Field()->GetStats();
    std::printf("\n-- sandbox probe / GetStats()（上一帧口径）--\n");
    std::printf("   projectionValid      = %u  (期望 1)\n", s.projectionValid);
    std::printf("   transformBlocked     = %u  (期望 0)\n", s.transformBlocked);
    std::printf("   tileCount            = %u  (给了几块瓦片)\n", s.tileCount);
    std::printf("   objectCount          = %u  (给了几个物体)\n", s.objectCount);
    std::printf("   objectsDrawn         = %u  (期望 1：那棵树)\n", s.objectsDrawn);
    std::printf("   visibleCells         = %u  (期望 > 0)\n", s.visibleCells);
    std::printf("   terrainBatches       = %u  (期望 1..8)\n", s.terrainBatches);
    std::printf("   objectBatches        = %u\n", s.objectBatches);
    std::printf("   truncatedTiles       = %u  (期望 0)\n", s.truncatedTiles);
    std::printf("   rejectedBatchesTotal = %u  (期望 0，累计口径)\n",
                s.rejectedBatchesTotal);
    std::printf("   culledSubPixel       = %u\n", s.culledSubPixel);
    std::printf("   skyQuads             = %u\n", s.skyQuads);
    std::printf("   图集                 = %s\n", scene.AtlasPath().c_str());
    std::printf("   瓦片/物体数组         = %zu / %zu（只读视图仍活着）\n",
                scene.TileCount(), scene.ObjectCount());

    const bool healthy = s.projectionValid == 1u && s.transformBlocked == 0u
                      && s.objectsDrawn == 1u && s.visibleCells > 0u
                      && s.tileCount == scene.TileCount()
                      && s.truncatedTiles == 0u
                      && s.rejectedBatchesTotal == 0u;
    std::printf("   结论                 = %s\n\n", healthy ? "活的" : "有静默失效");

    // 留一张像素证据：dummy 驱动 + 软件渲染器也能读回帧缓冲。
    SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
    if (shot != nullptr) {
        if (SDL_SavePNG(shot, "sandbox_probe.png")) {
            std::printf("   截图                 = sandbox_probe.png\n\n");
        } else {
            std::printf("   截图失败             = %s\n\n", SDL_GetError());
        }
        SDL_DestroySurface(shot);
    } else {
        std::printf("   截图读不回像素       = %s\n\n", SDL_GetError());
    }

    InkingSceneLibrary::SetActiveScene(nullptr);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return healthy ? 0 : 3;
}

}  // namespace

int main(int argc, char** argv) {
    bool probe = argc > 1 && std::strcmp(argv[1], "--probe") == 0;
    if (const char* env = std::getenv("SANDBOX_PROBE");
        env != nullptr && env[0] != '\0') {
        probe = true;
    }
    // hint 必须在 SDL_Init(SDL_INIT_VIDEO) **之前**设：只在建驱动时被消费。
    if (probe) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    }

    // 设计尺寸必须是第一条：第一个窗口 / 场景一构造就定型（config/window_config.h）。
    if (!InkingSetDesignSize(1280, 720)) {
        std::printf("[sandbox] 设定设计尺寸 1280x720 被拒绝。\n");
        return 1;
    }
    // 让资源根（exe 旁的 assets/）可解析；这一步不建窗口。
    SDL_Init(0);

    std::printf("[sandbox] Inking %s-%s / SDL3 %s\n", InkingVersion,
                InkingVersionStage, InkingSdl3_version().c_str());

    SandboxScene scene;
    if (!scene.IsRegistered()) {
        std::printf("[sandbox] 场景 \"sandbox\" 登记被驳回（名字被占）。\n");
        return 1;
    }
    if (!scene.Ok()) {
        std::printf("[sandbox] 场地没搭起来（图集 / SetField 失败）。\n");
        return 1;
    }
    const char* why = nullptr;
    const bool valid = InkingFieldValidate(scene.Field()->Projection(), &why);
    std::printf("[sandbox] 投影校验 = %s%s；地图 %dx%d，skew %.2f°，zoom %.2f\n",
                valid ? "通过" : "未通过",
                (valid || why == nullptr) ? "" : (std::string("（") + why + "）").c_str(),
                kCols, kRows, kSkewDeg, kZoom);

    if (probe) {
        return RunProbe(scene);
    }

    InkingWindow& window = InkingWindow::Instance();
    window.setWindowWidth(1280);
    window.setWindowHeight(720);
    std::printf("[sandbox] 开窗口：关窗或 ESC 退出；--probe 可离屏打印 GetStats()。\n");
    return window.Show("sandbox");
}
