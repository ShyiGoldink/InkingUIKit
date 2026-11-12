#pragma once

// 场地组件（1.2-b / T-02 + T-03）：一块**带深度的地面空间** + 站在上面的图元。
//
// ---------------------------------------------------------------------------
// 它是什么，不是什么
// ---------------------------------------------------------------------------
//
// 是：几何与提交。它知道"世界里的一个点画在屏幕哪儿""屏幕这一点对应哪个格子"
//     "谁挡住谁"，并把结果按顺序交给提交层。
//
// 不是：实体、系统链、黑板、进程模型。那些**一律不进库**（这是"组件库进化成
//       引擎库"的边界，不是遗漏）。库侧只承诺一段 POD（`InkingFieldObject`），
//       实体活在业务侧——见下面"派生"那一节。
//
// ---------------------------------------------------------------------------
// 层序（由构造保证，不靠文档）
// ---------------------------------------------------------------------------
//
// ```
// 背景板（既有 SetBackdrop）
//   └ 地形面   天空底色 / 地面底色 / 瓦片      ← 自由顶点，固定最下
//   └ 物体面   按 (d, lift, 提交序号) 排序     ← 矩形 + 旋转
//   └ UI 树    既有控件，树序
// ```
//
// 地形面与物体面**分成两笔是接口逼出来的**：地面 / 瓦片在斜投影下是**平行四边形**，
// `InkingQuad`（矩形 + 旋转）表达不了，只能走自由顶点；而立着的东西在屏幕上仍是
// 正矩形（`skew` 只改纵深方向的连续性，不改单个物体的形状）。
//
// 代价（知情）：贴地且会遮挡的东西（草、石头、树根）必须当**物体**，不能当瓦片
// ——地形面永远在物体面之下，两者不逐笔交错。
//
// ---------------------------------------------------------------------------
// 派生（库不承诺实体，业务自己派生）
// ---------------------------------------------------------------------------
//
// ```cpp
// struct MyEnemy {
//     InkingFieldObject base;   // **必须是第一个成员**
//     int hp; float cooldown;
// };
// static_assert(offsetof(MyEnemy, base) == 0);
// ```
//
// 回调拿到的是**调用方传进来的那份内存的指针**（不是副本），所以
// `reinterpret_cast<MyEnemy*>` 拿回去是安全的（标准布局 + 首成员，标准保证）。
// 这也是 `SetObjects` 必须是**只读视图**的原因：一旦内部拷贝，指针对应的就不再
// 是业务的对象了。
//
// ---------------------------------------------------------------------------
// 尺寸与坐标
// ---------------------------------------------------------------------------
//
// 提交坐标是**绝对设计坐标**：场地必须是场景的直接子节点（`InkingScene::SetField`），
// 挂到任何有平移 / 缩放的容器下都会整体错位**而且不报错**——那条护栏见
// `docs/AGENTS.md` §7 `field_parent_transform_drifts`。
//
// 设计尺寸不自己存：每帧读 `InkingDesignWidth()` / `InkingDesignHeight()` 填进
// 投影参数。两份尺寸不同步的症状是"某个东西位置差了一截"。

#include <asset/InkingTexture.h>
#include <dataStruct/InkingShapeSpec.h>
#include <field/InkingFieldProjection.h>
#include <widget/InkingStaticWidget.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

/**
 * @brief 场里的一个**立着的图元**。
 *
 * 四个量必须分开（`width` / `depth` / `height` / `lift`），理由见各字段。
 * 它是 POD：没有虚函数、没有构造逻辑，整块填好交出去即可。
 */
struct InkingFieldObject {
    // ---- 位置（**场地坐标**，不是设计坐标）----
    float x = 0.0f;
    float d = 0.0f;
    /// 脚底离地多高（"飞起来"）。**与 `height` 不是一个东西**：混成一个字段之后，
    /// "角色跳高一点"会变成"角色变高"。
    float lift = 0.0f;

    // ---- 尺寸（世界单位 = 贴图像素）----
    float width = 0.0f;
    float depth = 0.0f;  ///< **不参与视觉**，只用于占位（影子 / 选中框 / 吸附 / 将来的立体命中）
    float height = 0.0f;

