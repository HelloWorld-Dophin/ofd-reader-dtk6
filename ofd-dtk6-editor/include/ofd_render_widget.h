/*
 * ofd_render_widget.h — OFD 页面渲染 Widget（DTK6 画布）
 *
 * 继承 QWidget（DTK6 无 DWidget 画布基类），使用 QPainter 实现 OFD 三大元素
 * 完整渲染 + 鼠标命中检测 + 虚线选框交互。
 *
 * 坐标约定：
 *   OFD 物理坐标单位 mm，原点在页面左上角。
 *   渲染时统一 mm→px 缩放 = scaleFactor_，painter 全局 scale + translate 对齐页面原点。
 *
 * 极细描边：qMax(strokeWidthMM * scale_, 0.5) — 保证至少 0.5 设备像素。
 */
#pragma once

#include "ofd_jna_bridge.h"

#include <QWidget>
#include <QPainter>
#include <QColor>
#include <QImage>
#include <QTransform>
#include <QPainterPath>
#include <QString>
#include <QPointF>
#include <QRectF>

#include <memory>

/* ============ 全局字体映射配置 ============ */
// 下拉框选的是"楷体用哪个 family"，我们需要告诉 fontOf()：
// 当 OFD FontName 匹配"楷体/KaiTi"时，映射到哪个 family
struct FontAliasConfig {
    QString kaiFamily;    // 楷体 → Qt family
    QString heiFamily;    // 黑体 → Qt family
    QString songFamily;   // 宋体 → Qt family
    QString fangFamily;   // 仿宋 → Qt family
};

// 读写全局配置（线程安全：单线程 UI 线程，无需锁）
FontAliasConfig fontAliasConfig();
void setFontAliasConfig(const FontAliasConfig& cfg);

/** 启动时强制扫描 fonts/ 目录并加载内置字体。
 *  支持多路径候选（../fonts, fonts/, OFD_EDITOR_FONTS env）。
 *  可在 main() 里提前调用，确保字体在渲染前就绪。
 */
void ofdEditorPreloadFonts();

class OfdRenderWidget : public QWidget {
    Q_OBJECT

public:
    enum class ElementType { None = 0, Text, Image, Path };

    /** 画布上一个被选中的元素描述。 */
    struct SelectedItem {
        ElementType type = ElementType::None;
        int         index = -1;          // 在当前 page 的对应元素数组中的索引
        int         globalIndex = -1;    // 扁平索引（textCount + imageCount + pathCount 内），用于 FFI delete
        QRectF      localBounds;         // 元素在 OFD 本地坐标（mm）下的 bounding box
    };

    explicit OfdRenderWidget(QWidget* parent = nullptr);
    ~OfdRenderWidget() override = default;

    /** 设置/切换要渲染的文档。传入 nullptr 清空。 */
    void setDocument(ofd::OfdJnaBridge::Doc* doc);
    ofd::OfdJnaBridge::Doc* document() const noexcept { return doc_; }

    /** 切换页码（0 起始）。 */
    void setPageIndex(int index);
    int  pageIndex() const noexcept { return pageIndex_; }
    int  pageCount() const;

    /** 缩放系数。1.0 = 原始 72DPI mm→px 比例。 */
    void   setScaleFactor(double factor);
    double scaleFactor() const noexcept { return scaleFactor_; }

    /** 适配窗口。 */
    void fitToWidth();
    void fitToHeight();
    void fitToPage();

    /** 旋转页面（90° 步进）。正数顺时针，负数逆时针。 */
    void rotate(int degrees);

    /** 设置画布背景色（白纸周围的区域）。 */
    void setCanvasColor(const QColor& c);
    QColor canvasColor() const noexcept { return canvasColor_; }

    /** 页面物理尺寸（mm）。 */
    ofd::OfdPageSize pageSize() const noexcept { return pageSize_; }

    /** 当前选中元素（只读）。空 SelectedItem 表示未选中。 */
    const SelectedItem& selectedItem() const noexcept { return selected_; }
    void clearSelection();

