/*
 * about_dialog.h — 关于对话框（仿 DTK6 DAboutDialog）
 */
#pragma once

#include <QDialog>

class QLabel;
class QString;

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
    ~AboutDialog() override = default;

    void setProductIcon(const QIcon& icon);
    void setProductName(const QString& name);
    void setVersion(const QString& version);
    void setWebsiteName(const QString& name);
    void setWebsiteLink(const QString& link);
    void setDescription(const QString& desc);

private:
    QLabel* iconLabel_   = nullptr;
    QLabel* nameLabel_   = nullptr;
    QLabel* versionLabel_ = nullptr;
    QLabel* homepageLabel_ = nullptr;
    QLabel* descLabel_   = nullptr;
};