    // ---- 外观 ----
    InkingTextureHandle texture;
    /// 取纹理的哪一块（**像素**）。宽高全 0 = 整张纹理。
    InkingShapeBounds src;
    /// 调制色 0xAARRGGBB。alpha 参与混合（淡入淡出）。
    std::uint32_t tint = 0xFFFFFFFFu;
    /// 绕屏幕矩形中心旋转（**度**，顺时针为正，与 `InkingQuad` 同向）。
    float rotation = 0.0f;
    /// 只翻 **UV**，不翻几何。若去改 `x`，锚点语义当场变掉，占位会跑到锚点左边。
    bool flipX = false;

    // ---- 投影行为 ----
    /// false ⇒ 尺寸与 `lift` 的透视缩放全按 1 处理（"远处不变小"的贴花 / 血条）。
    /// **格子模式里设了没反应**（那里 `s ≡ 1`），见 §7 `field_follow_perspective_inert`。
    bool followPerspective = true;
    /// 可不可点。`false` 的物体照画，但 `HitObject` 不认它。
    bool hitEnabled = true;

    /// 锚点：`Corner`（默认，世界 `min x / min d` 那个角）或 `Center`。
    InkingFieldAnchor anchor = InkingFieldAnchor::Corner;
};

/**
 * @brief 地形面上的一块瓦片。
 *
 * **瓦片是地形面的内容**，不是"贴地的物体"：它跟着地面一起斜、一起被透视压扁。
 * 库只回答"可见格 + 格四边形"，autotile / 变体选择归引擎（业务侧）。
 */
struct InkingFieldTile {
    InkingFieldCell cell;
    InkingTextureHandle texture;
    InkingShapeBounds src;  ///< 像素；宽高全 0 = 整张
    std::uint32_t tint = 0xFFFFFFFFu;
    /// UV 翻转（瓦片只用得到这两个，旋转交给业务自己预烘）。
    bool flipX = false;
    bool flipY = false;
};

/**
 * @brief 场地：地面空间 + 站在上面的图元。
 *
 * 用法（最短路径）：
 *
 * ```cpp
 * auto field = std::make_unique<InkingField>(&scene, "field");
 * InkingFieldProjection projection;
 * projection.mode = InkingFieldMode::Grid;
 * field->SetProjection(projection);
 * field->SetObjects(enemies.data(), enemies.size());   // 只读视图
 * scene.SetField(field.get());
 * ```
 *
 * `SetObjects` / `SetTiles` 是**只读视图**：库不拷你的数组，也不接管它的生命周期，
 * 你必须保证它活到下一次 `SetObjects` 之前（命中回调返回的指针指向它）。
 * 这条不是省内存的小气——是"回调能拿回你自己那份实体"的前提。
 */
class InkingField : public InkingStaticWidget {
public:
    /**
     * @param parent 上级锚点（**必须是场景**，否则 `SetField` 会拒）；非拥有。
     * @param name   节点名（日志 / `FindByName`）。
     */
    InkingField(InkingWidget* parent, const std::string& name);

    InkingField(const InkingField&) = delete;
    InkingField& operator=(const InkingField&) = delete;
    InkingField(InkingField&&) = delete;
    InkingField& operator=(InkingField&&) = delete;

    // ---------------- 配置 ----------------

    /**
     * @brief 设投影参数。
     *
     * `designWidth` / `designHeight` 两个字段会被**每帧覆盖**成运行期的
     * `InkingDesignWidth()` / `InkingDesignHeight()`（组件不存设计尺寸，见文件头）。
     *
     * 参数不合法时：一条 WARN + 保留上一次那份（不静默换成"默认值"——那会让
     * 画面莫名跳一下，而且没人知道是自己填错了）。`GetStats().projectionValid`
     * 读得到当前状态。
     */
    void SetProjection(const InkingFieldProjection& projection);

    /// 当前生效的投影参数（设计尺寸是**上一次渲染时**读到的那一份）。
    const InkingFieldProjection& Projection() const noexcept;

