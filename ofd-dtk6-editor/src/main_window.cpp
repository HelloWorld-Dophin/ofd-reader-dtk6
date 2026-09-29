/*
 * main_window.cpp — DTK6 主窗口实现
 */

#include "main_window.h"

#include <DApplication>
#include <DGuiApplicationHelper>
#include <DTitlebar>
#include <DStatusBar>
#include <DFileDialog>
#include <DToolButton>
#include <DSlider>
#include <DStyle>
#include <DSpinBox>

#include <QDockWidget>
#include <QLabel>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QToolBar>
#include <QResizeEvent>
#include <QIcon>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <QFontDatabase>
#include <QAbstractItemView>
#include <QDir>
#include <QGridLayout>
#include <QStandardItemModel>
#include <algorithm>

MainWindow::MainWindow(QWidget* parent)
    : Dtk::Widget::DMainWindow(parent) {
    bridge_ = std::make_unique<ofd::OfdJnaBridge>();

    render_ = new OfdRenderWidget(this);
    setCentralWidget(render_);

    buildTitlebar();
    // 先创建 DTabWidget（tab 按钮 connect 需要它）
    ribbonTabs_ = new Dtk::Widget::DTabWidget();
    ribbonTabs_->tabBar()->hide();
    ribbonTabs_->setDocumentMode(true);
    ribbonTabs_->addTab(buildTabHome(),   QString());
    // ribbonTabs_->addTab(buildTabInsert(), QString());  // 阅读器阶段：隐藏插入 Tab
    ribbonTabs_->addTab(buildTabFont(),   QString());

    // 第二行：文件菜单 + Tab 切换
    buildMenuBarToolBar();

    // 强制换行 — 在已存在的 toolbar 之后、新 toolbar 之前调用
    addToolBarBreak();

    // 第三行：Ribbon 卡片内容
    auto* ribbonBar = addToolBar(QStringLiteral("Ribbon"));
    ribbonBar->setMovable(false);
    ribbonBar->setFloatable(false);
    ribbonBar->setFixedHeight(64);
    // 不要 setStyleSheet("QToolBar {...}") — 会让 DTK 主题失去工具栏控制权
    ribbonBar->addWidget(ribbonTabs_);

    // buildPropertyPanelDock();  // 阅读器阶段：不需要右侧属性面板
    buildStatusBar();
    connectSignals();

    setWindowTitle(QStringLiteral("OFD 阅读器 / 编辑器"));
    resize(1200, 800);
}

/* ==================== Title Bar（纯标题 + 金刚键） ==================== */

void MainWindow::buildTitlebar() {
    auto* tb = titlebar();
    if (!tb) return;
    tb->setMenuDisabled(false);          // 恢复汉堡菜单按钮可用
    tb->setSwitchThemeMenuVisible(true);  // DTK 自带：汉堡菜单里出现主题切换子菜单
    tb->setIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    tb->setTitle(windowTitle());
    // DTitlebar 自动显示 windowTitle + 金刚键 + 汉堡菜单 + 主题切换
}

/* ==================== Menu Bar ToolBar（第二行：左侧区 + Tab精确屏幕中心） ==================== */

void MainWindow::buildMenuBarToolBar() {
    menuBar_ = addToolBar(QStringLiteral("MenuBar"));
    menuBar_->setMovable(false);
    menuBar_->setFixedHeight(38);
    // 不要 setStyleSheet — 让 DTK 主题控制工具栏外观

    // === 整个 toolbar 放进一个容器，内部手动布局精确控制 ===
    auto* root = new QWidget();
    auto* rootLay = new QHBoxLayout(root);
    rootLay->setContentsMargins(8, 0, 8, 0);
    rootLay->setSpacing(0);

    // === 左侧容器（文件+保存+另存为）===
    leftContainer_ = new QWidget();
    auto* leftLay = new QHBoxLayout(leftContainer_);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(4);

    auto* fileBtn = new Dtk::Widget::DToolButton();
    fileBtn->setText(QStringLiteral("文件"));
    auto* fileMenu = new Dtk::Widget::DMenu(fileBtn);
    auto* actOpen = fileMenu->addAction(QStringLiteral("打开 OFD..."));
    actOpen->setShortcut(QKeySequence::Open);
    QObject::connect(actOpen, &QAction::triggered, this, &MainWindow::onOpen);
    auto* actSave = fileMenu->addAction(QStringLiteral("另存为..."));
    actSave->setShortcut(QKeySequence::SaveAs);
    QObject::connect(actSave, &QAction::triggered, this, &MainWindow::onSaveAs);
    fileMenu->addSeparator();
    auto* actQuit = fileMenu->addAction(QStringLiteral("退出"));
    actQuit->setShortcut(QKeySequence::Quit);
    QObject::connect(actQuit, &QAction::triggered, this, &QWidget::close);
    fileBtn->setMenu(fileMenu);
    fileBtn->setPopupMode(QToolButton::InstantPopup);
    leftLay->addWidget(fileBtn);

    auto* saveBtn = new Dtk::Widget::DToolButton();
    saveBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-save"),
                       QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton)));
    saveBtn->setToolTip(QStringLiteral("保存"));
    QObject::connect(saveBtn, &Dtk::Widget::DToolButton::clicked, this, &MainWindow::onSaveAs);
    leftLay->addWidget(saveBtn);

    auto* saveAsBtn = new Dtk::Widget::DToolButton();
    saveAsBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-save-as"),
                        QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton)));
    saveAsBtn->setToolTip(QStringLiteral("另存为"));
    QObject::connect(saveAsBtn, &Dtk::Widget::DToolButton::clicked, this, &MainWindow::onSaveAs);
    leftLay->addWidget(saveAsBtn);

    rootLay->addWidget(leftContainer_);

    // === 弹性 spacer（动态宽度让 Tab 组精确居中）===
    auto* spacer = new QWidget();
    spacer->setObjectName(QStringLiteral("__tabSpacer__"));
    spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    rootLay->addWidget(spacer);

    // === Tab 容器 ===
    tabContainer_ = new QWidget();
    auto* tabLay = new QHBoxLayout(tabContainer_);
    tabLay->setContentsMargins(0, 0, 0, 0);
    tabLay->setSpacing(4);

    tabBtnHome_   = new Dtk::Widget::DToolButton();
    tabBtnHome_->setText(QStringLiteral("开始"));
    tabBtnHome_->setCheckable(true);
    tabBtnHome_->setChecked(true);

    // tabBtnInsert_ = new Dtk::Widget::DToolButton();  // 阅读器阶段：隐藏
    // tabBtnInsert_->setText(QStringLiteral("插入"));
    // tabBtnInsert_->setCheckable(true);

    tabBtnFont_   = new Dtk::Widget::DToolButton();
    tabBtnFont_->setText(QStringLiteral("字体设置"));
    tabBtnFont_->setCheckable(true);

    auto* grp = new QButtonGroup(this);
    grp->setExclusive(true);
    grp->addButton(tabBtnHome_, 0);
    // grp->addButton(tabBtnInsert_, 1);  // 阅读器阶段
    grp->addButton(tabBtnFont_, 1);
    QObject::connect(grp, &QButtonGroup::idClicked,
                     ribbonTabs_, &Dtk::Widget::DTabWidget::setCurrentIndex);
    QObject::connect(ribbonTabs_, &Dtk::Widget::DTabWidget::currentChanged, this,
        [this](int idx) {
            Dtk::Widget::DToolButton* btns[] = { tabBtnHome_, /*tabBtnInsert_,*/ tabBtnFont_ };
            if (idx >= 0 && idx < 2) btns[idx]->setChecked(true);
        });

    tabLay->addWidget(tabBtnHome_);
    // tabLay->addWidget(tabBtnInsert_);  // 阅读器阶段
    tabLay->addWidget(tabBtnFont_);

    rootLay->addWidget(tabContainer_);
    rootLay->addStretch(1);   // 右侧弹性空间（主题切换已移至 DTitlebar 汉堡菜单）

    menuBar_->addWidget(root);
}

