import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AppSim

ApplicationWindow {
    id: window

    property double now: Date.now()

    width: 480
    height: 760
    visible: true
    title: qsTr("App Simulator")

    AppMonitor {
        id: monitor

        Component.onCompleted: {
            if (Qt.application.arguments.indexOf("--connect") >= 0)
                connectToBroker()
        }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: window.now = Date.now()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Broker")

            GridLayout {
                anchors.fill: parent
                columns: 2
                enabled: monitor.idle

                Label { text: qsTr("URI") }
                TextField {
                    Layout.fillWidth: true
                    text: monitor.brokerUri
                    onTextEdited: monitor.brokerUri = text
                }

                Label { text: qsTr("User") }
                TextField {
                    Layout.fillWidth: true
                    text: monitor.username
                    onTextEdited: monitor.username = text
                }

                Label { text: qsTr("Password") }
                TextField {
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    text: monitor.password
                    onTextEdited: monitor.password = text
                }

                Label { text: qsTr("CA file") }
                TextField {
                    Layout.fillWidth: true
                    text: monitor.caFile
                    placeholderText: qsTr("System trust store")
                    onTextEdited: monitor.caFile = text
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: qsTr("Connect")
                enabled: monitor.idle
                onClicked: monitor.connectToBroker()
            }
            Button {
                text: qsTr("Disconnect")
                enabled: !monitor.idle
                onClicked: monitor.disconnectFromBroker()
            }
            Button {
                text: qsTr("Clear")
                onClicked: monitor.clearDevices()
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                font.bold: true
                color: monitor.connected ? "#1b7f3b" : "#9a5b00"
                text: monitor.connectionState
            }
        }

        Label {
            Layout.fillWidth: true
            visible: monitor.lastError.length > 0
            wrapMode: Text.Wrap
            color: "#b3261e"
            text: monitor.lastError
        }

        Label {
            text: qsTr("%n message(s) received", "", monitor.receivedCount)
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            visible: deviceList.count === 0
            text: monitor.connected ? qsTr("Waiting for devices…") : qsTr("Connect to see devices")
        }

        ListView {
            id: deviceList

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: monitor.devices

            delegate: Frame {
                id: card

                required property var modelData
                readonly property var live: modelData.live

                width: ListView.view.width

                ColumnLayout {
                    anchors.fill: parent

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            font.bold: true
                            text: card.modelData.sn
                        }
                        Label {
                            visible: card.modelData.online !== undefined
                            font.bold: true
                            color: card.modelData.online ? "#1b7f3b" : "#b3261e"
                            text: card.modelData.online ? qsTr("online") : qsTr("offline")
                        }
                    }

                    Label {
                        visible: !card.live
                        text: qsTr("No live data yet")
                    }

                    GridLayout {
                        visible: !!card.live
                        columns: 2
                        columnSpacing: 16

                        Label { text: qsTr("Room") }
                        Label {
                            font.pixelSize: 22
                            text: card.live ? qsTr("%1 °C").arg(Number(card.live.current_temp).toFixed(1)) : ""
                        }
                        Label { text: qsTr("Humidity") }
                        Label { text: card.live ? qsTr("%1 %").arg(Number(card.live.current_humidity).toFixed(1)) : "" }
                        Label { text: qsTr("Setpoint") }
                        Label { text: card.live ? qsTr("%1 °C").arg(Number(card.live.temp).toFixed(1)) : "" }
                        Label { text: qsTr("Mode") }
                        Label { text: card.live ? card.live.mode + (card.live.hold_status ? qsTr(" (hold)") : "") : "" }
                        Label { text: qsTr("Equipment") }
                        Label {
                            text: card.live ? qsTr("heat %1 · cool %2 · fan %3")
                                              .arg(card.live.current_heating_stage)
                                              .arg(card.live.current_cooling_stage)
                                              .arg(card.live.current_fan_status ? qsTr("on") : qsTr("off"))
                                            : ""
                        }
                    }

                    Label {
                        visible: !!card.live
                        color: "#5f6368"
                        text: card.live
                              ? qsTr("#%1 · updated %2 s ago · %3 · %4 messages")
                                .arg(card.live.seq)
                                .arg(Math.max(0, Math.round((window.now - card.modelData.receivedAt) / 1000)))
                                .arg(card.modelData.retained ? qsTr("retained snapshot")
                                                             : qsTr("latency %1 ms").arg(card.modelData.latencyMs))
                                .arg(card.modelData.messages)
                              : ""
                    }
                }
            }
        }
    }
}
