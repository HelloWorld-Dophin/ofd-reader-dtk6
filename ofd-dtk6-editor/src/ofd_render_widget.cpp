/*
 * ofd_render_widget.cpp — OFD 页面渲染 + 交互实现
 *
 * 坐标系策略（一次设定，所有元素共享）：
 *   OFD 页面物理尺寸 (PhysicalBox) 单位 mm。
 *   渲染前 painter 全局 translate + scale(scaleFactor_)，
 *   后续所有绘制坐标直接用 OFD 原始 mm 值。
 *   scaleFactor_ 含义：1.0 = 屏幕 DPI 基准（pt→mm 需 *25.4/72）。
 *
 * CTM 说明：
 *   OFD CTM (a b c d e f) 是仿射变换矩阵： [[a c e], [b d f], [0 0 1]]
 *   作用于页面原始对象坐标（mm）。
 *
 * 极细描边处理：
 *   strokeWidth (mm) * scaleFactor_ → 设备像素
 *   qMax(value, 0.5) — 保证缩小时线条不消失。
 */

#include "ofd_render_widget.h"

#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QColor>
#include <QImage>
#include <QByteArray>
#include <QBuffer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QDir>
#include <QCoreApplication>
#include <QTextLayout>
#include <QTextOption>
#include <QDebug>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <initializer_list>
#include <cctype>

/* ==================== 字体映射（提前声明，供 renderTextItems 调用） ==================== */

namespace {
QFont fontOf(const ofd::OfdTextItem* item);
} // anon

/* ==================== 构造 ==================== */

OfdRenderWidget::OfdRenderWidget(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setMinimumSize(400, 300);
}

/* ==================== 文档 / 页码 / 缩放 ==================== */

void OfdRenderWidget::setDocument(ofd::OfdJnaBridge::Doc* doc) {
    doc_ = doc;
    pageIndex_ = 0;
    cacheValid_ = false;
    cachedPage_ = {};
    selected_ = {};
    textSel_.clear();
    emit selectionChanged(selected_);
    if (doc_ && doc_->valid()) {
        pageSize_ = doc_->pageSize(0);
    } else {
        pageSize_ = ofd::OfdPageSize{};
    }
    fitToPage();
    update();
}

void OfdRenderWidget::setPageIndex(int index) {
    if (!doc_ || !doc_->valid()) return;
    int n = doc_->pageCount();
    if (index < 0) index = 0;
    if (index >= n) index = n - 1;
    pageIndex_ = index;
    cacheValid_ = false;
    cachedPage_ = {};
    pageSize_ = doc_->pageSize(index);
    selected_ = {};
    textSel_.clear();
    emit selectionChanged(selected_);
    update();
}

int OfdRenderWidget::pageCount() const {
    return doc_ && doc_->valid() ? doc_->pageCount() : 0;
}

void OfdRenderWidget::setScaleFactor(double factor) {
    if (factor < 0.01) factor = 0.01;   // 1%
    if (factor > 10.0) factor = 10.0;    // 1000%
    scaleFactor_ = factor;
    // 注意：这里不要调 clampPanOffset()，因为此时 width()/height() 可能还没被 layout 确定
    // clamp 只在 mouseMoveEvent（用户真的在拖）里跑
    update();
    emit scaleFactorChanged(factor);
}

/** 限制 panOffset_ 不把页面拖出窗口。
 *  白纸左上角 = (marginX + panX, marginY + panY)，右下角 = (marginX+panX+rectW, marginY+panY+rectH)
 *  约束：白纸至少 minVis 在窗口内。 */
void OfdRenderWidget::clampPanOffset() {
    if (pageSize_.width <= 0 || pageSize_.height <= 0) return;
    if (width() <= 0 || height() <= 0) return;

    double nativeW = pageSize_.width  * scaleFactor_;
    double nativeH = pageSize_.height * scaleFactor_;
    bool swapped = (rotation_ % 180 != 0);
    double rectW = swapped ? nativeH : nativeW;
    double rectH = swapped ? nativeW : nativeH;

    double marginX = qMax(0.0, (width() - rectW) / 2.0);
    double marginY = qMax(0.0, (height() - rectH) / 2.0);

    double minVis = 10.0;

    // 白纸左边不能太靠右（否则右边全出了）：marginX + panX ≤ width - minVis
    // 白纸右边不能太靠左（否则左边全出了）：marginX + panX + rectW ≥ minVis
    double panMinX = minVis - marginX - rectW;
    double panMaxX = width() - marginX - minVis;
    panOffset_.setX(qBound(panMinX, panOffset_.x(), panMaxX));

    double panMinY = minVis - marginY - rectH;
    double panMaxY = height() - marginY - minVis;
    panOffset_.setY(qBound(panMinY, panOffset_.y(), panMaxY));
}

void OfdRenderWidget::fitToWidth() {
    panOffset_ = QPointF();
    if (pageSize_.width <= 0) return;
    double w = width() - 40;
    double sf = w / pageSize_.width;
    setScaleFactor(sf);
}

void OfdRenderWidget::fitToHeight() {
    panOffset_ = QPointF();
    if (pageSize_.height <= 0) return;
    double h = height() - 40;
    double sf = h / pageSize_.height;
    setScaleFactor(sf);
}

void OfdRenderWidget::fitToPage() {
    panOffset_ = QPointF();
    if (pageSize_.width <= 0 || pageSize_.height <= 0) return;
    double sf_w = (width() - 40) / pageSize_.width;
    double sf_h = (height() - 40) / pageSize_.height;
    setScaleFactor(std::min(sf_w, sf_h));
}

void OfdRenderWidget::setCanvasColor(const QColor& c) {
    canvasColor_ = c;
    update();
}

void OfdRenderWidget::rotate(int degrees) {
    rotation_ = (rotation_ + degrees) % 360;
    if (rotation_ < 0) rotation_ += 360;
    // 同上，这里不要 clampPanOffset()，等 paintEvent 用正确的 size
    update();
}

/* ==================== 视图 ↔ 文档坐标转换 ==================== */

// 前置声明
static QTransform buildPageTransform(
    const ofd::OfdPageSize& pageSize, double scaleFactor, int rotation,
    int widgetW, int widgetH,
    double* outMarginX = nullptr, double* outMarginY = nullptr);

QPointF OfdRenderWidget::viewToDoc(const QPointF& viewPos) const {
    // 用成员 pageBaseTransform_（paintEvent 算好的）
    bool ok = false;
    QTransform inv = pageBaseTransform_.inverted(&ok);
    if (!ok) return viewPos;
    return inv.map(viewPos);
}

QPointF OfdRenderWidget::docToView(const QPointF& docPos) const {
    return pageBaseTransform_.map(docPos);
}

/* ==================== 选中 ==================== */

void OfdRenderWidget::clearSelection() {
    if (selected_.type != ElementType::None) {
        selected_ = {};
        emit selectionChanged(selected_);
        update();
    }
}

/** 轻量同组判断：两个 text item 属于同一个渲染行（CTM 平移 + y + fontSize + argb 完全一致） */
static bool isSameTextGroup(const ofd::OfdTextItem* a, const ofd::OfdTextItem* b) {
    if (!a || !b) return false;
    if (a->fontSize != b->fontSize) return false;
    if (a->argb != b->argb) return false;
    if (qAbs(a->y - b->y) > 0.01) return false;
    for (int k = 0; k < 6; ++k)
        if (qAbs(a->ctm[k] - b->ctm[k]) > 0.001) return false;
    return true;
}

