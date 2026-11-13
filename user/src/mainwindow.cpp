/**
 * @file mainwindow.cpp
 * @brief 主窗口实现：布局 / QSS 主题 / 表格交互 / 扫描-查询-更新状态机
 */

#include "mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QScreen>
#include <QSet>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "aboutdialog.h"
#include "appinfo.h"
#include "updatenotesdialog.h"

// ===== 日志颜色常量（与参考工程一致的柔和色板）=====
namespace {
const QColor kColorInfo(0x53, 0x61, 0x74);      ///< 常规信息灰
const QColor kColorOk(0x15, 0x94, 0x47);        ///< 成功绿
const QColor kColorWarn(0xd5, 0x6a, 0x16);      ///< 提醒橙
const QColor kColorErr(Qt::red);                ///< 错误红
const QColor kColorDim(0x8a, 0x93, 0xa3);       ///< 弱化灰

const int kCellPadding = 24;           ///< 列宽余量：单元格左右内边距（QSS 各 7px）加富余
const int kWindowChromeWidth = 90;     ///< 窗口非表格区宽度：左右边距、面板边距、滚动条与边框
const int kWindowDefaultHeight = 1200; ///< 默认窗口高度

/**
 * @brief 按字体量出一列的宽度：取表头与各样例文本中最宽者，再加单元格内边距。
 * @param fm 表格所用字体的度量
 * @param texts 表头文字与代表性样例内容
 * @return 列宽（像素）
 */
int fit_column(const QFontMetrics &fm, const QStringList &texts)
{
    int widest = 0;
    for (const QString &text : texts)
        widest = std::max(widest, fm.horizontalAdvance(text));
    return widest + kCellPadding;
}
} // namespace

// ===== 构造 / 析构 =====

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral(APP_FULL_TITLE));
    setWindowIcon(QIcon(QStringLiteral(":/icon/app_256.ico")));

    m_manager = new PluginManager(this);

    setup_ui();

    // 连接 PluginManager 信号（全部排队到主线程处理）
    connect(m_manager, &PluginManager::log_message, this, &MainWindow::append_log);
    connect(m_manager, &PluginManager::scan_list_ready, this, &MainWindow::on_scan_list_ready);
    connect(m_manager, &PluginManager::scan_finished, this, &MainWindow::on_scan_finished);
    connect(m_manager, &PluginManager::query_progress, this, &MainWindow::on_query_progress);
    connect(m_manager, &PluginManager::query_result, this, &MainWindow::on_query_result);
    connect(m_manager, &PluginManager::query_finished, this, &MainWindow::on_query_finished);
    connect(m_manager, &PluginManager::update_progress, this, &MainWindow::on_update_progress);
    connect(m_manager, &PluginManager::update_result, this, &MainWindow::on_update_result);
    connect(m_manager, &PluginManager::update_finished, this, &MainWindow::on_update_finished);

    // 记住窗口标题基名，操作时在后面追加动态信息
    m_summaryLabel->setText(QStringLiteral("就绪 — 点击「开始扫描」索引 DSH 插件"));

    resize(default_window_size());
    setMinimumSize(860, 520);
}

MainWindow::~MainWindow() = default;

// ===== UI 构建 =====