    /**
     * @brief **只读视图**：交给本场地 N 个物体。
     *
     * `count == 0` 等价于清空。指针指向的数组必须在下次 `SetObjects`（或本对象
     * 析构）之前保持有效——命中回调拿到的是它里面的元素。
     *
     * **改了数组内容必须重新调一次**：库拿的是只读视图，**不会**替你发现内容
     * 变了（那份数据每帧都可能变，逐帧比对哈希不值当）。只改结构体不重设，
     * 画面和命中都停在上一份——而且不报错（自检里踩过一次）。
     *
     * 会标重绘脏（不标命中脏：本组件不进命中表）。
     */
    void SetObjects(const InkingFieldObject* items, std::size_t count);

    /**
     * @brief 同上，但允许元素是**业务侧派生**的结构（见下面那条模板）。
     *
     * @param strideBytes 相邻两个元素的**字节跨距**。传 `sizeof(你的结构)`。
     *
     * 为什么需要它（而不是让调用方自己转成 `InkingFieldObject*`）：派生结构的
     * 跨距比基类型大，"当成基类型数组来索引"会**逐元素错位**——第 1 个之后
     * 全读到别人的字段里去，而且不崩、不报错。而"指针指回业务对象"这条承诺
     * 又要求指针必须落在 `base` 成员上，所以跨距只能显式给。
     * 正常人别直接调它，用下面那个模板推导。
     */
    void SetObjects(const InkingFieldObject* items, std::size_t count,
                    std::size_t strideBytes);

    /**
     * @brief 派生结构的入口：`T` 必须**是标准布局**且**首成员叫 `base`**。
     *
     * ```cpp
     * struct MyEnemy { InkingFieldObject base; int hp; };
     * static_assert(offsetof(MyEnemy, base) == 0);
     * field.SetObjects(enemies.data(), enemies.size());   // 跨距自动 = sizeof(MyEnemy)
     * ```
     *
     * 三条约束都在编译期断死（`static_assert`），写错了不吃运行期亏。
     */
    template <class T, class = std::enable_if_t<!std::is_same_v<T, InkingFieldObject>>>
    void SetObjects(const T* items, std::size_t count) {
        static_assert(std::is_standard_layout_v<T>,
                      "SetObjects 的派生结构必须是标准布局（否则首成员偏移不是标准保证）");
        static_assert(std::is_same_v<std::remove_cv_t<decltype(T::base)>,
                                     InkingFieldObject>,
                      "SetObjects 的派生结构必须有一个名为 base 的 InkingFieldObject 成员");
        static_assert(offsetof(T, base) == 0,
                      "SetObjects 的派生结构里 base 必须是**第一个成员**（偏移 0）");
        const InkingFieldObject* first =
            (items == nullptr || count == 0) ? nullptr : &(items[0].base);
        SetObjects(first, count, sizeof(T));
    }

    /// 同上，瓦片。内部会建一份"格 → 瓦片"的索引（每帧按可见格查表）。
    void SetTiles(const InkingFieldTile* tiles, std::size_t count);

    /**
     * @brief 天空底色 / 地面底色（0xAARRGGBB）。
     *
     * **格子模式里没有天空**（`skew = 0`、无透视 ⇒ 地平线在无穷远），那里
     * `SetSkyColor` 设了不生效——与 `followPerspective` 同一类"参数在当前模式
     * 无意义"，见 §7 `field_follow_perspective_inert`。
     */
    void SetSkyColor(std::uint32_t argb);
    void SetGroundColor(std::uint32_t argb);
    std::uint32_t GetSkyColor() const noexcept;
    std::uint32_t GetGroundColor() const noexcept;

