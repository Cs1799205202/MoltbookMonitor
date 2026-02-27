import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root
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

            Label {
                text: "Human owner"
            }

            TextField {
                id: humanOwnerInput
                Layout.preferredWidth: 220
                placeholderText: "Optional human owner"
                onAccepted: addAgentButton.clicked()
            }

            Button {
                id: addAgentButton
                text: "Verify & Add"
                onClicked: monitorController.addAgent(agentIdInput.text,
                                                      postThresholdInput.value,
                                                      replyThresholdInput.value,
                                                      humanOwnerInput.text)
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                color: "#4a5568"
                text: "Timezone: Asia/Shanghai (CST)"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: "Batch tools"
                font.bold: true
            }

            Button {
                text: "Import CSV"
                onClicked: monitorController.importAgentsFromCsv()
            }

            Button {
                text: "Export CSV"
                onClicked: monitorController.exportAgentsToCsv()
            }

            Label {
                Layout.fillWidth: true
                color: "#4a5568"
                text: "CSV columns: agent_id, post_threshold_minutes, reply_threshold_minutes, human_owner_name"
                elide: Text.ElideRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: "App Version: " + monitorController.currentVersion
                color: "#334e68"
                font.bold: true
            }

            Button {
                text: monitorController.updateCheckInProgress ? "Checking..." : "Check Update"
                enabled: !monitorController.updateCheckInProgress && !monitorController.updateDownloadInProgress
                onClicked: monitorController.checkForUpdates()
            }

            Button {
                text: monitorController.updateDownloadInProgress
                      ? "Downloading..."
                      : (monitorController.updateDownloadAvailable
                         ? ("Download " + monitorController.latestVersion)
                         : (monitorController.updateAvailable ? "No Package" : "No Update"))
                enabled: monitorController.updateDownloadAvailable && !monitorController.updateDownloadInProgress
                onClicked: monitorController.downloadLatestUpdate()
            }

            Button {
                text: monitorController.latestUpdateIgnored
                      ? ("Ignored " + monitorController.latestVersion)
                      : "Ignore This Version"
                visible: monitorController.latestVersion.length > 0
                enabled: monitorController.updateAvailable && !monitorController.latestUpdateIgnored
                onClicked: monitorController.ignoreLatestUpdate()
            }

            Button {
                text: "Clear Ignore"
                visible: monitorController.ignoredUpdateVersion.length > 0
                onClicked: monitorController.clearIgnoredUpdateVersion()
            }

            Button {
                text: "Apply Downloaded Update"
                visible: monitorController.updatePackageReady
                enabled: monitorController.updatePackageReady
                onClicked: monitorController.applyDownloadedUpdate()
            }

            Button {
                text: "Release Page"
                onClicked: monitorController.openLatestReleasePage()
            }

            ProgressBar {
                Layout.preferredWidth: 180
                from: 0
                to: 1
                value: monitorController.updateDownloadProgress
                visible: monitorController.updateDownloadInProgress
            }

            Label {
                Layout.fillWidth: true
                color: "#4a5568"
                text: monitorController.updateStatus
                elide: Text.ElideRight
            }
        }
    }
}
