#pragma once

/*
 InkingAnchor是整个项目的基类 
 它的作用是作为一个数据结构，来为整个项目提供定位的能力
 */

#include <dataStruct/InkingAnchorData.h>

#include <cstdint>

/**
 * InkingAnchor作为一个底层的数据类，不支持实例化
 * 高级的内容移交给InkingStaticWidget和InkingDynamicWidget
 */
class InkingAnchor {
public:
    // 虚析构：本类要被继承。
    virtual ~InkingAnchor() = default;

    // 禁拷贝 / 移动：持有父指针，复制出的第二份关系会让两条链各指一个对象。
    InkingAnchor(const InkingAnchor&) = delete;
    InkingAnchor& operator=(const InkingAnchor&) = delete;
    InkingAnchor(InkingAnchor&&) = delete;
    InkingAnchor& operator=(InkingAnchor&&) = delete;

    // 自身锚点：自身矩形上的哪个百分比点，去对上级锚点。默认左上角。
    const InkingAnchorPoint& GetSelfAnchor() const noexcept { return _selfAnchor; }

    // 上级锚点：父级矩形上的哪个百分比点。默认左上角。
    const InkingAnchorPoint& GetTraceAnchor() const noexcept { return _traceAnchor; }

    // 对齐之后可以移动，为了可以微调位置，同时也是为了支持动态类
    
    //获取横轴位移
    float GetOffsetX() const noexcept { return _offsetX; }
    //获取纵轴位移
    float GetOffsetY() const noexcept { return _offsetY; }

    //获取相对于父组件左上角的X位置，如果没有父组件，则返回微调的偏移量OffsetX
    float GetX() const noexcept {
        if (_parent == nullptr) {
            return _offsetX;
        }
        return static_cast<float>(_parent->_width) * _traceAnchor.x
             - static_cast<float>(_width) * _selfAnchor.x
             + _offsetX;
    }

    //获取相对于父组件左上角的Y位置，如果没有父组件，则返回微调的偏移量OffsetY
    float GetY() const noexcept {
        if (_parent == nullptr) {
            return _offsetY;
        }
        return static_cast<float>(_parent->_height) * _traceAnchor.y
             - static_cast<float>(_height) * _selfAnchor.y
             + _offsetY;
    }

    /*
    获取宽度和高度，返回的是设计坐标的大小
    之所以使用虚函数，是因为对于有三态的组件，需要返回三态下的大小
    */
   //获取设计宽度
    virtual int GetWidth() const noexcept { return _width; }
    //获取设计高度
    virtual int GetHeight() const noexcept { return _height; }

    //获取相对可见性。注意：由于组件受到父级的影响，父级不可见的情况下，子组件仍返回true
    bool IsVisible() const noexcept { return _visible; }

    // 获取绝对可见性。
    bool IsVisibleInTree() const noexcept {
        for (const InkingAnchor* node = this; node != nullptr;
             node = node->_parent) {
            if (!node->_visible) {
                return false;
            }
        }
        return true;
    }

    //获取倍率。将设计大小转换为像素大小的倍率
    float GetMagnification() const noexcept { return _magnification; }

protected:
    // 非组件子类，例如场景和窗口，传入nullptr 当 parent。
    InkingAnchor(InkingAnchor* parent, const InkingAnchorData& data) noexcept
        : _selfAnchor(data.selfAnchor),
          _traceAnchor(data.traceAnchor),
          _offsetX(data.offsetX),
          _offsetY(data.offsetY),
          _width(data.width > 0 ? data.width : 0),
          _height(data.height > 0 ? data.height : 0),
          _visible(data.visible),
          _parent(parent) {}

    // 获取父级
    InkingAnchor* GetAnchorParent() const noexcept { return _parent; }

    InkingAnchorPoint _selfAnchor{}; //自身锚点
    InkingAnchorPoint _traceAnchor{}; //追踪的父级锚点

    float _offsetX = 0.0f; //横轴微调长度
    float _offsetY = 0.0f;//纵轴微调长度

    int _width = 0;//设计宽度
    int _height = 0;//设计高度

    bool _visible = true;  // 自己这一层的可见性

    float _magnification = 1.0f;  // 倍率，由此推导设计像素到屏幕像素

    InkingAnchor* _parent = nullptr;//父级组件
};