/* ==================== resizeEvent：让 Tab 组精确屏幕中心 ==================== */

void MainWindow::resizeEvent(QResizeEvent* event) {
    Dtk::Widget::DMainWindow::resizeEvent(event);

    if (!menuBar_ || !leftContainer_ || !tabContainer_) return;

    auto* spacer = menuBar_->findChild<QWidget*>(QStringLiteral("__tabSpacer__"));
    if (!spacer) return;

    // 居中公式：spacer = (toolbar宽度 - tab宽度)/2 - 左侧宽度
    // 这样 tabContainer_ 的左边缘 = leftWidth + spacer = (toolbarWidth - tabWidth)/2
    // → tabContainer_ 的中心 = toolbarWidth/2 = 窗口水平中心
    int barW   = menuBar_->width();
    int tabW   = tabContainer_->sizeHint().width();
    int leftW  = leftContainer_->sizeHint().width();
    int target = (barW - tabW) / 2 - leftW;

    spacer->setFixedWidth(qMax(0, target));
}

/* ==================== Ribbon 已内联到构造函数 ==================== */

/* ---------- 图标染色 + 主题辅助 ---------- */

// 图标前景色：浅色主题下黑色，深色主题下白色
static QColor iconFgColor(Dtk::Gui::DGuiApplicationHelper::ColorType type) {
    return (type == Dtk::Gui::DGuiApplicationHelper::DarkType) ? QColor(255, 255, 255)
                                                                : QColor(  0,   0,   0);
}

// 把任意 QIcon 染成目标前景色。用 SourceAtop 模式把形状保留、颜色替换。
static QIcon tintIcon(const QIcon& src, const QColor& fg) {
    if (src.isNull() || !fg.isValid()) return src;
    QIcon result;
    for (int sz : {16, 24, 32, 48}) {
        QPixmap pm = src.pixmap(sz, sz);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        p.fillRect(pm.rect(), fg);
        p.end();
        result.addPixmap(pm);
    }
    return result;
}

// 手绘圆圈 + / - 图标，接受前景色
static QIcon makeRoundIcon(bool plus, const QColor& fg) {
    QPixmap pm(16, 16); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(fg, 1.4));
    p.setBrush(Qt::transparent);
    p.drawEllipse(1, 1, 14, 14);
    p.setPen(QPen(fg, 1.5));
    if (plus) {
        p.drawLine(8, 4, 8, 12);
        p.drawLine(4, 8, 12, 8);
    } else {
        p.drawLine(4, 8, 12, 8);
    }
    return QIcon(pm);
}

/* ---------- 开始 Tab ---------- */

