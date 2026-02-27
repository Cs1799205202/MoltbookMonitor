.pragma library

var missingTimestampThreshold = -900000000000;

function isMissingTimestamp(value) {
    return value < missingTimestampThreshold;
}

function countdownColor(remainingSeconds, thresholdMinutes) {
    if (isMissingTimestamp(remainingSeconds)) {
        return "#7d8590";
    }
    if (remainingSeconds <= 0) {
        return "#d7301f";
    }

    var thresholdSeconds = thresholdMinutes * 60;
    if (thresholdSeconds <= 0) {
        return "#7d8590";
    }

    var ratio = Math.max(0.0, Math.min(1.0, remainingSeconds / thresholdSeconds));
    var hue = 120.0 * ratio;
    return Qt.hsla(hue / 360.0, 0.75, 0.43, 1.0);
}

function progressRatio(remainingSeconds, thresholdMinutes) {
    if (isMissingTimestamp(remainingSeconds) || remainingSeconds <= 0) {
        return 0.0;
    }

    var thresholdSeconds = thresholdMinutes * 60;
    if (thresholdSeconds <= 0) {
        return 0.0;
    }
    return Math.max(0.0, Math.min(1.0, remainingSeconds / thresholdSeconds));
}

function textForFilter(value) {
    if (value === undefined || value === null) {
        return "";
    }
    return String(value).toLowerCase();
}

function requestLogMatchesFilter(logEntry, searchText, statusFilter) {
    if (statusFilter === 1 && !logEntry.ok) {
        return false;
    }
    if (statusFilter === 2 && logEntry.ok) {
        return false;
    }

    var keyword = searchText.trim().toLowerCase();
    if (keyword.length === 0) {
        return true;
    }

    var fields = [
        logEntry.timestamp,
        logEntry.agentId,
        logEntry.method,
        logEntry.url,
        logEntry.statusCode,
        logEntry.networkError,
        logEntry.requestContent,
        logEntry.responseContent
    ];

    for (var i = 0; i < fields.length; ++i) {
        if (textForFilter(fields[i]).indexOf(keyword) !== -1) {
            return true;
        }
    }
    return false;
}
