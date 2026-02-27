import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root
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
