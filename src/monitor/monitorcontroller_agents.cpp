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
        entry.totalPostsCount = snapshot.totalPostsCount;
        entry.totalCommentsCount = snapshot.totalCommentsCount;
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

void MonitorController::scheduleConsistencyRefresh(const QString &agentId, int delayMs)
{
    const int row = findAgentRow(agentId);
    if (row < 0) {
        return;
    }

    AgentEntry &entry = m_agents[row];
    if (entry.consistencyRefreshPending) {
        return;
    }

    entry.consistencyRefreshPending = true;
    const QString trackedAgentId = entry.agentId;
    const int resolvedDelayMs = std::max(1000, delayMs);

    QTimer::singleShot(resolvedDelayMs, this, [this, trackedAgentId]() {
        const int refreshRow = findAgentRow(trackedAgentId);
        if (refreshRow < 0) {
            return;
        }

        AgentEntry &refreshEntry = m_agents[refreshRow];
        if (!refreshEntry.consistencyRefreshPending) {
            return;
        }

        refreshEntry.consistencyRefreshPending = false;
        refreshAgent(refreshRow);
    });
}

void MonitorController::applySnapshotToAgent(int row, const ProfileSnapshot &snapshot)
{
    if (row < 0 || row >= m_agents.size()) {
        return;
    }

    AgentEntry &entry = m_agents[row];
    const QString previousAgentId = entry.agentId;
    const int previousPostCount = entry.totalPostsCount;
    const int previousCommentCount = entry.totalCommentsCount;
    const QDateTime observedAtUtc = QDateTime::currentDateTimeUtc();
    const QString ownerDisplay = displayHumanOwner(entry.humanOwnerName);

    const bool newerPostSeen = snapshot.lastPostUtc.isValid() && (!entry.lastPostUtc.isValid() || snapshot.lastPostUtc > entry.lastPostUtc);
    const bool newerReplySeen = snapshot.lastReplyUtc.isValid() && (!entry.lastReplyUtc.isValid() || snapshot.lastReplyUtc > entry.lastReplyUtc);
    const bool postCountRegressed = snapshot.totalPostsCount >= 0
        && previousPostCount >= 0
        && snapshot.totalPostsCount < previousPostCount;
    const bool commentCountRegressed = snapshot.totalCommentsCount >= 0
        && previousCommentCount >= 0
        && snapshot.totalCommentsCount < previousCommentCount;
    const int postDelta = (snapshot.totalPostsCount >= 0 && previousPostCount >= 0)
        ? snapshot.totalPostsCount - previousPostCount
        : 0;
    const int replyDelta = (snapshot.totalCommentsCount >= 0 && previousCommentCount >= 0)
        ? snapshot.totalCommentsCount - previousCommentCount
        : 0;
    const bool postCountIncreased = snapshot.totalPostsCount >= 0
        && previousPostCount >= 0
        && snapshot.totalPostsCount > previousPostCount;
    const bool commentCountIncreased = snapshot.totalCommentsCount >= 0
        && previousCommentCount >= 0
        && snapshot.totalCommentsCount > previousCommentCount;
    const bool postAbnormalJump = postDelta >= kAbnormalCountJumpThreshold && !newerPostSeen;
    const bool replyAbnormalJump = replyDelta >= kAbnormalCountJumpThreshold && !newerReplySeen;
    const bool inferredPostSeen = postCountIncreased && !newerPostSeen;
    const bool inferredReplySeen = commentCountIncreased && !newerReplySeen;
    const bool canReconcileInferredPost = entry.postLastSeenInferred
        && snapshot.lastPostUtc.isValid()
        && snapshot.totalPostsCount >= 0
        && previousPostCount >= 0
        && snapshot.totalPostsCount == previousPostCount
        && entry.lastPostUtc.isValid()
        && snapshot.lastPostUtc <= entry.lastPostUtc
        && snapshot.lastPostUtc >= entry.lastPostUtc.addSecs(-30 * 60);
    const bool canReconcileInferredReply = entry.replyLastSeenInferred
        && snapshot.lastReplyUtc.isValid()
        && snapshot.totalCommentsCount >= 0
        && previousCommentCount >= 0
        && snapshot.totalCommentsCount == previousCommentCount
        && entry.lastReplyUtc.isValid()
        && snapshot.lastReplyUtc <= entry.lastReplyUtc
        && snapshot.lastReplyUtc >= entry.lastReplyUtc.addSecs(-30 * 60);

    entry.agentId = snapshot.agentId.isEmpty() ? entry.agentId : snapshot.agentId;
    entry.ownerId = snapshot.ownerId.isEmpty() ? entry.ownerId : snapshot.ownerId;
    if (snapshot.totalPostsCount >= 0 && !postCountRegressed) {
        entry.totalPostsCount = snapshot.totalPostsCount;
        entry.postCountRegressionAlerted = false;
    }
    if (snapshot.totalCommentsCount >= 0 && !commentCountRegressed) {
        entry.totalCommentsCount = snapshot.totalCommentsCount;
        entry.replyCountRegressionAlerted = false;
    }

    bool postConfirmedFromRecent = false;
    bool replyConfirmedFromRecent = false;
    if (snapshot.lastPostUtc.isValid() && newerPostSeen) {
        entry.lastPostUtc = snapshot.lastPostUtc;
        postConfirmedFromRecent = true;
    }
    if (snapshot.lastReplyUtc.isValid() && newerReplySeen) {
        entry.lastReplyUtc = snapshot.lastReplyUtc;
        replyConfirmedFromRecent = true;
    }
    if (canReconcileInferredPost) {
        entry.lastPostUtc = snapshot.lastPostUtc;
        postConfirmedFromRecent = true;
    }
    if (canReconcileInferredReply) {
        entry.lastReplyUtc = snapshot.lastReplyUtc;
        replyConfirmedFromRecent = true;
    }
    if (inferredPostSeen && (!entry.lastPostUtc.isValid() || observedAtUtc > entry.lastPostUtc)) {
        entry.lastPostUtc = observedAtUtc;
    }
    if (inferredReplySeen && (!entry.lastReplyUtc.isValid() || observedAtUtc > entry.lastReplyUtc)) {
        entry.lastReplyUtc = observedAtUtc;
    }
    if (postConfirmedFromRecent) {
        entry.postLastSeenInferred = false;
    } else if (inferredPostSeen) {
        entry.postLastSeenInferred = true;
    }
    if (replyConfirmedFromRecent) {
        entry.replyLastSeenInferred = false;
    } else if (inferredReplySeen) {
        entry.replyLastSeenInferred = true;
    }

    QSet<QString> knownOperationKeys;
    for (const OperationEntry &existing : entry.history) {
        knownOperationKeys.insert(operationKey(existing));
    }

    QVector<OperationEntry> incomingOperations = snapshot.operations;
    auto addDiagnostic = [&incomingOperations, &observedAtUtc](const QString &detail) {
        OperationEntry diag;
        diag.type = QStringLiteral("Diagnostic");
        diag.detail = detail;
        diag.timestampUtc = observedAtUtc;
        incomingOperations.push_back(std::move(diag));
    };

    if (postCountRegressed && !entry.postCountRegressionAlerted) {
        addDiagnostic(tr("posts_count regressed: %1 -> %2. Keeping previous baseline to avoid false inactivity resets.")
                          .arg(previousPostCount)
                          .arg(snapshot.totalPostsCount));
        emit notificationRaised(tr("Count regression alert: Agent %1 | Owner %2 posts_count dropped from %3 to %4. Check API consistency/caching.")
                                    .arg(entry.agentId, ownerDisplay)
                                    .arg(previousPostCount)
                                    .arg(snapshot.totalPostsCount));
        entry.postCountRegressionAlerted = true;
    }
    if (commentCountRegressed && !entry.replyCountRegressionAlerted) {
        addDiagnostic(tr("comments_count regressed: %1 -> %2. Keeping previous baseline to avoid false inactivity resets.")
                          .arg(previousCommentCount)
                          .arg(snapshot.totalCommentsCount));
        emit notificationRaised(tr("Count regression alert: Agent %1 | Owner %2 comments_count dropped from %3 to %4. Check API consistency/caching.")
                                    .arg(entry.agentId, ownerDisplay)
                                    .arg(previousCommentCount)
                                    .arg(snapshot.totalCommentsCount));
        entry.replyCountRegressionAlerted = true;
    }
    if (postAbnormalJump) {
        addDiagnostic(tr("posts_count jumped by %1 in one refresh (%2 -> %3) while recentPosts did not provide a newer timestamp.")
                          .arg(postDelta)
                          .arg(previousPostCount)
                          .arg(snapshot.totalPostsCount));
        emit notificationRaised(tr("Count anomaly alert: Agent %1 | Owner %2 posts_count jumped by %3 without a matching recentPosts timestamp.")
                                    .arg(entry.agentId, ownerDisplay)
                                    .arg(postDelta));
    }
    if (replyAbnormalJump) {
        addDiagnostic(tr("comments_count jumped by %1 in one refresh (%2 -> %3) while recentComments did not provide a newer timestamp.")
                          .arg(replyDelta)
                          .arg(previousCommentCount)
                          .arg(snapshot.totalCommentsCount));
        emit notificationRaised(tr("Count anomaly alert: Agent %1 | Owner %2 comments_count jumped by %3 without a matching recentComments timestamp.")
                                    .arg(entry.agentId, ownerDisplay)
                                    .arg(replyDelta));
    }
    if (inferredPostSeen) {
        OperationEntry inferred;
        inferred.type = QStringLiteral("Post (Inferred)");
        inferred.detail = tr("Inferred from posts_count increase: %1 -> %2; recentPosts had no newer record yet.")
                              .arg(previousPostCount)
                              .arg(snapshot.totalPostsCount);
        inferred.timestampUtc = observedAtUtc;
        incomingOperations.push_back(std::move(inferred));
    }
    if (inferredReplySeen) {
        OperationEntry inferred;
        inferred.type = QStringLiteral("Reply (Inferred)");
        inferred.detail = tr("Inferred from comments_count increase: %1 -> %2; recentComments had no newer record yet.")
                              .arg(previousCommentCount)
                              .arg(snapshot.totalCommentsCount);
        inferred.timestampUtc = observedAtUtc;
        incomingOperations.push_back(std::move(inferred));
    }

    for (const OperationEntry &incoming : incomingOperations) {
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

    if (newerPostSeen || inferredPostSeen) {
        entry.postAlertSent = false;
    }
    if (newerReplySeen || inferredReplySeen) {
        entry.replyAlertSent = false;
    }
    if (postConfirmedFromRecent || replyConfirmedFromRecent) {
        entry.consistencyRefreshPending = false;
    }

    entry.lastSyncError.clear();
    entry.lastRefreshUtc = observedAtUtc;

    if (inferredPostSeen || inferredReplySeen || postCountRegressed || commentCountRegressed) {
        scheduleConsistencyRefresh(entry.agentId);
    }

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
                                                PostActivityInferredRole,
                                                ReplyActivityInferredRole,
                                                PostActivitySourceTextRole,
                                                ReplyActivitySourceTextRole,
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
