#include "monitorcontroller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <QVersionNumber>
#include <algorithm>

void MonitorController::checkForUpdates()
{
    checkForUpdatesInternal(true);
}

void MonitorController::ignoreLatestUpdate()
{
    if (m_latestVersion.isEmpty()) {
        setUpdateStatus(tr("No latest version metadata is available yet."));
        return;
    }
    if (compareVersionStrings(m_latestVersion, currentVersion()) <= 0) {
        setUpdateStatus(tr("Current version is already up to date."));
        return;
    }

    setIgnoredUpdateVersion(m_latestVersion);
    setUpdateAvailable(false);
    setUpdateDownloadAvailable(false);
    m_downloadedUpdatePath.clear();
    setUpdatePackageReady(false);
    setUpdateDownloadProgress(0.0);
    scheduleSaveState();
    setUpdateStatus(tr("Ignored update %1.").arg(m_latestVersion));
}

void MonitorController::clearIgnoredUpdateVersion()
{
    if (m_ignoredUpdateVersion.isEmpty()) {
        return;
    }

    const QString previous = m_ignoredUpdateVersion;
    setIgnoredUpdateVersion(QString());
    scheduleSaveState();

    if (!m_latestVersion.isEmpty() && compareVersionStrings(m_latestVersion, currentVersion()) > 0) {
        setUpdateAvailable(true);
        setUpdateDownloadAvailable(!m_latestAssetUrl.isEmpty());
    }

    setUpdateStatus(tr("Cleared ignored update version %1.").arg(previous));
}

void MonitorController::checkForUpdatesInternal(bool userInitiated)
{
    if (m_updateCheckReply) {
        if (userInitiated) {
            setUpdateStatus(tr("Update check is already running."));
        }
        return;
    }

    QNetworkRequest request{QUrl(QString::fromLatin1(kLatestReleaseApiUrl))};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("MoltbookMonitor/%1").arg(currentVersion()));
    request.setRawHeader("Accept", QByteArrayLiteral("application/vnd.github+json"));
    request.setRawHeader("X-GitHub-Api-Version", QByteArrayLiteral("2022-11-28"));

    setUpdateCheckInProgress(true);
    if (userInitiated) {
        setUpdateStatus(tr("Checking for updates..."));
    }
    setUpdateDownloadAvailable(false);

    changePendingRequests(+1);
    QNetworkReply *reply = m_network.get(request);
    m_updateCheckReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, userInitiated]() {
        const auto finalize = [this, reply]() {
            m_updateCheckReply = nullptr;
            setUpdateCheckInProgress(false);
            changePendingRequests(-1);
            reply->deleteLater();
        };

        const QByteArray payload = reply->readAll();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkError = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();

        if (reply->error() != QNetworkReply::NoError) {
            if (userInitiated) {
                setUpdateStatus(tr("Update check failed: %1").arg(networkError));
            }
            finalize();
            return;
        }
        if (statusCode >= 400) {
            if (userInitiated) {
                setUpdateStatus(tr("Update check failed (HTTP %1).").arg(statusCode));
            }
            finalize();
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            if (userInitiated) {
                setUpdateStatus(tr("Update check failed: invalid release metadata."));
            }
            finalize();
            return;
        }

        const QJsonObject root = doc.object();
        const QString latestTag = normalizedVersionTag(root.value(QStringLiteral("tag_name")).toString());
        if (latestTag.isEmpty()) {
            if (userInitiated) {
                setUpdateStatus(tr("Update check failed: missing latest version tag."));
            }
            finalize();
            return;
        }

        const QString releaseUrl = root.value(QStringLiteral("html_url")).toString().trimmed();
        m_latestReleaseUrl = releaseUrl.isEmpty() ? QString::fromLatin1(kReleasesPageUrl) : releaseUrl;
        setLatestVersion(latestTag);

        const QString preferredSuffix = preferredUpdateAssetSuffix();
        QString matchedAssetUrl;
        QString matchedAssetName;

        const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &assetValue : assets) {
            if (!assetValue.isObject()) {
                continue;
            }
            const QJsonObject asset = assetValue.toObject();
            const QString assetName = asset.value(QStringLiteral("name")).toString().trimmed();
            const QString assetUrl = asset.value(QStringLiteral("browser_download_url")).toString().trimmed();
            if (assetName.isEmpty() || assetUrl.isEmpty()) {
                continue;
            }

            if (!preferredSuffix.isEmpty() && assetName.endsWith(preferredSuffix, Qt::CaseInsensitive)) {
                matchedAssetName = assetName;
                matchedAssetUrl = assetUrl;
                break;
            }
        }

        if (matchedAssetUrl.isEmpty()) {
            for (const QJsonValue &assetValue : assets) {
                if (!assetValue.isObject()) {
                    continue;
                }
                const QJsonObject asset = assetValue.toObject();
                const QString assetName = asset.value(QStringLiteral("name")).toString().trimmed();
                const QString assetUrl = asset.value(QStringLiteral("browser_download_url")).toString().trimmed();
                if (assetName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) && !assetUrl.isEmpty()) {
                    matchedAssetName = assetName;
                    matchedAssetUrl = assetUrl;
                    break;
                }
            }
        }

        m_latestAssetName = matchedAssetName;
        m_latestAssetUrl = matchedAssetUrl;

        const int cmp = compareVersionStrings(latestTag, currentVersion());
        const bool ignored = !m_ignoredUpdateVersion.isEmpty()
            && compareVersionStrings(latestTag, m_ignoredUpdateVersion) == 0;

        if (cmp > 0) {
            setUpdateAvailable(!ignored);
            setUpdateDownloadAvailable(!ignored && !m_latestAssetUrl.isEmpty());
            m_downloadedUpdatePath.clear();
            setUpdatePackageReady(false);
            setUpdateDownloadProgress(0.0);

            if (ignored) {
                if (userInitiated) {
                    setUpdateStatus(tr("Update %1 is available but currently ignored.").arg(latestTag));
                }
            } else if (m_latestAssetUrl.isEmpty()) {
                setUpdateStatus(tr("Update %1 is available, but no downloadable package was found.").arg(latestTag));
            } else {
                setUpdateStatus(tr("Update %1 is available.").arg(latestTag));
                if (!userInitiated) {
                    emit notificationRaised(tr("New version %1 is available. Click Check Update to download.").arg(latestTag));
                }
            }
        } else if (cmp < 0) {
            setUpdateAvailable(false);
            setUpdateDownloadAvailable(false);
            m_downloadedUpdatePath.clear();
            setUpdatePackageReady(false);
            setUpdateDownloadProgress(0.0);
            if (userInitiated) {
                setUpdateStatus(tr("Current build (%1) is newer than latest release (%2).").arg(currentVersion(), latestTag));
            }
        } else {
            setUpdateAvailable(false);
            setUpdateDownloadAvailable(false);
            m_downloadedUpdatePath.clear();
            setUpdatePackageReady(false);
            setUpdateDownloadProgress(0.0);
            if (userInitiated) {
                setUpdateStatus(tr("You are up to date (%1).").arg(currentVersion()));
            }
        }

        finalize();
    });
}