QWidget* MainWindow::buildTabHome() {
    auto* page = new QWidget();
    auto* row = new QHBoxLayout(page);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(6);
    row->addStretch();  // 左侧 stretch → 所有控件组整体居中

    // ---- 1. 缩放：左侧滑块两行 + 右侧比例下拉+放大缩小 ----
    auto* zoomWrap = new QWidget();
    auto* zoomLay = new QHBoxLayout(zoomWrap);
    zoomLay->setContentsMargins(0,0,0,0);
    zoomLay->setSpacing(6);

    // 左侧：两行 VBox —— 行1:滑块，行2:百分比标签，等宽对齐
    auto* leftCol = new QWidget();
    auto* leftLay = new QVBoxLayout(leftCol);
    leftLay->setContentsMargins(0,0,0,0);
    leftLay->setSpacing(0);

    const int kSliderW = 140;
    zoomSlider_ = new Dtk::Widget::DSlider(Qt::Horizontal);
    zoomSlider_->setMinimum(1); zoomSlider_->setMaximum(1000); zoomSlider_->setValue(100);
    zoomSlider_->setFixedWidth(kSliderW);
    zoomSlider_->setFixedHeight(22);
    zoomSlider_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QObject::connect(zoomSlider_, &Dtk::Widget::DSlider::valueChanged, this,
        [this](int v) { render_->setScaleFactor(v / 100.0); });
    QObject::connect(render_, &OfdRenderWidget::scaleFactorChanged, this,
        [this](double /*factor*/) { updateZoomLabel(); });

    zoomLabel_ = new QLabel(QStringLiteral("100%"));
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomLabel_->setFixedSize(kSliderW, 14);
    // 不要硬编码 color / background，让 DTK 主题自动上色

    leftLay->addWidget(zoomSlider_);
    leftLay->addWidget(zoomLabel_);
    zoomLay->addWidget(leftCol);

    // 右侧：两行 VBox —— 行1:比例下拉，行2:放大缩小图标按钮
    auto* rightCol = new QWidget();
    auto* rightLay = new QVBoxLayout(rightCol);
    rightLay->setContentsMargins(0,0,0,0);
    rightLay->setSpacing(0);

    auto* combo = new QComboBox();
    combo->setFixedWidth(80);
    combo->setFixedHeight(22);
    // 只缩字号，别动颜色/边框/背景 — 让 DTK 主题自动处理
    combo->setStyleSheet(
        "QComboBox { font-size: 10px; }"
        "QComboBox QAbstractItemView { font-size: 10px; }"
    );
    combo->addItems({QStringLiteral("25%"), QStringLiteral("50%"),
                     QStringLiteral("75%"), QStringLiteral("100%"),
                     QStringLiteral("150%"), QStringLiteral("200%"),
                     QStringLiteral("300%")});
    combo->setCurrentIndex(3);  // 100%
    // combo → render：用 currentIndexChanged（用户选择预设值）
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, combo](int) {
            QString t = combo->currentText();
            bool ok = false;
            int pct = t.remove('%').toInt(&ok);
            if (ok) render_->setScaleFactor(pct / 100.0);
        });
    // render → combo：重建列表，只保留预设项+当前动态值，防止无限膨胀
    QObject::connect(render_, &OfdRenderWidget::scaleFactorChanged, combo,
        [combo](double f) {
            static const QStringList kPreset = {
                QStringLiteral("25%"), QStringLiteral("50%"),
                QStringLiteral("75%"), QStringLiteral("100%"),
                QStringLiteral("150%"), QStringLiteral("200%"),
                QStringLiteral("300%")
            };
            int pct = qRound(f * 100);
            QString t = QStringLiteral("%1%").arg(pct);
            if (combo->currentText() == t) return;
            combo->blockSignals(true);
            combo->clear();
            combo->addItems(kPreset);
            if (!kPreset.contains(t)) combo->addItem(t);
            int idx = combo->findText(t);
            combo->setCurrentIndex(idx);
            combo->blockSignals(false);
        });

    // 行2: 放大/缩小图标按钮 —— 圆圈里 + / -
    auto fgNow = iconFgColor(Dtk::Gui::DGuiApplicationHelper::instance()->paletteType());

    auto* btnRow = new QWidget();
    auto* btnLay = new QHBoxLayout(btnRow);
    btnLay->setContentsMargins(0,0,0,0);
    btnLay->setSpacing(2);
    auto* zoomOutBtn = new Dtk::Widget::DPushButton();
    zoomOutBtn->setIcon(makeRoundIcon(false, fgNow));
    zoomOutBtn->setFixedSize(24, 18);
    zoomOutBtn->setIconSize(QSize(12, 12));
    iconSetters_.append([zoomOutBtn](const QColor& c) { zoomOutBtn->setIcon(makeRoundIcon(false, c)); });
    QObject::connect(zoomOutBtn, &Dtk::Widget::DPushButton::clicked, this,
        [this]{ render_->setScaleFactor(render_->scaleFactor() / 1.2); });
    auto* zoomInBtn = new Dtk::Widget::DPushButton();
    zoomInBtn->setIcon(makeRoundIcon(true, fgNow));
    zoomInBtn->setFixedSize(24, 18);
    zoomInBtn->setIconSize(QSize(12, 12));
    iconSetters_.append([zoomInBtn](const QColor& c) { zoomInBtn->setIcon(makeRoundIcon(true, c)); });
    btnLay->addWidget(zoomOutBtn);
    btnLay->addWidget(zoomInBtn);

    rightLay->addWidget(combo);
    rightLay->addWidget(btnRow);
    zoomLay->addWidget(rightCol);

    row->addWidget(zoomWrap);

    // 竖向分隔线 — QFrame::VLine 用 MidLine 颜色角色，DTK 主题自动管深浅色
    auto makeSep = []() -> QFrame* {
        auto* sep = new QFrame();
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Plain);
        sep->setFixedWidth(1);
        return sep;
    };
    row->addWidget(makeSep());

    // ---- 2. 田字 2×2 适配（带图标，随主题染色）----
    auto origFitPage = QIcon::fromTheme(QStringLiteral("zoom-fit-best"));
    if (origFitPage.isNull()) origFitPage = QApplication::style()->standardIcon(QStyle::SP_DialogResetButton);
    auto origFitReal = QIcon::fromTheme(QStringLiteral("zoom-original"));
    if (origFitReal.isNull()) origFitReal = QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    auto origFitW = QIcon::fromTheme(QStringLiteral("zoom-fit-width"));
    if (origFitW.isNull()) origFitW = QApplication::style()->standardIcon(QStyle::SP_DialogResetButton);
    auto origFitH = QIcon::fromTheme(QStringLiteral("zoom-fit-height"));
    if (origFitH.isNull()) origFitH = QApplication::style()->standardIcon(QStyle::SP_DialogResetButton);

    QIcon icFitPage = tintIcon(origFitPage, fgNow);
    QIcon icFitReal = tintIcon(origFitReal, fgNow);
    QIcon icFitW    = tintIcon(origFitW,    fgNow);
    QIcon icFitH    = tintIcon(origFitH,    fgNow);

    auto* fitWrap = new QWidget();
    fitWrap->setFixedHeight(50);
    auto* fitGrid = new QGridLayout(fitWrap);
    fitGrid->setContentsMargins(0,0,0,0);
    fitGrid->setHorizontalSpacing(3);
    fitGrid->setVerticalSpacing(3);
    auto* b1 = makeBtn(icFitPage, QStringLiteral("适合页面"),
                       [this]{ onZoomFit(); });
    b1->setFixedSize(80, 22);
    b1->setStyleSheet("font-size: 10px;");
    auto* b2 = makeBtn(icFitReal, QStringLiteral("实际大小"),
                       [this]{ zoomSlider_->setValue(100); });
    b2->setFixedSize(80, 22);
    b2->setStyleSheet("font-size: 10px;");
    auto* b3 = makeBtn(icFitW,    QStringLiteral("适合宽度"),
                       [this]{ render_->fitToWidth(); updateZoomLabel(); });
    b3->setFixedSize(80, 22);
    b3->setStyleSheet("font-size: 10px;");
    auto* b4 = makeBtn(icFitH,    QStringLiteral("适合高度"),
                       [this]{ render_->fitToHeight(); updateZoomLabel(); });
    b4->setFixedSize(80, 22);
    b4->setStyleSheet("font-size: 10px;");
    iconSetters_.append([b1, origFitPage](const QColor& c) { b1->setIcon(tintIcon(origFitPage, c)); });
    iconSetters_.append([b2, origFitReal](const QColor& c) { b2->setIcon(tintIcon(origFitReal, c)); });
    iconSetters_.append([b3, origFitW](const QColor& c)    { b3->setIcon(tintIcon(origFitW, c)); });
    iconSetters_.append([b4, origFitH](const QColor& c)    { b4->setIcon(tintIcon(origFitH, c)); });
    fitGrid->addWidget(b1, 0, 0);
    fitGrid->addWidget(b2, 0, 1);
    fitGrid->addWidget(b3, 1, 0);
    fitGrid->addWidget(b4, 1, 1);
    row->addWidget(fitWrap);

    // 竖向分隔线
    row->addWidget(makeSep());

    // ---- 3. 旋转（并排横排，图标上文字下，随主题染色） ----
    auto origRotCCW = QIcon::fromTheme(QStringLiteral("object-rotate-left"));
    if (origRotCCW.isNull()) origRotCCW = QApplication::style()->standardIcon(QStyle::SP_ArrowLeft);
    auto origRotCW = QIcon::fromTheme(QStringLiteral("object-rotate-right"));
    if (origRotCW.isNull()) origRotCW = QApplication::style()->standardIcon(QStyle::SP_ArrowRight);

    auto* rotWrap = new QWidget();
    auto* rotLay = new QHBoxLayout(rotWrap);
    rotLay->setContentsMargins(0,0,0,0);
    rotLay->setSpacing(4);

    auto makeRotBtn = [&](const QIcon& origIcon, const QString& txt, auto slot) {
        auto* b = new Dtk::Widget::DPushButton();
        b->setFixedHeight(42);
        b->setMinimumWidth(56);
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        b->setText(QString());
        b->setIcon(QIcon());

        auto* lay = new QVBoxLayout(b);
        lay->setContentsMargins(4, 2, 4, 2);
        lay->setSpacing(1);

        auto* icLabel = new QLabel();
        icLabel->setAlignment(Qt::AlignCenter);
        icLabel->setPixmap(tintIcon(origIcon, fgNow).pixmap(16, 16));
        lay->addWidget(icLabel);

        auto* txLabel = new QLabel(txt);
        txLabel->setAlignment(Qt::AlignCenter);
        // 不要 setStyleSheet font-size — 让 DTK 主题自动给 QLabel 上颜色/字体
        lay->addWidget(txLabel);

        // 按下时：用 DTK palette 的高亮色（highlight color），不要硬编码 #0066ff
        auto setHighlight = [icLabel, txLabel](bool on) {
            QString s;
            if (on) {
                QColor hl = qApp->palette().color(QPalette::Highlight);
                s = QStringLiteral("QLabel { color: %1; }").arg(hl.name());
            }
            icLabel->setStyleSheet(s);
            txLabel->setStyleSheet(s);
        };
        QObject::connect(b, &Dtk::Widget::DPushButton::pressed,  b, [setHighlight]{ setHighlight(true); });
        QObject::connect(b, &Dtk::Widget::DPushButton::released, b, [setHighlight]{ setHighlight(false); });
        QObject::connect(b, &Dtk::Widget::DPushButton::clicked, this, slot);

        iconSetters_.append([icLabel, origIcon](const QColor& c) {
            icLabel->setPixmap(tintIcon(origIcon, c).pixmap(16, 16));
        });
        rotLay->addWidget(b);
    };
    makeRotBtn(origRotCCW, QStringLiteral("逆时针"), [this]{ render_->rotate(-90); });
    makeRotBtn(origRotCW,  QStringLiteral("顺时针"), [this]{ render_->rotate(+90); });
    row->addWidget(rotWrap);

    // 竖向分隔线
    row->addWidget(makeSep());

    // ---- 4. 画布背景切换（带下拉菜单，随主题染色）----
    auto origBgIcon = QIcon::fromTheme(QStringLiteral("draw-freehand"));
    if (origBgIcon.isNull()) origBgIcon = QApplication::style()->standardIcon(QStyle::SP_DialogResetButton);

    auto* bgBtn = new Dtk::Widget::DPushButton();
    bgBtn->setFixedHeight(42);
    bgBtn->setMinimumWidth(64);
    bgBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    bgBtn->setText(QString());
    bgBtn->setIcon(QIcon());

    auto* bgLay = new QVBoxLayout(bgBtn);
    bgLay->setContentsMargins(4, 2, 4, 2);
    bgLay->setSpacing(1);
    auto* bgIcLabel = new QLabel();
    bgIcLabel->setAlignment(Qt::AlignCenter);
    bgIcLabel->setPixmap(tintIcon(origBgIcon, fgNow).pixmap(16, 16));
    bgLay->addWidget(bgIcLabel);
    auto* bgTxLabel = new QLabel(QStringLiteral("背景"));
    bgTxLabel->setAlignment(Qt::AlignCenter);
    // 不要 setStyleSheet — 让 DTK 主题自动上色
    bgLay->addWidget(bgTxLabel);

    iconSetters_.append([bgIcLabel, origBgIcon](const QColor& c) {
        bgIcLabel->setPixmap(tintIcon(origBgIcon, c).pixmap(16, 16));
    });

    auto* bgMenu = new QMenu(bgBtn);
    struct BgTheme { QString name; QColor color; };
    const QList<BgTheme> themes = {
        { QStringLiteral("默认"),   QColor(0xF5, 0xF5, 0xF5) },
        { QStringLiteral("日间"),   QColor(0xFF, 0xFF, 0xFF) },
        { QStringLiteral("夜间"),   QColor(0x1E, 0x1E, 0x1E) },
        { QStringLiteral("护眼"),   QColor(0xC7, 0xED, 0xCC) },
        { QStringLiteral("羊皮纸"), QColor(0xF5, 0xE6, 0xC8) },
    };
    // 根据当前 DTK 主题选中匹配项（深色→夜间，浅色→默认）
    auto* helper = Dtk::Gui::DGuiApplicationHelper::instance();
    bool isDark = (helper->paletteType() == Dtk::Gui::DGuiApplicationHelper::DarkType);
    int defaultIdx = isDark ? 2 : 0;  // 夜间 / 默认
    for (int i = 0; i < themes.size(); ++i) {
        auto* act = bgMenu->addAction(themes[i].name);
        act->setCheckable(true);
        if (i == defaultIdx) act->setChecked(true);
        QObject::connect(act, &QAction::triggered, this, [this, themes, i, bgMenu]{
            render_->setCanvasColor(themes[i].color);
            for (QAction* a : bgMenu->actions()) a->setChecked(a == bgMenu->actions()[i]);
        });
    }
    bgBtn->setMenu(bgMenu);
    row->addWidget(bgBtn);

    row->addStretch();
    return page;
}