void MainWindow::setup_ui()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWidget"));
    setCentralWidget(central);

    auto *top = new QVBoxLayout(central);
    top->setContentsMargins(14, 10, 14, 8);
    top->setSpacing(10);

    // ① 工具栏卡片
    setup_tool_bar(top);

    // ② DSH 目录行
    setup_dir_row(top);

    // ③ 表格/日志分栏（上下布局：表格在上、日志在下，日志常驻可见便于随时查看）
    auto *splitter = new QSplitter(Qt::Vertical, central);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({420, 200});
    setup_table_panel(splitter);
    setup_log_panel(splitter);
    // 窗口变高时把多余空间全部给表格，日志保持固定高度不被撑大
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    top->addWidget(splitter, 1);

    // ④ 图例
    setup_legend(top);

    // ⑤ 状态栏
    setup_status_bar();

    // ===== QSS 浅色主题（与参考工程一致）=====
    central->setStyleSheet(QStringLiteral(
        /* 全局背景 */
        "#centralWidget { background: #f5f7fa; }"
        /* 表格区 / 日志区：白色圆角方框，上下两块靠边框与间隙分隔 */
        "#panelCard { background: #ffffff; border: 1px solid #c2ccdb; border-radius: 6px; }"

        "QLabel { color: #536174; }"
        "QLabel#fieldLabel, QLabel#panelTitle { color: #344256; font-weight: bold; }"

        /* 通用按钮 */
        "QPushButton {"
        "  background: #ffffff;"
        "  border: 1px solid #d7dee8;"
        "  border-radius: 4px;"
        "  padding: 6px 13px;"
        "  color: #344256;"
        "}"
        "QPushButton:hover {"
        "  background: #f0f5ff;"
        "  border-color: #82a9e8;"
        "  color: #2166c1;"
        "}"
        "QPushButton:pressed { background: #e4efff; }"
        "QPushButton:disabled {"
        "  background: #f0f2f5;"
        "  border-color: #d7dee8;"
        "  color: #a2acb9;"
        "}"

        /* 工具栏按钮 / 主按钮 */
        "#toolBar QPushButton { padding: 7px 16px; min-height: 22px; }"
        "#primaryButton {"
        "  background: #2f73d9;"
        "  border-color: #2f73d9;"
        "  color: #ffffff;"
        "}"
        "#primaryButton:hover {"
        "  background: #2363c5;"
        "  border-color: #2363c5;"
        "  color: #ffffff;"
        "}"
        "#primaryButton:pressed { background: #1a4fa0; }"

        /* 输入框 */
        "QLineEdit {"
        "  background: #ffffff;"
        "  border: 1px solid #d7dee8;"
        "  border-radius: 4px;"
        "  padding: 5px 8px;"
        "  color: #344256;"
        "}"
        "QLineEdit:focus { border-color: #82a9e8; }"

        /* 表格 */
        "QTableWidget {"
        "  background: #ffffff;"
        "  alternate-background-color: #f8fafc;"
        "  border: none;"
        "  gridline-color: #f0f2f5;"
        "  selection-background-color: #e4efff;"
        "  selection-color: #243447;"
        "}"
        "QTableWidget::item { padding: 4px 7px; border-bottom: 1px solid #f0f2f5; }"
        "QTableWidget::item:selected {"
        "  background: #e4efff;"
        "  color: #243447;"
        "}"
        "QHeaderView::section {"
        "  background: #f0f3f7;"
        "  border: none;"
        "  border-bottom: 1px solid #d7dee8;"
        "  padding: 4px 7px;"
        "  color: #536174;"
        "  font-weight: bold;"
        "}"

        /* 日志 */
        "QTextEdit {"
        "  background: #ffffff;"
        "  border: none;"
        "  color: #536174;"
        "}"

        /* 状态栏 / 进度条 / 分栏条 */
        "QStatusBar { background: #edf1f6; border-top: 1px solid #d7dee8; }"
        "QProgressBar {"
        "  border: none;"
        "  background: #e0e6ee;"
        "  border-radius: 4px;"
        "  max-height: 8px;"
        "}"
        "QProgressBar::chunk {"
        "  background: #2f73d9;"
        "  border-radius: 4px;"
        "}"
        /* 分栏条：透明，让上下两块白框之间的浅灰底透出，形成明显分界 */
        "QSplitter::handle { width: 10px; height: 10px; background: transparent; }"));
}

// ===== 工具栏 =====

void MainWindow::setup_tool_bar(QVBoxLayout *top)
{
    auto *toolBar = new QWidget();
    toolBar->setObjectName(QStringLiteral("toolBar"));
    toolBar->setStyleSheet(QStringLiteral(
        "#toolBar { background: #ffffff; border: 1px solid #d7dee8; border-radius: 6px; }"));

    auto *bar = new QHBoxLayout(toolBar);
    bar->setContentsMargins(10, 6, 10, 6);
    bar->setSpacing(6);

    m_scanBtn = new QPushButton(QStringLiteral("开始扫描"));
    m_scanBtn->setObjectName(QStringLiteral("primaryButton"));
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scanBtn, &QPushButton::clicked, this, &MainWindow::on_start_scan);
    bar->addWidget(m_scanBtn);

    m_updateAllBtn = new QPushButton(QStringLiteral("一键更新全部"));
    m_updateAllBtn->setCursor(Qt::PointingHandCursor);
    m_updateAllBtn->setEnabled(false);
    connect(m_updateAllBtn, &QPushButton::clicked, this, &MainWindow::on_update_all);
    bar->addWidget(m_updateAllBtn);

    m_updateSelectedBtn = new QPushButton(QStringLiteral("更新选中"));
    m_updateSelectedBtn->setCursor(Qt::PointingHandCursor);
    m_updateSelectedBtn->setEnabled(false);
    connect(m_updateSelectedBtn, &QPushButton::clicked, this, &MainWindow::on_update_selected);
    bar->addWidget(m_updateSelectedBtn);

    m_stopBtn = new QPushButton(QStringLiteral("停止"));
    m_stopBtn->setCursor(Qt::PointingHandCursor);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::on_stop);
    bar->addWidget(m_stopBtn);

    bar->addSpacing(8);

    m_selectAllCb = new QCheckBox(QStringLiteral("全选"));
    m_selectAllCb->setTristate(true);
    connect(m_selectAllCb, &QCheckBox::clicked, this, [this](bool) {
        m_syncingSelectAll = true;
        const Qt::CheckState st = m_selectAllCb->checkState();
        for (int r = 0; r < m_table->rowCount(); ++r)
            m_table->item(r, ColCheck)->setCheckState(st == Qt::Checked ? Qt::Checked : Qt::Unchecked);
        m_syncingSelectAll = false;
        update_select_all_state();
        m_summaryLabel->setText(QStringLiteral("共 %1 个插件，%2 个选中")
                                     .arg(m_table->rowCount())
                                     .arg(checked_count()));
    });
    bar->addWidget(m_selectAllCb);

    bar->addStretch(1);

    // 右侧三按钮（关于=浅蓝 / 更新说明=浅绿 / 退出=浅红，与参考工程同款）
    m_aboutBtn = new QPushButton(QStringLiteral("关于"));
    m_aboutBtn->setCursor(Qt::PointingHandCursor);
    m_aboutBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #eaf1fc; border-color: #c3d8f5; color: #2166c1; }"
        "QPushButton:hover { background: #d6e4fa; border-color: #82a9e8; }"));
    connect(m_aboutBtn, &QPushButton::clicked, this, &MainWindow::on_about);
    bar->addWidget(m_aboutBtn);

    m_updateNotesBtn = new QPushButton(QStringLiteral("更新说明"));
    m_updateNotesBtn->setCursor(Qt::PointingHandCursor);
    m_updateNotesBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #e9f7ef; border-color: #c2e6d1; color: #1d7a45; }"
        "QPushButton:hover { background: #d4f0de; border-color: #82d19f; }"));
    connect(m_updateNotesBtn, &QPushButton::clicked, this, &MainWindow::on_update_notes);
    bar->addWidget(m_updateNotesBtn);

    m_exitBtn = new QPushButton(QStringLiteral("退出"));
    m_exitBtn->setCursor(Qt::PointingHandCursor);
    m_exitBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #fdecec; border-color: #f4c7c7; color: #b93a3a; }"
        "QPushButton:hover { background: #fadcdc; border-color: #e88a8a; }"));
    connect(m_exitBtn, &QPushButton::clicked, this, &MainWindow::on_exit);
    bar->addWidget(m_exitBtn);

    tool_bar_button_font();
    top->addWidget(toolBar);
}

