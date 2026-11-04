#pragma once

// 静态 / 动态图片共用的数据与观测层（G-02，纯重构）。
//
// `InkingImageLike` 不是 Widget，也不继承任何输入契约：两档各自继承自己的 Widget
// 基类，再多继承本类，避免把 `InkingWidget` 继承成菱形。构造时保存宿主指针，
// 只为读取尺寸与外部显示变换；重绘标脏仍留在两档的薄转发里，遵守“Like 自己
// 不标脏”的既有纪律。
//
// 两档继续声明原有公开函数并转发到这里：这既保住公开头里的签名列表，也保住
// DLL 已经导出的旧符号。真正的数据、资源加载与观测实现只有这里这一份。
// 拖动 / 缩放不在这里——静态档写 A13，动态档写几何，语义不能合并。
//
// 本类没有纯虚函数；代码生成器派生的类不会因为本层而变成抽象类。

#include <image/ImageData.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class InkingWidget;

class InkingImageLike {
public:
    InkingImageLike(const InkingImageLike&) = delete;
    InkingImageLike& operator=(const InkingImageLike&) = delete;

    const InkingShapeSpec& GetShape() const noexcept;
    const InkingButtonAppearance& GetAppearance() const noexcept;
    std::uint32_t GetDisplayColor() const noexcept;
    InkingShapeBounds GetHitEnvelope() const;

    const InkingImageData& GetData() const noexcept;
    const std::string& ImageName() const noexcept;
    std::size_t AssetByteCount() const noexcept;
    bool UsingAssetBytes() const noexcept;
    int AssetLoadCount() const noexcept;

    /**
     * @brief **要不要画形状边缘的羽化带**（dev010 T-10，默认 true）。
     *
     * 游戏精灵（`InkingSprite`）的素材自带 alpha 边缘，那圈 0→1 的过渡带除了
     * 把图元**外扩 0.5 px**（32×32 画成 33×33）之外没有别的作用，而每个图元要多
     * 10 个三角形。关掉之后：
     *
     *   - 图元尺寸**变准**（32×32 就占 32×32），相邻图元不再互相压 1 px；
     *   - 5000 个图元实测省 **2.4 ms**（12 三角形/个 → 2 个）；
     *   - 但边缘**不再抗锯齿**——只有素材自己带 alpha 边缘时才好看。
     *
     * **UI 图片不要关**：纯色块 / 圆角面板关掉会有肉眼可见的硬边。
     * 只对"矩形 + `fit: fill`"生效，圆角 / contain / cover 一律照旧。
     */
    void SetEdgeFeather(bool enabled) noexcept;

    /// 现在画不画羽化带（见 `SetEdgeFeather`）。
    bool IsEdgeFeather() const noexcept;

    bool LoadAsset(const std::string& relative);
    void SetImageBytes(const std::uint8_t* data, std::size_t size,
                       const std::string& name);

protected:
    // 非虚且受保护：Like 不是删除用的多态手柄；也避免给旧组件新增析构 thunk。
    ~InkingImageLike() = default;

    // 派生类须传 static_cast<InkingWidget&>(*this)：直接传 *this 会让禁拷贝
    // 构造函数也成为精确匹配候选，重载决议因此歧义。
    explicit InkingImageLike(InkingWidget& host) noexcept : _host(&host) {}

    /// 构造期装入 Normalize 后的配置与初始纹理名；几何仍由两档各自装。
    void resetImageData(const InkingImageData& data);

    /// 把 normal 与当前资源字节 / 源矩形合成实际绘制外观。
    void refreshRenderAppearance();

    /// 只换源矩形；调用方（具体图片档）负责标重绘脏。
    void SetRenderSourceRect(const InkingShapeBounds& srcRect);
    const InkingShapeBounds& GetRenderSourceRect() const noexcept;

    /// DEBUG 下检查当前素材是否欠采样；Release 为空实现。
    void noticeUndersampled() const;

private:
    InkingWidget* _host;  // 非拥有；与组件同生命周期，构造后永不为空。

    InkingImageData _data{};
    std::vector<std::uint8_t> _assetBytes;
    InkingButtonAppearance _renderAppearance{};

    /// 独立保存：refreshRenderAppearance 会重拷 normal，不能把切格状态冲掉。
    InkingShapeBounds _renderSourceRect{};

    std::string _imageName;
    int _assetLoadCount = 0;

    /// 画不画形状羽化带（dev010 T-10）。默认 true = 与改动前逐像素一致。
    bool _edgeFeather = true;
};

