/**
 * @file pluginmanager.cpp
 * @brief 插件管理器实现：扫描已装插件 / npm view 查最新版 / dsh plugin 官方通道更新
 */

#include "pluginmanager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>

// ===== 内部工具 =====

/**
 * @brief 强杀一棵子进程树（cmd.exe → node → pnpm 都要杀干净，避免残留孤儿进程）
 * @param proc 要杀的 QProcess（正在运行时用 taskkill /T /F）
 */
static void kill_process_tree(QProcess *proc)
{
    if (!proc || proc->state() != QProcess::Running)
        return;
    const qint64 pid = proc->processId();
    if (pid > 0) {
        // taskkill /T 连同子进程一起杀；/F 强制（pnpm 可能正占着锁）
        QProcess::startDetached(QStringLiteral("taskkill"),
                                QStringList() << QStringLiteral("/pid") << QString::number(pid)
                                              << QStringLiteral("/T") << QStringLiteral("/F"));
    } else {
        proc->kill();
    }
}

// ===== 构造 / 析构 =====

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
}

PluginManager::~PluginManager()
{
    // 析构时若还有子进程在跑（更新 1 个 + 查询 1 个），杀掉并短暂等待收尸，避免句柄泄漏
    m_abortFlag = true;
    m_querying = false; // 先摘掉批次标志，让 finished 回调不再发信号
    m_updating = false;
    kill_active_process();

    if (m_activeProc && m_activeProc->state() != QProcess::NotRunning)
        m_activeProc->waitForFinished(1000);
    if (m_queryProc && m_queryProc->state() != QProcess::NotRunning)
        m_queryProc->waitForFinished(1000);
}

// ===== 扫描 =====

