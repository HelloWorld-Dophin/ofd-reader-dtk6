/*
 * main_window.h — OFD 编辑器主窗口（纯 Qt6 QMainWindow，无 DTK6）
 */
#pragma once

#include "ofd_render_widget.h"
#include "property_panel.h"

#include <QMainWindow>
#include <QToolButton>
#include <QSlider>
#include <QSpinBox>
#include <QComboBox>
#include <QTabWidget>
#include <QPushButton>
#include <QGroupBox>
#include <QMenuBar>
#include <QMenu>
#include <QFileDialog>

#include <QMenu>
#include <QAction>
#include <QButtonGroup>
#include <QDockWidget>
#include <QLabel>
#include <QString>
#include <memory>
#include <functional>
#include <QList>

class MainWindow : public QMainWindow {
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
    void onAbout();
    void onThemeLight();
    void onThemeDark();
    void onThemeFollow();
    void applyThemePalette(const QString& mode);

private:
    void buildMenuBarToolBar();
    void buildHamburgerMenu();  // 汉堡菜单：主题 / 关于 / 退出
    QWidget* buildTabHome();
    QWidget* buildTabInsert();
    QWidget* buildTabFont();
    void buildPropertyPanelDock();
    void buildStatusBar();
    void connectSignals();

    template <typename F>
    QPushButton* makeBtn(const QString& text, F&& slot);
    template <typename F>
    QPushButton* makeBtn(const QIcon& icon, const QString& text, F&& slot);
    QWidget* makeRow(const QList<QWidget*>& widgets);
    QGroupBox* makeGroup(const QString& title);
    QGroupBox* makeGroup(const QString& title, const QList<QPushButton*>& buttons);

    void showError(const QString& title, const QString& msg);
    void updatePageLabel();
    void updateZoomLabel();

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    /* ============ 成员 ============ */
    std::unique_ptr<ofd::OfdJnaBridge> bridge_;
    std::unique_ptr<ofd::OfdJnaBridge::Doc> doc_;
    QString                               filePath_;

    OfdRenderWidget*  render_ = nullptr;
    PropertyPanel*    propPanel_ = nullptr;
    QTabWidget*       ribbonTabs_ = nullptr;
    QToolButton*      tabBtnHome_   = nullptr;
    QToolButton*      tabBtnInsert_ = nullptr;
    QToolButton*      tabBtnFont_   = nullptr;

    QSlider*      zoomSlider_ = nullptr;
    QSlider*      pageSlider_  = nullptr;
    QToolButton*  prevBtn_   = nullptr;
    QToolButton*  nextBtn_   = nullptr;
    QLabel*       pageLabelPrefix_ = nullptr;   // "页面:"
    QSpinBox*     pageSpin_        = nullptr;   // 可输入页码跳转
    QLabel*       pageLabelSuffix_ = nullptr;   // "/ 总数"
    QLabel*       zoomLabel_   = nullptr;

    // 字体设置 Tab 的四个下拉框
    QComboBox* fontKaiComboBox_   = nullptr;   // 楷体
    QComboBox* fontHeiComboBox_   = nullptr;   // 黑体
    QComboBox* fontSongComboBox_  = nullptr;   // 宋体
    QComboBox* fontFangComboBox_  = nullptr;   // 仿宋

    QToolBar*   menuBar_        = nullptr;
    QWidget*    leftContainer_  = nullptr;
    QWidget*    tabContainer_   = nullptr;

    // 图标主题色重设器列表
    QList<std::function<void(const QColor&)>> iconSetters_;
};
