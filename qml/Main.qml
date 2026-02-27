import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "components" as Components

ApplicationWindow {
    id: root
    width: 1320
    height: 860
    visible: true
    title: "Moltbook Agent Activity Monitor"
    color: "#f3f5f8"

    ListModel {
        id: notificationModel
    }

    Components.NotificationBanner {
        id: notificationBanner
    }

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

            notificationBanner.showMessage(message)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Components.MonitorControlPanel {
            Layout.fillWidth: true
        }

        Components.StatusMessagePanel {
            Layout.fillWidth: true
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

                Components.AgentListPanel {
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                }

                Components.NotificationsPanel {
                    notificationModel: notificationModel
                }
            }

            Components.RequestLogPanel {
            }
        }
    }
}