    /** 视图坐标 ↔ OFD 文档坐标 转换。 */
    QPointF viewToDoc(const QPointF& viewPos) const;
    QPointF docToView(const QPointF& docPos) const;

signals:
    /** 选中元素改变。参数是当前选中元素（空表示未选中）。 */
    void selectionChanged(const OfdRenderWidget::SelectedItem& selected);
    /** 缩放改变（setScaleFactor 被调）。用于同步 UI 滑块/标签。 */
    void scaleFactorChanged(double factor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    QSize sizeHint() const override;

private:
    /* ============ 渲染主入口 ============ */
    void renderPage(QPainter& painter);
    void renderSelectionFrame(QPainter& painter);

    /* ============ 三类元素渲染 ============ */
    void renderTextItems(QPainter& painter, const ofd::OfdPageElements* elems);
    void renderImageItems(QPainter& painter, const ofd::OfdPageElements* elems);
    void renderPathItems (QPainter& painter, const ofd::OfdPageElements* elems);

    /* ============ 命中检测 ============ */
    /** 在文档坐标（mm）下找最靠近的元素。返回空 SelectedItem 表示没命中。 */
    SelectedItem hitTest(const QPointF& docPointMM) const;

    /** 反查拖选矩形覆盖了哪些文字 item（精确到字符级，按阅读顺序排序） */
    void computeTextHitSelection();

    /* ============ 辅助 ============ */
    static QTransform ctmToTransform(const double* ctm);
    static QPainterPath parsePathData(const char* data);
    static QColor argbToColor(int32_t argb);
    static QImage bytesToImage(const void* data, int32_t size);

    double effectiveStrokeWidth(double strokeWidthMM) const;
    /** 限制 panOffset_ 不把页面拖出窗口太多 */
    void clampPanOffset();

    /* ============ 成员 ============ */
    ofd::OfdJnaBridge::Doc* doc_       = nullptr;   // 非拥有
    int                     pageIndex_  = 0;
    double                  scaleFactor_ = 1.0;      // 1.0 = 72DPI 基准
    int                     rotation_    = 0;        // 页面整体旋转角度（0/90/180/270）
    QColor                  canvasColor_ = QColor(0xF5, 0xF5, 0xF5); // 画布背景（白纸外围）
    ofd::OfdPageSize        pageSize_;               // 物理尺寸 mm
    SelectedItem            selected_;

    // 文字级拖选状态（成员变量，切换页面时自动清理）
    struct TextDragSel {
        bool    dragging = false;
        QPointF startView;
        QPointF endView;
        QVector<int> hitItems;
        QString      hitText;
        bool hasSelection() const { return !hitItems.isEmpty(); }
        void clear() { dragging = false; startView = endView = QPointF();
                       hitItems.clear(); hitText.clear(); }
    } textSel_;

    // 页面元素缓存：避免 paintEvent 每次都调 FFI（原来 4 次 paint × 2 层 = 8 次 readPage！）
    // 只有在 setPageIndex / setDocument / invalidateCache 时才重新读
    ofd::OfdPageElementsGuard cachedPage_;
    bool                      cacheValid_ = false;
    void                      invalidateCache();
    const ofd::OfdPageElements* cachedElements();

    // paintEvent 里算一次 buildPageTransform，存下来让 render 函数共享
    // 保证所有元素（文字、路径、图片、高亮）用同一个 base → 零偏移
    QTransform pageBaseTransform_;

    // 中键拖动画布：页面相对于窗口的额外屏幕像素偏移
    QPointF panOffset_;
    bool    panning_ = false;
    QPointF panStart_;    // 中键按下时的鼠标位置
    QPointF panStartOffset_;  // 中键按下时的 panOffset_ 快照

    // 在 renderTextItems 里算出每个字的屏幕矩形（和 drawText 用同一个 transform/font），
    // 高亮和命中检测直接用，彻底消除单位/变换不一致导致的偏移/空隙问题。
    QVector<QRectF> charScreenRects_;

public:
    /** 文档被修改（增删改元素）后调用，强制下次 paintEvent 重新读 FFI。 */
    void invalidatePageCache() { invalidateCache(); }
};
