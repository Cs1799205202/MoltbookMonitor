#include "monitorcontroller.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QFile>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QDir>
#include <QCoreApplication>
#include <algorithm>
#include <limits>

namespace {
constexpr int kMaxHistoryEntries = 200;
constexpr int kMaxRequestLogs = 300;
constexpr int kStateVersion = 1;
constexpr auto kMissingTimestamp = std::numeric_limits<qint64>::min();
}

MonitorController::MonitorController(QObject *parent)
    : QAbstractListModel(parent)
    , m_shanghaiZone(QByteArrayLiteral("Asia/Shanghai"))
{
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

void MonitorController::addAgent(const QString &agentId, int postThresholdMinutes, int replyThresholdMinutes)
{
    const QString requestedId = agentId.trimmed();
    if (requestedId.isEmpty()) {
        setStatusMessage(tr("Agent ID is required."));
        return;
    }
    if (m_apiKey.isEmpty()) {
        setStatusMessage(tr("Set a Moltbook API key before adding agents."));
        return;
    }
    if (postThresholdMinutes < 1 || replyThresholdMinutes < 1) {
        setStatusMessage(tr("Thresholds must be at least 1 minute."));
        return;
    }

    const QString normalized = normalizedId(requestedId);
    if (findAgentRow(requestedId) >= 0 || m_pendingAdds.contains(normalized)) {
        setStatusMessage(tr("Agent %1 is already monitored or being verified.").arg(requestedId));
        return;
    }

    m_pendingAdds.insert(normalized);
    setStatusMessage(tr("Verifying agent %1...").arg(requestedId));

    requestProfile(requestedId, [this, requestedId, postThresholdMinutes, replyThresholdMinutes, normalized](ProfileSnapshot snapshot) {
        m_pendingAdds.remove(normalized);

        if (!snapshot.ok) {
            setStatusMessage(tr("Cannot add %1: %2").arg(requestedId, snapshot.error));
            return;
        }

        if (findAgentRow(snapshot.agentId.isEmpty() ? requestedId : snapshot.agentId) >= 0) {
            setStatusMessage(tr("Agent %1 is already in the list.").arg(requestedId));
            return;
        }

        AgentEntry entry;
        entry.agentId = snapshot.agentId.isEmpty() ? requestedId : snapshot.agentId;
        entry.ownerId = snapshot.ownerId.isEmpty() ? QStringLiteral("unknown") : snapshot.ownerId;
        entry.postThresholdMinutes = postThresholdMinutes;
        entry.replyThresholdMinutes = replyThresholdMinutes;
        entry.lastPostUtc = snapshot.lastPostUtc;
        entry.lastReplyUtc = snapshot.lastReplyUtc;
        entry.history = snapshot.operations;
        entry.lastRefreshUtc = QDateTime::currentDateTimeUtc();

        beginInsertRows(QModelIndex(), m_agents.size(), m_agents.size());
        m_agents.push_back(std::move(entry));
        endInsertRows();

        setStatusMessage(tr("Added agent %1 (owner: %2).").arg(m_agents.back().agentId, m_agents.back().ownerId));
        tickCountdowns();
        scheduleSaveState();
    });
}

void MonitorController::removeAgent(int row)
{
    if (row < 0 || row >= m_agents.size()) {
        return;
    }

    const QString removedId = m_agents.at(row).agentId;
    beginRemoveRows(QModelIndex(), row, row);
    m_agents.removeAt(row);
    endRemoveRows();

    setStatusMessage(tr("Removed agent %1.").arg(removedId));
    scheduleSaveState();
}

void MonitorController::updateThresholds(int row, int postThresholdMinutes, int replyThresholdMinutes)
{
    if (row < 0 || row >= m_agents.size()) {
        return;
    }
    if (postThresholdMinutes < 1 || replyThresholdMinutes < 1) {
        setStatusMessage(tr("Thresholds must be at least 1 minute."));
        return;
    }

    AgentEntry &entry = m_agents[row];
    entry.postThresholdMinutes = postThresholdMinutes;
    entry.replyThresholdMinutes = replyThresholdMinutes;

    if (remainingSeconds(entry.lastPostUtc, entry.postThresholdMinutes) >= 0) {
        entry.postAlertSent = false;
    }
    if (remainingSeconds(entry.lastReplyUtc, entry.replyThresholdMinutes) >= 0) {
        entry.replyAlertSent = false;
    }

    const QModelIndex modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex, {
                                            PostThresholdMinutesRole,
                                            ReplyThresholdMinutesRole,
                                            PostRemainingSecondsRole,
                                            ReplyRemainingSecondsRole,
                                            PostCountdownTextRole,
                                            ReplyCountdownTextRole,
                                            PostOverdueRole,
                                            ReplyOverdueRole,
                                        });

    setStatusMessage(tr("Updated thresholds for %1.").arg(entry.agentId));
    scheduleSaveState();
}

