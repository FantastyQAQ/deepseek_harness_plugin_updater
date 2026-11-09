/**
 * @file updatenotesdialog.h
 * @brief 「更新说明」对话框声明
 */

#ifndef UPDATENOTESDIALOG_H
#define UPDATENOTESDIALOG_H

#include <QRegularExpression>
#include <QWidget>

class QTextBrowser;

/**
 * @brief 非模态「更新说明」窗口：展示 README 中的更新记录列表
 */
class UpdateNotesDialog : public QWidget
{
    Q_OBJECT

public:
    explicit UpdateNotesDialog(QWidget *parent = nullptr);

private:
    void loadUpdateNotes();                ///< 读取 :/docs/README.zh_CN.md 并填充浏览器
    QString notesToHtml(const QString &notes); ///< 极简 Markdown → HTML 转换

    QTextBrowser *m_browser = nullptr;     ///< 正文浏览器
};

#endif // UPDATENOTESDIALOG_H