int PluginManager::start_scan(const QString &dshHome)
{
    const int scanId = ++m_scanId;
    m_abortFlag = false;
    m_plugins.clear();

    const QDir dshDir(QDir::fromNativeSeparators(dshHome));
    if (!dshDir.exists()) {
        emit log_message(QStringLiteral("DSH 用户目录不存在: ") + dshHome, QColor(Qt::red));
        emit scan_finished(scanId, true);
        return scanId;
    }

    const QDir profilesDir(dshDir.filePath(QStringLiteral("profiles")));
    if (!profilesDir.exists()) {
        emit log_message(QStringLiteral("未找到 profiles 目录: ") + profilesDir.absolutePath(),
                         QColor(Qt::red));
        emit scan_finished(scanId, true);
        return scanId;
    }

    const QStringList profileDirs =
        profilesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (profileDirs.isEmpty()) {
        emit log_message(QStringLiteral("profiles 下没有任何 profile"), QColor(Qt::darkYellow));
        emit scan_finished(scanId, false);
        return scanId;
    }

    // 遍历每个 profile 的 package.json dependencies，收集插件清单
    QVector<PluginInfo> list;
    for (const QString &profileName : profileDirs) {
        if (m_abortFlag)
            break;

        const QString profilePath = profilesDir.filePath(profileName);
        QFile pkgJson(profilePath + QStringLiteral("/package.json"));
        if (!pkgJson.open(QIODevice::ReadOnly))
            continue; // 没有 package.json 的 profile 直接跳过

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(pkgJson.readAll(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject deps = doc.object().value(QStringLiteral("dependencies")).toObject();
        const QString nodeModules = profilePath + QStringLiteral("/node_modules");

        for (auto it = deps.begin(); it != deps.end(); ++it) {
            PluginInfo info;
            info.packageName = it.key();
            info.profileName = profileName;
            info.profilePath = profilePath;
            info.sourceSpec  = it.value().toString();
            info.sourceType  = classify_source(info.sourceSpec);
            info.installPath = nodeModules + QLatin1Char('/') + info.packageName;
            info.installedVersion = read_package_version(info.installPath);
            list.append(info);
        }

        emit log_message(QStringLiteral("已索引 profile [%1]：%2 个插件")
                             .arg(profileName)
                             .arg(deps.size()),
                         QColor(0x8a, 0x93, 0xa3));
    }

    // 按 profile、包名排序，保证表格展示顺序稳定
    std::sort(list.begin(), list.end(), [](const PluginInfo &a, const PluginInfo &b) {
        if (a.profileName != b.profileName)
            return a.profileName < b.profileName;
        return a.packageName < b.packageName;
    });

    m_plugins = list;
    emit scan_list_ready(scanId, list);
    emit scan_finished(scanId, m_abortFlag);
    return scanId;
}

// ===== 查询最新版本（单个 node 进程批量并发查询）=====

/**
 * @brief 把内嵌的查询脚本落地到临时目录，返回其绝对路径。
 *
 * 查询脚本编进 qrc（:/scripts/npm_dist_tags.js），运行时需落成一个真实文件
 * 才能交给 node 执行。首次调用写盘，之后复用同一路径（覆盖写，保证与资源同步）。
 * @return 脚本绝对路径；资源缺失或写盘失败返回空串
 */
QString PluginManager::ensure_query_script()
{
    QFile res(QStringLiteral(":/scripts/npm_dist_tags.js"));
    if (!res.open(QIODevice::ReadOnly)) {
        emit log_message(QStringLiteral("查询脚本资源缺失: :/scripts/npm_dist_tags.js"),
                         QColor(Qt::red));
        return QString();
    }
    const QByteArray code = res.readAll();
    res.close();

    const QString path = QDir(QDir::tempPath())
                             .filePath(QStringLiteral("dsh_plugin_updater_npm_dist_tags.js"));
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit log_message(QStringLiteral("无法写入查询脚本到临时目录: ") + path,
                         QColor(Qt::red));
        return QString();
    }
    out.write(code);
    out.close();
    m_queryScriptPath = path;
    return path;
}

void PluginManager::start_query_latest(const QVector<int> &indices)
{
    if (m_querying)
        return; // UI 状态机保证不会发生，防御性返回

    m_abortFlag = false;
    m_queryPending.clear();
    m_queryStdoutBuf.clear();

    // 收集有效索引，按包名分组：同一个包可能在多个 profile 里都装了，
    // 一次查询结果要同时回填到这些索引上
    m_queryTotal = 0;
    for (int idx : indices) {
        if (idx < 0 || idx >= m_plugins.size())
            continue;
        const QString pkg = m_plugins.at(idx).packageName;
        m_queryPending[pkg].append(idx);
        ++m_queryTotal;
    }
    m_queryDone = 0;
    if (m_queryTotal == 0) {
        emit query_finished(false);
        return;
    }

    // 脚本落地（失败则整批按失败收尾）
    const QString script = ensure_query_script();
    if (script.isEmpty()) {
        for (auto it = m_queryPending.constBegin(); it != m_queryPending.constEnd(); ++it) {
            for (int idx : it.value()) {
                PluginInfo &info = m_plugins[idx];
                info.latestChecked = true;
                info.latestFailed = true;
                info.latestError = QStringLiteral("查询脚本不可用");
                emit query_result(idx, QString(), info.latestError, QDateTime());
                emit query_progress(++m_queryDone, m_queryTotal);
            }
        }
        m_queryPending.clear();
        emit query_finished(false);
        return;
    }

    m_querying = true;
    emit query_progress(0, m_queryTotal);

    auto *proc = new QProcess(this);
    m_queryProc = proc;

    // stdout 增量读取：按行切分后逐行回填（脚本每查到一个包就打一行）
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        m_queryStdoutBuf += QString::fromUtf8(proc->readAllStandardOutput());
        int nl;
        while ((nl = m_queryStdoutBuf.indexOf(QLatin1Char('\n'))) >= 0) {
            QString line = m_queryStdoutBuf.left(nl);
            m_queryStdoutBuf.remove(0, nl + 1);
            if (line.endsWith(QLatin1Char('\r')))
                line.chop(1);
            handle_query_line(line);
        }
    });

    // 进程结束：把缓冲区最后一行（可能无换行）刷出，再补齐脚本没返回的包并收尾
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus) {
        if (!m_queryStdoutBuf.isEmpty()) {
            QString rest = m_queryStdoutBuf;
            m_queryStdoutBuf.clear();
            if (rest.endsWith(QLatin1Char('\r')))
                rest.chop(1);
            handle_query_line(rest);
        }
        proc->deleteLater();
        if (m_queryProc == proc)
            m_queryProc = nullptr;

        // 脚本没返回结果的包（如异常退出）标记为失败
        if (m_querying && !m_abortFlag) {
            for (auto it = m_queryPending.constBegin(); it != m_queryPending.constEnd(); ++it) {
                const QString msg = exitCode == 0
                    ? QStringLiteral("查询脚本未返回该包")
                    : QStringLiteral("查询脚本退出码 %1").arg(exitCode);
                for (int idx : it.value()) {
                    PluginInfo &info = m_plugins[idx];
                    info.latestChecked = true;
                    info.latestFailed = true;
                    info.latestError = msg;
                    emit query_result(idx, QString(), msg, QDateTime());
                    emit query_progress(++m_queryDone, m_queryTotal);
                }
            }
            m_queryPending.clear();
        }
        finish_query(m_abortFlag);
    });

    // 启动失败（如未装 Node.js）时 finished 不会发出，这里兜底收尾
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart)
            return; // 其余错误会伴随 finished 信号
        proc->deleteLater();
        if (m_queryProc == proc)
            m_queryProc = nullptr;
        if (m_querying && !m_abortFlag) {
            for (auto it = m_queryPending.constBegin(); it != m_queryPending.constEnd(); ++it) {
                for (int idx : it.value()) {
                    PluginInfo &info = m_plugins[idx];
                    info.latestChecked = true;
                    info.latestFailed = true;
                    info.latestError =
                        QStringLiteral("无法启动 node（请确认已安装 Node.js 且在 PATH 中）");
                    emit query_result(idx, QString(), info.latestError, QDateTime());
                    emit query_progress(++m_queryDone, m_queryTotal);
                }
            }
            m_queryPending.clear();
        }
        finish_query(m_abortFlag);
    });

    // 90 秒总看门狗：脚本内部每个请求 20 秒超时，正常 27 个包约 3 秒，
    // 超过 90 秒基本是网络/registry 异常，强杀进程（finished 会兜底收尾）
    QPointer<QProcess> guard(proc);
    QTimer::singleShot(90000, this, [this, guard]() {
        if (guard && guard->state() == QProcess::Running) {
            emit log_message(QStringLiteral("版本查询超时（90 秒），强制中止"),
                             QColor(Qt::darkYellow));
            kill_process_tree(guard.data());
        }
    });

    // 直接把包名列表从 stdin 灌给脚本（避免命令行长度/转义问题），随后关闭 stdin
    proc->start(QStringLiteral("node"), QStringList() << script);
    if (proc->waitForStarted(5000)) {
        QStringList names = m_queryPending.keys();
        proc->write(names.join(QLatin1Char('\n')).toUtf8() + '\n');
        proc->closeWriteChannel();
    }
}

