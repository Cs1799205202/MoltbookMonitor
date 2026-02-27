#include "monitorcontroller.h"

#include <QCoreApplication>
#include <QStringList>
#include <algorithm>
#include "app_version.h"

MonitorController::MonitorController(QObject *parent)
    : QAbstractListModel(parent)
    , m_shanghaiZone(QByteArrayLiteral("Asia/Shanghai"))
{
    if (QCoreApplication::applicationVersion().trimmed().isEmpty()) {
        QCoreApplication::setApplicationVersion(QString::fromLatin1(MoltbookMonitor::BuildInfo::kAppVersion));
    }

    if (!m_shanghaiZone.isValid()) {
        m_shanghaiZone = QTimeZone::systemTimeZone();
    }

    m_refreshTimer.setInterval(60 * 1000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &MonitorController::refreshAll);
    m_refreshTimer.start();

    m_countdownTimer.setInterval(1000);
    connect(&m_countdownTimer, &QTimer::timeout, this, &MonitorController::tickCountdowns);
    m_countdownTimer.start();

    m_saveDebounceTimer.setSingleShot(true);
    m_saveDebounceTimer.setInterval(300);
    connect(&m_saveDebounceTimer, &QTimer::timeout, this, [this]() { saveState(); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { saveState(); });

    loadState();

    QTimer::singleShot(3000, this, [this]() { checkForUpdatesInternal(false); });
}

int MonitorController::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_agents.size();
}

QVariant MonitorController::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_agents.size()) {
        return {};
    }

    const AgentEntry &entry = m_agents.at(index.row());
    const qint64 postRemaining = remainingSeconds(entry.lastPostUtc, entry.postThresholdMinutes);
    const qint64 replyRemaining = remainingSeconds(entry.lastReplyUtc, entry.replyThresholdMinutes);

    switch (role) {
    case AgentIdRole:
        return entry.agentId;
    case OwnerIdRole:
        return entry.ownerId;
    case HumanOwnerNameRole:
        return entry.humanOwnerName;
    case HumanOwnerGroupRole:
        return displayHumanOwner(entry.humanOwnerName);
    case PostThresholdMinutesRole:
        return entry.postThresholdMinutes;
    case ReplyThresholdMinutesRole:
        return entry.replyThresholdMinutes;
    case LastPostTimeRole:
        return formatShanghai(entry.lastPostUtc);
    case LastReplyTimeRole:
        return formatShanghai(entry.lastReplyUtc);
    case PostRemainingSecondsRole:
        return postRemaining;
    case ReplyRemainingSecondsRole:
        return replyRemaining;
    case PostCountdownTextRole:
        return formatRemaining(postRemaining);
    case ReplyCountdownTextRole:
        return formatRemaining(replyRemaining);
    case PostOverdueRole:
        return postRemaining != kMissingTimestamp && postRemaining < 0;
    case ReplyOverdueRole:
        return replyRemaining != kMissingTimestamp && replyRemaining < 0;
    case HistoryRole:
        return buildHistoryVariant(entry.history);
    case LastSyncErrorRole:
        return entry.lastSyncError;
    case LastRefreshTimeRole:
        return formatShanghai(entry.lastRefreshUtc);
    default:
        break;
    }

    return {};
}

QHash<int, QByteArray> MonitorController::roleNames() const
{
    return {
        {AgentIdRole, "agentId"},
        {OwnerIdRole, "ownerId"},
        {HumanOwnerNameRole, "humanOwnerName"},
        {HumanOwnerGroupRole, "humanOwnerGroup"},
        {PostThresholdMinutesRole, "postThresholdMinutes"},
        {ReplyThresholdMinutesRole, "replyThresholdMinutes"},
        {LastPostTimeRole, "lastPostTime"},
        {LastReplyTimeRole, "lastReplyTime"},
        {PostRemainingSecondsRole, "postRemainingSeconds"},
        {ReplyRemainingSecondsRole, "replyRemainingSeconds"},
        {PostCountdownTextRole, "postCountdownText"},
        {ReplyCountdownTextRole, "replyCountdownText"},
        {PostOverdueRole, "postOverdue"},
        {ReplyOverdueRole, "replyOverdue"},
        {HistoryRole, "history"},
        {LastSyncErrorRole, "lastSyncError"},
        {LastRefreshTimeRole, "lastRefreshTime"},
    };
}

QString MonitorController::apiKey() const
{
    return m_apiKey;
}

