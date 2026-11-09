#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QCheckBox>
#include <QColor>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QVector>

#include "pluginmanager.h"

class AboutDialog;
class UpdateNotesDialog;

/// @brief 主窗口：工具栏 + DSH 目录行 + 表格/日志分栏 + 图例 + 状态栏
///
/// 界面布局、交互与配色完全参照 github_pull_tool（git 批量拉取工具）。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    // 工具栏
    void on_start_scan();
    void on_update_selected();
    void on_update_all();
    void on_stop();

    // 表格交互
    void on_table_item_changed(QTableWidgetItem *item);
    void on_table_context_menu(const QPoint &pos);

    // PluginManager 信号
    void on_scan_list_ready(int scanId, const QVector<PluginInfo> &plugins);
    void on_scan_finished(int scanId, bool aborted);
    void on_query_progress(int done, int total);
    void on_query_result(int index, const QString &latestVersion, const QString &error,
                         const QDateTime &latestTime);
    void on_query_finished(bool aborted);
    void on_update_progress(int done, int total);
    void on_update_result(int index, bool ok, const QString &message);
    void on_update_finished(bool aborted);

    // 右侧三按钮
    void on_about();
    void on_update_notes();
    void on_exit();

private:
    // UI 构建
    void setup_ui();
    void setup_tool_bar(QVBoxLayout *topLayout);
    void setup_dir_row(QVBoxLayout *topLayout);
    void setup_table_panel(QSplitter *splitter);
    void setup_log_panel(QSplitter *splitter);
    void setup_legend(QVBoxLayout *topLayout);
    void setup_status_bar();

    // 辅助
    void append_log(const QString &text, const QColor &color = QColor(0x53, 0x61, 0x74));
    void tool_bar_button_font();
    void begin_update(const QVector<int> &indices);
    void open_plugin_dir(int row);
    void set_rows_checked(const QList<int> &rows, bool checked);
    void update_select_all_state();
    int checked_count() const;
    QList<int> selected_rows() const;
    static bool check_dsh_running();
    static bool is_release_too_fresh(const PluginInfo &info);

private:
    // ---- UI 成员 ----
    QLineEdit *m_rootEdit = nullptr;          ///< DSH 用户目录输入框
    QTableWidget *m_table = nullptr;          ///< 插件表格
    QTextEdit *m_logEdit = nullptr;           ///< 日志面板
    QLabel *m_legend = nullptr;               ///< 图例
    QLabel *m_summaryLabel = nullptr;         ///< 状态栏摘要
    QProgressBar *m_progress = nullptr;       ///< 状态栏进度条

    QPushButton *m_scanBtn = nullptr;             ///< 开始扫描（主按钮）
    QPushButton *m_updateAllBtn = nullptr;        ///< 一键更新全部
    QPushButton *m_updateSelectedBtn = nullptr;   ///< 更新选中
    QPushButton *m_stopBtn = nullptr;             ///< 停止
    QCheckBox *m_selectAllCb = nullptr;           ///< 全选（三态）

    QPushButton *m_aboutBtn = nullptr;            ///< 关于
    QPushButton *m_updateNotesBtn = nullptr;     ///< 更新说明
    QPushButton *m_exitBtn = nullptr;              ///< 退出

    // ---- 逻辑成员 ----
    PluginManager *m_manager = nullptr;            ///< 插件管理器
    UpdateNotesDialog *m_updateNotesWnd = nullptr; ///< 更新说明窗口（非模态单例）

    bool m_syncingSelectAll = false;  ///< 全选复选框同步抑制标志
    bool m_scanning = false;          ///< 扫描进行中
    bool m_querying = false;         ///< 版本查询进行中
    bool m_updating = false;         ///< 更新进行中

    QVector<PluginInfo> m_plugins;  ///< 表格数据（与 m_manager->plugins() 同步）

    static constexpr int kMaxLogBlocks = 2000;  ///< 日志面板最大块数

    /// 表格列索引
    enum Col {
        ColCheck = 0,   ///< 勾选
        ColName,        ///< 插件名称
        ColProfile,     ///< Profile
        ColSource,      ///< 安装源
        ColInstalled,   ///< 现版本
        ColLatest,      ///< 最新版本
        ColStatus,      ///< 状态
        ColDir,         ///< 安装目录
        ColCount
    };
};

#endif // MAINWINDOW_H
