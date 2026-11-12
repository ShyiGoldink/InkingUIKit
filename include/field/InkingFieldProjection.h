#pragma once

// 场地投影与格子的**纯函数层**（1.2-a / T-01）。
//
// 这一层零依赖、零 SDL、零全局状态：给一组参数和一组世界量，回答"画在屏幕哪儿"
// 与"屏幕这点对应哪个格子"。之所以把它从组件里拆出来单独成头，有两个硬理由：
//
//   1. **可脱窗口测试**。引擎的 core 进程（headless）只需要"点击 → 格坐标"，
//      不该为此拖进一整套锚点树 / 渲染器（这就是 `field/` 的依赖方向那条规矩）；
//   2. **算式只有一份**。命中反解、可见格枚举、组件每帧投影如果各写一遍，
//      迟早分叉——表现是"看着点中了却打偏"，而且是逐像素级的偏。
//
// ---------------------------------------------------------------------------
// 投影模型：**正俯视 + 斜投影**，不是透视
// ---------------------------------------------------------------------------
//
// ```
// ld = d − camera.d                                   // 相对相机的纵深
// s  = 1 / (1 + h·ld)          h = (1/depthScaleFar − 1) / farD
// sx = cx + (x − camera.x + sin(skew)·ld) · zoom · s
// sy = H  − cos(skew)·ld · zoom · s − y · zoom · s
// ```
//
// `cx = designWidth/2`、`H = designHeight`；`y` 是离地高度（站立物的脚底到它的
// 某一截），不是深度。
//
// **只有 `s = 1/(1+h·ld)` 这种形式能让世界直线保持是直线**（它是 Möbius 变换，
// 把直线映成直线）。线性衰减、任意曲线都会让纵线变弯——那条 21.36 px 的曲线
// 缺陷就是这么来的。所以这不是调参口味，是正确性条件。
//
// 两个参数正交，不要合并：
//   - `skew > 0` 让深度线朝一侧倾斜（格子变平行四边形）；
//   - `depthScaleFar < 1` 让远处变小，**必然同时带来汇聚**（缩放与汇聚是同一个
//     除法的两面）。消失点在 `cx + sin(skew)/h`，即倾斜那一侧。
//
// **格子模式（`Mode::Grid`）锁死 `skew = 0`、`depthScaleFar = 1`**：此时
// `s ≡ 1`，`sx = cx + (x − camera.x)·zoom`、`sy = H − ld·zoom − y·zoom`，
// `zoom = 1` 时 1 世界单位 = 1 像素。这是设计与美术的基准视图。
// 那两个参数在格子模式里**设了不生效**（见 `docs/AGENTS.md` §7
// `field_follow_perspective_inert`）。
//
// 地平线位置（1.5 的 `horizonRatio`）走的是**同一条** `s = 1/(1+h·ld)`：它不换
// 公式，只反解 `farD`（即 `h` 的来源），所以"天空带"与"地面灭点"永远自洽——
// 只裁地面带而不动 `h` 会让地面从天空里穿出来，看起来像美术画错了。

#include <cstdint>

/// 场地模块名（日志里用；和别的模块一个约定）。
inline constexpr const char* InkingFieldModuleName = "InkingField";

/// 场地模式。`Grid` = 正俯视无限格；`Oblique` = 斜投影（可带透视）、有限地图。
enum class InkingFieldMode : std::uint8_t { Grid = 0, Oblique = 1 };

/**
 * @brief 物体的锚点。
 *
 * `Corner` 是**起点角**——世界 `(min x, min d)` 那个角，**不是屏幕上的左下角**。
 * `skew > 0` 时两者相差 `depth·sin(skew)` 的横向量，正好是半个格子的斜移；
 * 而且角度越小差得越少，小角度下根本测不出来（所以别指望肉眼验收）。
 *
 * 它和 `InkingFieldCellOf` 的 `floor` 约定同源：向 `+x / +d / +y` 展开。
 */
enum class InkingFieldAnchor : std::uint8_t { Corner = 0, Center = 1 };

/**
 * @brief 天空贴图的铺法（1.5）。只在**存在天空带**时有效（`h > 0`，见
 *        `InkingFieldHorizonY`）。
 *
 * 三档都是纯枚举、无回调、无动画：动态天空由消费方每帧换句柄实现（库不读时钟）。
 */
