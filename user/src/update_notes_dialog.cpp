/**
 * @file update_notes_dialog.cpp
 * @brief 「更新说明」对话框实现：非模态窗口，展示 README 中的更新记录
 */

#include "update_notes_dialog.h"

#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

/**
 * @brief       构造更新说明窗口并载入 README
 *
 * @param[out]  {parent} QWidget* 父窗口
 * @return      none（构造/析构函数，无返回值）
 * @doxygen_sig_fp e2b3e5e74db5f0fc
 */
UpdateNotesDialog::UpdateNotesDialog(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(QStringLiteral("更新说明"));
    setWindowIcon(QIcon(QStringLiteral(":/icon/app_256.ico")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 14);
    layout->setSpacing(10);

    // 顶部标题
    auto *titleLabel = new QLabel(QStringLiteral("更新记录"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    // 正文：只读浏览器
    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(true);
    m_browser->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
        "  background: #ffffff;"
        "  border: 1px solid #d7dee8;"
        "  border-radius: 4px;"
        "  color: #344256;"
        "}"));
    layout->addWidget(m_browser, 1);

    // 底部关闭按钮（靠右）
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedWidth(96);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    load_update_notes();

    resize(900, 700);
    setMinimumSize(520, 400);
}

/**
 * @brief       读取资源里的 README 并填入浏览器
 *
 * @return      none
 * @doxygen_sig_fp bf699bb06b89b562
 */
void UpdateNotesDialog::load_update_notes()
{
    // 从嵌入资源读取 README，截取「# 更新记录」之后的部分
    QFile file(QStringLiteral(":/docs/README.zh_CN.md"));
    QString notes;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        notes = QString::fromUtf8(file.readAll());
        file.close();
    }

    const int pos = notes.indexOf(QStringLiteral("# 更新记录"));
    if (pos >= 0) {
        // 跳过「# 更新记录」标题行本身：窗口顶部已有同名标题，正文里不再重复显示
        const int lineEnd = notes.indexOf(QLatin1Char('\n'), pos);
        notes = (lineEnd >= 0) ? notes.mid(lineEnd + 1) : QString();
    }

    m_browser->setHtml(notes_to_html(notes));
}

/**
 * @brief       把更新记录 Markdown 转成可显示的 HTML
 *
 * @param[in]   {notes} const QString& 原始 Markdown 文本
 * @return      转换后的 HTML 文本
 * @doxygen_sig_fp 757958a31d99c2b4
 */
QString UpdateNotesDialog::notes_to_html(const QString &notes)
{
    // 极简 Markdown 转换：标题/粗体/行内代码，反引号用等宽着色
    QString html = notes.toHtmlEscaped();

    // 行内代码 `xxx` → 等宽蓝色
    static const QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
    html.replace(codeRe, QStringLiteral("<code style='font-family:Consolas,monospace;"
                                        "color:#2b6cb0;background:#f0f3f7;'>&lt;\\1&gt;</code>"));

    // 粗体 **xxx**
    static const QRegularExpression boldRe(QStringLiteral("\\*\\*([^*]+)\\*\\*"));
    html.replace(boldRe, QStringLiteral("<b>\\1</b>"));

    // 标题（## xxx / # xxx）
    QStringList lines = html.split(QLatin1Char('\n'));
    QStringList out;
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("## "))) {
            out << QStringLiteral("<h3>%1</h3>").arg(line.mid(3));
        } else if (line.startsWith(QStringLiteral("# "))) {
            out << QStringLiteral("<h2>%1</h2>").arg(line.mid(2));
        } else if (line.startsWith(QStringLiteral("- "))) {
            out << QStringLiteral("• %1").arg(line.mid(2));
        } else {
            out << line;
        }
    }
    html = out.join(QLatin1Char('\n'));

    // 连续两个换行成段，单个换行转 <br>
    html.replace(QStringLiteral("\n\n"), QStringLiteral("</p><p>"));
    html.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return QStringLiteral("<p>%1</p>").arg(html);
}