/**
 * @brief 解析查询脚本输出的一行并回填对应插件。
 *
 * 行格式（字段间以制表符分隔）：
 *   "包名\t版本\t发布时间ISO"（成功，时间可为空）或 "包名\t!错误信息"（失败）。
 * 同一包名可能对应多个插件索引（多个 profile 都装了它），都要回填。
 * @param line 不带换行的一行文本
 */
void PluginManager::handle_query_line(const QString &line)
{
    if (line.isEmpty())
        return;
    const int tab = line.indexOf(QLatin1Char('\t'));
    if (tab <= 0)
        return;

    const QString pkg = line.left(tab);
    QString value = line.mid(tab + 1);

    auto it = m_queryPending.find(pkg);
    if (it == m_queryPending.end())
        return; // 未知包名（脚本不该输出），忽略

    const bool failed = value.startsWith(QLatin1Char('!'));
    // 成功行还带第三段：发布时间（ISO 8601，可能缺失）；失败行没有第三段
    QString timeStr;
    if (!failed) {
        const int tab2 = value.indexOf(QLatin1Char('\t'));
        if (tab2 >= 0) {
            timeStr = value.mid(tab2 + 1);
            value = value.left(tab2);
        }
    }
    const QString ver = failed ? QString() : value;
    const QString err = failed ? value.mid(1) : QString();
    // ISO 8601 带时区偏移（如 ...Z），fromString 时 Qt::ISODate 可直接解析
    const QDateTime time = timeStr.isEmpty() ? QDateTime() : QDateTime::fromString(timeStr, Qt::ISODate);

    for (int idx : it.value()) {
        if (idx < 0 || idx >= m_plugins.size())
            continue;
        PluginInfo &info = m_plugins[idx];
        info.latestChecked = true;
        if (failed) {
            info.latestFailed = true;
            info.latestError = err;
            emit query_result(idx, QString(), err, QDateTime());
        } else {
            info.latestVersion = ver;
            info.latestTime = time;
            info.latestFailed = false;
            info.latestError.clear();
            emit query_result(idx, ver, QString(), time);
        }
        emit query_progress(++m_queryDone, m_queryTotal);
    }
    m_queryPending.erase(it);
}

