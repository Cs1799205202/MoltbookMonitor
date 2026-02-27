#include "monitorcontroller.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <memory>

namespace {
struct ImportSeed {
    QString agentId;
    int postThresholdMinutes = 0;
    int replyThresholdMinutes = 0;
    QString humanOwnerName;
};

struct ImportStats {
    int added = 0;
    int updated = 0;
    int duplicate = 0;
    int invalid = 0;
    int verifyFailed = 0;
};

QString normalizeHeader(const QString &value)
{
    QString normalized = value.trimmed().toLower();
    normalized.remove(QLatin1Char(' '));
    normalized.remove(QLatin1Char('_'));
    normalized.remove(QLatin1Char('-'));
    normalized.remove(QLatin1Char('('));
    normalized.remove(QLatin1Char(')'));
    return normalized;
}

QString csvEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(','))
        || escaped.contains(QLatin1Char('"'))
        || escaped.contains(QLatin1Char('\n'))
        || escaped.contains(QLatin1Char('\r'))) {
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return escaped;
}

QString csvLine(const QStringList &fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const QString &field : fields) {
        escaped.push_back(csvEscape(field));
    }
    return escaped.join(QLatin1Char(','));
}

bool isEmptyRow(const QStringList &row)
{
    for (const QString &cell : row) {
        if (!cell.trimmed().isEmpty()) {
            return false;
        }
    }
    return true;
}

int findHeaderIndex(const QStringList &header, const QSet<QString> &aliases)
{
    for (int i = 0; i < header.size(); ++i) {
        if (aliases.contains(normalizeHeader(header.at(i)))) {
            return i;
        }
    }
    return -1;
}

QString cellValue(const QStringList &row, int index)
{
    if (index < 0 || index >= row.size()) {
        return QString();
    }
    return row.at(index).trimmed();
}

bool parseCsvRows(const QString &text, QVector<QStringList> &rows, QString &error)
{
    rows.clear();

    QString field;
    QStringList row;
    bool inQuotes = false;

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);

        if (inQuotes) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += ch;
            }
            continue;
        }

        if (ch == QLatin1Char('"')) {
            inQuotes = true;
            continue;
        }
        if (ch == QLatin1Char(',')) {
            row.push_back(field);
            field.clear();
            continue;
        }
        if (ch == QLatin1Char('\r')) {
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            row.push_back(field);
            field.clear();
            rows.push_back(row);
            row.clear();
            continue;
        }
        if (ch == QLatin1Char('\n')) {
            row.push_back(field);
            field.clear();
            rows.push_back(row);
            row.clear();
            continue;
        }

        field += ch;
    }

    if (inQuotes) {
        error = QStringLiteral("CSV format error: unterminated quoted field.");
        return false;
    }

    row.push_back(field);
    rows.push_back(row);

    while (!rows.isEmpty() && isEmptyRow(rows.back())) {
        rows.removeLast();
    }

    return true;
}

QString ensureCsvSuffix(QString path)
{
    if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".csv");
    }
    return path;
}
} // namespace