/** 取字体引擎算出的 mm 宽度（setPixelSize 设 mm 值，FM 返回就是 mm） */
static double fmWidth(const ofd::OfdTextItem* item, double sf) {
    if (!item || !item->content) return 0.1;
    QString text = QString::fromUtf8(reinterpret_cast<const char*>(item->content));
    if (text.isEmpty()) return 0.1;
    QFont f = QFont();
    f.setPixelSize(qRound(item->fontSize));
    QFontMetrics fm(f);
    return qMax(0.001, static_cast<double>(fm.horizontalAdvance(text)) / sf);
}

/* ==================== 文字级拖选反查（成员方法，精确到字符） ==================== */

void OfdRenderWidget::computeTextHitSelection() {
    textSel_.hitItems.clear();
    textSel_.hitText.clear();
    if (!doc_ || !doc_->valid() || textSel_.startView.isNull() || textSel_.endView.isNull()) return;

    const auto* elems = cachedElements();
    if (!elems || elems->textCount <= 0 || !elems->texts) return;

    // 确保 charScreenRects_ 是最新的（paintEvent 里 renderTextItems 已经填好了，
    // 但 mouseMove 事件可能在 paintEvent 之间触发，以防万一触发一次 update）
    if (charScreenRects_.size() != elems->textCount) {
        update();
        return;
    }

    QRectF selRect = QRectF(textSel_.startView, textSel_.endView).normalized();
    auto** ptrs = reinterpret_cast<void**>(elems->texts);

    // 直接用 renderTextItems 缓存的屏幕矩形做命中！
    struct Hit { int idx; double x; };
    QVector<Hit> hits;
    hits.reserve(elems->textCount);

    for (int i = 0; i < elems->textCount; ++i) {
        const QRectF& sr = charScreenRects_[i];
        if (sr.isNull()) continue;
        if (sr.intersects(selRect)) {
            hits.append({i, sr.x()});
        }
    }

    // 按阅读顺序排序（先 y 再 x，简单的按屏幕 x 坐标排序够用）
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.x < b.x;
    });

    // 去重 + 收集
    QSet<int> seen;
    for (const auto& h : hits) {
        if (!seen.contains(h.idx)) {
            seen.insert(h.idx);
            textSel_.hitItems.append(h.idx);
            auto* item = reinterpret_cast<const ofd::OfdTextItem*>(ptrs[h.idx]);
            if (item && item->content) {
                textSel_.hitText += QString::fromUtf8(
                    reinterpret_cast<const char*>(item->content));
            }
        }
    }
}

/* ==================== paintEvent ==================== */

/** 构建包含 margin + scale + rotation 的 baseTransform（mm→屏幕px） */
static QTransform buildPageTransform(
    const ofd::OfdPageSize& pageSize, double scaleFactor, int rotation,
    int widgetW, int widgetH, double* outMarginX, double* outMarginY
) {
    double nativeW = pageSize.width  * scaleFactor;
    double nativeH = pageSize.height * scaleFactor;
    bool swapped = (rotation % 180 != 0);
    double rectW = swapped ? nativeH : nativeW;
    double rectH = swapped ? nativeW : nativeH;

    double marginX = (widgetW - rectW) / 2.0;
    double marginY = (widgetH - rectH) / 2.0;
    if (marginX < 0) marginX = 0;
    if (marginY < 0) marginY = 0;
    if (outMarginX) *outMarginX = marginX;
    if (outMarginY) *outMarginY = marginY;

    // 旋转补偿：先把旋转后的 page bbox 左上角对齐到 (0,0)
    double pcx = pageSize.x + pageSize.width  / 2.0;
    double pcy = pageSize.y + pageSize.height / 2.0;
    QRectF pageMm(pageSize.x, pageSize.y, pageSize.width, pageSize.height);
    QTransform rot;
    rot.translate(pcx, pcy);
    rot.rotate(rotation);
    rot.translate(-pcx, -pcy);
    QRectF rotatedMm = rot.mapRect(pageMm);

    QTransform t;
    // 屏幕坐标 scale → mm 坐标
    t.translate(marginX, marginY);
    t.scale(scaleFactor, scaleFactor);
    // 让旋转后的 bbox 左上角对齐原点
    t.translate(-rotatedMm.left(), -rotatedMm.top());
    // 绕中心旋转
    t.translate(pcx, pcy);
    t.rotate(rotation);
    t.translate(-pcx, -pcy);
    // 页面原点归零
    if (pageSize.x != 0 || pageSize.y != 0) {
        t.translate(-pageSize.x, -pageSize.y);
    }
    return t;
}

void OfdRenderWidget::paintEvent(QPaintEvent* /*event*/) {
    // ★ paintEvent 调用计数
    static int paintCnt = 0;
    fprintf(stderr, "[PAINT] #%d\n", ++paintCnt);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.fillRect(rect(), canvasColor_);

    if (!doc_ || !doc_->valid() || pageSize_.width <= 0) {
        painter.setPen(QColor(0x88, 0x88, 0x88));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("就绪。请先打开 OFD 文件。"));
        return;
    }

    // 画白色页面背景 + 边框（屏幕坐标）
    // 只算一次 buildPageTransform，存进成员 pageBaseTransform_，所有地方共享
    double marginX, marginY;
    pageBaseTransform_ = buildPageTransform(pageSize_, scaleFactor_, rotation_,
                                         width(), height(), &marginX, &marginY);
    // 中键拖动画布：额外平移 panOffset_（屏幕像素）
    pageBaseTransform_.translate(panOffset_.x(), panOffset_.y());
    QTransform& base = pageBaseTransform_;

    // 白纸也用 base transform 算！这样和 OFD 内容 100% 同位置！
    QRectF docRect(pageSize_.x, pageSize_.y, pageSize_.width, pageSize_.height);
    QRectF pageRect = base.mapRect(docRect);
    painter.fillRect(pageRect, Qt::white);
    painter.setPen(QPen(QColor(0xCC, 0xCC, 0xCC), 1.0));
    painter.drawRect(pageRect);

    painter.save();
    // clip 在 base transform 下画 mm 坐标，和白纸 docRect 完全一致
    painter.setTransform(base);
    painter.setClipRect(docRect);

    renderPage(painter);
    painter.restore();

    // 公章选框
    renderSelectionFrame(painter);
}

void OfdRenderWidget::renderPage(QPainter& painter) {
    auto page = doc_->readPage(pageIndex_);
    fprintf(stderr, "[renderPage] pageIndex=%d valid=%d\n", pageIndex_, page.valid() ? 1 : 0);
    if (!page.valid()) {
        fprintf(stderr, "[renderPage] readPage FAILED!\n");
        return;
    }
    const auto* elems = page.get();
    fprintf(stderr, "[renderPage] elems texts=%d paths=%d imgs=%d\n",
        elems ? elems->textCount : -1, elems ? elems->pathCount : -1, elems ? elems->imageCount : -1);

    // 顺序：Path → Image → Text（前景）
    renderPathItems(painter, elems);
    renderImageItems(painter, elems);
    renderTextItems(painter, elems);
}

/* ==================== 页面元素缓存 ==================== */

void OfdRenderWidget::invalidateCache() {
    cacheValid_ = false;
    cachedPage_ = {};
}

