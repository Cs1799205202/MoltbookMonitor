import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root
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

        delegate: AgentCard {
            width: agentList.width - 12
            rowIndex: index
            agentId: model.agentId
            ownerId: model.ownerId
            postThresholdMinutes: model.postThresholdMinutes
            replyThresholdMinutes: model.replyThresholdMinutes
            lastPostTime: model.lastPostTime
            lastReplyTime: model.lastReplyTime
            postRemainingSeconds: model.postRemainingSeconds
            replyRemainingSeconds: model.replyRemainingSeconds
            postCountdownText: model.postCountdownText
            replyCountdownText: model.replyCountdownText
            postOverdue: model.postOverdue
            replyOverdue: model.replyOverdue
            history: model.history
            lastSyncError: model.lastSyncError
            lastRefreshTime: model.lastRefreshTime
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