void MonitorController::importAgentsFromCsv()
{
    if (m_batchImportInProgress) {
        setStatusMessage(tr("Another batch import is already running."));
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(nullptr,
                                                           tr("Import agents from CSV"),
                                                           QDir::homePath(),
                                                           tr("CSV files (*.csv);;All files (*.*)"));
    if (filePath.isEmpty()) {
        return;
    }

    importAgentsFromCsvPath(filePath);
}

void MonitorController::exportAgentsToCsv()
{
    const QString suggested = QDir::home().filePath(QStringLiteral("moltbook_agents.csv"));
    const QString filePath = QFileDialog::getSaveFileName(nullptr,
                                                           tr("Export agents to CSV"),
                                                           suggested,
                                                           tr("CSV files (*.csv)"));
    if (filePath.isEmpty()) {
        return;
    }

    exportAgentsToCsvPath(filePath);
}

void MonitorController::importAgentsFromCsvPath(const QString &filePath)
{
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        setStatusMessage(tr("Import file path is empty."));
        return;
    }
    if (m_batchImportInProgress) {
        setStatusMessage(tr("Another batch import is already running."));
        return;
    }
    if (m_apiKey.isEmpty()) {
        setStatusMessage(tr("Set a Moltbook API key before importing agents."));
        return;
    }

    QFile file(trimmedPath);
    if (!file.exists()) {
        setStatusMessage(tr("Import file does not exist: %1").arg(QDir::toNativeSeparators(trimmedPath)));
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setStatusMessage(tr("Cannot open import file: %1").arg(QDir::toNativeSeparators(trimmedPath)));
        return;
    }

    QByteArray payload = file.readAll();
    if (payload.startsWith("\xEF\xBB\xBF")) {
        payload.remove(0, 3);
    }

    QString text = QString::fromUtf8(payload);
    if (text.contains(QChar::ReplacementCharacter)) {
        const QString fallback = QString::fromLocal8Bit(payload);
        if (!fallback.contains(QChar::ReplacementCharacter)) {
            text = fallback;
        }
    }

    QVector<QStringList> rows;
    QString parseError;
    if (!parseCsvRows(text, rows, parseError)) {
        setStatusMessage(tr("Import failed: %1").arg(parseError));
        return;
    }

    int headerRow = -1;
    for (int i = 0; i < rows.size(); ++i) {
        if (!isEmptyRow(rows.at(i))) {
            headerRow = i;
            break;
        }
    }
    if (headerRow < 0) {
        setStatusMessage(tr("Import failed: CSV is empty."));
        return;
    }

    const QStringList header = rows.at(headerRow);
    const QSet<QString> agentHeaders = {
        QStringLiteral("agentid"),
        QStringLiteral("agentname"),
        QStringLiteral("agent"),
        QStringLiteral("id"),
        QStringLiteral("代理id"),
        QStringLiteral("代理"),
    };
    const QSet<QString> postHeaders = {
        QStringLiteral("postthreshold"),
        QStringLiteral("postthresholdminutes"),
        QStringLiteral("postminutes"),
        QStringLiteral("发帖阈值"),
        QStringLiteral("发帖时间阈值"),
    };
    const QSet<QString> replyHeaders = {
        QStringLiteral("replythreshold"),
        QStringLiteral("replythresholdminutes"),
        QStringLiteral("replyminutes"),
        QStringLiteral("回复阈值"),
        QStringLiteral("回复时间阈值"),
    };
    const QSet<QString> humanOwnerHeaders = {
        QStringLiteral("humanowner"),
        QStringLiteral("humanownername"),
        QStringLiteral("ownername"),
        QStringLiteral("owner"),
        QStringLiteral("所属人类主人"),
        QStringLiteral("人类主人"),
        QStringLiteral("主人"),
    };

    const int agentCol = findHeaderIndex(header, agentHeaders);
    const int postCol = findHeaderIndex(header, postHeaders);
    const int replyCol = findHeaderIndex(header, replyHeaders);
    const int humanOwnerCol = findHeaderIndex(header, humanOwnerHeaders);

    if (agentCol < 0 || postCol < 0 || replyCol < 0) {
        setStatusMessage(tr("Import failed: required columns are missing. Need agent_id, post threshold, and reply threshold."));
        return;
    }

    QVector<ImportSeed> seeds;
    const int estimatedRows = std::max(0, static_cast<int>(rows.size()) - (headerRow + 1));
    seeds.reserve(estimatedRows);

    ImportStats parseStats;
    QSet<QString> seenFileIds;

    for (int rowIndex = headerRow + 1; rowIndex < rows.size(); ++rowIndex) {
        const QStringList row = rows.at(rowIndex);
        if (isEmptyRow(row)) {
            continue;
        }

        const QString agentId = cellValue(row, agentCol);
        const QString postText = cellValue(row, postCol);
        const QString replyText = cellValue(row, replyCol);
        const QString humanOwnerName = cellValue(row, humanOwnerCol);

        bool postOk = false;
        bool replyOk = false;
        const int postThreshold = postText.toInt(&postOk);
        const int replyThreshold = replyText.toInt(&replyOk);

        if (agentId.isEmpty() || !postOk || !replyOk || postThreshold < 1 || replyThreshold < 1) {
            ++parseStats.invalid;
            continue;
        }

        const QString normalizedAgent = normalizedId(agentId);
        if (seenFileIds.contains(normalizedAgent) || m_pendingAdds.contains(normalizedAgent)) {
            ++parseStats.duplicate;
            continue;
        }

        seenFileIds.insert(normalizedAgent);
        ImportSeed seed;
        seed.agentId = agentId;
        seed.postThresholdMinutes = postThreshold;
        seed.replyThresholdMinutes = replyThreshold;
        seed.humanOwnerName = humanOwnerName;
        seeds.push_back(std::move(seed));
    }

    if (seeds.isEmpty()) {
        setStatusMessage(tr("Import finished: 0 added, 0 updated, %1 duplicates skipped, %2 invalid rows, 0 verification failures.")
                             .arg(parseStats.duplicate)
                             .arg(parseStats.invalid));
        return;
    }

    m_batchImportInProgress = true;
    setStatusMessage(tr("Importing %1 agents from %2...")
                         .arg(seeds.size())
                         .arg(QFileInfo(trimmedPath).fileName()));

    auto queue = std::make_shared<QVector<ImportSeed>>(std::move(seeds));
    auto stats = std::make_shared<ImportStats>(parseStats);
    auto updatedAgentIds = std::make_shared<QStringList>();
    auto processNext = std::make_shared<std::function<void(int)>>();

    *processNext = [this, queue, stats, updatedAgentIds, processNext, trimmedPath](int cursor) {
        if (cursor >= queue->size()) {
            m_batchImportInProgress = false;
            if (stats->added > 0 || stats->updated > 0) {
                tickCountdowns();
                scheduleSaveState();
            }

            if (!updatedAgentIds->isEmpty()) {
                QStringList requestLines;
                requestLines << QStringLiteral("Action: CSV import upsert summary");
                requestLines << QStringLiteral("Source file: %1").arg(QDir::toNativeSeparators(trimmedPath));
                requestLines << QStringLiteral("Updated agents: %1").arg(updatedAgentIds->size());

                QStringList responseLines;
                responseLines << QStringLiteral("Updated agent list:");
                for (const QString &agent : *updatedAgentIds) {
                    responseLines << QStringLiteral("- %1").arg(agent);
                }

                appendRequestLog(QStringLiteral("BATCH"),
                                 QStringLiteral("IMPORT"),
                                 QDir::toNativeSeparators(trimmedPath),
                                 requestLines.join(QLatin1Char('\n')),
                                 responseLines.join(QLatin1Char('\n')),
                                 0,
                                 true,
                                 QString(),
                                 QDateTime::currentDateTimeUtc());
            }

            setStatusMessage(tr("Import finished from %1: %2 added, %3 updated, %4 duplicates skipped, %5 invalid rows, %6 verification failures.")
                                 .arg(QFileInfo(trimmedPath).fileName())
                                 .arg(stats->added)
                                 .arg(stats->updated)
                                 .arg(stats->duplicate)
                                 .arg(stats->invalid)
                                 .arg(stats->verifyFailed));
            return;
        }

        const ImportSeed seed = queue->at(cursor);
        requestProfile(seed.agentId, [this, queue, stats, updatedAgentIds, processNext, cursor, seed, trimmedPath](ProfileSnapshot snapshot) {
            if (!snapshot.ok) {
                ++stats->verifyFailed;
                (*processNext)(cursor + 1);
                return;
            }

            const QString verifiedAgentId = snapshot.agentId.isEmpty() ? seed.agentId.trimmed() : snapshot.agentId.trimmed();
            if (verifiedAgentId.isEmpty()) {
                ++stats->verifyFailed;
                (*processNext)(cursor + 1);
                return;
            }

            const int resolvedRow = findAgentRow(verifiedAgentId);
            const int requestedRow = findAgentRow(seed.agentId);
            if (resolvedRow >= 0 && requestedRow >= 0 && resolvedRow != requestedRow) {
                ++stats->duplicate;
                (*processNext)(cursor + 1);
                return;
            }

            const int targetRow = resolvedRow >= 0 ? resolvedRow : requestedRow;
            if (targetRow >= 0) {
                const AgentEntry beforeUpdate = m_agents.at(targetRow);
                applySnapshotToAgent(targetRow, snapshot);
                const int postApplyRow = findAgentRow(verifiedAgentId);
                const int updateRow = postApplyRow >= 0 ? postApplyRow : targetRow;
                updateAgentConfig(updateRow,
                                  seed.postThresholdMinutes,
                                  seed.replyThresholdMinutes,
                                  seed.humanOwnerName);

                const int finalRow = findAgentRow(verifiedAgentId);
                if (finalRow >= 0) {
                    const AgentEntry &afterUpdate = m_agents.at(finalRow);
                    QStringList requestLines;
                    requestLines << QStringLiteral("Action: CSV import upsert update");
                    requestLines << QStringLiteral("Source file: %1").arg(QDir::toNativeSeparators(trimmedPath));
                    requestLines << QStringLiteral("Input agent_id: %1").arg(seed.agentId);
                    requestLines << QStringLiteral("Verified agent_id: %1").arg(verifiedAgentId);
                    requestLines << QStringLiteral("Requested post threshold: %1").arg(seed.postThresholdMinutes);
                    requestLines << QStringLiteral("Requested reply threshold: %1").arg(seed.replyThresholdMinutes);
                    requestLines << QStringLiteral("Requested human owner: %1").arg(displayHumanOwner(seed.humanOwnerName));

                    QStringList responseLines;
                    responseLines << QStringLiteral("Result: Updated existing agent.");
                    responseLines << QStringLiteral("Post threshold: %1 -> %2")
                                         .arg(beforeUpdate.postThresholdMinutes)
                                         .arg(afterUpdate.postThresholdMinutes);
                    responseLines << QStringLiteral("Reply threshold: %1 -> %2")
                                         .arg(beforeUpdate.replyThresholdMinutes)
                                         .arg(afterUpdate.replyThresholdMinutes);
                    responseLines << QStringLiteral("Human owner: %1 -> %2")
                                         .arg(displayHumanOwner(beforeUpdate.humanOwnerName),
                                              displayHumanOwner(afterUpdate.humanOwnerName));
                    responseLines << QStringLiteral("Claim owner (X): %1 -> %2")
                                         .arg(beforeUpdate.ownerId, afterUpdate.ownerId);

                    appendRequestLog(verifiedAgentId,
                                     QStringLiteral("IMPORT"),
                                     QDir::toNativeSeparators(trimmedPath),
                                     requestLines.join(QLatin1Char('\n')),
                                     responseLines.join(QLatin1Char('\n')),
                                     0,
                                     true,
                                     QString(),
                                     QDateTime::currentDateTimeUtc());
                    updatedAgentIds->append(verifiedAgentId);
                }

                ++stats->updated;
                (*processNext)(cursor + 1);
                return;
            }

            const QString normalizedResolvedId = normalizedId(verifiedAgentId);
            if (normalizedResolvedId.isEmpty()
                || m_pendingAdds.contains(normalizedResolvedId)
                || findAgentRow(verifiedAgentId) >= 0) {
                ++stats->duplicate;
                (*processNext)(cursor + 1);
                return;
            }

            AgentEntry entry;
            entry.agentId = verifiedAgentId;
            entry.ownerId = snapshot.ownerId.isEmpty() ? QStringLiteral("unknown") : snapshot.ownerId;
            entry.humanOwnerName = seed.humanOwnerName.trimmed();
            entry.postThresholdMinutes = seed.postThresholdMinutes;
            entry.replyThresholdMinutes = seed.replyThresholdMinutes;
            entry.lastPostUtc = snapshot.lastPostUtc;
            entry.lastReplyUtc = snapshot.lastReplyUtc;
            entry.totalPostsCount = snapshot.totalPostsCount;
            entry.totalCommentsCount = snapshot.totalCommentsCount;
            entry.history = snapshot.operations;
            entry.lastRefreshUtc = QDateTime::currentDateTimeUtc();

            insertAgentSorted(std::move(entry));
            ++stats->added;
            (*processNext)(cursor + 1);
        });
    };

    (*processNext)(0);
}

