#include "monitorcontroller.h"

#include <algorithm>

void MonitorController::addAgent(const QString &agentId,
                                 int postThresholdMinutes,
                                 int replyThresholdMinutes,
                                 const QString &humanOwnerName)
{
    const QString requestedId = agentId.trimmed();
    const QString requestedHumanOwner = humanOwnerName.trimmed();
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

    requestProfile(requestedId, [this, requestedId, requestedHumanOwner, postThresholdMinutes, replyThresholdMinutes, normalized](ProfileSnapshot snapshot) {
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
        entry.humanOwnerName = requestedHumanOwner;
        entry.postThresholdMinutes = postThresholdMinutes;
        entry.replyThresholdMinutes = replyThresholdMinutes;
        entry.lastPostUtc = snapshot.lastPostUtc;
        entry.lastReplyUtc = snapshot.lastReplyUtc;
        entry.history = snapshot.operations;
        entry.lastRefreshUtc = QDateTime::currentDateTimeUtc();

        const QString finalAgentId = entry.agentId;
        const QString ownerDisplay = displayHumanOwner(entry.humanOwnerName);
        insertAgentSorted(std::move(entry));

        setStatusMessage(tr("Added agent %1 (human owner: %2).").arg(finalAgentId, ownerDisplay));
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

    updateAgentConfig(row, postThresholdMinutes, replyThresholdMinutes, m_agents.at(row).humanOwnerName);
}

void MonitorController::updateAgentConfig(int row,
                                          int postThresholdMinutes,
                                          int replyThresholdMinutes,
                                          const QString &humanOwnerName)
{
    if (row < 0 || row >= m_agents.size()) {
        return;
    }
    if (postThresholdMinutes < 1 || replyThresholdMinutes < 1) {
        setStatusMessage(tr("Thresholds must be at least 1 minute."));
        return;
    }

    AgentEntry &entry = m_agents[row];
    const QString agentIdForStatus = entry.agentId;
    const QString previousOwner = entry.humanOwnerName;
    const QString updatedOwner = humanOwnerName.trimmed();
    const bool ownerChanged = previousOwner != updatedOwner;

    entry.postThresholdMinutes = postThresholdMinutes;
    entry.replyThresholdMinutes = replyThresholdMinutes;
    entry.humanOwnerName = updatedOwner;

    if (remainingSeconds(entry.lastPostUtc, entry.postThresholdMinutes) >= 0) {
        entry.postAlertSent = false;
    }
    if (remainingSeconds(entry.lastReplyUtc, entry.replyThresholdMinutes) >= 0) {
        entry.replyAlertSent = false;
    }

    if (ownerChanged) {
        sortAgentsForGrouping();
    } else {
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
                                                HumanOwnerNameRole,
                                                HumanOwnerGroupRole,
                                            });
    }

    const QString ownerDisplay = displayHumanOwner(updatedOwner);
    if (!m_batchImportInProgress) {
        setStatusMessage(tr("Updated %1 (human owner: %2).").arg(agentIdForStatus, ownerDisplay));
        scheduleSaveState();
    }
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

int MonitorController::insertionRowForAgent(const AgentEntry &entry) const
{
    for (int row = 0; row < m_agents.size(); ++row) {
        if (lessByOwnerGrouping(entry, m_agents.at(row))) {
            return row;
        }
    }
    return m_agents.size();
}

void MonitorController::insertAgentSorted(AgentEntry entry)
{
    const int row = insertionRowForAgent(entry);
    beginInsertRows(QModelIndex(), row, row);
    m_agents.insert(row, std::move(entry));
    endInsertRows();
}

void MonitorController::sortAgentsForGrouping()
{
    if (m_agents.size() < 2) {
        return;
    }
    beginResetModel();
    std::sort(m_agents.begin(), m_agents.end(), lessByOwnerGrouping);
    endResetModel();
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
    const QString previousAgentId = entry.agentId;

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

    const bool agentIdChanged = normalizedId(previousAgentId) != normalizedId(entry.agentId);
    if (agentIdChanged) {
        sortAgentsForGrouping();
    } else {
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
    }
    if (!m_batchImportInProgress) {
        scheduleSaveState();
    }
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