    /**
     * @brief **天空贴图**（1.5）。传无效句柄（`0`）= 回到 `SetSkyColor` 的纯色。
     *
     * 绘制规则（**只有这一份**，别在别处再解释一遍）：
     *
     * 1. 天空带 = `(0, 0) … (designWidth, horizonY)`，`horizonY` 由
     *    `InkingFieldHorizonY` 给（因此 `horizonRatio >= 0` 时它就是你指定的
     *    `ratio × designHeight`）；
     * 2. 贴图**只画在天空带内**（裁剪），不得盖到地面；
     * 3. 铺法看 `InkingFieldProjection::skyFit`：`Cover` 等比放大到**同时覆盖**
     *    带宽与带高、居中裁切；`Stretch` 直接拉伸（会变形）；`Tile` 按**原始像素**
     *    1:1 平铺，从天空带左上角起（右 / 下边缘是半块就画半块）；
     * 4. 纯色天空是**贴图缺席时的回退**，两者不混用（有贴图就不画纯色——叠一层
     *    半透明底色会让颜色偏掉，而且"偏多少"没人算得出来）；
     * 5. `Grid` 模式 / 平行投影（`h <= 0`）下天空带高度为 0 ⇒ 贴图被忽略，**打一条
     *    WARN**（每个场地实例一次），不静默。"先设贴图、后设投影"不会误报：判定
     *    发生在**渲染时**（已在 `Oblique` 下却没有天空带则设的时候就喊）；
     * 6. 句柄**所有权在调用方**：库不加载、不释放、不缓存路径，每帧换句柄必须便宜
     *    （动态天空 = 消费方每帧调它，库不提供动画抽象、也不读时钟）；
     * 7. 句柄失效（换过渲染器 / 已 Release）⇒ 退回纯色。画面不出现空洞，
     *    `InkingTextureStats::staleSubmits` 记着这笔账。
     *
     * 会标重绘脏（不标命中脏：它不影响"点中谁"）。
     */
    void SetSkyTexture(InkingTextureHandle handle);

    /**
     * @brief 当前天空带在**设计坐标**里的矩形（1.5）：`(0, 0, designWidth, horizonY)`。
     *
     * `h <= 0`（格子模式 / 平行投影）时高度为 **0**。天空带随相机 / 缩放 / 设计尺寸
     * 变，**每帧都能查，消费方不要缓存**。
     *
     * 返回类型是 `InkingShapeBounds`（本库唯一的矩形类型）而不是任务书里写的
     * `InkingRect`：后者从来不存在，为它新造一个矩形类型等于在公开面放两个
     * "x/y/w/h"，迟早有人拿错。
     */
    InkingShapeBounds GetSkyBand() const noexcept;

    // ---------------- 命中 ----------------

    /**
     * @brief 屏幕点（**设计坐标**）命中了哪个物体。
     *
     * 判据是**每帧投影时存下的屏幕矩形**（含旋转时绕中心逆旋转一次），
     * **绝不重新投影**——两份算式迟早分叉，表现是"看着点中了却打偏"。
     *
     * 扫描顺序是提交序的**逆序**（近的在前），所以重叠时拿到的是最上面那个。
     * 以下三种不认：`hitEnabled == false`、`tint` 的 alpha 为 0、被 1px 规则剔除。
     *
     * @param out 命中时写入**调用方那份内存**的指针（不是副本）。可为 nullptr。
     */
    bool HitObject(float designX, float designY, const InkingFieldObject** out) const;

    /**
     * @brief 屏幕点（**设计坐标**）落在地面的哪一格。闭式反解，一次除法。
     *
     * @return 有点在外面时 false：设计画布之外、地平线之上、超出有限地图、
     *         或者超出 1px 界（那里几何本身在抖，不给点）。
     */
    bool HitCell(float designX, float designY, InkingFieldCell* out) const;

    /**
     * @brief 用设计坐标"点一下"：命中物体就回调，否则按格回调地面。
     *
     * **只能在上层（UI）明确未命中之后调**——场地面铺满全屏，它自己不参与
     * 场景命中表的仲裁（`SetHitPassThrough(true)`，且 `HitTest` 恒 false）。
     * "UI 优先"因此是天然成立的，不需要把两边的 z 混成一个数（UI 的顺序是
     * "树序 + 兄弟 z"，不是可加的数）。
     *
     * @return 命中了物体才 true（地面点击不返回值，只有回调）。
     */
    bool ClickAt(float designX, float designY);

