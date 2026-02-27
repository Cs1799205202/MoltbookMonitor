import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

ApplicationWindow {
    id: root
    width: 1320
    height: 860
    visible: true
    title: "Moltbook Agent Activity Monitor"
    color: "#f3f5f8"

    property color safeColor: "#2ca25f"
    property color dangerColor: "#d7301f"
    property color noDataColor: "#7d8590"
    property var expandedRequestLogs: ({})
    property var filteredRequestLogs: ([])
    property string requestLogSearchText: ""
    property int requestLogStatusFilter: 0

    function countdownColor(remainingSeconds, thresholdMinutes) {
        if (remainingSeconds < -900000000000) {
            return noDataColor
        }
        if (remainingSeconds <= 0) {
            return dangerColor
        }

        var thresholdSeconds = thresholdMinutes * 60
        if (thresholdSeconds <= 0) {
            return noDataColor
        }

        var ratio = Math.max(0.0, Math.min(1.0, remainingSeconds / thresholdSeconds))
        var hue = 120.0 * ratio
        return Qt.hsla(hue / 360.0, 0.75, 0.43, 1.0)
    }

    function progressRatio(remainingSeconds, thresholdMinutes) {
        if (remainingSeconds < -900000000000) {
            return 0.0
        }
        if (remainingSeconds <= 0) {
            return 0.0
        }

        var thresholdSeconds = thresholdMinutes * 60
        if (thresholdSeconds <= 0) {
            return 0.0
        }
        return Math.max(0.0, Math.min(1.0, remainingSeconds / thresholdSeconds))
    }

    function isRequestLogExpanded(logId) {
        return expandedRequestLogs[logId] === true
    }

    function toggleRequestLogExpanded(logId) {
        var next = {}
        for (var key in expandedRequestLogs) {
            next[key] = expandedRequestLogs[key]
        }
        next[logId] = !isRequestLogExpanded(logId)
        expandedRequestLogs = next
    }

    function textForFilter(value) {
        if (value === undefined || value === null) {
            return ""
        }
        return String(value).toLowerCase()
    }

    function requestLogMatchesFilter(logEntry) {
        if (requestLogStatusFilter === 1 && !logEntry.ok) {
            return false
        }
        if (requestLogStatusFilter === 2 && logEntry.ok) {
            return false
        }

        var keyword = requestLogSearchText.trim().toLowerCase()
        if (keyword.length === 0) {
            return true
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
        ]
        for (var i = 0; i < fields.length; ++i) {
            if (textForFilter(fields[i]).indexOf(keyword) !== -1) {
                return true
            }
        }
        return false
    }

    function rebuildFilteredRequestLogs() {
        var next = []
        var logs = monitorController.requestLogs
        for (var i = 0; i < logs.length; ++i) {
            if (requestLogMatchesFilter(logs[i])) {
                next.push(logs[i])
            }
        }
        filteredRequestLogs = next
    }

    Settings {
        id: uiSettings
        category: "MainUi"
        property int logPanelPreferredHeight: 280
    }

    ListModel {
        id: notificationModel
    }

    Component.onCompleted: rebuildFilteredRequestLogs()
    onRequestLogSearchTextChanged: rebuildFilteredRequestLogs()
    onRequestLogStatusFilterChanged: rebuildFilteredRequestLogs()

    Connections {
        target: monitorController
        function onNotificationRaised(message) {
            notificationModel.insert(0, {
                                         "timestamp": monitorController.currentShanghaiTimeString(),
                                         "message": message
                                     })
            if (notificationModel.count > 300) {
                notificationModel.remove(300, notificationModel.count - 300)
            }

            notificationBannerText.text = message
            notificationBanner.visible = true
            notificationBanner.opacity = 1.0
            hideBannerTimer.restart()
        }

        function onRequestLogsChanged() {
            rebuildFilteredRequestLogs()
        }
    }

    Rectangle {
        id: notificationBanner
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        width: Math.min(parent.width - 40, 900)
        height: 42
        radius: 8
        color: "#fff4d6"
        border.color: "#f6c453"
        visible: false
        opacity: 0.0
        z: 999

        Behavior on opacity {
            NumberAnimation {
                duration: 180
            }
        }

        Label {
            id: notificationBannerText
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 13
            text: ""
            elide: Text.ElideRight
            color: "#3c2f04"
        }
    }

    Timer {
        id: hideBannerTimer
        interval: 6000
        repeat: false
        onTriggered: {
            notificationBanner.opacity = 0.0
            notificationBanner.visible = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Frame {
            Layout.fillWidth: true
            padding: 12

            background: Rectangle {
                radius: 10
                color: "white"
                border.color: "#d8dde3"
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: "Moltbook API Key"
                        font.bold: true
                    }

                    TextField {
                        id: apiKeyInput
                        Layout.fillWidth: true
                        placeholderText: "moltbook_xxx"
                        echoMode: TextInput.Password
                        text: monitorController.apiKey
                    }

                    Button {
                        text: "Apply Key"
                        onClicked: monitorController.setApiKey(apiKeyInput.text)
                    }

                    Button {
                        text: "Refresh All"
                        onClicked: monitorController.refreshAll()
                    }

                    BusyIndicator {
                        running: monitorController.busy
                        visible: running
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: "Add Agent ID"
                        font.bold: true
                    }

                    TextField {
                        id: agentIdInput
                        Layout.preferredWidth: 260
                        placeholderText: "Agent name"
                        onAccepted: addAgentButton.clicked()
                    }

                    Label {
                        text: "Post threshold (min)"
                    }

                    SpinBox {
                        id: postThresholdInput
                        from: 1
                        to: 10080
                        editable: true
                        value: 180
                    }

                    Label {
                        text: "Reply threshold (min)"
                    }

                    SpinBox {
                        id: replyThresholdInput
                        from: 1
                        to: 10080
                        editable: true
                        value: 120
                    }

                    Button {
                        id: addAgentButton
                        text: "Verify & Add"
                        onClicked: {
                            monitorController.addAgent(agentIdInput.text, postThresholdInput.value, replyThresholdInput.value)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        color: "#4a5568"
                        text: "Timezone: Asia/Shanghai (CST)"
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            padding: 8
            background: Rectangle {
                radius: 8
                color: "white"
                border.color: "#d8dde3"
            }

            Label {
                anchors.fill: parent
                text: monitorController.statusMessage
                color: "#3c4a5c"
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        SplitView {
            id: mainContentSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            spacing: 12

            SplitView {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 260
                orientation: Qt.Horizontal
                spacing: 12

                Frame {
                    SplitView.fillWidth: true
                    padding: 10
                    background: Rectangle {
                        radius: 10
                        color: "white"
                        border.color: "#d8dde3"
                    }

                    ListView {
                        id: agentList
                        anchors.fill: parent
                        spacing: 10
                        model: monitorController
                        clip: true

                        delegate: Frame {
                        required property int index
                        required property string agentId
                        required property string ownerId
                        required property int postThresholdMinutes
                        required property int replyThresholdMinutes
                        required property string lastPostTime
                        required property string lastReplyTime
                        required property real postRemainingSeconds
                        required property real replyRemainingSeconds
                        required property string postCountdownText
                        required property string replyCountdownText
                        required property bool postOverdue
                        required property bool replyOverdue
                        required property var history
                        required property string lastSyncError
                        required property string lastRefreshTime

                        width: agentList.width - 12
                        padding: 10

                        background: Rectangle {
                            radius: 10
                            color: (postOverdue || replyOverdue) ? "#fff1f1" : "#f8fafc"
                            border.color: (postOverdue || replyOverdue) ? "#e76f6f" : "#d6dde5"
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "Agent: " + agentId
                                    font.pixelSize: 17
                                    font.bold: true
                                    color: "#102a43"
                                }

                                Label {
                                    text: "Owner ID: " + ownerId
                                    color: "#334e68"
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: "Last sync: " + lastRefreshTime
                                    color: "#627d98"
                                    font.pixelSize: 12
                                }

                                Button {
                                    text: "Refresh"
                                    onClicked: monitorController.refreshAgent(index)
                                }

                                Button {
                                    text: "Remove"
                                    onClicked: monitorController.removeAgent(index)
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                rowSpacing: 8
                                columnSpacing: 18

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.minimumHeight: 94
                                    radius: 8
                                    color: "white"
                                    border.color: "#dce4ec"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 4

                                        Label {
                                            text: "Posting Activity"
                                            font.bold: true
                                            color: "#243b53"
                                        }

                                        Label {
                                            text: "Last post: " + lastPostTime
                                            color: "#486581"
                                        }

                                        Label {
                                            text: postCountdownText
                                            color: root.countdownColor(postRemainingSeconds, postThresholdMinutes)
                                            font.bold: true
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: 8
                                            radius: 4
                                            color: "#d5dde6"

                                            Rectangle {
                                                anchors.left: parent.left
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: parent.width * root.progressRatio(postRemainingSeconds, postThresholdMinutes)
                                                height: parent.height
                                                radius: 4
                                                color: root.countdownColor(postRemainingSeconds, postThresholdMinutes)
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.minimumHeight: 94
                                    radius: 8
                                    color: "white"
                                    border.color: "#dce4ec"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 4

                                        Label {
                                            text: "Reply Activity"
                                            font.bold: true
                                            color: "#243b53"
                                        }

                                        Label {
                                            text: "Last reply: " + lastReplyTime
                                            color: "#486581"
                                        }

                                        Label {
                                            text: replyCountdownText
                                            color: root.countdownColor(replyRemainingSeconds, replyThresholdMinutes)
                                            font.bold: true
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: 8
                                            radius: 4
                                            color: "#d5dde6"

                                            Rectangle {
                                                anchors.left: parent.left
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: parent.width * root.progressRatio(replyRemainingSeconds, replyThresholdMinutes)
                                                height: parent.height
                                                radius: 4
                                                color: root.countdownColor(replyRemainingSeconds, replyThresholdMinutes)
                                            }
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Label {
                                    text: "Thresholds:"
                                    color: "#243b53"
                                    font.bold: true
                                }

                                Label {
                                    text: "Post"
                                }

                                SpinBox {
                                    id: postThresholdEditor
                                    from: 1
                                    to: 10080
                                    editable: true
                                    value: postThresholdMinutes
                                }

                                Label {
                                    text: "Reply"
                                }

                                SpinBox {
                                    id: replyThresholdEditor
                                    from: 1
                                    to: 10080
                                    editable: true
                                    value: replyThresholdMinutes
                                }

                                Button {
                                    text: "Apply"
                                    onClicked: monitorController.updateThresholds(index, postThresholdEditor.value, replyThresholdEditor.value)
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Label {
                                    visible: lastSyncError.length > 0
                                    text: "Sync error: " + lastSyncError
                                    color: "#b42318"
                                    elide: Text.ElideRight
                                    Layout.preferredWidth: 420
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.minimumHeight: 120
                                radius: 8
                                color: "white"
                                border.color: "#dce4ec"

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 6

                                    Label {
                                        text: "Operation History"
                                        font.bold: true
                                        color: "#243b53"
                                    }

                                    ListView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        spacing: 4
                                        model: history

                                        delegate: Rectangle {
                                            required property var modelData
                                            width: ListView.view.width
                                            height: 26
                                            color: "transparent"

                                            RowLayout {
                                                anchors.fill: parent
                                                spacing: 8

                                                Label {
                                                    text: modelData.type
                                                    color: modelData.type === "Post" ? "#155eef" : "#0e9384"
                                                    font.bold: true
                                                    Layout.preferredWidth: 48
                                                }

                                                Label {
                                                    text: modelData.timestamp
                                                    color: "#486581"
                                                    Layout.preferredWidth: 188
                                                }

                                                Label {
                                                    text: modelData.detail
                                                    color: "#334e68"
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }
                                            }
                                        }

                                        ScrollBar.vertical: ScrollBar {
                                            visible: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                    Label {
                        anchors.centerIn: parent
                        visible: agentList.count === 0
                        text: "No monitored agents.\nAdd an Agent ID and verify it to start tracking."
                        horizontalAlignment: Text.AlignHCenter
                        color: "#5f6c7b"
                    }
                }

                Frame {
                    SplitView.preferredWidth: 330
                    padding: 10
                    background: Rectangle {
                        radius: 10
                        color: "white"
                        border.color: "#d8dde3"
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        Label {
                            text: "Notifications"
                            font.bold: true
                            font.pixelSize: 16
                            color: "#102a43"
                        }

                        Label {
                            text: "Includes Agent ID and Owner ID for inactivity alerts."
                            wrapMode: Text.WordWrap
                            color: "#486581"
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 8
                            model: notificationModel

                            delegate: Rectangle {
                                width: ListView.view.width
                                radius: 6
                                color: "#fff7e6"
                                border.color: "#f9d087"
                                implicitHeight: contentColumn.implicitHeight + 10

                                Column {
                                    id: contentColumn
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 4

                                    Label {
                                        text: model.timestamp
                                        color: "#8a5a00"
                                        font.pixelSize: 12
                                    }

                                    Label {
                                        text: model.message
                                        wrapMode: Text.WordWrap
                                        color: "#4f2f00"
                                    }
                                }
                            }

                            ScrollBar.vertical: ScrollBar {
                                visible: true
                            }
                        }
                    }
                }
            }

            Frame {
                id: requestLogPanel
                SplitView.fillWidth: true
                SplitView.preferredHeight: Math.max(160, uiSettings.logPanelPreferredHeight)
                SplitView.minimumHeight: 160
                padding: 10
                background: Rectangle {
                    radius: 10
                    color: "white"
                    border.color: "#d8dde3"
                }

                onHeightChanged: {
                    var h = Math.round(height)
                    if (h >= 160 && uiSettings.logPanelPreferredHeight !== h) {
                        uiSettings.logPanelPreferredHeight = h
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "Request / Response Logs"
                            font.bold: true
                            font.pixelSize: 16
                            color: "#102a43"
                        }

                        Label {
                            text: root.filteredRequestLogs.length + " / "
                                  + monitorController.requestLogs.length + " entries"
                            color: "#627d98"
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            text: "Clear Logs"
                            enabled: monitorController.requestLogs.length > 0
                            onClicked: {
                                monitorController.clearRequestLogs()
                                root.expandedRequestLogs = ({})
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: requestLogSearchInput
                            Layout.fillWidth: true
                            placeholderText: "Filter by keyword (agent, URL, error, request, response...)"
                            onTextChanged: root.requestLogSearchText = text
                        }

                        ComboBox {
                            id: requestLogStatusFilterBox
                            Layout.preferredWidth: 150
                            model: ["All", "Success only", "Failed only"]
                            currentIndex: root.requestLogStatusFilter
                            onActivated: root.requestLogStatusFilter = currentIndex
                        }

                        Button {
                            text: "Reset Filter"
                            enabled: requestLogSearchInput.text.length > 0 || root.requestLogStatusFilter !== 0
                            onClicked: {
                                requestLogSearchInput.text = ""
                                root.requestLogStatusFilter = 0
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ListView {
                            id: requestLogList
                            anchors.fill: parent
                            model: root.filteredRequestLogs
                            visible: root.filteredRequestLogs.length > 0
                            clip: true
                            spacing: 8

                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width
                                radius: 8
                                color: "#f8fafc"
                                border.color: "#dce4ec"
                                implicitHeight: logColumn.implicitHeight + 12

                                ColumnLayout {
                                    id: logColumn
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 6

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Label {
                                            text: modelData.timestamp + " | Agent: " + modelData.agentId
                                            color: "#334e68"
                                            font.bold: true
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            text: modelData.statusCode ? ("HTTP " + modelData.statusCode) : "No HTTP status"
                                            color: modelData.ok ? "#067647" : "#b42318"
                                        }

                                        Button {
                                            text: root.isRequestLogExpanded(modelData.id) ? "Collapse" : "Expand"
                                            onClicked: root.toggleRequestLogExpanded(modelData.id)
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.method + " " + modelData.url
                                        color: "#486581"
                                        elide: Text.ElideMiddle
                                    }

                                    Label {
                                        visible: !modelData.ok && modelData.networkError.length > 0
                                        text: "Network error: " + modelData.networkError
                                        color: "#b42318"
                                        wrapMode: Text.WordWrap
                                    }

                                    ColumnLayout {
                                        visible: root.isRequestLogExpanded(modelData.id)
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: "Request"
                                            font.bold: true
                                            color: "#243b53"
                                        }

                                        ScrollView {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 150
                                            clip: true
                                            background: Rectangle {
                                                radius: 6
                                                color: "white"
                                                border.color: "#dce4ec"
                                            }

                                            ScrollBar.vertical.policy: ScrollBar.AsNeeded
                                            ScrollBar.horizontal.policy: ScrollBar.AsNeeded

                                            TextArea {
                                                readOnly: true
                                                selectByMouse: true
                                                textFormat: TextEdit.PlainText
                                                wrapMode: TextEdit.NoWrap
                                                text: modelData.requestContent
                                                font.family: "Consolas"
                                            }
                                        }

                                        Label {
                                            text: "Response"
                                            font.bold: true
                                            color: "#243b53"
                                        }

                                        ScrollView {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 180
                                            clip: true
                                            background: Rectangle {
                                                radius: 6
                                                color: "white"
                                                border.color: "#dce4ec"
                                            }

                                            ScrollBar.vertical.policy: ScrollBar.AsNeeded
                                            ScrollBar.horizontal.policy: ScrollBar.AsNeeded

                                            TextArea {
                                                readOnly: true
                                                selectByMouse: true
                                                textFormat: TextEdit.PlainText
                                                wrapMode: TextEdit.NoWrap
                                                text: modelData.responseContent
                                                font.family: "Consolas"
                                            }
                                        }
                                    }
                                }
                            }

                            ScrollBar.vertical: ScrollBar {
                                visible: true
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: root.filteredRequestLogs.length === 0
                            text: monitorController.requestLogs.length === 0
                                  ? "No request logs yet. Trigger refresh or add an agent to generate entries."
                                  : "No logs match current filter."
                            horizontalAlignment: Text.AlignHCenter
                            color: "#5f6c7b"
                        }
                    }
                }
            }
        }
    }
}
