#pragma once
//InkingAnchor使用的一些数据结构
#include <dataStruct/InkingLayout.h>

#include <cstdint>

//锚点
struct InkingAnchorPoint {
    float x = 0.0f;
    float y = 0.0f;
};

//提供的一些锚点位置的常量
struct PreAnchors {
    static constexpr InkingAnchorPoint LeftTop{0.0f, 0.0f};
    static constexpr InkingAnchorPoint CenterTop{0.5f, 0.0f};
    static constexpr InkingAnchorPoint RightTop{1.0f, 0.0f};
    static constexpr InkingAnchorPoint LeftCenter{0.0f, 0.5f};
    static constexpr InkingAnchorPoint Center{0.5f, 0.5f};
    static constexpr InkingAnchorPoint RightCenter{1.0f, 0.5f};
    static constexpr InkingAnchorPoint LeftBottom{0.0f, 1.0f};
    static constexpr InkingAnchorPoint CenterBottom{0.5f, 1.0f};
    static constexpr InkingAnchorPoint RightBottom{1.0f, 1.0f};
};

// Resize 的哨兵值。某个方向传 none，表示这个方向不动。
struct InkingResize {
    static constexpr int none = -1;
};

//ZIndex的下限，用于后续的夹取
struct InkingZIndex {
    static constexpr int background = -128;
};

//InkingAnchor进行初始化时需要的数据结构
struct InkingAnchorData {
    InkingAnchorPoint selfAnchor{};  //自身锚点
    InkingAnchorPoint traceAnchor{}; //上级锚点（追踪的父级锚点）
    float offsetX = 0.0f;//锚点定好之后可以在横轴上进行微调位移
    float offsetY = 0.0f;//锚点定好之后可以在纵轴上进行微调位移
    int width = 0;//设计宽度大小
    int height = 0;//设计高度大小
    int zIndex = 0;   //越大的数字越靠后渲染，只在兄弟之间比较
    bool visible = true;  //可见性，不可见时会跳过命中
    std::uint32_t color = 0xFF569CD6u;  //占位填充色 0xAARRGGBB
    InkingLayoutSpec layout{};//作为容器的排布意愿，用于表示如何管理我的孩子
    InkingLayoutItem layoutItem{};//作为容器下的内容是，我的布局意愿
};

