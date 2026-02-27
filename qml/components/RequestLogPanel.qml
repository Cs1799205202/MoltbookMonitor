import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

import "../js/UiHelpers.js" as UiHelpers

Frame {
    id: root

    SplitView.fillWidth: true
    SplitView.preferredHeight: Math.max(160, uiSettings.logPanelPreferredHeight)
    SplitView.minimumHeight: 160
    padding: 10

    property var expandedRequestLogs: ({})
    property var filteredRequestLogs: ([])
    property string requestLogSearchText: ""
    property int requestLogStatusFilter: 0

    function isRequestLogExpanded(logId) {
        return expandedRequestLogs[logId] === true
    }

    function toggleRequestLogExpanded(logId) {
        const next = {}
        for (const key in expandedRequestLogs) {
            next[key] = expandedRequestLogs[key]
        }
        next[logId] = !isRequestLogExpanded(logId)
        expandedRequestLogs = next
    }

    function rebuildFilteredRequestLogs() {
        const next = []
        const logs = monitorController.requestLogs
        for (let i = 0; i < logs.length; ++i) {
            if (UiHelpers.requestLogMatchesFilter(logs[i], requestLogSearchText, requestLogStatusFilter)) {
                next.push(logs[i])
            }
        }
        filteredRequestLogs = next
    }

    background: Rectangle {
        radius: 10
        color: "white"
        border.color: "#d8dde3"
    }

    onHeightChanged: {
        const roundedHeight = Math.round(height)
        if (roundedHeight >= 160 && uiSettings.logPanelPreferredHeight !== roundedHeight) {
            uiSettings.logPanelPreferredHeight = roundedHeight
        }
    }

    onRequestLogSearchTextChanged: rebuildFilteredRequestLogs()
    onRequestLogStatusFilterChanged: rebuildFilteredRequestLogs()

    Component.onCompleted: rebuildFilteredRequestLogs()

    Connections {
        target: monitorController

        function onRequestLogsChanged() {
            root.rebuildFilteredRequestLogs()
        }
    }

    Settings {
        id: uiSettings
        category: "MainUi"
        property int logPanelPreferredHeight: 280
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