/* ---------- 字体设置 Tab ---------- */

// 加载应用自带字体目录下所有 .ttf/.otf，返回 familyName 列表
static QStringList loadBundledFonts() {
    QStringList families;
    QString appDir = QCoreApplication::applicationDirPath();
    // 多路径候选：
    //   ① 发布版：bin/ → 上一级 → fonts/
    //   ② 开发版：build/ → fonts/
    //   ③ Qt 资源（如果以后放进 qrc）
    QString searchDirs[] = {
        QDir(appDir).absoluteFilePath(QStringLiteral("../fonts")),
        appDir + QStringLiteral("/fonts"),
        QStringLiteral(":/fonts"),
    };
    for (const QString& dirPath : searchDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;
        fprintf(stderr, "[FontTab] scanning: %s\n", dirPath.toUtf8().constData());
        for (const QString& f : dir.entryList({QStringLiteral("*.ttf"),
                                                QStringLiteral("*.otf"),
                                                QStringLiteral("*.TTF"),
                                                QStringLiteral("*.OTF")})) {
            int id = QFontDatabase::addApplicationFont(dir.absoluteFilePath(f));
            if (id >= 0) {
                QStringList fams = QFontDatabase::applicationFontFamilies(id);
                families.append(fams);
                fprintf(stderr, "[FontTab] loaded %s → %s\n",
                        f.toUtf8().constData(), fams.join(",").toUtf8().constData());
            } else {
                fprintf(stderr, "[FontTab] FAILED %s\n", f.toUtf8().constData());
            }
        }
    }
    families.removeDuplicates();
    fprintf(stderr, "[FontTab] total bundled families: %lld\n", (long long)families.size());
    return families;
}

// 字符里是否含 CJK 统一表意文字（中日韩）
static bool hasCjk(const QString& s) {
    for (const QChar& c : s) {
        ushort cp = c.unicode();
        if (cp >= 0x4E00 && cp <= 0x9FFF) return true;   // CJK Unified
        if (cp >= 0x3400 && cp <= 0x4DBF) return true;   // CJK Ext A
        if (cp >= 0xF900 && cp <= 0xFAFF) return true;   // CJK Compat
        if (cp >= 0x3040 && cp <= 0x30FF) return true;   // Hiragana/Katakana
        if (cp >= 0xAC00 && cp <= 0xD7AF) return true;   // Hangul
    }
    return false;
}