QString MonitorController::statusMessage() const
{
    return m_statusMessage;
}

bool MonitorController::busy() const
{
    return m_pendingRequests > 0;
}

QVariantList MonitorController::requestLogs() const
{
    return m_requestLogs;
}

QString MonitorController::currentVersion() const
{
    const QString runtimeVersion = normalizedVersionTag(QCoreApplication::applicationVersion().trimmed());
    if (!runtimeVersion.isEmpty()) {
        return runtimeVersion;
    }

    const QString buildVersion = normalizedVersionTag(QString::fromLatin1(MoltbookMonitor::BuildInfo::kAppVersion).trimmed());
    return buildVersion.isEmpty() ? QStringLiteral("0.0.0") : buildVersion;
}

bool MonitorController::updateCheckInProgress() const
{
    return m_updateCheckInProgress;
}

bool MonitorController::updateAvailable() const
{
    return m_updateAvailable;
}

QString MonitorController::latestVersion() const
{
    return m_latestVersion;
}

QString MonitorController::ignoredUpdateVersion() const
{
    return m_ignoredUpdateVersion;
}

bool MonitorController::latestUpdateIgnored() const
{
    if (m_latestVersion.isEmpty() || m_ignoredUpdateVersion.isEmpty()) {
        return false;
    }
    return compareVersionStrings(m_latestVersion, m_ignoredUpdateVersion) == 0;
}

QString MonitorController::updateStatus() const
{
    return m_updateStatus;
}

bool MonitorController::updateDownloadAvailable() const
{
    return m_updateDownloadAvailable;
}

bool MonitorController::updateDownloadInProgress() const
{
    return m_updateDownloadInProgress;
}

double MonitorController::updateDownloadProgress() const
{
    return m_updateDownloadProgress;
}

bool MonitorController::updatePackageReady() const
{
    return m_updatePackageReady;
}

void MonitorController::setApiKey(const QString &apiKey)
{
    const QString trimmed = apiKey.trimmed();
    if (trimmed == m_apiKey) {
        return;
    }

    m_apiKey = trimmed;
    emit apiKeyChanged();
    setStatusMessage(m_apiKey.isEmpty() ? tr("API key cleared.") : tr("API key updated."));
    scheduleSaveState();

    if (!m_apiKey.isEmpty() && !m_agents.isEmpty()) {
        refreshAll();
    }
}

QString MonitorController::currentShanghaiTimeString() const
{
    return QDateTime::currentDateTimeUtc().toTimeZone(m_shanghaiZone).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'CST'"));
}

void MonitorController::clearRequestLogs()
{
    if (m_requestLogs.isEmpty()) {
        return;
    }
    m_requestLogs.clear();
    emit requestLogsChanged();
}

