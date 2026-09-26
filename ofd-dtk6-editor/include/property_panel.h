/*
 * property_panel.h — DTK6 属性面板
 */
#pragma once

#include "ofd_render_widget.h"

#include <DFrame>
#include <DLabel>
#include <DLineEdit>
#include <DDoubleSpinBox>
#include <DComboBox>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>

class PropertyPanel : public Dtk::Widget::DFrame {
    Q_OBJECT

public:
    explicit PropertyPanel(QWidget* parent = nullptr);
    ~PropertyPanel() override = default;

    void setRenderWidget(OfdRenderWidget* widget);

private slots:
    void onSelectionChanged(const OfdRenderWidget::SelectedItem& selected);
    void onContentEdited();
    void onFontSizeChanged(double val);

private:
    void buildUI();
    void loadFromSelection();

    OfdRenderWidget* renderWidget_ = nullptr;
    OfdRenderWidget::SelectedItem current_;

    QLabel*                     typeLabel_    = nullptr;
    Dtk::Widget::DLineEdit*     contentEdit_  = nullptr;
    Dtk::Widget::DDoubleSpinBox* fontSizeSpin_ = nullptr;
    Dtk::Widget::DComboBox*     colorComboBox_ = nullptr;

    QGroupBox* textGroup_ = nullptr;
};