// 排序规则：内置字体排最前 → 含 CJK 的系统字体 → 纯西文字体
static int fontFamilySortKey(const QString& fam, const QStringList& bundled) {
    if (bundled.contains(fam)) return 0;   // 内置：最前
    if (hasCjk(fam))            return 1;   // CJK：其次
    return 2;                                // 西文：最后
}

// 往下拉框填字体：先内置（标记 ★），后系统（中文在前），默认选 defaultFamily
static void populateFontCombo(Dtk::Widget::DComboBox* combo,
                              const QStringList& bundledFamilies,
                              const QString& defaultFamily) {
    combo->clear();
    combo->setFixedHeight(26);
    auto* model = static_cast<QStandardItemModel*>(combo->model());

    // 内置区（已按 bundledFamilies 顺序排好，不动）
    if (!bundledFamilies.isEmpty()) {
        combo->addItem(QStringLiteral("—— 内置字体 ——"));
        if (model) {
            auto* item = model->item(combo->count() - 1);
            if (item) item->setEnabled(false);
        }
        for (const QString& fam : bundledFamilies) {
            combo->addItem(fam + QStringLiteral(" ★"), fam);
        }
    }

    // 系统区：排序（内置已显示过的跳过）
    combo->addItem(QStringLiteral("—— 系统字体 ——"));
    if (model) {
        auto* item = model->item(combo->count() - 1);
        if (item) item->setEnabled(false);
    }

    QStringList sysFamilies = QFontDatabase::families();
    sysFamilies.removeIf([&bundledFamilies](const QString& f) {
        return bundledFamilies.contains(f);
    });
    // 排序：按 (分类, 字母序)
    std::sort(sysFamilies.begin(), sysFamilies.end(),
        [&bundledFamilies](const QString& a, const QString& b) {
            int ka = fontFamilySortKey(a, bundledFamilies);
            int kb = fontFamilySortKey(b, bundledFamilies);
            if (ka != kb) return ka < kb;
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
    for (const QString& fam : sysFamilies) {
        combo->addItem(fam, fam);
    }

    // 设默认选中项
    int idx = combo->findData(defaultFamily);
    if (idx < 0) idx = combo->findText(defaultFamily);
    if (idx < 0 && !bundledFamilies.isEmpty()) {
        for (int i = 0; i < combo->count(); ++i) {
            QString data = combo->itemData(i).toString();
            if (data.compare(defaultFamily, Qt::CaseInsensitive) == 0) { idx = i; break; }
        }
    }
    if (idx >= 0) combo->setCurrentIndex(idx);
}

QWidget* MainWindow::buildTabFont() {
    auto* page = new QWidget();
    auto* outerRow = new QHBoxLayout(page);
    outerRow->setContentsMargins(8, 4, 8, 4);
    outerRow->setSpacing(6);
    outerRow->addStretch();

    // 先加载内置字体
    QStringList bundled = loadBundledFonts();

    // 田字 2×2 网格
    auto* gridWrap = new QWidget();
    auto* grid = new QGridLayout(gridWrap);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);

    const int kLabelW = 32;
    const int kComboW = 180;   // 加宽：之前 130 显示不下 "Noto Sans CJK SC" 等长字体名
    const int kPopupMinW = 260; // 下拉弹出列表最小宽度（比 combo 本身宽）

    // 辅助 lambda：设置 combo 宽度 + 下拉弹出列表宽度
    auto setupCombo = [kComboW, kPopupMinW](Dtk::Widget::DComboBox* cb) {
        cb->setFixedWidth(kComboW);
        if (auto* view = cb->view()) {
            view->setMinimumWidth(kPopupMinW);
        }
    };

    // ==== 第一行：楷体 | 黑体 ====

    // 楷体（默认内置 ZhenKai_GBK-Regular.ttf → 家族名 ZhenKai_GBK）
    {
        auto* label = new QLabel(QStringLiteral("楷体:"), gridWrap);
        label->setFixedWidth(kLabelW);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fontKaiComboBox_ = new Dtk::Widget::DComboBox(gridWrap);
        setupCombo(fontKaiComboBox_);
        populateFontCombo(fontKaiComboBox_, bundled, QStringLiteral("ZhenKai_GBK"));
        auto* cell = new QHBoxLayout();
        cell->setContentsMargins(0, 0, 0, 0);
        cell->setSpacing(4);
        cell->addWidget(label);
        cell->addWidget(fontKaiComboBox_);
        auto* cellW = new QWidget(); cellW->setLayout(cell);
        grid->addWidget(cellW, 0, 0);
    }

    // 黑体（默认内置 SimXiHeiPlus.ttf → 家族名 SimXiHei Plus）
    {
        auto* label = new QLabel(QStringLiteral("黑体:"), gridWrap);
        label->setFixedWidth(kLabelW);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fontHeiComboBox_ = new Dtk::Widget::DComboBox(gridWrap);
        setupCombo(fontHeiComboBox_);
        populateFontCombo(fontHeiComboBox_, bundled, QStringLiteral("SimXiHei Plus"));
        auto* cell = new QHBoxLayout();
        cell->setContentsMargins(0, 0, 0, 0);
        cell->setSpacing(4);
        cell->addWidget(label);
        cell->addWidget(fontHeiComboBox_);
        auto* cellW = new QWidget(); cellW->setLayout(cell);
        grid->addWidget(cellW, 0, 1);
    }

    // ==== 第二行：宋体 | 仿宋 ====

    // 宋体（默认内置 SimZhiSong.ttf → 家族名 SimZhiSong）
    {
        auto* label = new QLabel(QStringLiteral("宋体:"), gridWrap);
        label->setFixedWidth(kLabelW);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fontSongComboBox_ = new Dtk::Widget::DComboBox(gridWrap);
        setupCombo(fontSongComboBox_);
        populateFontCombo(fontSongComboBox_, bundled, QStringLiteral("SimZhiSong"));
        auto* cell = new QHBoxLayout();
        cell->setContentsMargins(0, 0, 0, 0);
        cell->setSpacing(4);
        cell->addWidget(label);
        cell->addWidget(fontSongComboBox_);
        auto* cellW = new QWidget(); cellW->setLayout(cell);
        grid->addWidget(cellW, 1, 0);
    }

    // 仿宋（默认内置 ZhuqueFangsong-Regular.ttf → 家族名 Zhuque Fangsong (technical preview)）
    {
        auto* label = new QLabel(QStringLiteral("仿宋:"), gridWrap);
        label->setFixedWidth(kLabelW);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fontFangComboBox_ = new Dtk::Widget::DComboBox(gridWrap);
        setupCombo(fontFangComboBox_);
        populateFontCombo(fontFangComboBox_, bundled,
                          QStringLiteral("Zhuque Fangsong (technical preview)"));
        auto* cell = new QHBoxLayout();
        cell->setContentsMargins(0, 0, 0, 0);
        cell->setSpacing(4);
        cell->addWidget(label);
        cell->addWidget(fontFangComboBox_);
        auto* cellW = new QWidget(); cellW->setLayout(cell);
        grid->addWidget(cellW, 1, 1);
    }

    outerRow->addWidget(gridWrap);
    outerRow->addStretch();

    // ==== 一键重置按钮 ====
    auto* resetBtn = new Dtk::Widget::DPushButton(gridWrap);
    // 走 DStyle 代理取 Qt 标准图标 → 深浅色主题自动换色
    if (auto* ds = qobject_cast<Dtk::Widget::DStyle*>(QApplication::style())) {
        resetBtn->setIcon(ds->standardIcon(QStyle::SP_BrowserReload, nullptr, resetBtn));
    } else {
        resetBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserReload));
    }
    resetBtn->setText(QStringLiteral("一键重置"));
    resetBtn->setCursor(Qt::PointingHandCursor);
    resetBtn->setFixedHeight(32);
    outerRow->addWidget(resetBtn);

    // 默认值常量（与 populateFontCombo 调用参数保持同步）
    const QString kDefaultKai  = QStringLiteral("ZhenKai_GBK");
    const QString kDefaultHei  = QStringLiteral("SimXiHei Plus");
    const QString kDefaultSong = QStringLiteral("SimZhiSong");
    const QString kDefaultFang = QStringLiteral("Zhuque Fangsong (technical preview)");

    auto resetFontDefaults = [this, kDefaultKai, kDefaultHei, kDefaultSong, kDefaultFang]() {
        auto selectData = [](Dtk::Widget::DComboBox* cb, const QString& data) {
            if (!cb) return;
            int idx = cb->findData(data);
            if (idx >= 0) cb->setCurrentIndex(idx);
        };
        selectData(fontKaiComboBox_,  kDefaultKai);
        selectData(fontHeiComboBox_,  kDefaultHei);
        selectData(fontSongComboBox_, kDefaultSong);
        selectData(fontFangComboBox_, kDefaultFang);
    };
    QObject::connect(resetBtn, &Dtk::Widget::DPushButton::clicked,
                     this, resetFontDefaults);

    // ==== 信号连接：4 个下拉框任一 change → 更新全局配置 → 重绘 ====
    // 注意：populateFontCombo 里的 setCurrentIndex 会 emit 一次 currentIndexChanged，
    // 但因为 connect 在之后，所以 UI 构建阶段不会误触发。
    auto applyFontSettings = [this]() {
        FontAliasConfig cfg;
        if (fontKaiComboBox_)  cfg.kaiFamily  = fontKaiComboBox_->currentData().toString();
        if (fontHeiComboBox_)  cfg.heiFamily  = fontHeiComboBox_->currentData().toString();
        if (fontSongComboBox_) cfg.songFamily = fontSongComboBox_->currentData().toString();
        if (fontFangComboBox_) cfg.fangFamily = fontFangComboBox_->currentData().toString();
        setFontAliasConfig(cfg);
        if (render_) {
            render_->invalidatePageCache();
            render_->update();
        }
    };

    auto connectCombo = [&](Dtk::Widget::DComboBox* combo) {
        if (!combo) return;
        // 先同步一次（UI 构建阶段 currentIndexChanged 可能没 emit，但数据已经在 combo 里了）
        QObject::connect(combo, QOverload<int>::of(&Dtk::Widget::DComboBox::currentIndexChanged),
                         this, [applyFontSettings](int){ applyFontSettings(); });
    };
    connectCombo(fontKaiComboBox_);
    connectCombo(fontHeiComboBox_);
    connectCombo(fontSongComboBox_);
    connectCombo(fontFangComboBox_);
    applyFontSettings();  // UI 构建完立即同步一次默认值到全局配置

    return page;
}