void MonitorController::refreshAgent(int row)
{
    if (row < 0 || row >= m_agents.size()) {
        return;
    }

    const QString agentId = m_agents.at(row).agentId;
    requestProfile(agentId, [this, agentId](ProfileSnapshot snapshot) {
        const int currentRow = findAgentRow(agentId);
        if (currentRow < 0) {
            return;
        }

        if (!snapshot.ok) {
            m_agents[currentRow].lastSyncError = snapshot.error;
            m_agents[currentRow].lastRefreshUtc = QDateTime::currentDateTimeUtc();
            const QModelIndex modelIndex = index(currentRow, 0);
            emit dataChanged(modelIndex, modelIndex, {LastSyncErrorRole, LastRefreshTimeRole});
            setStatusMessage(tr("Failed to refresh %1: %2").arg(agentId, snapshot.error));
            scheduleSaveState();
            return;
        }

        applySnapshotToAgent(currentRow, snapshot);
    });
}

void MonitorController::refreshAll()
{
    if (m_agents.isEmpty()) {
        return;
    }
    if (m_apiKey.isEmpty()) {
        setStatusMessage(tr("Set an API key before refreshing."));
        return;
    }

    for (int row = 0; row < m_agents.size(); ++row) {
        refreshAgent(row);
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

void MonitorController::requestProfile(const QString &agentId, std::function<void(ProfileSnapshot)> callback)
{
    if (m_apiKey.isEmpty()) {
        ProfileSnapshot snapshot;
        snapshot.error = tr("API key is empty.");
        callback(std::move(snapshot));
        return;
    }

    QUrl url(QStringLiteral("https://www.moltbook.com/api/v1/agents/profile"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("name"), agentId.trimmed());
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("MoltbookMonitor/0.1"));
    const QString requestUrl = url.toString(QUrl::FullyEncoded);
    const QString requestContent = buildRequestContent(QStringLiteral("GET"), requestUrl);
    const QDateTime requestStartedUtc = QDateTime::currentDateTimeUtc();
    const QString requestedAgent = agentId.trimmed();

    changePendingRequests(+1);
    QNetworkReply *reply = m_network.get(request);

    connect(reply, &QNetworkReply::finished, this, [this,
                                                     reply,
                                                     callback = std::move(callback),
                                                     requestUrl,
                                                     requestContent,
                                                     requestStartedUtc,
                                                     requestedAgent]() mutable {
        const QByteArray payload = reply->readAll();
        ProfileSnapshot snapshot = parseProfileResponse(payload);
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkError = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();

        if (reply->error() != QNetworkReply::NoError) {
            if (snapshot.error.isEmpty()) {
                if (statusCode == 401 || statusCode == 403) {
                    snapshot.error = tr("Unauthorized. Check API key.");
                } else if (statusCode == 404) {
                    snapshot.error = tr("Agent not found.");
                } else {
                    snapshot.error = networkError;
                }
            }
            snapshot.ok = false;
        }

        if (!snapshot.ok && snapshot.error.isEmpty()) {
            snapshot.error = tr("Unknown profile fetch error.");
        }

        appendRequestLog(requestedAgent,
                         QStringLiteral("GET"),
                         requestUrl,
                         requestContent,
                         buildResponseContent(statusCode, networkError, payload),
                         statusCode,
                         snapshot.ok,
                         networkError,
                         requestStartedUtc);

        callback(std::move(snapshot));
        reply->deleteLater();
        changePendingRequests(-1);
    });
}

MonitorController::ProfileSnapshot MonitorController::parseProfileResponse(const QByteArray &payload) const
{
    ProfileSnapshot snapshot;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        snapshot.error = tr("Invalid server response.");
        return snapshot;
    }

    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("success")) && !root.value(QStringLiteral("success")).toBool(true)) {
        snapshot.error = root.value(QStringLiteral("error")).toString();
        if (snapshot.error.isEmpty()) {
            snapshot.error = tr("Request was rejected by server.");
        }
        const QString hint = root.value(QStringLiteral("hint")).toString();
        if (!hint.isEmpty()) {
            snapshot.error += QStringLiteral(" (%1)").arg(hint);
        }
        return snapshot;
    }

    const QJsonObject agentObject = root.value(QStringLiteral("agent")).toObject();
    if (agentObject.isEmpty()) {
        snapshot.error = tr("Agent profile is missing in response.");
        return snapshot;
    }

    snapshot.agentId = agentObject.value(QStringLiteral("name")).toString();

    const QJsonObject ownerObject = agentObject.value(QStringLiteral("owner")).toObject();
    snapshot.ownerId = ownerObject.value(QStringLiteral("x_handle")).toString();
    if (snapshot.ownerId.isEmpty()) {
        snapshot.ownerId = ownerObject.value(QStringLiteral("x_name")).toString();
    }
    if (snapshot.ownerId.isEmpty()) {
        snapshot.ownerId = QStringLiteral("unknown");
    }

    auto collectOperations = [this, &snapshot](const QJsonArray &items, const QString &type) {
        for (const QJsonValue &itemValue : items) {
            if (!itemValue.isObject()) {
                continue;
            }
            const QJsonObject item = itemValue.toObject();
            const QDateTime timestampUtc = parseIsoDate(item.value(QStringLiteral("created_at")).toString());
            if (!timestampUtc.isValid()) {
                continue;
            }

            OperationEntry op;
            op.type = type;
            op.timestampUtc = timestampUtc;

            if (type == QStringLiteral("Post")) {
                const QString title = item.value(QStringLiteral("title")).toString().trimmed();
                if (!title.isEmpty()) {
                    op.detail = summarize(title);
                } else {
                    op.detail = summarize(item.value(QStringLiteral("content")).toString());
                }
                if (!snapshot.lastPostUtc.isValid() || timestampUtc > snapshot.lastPostUtc) {
                    snapshot.lastPostUtc = timestampUtc;
                }
            } else {
                op.detail = summarize(item.value(QStringLiteral("content")).toString());
                if (!snapshot.lastReplyUtc.isValid() || timestampUtc > snapshot.lastReplyUtc) {
                    snapshot.lastReplyUtc = timestampUtc;
                }
            }

            snapshot.operations.push_back(std::move(op));
        }
    };

    collectOperations(root.value(QStringLiteral("recentPosts")).toArray(), QStringLiteral("Post"));
    collectOperations(root.value(QStringLiteral("recentComments")).toArray(), QStringLiteral("Reply"));

    std::sort(snapshot.operations.begin(), snapshot.operations.end(), [](const OperationEntry &left, const OperationEntry &right) {
        return left.timestampUtc > right.timestampUtc;
    });

    if (snapshot.operations.size() > kMaxHistoryEntries) {
        snapshot.operations.resize(kMaxHistoryEntries);
    }

    snapshot.ok = true;
    return snapshot;
}