void MainWindow::tool_bar_button_font()
{
    QFont f = QApplication::font();
    if (f.pointSizeF() > 0)
        f.setPointSizeF(f.pointSizeF() + 1);
    else
        f.setPixelSize(f.pixelSize() + 1);

    for (auto *w : {static_cast<QWidget *>(m_scanBtn),
                    static_cast<QWidget *>(m_updateAllBtn),
                    static_cast<QWidget *>(m_updateSelectedBtn),
                    static_cast<QWidget *>(m_stopBtn),
                    static_cast<QWidget *>(m_selectAllCb),
                    static_cast<QWidget *>(m_aboutBtn),
                    static_cast<QWidget *>(m_updateNotesBtn),
                    static_cast<QWidget *>(m_exitBtn)})
        w->setFont(f);
}

// ===== DSH 目录行 =====

void MainWindow::setup_dir_row(QVBoxLayout *top)
{
    auto *row = new QHBoxLayout();
    row->setSpacing(8);

    auto *label = new QLabel(QStringLiteral("DSH 用户目录："));
    label->setObjectName(QStringLiteral("fieldLabel"));
    row->addWidget(label);

    m_rootEdit = new QLineEdit();
    // 默认填当前用户 HOME/.dsh
    m_rootEdit->setText(QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + QStringLiteral("/.dsh")));
    row->addWidget(m_rootEdit, 1);

    auto *browseBtn = new QPushButton(QStringLiteral("浏览…"));
    browseBtn->setCursor(Qt::PointingHandCursor);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择 DSH 用户目录"), m_rootEdit->text());
        if (!dir.isEmpty())
            m_rootEdit->setText(QDir::toNativeSeparators(dir));
    });
    row->addWidget(browseBtn);

    top->addLayout(row);
}

// ===== 表格面板 =====

void MainWindow::setup_table_panel(QSplitter *splitter)
{
    // 面板容器：加圆角边框，让「表格区」成为清晰的方框，与下方日志区的分界一目了然
    auto *panel = new QWidget();
    panel->setObjectName(QStringLiteral("panelCard"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(10, 8, 10, 10);
    lay->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("已安装插件"));
    title->setObjectName(QStringLiteral("panelTitle"));
    lay->addWidget(title);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({
        QString(),                     // 勾选
        QStringLiteral("插件名称"),
        QStringLiteral("Profile"),
        QStringLiteral("安装源"),
        QStringLiteral("现版本"),
        QStringLiteral("可用最新版本"),
        QStringLiteral("最新版本"),
        QStringLiteral("状态"),
        QStringLiteral("安装目录"),
    });
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    // 列宽策略：所有列一律 Interactive，表头上任意分隔条都能拖动（含末列「安装目录」）。
    // 不设 Stretch：Stretch 段自身不可拖，且会挡住相邻分隔条；改为固定列宽，
    // 列总宽超出可视宽度时由表格横向滚动条兜底，从而保证「每列都能拖」。
    QHeaderView *header = m_table->horizontalHeader();
    for (int c = 0; c < ColCount; ++c)
        header->setSectionResizeMode(c, QHeaderView::Interactive);
    header->setStretchLastSection(false);
    // 默认列宽按表格字体量出样例文本宽度（见 fit_column），保证默认打开时各列内容完整显示
    const QFontMetrics fm(m_table->font());
    m_table->setColumnWidth(ColCheck, 38);
    m_table->setColumnWidth(ColName, fit_column(fm, {QStringLiteral("插件名称"),
                                                     QStringLiteral("@linxin666/dsh-client-ui-skill-explorer")}));
    m_table->setColumnWidth(ColProfile, fit_column(fm, {QStringLiteral("Profile"),
                                                        QStringLiteral("desktop")}));
    m_table->setColumnWidth(ColSource, fit_column(fm, {QStringLiteral("安装源"),
                                                       QStringLiteral("github:FantastyQAQ/dsh-doxygen")}));
    m_table->setColumnWidth(ColInstalled, fit_column(fm, {QStringLiteral("现版本"),
                                                          QStringLiteral("1.73.17")}));
    m_table->setColumnWidth(ColLatest, fit_column(fm, {QStringLiteral("最新版本"),
                                                       QStringLiteral("1.73.19")}));
    m_table->setColumnWidth(ColStatus, fit_column(fm, {QStringLiteral("状态"),
                                                       QStringLiteral("可更新（最新版 24h 后可装）"),
                                                       QStringLiteral("已是最新"),
                                                       QStringLiteral("已更新，重启 DSH 生效")}));
    m_table->setColumnWidth(ColUsable, fit_column(fm, {QStringLiteral("可用最新版本"),
                                                       QStringLiteral("1.73.19")}));
    m_table->setColumnWidth(ColDir, fit_column(fm, {QStringLiteral("安装目录"),
                                                    QStringLiteral("C:\\Users\\lch_w\\.dsh\\profiles\\desktop\\node_modules\\@linxin666\\dsh-client-ui-skill-explorer")}));
    m_table->setWordWrap(false);

    connect(m_table, &QTableWidget::itemChanged, this, &MainWindow::on_table_item_changed);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::on_table_context_menu);

    lay->addWidget(m_table);
    splitter->addWidget(panel);
}