void MonitorController::downloadLatestUpdate()
{
    if (m_updateDownloadReply) {
        setUpdateStatus(tr("Update package download is already running."));
        return;
    }
    if (!m_updateAvailable) {
        setUpdateStatus(tr("No newer update is currently available."));
        return;
    }
    if (m_latestAssetUrl.isEmpty() || m_latestAssetName.isEmpty()) {
        setUpdateStatus(tr("No compatible update package is available for this platform."));
        return;
    }

    const QString targetPath = updateDownloadPathForAsset(m_latestAssetName);
    if (targetPath.isEmpty()) {
        setUpdateStatus(tr("Cannot resolve update package download location."));
        return;
    }

    std::unique_ptr<QFile> file = std::make_unique<QFile>(targetPath);
    if (file->exists() && !file->remove()) {
        setUpdateStatus(tr("Cannot replace existing file: %1").arg(targetPath));
        return;
    }
    if (!file->open(QIODevice::WriteOnly)) {
        setUpdateStatus(tr("Cannot write update package: %1").arg(targetPath));
        return;
    }

    m_updateDownloadFile = std::move(file);
    m_downloadedUpdatePath.clear();
    setUpdatePackageReady(false);
    setUpdateDownloadProgress(0.0);
    setUpdateDownloadInProgress(true);
    setUpdateStatus(tr("Downloading update package..."));

    QNetworkRequest request{QUrl(m_latestAssetUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("MoltbookMonitor/%1").arg(currentVersion()));
    request.setRawHeader("Accept", QByteArrayLiteral("application/octet-stream"));

    changePendingRequests(+1);
    QNetworkReply *reply = m_network.get(request);
    m_updateDownloadReply = reply;

    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            setUpdateDownloadProgress(static_cast<double>(received) / static_cast<double>(total));
        }
    });

    connect(reply, &QIODevice::readyRead, this, [this, reply]() {
        if (!m_updateDownloadFile) {
            return;
        }
        m_updateDownloadFile->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, targetPath]() {
        const auto finalize = [this, reply]() {
            m_updateDownloadReply = nullptr;
            m_updateDownloadFile.reset();
            setUpdateDownloadInProgress(false);
            changePendingRequests(-1);
            reply->deleteLater();
        };

        if (!m_updateDownloadFile) {
            setUpdateStatus(tr("Update download failed: local file stream is unavailable."));
            finalize();
            return;
        }

        m_updateDownloadFile->write(reply->readAll());
        m_updateDownloadFile->flush();
        m_updateDownloadFile->close();

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkError = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        const bool fileWriteOk = (m_updateDownloadFile->error() == QFile::NoError);

        if (reply->error() != QNetworkReply::NoError || statusCode >= 400 || !fileWriteOk) {
            QFile::remove(targetPath);
            setUpdatePackageReady(false);
            setUpdateDownloadProgress(0.0);
            if (!networkError.isEmpty()) {
                setUpdateStatus(tr("Update download failed: %1").arg(networkError));
            } else if (!fileWriteOk) {
                setUpdateStatus(tr("Update download failed: cannot write package file."));
            } else {
                setUpdateStatus(tr("Update download failed (HTTP %1).").arg(statusCode));
            }
            finalize();
            return;
        }

        m_downloadedUpdatePath = targetPath;
        setUpdatePackageReady(true);
        setUpdateDownloadProgress(1.0);
        setUpdateStatus(tr("Update package downloaded: %1").arg(QFileInfo(targetPath).fileName()));
        finalize();
    });
}

