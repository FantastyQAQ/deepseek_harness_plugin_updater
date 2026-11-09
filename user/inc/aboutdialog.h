/**
 * @file aboutdialog.h
 * @brief 「关于」对话框声明
 */

#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

/**
 * @brief 模态「关于」对话框：应用名 / 版本 / 编译时间 / Qt 版本 / 作者 / 邮箱
 */
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

#endif // ABOUTDIALOG_H