enum class InkingSkyFit : std::uint8_t {
    Cover = 0,    ///< 等比放大到**同时覆盖带宽与带高**，居中裁切（不变形；默认）
    Stretch = 1,  ///< 直接拉伸到天空带（会变形，但"整张都看得见"）
    Tile = 2,     ///< 按**原始像素** 1:1 平铺，从天空带左上角起
};

/// 格子尺寸：一格覆盖多少世界单位（**就是瓦片的像素尺寸**）。
struct InkingFieldGrid {
    float cellWidth = 64.0f;  ///< 横向；`i` 方向
    float cellDepth = 64.0f;  ///< 纵深；`j` 方向

    friend bool operator==(const InkingFieldGrid&, const InkingFieldGrid&) = default;
};

/// 相机：场地坐标里的位置 + 缩放。
struct InkingFieldCamera {
    float x = 0.0f;
    float d = 0.0f;
    float zoom = 1.0f;  ///< > 0；`1` = 1 世界单位 1 像素（`Grid` 下的美术基准）

    friend bool operator==(const InkingFieldCamera&, const InkingFieldCamera&) = default;
};

/// **场地坐标**：`x` 横向、`d` 纵深、`y` 离地高度（单位 = 像素）。
///
/// 三个字母是有意为之：`x / d / y` 是坐标，`width / depth / height` 是尺寸，
/// 两者拆成不同类型，混用编译不过（见 `InkingFieldSize`）。
struct InkingFieldPos {
    float x = 0.0f;
    float d = 0.0f;
    float y = 0.0f;

    friend bool operator==(const InkingFieldPos&, const InkingFieldPos&) = default;
};

/// **格坐标**：整数 `(i, j)`，`i ↔ x`、`j ↔ d`。
struct InkingFieldCell {
    std::int32_t i = 0;
    std::int32_t j = 0;

    friend bool operator==(const InkingFieldCell&, const InkingFieldCell&) = default;
};

/// 一块**闭区间**格范围（`i0..i1` × `j0..j1`，含端点）。`valid == false` 时其余字段无意义。
struct InkingFieldCellRange {
    std::int32_t i0 = 0;
    std::int32_t i1 = -1;
    std::int32_t j0 = 0;
    std::int32_t j1 = -1;
    bool valid = false;
};

/// **设计坐标**：屏幕 / UI 布局空间（1280×720 那块虚拟画布）。
///
/// 它不是场地坐标。库里"设计坐标"这个词早就指向 UI 布局（`InkingSetDesignSize`
/// 与 LETTERBOX），场地自己那套连续量叫**场地坐标**——名字撞了，症状是"位置
/// 差了一截"，而且两边各自看起来都对。
struct InkingDesignPos {
    float x = 0.0f;
    float y = 0.0f;

    friend bool operator==(const InkingDesignPos&, const InkingDesignPos&) = default;
};

/**
 * @brief 一次投影需要的全部参数（**值语义**，可以随场景各持一份）。
 *
 * `designWidth` / `designHeight` 在这里是**显式参数**而不是去读
 * `InkingDesignWidth()`：纯函数层要能脱窗口单测，读全局就断了那条路。
 * 组件那一侧每帧把运行期值填进来（`InkingField::SetProjection` 的另一半）。
 */
struct InkingFieldProjection {
    InkingFieldMode mode = InkingFieldMode::Grid;
    InkingFieldGrid grid{};
    InkingFieldCamera camera{};

    float skew = 0.0f;           ///< 深度线偏离垂直的角度（度），有效区间 `[0, 80]`；格子模式锁 0
    float depthScaleFar = 1.0f;  ///< 最远处相对近处的尺寸比 `(0, 1]`；1 = 无透视
    float farD = 640.0f;         ///< 只在 `depthScaleFar < 1` 时有意义（"多远缩到最远"）

    float designWidth = 1280.0f;   ///< 目标画布宽（`cx = designWidth / 2`）
    float designHeight = 720.0f;   ///< 目标画布高（`H = designHeight`，地面在屏幕底边）

    /// 倾斜模式的规划格数。`0` = 无限，**只在格子模式合法**（那里本来就是无限格）。
    std::int32_t cols = 0;
    std::int32_t rows = 0;