QDateTime MonitorController::parseIsoDate(const QString &value)
{
    if (value.trimmed().isEmpty()) {
        return {};
    }

    QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(value, Qt::ISODate);
    }

    if (!parsed.isValid()) {
        return {};
    }

    return parsed.toUTC();
}

QString MonitorController::normalizedId(const QString &agentId)
{
    return agentId.trimmed().toLower();
}

QString MonitorController::summarize(const QString &text, int maxLen)
{
    const QString cleaned = text.simplified();
    if (cleaned.isEmpty()) {
        return QStringLiteral("(no content)");
    }

    if (cleaned.size() <= maxLen) {
        return cleaned;
    }

    return cleaned.left(maxLen - 3) + QStringLiteral("...");
}

QString MonitorController::operationKey(const OperationEntry &entry)
{
    return entry.type + QLatin1Char('|') + QString::number(entry.timestampUtc.toSecsSinceEpoch()) + QLatin1Char('|') + entry.detail;
}

QString MonitorController::maskedApiKey(const QString &apiKey)
{
    const QString trimmed = apiKey.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("(empty)");
    }
    if (trimmed.size() <= 8) {
        return QString(trimmed.size(), QLatin1Char('*'));
    }
    return trimmed.left(4) + QStringLiteral("...") + trimmed.right(4);
}