/* ---------- 辅助：按钮 & 分组 ---------- */

template <typename F>
Dtk::Widget::DPushButton* MainWindow::makeBtn(const QString& text, F&& slot) {
    auto* b = new Dtk::Widget::DPushButton();
    b->setText(text);
    b->setFixedHeight(28);
    QObject::connect(b, &Dtk::Widget::DPushButton::clicked, this, std::forward<F>(slot));
    return b;
}

template <typename F>
Dtk::Widget::DPushButton* MainWindow::makeBtn(const QIcon& icon, const QString& text, F&& slot) {
    auto* b = makeBtn(text, std::forward<F>(slot));
    b->setIcon(icon);
    b->setIconSize(QSize(16, 16));
    return b;
}

QWidget* MainWindow::makeRow(const QList<QWidget*>& widgets) {
    auto* w = new QWidget();
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(2, 2, 2, 2);
    l->setSpacing(2);
    for (auto* wgt : widgets) l->addWidget(wgt);
    return w;
}

Dtk::Widget::DGroupBox* MainWindow::makeGroup(const QString& title) {
    auto* g = new Dtk::Widget::DGroupBox(title);
    g->setFlat(true);
    auto* l = new QVBoxLayout(g);
    l->setContentsMargins(8, 18, 8, 8);
    l->setSpacing(4);
    return g;
}

Dtk::Widget::DGroupBox* MainWindow::makeGroup(const QString& title, const QList<Dtk::Widget::DPushButton*>& buttons) {
    auto* g = makeGroup(title);
    auto* wrap = new QWidget();
    auto* row = new QHBoxLayout(wrap);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    for (auto* b : buttons) row->addWidget(b);
    row->addStretch();
    g->layout()->addWidget(wrap);
    return g;
}

/* ==================== Status Bar ==================== */

void MainWindow::buildStatusBar() {
    auto* sb = statusBar();

    // 页面导航区：◀ 页码滑条 ▶  页面 N/M
    prevBtn_ = new Dtk::Widget::DToolButton(sb);
    prevBtn_->setIcon(Dtk::Widget::DStyle::standardIcon(
        QApplication::style(), Dtk::Widget::DStyle::SP_ArrowPrev, nullptr, prevBtn_));
    prevBtn_->setToolTip(QStringLiteral("上一页 (PageUp)"));
    sb->addPermanentWidget(prevBtn_);

    pageSlider_ = new Dtk::Widget::DSlider(Qt::Horizontal, sb);
    pageSlider_->setMinimum(1);
    pageSlider_->setMaximum(1);
    pageSlider_->setFixedWidth(140);
    pageSlider_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    sb->addPermanentWidget(pageSlider_);

    nextBtn_ = new Dtk::Widget::DToolButton(sb);
    nextBtn_->setIcon(Dtk::Widget::DStyle::standardIcon(
        QApplication::style(), Dtk::Widget::DStyle::SP_ArrowNext, nullptr, nextBtn_));
    nextBtn_->setToolTip(QStringLiteral("下一页 (PageDown)"));
    sb->addPermanentWidget(nextBtn_);

    // "页面:" + SpinBox + "/ 总数" —— 可直接输入跳转
    pageLabelPrefix_ = new QLabel(QStringLiteral("页面:"), sb);
    pageLabelPrefix_->setContentsMargins(8, 0, 4, 0);
    sb->addPermanentWidget(pageLabelPrefix_);

    pageSpin_ = new Dtk::Widget::DSpinBox(sb);
    pageSpin_->setRange(1, 1);
    pageSpin_->setValue(1);
    pageSpin_->setFixedWidth(52);
    pageSpin_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    pageSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);  // 隐藏上下箭头
    pageSpin_->setToolTip(QStringLiteral("输入页码回车跳转"));
    sb->addPermanentWidget(pageSpin_);

    pageLabelSuffix_ = new QLabel(QStringLiteral("/ 0"), sb);
    pageLabelSuffix_->setContentsMargins(4, 0, 8, 0);
    sb->addPermanentWidget(pageLabelSuffix_);

    // 信号连接
    QObject::connect(prevBtn_, &Dtk::Widget::DToolButton::clicked, this, &MainWindow::onPagePrev);
    QObject::connect(nextBtn_, &Dtk::Widget::DToolButton::clicked, this, &MainWindow::onPageNext);

    QObject::connect(pageSlider_, &Dtk::Widget::DSlider::valueChanged, this,
        [this](int v) {
            if (!render_) return;
            render_->setPageIndex(v - 1);   // slider 1-based → 内部 0-based
            updatePageLabel();
        });

    // SpinBox 输入 → 跳转
    QObject::connect(pageSpin_, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
        this, [this](int v) {
            if (!render_) return;
            render_->setPageIndex(v - 1);   // 1-based → 内部 0-based
            updatePageLabel();
        });

    // 初始消息
    sb->showMessage(QStringLiteral("就绪。请打开 OFD 文件。"));
}