    /**
     * @brief **地平线位置**：天空带高度占画布高度的比例，`[0, 1)`（1.5 新增）。
     *
     * 界面上的名字就叫「地平线位置」（不叫"天空占比"，也不叫"天空高度"）——它本质是
     * **整块地面的压缩率**：值越大，地平线越靠下，每一格都更矮（见下面「语义」）。
     *
     * `-1`（缺省）= **不覆盖**：地平线仍由 `skew / zoom / depthScaleFar / farD` 算出，
     * 即 1.4.0 的行为，逐位不变。
     *
     * `>= 0` = 覆盖：库内部**反解 `farD`**，使 `horizonY == horizonRatio × designHeight`
     * 成立。**不改变** `skew` / `depthScaleFar` / `zoom` 的语义——它们仍然是作者调的
     * 透视强度与尺度，只有"多远缩到最远"由这条约束定出来
     * （`InkingFieldEffectiveFarD` 查得到被定住的那个值，编辑器要显示它）。
     *
     * 反解（由 `horizonY = H − cos(skew)·zoom/h`、`h = (1/depthScaleFar − 1)/farD` 而来）：
     * ```
     * farD = (1/depthScaleFar − 1) · (1 − horizonRatio) · designHeight / (cos(skew) · zoom)
     * ```
     * 它是**运行期**的量：`designHeight` 一变，同一个比例对应另一个 `farD`，所以
     * `InkingField` 每帧按当前设计尺寸重算（见 `SetProjection` 的注释）。
     *
     * **为什么反解 `farD` 而不是 `zoom`**：`zoom` 是作者用的尺度（一格几像素），被
     * 地平线位置顺手改掉会让整张地图缩放跳动；`farD` 的语义就是"多远缩到最远"，
     * 它正是"地平线落在哪"的直接决定量。
     *
     * 拒绝（`InkingFieldValidate` 返回 false，**不静默夹取**）：
     *   - 不是有限值，或既不是 `-1` 也不在 `[0, 1)`（含 `1.0` / `1.2` / `-0.5` / NaN）；
     *   - `depthScaleFar >= 1`（无透视 ⇒ 本来就没有天空带，覆盖无意义）；
     *   - 反解出的 `farD <= 0`（`cos(skew)·zoom` 退化）。
     *
     * **格子模式里设了不生效**（那里 `s ≡ 1`、没有天空带）——与 `skew` /
     * `followPerspective` 同一类，见 `docs/AGENTS.md` §7 `field_follow_perspective_inert`。
     */
    float horizonRatio = -1.0f;

    /**
     * @brief 天空贴图的铺法（1.5 新增）。只在存在天空带时有效。
     *
     * 纯枚举值，越界取值由 `InkingFieldValidate` 拒绝（0..2）。
     */
    InkingSkyFit skyFit = InkingSkyFit::Cover;
};

// ---------------------------------------------------------------------------
// 正向：场地坐标 → 设计坐标
// ---------------------------------------------------------------------------

/**
 * @brief 一次投影里**与点无关**的那几个量（三角函数、透视系数、画布中心）。
 *
 * 为什么单独拿出来：一帧要投几千个物体，而 `sin(skew)` / `cos(skew)` / `h`
 * 在整帧里是常量。把它们提出去之后，每个点只剩乘加（实测 5000 个物体省约
 * 0.3 ms）。**公式仍然只有一份**——`InkingFieldProject` 就是用
 * `InkingFieldPrecompute` + `InkingFieldProjectPrecomputed` 实现的，不是抄的
 * 第二份（抄两份迟早分叉，表现是"画的和点的不一样"）。
 */
struct InkingFieldPrecomputed {
    float sinSkew = 0.0f;   ///< `sin(有效 skew)`；格子模式恒 0
    float cosSkew = 1.0f;   ///< `cos(有效 skew)`；格子模式恒 1
    float h = 0.0f;         ///< 透视系数；0 = 平行投影
    float zoom = 1.0f;
    float centerX = 0.0f;   ///< `designWidth / 2`
    float groundY = 0.0f;   ///< `designHeight`（地面在屏幕底边）
};

/// 把投影参数烘成"每点常量"。一帧算一次。
InkingFieldPrecomputed InkingFieldPrecompute(const InkingFieldProjection& projection) noexcept;

/**
 * @brief 正向投影的**快路径**：`pre` 由 `InkingFieldPrecompute` 算好。
 *
 * 与 `InkingFieldProject` 逐位等价（后者就是它 + 现算 `pre`）；批量投影用它，
 * 单个点或自检用后者。
 */
InkingDesignPos InkingFieldProjectPrecomputed(const InkingFieldProjection& projection,
                                              const InkingFieldPrecomputed& pre,
                                              InkingFieldPos pos) noexcept;