void MonitorController::setStatusMessage(const QString &message)
{
    if (message == m_statusMessage) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

void MonitorController::setUpdateStatus(const QString &message)
{
    if (message == m_updateStatus) {
        return;
    }
    m_updateStatus = message;
    emit updateStatusChanged();
}

void MonitorController::setUpdateCheckInProgress(bool value)
{
    if (value == m_updateCheckInProgress) {
        return;
    }
    m_updateCheckInProgress = value;
    emit updateCheckInProgressChanged();
}

void MonitorController::setUpdateAvailable(bool value)
{
    if (value == m_updateAvailable) {
        return;
    }
    m_updateAvailable = value;
    emit updateAvailableChanged();
}

void MonitorController::setLatestVersion(const QString &value)
{
    if (value == m_latestVersion) {
        return;
    }
    m_latestVersion = value;
    emit latestVersionChanged();
    emit latestUpdateIgnoredChanged();
}

void MonitorController::setIgnoredUpdateVersion(const QString &value)
{
    const QString normalized = normalizedVersionTag(value);
    if (normalized == m_ignoredUpdateVersion) {
        return;
    }
    m_ignoredUpdateVersion = normalized;
    emit ignoredUpdateVersionChanged();
    emit latestUpdateIgnoredChanged();
}

void MonitorController::setUpdateDownloadAvailable(bool value)
{
    if (value == m_updateDownloadAvailable) {
        return;
    }
    m_updateDownloadAvailable = value;
    emit updateDownloadAvailableChanged();
}

void MonitorController::setUpdateDownloadInProgress(bool value)
{
    if (value == m_updateDownloadInProgress) {
        return;
    }
    m_updateDownloadInProgress = value;
    emit updateDownloadInProgressChanged();
}

void MonitorController::setUpdateDownloadProgress(double value)
{
    const double clamped = std::clamp(value, 0.0, 1.0);
    if (qFuzzyCompare(1.0 + clamped, 1.0 + m_updateDownloadProgress)) {
        return;
    }
    m_updateDownloadProgress = clamped;
    emit updateDownloadProgressChanged();
}

void MonitorController::setUpdatePackageReady(bool value)
{
    if (value == m_updatePackageReady) {
        return;
    }
    m_updatePackageReady = value;
    emit updatePackageReadyChanged();
}

qint64 MonitorController::remainingSeconds(const QDateTime &lastUtc, int thresholdMinutes) const
{
    if (!lastUtc.isValid()) {
        return kMissingTimestamp;
    }

    const qint64 elapsed = lastUtc.secsTo(QDateTime::currentDateTimeUtc());
    return static_cast<qint64>(thresholdMinutes) * 60 - elapsed;
}

QString MonitorController::formatRemaining(qint64 seconds) const
{
    if (seconds == kMissingTimestamp) {
        return tr("No data");
    }

    const bool overdue = seconds < 0;
    qint64 remaining = overdue ? -seconds : seconds;

    const qint64 days = remaining / 86400;
    remaining %= 86400;
    const qint64 hours = remaining / 3600;
    remaining %= 3600;
    const qint64 minutes = remaining / 60;
    const qint64 secs = remaining % 60;

    QStringList parts;
    if (days > 0) {
        parts << tr("%1d").arg(days);
    }
    if (hours > 0 || !parts.isEmpty()) {
        parts << tr("%1h").arg(hours);
    }
    parts << tr("%1m").arg(minutes);
    parts << tr("%1s").arg(secs);

    if (overdue) {
        return tr("Overdue by %1").arg(parts.join(QLatin1Char(' ')));
    }
    return tr("%1 left").arg(parts.join(QLatin1Char(' ')));
}

QString MonitorController::formatShanghai(const QDateTime &utc) const
{
    if (!utc.isValid()) {
        return tr("N/A");
    }
    return utc.toTimeZone(m_shanghaiZone).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'CST'"));
}

void MonitorController::tickCountdowns()
{
    if (m_agents.isEmpty()) {
        return;
    }

    for (AgentEntry &entry : m_agents) {
        const qint64 postRemaining = remainingSeconds(entry.lastPostUtc, entry.postThresholdMinutes);
        const qint64 replyRemaining = remainingSeconds(entry.lastReplyUtc, entry.replyThresholdMinutes);

        if (postRemaining != kMissingTimestamp) {
            if (postRemaining < 0 && !entry.postAlertSent) {
                const QString ownerDisplay = displayHumanOwner(entry.humanOwnerName);
                const QString message = tr("Post inactivity alert: Agent %1 | Owner %2 exceeded %3 minutes.")
                                            .arg(entry.agentId, ownerDisplay, QString::number(entry.postThresholdMinutes));
                emit notificationRaised(message);
                entry.postAlertSent = true;
            } else if (postRemaining >= 0) {
                entry.postAlertSent = false;
            }
        }

        if (replyRemaining != kMissingTimestamp) {
            if (replyRemaining < 0 && !entry.replyAlertSent) {
                const QString ownerDisplay = displayHumanOwner(entry.humanOwnerName);
                const QString message = tr("Reply inactivity alert: Agent %1 | Owner %2 exceeded %3 minutes.")
                                            .arg(entry.agentId, ownerDisplay, QString::number(entry.replyThresholdMinutes));
                emit notificationRaised(message);
                entry.replyAlertSent = true;
            } else if (replyRemaining >= 0) {
                entry.replyAlertSent = false;
            }
        }
    }

    emit dataChanged(index(0, 0), index(m_agents.size() - 1, 0), {
                                                               PostRemainingSecondsRole,
                                                               ReplyRemainingSecondsRole,
                                                               PostCountdownTextRole,
                                                               ReplyCountdownTextRole,
                                                               PostOverdueRole,
                                                               ReplyOverdueRole,
                                                           });
}

void MonitorController::scheduleSaveState()
{
    m_saveDebounceTimer.start();
}

void MonitorController::changePendingRequests(int delta)
{
    const bool wasBusy = busy();
    m_pendingRequests = std::max(0, m_pendingRequests + delta);
    if (wasBusy != busy()) {
        emit busyChanged();
    }
}
