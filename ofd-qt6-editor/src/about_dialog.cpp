/*
 * about_dialog.cpp — 关于对话框（仿 DTK6 DAboutDialog 布局）
 *
 * 布局：
 *   ┌─────────────────────────────────────────┐
 *   │                                  [ × ]  │  ← 标题栏（无文字）
 *   ├─────────────────────────────────────────┤
 *   │  ┌─────┐    版本    1.0.3               │
 *   │  │ ICON│    主页    https://...          │
 *   │  │ 128 │    描述    基于...              │
 *   │  └─────┘           支持增值税电子...     │
 *   │  OFD 阅读器 / 编辑器                     │
 *   └─────────────────────────────────────────┘
 */
#include "about_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFrame>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);  // 去掉 ? 按钮
    setFixedSize(560, 320);
    setWindowTitle(QStringLiteral("关于"));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(24);

    // ===== 左侧：大图标 + 产品名 =====
    auto* leftCol = new QVBoxLayout();
    leftCol->setSpacing(8);
    leftCol->setAlignment(Qt::AlignTop);

    iconLabel_ = new QLabel();
    iconLabel_->setFixedSize(128, 128);
    iconLabel_->setAlignment(Qt::AlignCenter);
    iconLabel_->setStyleSheet(
        "QLabel { border: 1px solid rgba(128,128,128,0.3); border-radius: 16px; background: #f0f0f0; }"
    );
    leftCol->addWidget(iconLabel_);

    nameLabel_ = new QLabel();
    nameLabel_->setAlignment(Qt::AlignHCenter);
    nameLabel_->setStyleSheet("font-weight: 600; font-size: 14px;");
    leftCol->addWidget(nameLabel_);

    leftCol->addStretch(1);
    root->addLayout(leftCol);

    // ===== 右侧：版本 / 主页 / 描述 =====
    auto* rightCol = new QVBoxLayout();
    rightCol->setSpacing(12);

    auto makeRow = [](const QString& label, QLabel*& valueOut) {
        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        auto* lbl = new QLabel(label);
        lbl->setFixedWidth(48);
        lbl->setStyleSheet("color: #888;");
        valueOut = new QLabel();
        valueOut->setWordWrap(true);
        row->addWidget(lbl);
        row->addWidget(valueOut, 1);
        return row;
    };

    rightCol->addLayout(makeRow(QStringLiteral("版本"), versionLabel_));
    rightCol->addLayout(makeRow(QStringLiteral("主页"), homepageLabel_));

    // 描述（可多行、可拉伸）
    auto* descRow = new QHBoxLayout();
    descRow->setSpacing(8);
    auto* descLbl = new QLabel(QStringLiteral("描述"));
    descLbl->setFixedWidth(48);
    descLbl->setStyleSheet("color: #888;");
    descLabel_ = new QLabel();
    descLabel_->setWordWrap(true);
    descLabel_->setStyleSheet("line-height: 1.5;");
    descRow->addWidget(descLbl);
    descRow->addWidget(descLabel_, 1);
    rightCol->addLayout(descRow);

    rightCol->addStretch(1);
    root->addLayout(rightCol, 1);
}

void AboutDialog::setProductIcon(const QIcon& icon) {
    if (iconLabel_) iconLabel_->setPixmap(icon.pixmap(128, 128));
}
void AboutDialog::setProductName(const QString& name) {
    if (nameLabel_) nameLabel_->setText(name);
}
void AboutDialog::setVersion(const QString& version) {
    if (versionLabel_) versionLabel_->setText(version);
}
void AboutDialog::setWebsiteName(const QString& name) {
    // 可点击链接样式
    if (homepageLabel_) {
        homepageLabel_->setText(QStringLiteral(
            "<a href=\"%1\" style=\"color: #1d99f3; text-decoration: none;\">%1</a>").arg(name));
        homepageLabel_->setOpenExternalLinks(true);
    }
}
void AboutDialog::setWebsiteLink(const QString& link) {
    Q_UNUSED(link);  // 同 name，已在 setWebsiteName 里处理
}
void AboutDialog::setDescription(const QString& desc) {
    if (descLabel_) descLabel_->setText(desc);
}
