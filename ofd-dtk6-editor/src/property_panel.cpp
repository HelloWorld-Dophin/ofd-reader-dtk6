/*
 * property_panel.cpp — DTK6 属性面板实现
 */

#include "property_panel.h"
#include "ofd_jna_bridge.h"

#include <QGroupBox>
#include <QLabel>
#include <QFormLayout>
#include <QScrollArea>
#include <QMessageBox>
#include <QColor>

PropertyPanel::PropertyPanel(QWidget* parent)
    : Dtk::Widget::DFrame(parent) {
    buildUI();
}

void PropertyPanel::buildUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // 标题 / 当前选中类型
    typeLabel_ = new QLabel(QStringLiteral("未选中元素"), this);
    typeLabel_->setStyleSheet("font-weight: bold; font-size: 13px;");
    root->addWidget(typeLabel_);

    // 文本元素编辑组
    textGroup_ = new QGroupBox(QStringLiteral("文本属性"), this);
    auto* form = new QFormLayout(textGroup_);
    form->setLabelAlignment(Qt::AlignRight);

    contentEdit_ = new Dtk::Widget::DLineEdit(textGroup_);
    contentEdit_->setPlaceholderText(QStringLiteral("文本内容"));
    connect(contentEdit_, &Dtk::Widget::DLineEdit::editingFinished,
            this, &PropertyPanel::onContentEdited);
    form->addRow(QStringLiteral("内容:"), contentEdit_);

    fontSizeSpin_ = new Dtk::Widget::DDoubleSpinBox(textGroup_);
    fontSizeSpin_->setRange(0.5, 200.0);
    fontSizeSpin_->setDecimals(2);
    fontSizeSpin_->setSingleStep(0.5);
    fontSizeSpin_->setSuffix(QStringLiteral(" pt"));
    connect(fontSizeSpin_, QOverload<double>::of(&Dtk::Widget::DDoubleSpinBox::valueChanged),
            this, &PropertyPanel::onFontSizeChanged);
    form->addRow(QStringLiteral("字号:"), fontSizeSpin_);

    colorComboBox_ = new Dtk::Widget::DComboBox(textGroup_);
    colorComboBox_->addItem(QStringLiteral("自动 (保持原始)"), QColor());
    colorComboBox_->addItem(QStringLiteral("黑色"), QColor(0, 0, 0));
    colorComboBox_->addItem(QStringLiteral("红色"), QColor(204, 0, 0));
    colorComboBox_->addItem(QStringLiteral("蓝色"), QColor(0, 0, 153));
    colorComboBox_->addItem(QStringLiteral("灰色"), QColor(128, 128, 128));
    form->addRow(QStringLiteral("颜色:"), colorComboBox_);

    root->addWidget(textGroup_);
    textGroup_->setVisible(false);   // 初始隐藏

    root->addStretch();
}

void PropertyPanel::setRenderWidget(OfdRenderWidget* widget) {
    if (renderWidget_) {
        disconnect(renderWidget_, &OfdRenderWidget::selectionChanged,
                   this, &PropertyPanel::onSelectionChanged);
    }
    renderWidget_ = widget;
    if (renderWidget_) {
        connect(renderWidget_, &OfdRenderWidget::selectionChanged,
                this, &PropertyPanel::onSelectionChanged);
        onSelectionChanged(renderWidget_->selectedItem());
    }
}

void PropertyPanel::onSelectionChanged(const OfdRenderWidget::SelectedItem& selected) {
    current_ = selected;
    loadFromSelection();
}

void PropertyPanel::loadFromSelection() {
    if (current_.type == OfdRenderWidget::ElementType::None || !renderWidget_ || !renderWidget_->document()) {
        typeLabel_->setText(QStringLiteral("未选中元素"));
        textGroup_->setVisible(false);
        contentEdit_->clear();
        return;
    }

    // 取当前 page 的 elements
    auto page = renderWidget_->document()->readPage(renderWidget_->pageIndex());
    if (!page.valid()) return;
    const auto* elems = page.get();

    switch (current_.type) {
    case OfdRenderWidget::ElementType::Text: {
        typeLabel_->setText(QStringLiteral("文本元素"));
        textGroup_->setVisible(true);

        if (current_.index >= 0 && current_.index < elems->textCount && elems->texts) {
            auto** ptrs = reinterpret_cast<void**>(elems->texts);
            auto* item = reinterpret_cast<const ofd::OfdTextItem*>(ptrs[current_.index]);
            if (item && item->content) {
                contentEdit_->blockSignals(true);
                contentEdit_->setText(QString::fromUtf8(
                    reinterpret_cast<const char*>(item->content)));
                contentEdit_->blockSignals(false);

                fontSizeSpin_->blockSignals(true);
                fontSizeSpin_->setValue(item->fontSize);
                fontSizeSpin_->blockSignals(false);
            }
        }
        break;
    }
    case OfdRenderWidget::ElementType::Image:
        typeLabel_->setText(QStringLiteral("图片元素 (只读)"));
        textGroup_->setVisible(false);
        break;
    case OfdRenderWidget::ElementType::Path:
        typeLabel_->setText(QStringLiteral("矢量路径 (只读)"));
        textGroup_->setVisible(false);
        break;
    default:
        typeLabel_->setText(QStringLiteral("未选中元素"));
        textGroup_->setVisible(false);
        break;
    }
}

void PropertyPanel::onContentEdited() {
    if (current_.type != OfdRenderWidget::ElementType::Text || !renderWidget_ || !renderWidget_->document()) return;
    if (current_.index < 0) return;

    try {
        QColor color = colorComboBox_->currentData().value<QColor>();
        int32_t argb = color.isValid() ? (color.alpha() << 24) | (color.red() << 16) | (color.green() << 8) | color.blue() : 0;

        renderWidget_->document()->modifyText(
            renderWidget_->pageIndex(),
            current_.index,
            contentEdit_->text().toStdString(),
            fontSizeSpin_->value(),
            argb);

        renderWidget_->update();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, QStringLiteral("编辑失败"),
                             QString::fromStdString(e.what()));
    }
}

void PropertyPanel::onFontSizeChanged(double val) {
    if (current_.type != OfdRenderWidget::ElementType::Text) return;
    // 字号变化时也触发修改（颜色保持当前值）
    onContentEdited();
}