    /// 回调形态（与派生重写 `OnFieldClick` 二选一，两个都会被调用）。
    using ClickHandler =
        std::function<void(const InkingFieldObject&, InkingFieldCell, InkingDesignPos)>;

    /// 挂回调；传 `{}` 摘掉。**不标脏**（它不影响画面）。
    void SetOnFieldClick(ClickHandler handler);

    // ---------------- 观测口 ----------------

    /**
     * @brief **上一帧**的账（"画了几个 / 切了几批 / 剔了几个"）。
     *
     * 为什么要它：这一层所有的失效都是**静默**的——远处少画几个（1px 规则）、
     * 地形整块停在上一帧（批被拒）、瓦片被截断。画面上只是"少了点东西"，
     * 不看数就查不出来。
     *
     * **时间口径只有两种，别读错**（同一个结构体里两种口径是刻意的，因为两种
     * 问题要两种读法）：
     *
     * | 字段 | 口径 | 为什么要这个口径 |
     * | - | - | - |
     * | 除下面两个之外的全部 | **上一帧** | 它们是"这一帧画成什么样"的度量，攒起来没有意义 |
     * | `rejectedBatchesTotal` | **累计** | 它代表"出过事没有"，一旦出过就该一直看得见（名字里带 `Total`） |
     *
     * `culledSubPixel` 只在**投影缓存重建**那一帧才可能非 0：物体没变、相机没动
     * 的帧上它就是 0（没重建 = 这一帧什么都没剔）。不要把它当"累计剔掉了多少"。
     */
    struct Stats {
        std::uint32_t objectCount = 0;    ///< 调用方给了几个（不是画了几个）
        std::uint32_t tileCount = 0;      ///< 同上
        std::uint32_t objectsDrawn = 0;   ///< 上一帧真的提交了几个
        std::uint32_t visibleCells = 0;   ///< 上一帧枚举到几格（含没瓦片的空地盘）
        std::uint32_t terrainBatches = 0; ///< 地形面切成几批（上限 `kTerrainBatchPool`）
        std::uint32_t objectBatches = 0;  ///< 物体面切成几批（按顶点上限）
        /// 地形批被拒的**累计**次数（单批顶点超限 / 纹理解析不出）。
        /// 正常应该恒为 0——不为 0 就是"地形整块消失、停在上一次内容"。
        /// 名字带 `Total` 是有意的：它和上面那批"上一帧"的字段不是一个口径。
        std::uint32_t rejectedBatchesTotal = 0;
        /// 上一帧**投影缓存重建时**被 1px 规则剔掉的物体数（没重建 = 0）。
        std::uint32_t culledSubPixel = 0;
        std::uint32_t truncatedTiles = 0;  ///< 上一帧因层池满截掉的瓦片数
        /// 上一帧天空贴图画了几个四边形（1.5）。`0` = 没画贴图——要么没设、要么
        /// 天空带高度为 0、要么句柄失效退回了纯色、要么 `Tile` 撞了块数上限。
        /// `Cover` / `Stretch` 恒为 `0` 或 `1`，`Tile` 是 `行 × 列`（含边缘半块）。
        std::uint32_t skyQuads = 0;
        /// 1 = 祖先链上有变换，上一帧**整块没画**（画在错的地方比不画难查得多）。
        std::uint32_t transformBlocked = 0;
        /// 1 = 当前投影参数合法。
        std::uint32_t projectionValid = 0;
    };

    /// 当前这帧的账（读之前会先把缓存推到最新，所以不依赖"渲染过了"）。
    Stats GetStats() const noexcept;

    /// 调用方给了几个物体 / 几块瓦片。
    std::size_t GetObjectCount() const noexcept;
    std::size_t GetTileCount() const noexcept;

    /// 上一帧真的提交了几个物体（= `GetStats().objectsDrawn`）。
    std::size_t GetDrawnObjectCount() const noexcept;

    /// 地形面层池上限：8 批 × 4096 四边形（= 16384 顶点 / 批）。
    static constexpr std::uint32_t kTerrainBatchPool = 8;