void MonitorController::setStatusMessage(const QString &message)
{
    if (message == m_statusMessage) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

int MonitorController::findAgentRow(const QString &agentId) const
{
    const QString target = normalizedId(agentId);
    for (int i = 0; i < m_agents.size(); ++i) {
        if (normalizedId(m_agents.at(i).agentId) == target) {
            return i;
        }
    }
    return -1;
}

void MonitorController::applySnapshotToAgent(int row, const ProfileSnapshot &snapshot)
{
    if (row < 0 || row >= m_agents.size()) {
        return;
    }

    AgentEntry &entry = m_agents[row];

    const bool newerPostSeen = snapshot.lastPostUtc.isValid() && (!entry.lastPostUtc.isValid() || snapshot.lastPostUtc > entry.lastPostUtc);
    const bool newerReplySeen = snapshot.lastReplyUtc.isValid() && (!entry.lastReplyUtc.isValid() || snapshot.lastReplyUtc > entry.lastReplyUtc);

    entry.agentId = snapshot.agentId.isEmpty() ? entry.agentId : snapshot.agentId;
    entry.ownerId = snapshot.ownerId.isEmpty() ? entry.ownerId : snapshot.ownerId;
    if (snapshot.lastPostUtc.isValid()) {
        entry.lastPostUtc = snapshot.lastPostUtc;
    }
    if (snapshot.lastReplyUtc.isValid()) {
        entry.lastReplyUtc = snapshot.lastReplyUtc;
    }

    QSet<QString> knownOperationKeys;
    for (const OperationEntry &existing : entry.history) {
        knownOperationKeys.insert(operationKey(existing));
    }

    for (const OperationEntry &incoming : snapshot.operations) {
        const QString key = operationKey(incoming);
        if (knownOperationKeys.contains(key)) {
            continue;
        }
        entry.history.push_back(incoming);
        knownOperationKeys.insert(key);
    }

    std::sort(entry.history.begin(), entry.history.end(), [](const OperationEntry &left, const OperationEntry &right) {
        return left.timestampUtc > right.timestampUtc;
    });
    if (entry.history.size() > kMaxHistoryEntries) {
        entry.history.resize(kMaxHistoryEntries);
    }

    if (newerPostSeen) {
        entry.postAlertSent = false;
    }
    if (newerReplySeen) {
        entry.replyAlertSent = false;
    }

    entry.lastSyncError.clear();
    entry.lastRefreshUtc = QDateTime::currentDateTimeUtc();

    const QModelIndex modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex, {
                                            AgentIdRole,
                                            OwnerIdRole,
                                            LastPostTimeRole,
                                            LastReplyTimeRole,
                                            PostRemainingSecondsRole,
                                            ReplyRemainingSecondsRole,
                                            PostCountdownTextRole,
                                            ReplyCountdownTextRole,
                                            PostOverdueRole,
                                            ReplyOverdueRole,
                                            HistoryRole,
                                            LastSyncErrorRole,
                                            LastRefreshTimeRole,
                                        });
    scheduleSaveState();
}

