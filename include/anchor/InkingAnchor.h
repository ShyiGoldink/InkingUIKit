#pragma once

// 锚点定位层：只回答“我在哪里？”
//
// 这是整个项目里最底层的一个类，它有三条边界：
//
//   1. 不知道 SDL：这个头不拖任何 SDL 类型进来
//   2. 不知道场景、不知道树是谁的：它只有一条 `_parent` 裸指针——那是定位的输入，不是树的所有权。
//      "谁是孩子、谁拥有谁、登记进哪个
//      场景"全在 Widget 层（`include/widget/InkingWidget.h`）；
//   3. 没有变换、没有命中、没有脏：那些都是 Widget 的概念。
//      定位只用锚点公式 + 父级尺寸，所以"算出来在哪"这件事不依赖任何显示状态。
//
//     位置不存，由锚点推导
//
//     父级尺寸 × 上级锚点 − 自身尺寸 × 自身锚点 + 偏移
//
//     于是GetX()永远等于"按当前父级尺寸算出来的位置"，父级一改尺寸、
//     孩子下一帧就在新位置上——不需要任何同步代码。

#include <dataStruct/InkingAnchorData.h>

#include <cstdint>

namespace inking {

/**
 * @brief 定位基类。
 *
 * 不可直接实例化。
 *
 * 本类没有几何写入口（`Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` /
 * `ChangeOffset` 都在动态档上）：静态组件"几何构造即定型"是一条编译期保证，
 * 靠的就是"类型上根本没有那个成员函数"——拿到 `InkingAnchor` 也改不了几何。
 */
class InkingAnchor {
public:
    /// 虚析构：本类要被继承。
    virtual ~InkingAnchor();

    /// 禁拷贝 / 移动：持有父指针，复制出的第二份关系会让两条链各指一个对象。
    InkingAnchor(const InkingAnchor&) = delete;
    InkingAnchor& operator=(const InkingAnchor&) = delete;
    InkingAnchor(InkingAnchor&&) = delete;
    InkingAnchor& operator=(InkingAnchor&&) = delete;

    // ---------------- 只读查询：锚点与位置 ----------------

    /// 自身锚点：自身矩形上的哪个百分比点，去对上级锚点。默认左上角。
    const AnchorPoint& GetSelfAnchor() const noexcept { return _selfAnchor; }

    /// 上级锚点：父级矩形上的哪个百分比点。默认左上角。
    const AnchorPoint& GetTraceAnchor() const noexcept { return _traceAnchor; }

    /// 对齐之后再挪一点（设计坐标）。
    float GetOffsetX() const noexcept { return _offsetX; }
    float GetOffsetY() const noexcept { return _offsetY; }

    /// 相对**父级左上角**的位置（设计坐标）。
    ///
    /// 它是算出来的，不是存下来的：父级尺寸一变，它跟着变。
    /// 没有父级时（窗口 / 场景）退化成偏移量本身。
    float GetX() const noexcept {
        if (_parent == nullptr) {
            return _offsetX;
        }
        return static_cast<float>(_parent->_width) * _traceAnchor.x
             - static_cast<float>(_width) * _selfAnchor.x
             + _offsetX;
    }

    float GetY() const noexcept {
        if (_parent == nullptr) {
            return _offsetY;
        }
        return static_cast<float>(_parent->_height) * _traceAnchor.y
             - static_cast<float>(_height) * _selfAnchor.y
             + _offsetY;
    }

    // `GetAbsX()` / `GetAbsY()`（相对根的位置，O(深度)）在 **Widget 层**：
    // 它们除了父链还要问"当前显示尺寸"，而尺寸入口在那儿。

    // ---------------- 只读查询：尺寸与可见性 ----------------

    /**
     * 这个矩形多大，设计坐标。
     *
     * 默认就是推导位置用的 `_width` / `_height`——只有一份数据，不另存"显示尺寸"
     * 那样必须永远同步，而写入点散在构造 / 布局通道 / `Resize` /
     * 每态尺寸四处，漏一处就是"布局按 100 排、画出来 80"这类静默错。
     *
     * 它是虚函数：有"每态尺寸"的组件（T-5：静态按钮三态各一份尺寸）重写它去返回当前显示尺寸
     * 绘制与命中都问这个 getter
     * 重写时必须写 `override`：漏了就变成"隐藏"而不是"重写"，而通过基类指针调用会静默拿到 `_width`
     * _width是推导尺寸，不报错、只是画的和算的不一样。
     */
    virtual int GetWidth() const noexcept { return _width; }
    virtual int GetHeight() const noexcept { return _height; }