void PluginManager::finish_query(bool aborted)
{
    if (!m_querying)
        return;
    m_querying = false;
    m_queryPending.clear();
    m_queryStdoutBuf.clear();
    emit query_finished(aborted);
}

// ===== 更新（串行队列）=====

void PluginManager::start_update(const QVector<int> &indices)
{
    if (m_updating)
        return; // 防御性返回

    m_abortFlag = false;
    m_updateQueue.clear();
    for (int idx : indices)
        if (idx >= 0 && idx < m_plugins.size() && m_plugins.at(idx).sourceType == SourceNpm)
            m_updateQueue.append(idx); // 只允许 npm 源走更新通道

    m_updateDone = 0;
    m_updateTotal = m_updateQueue.size();
    if (m_updateTotal == 0) {
        emit log_message(QStringLiteral("没有可通过 npm 通道更新的插件"), QColor(Qt::darkYellow));
        emit update_finished(false);
        return;
    }

    m_updating = true;

    // 更新前备份涉及的 profile package.json（与 DSH 自身 bak-* 惯例一致）
    backup_profiles(m_updateQueue);

    emit update_progress(0, m_updateTotal);
    process_next_update();
}

void PluginManager::process_next_update()
{
    if (!m_updating)
        return;

    if (m_abortFlag || m_updateQueue.isEmpty()) {
        finish_update(m_abortFlag);
        return;
    }

    const int idx = m_updateQueue.takeFirst();
    if (idx < 0 || idx >= m_plugins.size()) {
        QTimer::singleShot(0, this, &PluginManager::process_next_update);
        return;
    }

    const PluginInfo info = m_plugins.at(idx);
    emit log_message(QStringLiteral("正在更新 %1 (%2/%3)…")
                         .arg(info.packageName)
                         .arg(m_updateDone + 1)
                         .arg(m_updateTotal),
                     QColor(Qt::darkGreen));

    auto *proc = new QProcess(this);
    m_activeProc = proc;

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, idx](int exitCode, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        const QString err = QString::fromUtf8(proc->readAllStandardError());
        proc->deleteLater();
        if (m_activeProc == proc)
            m_activeProc = nullptr;

        if (m_updating && !m_abortFlag && idx >= 0 && idx < m_plugins.size()) {
            const bool ok = (exitCode == 0);
            QString msg;
            if (ok) {
                msg = last_nonempty_line(out); // 成功时取输出末行（通常是 pnpm 的 Done 行）
            } else {
                msg = last_nonempty_line(err);
                if (msg.isEmpty())
                    msg = QStringLiteral("dsh plugin 退出码 %1").arg(exitCode);
            }
            emit update_result(idx, ok, msg);
            emit update_progress(++m_updateDone, m_updateTotal);
        }
        QTimer::singleShot(0, this, &PluginManager::process_next_update);
    });

    // 启动失败兜底（dsh.cmd 不在 PATH 时推进队列并报错）
    connect(proc, &QProcess::errorOccurred, this, [this, proc, idx](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        if (m_activeProc == proc)
            m_activeProc = nullptr;

        if (m_updating && !m_abortFlag && idx >= 0 && idx < m_plugins.size()) {
            emit update_result(idx, false,
                               QStringLiteral("无法启动 dsh.cmd（请确认 DSH CLI 已安装且在 PATH 中）"));
            emit update_progress(++m_updateDone, m_updateTotal);
        }
        QTimer::singleShot(0, this, &PluginManager::process_next_update);
    });

    // 10 分钟看门狗：pnpm 安装偶发卡死（网络/锁）时强杀，按失败继续
    QPointer<QProcess> guard(proc);
    QTimer::singleShot(600000, this, [this, guard]() {
        if (guard && guard.data() == m_activeProc && guard->state() == QProcess::Running) {
            emit log_message(QStringLiteral("更新超时（10 分钟），强制中止当前插件更新"),
                             QColor(Qt::red));
            kill_process_tree(guard.data());
        }
    });

    // 官方插件更新通道：dsh plugin --profile <name> add <pkg>@latest
    proc->start(QStringLiteral("cmd.exe"),
                QStringList() << QStringLiteral("/d") << QStringLiteral("/c")
                              << QStringLiteral("dsh.cmd") << QStringLiteral("plugin")
                              << QStringLiteral("--profile") << info.profileName
                              << QStringLiteral("add")
                              << info.packageName + QStringLiteral("@latest"));
}