// ===== 日志面板 =====

void MainWindow::setup_log_panel(QSplitter *splitter)
{
    // 面板容器：与表格区同款圆角方框，形成上下两块清晰的分界
    auto *panel = new QWidget();
    panel->setObjectName(QStringLiteral("panelCard"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(10, 8, 10, 10);
    lay->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("日志"));
    title->setObjectName(QStringLiteral("panelTitle"));
    lay->addWidget(title);

    m_logEdit = new QTextEdit();
    m_logEdit->setReadOnly(true);
    m_logEdit->setPlaceholderText(QStringLiteral("操作日志会显示在这里"));
    // 日志常驻可见：给一个最小高度，拖分栏条时不会被压到看不见
    m_logEdit->setMinimumHeight(90);
    lay->addWidget(m_logEdit);

    panel->setMinimumHeight(130);
    splitter->addWidget(panel);
}

// ===== 图例 =====

void MainWindow::setup_legend(QVBoxLayout *top)
{
    m_legend = new QLabel();
    m_legend->setTextFormat(Qt::RichText);
    m_legend->setText(QStringLiteral(
        "<span style='color:#159447;'>●</span> 已是最新 &nbsp;&nbsp;"
        "<span style='color:#d56a16;'>●</span> 可更新（最新版未满 24h 时改装满 24h 的可用版） &nbsp;&nbsp;"
        "<span style='color:#7c8798;'>●</span> 最新版 24h 后可装 &nbsp;&nbsp;"
        "<span style='color:#7c8798;'>●</span> 无法检查（Git 源 / 查询失败） &nbsp;&nbsp;"
        "<span style='color:#bf8b09;'>●</span> 更新中 / 其他"));
    top->addWidget(m_legend);
}

// ===== 状态栏 =====

void MainWindow::setup_status_bar()
{
    m_summaryLabel = new QLabel(QStringLiteral("就绪"));
    statusBar()->addWidget(m_summaryLabel, 1);

    m_progress = new QProgressBar();
    m_progress->setFixedWidth(160);
    m_progress->setVisible(false);
    statusBar()->addPermanentWidget(m_progress);
}

// ===== 日志 =====

void MainWindow::append_log(const QString &text, const QColor &color)
{
    const QString html = QStringLiteral("<span style='color:%1;'>%2</span>")
                             .arg(color.name(), text.toHtmlEscaped());
    m_logEdit->append(html);

    // 上限 2000 块，超出时从头裁掉多余部分
    QTextDocument *doc = m_logEdit->document();
    if (doc->blockCount() > kMaxLogBlocks) {
        QTextCursor cursor(doc->begin());
        cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor,
                            doc->blockCount() - kMaxLogBlocks);
        cursor.removeSelectedText();
    }
}

// ===== 工具栏动作 =====

void MainWindow::on_start_scan()
{
    if (m_scanning || m_querying || m_updating)
        return;

    const QString home = QDir::fromNativeSeparators(m_rootEdit->text().trimmed());
    if (home.isEmpty()) {
        append_log(QStringLiteral("请先填写 DSH 用户目录"), kColorErr);
        return;
    }

    m_scanning = true;
    m_scanBtn->setEnabled(false);
    m_updateAllBtn->setEnabled(false);
    m_updateSelectedBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_table->setRowCount(0);
    m_plugins.clear();
    m_selectAllCb->setCheckState(Qt::Unchecked);

    append_log(QStringLiteral("正在扫描 %1 …").arg(m_rootEdit->text()), kColorOk);

    // 扫描是同步调用（本地读文件，很快），结束后由信号驱动后续流程
    m_manager->start_scan(home);
}