    /// 单批四边形上限（提交层单批 16384 顶点 ÷ 4）。
    static constexpr std::uint32_t kQuadsPerBatch = 4096;

protected:
    /**
     * @brief 点中物体时的回调（派生重写即可）。
     *
     * `object` 指向**调用方那份内存**；`cell` 是它脚下那一格；`at` 是点击处的
     * 设计坐标。默认空实现——不点就不需要写它。
     */
    virtual void OnFieldClick(const InkingFieldObject& /*object*/,
                              InkingFieldCell /*cell*/, InkingDesignPos /*at*/) {}

    void onRender(float pixelX, float pixelY) const override;

private:
    /// 一帧的投影结果：绘制与命中共用这一份（"命中绝不重新投影"）。
    struct Projected {
        InkingShapeBounds rect;  ///< 屏幕矩形（设计坐标，绝对；未含旋转）
        float centerX = 0.0f;    ///< 矩形的中心（旋转轴）
        float centerY = 0.0f;
        float rotation = 0.0f;   ///< 度
        std::uint32_t index = 0; ///< 指向调用方数组
        bool visible = false;    ///< 过了 1px 规则、且句柄解析得出来
    };

    /// 第 `index` 个物体的 `base`（跨距可能大于 `sizeof(InkingFieldObject)`，见 `SetObjects`）。
    const InkingFieldObject& objectAt(std::size_t index) const noexcept;

    /// 把投影缓存推到最新（幂等；命中与绘制都先调它）。
    void ensureCache() const;
    bool ancestorTransformActive() const noexcept;
    void submitTerrain(float pixelX, float pixelY, float magnification, Stats& stats) const;
    void submitObjects(float pixelX, float pixelY, float magnification, Stats& stats) const;

    /**
     * @brief 把天空贴图按 `skyFit` 提交到天空带内（1.5）。
     *
     * @return 真的画了才 true（句柄解析不出来 ⇒ false，调用方退回纯色）。
     */
    bool submitSkyTexture(float pixelX, float pixelY, float magnification, float bandWidth,
                          float bandHeight, Stats& stats) const;

    /// 「有贴图但没有天空带」只喊一次（每个实例）。`const` 里也要写 ⇒ mutable 标志。
    void warnSkyTextureInertOnce() const;

    InkingFieldProjection _projection{};
    bool _projectionValid = true;

    const InkingFieldObject* _objects = nullptr;  ///< 指向**第 0 个元素的 base**
    std::size_t _objectCount = 0;
    std::size_t _objectStride = sizeof(InkingFieldObject);
    const InkingFieldTile* _tiles = nullptr;
    std::size_t _tileCount = 0;

    std::uint32_t _skyColor = 0xFF000000u;
    std::uint32_t _groundColor = 0xFF1E1E1Eu;
    InkingTextureHandle _skyTexture{};  ///< 无效 = 纯色天空（所有权在调用方）

    ClickHandler _onClick;

    // ---- 每帧缓存（const 方法里也要写 ⇒ mutable）----
    mutable InkingFieldProjection _effective{};
    /// 排序键（`d` 降、`lift` 升、序号升）。连续内存：比较器不碰跨距算术。
    struct SortKey {
        float d;
        float lift;
        std::uint32_t index;
    };
    mutable std::vector<SortKey> _keys;          ///< 提交序（远 → 近）
    mutable std::vector<Projected> _projected;   ///< 与 `_order` 同序
    mutable bool _cacheDirty = true;
    mutable bool _warnedTransform = false;
    /// 「设了天空贴图、但当前没有天空带」只喊一次（1.5）。
    mutable bool _warnedSkyInert = false;
    /// 「`Tile` 平铺块数超上限被截」只喊一次（1.5）。
    mutable bool _warnedSkyTileCap = false;
    /// 上一次**重建投影缓存**时剔掉的亚像素物体数，以及"这个数新不新"。
    /// 拆出来是因为 `_stats` 要报的是"上一帧"，而重建可能不在这一帧发生。
    mutable std::uint32_t _cullCount = 0;
    mutable bool _cullFresh = false;
    mutable Stats _stats{};
};