void MonitorController::applyDownloadedUpdate()
{
    if (m_downloadedUpdatePath.isEmpty()) {
        setUpdateStatus(tr("No downloaded update package is available yet."));
        return;
    }

    const QFileInfo packageInfo(m_downloadedUpdatePath);
    if (!packageInfo.exists() || !packageInfo.isFile()) {
        m_downloadedUpdatePath.clear();
        setUpdatePackageReady(false);
        setUpdateStatus(tr("Downloaded update package was not found. Please download again."));
        return;
    }

    if (QDesktopServices::openUrl(QUrl::fromLocalFile(m_downloadedUpdatePath))) {
        setUpdateStatus(tr("Opened update package. Extract and replace the app to finish updating."));
        return;
    }

    if (QDesktopServices::openUrl(QUrl::fromLocalFile(packageInfo.absolutePath()))) {
        setUpdateStatus(tr("Cannot open package directly. Opened containing folder instead."));
        return;
    }

    setUpdateStatus(tr("Cannot open downloaded update package."));
}

void MonitorController::openLatestReleasePage()
{
    const QString url = m_latestReleaseUrl.isEmpty() ? QString::fromLatin1(kReleasesPageUrl) : m_latestReleaseUrl;
    if (QDesktopServices::openUrl(QUrl(url))) {
        setUpdateStatus(tr("Opened release page."));
        return;
    }
    setUpdateStatus(tr("Cannot open release page."));
}

QString MonitorController::normalizedVersionTag(const QString &tag)
{
    QString normalized = tag.trimmed();
    if (normalized.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        normalized.remove(0, 1);
    }
    return normalized;
}

int MonitorController::compareVersionStrings(const QString &left, const QString &right)
{
    const QString leftNormalized = normalizedVersionTag(left);
    const QString rightNormalized = normalizedVersionTag(right);

    int leftSuffixPos = -1;
    int rightSuffixPos = -1;
    const QVersionNumber leftVersion = QVersionNumber::fromString(leftNormalized, &leftSuffixPos);
    const QVersionNumber rightVersion = QVersionNumber::fromString(rightNormalized, &rightSuffixPos);

    if (!leftVersion.isNull() && !rightVersion.isNull()) {
        const int cmp = QVersionNumber::compare(leftVersion, rightVersion);
        if (cmp != 0) {
            return cmp;
        }

        const bool leftHasSuffix = leftSuffixPos >= 0 && leftSuffixPos < leftNormalized.size();
        const bool rightHasSuffix = rightSuffixPos >= 0 && rightSuffixPos < rightNormalized.size();
        if (leftHasSuffix != rightHasSuffix) {
            return leftHasSuffix ? -1 : 1;
        }
        return 0;
    }

    return QString::compare(leftNormalized, rightNormalized, Qt::CaseInsensitive);
}

QString MonitorController::preferredUpdateAssetSuffix() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows-x64.zip");
#elif defined(Q_OS_MACOS)
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    if (arch.contains(QStringLiteral("arm")) || arch.contains(QStringLiteral("aarch64"))) {
        return QStringLiteral("macos-arm64.zip");
    }
    return QStringLiteral("macos-x64.zip");
#else
    return QString();
#endif
}

QString MonitorController::updateDownloadPathForAsset(const QString &assetName) const
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (baseDir.isEmpty()) {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (baseDir.isEmpty()) {
        baseDir = QDir::homePath();
    }

    QDir dir(baseDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return QString();
    }

    QString safeAssetName = assetName.trimmed();
    safeAssetName.replace(QLatin1Char('/'), QLatin1Char('_'));
    safeAssetName.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (safeAssetName.isEmpty()) {
        safeAssetName = QStringLiteral("MoltbookMonitor-update.zip");
    }

    return dir.filePath(safeAssetName);
}