void MainWindow::on_update_selected()
{
    if (m_scanning || m_querying || m_updating)
        return;

    QVector<int> indices;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (m_table->item(r, ColCheck)->checkState() == Qt::Checked)
            indices.append(r);

    if (indices.isEmpty()) {
        append_log(QStringLiteral("没有勾选任何插件，请先在列表中勾选要更新的插件"), kColorWarn);
        return;
    }

    begin_update(indices);
}

void MainWindow::on_update_all()
{
    if (m_scanning || m_querying || m_updating)
        return;

    // "一键更新全部" = 勾选所有可更新（有可用版本且比已装版本新）的插件；
    // 最新版未满 24h 的项也纳入，更新时改装其满 24h 的可用版本
    QVector<int> indices;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (needs_update(m_plugins.at(r)))
            indices.append(r);

    if (indices.isEmpty()) {
        append_log(QStringLiteral("没有检测到可更新的插件（已是可用最新）"), kColorOk);
        return;
    }

    begin_update(indices);
}

void MainWindow::on_stop()
{
    m_manager->request_abort();
    append_log(QStringLiteral("正在中止当前操作…"), kColorWarn);
}

// ===== 表格交互 =====

void MainWindow::on_table_item_changed(QTableWidgetItem *item)
{
    if (!item || item->column() != ColCheck || m_syncingSelectAll)
        return;

    update_select_all_state();
    m_summaryLabel->setText(QStringLiteral("共 %1 个插件，%2 个选中")
                                .arg(m_table->rowCount())
                                .arg(checked_count()));
}

void MainWindow::on_table_context_menu(const QPoint &pos)
{
    const QTableWidgetItem *item = m_table->itemAt(pos);
    if (!item)
        return;

    // 优先取选中的行集合，空则回退到右键点击行
    QList<int> rows = selected_rows();
    if (rows.isEmpty())
        rows.append(item->row());
    std::sort(rows.begin(), rows.end());

    auto *menu = new QMenu(this);

    QAction *selectAction = menu->addAction(QStringLiteral("选择"));
    connect(selectAction, &QAction::triggered, this, [this, rows]() {
        set_rows_checked(rows, true);
    });

    QAction *deselectAction = menu->addAction(QStringLiteral("取消选择"));
    connect(deselectAction, &QAction::triggered, this, [this, rows]() {
        set_rows_checked(rows, false);
    });

    menu->addSeparator();

    QAction *openAction = menu->addAction(QStringLiteral("打开插件目录"));
    connect(openAction, &QAction::triggered, this, [this, rows]() {
        if (!rows.isEmpty())
            open_plugin_dir(rows.first());
    });

    QAction *copyPkgAction = menu->addAction(QStringLiteral("复制包名"));
    connect(copyPkgAction, &QAction::triggered, this, [this, rows]() {
        if (!rows.isEmpty() && rows.first() < m_plugins.size())
            QApplication::clipboard()->setText(m_plugins.at(rows.first()).packageName);
    });

    QAction *copySourceAction = menu->addAction(QStringLiteral("复制安装源"));
    connect(copySourceAction, &QAction::triggered, this, [this, rows]() {
        if (!rows.isEmpty() && rows.first() < m_plugins.size())
            QApplication::clipboard()->setText(m_plugins.at(rows.first()).sourceSpec);
    });

    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(m_table->viewport()->mapToGlobal(pos));
}

// ===== 扫描/查询/更新信号处理 =====

void MainWindow::on_scan_list_ready(int scanId, const QVector<PluginInfo> &plugins)
{
    Q_UNUSED(scanId);
    m_plugins = plugins;

    m_table->setRowCount(plugins.size());
    m_syncingSelectAll = true;
    for (int i = 0; i < plugins.size(); ++i) {
        const PluginInfo &info = plugins.at(i);

        // 勾选列（QTableWidgetItem 自带 checkState）
        auto *chk = new QTableWidgetItem();
        chk->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        chk->setCheckState(Qt::Unchecked);
        m_table->setItem(i, ColCheck, chk);

        m_table->setItem(i, ColName, new QTableWidgetItem(info.packageName));
        m_table->setItem(i, ColProfile, new QTableWidgetItem(info.profileName));

        auto *sourceItem = new QTableWidgetItem(info.sourceSpec);
        sourceItem->setToolTip(info.sourceSpec);
        m_table->setItem(i, ColSource, sourceItem);

        m_table->setItem(i, ColInstalled,
                         new QTableWidgetItem(info.installedVersion.isEmpty()
                                                  ? QStringLiteral("（未安装）")
                                                  : info.installedVersion));

        // 最新版本列初始状态：npm 源待查询，Git 源直接标灰
        auto *latestItem = new QTableWidgetItem(
            info.sourceType == SourceNpm ? QStringLiteral("查询中…") : QStringLiteral("—"));
        m_table->setItem(i, ColLatest, latestItem);

        // 可用最新版本列与最新版本列同步：npm 源待查询，Git 源标灰
        m_table->setItem(i, ColUsable,
                         new QTableWidgetItem(info.sourceType == SourceNpm
                                                  ? QStringLiteral("查询中…")
                                                  : QStringLiteral("—")));

        auto *statusItem = new QTableWidgetItem(
            info.sourceType == SourceNpm ? QStringLiteral("等待查询")
                                         : QStringLiteral("Git 源，无法自动检查"));
        statusItem->setForeground(info.sourceType == SourceNpm ? kColorDim : kColorWarn);
        m_table->setItem(i, ColStatus, statusItem);

        auto *dirItem = new QTableWidgetItem(QDir::toNativeSeparators(info.installPath));
        dirItem->setToolTip(QDir::toNativeSeparators(info.installPath));
        m_table->setItem(i, ColDir, dirItem);
    }
    m_syncingSelectAll = false;
    update_select_all_state();
}