/* ==================== Property Panel Dock ==================== */

void MainWindow::buildPropertyPanelDock() {
    propPanel_ = new PropertyPanel(this);
    propPanel_->setRenderWidget(render_);

    auto* dock = new QDockWidget(QStringLiteral("属性"), this);
    dock->setWidget(propPanel_);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

/* ==================== Signal Connections ==================== */

void MainWindow::connectSignals() {
    render_->installEventFilter(this);

    auto* helper = Dtk::Gui::DGuiApplicationHelper::instance();

    // 强制整个窗口树重新应用 DTK 主题样式
    // DToolButton / QComboBox 这类传统控件不会自动响应 paletteTypeChanged，
    // 必须显式调用 style()->unpolish/polish 让它们重新读取新 palette
    auto polishAll = [this]() {
        this->style()->unpolish(this);
        this->style()->polish(this);
        // 同时对所有子控件单独 polish（防止嵌套子控件没被刷到）
        const auto all = this->findChildren<QWidget*>();
        for (auto* w : all) {
            if (w && w->style()) {
                w->style()->unpolish(w);
                w->style()->polish(w);
            }
            w->update();
        }
        this->update();
    };

    auto recollectAll = [this, polishAll](Dtk::Gui::DGuiApplicationHelper::ColorType t) {
        if (!render_) return;
        // 注意：不再在这里调 setCanvasColor！
        // 画布颜色由用户通过背景菜单选择，启动时根据主题设一次初始值，
        // 之后完全由用户控制，主题切换不覆盖画布颜色。
        for (auto& setter : iconSetters_) setter(iconFgColor(t));
        polishAll();
    };

    QObject::connect(helper, &Dtk::Gui::DGuiApplicationHelper::paletteTypeChanged,
                     this, recollectAll);

    // themeTypeChanged：系统主题变了（或用户在 DTitlebar 选"跟随系统"）
    // 直接用 themeType 染图标/画布（不依赖 paletteType 是否变化）
    // 不调 setPaletteType — 避免 hasUserManual 被设成 true 导致菜单勾选跳走
    QObject::connect(helper, &Dtk::Gui::DGuiApplicationHelper::themeTypeChanged,
                     [recollectAll](auto newTheme) {
                         if (newTheme == Dtk::Gui::DGuiApplicationHelper::UnknownType) return;
                         recollectAll(newTheme);
                     });

    // 启动时：根据主题设一次初始画布色（之后主题切换不覆盖）
    {
        auto pt = helper->paletteType();
        QColor initCanvas = (pt == Dtk::Gui::DGuiApplicationHelper::DarkType)
                            ? QColor(0x1E, 0x1E, 0x1E)   // 夜间
                            : QColor(0xF5, 0xF5, 0xF5);  // 默认（非纯白，避免白纸周围全白太刺眼）
        render_->setCanvasColor(initCanvas);
    }

    recollectAll(helper->paletteType());

    // 兜底 pollTimer：每 500ms 检查（只在自动跟随模式）
    // 覆盖 DTK 不发信号的边界情况（如用户选"跟随系统"但 themeType 没变）
    auto* pollTimer = new QTimer(this);
    QObject::connect(pollTimer, &QTimer::timeout, this, [helper, recollectAll]() {
        if (helper->hasUserManual()) return;  // 用户手动锁定了就别碰
        auto tt = helper->themeType();
        auto pt = helper->paletteType();
        if (tt != Dtk::Gui::DGuiApplicationHelper::UnknownType && tt != pt) {
            // 用 themeType 来染色（DTK 可能稍后会同步 paletteType）
            recollectAll(tt);
        }
    });
    pollTimer->start(500);

    // 键盘翻页快捷键（PageUp/PageDown）
    auto* actPrev = new QAction(this);
    actPrev->setShortcut(QKeySequence(Qt::Key_PageUp));
    QObject::connect(actPrev, &QAction::triggered, this, &MainWindow::onPagePrev);
    this->addAction(actPrev);

    auto* actNext = new QAction(this);
    actNext->setShortcut(QKeySequence(Qt::Key_PageDown));
    QObject::connect(actNext, &QAction::triggered, this, &MainWindow::onPageNext);
    this->addAction(actNext);
}

/* ==================== Helpers ==================== */

void MainWindow::showError(const QString& title, const QString& msg) {
    QMessageBox::warning(this, title, msg);
}

void MainWindow::updatePageLabel() {
    int cur = render_->pageIndex() + 1;
    int total = render_->pageCount();

    // 箭头禁用：首页禁左、末页禁右、单页全禁
    if (prevBtn_) prevBtn_->setEnabled(cur > 1 && total > 1);
    if (nextBtn_) nextBtn_->setEnabled(cur < total && total > 1);

    // SpinBox —— blockSignals 避免循环触发 valueChanged
    if (pageSpin_) {
        pageSpin_->blockSignals(true);
        pageSpin_->setRange(1, std::max(1, total));
        pageSpin_->setValue(cur);
        pageSpin_->blockSignals(false);
    }
    if (pageLabelSuffix_) {
        pageLabelSuffix_->setText(QStringLiteral("/ %1").arg(total));
    }
    if (pageSlider_) {
        pageSlider_->blockSignals(true);
        pageSlider_->setMinimum(1);
        pageSlider_->setMaximum(std::max(1, total));
        pageSlider_->setValue(cur);
        pageSlider_->blockSignals(false);
    }
}

void MainWindow::updateZoomLabel() {
    int pct = static_cast<int>(render_->scaleFactor() * 100);
    if (zoomLabel_) {
        zoomLabel_->setText(QStringLiteral("%1%").arg(pct));
    }
    if (zoomSlider_) {
        zoomSlider_->blockSignals(true);
        zoomSlider_->setValue(pct);
        zoomSlider_->blockSignals(false);
    }
}

/* ==================== File Ops ==================== */

void MainWindow::onOpen() {
    QString path = Dtk::Widget::DFileDialog::getOpenFileName(
        this, QStringLiteral("打开 OFD 文件"),
        QString(), QStringLiteral("OFD 文件 (*.ofd);;所有文件 (*)"));
    if (path.isEmpty()) return;

    // ★ 统一走 openFileByPath —— 字体加载逻辑在里面（setDocument 之前必须加载！）
    openFileByPath(path);
}

void MainWindow::openFileByPath(const QString& path) {
    fprintf(stderr, "[openFileByPath] path=%s\n", path.toUtf8().constData());
    if (path.isEmpty()) return;
    try {
        doc_ = std::make_unique<ofd::OfdJnaBridge::Doc>(bridge_->open(path.toStdString()));
        filePath_ = path;

        // ★★★ 关键：字体加载必须在 setDocument 之前！★★★
        // setDocument 会触发 paintEvent，如果 Qt 还没 sysfST 字体，第一次渲染就乱码
        // 即使后面 addApplicationFont 成功，Qt 已缓存错误字形
        try {
            std::string wd = doc_->workDir();
            if (!wd.empty()) {
                QDir workDir(QString::fromStdString(wd));
                fprintf(stderr, "[Font] OFD workDir=%s\n", wd.c_str());
                QStringList filters = { "*.ttf", "*.otf", "*.ttc", "*.cff" };
                QStringList fontFiles;
                QStringList subDirs = workDir.entryList(QDir::Dirs);
                for (const QString& sub : subDirs) {
                    QDir docDir(workDir.absoluteFilePath(sub));
                    QStringList resDirs = docDir.entryList(QStringList{"Res"}, QDir::Dirs);
                    for (const QString& rd : resDirs) {
                        QDir resDir(docDir.absoluteFilePath(rd));
                        QStringList more = resDir.entryList(filters, QDir::Files);
                        for (const QString& f : more) {
                            fontFiles.append(resDir.absoluteFilePath(f));
                        }
                    }
                }
                int loadedCount = 0;
                for (const QString& fp : fontFiles) {
                    int id = QFontDatabase::addApplicationFont(fp);
                    if (id >= 0) {
                        QStringList fams = QFontDatabase::applicationFontFamilies(id);
                        fprintf(stderr, "[Font] loaded OFD font: %s → %s\n",
                                fp.toUtf8().constData(),
                                fams.join(", ").toUtf8().constData());
                        loadedCount++;
                    } else {
                        fprintf(stderr, "[Font] FAILED to load OFD font: %s\n", fp.toUtf8().constData());
                    }
                }
                fprintf(stderr, "[Font] OFD embedded fonts loaded: %d\n", loadedCount);
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[Font] workDir/font load error: %s\n", e.what());
        }

        render_->setDocument(doc_.get());
        render_->fitToPage();
        setWindowTitle(QStringLiteral("%1 — OFD 编辑器").arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(QStringLiteral("已打开: %1").arg(path));
        updatePageLabel();
        updateZoomLabel();
    } catch (const std::exception& e) {
        fprintf(stderr, "[openFileByPath] FAILED: %s\n", e.what());
        showError(QStringLiteral("打开失败"), QString::fromStdString(e.what()));
    }
}

void MainWindow::onSaveAs() {
    if (!doc_ || !doc_->valid()) {
        showError(QStringLiteral("保存失败"), QStringLiteral("没有可保存的文档"));
        return;
    }
    QString path = Dtk::Widget::DFileDialog::getSaveFileName(
        this, QStringLiteral("另存为 OFD"),
        filePath_.isEmpty() ? QString() : filePath_,
        QStringLiteral("OFD 文件 (*.ofd)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".ofd", Qt::CaseInsensitive)) path += ".ofd";

    try {
        doc_->saveAs(path.toStdString());
        filePath_ = path;
        statusBar()->showMessage(QStringLiteral("已保存: %1").arg(path));
    } catch (const std::exception& e) {
        showError(QStringLiteral("保存失败"), QString::fromStdString(e.what()));
    }
}

/* ==================== Page Navigation ==================== */

void MainWindow::onPagePrev() {
    if (!render_) return;
    render_->setPageIndex(render_->pageIndex() - 1);
    updatePageLabel();
}

void MainWindow::onPageNext() {
    if (!render_) return;
    render_->setPageIndex(render_->pageIndex() + 1);
    updatePageLabel();
}

/* ==================== Zoom ==================== */

void MainWindow::onZoomFit() {
    if (!render_) return;
    render_->fitToPage();
    updateZoomLabel();
}

/* ==================== Edit Ops ==================== */

void MainWindow::onAddText() {
    if (!doc_ || !doc_->valid()) { showError(QStringLiteral("提示"), QStringLiteral("请先打开文档")); return; }

    bool ok = false;
    QString text = QInputDialog::getText(this, QStringLiteral("新增文本"),
                                         QStringLiteral("请输入文本内容:"),
                                         QLineEdit::Normal,
                                         QStringLiteral("新文本"), &ok);
    if (!ok || text.isEmpty()) return;

    try {
        doc_->addText(render_->pageIndex(), 50.0, 50.0,
                      text.toStdString(), 10.0, 0xFF000000);
        render_->update();
        statusBar()->showMessage(QStringLiteral("已添加文本元素"));
    } catch (const std::exception& e) {
        showError(QStringLiteral("添加失败"), QString::fromStdString(e.what()));
    }
}

void MainWindow::onAddRect() {
    if (!doc_ || !doc_->valid()) { showError(QStringLiteral("提示"), QStringLiteral("请先打开文档")); return; }

    try {
        doc_->addRectPath(render_->pageIndex(), 50.0, 50.0, 40.0, 20.0,
                          0.5, 0xFF000000, 0x00000000);
        render_->update();
        statusBar()->showMessage(QStringLiteral("已添加矩形"));
    } catch (const std::exception& e) {
        showError(QStringLiteral("添加失败"), QString::fromStdString(e.what()));
    }
}

void MainWindow::onDeleteSelected() {
    if (!doc_ || !doc_->valid() || !render_) return;
    const auto& sel = render_->selectedItem();
    if (sel.type == OfdRenderWidget::ElementType::None) {
        showError(QStringLiteral("提示"), QStringLiteral("请先选中要删除的元素"));
        return;
    }

    try {
        doc_->deleteObject(render_->pageIndex(), sel.globalIndex);
        render_->clearSelection();
        render_->update();
        statusBar()->showMessage(QStringLiteral("已删除选中元素"));
    } catch (const std::exception& e) {
        showError(QStringLiteral("删除失败"), QString::fromStdString(e.what()));
    }
}

void MainWindow::onAddBlankPage() {
    if (!doc_ || !doc_->valid()) { showError(QStringLiteral("提示"), QStringLiteral("请先打开文档")); return; }

    try {
        doc_->addPage();
        render_->setPageIndex(render_->pageCount() - 1);
        render_->fitToPage();
        updatePageLabel();
        statusBar()->showMessage(QStringLiteral("已添加空白页"));
    } catch (const std::exception& e) {
        showError(QStringLiteral("添加失败"), QString::fromStdString(e.what()));
    }
}
