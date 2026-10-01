/*
 * property_panel.h — 纯 Qt6 属性面板
 */
#pragma once

#include "ofd_render_widget.h"

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QComboBox>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

class PropertyPanel : public QFrame {
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

    QLabel*          typeLabel_    = nullptr;
    QLineEdit*       contentEdit_  = nullptr;
    QDoubleSpinBox*  fontSizeSpin_ = nullptr;
    QComboBox*       colorComboBox_ = nullptr;

    QGroupBox* textGroup_ = nullptr;
};