void MainWindow::on_scan_finished(int scanId, bool aborted)
{
    Q_UNUSED(scanId);
    m_scanning = false;
    m_scanBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);

    const int count = m_table->rowCount();
    m_summaryLabel->setText(QStringLiteral("扫描完成，共 %1 个插件").arg(count));
    append_log(QStringLiteral("扫描完成，共 %1 个插件").arg(count), kColorOk);

    if (aborted || count == 0) {
        if (aborted)
            append_log(QStringLiteral("扫描已中止"), kColorWarn);
        return;
    }

    // 扫描完成 → 自动发起全部 npm 源插件的最新版查询
    QVector<int> npmIndices;
    for (int i = 0; i < m_plugins.size(); ++i)
        if (m_plugins.at(i).sourceType == SourceNpm)
            npmIndices.append(i);

    if (npmIndices.isEmpty()) {
        append_log(QStringLiteral("没有可查询最新版本的 npm 插件"), kColorWarn);
        m_updateSelectedBtn->setEnabled(true);
        return;
    }

    m_querying = true;
    m_stopBtn->setEnabled(true);
    m_progress->setVisible(true);
    m_progress->setRange(0, npmIndices.size());
    m_progress->setValue(0);
    m_summaryLabel->setText(QStringLiteral("正在查询最新版本 0/%1 …").arg(npmIndices.size()));
    append_log(QStringLiteral("正在查询 %1 个插件的最新版本（批量查询）…").arg(npmIndices.size()),
               kColorOk);

    m_manager->start_query_latest(npmIndices);
}

void MainWindow::on_query_progress(int done, int total)
{
    m_progress->setValue(done);
    m_summaryLabel->setText(QStringLiteral("正在查询最新版本 %1/%2 …").arg(done).arg(total));
}

void MainWindow::on_query_result(int index, const QString &latestVersion, const QString &error,
                                 const QDateTime &latestTime, const QString &usableVersion)
{
    if (index < 0 || index >= m_table->rowCount())
        return;

    auto *latestItem = m_table->item(index, ColLatest);
    auto *usableItem = m_table->item(index, ColUsable);
    auto *statusItem = m_table->item(index, ColStatus);
    if (!latestItem || !usableItem || !statusItem)
        return;

    if (latestVersion.isEmpty()) {
        // 查询失败：版本两列标「—」，状态列显示失败原因
        latestItem->setText(QStringLiteral("—"));
        usableItem->setText(QStringLiteral("—"));
        statusItem->setText(error.isEmpty() ? QStringLiteral("无法检查") : error);
        statusItem->setForeground(kColorDim);
        return;
    }

    if (index >= m_plugins.size())
        return;

    // 同步查询结果到本地数据副本（更新目标与统计要用）
    PluginInfo &info = m_plugins[index];
    info.latestVersion = latestVersion;
    info.latestTime = latestTime;
    info.usableVersion = usableVersion;

    latestItem->setText(latestVersion);
    usableItem->setText(usableVersion.isEmpty() ? QStringLiteral("—") : usableVersion);

    const bool pending = is_release_pending(info);
    if (needs_update(info)) {
        // 可更新：默认勾选；最新版未满 24h 时更新的是其可用版本
        statusItem->setText(pending ? QStringLiteral("可更新（最新版 24h 后可装）")
                                    : QStringLiteral("可更新"));
        statusItem->setForeground(kColorWarn);
        m_table->item(index, ColCheck)->setCheckState(Qt::Checked);
    } else if (pending) {
        // 最新版未满 24h 且没有比已装版本更新的可用版：只能等满 24h
        statusItem->setText(QStringLiteral("最新版 24h 后可装"));
        statusItem->setForeground(kColorDim);
    } else {
        statusItem->setText(QStringLiteral("已是最新"));
        statusItem->setForeground(kColorOk);
    }

    if (!pending) {
        latestItem->setToolTip(QString());
        return;
    }
    // 悬停说明：最新版为何暂不可装，以及本次会改装哪个版本
    const QString when = latestTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    latestItem->setToolTip(usableVersion.isEmpty()
        ? QStringLiteral("最新版 %1 发布于 %2，未满 24 小时，DSH 暂缓安装。\n"
                         "暂无满 24 小时的可用版本，请稍后再更新。")
              .arg(latestVersion, when)
        : QStringLiteral("最新版 %1 发布于 %2，未满 24 小时，DSH 暂缓安装。\n"
                         "更新时将安装满 24 小时的可用版本 %3。")
              .arg(latestVersion, when, usableVersion));
}

