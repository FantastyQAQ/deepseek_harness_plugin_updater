#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

class QProcess;

/// @brief 安装来源类型（由 profile package.json dependencies 的版本字符串判型）
enum PluginSourceType {
    SourceNpm = 0,      ///< npm 注册表版本（如 "^1.2.3" / "1.2.3" / "latest"）
    SourceGit,          ///< Git 源（"github:user/repo#commit" / "git+https://..." 等）
    SourceUnknown       ///< 无法识别的源（"file:..." / 空串等），跳过自动检查
};

/// @brief 单个已安装插件的信息（扫描结果与版本查询结果都写回这里）
struct PluginInfo
{
    // ---- 基础信息（来自 profile 的 package.json）----
    QString packageName;         ///< npm 包名（可能带 @scope/ 作用域）
    QString profileName;         ///< 所属 profile 目录名（如 "desktop"）
    QString profilePath;         ///< profile 绝对路径
    QString sourceSpec;          ///< package.json 里的原始版本/来源字符串
    PluginSourceType sourceType; ///< 来源类型

    // ---- 本地安装状态 ----
    QString installPath;         ///< node_modules/<pkg> 绝对路径（可能不存在）
    QString installedVersion;    ///< 已安装版本（node_modules/<pkg>/package.json 的 version）

    // ---- 远端最新版（npm view 查询结果）----
    QString latestVersion;       ///< npm 最新版本（空 = 未查询到）
    QDateTime latestTime;        ///< 最新版本的发布时间（registry time 表；无效 = 未拿到）
    QString usableVersion;       ///< 可用最新版：最新版已满 24h 则同 latestVersion，否则为满 24h 的最新稳定版（空 = 无）
    bool latestChecked;           ///< 是否已查询过最新版本
    bool latestFailed;            ///< 查询失败标记
    QString latestError;         ///< 查询失败原因

    PluginInfo()
        : sourceType(SourceNpm)
        , latestChecked(false)
        , latestFailed(false)
    {}
};

/// @brief 插件管理器：扫描已安装插件、查询 npm 最新版本、通过官方 dsh plugin 命令更新。
///
/// 三类操作的设计：
///  - 扫描：同步执行（纯本地读文件，27 个插件 <100ms），返回扫描代次 ID；
///  - 查询：单个 node 进程批量并发查询（见 scripts/npm_dist_tags.js）——为每个包
///    单独起 npm.cmd 太慢（单次 node 启动 0.4s + registry 往返 1~3s，串行 27 个要
///    40s+；而多个 npm 进程并发又会因 npm 缓存锁争抢更慢）。改为一次 node 进程读入
///    全部包名、内部并发打 registry 的 /-/package/<pkg>/dist-tags 端点，27 个包约 3s；
///  - 更新：QProcess 严格串行 —— pnpm 安装会争抢 node_modules 与缓存锁，不能并发；
///  - 更新子进程经 cmd.exe /d /c 包装调用（.cmd 文件必须如此），
///    中止时用 taskkill /T /F 杀整棵进程树（cmd → node → pnpm）。
class PluginManager : public QObject
{
    Q_OBJECT

public:
    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    /// @brief 扫描 DSH 用户目录下全部 profile 的已安装插件（同步）
    /// @param dshHome DSH 用户目录（如 C:/Users/xxx/.dsh）
    /// @return 扫描代次 ID
    int start_scan(const QString &dshHome);

    /// @brief 查询一批插件的 npm 最新版本（异步并发池）
    /// @param indices m_plugins 的索引列表（只应有 npm 源项，Git 源由 UI 直接标灰）
    void start_query_latest(const QVector<int> &indices);

    /// @brief 更新一批插件到最新版（异步串行队列，dsh plugin --profile X add pkg@latest）
    /// @param indices m_plugins 的索引列表（非 npm 源项会被过滤）
    void start_update(const QVector<int> &indices);

    /// @brief 请求中止当前查询/更新（杀掉活动子进程，队列停止补发）
    void request_abort();

    /// @brief 插件列表（扫描完成后有效）
    const QVector<PluginInfo> &plugins() const { return m_plugins; }

    /// @brief 按数字段比较两个 x.y.z 版本号
    /// @return a 更新返回正数，相等返回 0，更旧返回负数
    static int compare_versions(const QString &a, const QString &b);

signals:
    /// @brief 日志消息
    void log_message(const QString &text, const QColor &color);

    /// @brief 扫描清单就绪（可直接建表）
    void scan_list_ready(int scanId, const QVector<PluginInfo> &plugins);

    /// @brief 扫描结束
    void scan_finished(int scanId, bool aborted);

    /// @brief 查询进度（done = 已完成个数，0..total）
    void query_progress(int done, int total);

    /// @brief 单个插件查询完成（latestVersion 空 = 失败，原因在 error；latestTime 为最新版发布时间；usableVersion 为可用最新版）
    void query_result(int index, const QString &latestVersion, const QString &error,
                      const QDateTime &latestTime, const QString &usableVersion);

    /// @brief 查询批次结束
    void query_finished(bool aborted);

    /// @brief 更新进度（done = 已完成个数，0..total）
    void update_progress(int done, int total);

    /// @brief 单个插件更新完成（message 为输出末行或失败原因）
    void update_result(int index, bool ok, const QString &message);

    /// @brief 更新批次结束
    void update_finished(bool aborted);

private:
    void process_next_update();
    void finish_query(bool aborted);
    void finish_update(bool aborted);
    void kill_active_process();
    void backup_profiles(const QVector<int> &indices);

    /// @brief 解析查询脚本 stdout 里的一行（"包名\t版本" 或 "包名\t!错误"）
    void handle_query_line(const QString &line);
    /// @brief 确保查询脚本已落地到临时目录，返回可执行脚本路径（失败返回空）
    QString ensure_query_script();

    static PluginSourceType classify_source(const QString &spec);
    static QString read_package_version(const QString &pkgDir);
    static QString last_nonempty_line(const QString &text);

private:
    QVector<PluginInfo> m_plugins;   ///< 插件列表
    int m_scanId = 0;               ///< 扫描代次（每次 start_scan 递增）

    bool m_abortFlag = false;       ///< 中止标志
    bool m_querying = false;        ///< 查询批次进行中
    bool m_updating = false;        ///< 更新批次进行中

    // 查询（单个 node 进程批量并发查询）
    QProcess *m_queryProc = nullptr;             ///< 查询用 node 脚本进程（批次内唯一）
    QHash<QString, QVector<int>> m_queryPending;  ///< 包名 → 尚有未回填的插件索引列表
    QString m_queryStdoutBuf;                     ///< stdout 行缓冲（跨 readyRead 拼接）
    QString m_queryScriptPath;                    ///< 已落地的查询脚本临时路径（首次落盘后复用）
    int m_queryDone = 0;           ///< 已完成个数
    int m_queryTotal = 0;          ///< 本批总数

    // 更新串行队列
    QVector<int> m_updateQueue;
    int m_updateDone = 0;
    int m_updateTotal = 0;

    QProcess *m_activeProc = nullptr;  ///< 当前更新的子进程（更新严格串行，同时只有一个）
};

#endif // PLUGINMANAGER_H
