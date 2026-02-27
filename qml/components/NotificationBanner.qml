import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
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

    function showMessage(message) {
        bannerText.text = message
        root.visible = true
        root.opacity = 1.0
        hideBannerTimer.restart()
    }

    Behavior on opacity {
        NumberAnimation {
            duration: 180
        }
    }

    Label {
        id: bannerText
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

    Timer {
        id: hideBannerTimer
        interval: 6000
        repeat: false
        onTriggered: {
            root.opacity = 0.0
            hideAfterFadeTimer.restart()
        }
    }

    Timer {
        id: hideAfterFadeTimer
        interval: 200
        repeat: false
        onTriggered: root.visible = false
    }
}
