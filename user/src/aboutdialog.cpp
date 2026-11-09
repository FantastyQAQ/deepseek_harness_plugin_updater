/**
 * @file aboutdialog.cpp
 * @brief 「关于」对话框实现：模态展示应用名称 / 版本 / 编译时间 / 作者信息
 */

#include "aboutdialog.h"

#include "appinfo.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    // 去掉 Windows 默认的问号帮助按钮
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(QStringLiteral("关于"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(10);

    // 大标题：应用名 + 版本 + 构建日期
    auto *titleLabel = new QLabel(QStringLiteral(APP_FULL_TITLE), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    layout->addSpacing(6);

    const QString qtVersion = QStringLiteral("Qt %1").arg(QLatin1String(qVersion()));
    const QString buildTime = QStringLiteral(__DATE__) + QStringLiteral(" ")
                              + QStringLiteral(__TIME__);

    // 信息行（全部居中，弱化色）
    auto addLine = [this, layout](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("color: #536174;"));
        layout->addWidget(label);
    };

    addLine(QStringLiteral("编译时间：") + buildTime);
    addLine(qtVersion);
    addLine(QStringLiteral("作者：") + QStringLiteral(APP_AUTHOR_NAME));
    addLine(QStringLiteral("邮箱：") + QStringLiteral(APP_AUTHOR_MAIL));

    layout->addSpacing(12);

    // 确定按钮
    auto *okBtn = new QPushButton(QStringLiteral("确定"), this);
    okBtn->setFixedSize(120, 36);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(okBtn, 0, Qt::AlignHCenter);

    resize(360, 260);
}
