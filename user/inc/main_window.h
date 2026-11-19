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

#include "plugin_manager.h"

class AboutDialog;
class UpdateNotesDialog;
class OnlineUpdateDialog;

/// @brief 主窗口：工具栏 + DSH 目录行 + 表格/日志分栏 + 图例 + 状态栏
///
/// 界面布局、交互与配色完全参照 github_pull_tool（git 批量拉取工具）。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief       构造主窗口并连接管理器信号
     *
     */
    explicit MainWindow(QWidget *parent = nullptr);
    /**
     * @brief       析构函数（默认实现）
     *
     */
    ~MainWindow() override;

private slots:
    // 工具栏
    /**
     * @brief       开始扫描 DSH 插件目录
     *
     */
    void on_start_scan();
    /**
     * @brief       更新勾选的插件
     *
     */
    void on_update_selected();
    /**
     * @brief       更新全部可更新插件
     *
     */
    void on_update_all();
    /**
     * @brief       请求停止当前扫描或更新
     *
     */
    void on_stop();

    // 表格交互
    /**
     * @brief       首列勾选变化后同步全选框
     *
     */
    void on_table_item_changed(QTableWidgetItem *item);
    /**
     * @brief       弹出表格右键菜单
     *
     */
    void on_table_context_menu(const QPoint &pos);

    // PluginManager 信号
    /**
     * @brief       按扫描结果填充表格
     *
     */
    void on_scan_list_ready(int scanId, const QVector<PluginInfo> &plugins);
    /**
     * @brief       扫描结束：恢复按钮并汇总
     *
     */
    void on_scan_finished(int scanId, bool aborted);
    /**
     * @brief       刷新版本查询进度
     *
     */
    void on_query_progress_changed(int done, int total);
    /**
     * @brief       回填单个插件的版本查询结果
     *
     */
    void on_query_result_ready(int index, const QString &latestVersion, const QString &error,
                         const QDateTime &latestTime, const QString &usableVersion);
    /**
     * @brief       版本查询结束：恢复按钮并汇总
     *
     */
    void on_query_finished(bool aborted);
    /**
     * @brief       刷新更新进度
     *
     */
    void on_update_progress_changed(int done, int total);
    /**
     * @brief       回填单个插件的更新结果
     *
     */
    void on_update_result_ready(int index, bool ok, const QString &message);
    /**
     * @brief       更新结束：恢复按钮并提示重启
     *
     */
    void on_update_finished(bool aborted);

    // 右侧三按钮
    /**
     * @brief       打开关于对话框
     *
     */
    void on_about();
    /**
     * @brief       打开更新说明窗口
     *
     */
    void on_update_notes();
    /**
     * @brief       打开在线更新窗口
     *
     */
    void on_online_update();
    /**
     * @brief       退出程序
     *
     */
    void on_exit();

private:
    // UI 构建
    /**
     * @brief       搭建主窗口整体界面框架
     *
     */
    void setup_ui();
    /**
     * @brief       创建顶部工具栏按钮行
     *
     */
    void setup_tool_bar(QVBoxLayout *topLayout);
    /**
     * @brief       创建 DSH 用户目录输入行
     *
     */
    void setup_dir_row(QVBoxLayout *topLayout);
    /**
     * @brief       创建插件表格面板
     *
     */
    void setup_table_panel(QSplitter *splitter);
    /**
     * @brief       创建日志面板
     *
     */
    void setup_log_panel(QSplitter *splitter);
    /**
     * @brief       创建状态图例
     *
     */
    void setup_legend(QVBoxLayout *topLayout);
    /**
     * @brief       创建状态栏（摘要 + 进度条）
     *
     */
    void setup_status_bar();

    // 辅助
    /**
     * @brief       追加一行带颜色的日志
     *
     */
    void append_log(const QString &text, const QColor &color = QColor(0x53, 0x61, 0x74));
    /**
     * @brief       统一放大工具栏按钮字号
     *
     */
    void apply_tool_bar_button_font();
    /**
     * @brief       校验前置条件后启动更新队列
     *
     */
    void begin_update(const QVector<int> &indices);
    /**
     * @brief       在资源管理器中打开插件目录
     *
     */
    void open_plugin_dir(int row);
    /**
     * @brief       批量设置指定行的勾选状态
     *
     */
    void set_rows_checked(const QList<int> &rows, bool checked);
    /**
     * @brief       根据各行勾选状态同步全选框
     *
     */
    void update_select_all_state();
    /**
     * @brief       当前勾选的插件数量
     *
     */
    int get_checked_count() const;
    /**
     * @brief       当前勾选的行号列表
     *
     */
    QList<int> get_selected_rows() const;
    /**
     * @brief       按屏幕可用区域计算默认窗口尺寸
     *
     */
    QSize get_default_window_size() const;
    /**
     * @brief       当前是否有插件更新任务在跑（在线更新启动新版前需确认）
     *
     */
    bool check_update_busy() const;
    /**
     * @brief       DSH 是否正在运行（运行中禁止更新）
     *
     */
    static bool check_dsh_running();
    /**
     * @brief       是否仍有待处理的 release 目录
     *
     */
    static bool check_release_pending(const PluginInfo &info);
    /**
     * @brief       该插件是否需要更新
     *
     */
    static bool check_needs_update(const PluginInfo &info);

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
    QPushButton *m_onlineUpdateBtn = nullptr;    ///< 在线更新
    QPushButton *m_updateNotesBtn = nullptr;     ///< 更新说明
    QPushButton *m_exitBtn = nullptr;              ///< 退出

    // ---- 逻辑成员 ----
    PluginManager *m_manager = nullptr;            ///< 插件管理器
    UpdateNotesDialog *m_updateNotesWnd = nullptr; ///< 更新说明窗口（非模态单例）

    bool m_syncingSelectAll = false;  ///< 全选复选框同步抑制标志
    bool m_scanning = false;          ///< 扫描进行中
    bool m_querying = false;         ///< 版本查询进行中
    bool m_updating = false;         ///< 更新进行中

    QVector<PluginInfo> m_plugins;  ///< 表格数据（与 m_manager->get_plugins() 同步）

    static constexpr int kMaxLogBlocks = 2000;  ///< 日志面板最大块数

    /// 表格列索引
    enum Col {
        ColCheck = 0,   ///< 勾选
        ColName,        ///< 插件名称
        ColProfile,     ///< Profile
        ColSource,      ///< 安装源
        ColInstalled,   ///< 现版本
        ColUsable,      ///< 可用最新版本
        ColLatest,      ///< 最新版本
        ColStatus,      ///< 状态
        ColDir,         ///< 安装目录
        ColCount
    };
};

#endif // MAINWINDOW_H