const ofd::OfdPageElements* OfdRenderWidget::cachedElements() {
    if (!doc_ || !doc_->valid()) return nullptr;
    if (!cacheValid_) {
        cachedPage_ = doc_->readPage(pageIndex_);
        cacheValid_ = true;
    }
    return cachedPage_.valid() ? cachedPage_.get() : nullptr;
}

QSize OfdRenderWidget::sizeHint() const {
    if (pageSize_.width > 0 && pageSize_.height > 0) {
        int w = static_cast<int>(pageSize_.width  * scaleFactor_) + 60;
        int h = static_cast<int>(pageSize_.height * scaleFactor_) + 60;
        return QSize(w, h);
    }
    return QSize(600, 800);
}

/* ==================== 命中检测 ==================== */

OfdRenderWidget::SelectedItem OfdRenderWidget::hitTest(const QPointF& docPt) const {
    if (!doc_ || !doc_->valid() || pageSize_.width <= 0) return {};

    auto page = doc_->readPage(pageIndex_);
    if (!page.valid()) return {};
    const auto* elems = page.get();

    if (docPt.x() < pageSize_.x || docPt.y() < pageSize_.y ||
        docPt.x() > pageSize_.x + pageSize_.width ||
        docPt.y() > pageSize_.y + pageSize_.height) {
        return {};
    }

    // 阅读器阶段：只检测公章（红色描边 + 椭圆路径 + 纯旋转 CTM 的 Path）
    // 其他元素（Text/Image/普通 Path）不进入命中检测
    if (elems->pathCount > 0 && elems->paths) {
        auto** ptrs = reinterpret_cast<void**>(elems->paths);
        for (int i = elems->pathCount - 1; i >= 0; --i) {
            auto* item = reinterpret_cast<const ofd::OfdPathItem*>(ptrs[i]);
            if (!item || !item->data) continue;

            // 公章特征 1：红色描边（strokeArgb R 通道 > 200, G+B < 100）
            uint32_t sargb = static_cast<uint32_t>(item->strokeArgb);
            uint8_t r = (sargb >> 16) & 0xFF;
            uint8_t g = (sargb >> 8)  & 0xFF;
            uint8_t b =  sargb        & 0xFF;
            bool redStroke = (r > 180 && g < 120 && b < 120);

            // 公章特征 2：纯旋转 CTM（无 translate：ctm[4]==ctm[5]==0）
            double a = item->ctm[0], b1 = item->ctm[1];
            double tx = item->ctm[4], ty = item->ctm[5];
            bool pureRotate = (qAbs(tx) < 0.01 && qAbs(ty) < 0.01)
                           && (qAbs(a*a + b1*b1 - 1.0) < 0.05);  // 旋转矩阵正交

            // 公章特征 3：路径包含椭圆指令（A 指令）
            bool hasArc = false;
            QByteArray data = QByteArray::fromRawData(
                reinterpret_cast<const char*>(item->data),
                qstrlen(reinterpret_cast<const char*>(item->data)));
            hasArc = data.contains('A') || data.contains('a');

            if (!redStroke || !pureRotate || !hasArc) continue;

            // 命中检测：路径 boundingRect
            QPainterPath path = parsePathData(reinterpret_cast<const char*>(item->data));
            if (path.isEmpty()) continue;
            QTransform t = ctmToTransform(item->ctm);
            QPainterPath mapped = t.map(path);
            QRectF bb = mapped.boundingRect();
            if (bb.isNull() || bb.width() < 1e-3) continue;
            if (bb.contains(docPt)) {
                SelectedItem s;
                s.type = ElementType::Path;
                s.index = i;
                s.globalIndex = elems->textCount + elems->imageCount + i;
                s.localBounds = bb;
                return s;
            }
        }
    }

    // Text/Image/普通 Path → 阅读器阶段不选中
    return {};
}

/* ==================== mousePressEvent ==================== */

// 文本拖选 + 右键菜单 + 公章命中（不发 selectionChanged）

void OfdRenderWidget::mousePressEvent(QMouseEvent* event) {
    if (!doc_ || !doc_->valid()) {
        event->accept();
        return;
    }

    // 中键：开始拖动画布
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        panStart_ = event->position();
        panStartOffset_ = panOffset_;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // 右键：弹复制菜单
    if (event->button() == Qt::RightButton) {
        QMenu menu(this);
        QAction* actCopy = menu.addAction(QStringLiteral("复制选中文本"));
        QAction* actSelAll = menu.addAction(QStringLiteral("全选页面文本"));
        QAction* act = menu.exec(event->globalPosition().toPoint());
        if (act == actSelAll) {
            // 遍历所有 text items 合并作为"全选"
            computeTextHitSelection();  // 先清掉拖选
            // 手动全选：把 startView 设为页面左上角、endView 设为右下角
            auto page = doc_->readPage(pageIndex_);
            if (page.valid() && page.get()->textCount > 0) {
                textSel_.startView = docToView(QPointF(pageSize_.x, pageSize_.y));
                textSel_.endView   = docToView(QPointF(pageSize_.x + pageSize_.width,
                                                         pageSize_.y + pageSize_.height));
                computeTextHitSelection();
            }
            update();
        } else if (act == actCopy) {
            if (textSel_.hasSelection()) {
                QApplication::clipboard()->setText(textSel_.hitText);
            }
        }
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }

    QPointF docPt = viewToDoc(event->position());
    SelectedItem hit = hitTest(docPt);

    if (hit.type != ElementType::None) {
        // 公章可以选中高亮，但不发 selectionChanged（不触发属性面板）
        if (selected_.type != hit.type || selected_.index != hit.index) {
            selected_ = hit;
        }
        textSel_.dragging = false;
        textSel_.startView = QPointF();
        textSel_.endView   = QPointF();
        textSel_.hitItems.clear();
        textSel_.hitText.clear();
        update();
        event->accept();
        return;
    }

    // 空白处 → 开始文字级拖选
    clearSelection();  // 清掉公章选框
    textSel_.dragging  = true;
    textSel_.startView = event->position();
    textSel_.endView   = event->position();
    textSel_.hitItems.clear();
    textSel_.hitText.clear();
    update();
    event->accept();
}