QVariantList MonitorController::buildHistoryVariant(const QVector<OperationEntry> &history) const
{
    QVariantList list;
    list.reserve(history.size());
    for (const OperationEntry &entry : history) {
        QVariantMap row;
        row.insert(QStringLiteral("type"), entry.type);
        row.insert(QStringLiteral("detail"), entry.detail);
        row.insert(QStringLiteral("timestamp"), formatShanghai(entry.timestampUtc));
        list.push_back(row);
    }
    return list;
}

QString MonitorController::payloadForLog(const QByteArray &payload) const
{
    if (payload.isEmpty()) {
        return QStringLiteral("(empty)");
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error == QJsonParseError::NoError && !doc.isNull()) {
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).trimmed();
    }

    QString text = QString::fromUtf8(payload).trimmed();
    if (!text.contains(QChar::ReplacementCharacter)) {
        return text.isEmpty() ? QStringLiteral("(empty)") : text;
    }

    return QStringLiteral("Non-UTF8 payload (%1 bytes)\n%2")
        .arg(payload.size())
        .arg(QString::fromLatin1(payload.toHex(' ')));
}

QString MonitorController::buildRequestContent(const QString &method, const QString &url) const
{
    QStringList lines;
    lines << QStringLiteral("Method: %1").arg(method);
    lines << QStringLiteral("URL: %1").arg(url);
    lines << QStringLiteral("Headers:");
    lines << QStringLiteral("  Authorization: Bearer %1").arg(maskedApiKey(m_apiKey));
    lines << QStringLiteral("  User-Agent: MoltbookMonitor/0.1");
    lines << QStringLiteral("Body:");
    lines << QStringLiteral("  (none)");
    return lines.join(QLatin1Char('\n'));
}

QString MonitorController::buildResponseContent(int statusCode, const QString &networkError, const QByteArray &payload) const
{
    QStringList lines;
    lines << QStringLiteral("HTTP Status: %1").arg(statusCode > 0 ? QString::number(statusCode) : QStringLiteral("N/A"));
    lines << QStringLiteral("Network Error: %1").arg(networkError.isEmpty() ? QStringLiteral("None") : networkError);
    lines << QStringLiteral("Payload:");
    lines << payloadForLog(payload);
    return lines.join(QLatin1Char('\n'));
}

