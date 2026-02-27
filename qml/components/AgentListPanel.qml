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
        section.property: "humanOwnerGroup"
        section.criteria: ViewSection.FullString
        section.delegate: Rectangle {
            width: agentList.width - 12
            height: 34
            radius: 8
            color: "#eef2f8"
            border.color: "#d8dde3"

            Label {
                anchors.fill: parent
                anchors.leftMargin: 12
                verticalAlignment: Text.AlignVCenter
                text: "Human Owner: " + section
                font.bold: true
                color: "#243b53"
            }
        }

        delegate: AgentCard {
            width: agentList.width - 12
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