/**
 * @brief 把一个场地坐标点投影到设计坐标。
 *
 * 这是**唯一**的正向算式：组件每帧的物体矩形、格子四角、自检的期望值全部走它。
 */
InkingDesignPos InkingFieldProject(const InkingFieldProjection& projection,
                                   InkingFieldPos pos) noexcept;

/**
 * @brief 某一纵深处的**缩放系数** `zoom · s`，即"该深度上 1 世界单位 = 多少设计像素"。
 *
 * 单独暴露的理由很具体：立着的物体在屏幕上仍是**正矩形**（同一 `d` 上 `x` 与 `y`
 * 的缩放相同），它的宽高只要乘这一份系数——组件不必自己再算一遍 `s`，否则
 * "投影一份、尺寸一份"迟早差一个因子，表现是"越远越瘦"。
 *
 * @return `<= 0` 表示这个纵深已经在消失线之后（不该画）。
 */
float InkingFieldScaleAt(const InkingFieldProjection& projection,
                         float depth) noexcept;

// ---------------------------------------------------------------------------
// 反向：设计坐标 → 场地坐标（地面命中）
// ---------------------------------------------------------------------------

/**
 * @brief 把设计坐标的**地面点**（`y = 0`）反解成场地坐标。闭式，一次除法。
 *
 * 为什么不用图元 / 射线：地面是一个无限平面，反解是闭式的、`O(1)`、天然支持
 * 无限格；而"每格一条命中记录"是纯开销。
 *
 * @param design 设计坐标（屏幕点）。
 * @param out    解出来的场地点（`y` 恒 0）。可为 nullptr。
 * @return 有解才 true。无解的三种情况：深度线接近水平（`cos(skew) ≈ 0`，已由
 *         校验挡在 80°）、点落在地平线之上、以及解出的缩放非正。
 */
bool InkingFieldGroundHit(const InkingFieldProjection& projection,
                          InkingDesignPos design, InkingFieldPos* out) noexcept;

/**
 * @brief 屏幕上某个 `y` 对应的地面纵深 `ld`（相对相机的 `d` 差）。
 *
 * 地面深度只由屏幕纵坐标决定（与 `x` 无关：同一 `d` 上 `y` 只是平移），所以它是
 * 一条纯函数。可见格枚举、瓦片裁剪、点击反解都读它——**不要各写一遍**。
 *
 * @return 无解时返回 NaN（地平线及以上）。
 */
float InkingFieldDepthAt(const InkingFieldProjection& projection,
                         float designY) noexcept;

// ---------------------------------------------------------------------------
// 格子
// ---------------------------------------------------------------------------

/**
 * @brief 场地坐标 → 格坐标。`i = floor(x / cellWidth)`、`j = floor(d / cellDepth)`。
 *
 * **必须 `floor`，不许 `trunc`**：无限格意味着负坐标是常态，`trunc(−0.5)` 是 `0`，
 * 会把左上那片区域全算进第 0 格——小场景测不出来，大地图左上角整块错位。
 */
InkingFieldCell InkingFieldCellOf(const InkingFieldProjection& projection,
                                  InkingFieldPos pos) noexcept;

/// 格中心（场地坐标）→ 设计坐标。瓦片居中、格内摆放都从这里出发。
InkingDesignPos InkingFieldCellCenter(const InkingFieldProjection& projection,
                                      InkingFieldCell cell) noexcept;

/**
 * @brief 一格的四个角投到设计坐标上，按 **(min x, min d) → (+x) → (+x,+d) → (+d)**
 *        的绕序写进 `out[4]`（顺序固定，提交 / 自检都靠它）。
 *
 * 斜投影下这是**平行四边形**，不是旋转矩形——`InkingQuad`（矩形 + 旋转）表达
 * 不了它，所以地形面必须走自由顶点（`SetGeometry` / `AppendTexturedGeometry`）。
 *
 * @return 格中心的屏幕坐标（贴图居中、文字标注用得上）。
 */
InkingDesignPos InkingFieldCellQuad(const InkingFieldProjection& projection,
                                    InkingFieldCell cell,
                                    InkingDesignPos out[4]) noexcept;