void MainWindow::on_query_finished(bool aborted)
{
    m_querying = false;
    m_progress->setVisible(false);
    m_stopBtn->setEnabled(false);

    // 统计可更新数量（即默认勾选数）；最新版未满 24h 的项另行汇总说明
    int updatable = 0;
    int pending = 0;
    for (const PluginInfo &info : m_plugins) {
        if (needs_update(info))
            ++updatable;
        if (is_release_pending(info))
            ++pending;
    }

    m_summaryLabel->setText(QStringLiteral("共 %1 个插件，%2 个可更新")
                                .arg(m_table->rowCount())
                                .arg(updatable));
    append_log(QStringLiteral("版本查询完成：%1 个可更新").arg(updatable), kColorOk);
    if (pending > 0) {
        append_log(QStringLiteral(
                       "另有 %1 个插件的最新版发布不足 24 小时，DSH 的安装策略（minimumReleaseAge = 24h）"
                       "会暂缓安装它，更新时改装其满 24 小时的可用版本（见「可用最新版本」列）。")
                       .arg(pending),
                   kColorDim);
    }
    if (aborted)
        append_log(QStringLiteral("版本查询已中止"), kColorWarn);

    m_updateAllBtn->setEnabled(updatable > 0);
    m_updateSelectedBtn->setEnabled(m_table->rowCount() > 0);
    update_select_all_state();
}

void MainWindow::on_update_progress(int done, int total)
{
    m_progress->setValue(done);
    m_summaryLabel->setText(QStringLiteral("正在更新 %1/%2 …").arg(done).arg(total));
}

void MainWindow::on_update_result(int index, bool ok, const QString &message)
{
    if (index < 0 || index >= m_table->rowCount())
        return;

    auto *statusItem = m_table->item(index, ColStatus);
    if (ok) {
        if (statusItem) {
            statusItem->setText(QStringLiteral("已更新，重启 DSH 生效"));
            statusItem->setForeground(kColorOk);
        }
        append_log(QStringLiteral("%1 更新成功（%2）")
                       .arg(m_plugins.at(index).packageName, message),
                   kColorOk);
    } else {
        if (statusItem) {
            statusItem->setText(QStringLiteral("更新失败"));
            statusItem->setForeground(kColorErr);
        }
        append_log(QStringLiteral("%1 更新失败：%2")
                       .arg(m_plugins.at(index).packageName, message),
                   kColorErr);
    }
}

void MainWindow::on_update_finished(bool aborted)
{
    m_updating = false;
    m_progress->setVisible(false);
    m_scanBtn->setEnabled(true);
    m_updateSelectedBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);

    if (aborted) {
        append_log(QStringLiteral("更新已中止"), kColorWarn);
        m_summaryLabel->setText(QStringLiteral("更新已中止"));
        return;
    }

    append_log(QStringLiteral("全部更新完成。请重启 DeepSeek Harness 以载入新版插件。"), kColorOk);
    m_summaryLabel->setText(QStringLiteral("更新完成 — 请重启 DSH 生效"));

    // 500ms 后自动重扫刷新版本号（保留日志）
    QTimer::singleShot(500, this, [this]() {
        if (!m_scanning && !m_querying && !m_updating)
            on_start_scan();
    });
}

// ===== 更新启动（含 DSH 运行检测）=====

