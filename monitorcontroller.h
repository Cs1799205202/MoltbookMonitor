#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTimeZone>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <functional>

class MonitorController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList requestLogs READ requestLogs NOTIFY requestLogsChanged)

public:
    explicit MonitorController(QObject *parent = nullptr);

    enum Roles {
        AgentIdRole = Qt::UserRole + 1,
        OwnerIdRole,
        PostThresholdMinutesRole,
        ReplyThresholdMinutesRole,
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
        LastRefreshTimeRole
    };
    Q_ENUM(Roles)

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString apiKey() const;
    QString statusMessage() const;
    bool busy() const;
    QVariantList requestLogs() const;

    Q_INVOKABLE void setApiKey(const QString &apiKey);
    Q_INVOKABLE void addAgent(const QString &agentId, int postThresholdMinutes, int replyThresholdMinutes);
    Q_INVOKABLE void removeAgent(int row);
    Q_INVOKABLE void updateThresholds(int row, int postThresholdMinutes, int replyThresholdMinutes);
    Q_INVOKABLE void refreshAgent(int row);
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE QString currentShanghaiTimeString() const;
    Q_INVOKABLE void clearRequestLogs();

signals:
    void apiKeyChanged();
    void statusMessageChanged();
    void busyChanged();
    void requestLogsChanged();
    void notificationRaised(const QString &message);

private:
    struct OperationEntry {
        QString type;
        QString detail;
        QDateTime timestampUtc;
    };

    struct AgentEntry {
        QString agentId;
        QString ownerId;
        int postThresholdMinutes = 60;
        int replyThresholdMinutes = 60;
        QDateTime lastPostUtc;
        QDateTime lastReplyUtc;
        QVector<OperationEntry> history;
        bool postAlertSent = false;
        bool replyAlertSent = false;
        QString lastSyncError;
        QDateTime lastRefreshUtc;
    };

    struct ProfileSnapshot {
        bool ok = false;
        QString error;
        QString agentId;
        QString ownerId;
        QDateTime lastPostUtc;
        QDateTime lastReplyUtc;
        QVector<OperationEntry> operations;
    };

    void requestProfile(const QString &agentId, std::function<void(ProfileSnapshot)> callback);
    ProfileSnapshot parseProfileResponse(const QByteArray &payload) const;
    static QDateTime parseIsoDate(const QString &value);
    static QString normalizedId(const QString &agentId);
    static QString summarize(const QString &text, int maxLen = 90);
    static QString operationKey(const OperationEntry &entry);
    static QString maskedApiKey(const QString &apiKey);

    void setStatusMessage(const QString &message);
    int findAgentRow(const QString &agentId) const;
    void applySnapshotToAgent(int row, const ProfileSnapshot &snapshot);
    QVariantList buildHistoryVariant(const QVector<OperationEntry> &history) const;
    QString payloadForLog(const QByteArray &payload) const;
    QString buildRequestContent(const QString &method, const QString &url) const;
    QString buildResponseContent(int statusCode, const QString &networkError, const QByteArray &payload) const;
    void appendRequestLog(const QString &agentId,
                          const QString &method,
                          const QString &url,
                          const QString &requestContent,
                          const QString &responseContent,
                          int statusCode,
                          bool ok,
                          const QString &networkError,
                          const QDateTime &startedUtc);
    qint64 remainingSeconds(const QDateTime &lastUtc, int thresholdMinutes) const;
    QString formatRemaining(qint64 seconds) const;
    QString formatShanghai(const QDateTime &utc) const;
    void tickCountdowns();
    void changePendingRequests(int delta);
    void scheduleSaveState();
    void loadState();
    void saveState() const;
    QString stateFilePath() const;

    QNetworkAccessManager m_network;
    QTimer m_refreshTimer;
    QTimer m_countdownTimer;
    QTimer m_saveDebounceTimer;
    QVector<AgentEntry> m_agents;
    QTimeZone m_shanghaiZone;
    QString m_apiKey;
    QString m_statusMessage;
    QVariantList m_requestLogs;
    int m_pendingRequests = 0;
    QSet<QString> m_pendingAdds;
};