void PluginManager::finish_update(bool aborted)
{
    if (!m_updating)
        return;
    m_updating = false;
    m_updateQueue.clear();
    emit update_finished(aborted);
}

// ===== 中止 / 辅助 =====

void PluginManager::request_abort()
{
    m_abortFlag = true;
    // 杀掉正在跑的子进程；若正好在两个子进程之间，下一次 process_next_* 自行收尾
    kill_active_process();
}

void PluginManager::kill_active_process()
{
    // 更新严格串行，只有 m_activeProc 一个；查询是单个 node 脚本进程
    kill_process_tree(m_activeProc);
    kill_process_tree(m_queryProc);
}

void PluginManager::backup_profiles(const QVector<int> &indices)
{
    // 去重收集涉及的 profile，更新前各备份一份 package.json
    QSet<QString> paths;
    for (int idx : indices)
        if (idx >= 0 && idx < m_plugins.size())
            paths.insert(m_plugins.at(idx).profilePath);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyMMdd-HHmmss"));
    for (const QString &profilePath : paths) {
        const QString src = profilePath + QStringLiteral("/package.json");
        if (!QFile::exists(src))
            continue;

        QString dst = profilePath + QStringLiteral("/package.json.bak-") + stamp;
        int n = 1;
        while (QFile::exists(dst))
            dst = profilePath + QStringLiteral("/package.json.bak-") + stamp
                  + QStringLiteral("-%1").arg(n++);

        if (QFile::copy(src, dst))
            emit log_message(QStringLiteral("已备份 %1 → %2").arg(src, dst),
                             QColor(0x8a, 0x93, 0xa3));
    }
}

PluginSourceType PluginManager::classify_source(const QString &spec)
{
    const QString s = spec.trimmed();
    if (s.isEmpty())
        return SourceUnknown;

    // Git 源的几种典型形态：github:user/repo#commit、git+https://…、git://…、ssh://…
    if (s.startsWith(QStringLiteral("github:"))
        || s.startsWith(QStringLiteral("git+"))
        || s.startsWith(QStringLiteral("git://"))
        || s.startsWith(QStringLiteral("ssh://"))
        || (s.startsWith(QStringLiteral("https://")) && s.endsWith(QStringLiteral(".git"))))
        return SourceGit;

    // 本地路径源无法走 npm 通道
    if (s.startsWith(QStringLiteral("file:")) || s.startsWith(QStringLiteral("link:"))
        || s.startsWith(QStringLiteral("workspace:")))
        return SourceUnknown;

    return SourceNpm; // 其余按 semver 范围（^1.2.3 等）视为 npm 源
}

QString PluginManager::read_package_version(const QString &pkgDir)
{
    QFile f(pkgDir + QStringLiteral("/package.json"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QString();
    return doc.object().value(QStringLiteral("version")).toString();
}

QString PluginManager::last_nonempty_line(const QString &text)
{
    // Qt 5.12 用 QString::SkipEmptyParts（Qt 5.14 起才改名 Qt::SkipEmptyParts）
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                          QString::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString s = lines.at(i).trimmed();
        if (!s.isEmpty())
            return s;
    }
    return QString();
}