void MainWindow::begin_update(const QVector<int> &indices)
{
    // DSH 正在运行时更新插件有风险（HMR 状态丢失/文件占用），先确认
    if (check_dsh_running()) {
        const QMessageBox::StandardButton btn = QMessageBox::question(
            this,
            QStringLiteral("DeepSeek Harness 正在运行"),
            QStringLiteral("检测到 DeepSeek Harness 正在运行。\n\n"
                           "更新插件前建议先完全退出 DSH（托盘图标也退出），\n"
                           "否则可能出现文件占用或状态异常。\n\n"
                           "是否仍要继续更新？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (btn != QMessageBox::Yes)
            return;
    }

    // 最新版未满 24h 的插件改装可用版本：逐条写日志，让实际安装的版本一目了然
    for (int idx : indices) {
        if (idx < 0 || idx >= m_plugins.size())
            continue;
        const PluginInfo &info = m_plugins.at(idx);
        if (info.usableVersion != info.latestVersion)
            append_log(QStringLiteral("%1 最新版 %2 未满 24 小时，本次安装可用版本 %3")
                           .arg(info.packageName, info.latestVersion, info.usableVersion),
                       kColorDim);
    }

    m_updating = true;
    m_scanBtn->setEnabled(false);
    m_updateAllBtn->setEnabled(false);
    m_updateSelectedBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);

    m_progress->setVisible(true);
    m_progress->setRange(0, indices.size());
    m_progress->setValue(0);

    append_log(QStringLiteral("开始更新 %1 个插件（dsh plugin add …@latest）…")
                   .arg(indices.size()),
               kColorOk);

    m_manager->start_update(indices);
}

bool MainWindow::check_dsh_running()
{
    // 用 tasklist 静默探测 DeepSeek Harness.exe 进程
    QProcess proc;
    proc.start(QStringLiteral("cmd.exe"),
               QStringList() << QStringLiteral("/d") << QStringLiteral("/c")
                             << QStringLiteral("tasklist /fi \"IMAGENAME eq DeepSeek Harness.exe\" "
                                               "/fo csv /nh"));
    proc.waitForFinished(5000);
    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    return out.contains(QStringLiteral("DeepSeek Harness.exe"), Qt::CaseInsensitive);
}

/**
 * @brief 判断某插件的最新版是否"尚未满 24 小时"（DSH 暂不会安装它）。
 *
 * DSH 安装插件走 pnpm 并带供应链防护策略 minimumReleaseAge = 24h：
 * 发布不足 24 小时的版本会被当作不存在，pkg@latest 会落到更早的版本。
 * 因此查询脚本会同时给出「可用最新版」（满 24h 的最新稳定版），更新时直接装它。
 * 本函数只负责界面标注：可用版与最新版不同，且已装版本本身不是最新版。
 * @param info 插件信息（须已回填 latestVersion / usableVersion / installedVersion）
 * @return true = 最新版未满 24 小时，界面需要说明
 */
bool MainWindow::is_release_pending(const PluginInfo &info)
{
    return !info.latestVersion.isEmpty() && !info.installedVersion.isEmpty()
           && info.usableVersion != info.latestVersion
           && info.installedVersion != info.latestVersion;
}

/**
 * @brief 判断某插件是否应被标记为可更新（默认勾选、纳入一键更新）。
 *
 * 条件：查到了可用版本，且它比已装版本更新。已装版本与可用版相同或更高时不动，
 * 避免把已装的更高版本降级到较旧的可用版。
 * @param info 插件信息（须已回填 usableVersion / installedVersion）
 * @return true = 可用版比已装版本新，可以安装
 */
bool MainWindow::needs_update(const PluginInfo &info)
{
    return !info.usableVersion.isEmpty() && !info.installedVersion.isEmpty()
           && PluginManager::compare_versions(info.usableVersion, info.installedVersion) > 0;
}

/**
 * @brief 计算主窗口默认尺寸：宽度 = 全部列宽之和 + 非表格区余量，保证默认打开时所有列完整显示。
 *
 * 高度固定为 kWindowDefaultHeight。两者都会被当前屏幕的可用区域截断，
 * 屏幕不够宽时才出现横向滚动条（表格列仍可通过滚动访问）。
 * @return 默认窗口尺寸
 */
QSize MainWindow::default_window_size() const
{
    int width = kWindowChromeWidth;
    for (int c = 0; c < ColCount; ++c)
        width += m_table->columnWidth(c);

    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    return QSize(std::min(width, avail.width() - 20),
                 std::min(kWindowDefaultHeight, avail.height() - 20));
}

// ===== 辅助函数 =====

void MainWindow::open_plugin_dir(int row)
{
    if (row < 0 || row >= m_plugins.size())
        return;

    QString path = m_plugins.at(row).installPath;
    if (!QDir(path).exists()) {
        // node_modules/<pkg> 不存在时退回 profile 目录
        path = m_plugins.at(row).profilePath;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::set_rows_checked(const QList<int> &rows, bool checked)
{
    m_syncingSelectAll = true;
    for (int r : rows) {
        if (r >= 0 && r < m_table->rowCount() && m_table->item(r, ColCheck))
            m_table->item(r, ColCheck)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    m_syncingSelectAll = false;
    update_select_all_state();
    m_summaryLabel->setText(QStringLiteral("共 %1 个插件，%2 个选中")
                                .arg(m_table->rowCount())
                                .arg(checked_count()));
}

void MainWindow::update_select_all_state()
{
    const int total = m_table->rowCount();
    const int checked = checked_count();

    m_syncingSelectAll = true;
    if (total == 0 || checked == 0)
        m_selectAllCb->setCheckState(Qt::Unchecked);
    else if (checked == total)
        m_selectAllCb->setCheckState(Qt::Checked);
    else
        m_selectAllCb->setCheckState(Qt::PartiallyChecked);
    m_syncingSelectAll = false;
}

int MainWindow::checked_count() const
{
    int n = 0;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (m_table->item(r, ColCheck) && m_table->item(r, ColCheck)->checkState() == Qt::Checked)
            ++n;
    return n;
}

QList<int> MainWindow::selected_rows() const
{
    QList<int> rows;
    const QList<QTableWidgetItem *> items = m_table->selectedItems();
    QSet<int> seen;
    for (const QTableWidgetItem *item : items) {
        if (!seen.contains(item->row())) {
            seen.insert(item->row());
            rows.append(item->row());
        }
    }
    return rows;
}

// ===== 关于 / 更新说明 / 退出 =====

void MainWindow::on_about()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_update_notes()
{
    if (!m_updateNotesWnd) {
        m_updateNotesWnd = new UpdateNotesDialog(this);
        m_updateNotesWnd->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_updateNotesWnd, &QObject::destroyed, this, [this]() {
            m_updateNotesWnd = nullptr;
        });
        m_updateNotesWnd->show();
    } else {
        m_updateNotesWnd->raise();
        m_updateNotesWnd->activateWindow();
    }
}

void MainWindow::on_exit()
{
    qApp->quit();
}
