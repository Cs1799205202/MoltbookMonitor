import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../js/UiHelpers.js" as UiHelpers

Frame {
    id: root
    readonly property string humanOwnerDisplay: root.humanOwnerName.trim().length > 0 ? root.humanOwnerName : "Unassigned"
    readonly property bool postHasData: !UiHelpers.isMissingTimestamp(root.postRemainingSeconds)
    readonly property bool replyHasData: !UiHelpers.isMissingTimestamp(root.replyRemainingSeconds)

    required property int index
    required property string agentId
    required property string ownerId
    required property string humanOwnerName
    required property int postThresholdMinutes
    required property int replyThresholdMinutes
    required property string lastPostTime
    required property string lastReplyTime
    required property bool postActivityInferred
    required property bool replyActivityInferred
    required property string postActivitySourceText
    required property string replyActivitySourceText
    required property real postRemainingSeconds
    required property real replyRemainingSeconds
    required property string postCountdownText
    required property string replyCountdownText
    required property bool postOverdue
    required property bool replyOverdue
    required property var history
    required property string lastSyncError
    required property string lastRefreshTime

    padding: 10

    background: Rectangle {
        radius: 10
        color: (root.postOverdue || root.replyOverdue) ? "#fff1f1" : "#f8fafc"
        border.color: (root.postOverdue || root.replyOverdue) ? "#e76f6f" : "#d6dde5"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: "Agent: " + root.agentId
                font.pixelSize: 17
                font.bold: true
                color: "#102a43"
            }

            Label {
                text: "Human Owner: " + root.humanOwnerDisplay
                color: "#334e68"
            }

            Label {
                text: "Claim Owner (X): " + root.ownerId
                color: "#334e68"
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: "Last sync: " + root.lastRefreshTime
                color: "#627d98"
                font.pixelSize: 12
            }

            Button {
                text: "Refresh"
                onClicked: monitorController.refreshAgent(root.index)
            }

            Button {
                text: "Remove"
                onClicked: monitorController.removeAgent(root.index)
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
                        text: "Last post: " + root.lastPostTime
                        color: "#486581"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Rectangle {
                            radius: 4
                            color: !root.postHasData ? "#f1f5f9" : (root.postActivityInferred ? "#fff4e5" : "#eaf6f1")
                            border.color: !root.postHasData ? "#cbd5e1" : (root.postActivityInferred ? "#f2a93b" : "#73c19e")
                            implicitWidth: sourceTag.implicitWidth + 10
                            implicitHeight: sourceTag.implicitHeight + 4

                            Label {
                                id: sourceTag
                                anchors.centerIn: parent
                                text: !root.postHasData ? "No Data" : (root.postActivityInferred ? "Inferred" : "Confirmed")
                                color: !root.postHasData ? "#475569" : (root.postActivityInferred ? "#8a4b00" : "#0f5132")
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.postActivitySourceText
                            color: "#627d98"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    Label {
                        text: root.postCountdownText
                        color: UiHelpers.countdownColor(root.postRemainingSeconds, root.postThresholdMinutes)
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
                            width: parent.width * UiHelpers.progressRatio(root.postRemainingSeconds, root.postThresholdMinutes)
                            height: parent.height
                            radius: 4
                            color: UiHelpers.countdownColor(root.postRemainingSeconds, root.postThresholdMinutes)
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
                        text: "Last reply: " + root.lastReplyTime
                        color: "#486581"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Rectangle {
                            radius: 4
                            color: !root.replyHasData ? "#f1f5f9" : (root.replyActivityInferred ? "#fff4e5" : "#eaf6f1")
                            border.color: !root.replyHasData ? "#cbd5e1" : (root.replyActivityInferred ? "#f2a93b" : "#73c19e")
                            implicitWidth: replySourceTag.implicitWidth + 10
                            implicitHeight: replySourceTag.implicitHeight + 4

                            Label {
                                id: replySourceTag
                                anchors.centerIn: parent
                                text: !root.replyHasData ? "No Data" : (root.replyActivityInferred ? "Inferred" : "Confirmed")
                                color: !root.replyHasData ? "#475569" : (root.replyActivityInferred ? "#8a4b00" : "#0f5132")
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.replyActivitySourceText
                            color: "#627d98"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    Label {
                        text: root.replyCountdownText
                        color: UiHelpers.countdownColor(root.replyRemainingSeconds, root.replyThresholdMinutes)
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
                            width: parent.width * UiHelpers.progressRatio(root.replyRemainingSeconds, root.replyThresholdMinutes)
                            height: parent.height
                            radius: 4
                            color: UiHelpers.countdownColor(root.replyRemainingSeconds, root.replyThresholdMinutes)
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
                text: "Post threshold (min)"
            }

            SpinBox {
                id: postThresholdEditor
                from: 1
                to: 10080
                editable: true
                value: root.postThresholdMinutes
            }

            Label {
                text: "Reply threshold (min)"
            }

            SpinBox {
                id: replyThresholdEditor
                from: 1
                to: 10080
                editable: true
                value: root.replyThresholdMinutes
            }

            Label {
                text: "Human owner"
            }

            TextField {
                id: humanOwnerEditor
                Layout.preferredWidth: 180
                text: root.humanOwnerName
                placeholderText: "Optional"
            }

            Button {
                text: "Apply"
                onClicked: monitorController.updateAgentConfig(root.index,
                                                               postThresholdEditor.value,
                                                               replyThresholdEditor.value,
                                                               humanOwnerEditor.text)
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                visible: root.lastSyncError.length > 0
                text: "Sync error: " + root.lastSyncError
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
                    model: root.history

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
                                color: modelData.type === "Post" ? "#155eef"
                                       : (modelData.type === "Reply" ? "#0e9384"
                                           : (modelData.type.indexOf("Inferred") !== -1 ? "#8a4b00" : "#b42318"))
                                font.bold: true
                                Layout.preferredWidth: 92
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
