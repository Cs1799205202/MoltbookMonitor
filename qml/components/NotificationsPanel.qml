import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root
    required property var notificationModel

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
            model: root.notificationModel

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