void MonitorController::exportAgentsToCsvPath(const QString &filePath)
{
    if (m_agents.isEmpty()) {
        setStatusMessage(tr("No monitored agents to export."));
        return;
    }

    QString targetPath = filePath.trimmed();
    if (targetPath.isEmpty()) {
        setStatusMessage(tr("Export file path is empty."));
        return;
    }
    targetPath = ensureCsvSuffix(targetPath);

    QStringList lines;
    lines.reserve(m_agents.size() + 1);
    lines.push_back(csvLine({
        QStringLiteral("agent_id"),
        QStringLiteral("post_threshold_minutes"),
        QStringLiteral("reply_threshold_minutes"),
        QStringLiteral("human_owner_name"),
        QStringLiteral("claim_owner_id"),
    }));

    for (const AgentEntry &entry : m_agents) {
        lines.push_back(csvLine({
            entry.agentId,
            QString::number(entry.postThresholdMinutes),
            QString::number(entry.replyThresholdMinutes),
            entry.humanOwnerName,
            entry.ownerId,
        }));
    }

    QByteArray encoded;
    encoded.append("\xEF\xBB\xBF");
    encoded.append(lines.join(QStringLiteral("\r\n")).toUtf8());
    encoded.append("\r\n");

    QFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatusMessage(tr("Cannot write export file: %1").arg(QDir::toNativeSeparators(targetPath)));
        return;
    }

    const qint64 bytes = file.write(encoded);
    file.close();
    if (bytes < 0) {
        setStatusMessage(tr("Failed to write export file: %1").arg(QDir::toNativeSeparators(targetPath)));
        return;
    }

    setStatusMessage(tr("Exported %1 agents to %2.")
                         .arg(m_agents.size())
                         .arg(QDir::toNativeSeparators(targetPath)));
}