    // 自己这一层的可见性；父级不可见时本项仍为 `true`。
    //
    // 它为什么在Anchor层而不是 Widget 层：`IsVisibleInTree()` 要沿父链问祖先，而父链是Anchor的东西。
    // 放进 Widget 会把整棵树的可见性查询抬到上层。
    bool IsVisible() const noexcept { return _visible; }

    /// 自己与全部祖先都可见时为 `true`。
    bool IsVisibleInTree() const noexcept {
        for (const InkingAnchor* node = this; node != nullptr;
             node = node->_parent) {
            if (!node->_visible) {
                return false;
            }
        }
        return true;
    }

    // ---------------- 展示倍率（设计单位 → 设备像素） ----------------
    //
    // 只读。倍率由窗口层写（`SetMagnification` 在 Widget 层，与`GetDeviceWidth/Height` 同处

    // 来源是窗口层，不由组件自己倒推；它不标脏，因为它只影响绘制换算。
    float GetMagnification() const noexcept { return _magnification; }

protected:
    // 只给派生类调：本类不可直接实例化，每个组件声明时必须选一种档位。
    // 根（窗口 / 场景）传 `nullptr` 当 parent。
    //
    // 定位层只需要这一条父链；子表与场景登记由 Widget 层的构造体接手
    // （它在本构造体之后跑，所以 `_parent` 已经就位）。
    InkingAnchor(InkingAnchor* parent, const AnchorData& data) noexcept
        : _selfAnchor(data.selfAnchor),
          _traceAnchor(data.traceAnchor),
          _offsetX(data.offsetX),
          _offsetY(data.offsetY),
          _width(data.width > 0 ? data.width : 0),
          _height(data.height > 0 ? data.height : 0),
          _visible(data.visible),
          _parent(parent) {}
    // 给派生类读自己的父级（定位链）。
    //
    // window 需要的留 Anchor，外部不需要沿定位链走；
    // 业务侧要的父子关系是 Widget 层的 `GetParent()`
    //（返回 `InkingWidget*`，那一份带树语义）。
    // 基类这一份只服务几何推导。
    InkingAnchor* GetAnchorParent() const noexcept { return _parent; }

    // 派生类的定位数据
    // 几何写入口在动态档上（`InkingDynamicWidget`），所以这里的字段由那一档 或布局通道写。
    // 本类只读。

    AnchorPoint _selfAnchor{};   //< 自身锚点
    AnchorPoint _traceAnchor{};  //< 上级锚点（追踪的父级锚点）

    float _offsetX = 0.0f;  ///< 对齐之后的微调，设计坐标
    float _offsetY = 0.0f;

    /**
     * 自身尺寸，设计坐标。
     * 它同时是位置推导的输入，也是 `GetWidth()` / `GetHeight()` 的默认返回值*。
     *
     * 不另存"显示尺寸"的理由：
     * 那份数据必须永远和这一份同步，而写入点散在构造 / 布局通道 / `Resize` / 每态尺寸四处。
     * 漏一处就是"布局按 100 排、 画出来 80"这类静默错。
     * 有"每态尺寸"的组件改为重写那两个虚getter 去返回当前那份，字段仍然是这一个。
     */
    int _width = 0;
    int _height = 0;

    bool _visible = true;  // 自己这一层的可见性

    float _magnification = 1.0f;  // 展示倍率：设计单位 → 设备像素

    /**
     * 父级的定位链（非拥有）。根（窗口 / 场景）为 `nullptr`。
     *
     * 它只承担一件事：让 `GetX/Y` / `IsVisibleInTree` 能沿链往上走。
     * `GetAbsX/Y` 走的也是这条链，但它们的声明在 Widget 层——那里有这条链的
     * 树语义视图（`InkingWidget::GetParent()`，同一条指针的定型转换）。
     *
     * 它不代表树结构："谁是孩子、谁拥有谁、在哪个场景"全在 Widget 层。
     * 两份语义分开之后，定位层可以完全不知道树是怎么组织的。
     *
     * 这条边界别破：一旦本层开始认识"孩子"或"场景"，它就不再是纯定位层，
     * 而"静态组件的几何改不了"那类编译期保证也会跟着松掉。
     */
    InkingAnchor* _parent = nullptr;
};

}  // namespace inking
