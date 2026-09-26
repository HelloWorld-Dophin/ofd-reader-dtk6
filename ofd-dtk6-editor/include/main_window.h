/*
 * main_window.h — OFD 编辑器主窗口（DTK6 DMainWindow）
 *
 * 全部 UI 组件使用 DTK6 控件，禁止原生 Qt 控件。
 */
#pragma once

#include "ofd_render_widget.h"
#include "property_panel.h"

#include <DMainWindow>
#include <DGuiApplicationHelper>
#include <DMenuBar>
#include <DMenu>
#include <DToolButton>
#include <DSlider>
#include <DFileDialog>
#include <DGroupBox>
#include <DTabWidget>
#include <DPushButton>

#include <QMenu>
#include <QAction>
#include <QButtonGroup>
#include <QDockWidget>
#include <QLabel>
#include <QString>
#include <memory>
#include <functional>
#include <QList>

class MainWindow : public Dtk::Widget::DMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

    // 命令行直接打开指定文件
    void openFileByPath(const QString& path);

private slots:
    void onOpen();
    void onSaveAs();
    void onAddText();
    void onAddRect();
    void onDeleteSelected();
    void onAddBlankPage();
    void onPagePrev();
    void onPageNext();
    void onZoomFit();

private:
    void buildTitlebar();
    void buildMenuBarToolBar();
    QWidget* buildTabHome();
    QWidget* buildTabInsert();
    QWidget* buildTabFont();
    void buildPropertyPanelDock();
    void buildStatusBar();
    void connectSignals();

    template <typename F>
    Dtk::Widget::DPushButton* makeBtn(const QString& text, F&& slot);
    template <typename F>
    Dtk::Widget::DPushButton* makeBtn(const QIcon& icon, const QString& text, F&& slot);
    QWidget* makeRow(const QList<QWidget*>& widgets);
    Dtk::Widget::DGroupBox* makeGroup(const QString& title);
    Dtk::Widget::DGroupBox* makeGroup(const QString& title, const QList<Dtk::Widget::DPushButton*>& buttons);

    void showError(const QString& title, const QString& msg);
    void updatePageLabel();
    void updateZoomLabel();

protected:
    void resizeEvent(QResizeEvent* event) override;

    /* ============ 成员 ============ */
    std::unique_ptr<ofd::OfdJnaBridge> bridge_;
    std::unique_ptr<ofd::OfdJnaBridge::Doc> doc_;
    QString                               filePath_;

    OfdRenderWidget*  render_ = nullptr;
    PropertyPanel*    propPanel_ = nullptr;
    Dtk::Widget::DTabWidget* ribbonTabs_ = nullptr;
    Dtk::Widget::DToolButton* tabBtnHome_   = nullptr;
    Dtk::Widget::DToolButton* tabBtnInsert_ = nullptr;
    Dtk::Widget::DToolButton* tabBtnFont_   = nullptr;

    Dtk::Widget::DSlider*   zoomSlider_ = nullptr;
    Dtk::Widget::DSlider*   pageSlider_  = nullptr;
    QLabel*                 pageLabel_   = nullptr;
    QLabel*                 zoomLabel_   = nullptr;

    // 字体设置 Tab 的四个下拉框
    Dtk::Widget::DComboBox* fontKaiComboBox_   = nullptr;   // 楷体
    Dtk::Widget::DComboBox* fontHeiComboBox_   = nullptr;   // 黑体
    Dtk::Widget::DComboBox* fontSongComboBox_  = nullptr;   // 宋体
    Dtk::Widget::DComboBox* fontFangComboBox_  = nullptr;   // 仿宋

    QToolBar*   menuBar_        = nullptr;
    QWidget*    leftContainer_  = nullptr;
    QWidget*    tabContainer_   = nullptr;

    // 图标主题色重设器列表：每个 setter 接收目标 QColor，内部把对应 icon 重绘染色
    QList<std::function<void(const QColor&)>> iconSetters_;
};
