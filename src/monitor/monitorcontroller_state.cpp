#include "monitorcontroller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

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

    const QString loadedIgnoredVersion = normalizedVersionTag(root.value(QStringLiteral("ignored_update_version")).toString());
    if (!loadedIgnoredVersion.isEmpty()) {
        m_ignoredUpdateVersion = loadedIgnoredVersion;
        emit ignoredUpdateVersionChanged();
        emit latestUpdateIgnoredChanged();
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
    root.insert(QStringLiteral("ignored_update_version"), m_ignoredUpdateVersion);
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