/**
 * @brief 当前参数下**屏幕可能看到的格范围**（闭区间）。
 *
 * 三条边界一起作用：
 *   1. 视口上下沿反解出深度区间；
 *   2. `1px 规则`截断远端（亚像素的格子继续画不是"看不出"，是**几何在抖**）；
 *   3. `cols / rows > 0` 时夹到地图边界。
 *
 * 无限格之所以成立，正是因为**平行投影下格子永远不会小于 1px**（边界 2 自动消失）。
 *
 * 格是**半开区间** `[i·cw, (i+1)·cw)`，所以两端各按这个口径取：
 * `i0 = floor(minX/cw)`、`i1 = ceil(maxX/cw) − 1`（后者在 `maxX` 正好落在格边界
 * 上时不会多算一整列）。返回的范围仍然是保守的——多算几格只是白画，少算就是
 * 漏洞——但**数目可预判**：设计 1280×720、格 8×8 ⇒ 恰好 160×90。
 */
InkingFieldCellRange InkingFieldVisibleCells(const InkingFieldProjection& projection) noexcept;

// ---------------------------------------------------------------------------
// 量化与校验
// ---------------------------------------------------------------------------

/**
 * @brief 地平线落在屏幕的哪一行（设计坐标 `y`）。
 *
 * 深度线水平、没有透视时**没有地平线**，返回 **0**（天空带高度 0、整屏都是
 * 地面）——格子模式正是这样，所以它"没有天空"（`SetSkyColor` 在那里不生效）。
 *
 * 用途：地形面按它切成"天空带 + 地面带"两条实心矩形。地平线在任何投影下都是
 * **水平**的（`sy` 只跟 `ld` 有关、与 `x` 无关），所以这两条永远是轴对齐的。
 *
 * `horizonRatio >= 0` 时它是**被指定**的那个值（`ratio × designHeight`），由
 * `PerspectiveH` 反解 `farD` 保证；本函数与 `InkingFieldDepthAt` /
 * `InkingFieldVisibleCells` 读的是同一份反解结果，所以天空带与地面灭点不会分叉。
 *
 * @return 可能落在屏幕外（负 / 大于 `designHeight`），调用方自己夹。
 */
float InkingFieldHorizonY(const InkingFieldProjection& projection) noexcept;

/**
 * @brief 这次投影**实际用的** `farD`（1.5）：覆盖生效时是反解出来的那个，否则原样
 *        返回 `projection.farD`。
 *
 * 为什么要它：`horizonRatio >= 0` 会**改掉** `farD`（以约束为准，见字段注释）。
 * 编辑器要让用户看见"farD 被地平线位置定住了"，否则用户会以为自己的 `farD` 无效
 * 是 bug。同时也是自检的判据：`ratio = -1` 时它必须与 `projection.farD` **逐位相等**。
 */
float InkingFieldEffectiveFarD(const InkingFieldProjection& projection) noexcept;

/**
 * @brief **1px 判据**：从哪个纵深开始，格子已经小于一个屏幕像素。
 *
 * ```
 * 远端格子的屏幕边长 = min(cellWidth, cellDepth) · zoom · depthScaleFar
 * 若 < 1px ⇒ ld_1px = (min(cellWidth,cellDepth)·zoom − 1) / h
 * ```
 *
 * @return 平行投影（`depthScaleFar = 1`）下返回 **+∞**——那正是"无限格子"能成立的
 *         数学依据：格子永远不小于 1px，不需要裁。
 */
float InkingFieldDepthLimitForPixel(const InkingFieldProjection& projection) noexcept;

/**
 * @brief 参数自检。`false` 时 `*why` 指向一句人话（静态字符串，别释放）。
 *
 * 为什么在库里而不是在调用方：这组约束（`cellWidth > 0`、`zoom > 0`、倾斜模式
 * 必须有界地图…）一旦被违反，画面是"整块空白"或"格子无限细"，而日志一个字都
 * 没有。校验必须和公式住在一起，才不会两边各说各话。
 *
 * **格子模式下 `skew` / `depthScaleFar` / `horizonRatio` 不参与校验**——它们在那里
 * 本来就不生效（见文件头），报错只会误导人。`skyFit` 例外：越界的枚举值在任何
 * 模式下都是写错了，一律拒。
 */
bool InkingFieldValidate(const InkingFieldProjection& projection,
                         const char** why) noexcept;

/// 这一个格在**有限地图**里吗（`cols / rows <= 0` 视为无限，恒 true）。
bool InkingFieldCellInMap(const InkingFieldProjection& projection,
                          InkingFieldCell cell) noexcept;
