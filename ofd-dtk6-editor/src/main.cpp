/*
 * main.cpp — OFD DTK6 编辑器入口
 */
#include "main_window.h"

#include <DApplication>
#include <DGuiApplicationHelper>
#include <DAboutDialog>

#include <QTranslator>
#include <QLocale>
#include <QIcon>
#include <QGuiApplication>

int main(int argc, char* argv[]) {
    Dtk::Widget::DApplication app(argc, argv);
    app.setApplicationName("ofd-dtk6-editor");
    app.setOrganizationName("Kelvinxi");
    app.setApplicationDisplayName(QStringLiteral("OFD 阅读器 / 编辑器"));
    app.setApplicationVersion("1.0.0");

    // ===== 统一图标：任务栏 / 窗口 / 关于窗口右侧 =====
    const QIcon appIcon(QStringLiteral(":/icons/app.svg"));
    app.setWindowIcon(appIcon);

    // 手动加载 dtkwidget 翻译（DApplication::loadTranslator 搜路径不匹配）
    // DWidget 包含 DTitlebar/DMenu/DFileDialog/DPushButton...
    // DGui 没有 zh_CN.qm 翻译文件，跳过
    auto* t = new QTranslator(&app);
    if (t->load(QLocale("zh_CN"), QStringLiteral("dtkwidget"),
                QStringLiteral("_"),
                QStringLiteral("/usr/share/dtk6/DWidget/translations"))) {
        app.installTranslator(t);
    } else {
        delete t;
    }

    // ===== 自定义 DAboutDialog =====
    // 鉴于 applicationHomePage 并未传递至 DAboutDialog，且上游 dtk 主分支中
    // 已将 DAboutDialog 的传参构造去掉，我们主动构造 DAboutDialog 后传给 app
    auto* about = new Dtk::Widget::DAboutDialog();
    about->setProductIcon(appIcon);
    about->setProductName(app.applicationDisplayName());
    about->setVersion(app.applicationVersion());
    about->setWebsiteName(QStringLiteral("https://www.cnblogs.com/Kelvinxi"));
    about->setWebsiteLink(QStringLiteral("https://www.cnblogs.com/Kelvinxi"));
    about->setDescription(QObject::tr(
        "基于 DTK6 (Deepin Tool Kit 6) 构建的 OFD 阅读器 + 简易编辑器。\n"
        "支持增值税电子普通发票 OFD 完整渲染：文字、Path 细线、表格、\n"
        "二维码 (JBIG2)、嵌套公章 (Signature Annotation)。\n\n"
        "技术栈：ofdrw 2.0.2 (org.ofdrw) + JNA 5.14 + DTK6 + Qt6"));
    about->setAcknowledgementVisible(false);
    app.setAboutDialog(about);

    MainWindow w;
    w.setWindowIcon(appIcon);
    w.show();

    // 命令行参数：argv[1] 是 OFD 文件路径
    if (argc >= 2) {
        QString filePath = QString::fromLocal8Bit(argv[1]);
        fprintf(stderr, "[main] 命令行打开: %s\n", filePath.toUtf8().constData());
        // 调用 MainWindow 的 openFile（直接调 bridge_->open）
        // 用 QMetaObject::invokeMethod 延迟到事件循环开始后执行
        QMetaObject::invokeMethod(&w, [&w, filePath]() {
            w.openFileByPath(filePath);
        }, Qt::QueuedConnection);
    }

    return app.exec();
}