void OfdRenderWidget::mouseMoveEvent(QMouseEvent* event) {
    // 中键拖动画布
    if (panning_) {
        QPointF delta = event->position() - panStart_;
        const double kDamping = 0.5;  // 阻尼：鼠标动 100px 画布动 50px
        panOffset_ = panStartOffset_ + delta * kDamping;
        clampPanOffset();
        update();
        event->accept();
        return;
    }

    if (!textSel_.dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    textSel_.endView = event->position();
    computeTextHitSelection();
    update();
    event->accept();
}

void OfdRenderWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        unsetCursor();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && textSel_.dragging) {
        textSel_.dragging = false;
        // 只有拖动距离 > 3px 才算选中（避免误点）
        if (QPointF(textSel_.endView - textSel_.startView).manhattanLength() < 3) {
            textSel_.startView = QPointF();
            textSel_.endView   = QPointF();
            textSel_.hitItems.clear();
            textSel_.hitText.clear();
        }
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

/* ==================== keyPressEvent: Ctrl+C 复制 ==================== */

void OfdRenderWidget::keyPressEvent(QKeyEvent* event) {
    if ((event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
        && event->key() == Qt::Key_C) {
        if (textSel_.hasSelection()) {
            QApplication::clipboard()->setText(textSel_.hitText);
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

/* ==================== wheelEvent: Ctrl+滚轮缩放 ==================== */

void OfdRenderWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        double delta = event->angleDelta().y();
        double factor = (delta > 0) ? 1.15 : 1.0 / 1.15;
        setScaleFactor(scaleFactor_ * factor);
        event->accept();
    } else {
        QWidget::wheelEvent(event);
    }
}

/* ==================== 选框绘制 ==================== */

void OfdRenderWidget::renderSelectionFrame(QPainter& painter) {
    if (selected_.type == ElementType::None || selected_.localBounds.isNull()) return;

    QRectF viewRect = QRectF(docToView(selected_.localBounds.topLeft()),
                              docToView(selected_.localBounds.bottomRight()));
    // 给一点 padding
    viewRect.adjust(-2, -2, 2, 2);

    QPen pen(QColor(0x1A, 0x7F, 0xFF));
    pen.setWidthF(1.5);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({6, 4});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(viewRect);
}

/* ==================== 文本渲染 ==================== */

/**
 * 从内存里读 OfdGradient + stops[]，构造 QBrush（线性/径向渐变）。
 * 返回 QBrush() 表示无可用渐变。
 *
 * 内存布局（与 Java 侧 parseGradient 完全对应）：
 *   header 48B: type(4+4pad), startX(8), startY(8), endX(8), endY(8), segCount(4+4pad)
 *   stops[segCount] 每个 16B: position(8), argb(4+4pad)
 */
static QBrush gradientBrush(const void* gradPtr) {
    if (!gradPtr) return {};
    auto* base = reinterpret_cast<const uint8_t*>(gradPtr);

    int32_t type = *reinterpret_cast<const int32_t*>(base + 0);
    if (type != 1 && type != 2) return {};

    double startX = *reinterpret_cast<const double*>(base + 8);
    double startY = *reinterpret_cast<const double*>(base + 16);
    double endX   = *reinterpret_cast<const double*>(base + 24);
    double endY   = *reinterpret_cast<const double*>(base + 32);
    int32_t segCount = *reinterpret_cast<const int32_t*>(base + 40);
    if (segCount <= 0) return {};

    static int printed = 0;
    if (printed++ < 3) {
        fprintf(stderr, "[GRAD] type=%d start=(%.2f,%.2f) end=(%.2f,%.2f) segs=%d ptr=%p\n",
            type, startX, startY, endX, endY, segCount, gradPtr);
    }

    QGradient* grad = nullptr;
    if (type == 1) {
        grad = new QLinearGradient(startX, startY, endX, endY);
    } else {
        double r = std::hypot(endX - startX, endY - startY);
        if (r < 0.001) r = 1.0;
        grad = new QRadialGradient(startX, startY, r);
    }

    // ★ LogicalMode：Java 侧存的是 OFD 原始页面级 mm 绝对坐标！
    // 直接把它们作为渐变的逻辑坐标，在 painter 的当前 transform 下生效。
    // 这样同一页面上不同 TextObject/PathObject 共享同一个渐变空间，
    // 即使它们分属不同对象，渐变也能连续跨字/跨形状！
    grad->setCoordinateMode(QGradient::LogicalMode);
    grad->setSpread(QGradient::ReflectSpread);  // OFD Extend=3

    auto* stopsBase = reinterpret_cast<const uint8_t*>(base + 48);
    for (int i = 0; i < segCount; i++) {
        long long off = i * 16LL;
        double pos = *reinterpret_cast<const double*>(stopsBase + off);
        int32_t argb = *reinterpret_cast<const int32_t*>(stopsBase + off + 8);
        uint32_t v = static_cast<uint32_t>(argb);
        int a = (v >> 24) & 0xFF;
        int r = (v >> 16) & 0xFF;
        int g = (v >>  8) & 0xFF;
        int b = (v      ) & 0xFF;
        if (printed <= 3) {
            fprintf(stderr, "  stop[%d] pos=%.4f color=(%d,%d,%d,%d)\n", i, pos, r, g, b, a);
        }
        if (a == 0 && r == 0 && g == 0 && b == 0) continue;
        if (a == 0) a = 255;
        grad->setColorAt(pos, QColor(r, g, b, a));
    }

    QBrush brush(*grad);
    delete grad;
    return brush;
}

void OfdRenderWidget::renderTextItems(QPainter& painter, const ofd::OfdPageElements* elems) {
    static int rtiCnt = 0;
    fprintf(stderr, "[RENDER_TEXT] call #%d textCount=%d\n", ++rtiCnt, elems ? elems->textCount : -1);
    if (!elems || elems->textCount <= 0 || !elems->texts) return;

    auto** ptrs = reinterpret_cast<void**>(elems->texts);
    int count   = elems->textCount;

    // ★ 屏幕空间坐标重复检查（transform 后）
    static int screenDumpDone = 0;
    if (!screenDumpDone && count > 100) {
        screenDumpDone = 1;
        fprintf(stderr, "=== SCREEN DUP CHECK (count=%d) ===\n", count);
        struct ScreenEntry { double sx, sy; QString txt; };
        QVector<ScreenEntry> entries;
        for (int i = 0; i < count; i++) {
            auto* it = reinterpret_cast<const ofd::OfdTextItem*>(ptrs[i]);
            if (!it || !it->content) continue;
            QTransform ctm = ctmToTransform(it->ctm);
            QTransform cb = ctm * pageBaseTransform_;
            QPointF sp = cb.map(QPointF(it->x, it->y));
            entries.push_back({sp.x(), sp.y(), QString::fromUtf8((const char*)it->content)});
        }
        // 查屏幕空间重复（坐标差 < 2px + 文字相同）
        int sd = 0;
        for (int a = 0; a < entries.size(); a++) {
            for (int b = a+1; b < qMin(entries.size(), a+30); b++) {
                double dx = entries[a].sx - entries[b].sx;
                double dy = entries[a].sy - entries[b].sy;
                if (qAbs(dx) < 3.0 && qAbs(dy) < 3.0 && entries[a].txt == entries[b].txt) {
                    if (sd < 20) fprintf(stderr, "[SCR_DUP] a=%d b=%d dx=%.1f dy=%.1f t=[%s]\n",
                        a, b, dx, dy, entries[a].txt.toUtf8().constData());
                    sd++;
                }
            }
        }
        fprintf(stderr, "=== SCR_DUP total=%d ===\n", sd);
        // 打印前 25 个的屏幕坐标
        fprintf(stderr, "=== FIRST 25 SCREEN POS ===\n");
        for (int i = 0; i < qMin(25, entries.size()); i++) {
            fprintf(stderr, "[SCR#%d] sx=%.1f sy=%.1f t=[%s]\n",
                i, entries[i].sx, entries[i].sy, entries[i].txt.toUtf8().constData());
        }
    }

    // 提前分配缓存
    charScreenRects_.clear();
    charScreenRects_.fill(QRectF(), count);

    // 把 hitItems 转成 QSet 做 O(1) 查找
    QSet<int> hitSet(textSel_.hitItems.begin(), textSel_.hitItems.end());

    // 预算选中色（避免循环内重复调 palette）
    QColor selBgColor = palette().color(QPalette::Highlight);
    selBgColor.setAlpha(90);

    for (int i = 0; i < count; ++i) {
        auto* item = reinterpret_cast<const ofd::OfdTextItem*>(ptrs[i]);
        if (!item || !item->content) continue;

        QByteArray ba(reinterpret_cast<const char*>(item->content));
        QString text = QString::fromUtf8(ba);
        if (text.isEmpty()) continue;

        QColor color = argbToColor(item->argb);
        if (!color.isValid() || color.alpha() == 0) continue;

        QTransform itemCTM = ctmToTransform(item->ctm);
        // Qt A*B = 先 A 后 B (Qt 官方文档: "This transform is applied first, then the matrix")
        // 所以 itemCTM * pageBase = 先 itemCTM(本地→mm) 后 pageBase(mm→屏幕) ✓
        QTransform combined = itemCTM * pageBaseTransform_;

        // ★ 公章字 + 普通页面打 combined.matrix() debug
        {
            static bool printed = false;
            double a=item->ctm[0], b=item->ctm[1], c=item->ctm[2], d=item->ctm[3];
            bool isStamp = (std::abs(a) < 0.99 || std::abs(b) > 0.01 || std::abs(c) > 0.01 || std::abs(d) < 0.99);
            if (isStamp || (!printed && item->fontSize > 4.0)) {
                fprintf(stderr, "[CTM_CHECK] %s char=[%s]\n",
                        isStamp ? "STAMP" : "PLAIN", text.toUtf8().constData());
                fprintf(stderr, "  itemCTM=(%.4f,%.4f,%.4f,%.4f,%.4f,%.4f)\n",
                        itemCTM.m11(),itemCTM.m12(),itemCTM.m21(),itemCTM.m22(),itemCTM.dx(),itemCTM.dy());
                fprintf(stderr, "  combined=(%.4f,%.4f,%.4f,%.4f,%.4f,%.4f)\n",
                        combined.m11(),combined.m12(),combined.m21(),combined.m22(),combined.dx(),combined.dy());
                fprintf(stderr, "  combined.map(%.2f,%.2f)=(%.2f,%.2f)\n",
                        item->x, item->y, combined.map(QPointF(item->x, item->y)).x(),
                        combined.map(QPointF(item->x, item->y)).y());
                if (!isStamp) printed = true;
            }
        }

        // QFont pixelSize 是固有像素，但 painter.setTransform 会自动缩放 drawText 字体！
        // 所以 pixelSize 直接用 OFD fontSize(mm) 值，transform 里的 scale 自动放大到屏幕像素
        QFont font = fontOf(item);
        font.setPixelSize(qMax(1, qRound(item->fontSize)));
        font.setStyleStrategy(QFont::PreferAntialias);

        // === 公章字诊断日志 ===
        {
            double a=item->ctm[0], b=item->ctm[1], c=item->ctm[2], d=item->ctm[3];
            if (std::abs(a) < 0.99 || std::abs(b) > 0.01 || std::abs(c) > 0.01 || std::abs(d) < 0.99) {
                QPointF result = itemCTM.map(QPointF(item->x, item->y));
                double angle = std::atan2(b, a) * 180.0 / M_PI;
                fprintf(stderr, "[C++Stamp] char=[%s] OFD_CTM=(%.2f,%.2f,%.2f,%.2f,%.2f,%.2f) angle=%.1f° TextCode=(%.2f,%.2f) → apply=(%.2f,%.2f)\n",
                        text.toUtf8().constData(), a, b, c, d, item->ctm[4], item->ctm[5],
                        angle, item->x, item->y, result.x(), result.y());
            }
        }

        painter.save();
        painter.resetTransform();
        painter.setTransform(combined, false);
        painter.setFont(font);

        // ===== 核心 insight =====
        // setPixelSize(mm值) 让 font.pixelSize() ≈ mm值（如 3.5mm → 4px）
        // FM.ascent() / font.pixelSize() = 字体的 ascent 比例（和 font 大小无关！）
        // 乘以 fontSize_mm 就得到正确的 mm 值！
        QFontMetrics fm = painter.fontMetrics();
        double pxSize = qMax(1.0, (double)font.pixelSize());
        double ascent_mm  = fm.ascent()      / pxSize * item->fontSize;
        double height_mm   = fm.height()     / pxSize * item->fontSize;
        double advance_mm  = fm.horizontalAdvance(text) / pxSize * item->fontSize;

        // 同组字：宽度用 DeltaX 差值（最精确）
        double charW_mm = advance_mm;
        if (i + 1 < count) {
            auto* nx = reinterpret_cast<const ofd::OfdTextItem*>(ptrs[i + 1]);
            if (nx && isSameTextGroup(item, nx) && nx->x > item->x - 0.01) {
                charW_mm = nx->x - item->x;  // OFD DeltaX，精确
            }
        }
        charW_mm = qMax(0.001, charW_mm);

        // 本地矩形（mm，和 drawText 同坐标系）
        QRectF localRect(item->x, item->y - ascent_mm, charW_mm, height_mm);
        localRect.adjust(-0.05, -0.05, 0.05, 0.05);
        charScreenRects_[i] = combined.mapRect(localRect);

        // 命中的字：在同一个 combined 下画背景（含旋转！）
        if (hitSet.contains(i)) {
            painter.fillRect(localRect, selBgColor);
        }

        // ★ 公章字 debug 视觉标记已清理（红色短线、绿色方向线、蓝色圆点）

        // 画字（在背景上）—— 优先渐变
        QBrush gradBrush = gradientBrush(item->gradient);
        if (gradBrush.style() != Qt::NoBrush) {
            // ★ gradBrush 用 LogicalMode，坐标是页面 mm 绝对坐标（OFD StartPoint/EndPoint）
            // painter 当前 transform = base × itemCTM，所以：
            //   1. textPath 在 item CTM 局部空间画（item->x, item->y 是 CTM 局部坐标）
            //   2. 用 itemCTM map → 页面 mm 空间
            //   3. painter 切回仅 base transform → fillPath（页面 mm 空间和 gradBrush 一致！）
            painter.setPen(Qt::NoPen);
            QPainterPath textPath;
            textPath.addText(QPointF(item->x, item->y), font, text);

            QTransform itemCTM(item->ctm[0], item->ctm[1], item->ctm[2],
                               item->ctm[3], item->ctm[4], item->ctm[5]);
            QPainterPath pageSpacePath = itemCTM.map(textPath);

            painter.save();
            painter.resetTransform();
            painter.setTransform(pageBaseTransform_, false);
            painter.fillPath(pageSpacePath, gradBrush);
            painter.restore();
        } else {
            painter.setPen(color);
            painter.drawText(QPointF(item->x, item->y), text);
        }

        painter.restore();
    }
}

/* ==================== 字体映射 + 自动加载 fonts/ 目录 ==================== */

/* ============ 全局 FontAliasConfig 实现 ============ */
namespace {
FontAliasConfig g_fontAliasConfig;  // 空字段表示未设置，fontOf() 走默认 alias 表
}
FontAliasConfig fontAliasConfig() { return g_fontAliasConfig; }
void setFontAliasConfig(const FontAliasConfig& cfg) { g_fontAliasConfig = cfg; }

namespace {

/** 启动时扫描可执行文件同级 fonts/ 目录，用 QFontDatabase 加载所有 .ttf/.otf/.ttc。
 *  支持多路径候选：
 *    1) 可执行文件所在目录的上一级的 fonts/  ← 发布版 /opt/ofd-editor/fonts
 *    2) 可执行文件所在目录的 fonts/         ← 开发版 bin/fonts
 *    3) OFD_EDITOR_FONTS 环境变量指向的目录 ← 调试覆盖
 */
void loadBundledFonts() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;

    QString appDir = QCoreApplication::applicationDirPath();

    QStringList candidates;
    // ① 发布版：bin/ → 上一级 → fonts/
    candidates << QDir(appDir).absoluteFilePath("../fonts");
    // ② 开发版：bin/fonts（CMake 拷贝到 build/fonts）
    candidates << QDir(appDir).absoluteFilePath("fonts");
    // ③ 环境变量覆盖
    QString envFonts = qEnvironmentVariable("OFD_EDITOR_FONTS");
    if (!envFonts.isEmpty()) candidates << envFonts;

    QStringList filters = {"*.ttf", "*.otf", "*.ttc", "*.TTF", "*.OTF", "*.TTC"};

    for (const QString& fontsDir : candidates) {
        QDir dir(fontsDir);
        if (!dir.exists()) {
            fprintf(stderr, "[Font] skip (not found): %s\n", fontsDir.toUtf8().constData());
            continue;
        }
        QStringList fontFiles = dir.entryList(filters, QDir::Files);
        fprintf(stderr, "[Font] scanning: %s (%d files)\n",
                fontsDir.toUtf8().constData(), fontFiles.size());
        for (const QString& fn : fontFiles) {
            QString path = dir.absoluteFilePath(fn);
            int id = QFontDatabase::addApplicationFont(path);
            if (id >= 0) {
                QStringList families = QFontDatabase::applicationFontFamilies(id);
                fprintf(stderr, "[Font] loaded %s → %s (id=%d)\n",
                        path.toUtf8().constData(),
                        families.join(", ").toUtf8().constData(), id);
            } else {
                fprintf(stderr, "[Font] FAILED to load %s\n", path.toUtf8().constData());
            }
        }
    }
}

/** OFD FontName → 系统可用 QFont.Family 查找。
 *  策略：1) 直接用 OFD 原始 FontName 查 Qt（含已加载的 bundled）
 *        2) 常见中英文别名互查（KaiTi ↔ 楷体 ↔ 楷体_GB2312）
 *        3) 都找不到 → fallback 到 Noto Sans CJK SC（宽字体，不会显小）
 */
QFont fontOf(const ofd::OfdTextItem* item) {
    if (!item || !item->fontNamePtr) {
        if (item) {
            double a=item->ctm[0], b=item->ctm[1];
            if (std::abs(a) < 0.99 || std::abs(b) > 0.01) {
                fprintf(stderr, "[Font] STAMP char NO FONTNAME PTR! returning default QFont\n");
            }
        }
        return QFont();
    }

    loadBundledFonts(); // 只执行一次

    QByteArray raw(reinterpret_cast<const char*>(item->fontNamePtr));
    QString ofdName = QString::fromUtf8(raw).trimmed();
    if (ofdName.isEmpty()) {
        double a=item->ctm[0], b=item->ctm[1];
        if (std::abs(a) < 0.99 || std::abs(b) > 0.01) {
            fprintf(stderr, "[Font] STAMP char EMPTY FONTNAME! returning default QFont\n");
        }
        return QFont();
    }

    QStringList families = QFontDatabase::families();

    // ===== Step 1: 直接用 OFD 原始名查 =====
    if (families.contains(ofdName, Qt::CaseInsensitive)) {
        fprintf(stderr, "[Font] direct match: %s\n", ofdName.toUtf8().constData());
        return QFont(ofdName);
    }

    // ===== Step 1.5: 全局可配置 alias（来自 UI 下拉框）—— 优先级高于硬编码表 =====
    {
        FontAliasConfig cfg = g_fontAliasConfig; // 拷贝，避免跨命名空间访问
        auto tryMatch = [&](const char* classKey, const QString& targetFamily) -> bool {
            if (targetFamily.isEmpty()) return false;  // 未设置
            // classKey 匹配：检查 OFD FontName 是否属于这个类
            struct ClassKeywords { const char* key; std::initializer_list<const char*> names; };
            static const ClassKeywords classes[] = {
                { "楷体", { "楷体", "KaiTi", "STKaiti", "华文楷体" } },
                { "黑体", { "黑体", "SimHei", "微软雅黑", "Microsoft YaHei" } },
                { "宋体", { "宋体", "SimSun", "思源宋体", "Source Han Serif" } },
                { "仿宋", { "仿宋", "FangSong", "FangSong_GB2312" } },
            };
            for (const auto& c : classes) {
                if (strcmp(c.key, classKey) != 0) continue;
                for (const char* nm : c.names) {
                    if (ofdName.compare(QString::fromUtf8(nm), Qt::CaseInsensitive) == 0) {
                        fprintf(stderr, "[Font] UI→%s: %s → %s\n",
                                classKey, ofdName.toUtf8().constData(),
                                targetFamily.toUtf8().constData());
                        return true;
                    }
                }
            }
            return false;
        };
        QString target;
        if      (tryMatch("楷体", cfg.kaiFamily))  target = cfg.kaiFamily;
        else if (tryMatch("黑体", cfg.heiFamily))  target = cfg.heiFamily;
        else if (tryMatch("宋体", cfg.songFamily)) target = cfg.songFamily;
        else if (tryMatch("仿宋", cfg.fangFamily)) target = cfg.fangFamily;

        if (!target.isEmpty()) {
            // 全局配置里的 family 名可能不精确匹配 Qt 注册名，做一次模糊查找
            for (const QString& fam : families) {
                if (fam.compare(target, Qt::CaseInsensitive) == 0) {
                    fprintf(stderr, "[Font] UI match final: %s\n", fam.toUtf8().constData());
                    return QFont(fam);
                }
            }
            // 直接用（哪怕 Qt 找不到也不会 crash，QFont 会回退）
            return QFont(target);
        }
    }

    // ===== Step 2: 别名互查（硬编码默认，含项目自带开源字体）=====
    struct Alias { const char* primary; const char* alt1; const char* alt2; };
    static const Alias aliases[] = {
        // 项目 fonts/ 目录里的开源字体（优先）
        {"黑体",        "SimXiHei Plus",  "新晰黑体＋"},
        {"SimHei",      "SimXiHei Plus",  nullptr},
        {"宋体",        "SimZhiSong",     "新致宋体"},
        {"SimSun",      "SimZhiSong",     nullptr},
        {"仿宋",        "Zhuque Fangsong (technical preview)", "朱雀仿宋（预览测试版）"},
        {"FangSong",    "Zhuque Fangsong (technical preview)", nullptr},
        {"楷体",        "ZhenKai_GBK",    "臻楷_GBK"},
        {"KaiTi",       "ZhenKai_GBK",     nullptr},
        {"Courier New", "Liberation Mono", nullptr},
        {"Courier",     "Liberation Mono", nullptr},
        // 系统常见字体（兜底）
        {"STKaiti",     "华文楷体",       nullptr},
        {"华文楷体",     "STKaiti",        nullptr},
        {"微软雅黑",     "Microsoft YaHei", nullptr},
        {"思源黑体",     "Source Han Sans CN", nullptr},
        {"思源宋体",     "Source Han Serif SC", nullptr},
        {"Noto Sans SC", "Noto Sans CJK SC", nullptr},
        {"Noto Serif SC","Noto Serif CJK SC", nullptr},
    };

    for (const auto& a : aliases) {
        bool matchPrimary = ofdName.compare(QString::fromUtf8(a.primary), Qt::CaseInsensitive) == 0;
        bool matchAlt1 = a.alt1 && ofdName.compare(QString::fromUtf8(a.alt1), Qt::CaseInsensitive) == 0;
        if (matchPrimary || matchAlt1) {
            // primary 是首选名，先查 primary
            QString primary = QString::fromUtf8(a.primary);
            if (families.contains(primary, Qt::CaseInsensitive)) {
                fprintf(stderr, "[Font] alias→primary: %s → %s\n", ofdName.toUtf8().constData(), primary.toUtf8().constData());
                return QFont(primary);
            }
            // 再查 alt1 / alt2
            if (a.alt1) {
                QString alt1 = QString::fromUtf8(a.alt1);
                if (families.contains(alt1, Qt::CaseInsensitive)) {
                    fprintf(stderr, "[Font] alias→alt1: %s → %s\n", ofdName.toUtf8().constData(), alt1.toUtf8().constData());
                    return QFont(alt1);
                }
            }
            if (a.alt2) {
                QString alt2 = QString::fromUtf8(a.alt2);
                if (families.contains(alt2, Qt::CaseInsensitive)) {
                    fprintf(stderr, "[Font] alias→alt2: %s → %s\n", ofdName.toUtf8().constData(), alt2.toUtf8().constData());
                    return QFont(alt2);
                }
            }
        }
    }

    // ===== Step 3: fallback（Noto Sans CJK SC 比 Noto Sans SC 字重更接近印刷体）=====
    const char* fallback = nullptr;
    if (families.contains("Noto Sans CJK SC"))  fallback = "Noto Sans CJK SC";
    else if (families.contains("Noto Sans SC")) fallback = "Noto Sans SC";
    else if (families.contains("Source Han Sans CN")) fallback = "Source Han Sans CN";

    if (fallback) {
        fprintf(stderr, "[Font] FALLBACK: %s → %s\n", ofdName.toUtf8().constData(), fallback);
        return QFont(fallback);
    }
    fprintf(stderr, "[Font] CRITICAL: no fallback font! ofdName=%s\n", ofdName.toUtf8().constData());
    return QFont();
}

} // anon namespace

/* ==================== 图片 ==================== */

void OfdRenderWidget::renderImageItems(QPainter& painter, const ofd::OfdPageElements* elems) {
    if (!elems || elems->imageCount <= 0 || !elems->images) return;

    auto** ptrs = reinterpret_cast<void**>(elems->images);
    for (int i = 0; i < elems->imageCount; ++i) {
        auto* item = reinterpret_cast<const ofd::OfdImageItem*>(ptrs[i]);
        if (!item || !item->data || item->dataSize <= 0) continue;

        QImage img = bytesToImage(item->data, item->dataSize);
        if (img.isNull()) continue;

        // Image 只取 CTM 的 translate(e,f) 定位
        // Qt A*B = 先 A 后 B → translate * pageBase 先 translate(mm) 后 pageBase(屏幕) ✓
        QTransform itemT = QTransform().translate(item->ctm[4], item->ctm[5]);
        QTransform combined = itemT * pageBaseTransform_;

        painter.save();
        painter.resetTransform();
        painter.setTransform(combined, false);
        painter.drawImage(QRectF(0, 0, item->width, item->height), img);
        painter.restore();
    }
}

/* ==================== 路径 ==================== */

void OfdRenderWidget::renderPathItems(QPainter& painter, const ofd::OfdPageElements* elems) {
    if (!elems || elems->pathCount <= 0 || !elems->paths) return;

    auto** ptrs = reinterpret_cast<void**>(elems->paths);
    int count   = elems->pathCount;

    for (int i = 0; i < count; ++i) {
        auto* item = reinterpret_cast<const ofd::OfdPathItem*>(ptrs[i]);
        if (!item || !item->data) continue;

        QPainterPath path = parsePathData(reinterpret_cast<const char*>(item->data));
        if (path.isEmpty()) continue;

        QColor strokeColor = argbToColor(item->strokeArgb);
        QColor fillColor   = argbToColor(item->fillArgb);
        double strokeW     = effectiveStrokeWidth(item->strokeWidth);

        // Qt A*B = 先 A 后 B → itemCTM * pageBase 先 itemCTM 后 pageBase
        QTransform itemCTM = ctmToTransform(item->ctm);
        QTransform combined = itemCTM * pageBaseTransform_;

        painter.save();
        painter.resetTransform();
        painter.setTransform(combined, false);

        if (fillColor.isValid() && fillColor.alpha() > 0) {
            // 优先渐变
            QBrush gradBrush = gradientBrush(item->gradient);
            if (gradBrush.style() != Qt::NoBrush) {
                // ★ gradBrush LogicalMode + 页面 mm 坐标
                // path 是 item CTM 局部坐标 → itemCTM.map → 页面 mm → base transform fillPath
                QPainterPath pageSpacePath = itemCTM.map(path);
                painter.save();
                painter.resetTransform();
                painter.setTransform(pageBaseTransform_, false);
                painter.fillPath(pageSpacePath, gradBrush);
                painter.restore();
            } else {
                painter.fillPath(path, fillColor);
            }
        }
        if (strokeColor.isValid() && strokeColor.alpha() > 0 && strokeW > 0) {
            QPen pen(strokeColor);
            pen.setWidthF(strokeW);

            // OFD 默认 LineJoin=MiterJoin, LineCap=ButtCap (FlatCap)
            // 之前硬编码 RoundJoin/RoundCap 导致视觉上粗了一圈
            switch (item->joinType) {
                case 1:  pen.setJoinStyle(Qt::RoundJoin);  break;
                case 2:  pen.setJoinStyle(Qt::BevelJoin);  break;
                default: pen.setJoinStyle(Qt::MiterJoin);  break;
            }
            switch (item->capType) {
                case 1:  pen.setCapStyle(Qt::RoundCap);    break;
                case 2:  pen.setCapStyle(Qt::SquareCap);   break;
                default: pen.setCapStyle(Qt::FlatCap);     break;
            }
            double ml = (item->miterLimit > 0) ? item->miterLimit : 10.0;
            pen.setMiterLimit(ml);

            painter.setPen(pen);
            painter.drawPath(path);
        }

        painter.restore();
    }
}

/* ==================== 辅助 ==================== */

QTransform OfdRenderWidget::ctmToTransform(const double* ctm) {
    // OFD CTM = (a b c d e f) 对应 2x3 仿射矩阵（列向量）：
    //   [ a  c  e ]   x' = a·x + c·y + e
    //   [ b  d  f ]   y' = b·x + d·y + f
    //
    // Qt QTransform 6 参数版 QTransform(m11, m12, m21, m22, dx, dy):
    //   apply(x,y) = (m11·x + m21·y + dx, m12·x + m22·y + dy)
    //
    // 匹配得：m11=a, m12=b, m21=c, m22=d, dx=e, dy=f
    return QTransform(ctm[0], ctm[1],
                      ctm[2], ctm[3],
                      ctm[4], ctm[5]);
}

QColor OfdRenderWidget::argbToColor(int32_t argb) {
    uint32_t v = static_cast<uint32_t>(argb);
    int a = (v >> 24) & 0xFF;
    int r = (v >> 16) & 0xFF;
    int g = (v >>  8) & 0xFF;
    int b = (v      ) & 0xFF;
    if (a == 0 && r == 0 && g == 0 && b == 0) return QColor();
    if (a == 0) a = 255;
    return QColor(r, g, b, a);
}

QImage OfdRenderWidget::bytesToImage(const void* data, int32_t size) {
    if (!data || size <= 0) return {};
    QByteArray ba(static_cast<const char*>(data), size);
    QBuffer buf(&ba);
    buf.open(QIODevice::ReadOnly);
    QImage img;
    img.load(&buf, nullptr);
    return img;
}

double OfdRenderWidget::effectiveStrokeWidth(double strokeWidthMM) const {
    if (strokeWidthMM <= 0) return 0;
    // strokeWidth 是 mm，painter 的全局 scale(scaleFactor_) 和元素 CTM 会自动把 mm → px
    // 这里不能再乘 scaleFactor_，否则双重缩放导致线条过粗
    return std::max(strokeWidthMM, 0.01);
}

/* ==================== Path Data 解析 ==================== */
/**
 * OFD Path Data Mini 语言 → QPainterPath。
 * 支持：M L H V C B S Q T A Z 及小写（相对）变体。
 */
QPainterPath OfdRenderWidget::parsePathData(const char* data) {
    QPainterPath path;
    if (!data || !data[0]) return path;

    const char* p = data;
    auto skipWs = [&]() { while (*p && (isspace(static_cast<unsigned char>(*p)) || *p == ',')) ++p; };

    double cx = 0, cy = 0;
    double lastCx = 0, lastCy = 0;
    bool hasLastCtrl = false;

    auto readDouble = [&](double& out) -> bool {
        skipWs();
        if (!*p) return false;
        char* end = nullptr;
        double v = strtod(p, &end);
        if (end == p) return false;
        out = v;
        p = end;
        return true;
    };

    auto doMoveTo = [&](double x, double y) { path.moveTo(x, y); cx = x; cy = y; hasLastCtrl = false; };
    auto doLineTo = [&](double x, double y) { path.lineTo(x, y); cx = x; cy = y; hasLastCtrl = false; };

    while (*p) {
        skipWs();
        if (!*p) break;
        char cmd = *p;
        bool rel = false;
        if (cmd >= 'a' && cmd <= 'z') { rel = true; cmd = static_cast<char>(cmd - 'a' + 'A'); }
        if (cmd < 'A' || cmd > 'Z') break;
        ++p;

        switch (cmd) {
        case 'M': {
            double x, y;
            if (!readDouble(x) || !readDouble(y)) break;
            if (rel) { x += cx; y += cy; }
            doMoveTo(x, y);
            while (readDouble(x) && readDouble(y)) {
                if (rel) { x += cx; y += cy; }
                doLineTo(x, y);
            }
            break;
        }
        case 'L': {
            double x, y;
            if (!readDouble(x) || !readDouble(y)) break;
            if (rel) { x += cx; y += cy; }
            doLineTo(x, y);
            break;
        }
        case 'H': { double x; if (!readDouble(x)) break; if (rel) x += cx; doLineTo(x, cy); break; }
        case 'V': { double y; if (!readDouble(y)) break; if (rel) y += cy; doLineTo(cx, y); break; }
        case 'B': {
            // OFD 规范：B 是三次贝塞尔曲线
            double x1, y1, x2, y2, x, y;
            if (!readDouble(x1) || !readDouble(y1)) break;
            if (!readDouble(x2) || !readDouble(y2)) break;
            if (!readDouble(x)  || !readDouble(y))  break;
            if (rel) { x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy; }
            path.cubicTo(x1, y1, x2, y2, x, y);
            cx = x; cy = y; lastCx = x2; lastCy = y2; hasLastCtrl = true;
            break;
        }
        case 'C': {
            // OFD 里 C 通常是 ClosePath（Close 缩写），但也兼容 SVG 风格 C 贝塞尔
            // 有 6 个参数 → 三次贝塞尔；参数不够或没参数 → closeSubpath
            double x1, y1, x2, y2, x, y;
            if (readDouble(x1) && readDouble(y1) &&
                readDouble(x2) && readDouble(y2) &&
                readDouble(x)  && readDouble(y)) {
                // 有参数 → 三次贝塞尔（SVG 风格兼容）
                if (rel) { x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy; }
                path.cubicTo(x1, y1, x2, y2, x, y);
                cx = x; cy = y; lastCx = x2; lastCy = y2; hasLastCtrl = true;
            } else {
                // 没参数 → ClosePath（OFD 非标准生成器常见用法：C = Close）
                path.closeSubpath();
                hasLastCtrl = false;
            }
            break;
        }
        case 'S': {
            double x2, y2, x, y;
            if (!readDouble(x2) || !readDouble(y2)) break;
            if (!readDouble(x)  || !readDouble(y))  break;
            if (rel) { x2 += cx; y2 += cy; x += cx; y += cy; }
            double x1, y1;
            if (hasLastCtrl) { x1 = 2 * cx - lastCx; y1 = 2 * cy - lastCy; }
            else { x1 = cx; y1 = cy; }
            path.cubicTo(x1, y1, x2, y2, x, y);
            cx = x; cy = y; lastCx = x2; lastCy = y2; hasLastCtrl = true;
            break;
        }
        case 'Q': {
            double x1, y1, x, y;
            if (!readDouble(x1) || !readDouble(y1)) break;
            if (!readDouble(x)  || !readDouble(y))  break;
            if (rel) { x1 += cx; y1 += cy; x += cx; y += cy; }
            path.quadTo(x1, y1, x, y);
            cx = x; cy = y; lastCx = x1; lastCy = y1; hasLastCtrl = true;
            break;
        }
        case 'T': {
            double x, y;
            if (!readDouble(x) || !readDouble(y)) break;
            if (rel) { x += cx; y += cy; }
            double x1, y1;
            if (hasLastCtrl) { x1 = 2 * cx - lastCx; y1 = 2 * cy - lastCy; }
            else { x1 = cx; y1 = cy; }
            path.quadTo(x1, y1, x, y);
            cx = x; cy = y; lastCx = x1; lastCy = y1; hasLastCtrl = true;
            break;
        }
        case 'A': {
            double rx, ry, rot, x, y;
            int largeArc, sweep;
            if (!readDouble(rx) || !readDouble(ry) || !readDouble(rot)) break;
            double la = 0, sw = 0;
            readDouble(la); readDouble(sw);
            largeArc = (int)la; sweep = (int)sw;
            if (!readDouble(x) || !readDouble(y)) break;
            if (rel) { x += cx; y += cy; }
            if (rx <= 0) rx = 1e-6;
            if (ry <= 0) ry = 1e-6;
            path.lineTo(x, y);   // 简化为直线，保证不会 crash
            cx = x; cy = y; hasLastCtrl = false;
            Q_UNUSED(rot); Q_UNUSED(largeArc); Q_UNUSED(sweep);
            break;
        }
        case 'Z': { path.closeSubpath(); hasLastCtrl = false; break; }
        default: break;
        }
    }
    return path;
}
