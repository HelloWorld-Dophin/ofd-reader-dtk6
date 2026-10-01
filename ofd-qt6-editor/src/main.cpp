/*
 * main.cpp — OFD Qt6 编辑器入口（纯 Qt6，无 DTK6）
 */
#include "main_window.h"

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char* argv[]) {
    // ★ 强制 Fusion style —— 最可靠地尊重 QPalette 变化
    // Ubuntu 默认 Adwaita/QtCurve 平台风格不完全尊重 setPalette()，
    // 导致 QMenu 弹出文本颜色、toolbar 图标等无法跟随主题
    QApplication::setStyle("Fusion");

    QApplication app(argc, argv);
    app.setApplicationName("ofd-qt6-editor");
    app.setOrganizationName("Kelvinxi");
    app.setApplicationDisplayName(QStringLiteral("OFD 阅读器 / 编辑器"));
    app.setApplicationVersion("1.0.3");

    const QIcon appIcon(QStringLiteral(":/icons/app.svg"));
    app.setWindowIcon(appIcon);

    MainWindow w;
    w.setWindowIcon(appIcon);
    w.show();

    // 命令行参数：argv[1] 是 OFD 文件路径
    if (argc >= 2) {
        QString filePath = QString::fromLocal8Bit(argv[1]);
        fprintf(stderr, "[main] 命令行打开: %s\n", filePath.toUtf8().constData());
        QMetaObject::invokeMethod(&w, [&w, filePath]() {
            w.openFileByPath(filePath);
        }, Qt::QueuedConnection);
    }

    return app.exec();
}
