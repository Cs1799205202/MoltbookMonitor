#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>
#include <QTimeZone>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <functional>
#include <memory>

class QNetworkReply;

class MonitorController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList requestLogs READ requestLogs NOTIFY requestLogsChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(bool updateCheckInProgress READ updateCheckInProgress NOTIFY updateCheckInProgressChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString ignoredUpdateVersion READ ignoredUpdateVersion NOTIFY ignoredUpdateVersionChanged)
    Q_PROPERTY(bool latestUpdateIgnored READ latestUpdateIgnored NOTIFY latestUpdateIgnoredChanged)
    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateStatusChanged)
    Q_PROPERTY(bool updateDownloadAvailable READ updateDownloadAvailable NOTIFY updateDownloadAvailableChanged)
    Q_PROPERTY(bool updateDownloadInProgress READ updateDownloadInProgress NOTIFY updateDownloadInProgressChanged)
    Q_PROPERTY(double updateDownloadProgress READ updateDownloadProgress NOTIFY updateDownloadProgressChanged)
    Q_PROPERTY(bool updatePackageReady READ updatePackageReady NOTIFY updatePackageReadyChanged)

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
    QString currentVersion() const;
    bool updateCheckInProgress() const;
    bool updateAvailable() const;
    QString latestVersion() const;
    QString ignoredUpdateVersion() const;
    bool latestUpdateIgnored() const;
    QString updateStatus() const;
    bool updateDownloadAvailable() const;
    bool updateDownloadInProgress() const;
    double updateDownloadProgress() const;
    bool updatePackageReady() const;

    Q_INVOKABLE void setApiKey(const QString &apiKey);
    Q_INVOKABLE void addAgent(const QString &agentId, int postThresholdMinutes, int replyThresholdMinutes);
    Q_INVOKABLE void removeAgent(int row);
    Q_INVOKABLE void updateThresholds(int row, int postThresholdMinutes, int replyThresholdMinutes);
    Q_INVOKABLE void refreshAgent(int row);
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE QString currentShanghaiTimeString() const;
    Q_INVOKABLE void clearRequestLogs();
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void ignoreLatestUpdate();
    Q_INVOKABLE void clearIgnoredUpdateVersion();
    Q_INVOKABLE void downloadLatestUpdate();
    Q_INVOKABLE void applyDownloadedUpdate();
    Q_INVOKABLE void openLatestReleasePage();

signals:
    void apiKeyChanged();
    void statusMessageChanged();
    void busyChanged();
    void requestLogsChanged();
    void notificationRaised(const QString &message);
    void updateCheckInProgressChanged();
    void updateAvailableChanged();
    void latestVersionChanged();
    void ignoredUpdateVersionChanged();
    void latestUpdateIgnoredChanged();
    void updateStatusChanged();
    void updateDownloadAvailableChanged();
    void updateDownloadInProgressChanged();
    void updateDownloadProgressChanged();
    void updatePackageReadyChanged();

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
    static QString normalizedVersionTag(const QString &tag);
    static int compareVersionStrings(const QString &left, const QString &right);
    void checkForUpdatesInternal(bool userInitiated);

    void setStatusMessage(const QString &message);
    void setUpdateStatus(const QString &message);
    void setUpdateCheckInProgress(bool value);
    void setUpdateAvailable(bool value);
    void setLatestVersion(const QString &value);
    void setIgnoredUpdateVersion(const QString &value);
    void setUpdateDownloadAvailable(bool value);
    void setUpdateDownloadInProgress(bool value);
    void setUpdateDownloadProgress(double value);
    void setUpdatePackageReady(bool value);
    QString preferredUpdateAssetSuffix() const;
    QString updateDownloadPathForAsset(const QString &assetName) const;
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
    QPointer<QNetworkReply> m_updateCheckReply;
    QPointer<QNetworkReply> m_updateDownloadReply;
    std::unique_ptr<QFile> m_updateDownloadFile;
    QString m_latestVersion;
    QString m_ignoredUpdateVersion;
    QString m_latestReleaseUrl;
    QString m_latestAssetUrl;
    QString m_latestAssetName;
    QString m_updateStatus = QStringLiteral("No update check yet.");
    QString m_downloadedUpdatePath;
    bool m_updateCheckInProgress = false;
    bool m_updateAvailable = false;
    bool m_updateDownloadAvailable = false;
    bool m_updateDownloadInProgress = false;
    double m_updateDownloadProgress = 0.0;
    bool m_updatePackageReady = false;
};
