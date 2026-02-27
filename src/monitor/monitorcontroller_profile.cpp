#include "monitorcontroller.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

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
    const QString userAgent = QStringLiteral("MoltbookMonitor/%1").arg(currentVersion());
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
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

QString MonitorController::normalizedHumanOwner(const QString &humanOwnerName)
{
    return humanOwnerName.simplified().toLower();
}

QString MonitorController::displayHumanOwner(const QString &humanOwnerName)
{
    const QString trimmed = humanOwnerName.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("Unassigned") : trimmed;
}

bool MonitorController::lessByOwnerGrouping(const AgentEntry &left, const AgentEntry &right)
{
    const QString leftOwner = normalizedHumanOwner(left.humanOwnerName);
    const QString rightOwner = normalizedHumanOwner(right.humanOwnerName);
    const bool leftAssigned = !leftOwner.isEmpty();
    const bool rightAssigned = !rightOwner.isEmpty();

    if (leftAssigned != rightAssigned) {
        return leftAssigned;
    }
    if (leftOwner != rightOwner) {
        return leftOwner < rightOwner;
    }

    const QString leftId = normalizedId(left.agentId);
    const QString rightId = normalizedId(right.agentId);
    if (leftId != rightId) {
        return leftId < rightId;
    }

    return left.agentId < right.agentId;
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
    lines << QStringLiteral("  User-Agent: MoltbookMonitor/%1").arg(currentVersion());
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