void MonitorController::appendRequestLog(const QString &agentId,
                                         const QString &method,
                                         const QString &url,
                                         const QString &requestContent,
                                         const QString &responseContent,
                                         int statusCode,
                                         bool ok,
                                         const QString &networkError,
                                         const QDateTime &startedUtc)
{
    QVariantMap row;
    row.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    row.insert(QStringLiteral("timestamp"), formatShanghai(startedUtc));
    row.insert(QStringLiteral("agentId"), agentId);
    row.insert(QStringLiteral("method"), method);
    row.insert(QStringLiteral("url"), url);
    row.insert(QStringLiteral("requestContent"), requestContent);
    row.insert(QStringLiteral("responseContent"), responseContent);
    row.insert(QStringLiteral("statusCode"), statusCode > 0 ? statusCode : QVariant());
    row.insert(QStringLiteral("ok"), ok);
    row.insert(QStringLiteral("networkError"), networkError);

    m_requestLogs.prepend(row);
    if (m_requestLogs.size() > kMaxRequestLogs) {
        m_requestLogs.remove(kMaxRequestLogs, m_requestLogs.size() - kMaxRequestLogs);
    }
    emit requestLogsChanged();
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
                const QString message = tr("Post inactivity alert: Agent %1 | Owner %2 exceeded %3 minutes.")
                                            .arg(entry.agentId, entry.ownerId, QString::number(entry.postThresholdMinutes));
                emit notificationRaised(message);
                entry.postAlertSent = true;
            } else if (postRemaining >= 0) {
                entry.postAlertSent = false;
            }
        }

        if (replyRemaining != kMissingTimestamp) {
            if (replyRemaining < 0 && !entry.replyAlertSent) {
                const QString message = tr("Reply inactivity alert: Agent %1 | Owner %2 exceeded %3 minutes.")
                                            .arg(entry.agentId, entry.ownerId, QString::number(entry.replyThresholdMinutes));
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

void MonitorController::loadState()
{
    QFile file(stateFilePath());
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setStatusMessage(tr("Cannot load monitor state file."));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        setStatusMessage(tr("Saved monitor state is invalid."));
        return;
    }

    const QJsonObject root = doc.object();
    const QString loadedApiKey = root.value(QStringLiteral("api_key")).toString().trimmed();
    if (!loadedApiKey.isEmpty()) {
        m_apiKey = loadedApiKey;
        emit apiKeyChanged();
    }

    QVector<AgentEntry> loadedAgents;
    const QJsonArray agentsArray = root.value(QStringLiteral("agents")).toArray();
    loadedAgents.reserve(agentsArray.size());

    for (const QJsonValue &agentValue : agentsArray) {
        if (!agentValue.isObject()) {
            continue;
        }
        const QJsonObject agentObj = agentValue.toObject();
        const QString agentId = agentObj.value(QStringLiteral("agent_id")).toString().trimmed();
        if (agentId.isEmpty()) {
            continue;
        }

        AgentEntry entry;
        entry.agentId = agentId;
        entry.ownerId = agentObj.value(QStringLiteral("owner_id")).toString().trimmed();
        if (entry.ownerId.isEmpty()) {
            entry.ownerId = QStringLiteral("unknown");
        }

        entry.postThresholdMinutes = std::max(1, agentObj.value(QStringLiteral("post_threshold_minutes")).toInt(60));
        entry.replyThresholdMinutes = std::max(1, agentObj.value(QStringLiteral("reply_threshold_minutes")).toInt(60));

        entry.lastPostUtc = parseIsoDate(agentObj.value(QStringLiteral("last_post_utc")).toString());
        entry.lastReplyUtc = parseIsoDate(agentObj.value(QStringLiteral("last_reply_utc")).toString());
        entry.lastRefreshUtc = parseIsoDate(agentObj.value(QStringLiteral("last_refresh_utc")).toString());
        entry.lastSyncError = agentObj.value(QStringLiteral("last_sync_error")).toString();
        entry.postAlertSent = agentObj.value(QStringLiteral("post_alert_sent")).toBool(false);
        entry.replyAlertSent = agentObj.value(QStringLiteral("reply_alert_sent")).toBool(false);

        const QJsonArray historyArray = agentObj.value(QStringLiteral("history")).toArray();
        entry.history.reserve(historyArray.size());
        for (const QJsonValue &historyValue : historyArray) {
            if (!historyValue.isObject()) {
                continue;
            }
            const QJsonObject historyObj = historyValue.toObject();
            const QDateTime timestampUtc = parseIsoDate(historyObj.value(QStringLiteral("timestamp_utc")).toString());
            if (!timestampUtc.isValid()) {
                continue;
            }

            OperationEntry op;
            op.type = historyObj.value(QStringLiteral("type")).toString();
            if (op.type.isEmpty()) {
                op.type = QStringLiteral("Unknown");
            }
            op.detail = historyObj.value(QStringLiteral("detail")).toString();
            if (op.detail.isEmpty()) {
                op.detail = QStringLiteral("(no content)");
            }
            op.timestampUtc = timestampUtc;
            entry.history.push_back(std::move(op));
        }

        std::sort(entry.history.begin(), entry.history.end(), [](const OperationEntry &left, const OperationEntry &right) {
            return left.timestampUtc > right.timestampUtc;
        });
        if (entry.history.size() > kMaxHistoryEntries) {
            entry.history.resize(kMaxHistoryEntries);
        }

        loadedAgents.push_back(std::move(entry));
    }

    if (!loadedAgents.isEmpty()) {
        beginResetModel();
        m_agents = std::move(loadedAgents);
        endResetModel();
        setStatusMessage(tr("Loaded %1 monitored agents from local state.").arg(m_agents.size()));
        tickCountdowns();
    }

    if (!m_apiKey.isEmpty() && !m_agents.isEmpty()) {
        refreshAll();
    }
}

void MonitorController::saveState() const
{
    const QString path = stateFilePath();
    const QFileInfo fileInfo(path);
    QDir dir;
    if (!dir.mkpath(fileInfo.absolutePath())) {
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kStateVersion);
    root.insert(QStringLiteral("api_key"), m_apiKey);
    root.insert(QStringLiteral("saved_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    QJsonArray agentsArray;
    for (const AgentEntry &entry : m_agents) {
        QJsonObject agentObj;
        agentObj.insert(QStringLiteral("agent_id"), entry.agentId);
        agentObj.insert(QStringLiteral("owner_id"), entry.ownerId);
        agentObj.insert(QStringLiteral("post_threshold_minutes"), entry.postThresholdMinutes);
        agentObj.insert(QStringLiteral("reply_threshold_minutes"), entry.replyThresholdMinutes);
        agentObj.insert(QStringLiteral("last_post_utc"), entry.lastPostUtc.isValid() ? entry.lastPostUtc.toString(Qt::ISODateWithMs) : QString());
        agentObj.insert(QStringLiteral("last_reply_utc"), entry.lastReplyUtc.isValid() ? entry.lastReplyUtc.toString(Qt::ISODateWithMs) : QString());
        agentObj.insert(QStringLiteral("last_refresh_utc"), entry.lastRefreshUtc.isValid() ? entry.lastRefreshUtc.toString(Qt::ISODateWithMs) : QString());
        agentObj.insert(QStringLiteral("last_sync_error"), entry.lastSyncError);
        agentObj.insert(QStringLiteral("post_alert_sent"), entry.postAlertSent);
        agentObj.insert(QStringLiteral("reply_alert_sent"), entry.replyAlertSent);

        QJsonArray historyArray;
        for (const OperationEntry &op : entry.history) {
            QJsonObject opObj;
            opObj.insert(QStringLiteral("type"), op.type);
            opObj.insert(QStringLiteral("detail"), op.detail);
            opObj.insert(QStringLiteral("timestamp_utc"), op.timestampUtc.toString(Qt::ISODateWithMs));
            historyArray.push_back(opObj);
        }
        agentObj.insert(QStringLiteral("history"), historyArray);

        agentsArray.push_back(agentObj);
    }
    root.insert(QStringLiteral("agents"), agentsArray);

    const QJsonDocument doc(root);
    QSaveFile saveFile(path);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        return;
    }
    saveFile.write(doc.toJson(QJsonDocument::Indented));
    saveFile.commit();
}

QString MonitorController::stateFilePath() const
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QDir::home().filePath(QStringLiteral(".moltbook-monitor"));
    }
    return QDir(basePath).filePath(QStringLiteral("monitor_state.json"));
}

void MonitorController::changePendingRequests(int delta)
{
    const bool wasBusy = busy();
    m_pendingRequests = std::max(0, m_pendingRequests + delta);
    if (wasBusy != busy()) {
        emit busyChanged();
    }
}
