import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DeviceSim

ApplicationWindow {
    id: window

    property bool quitting: false

    width: 480
    height: 760
    visible: true
    title: qsTr("Device Simulator")

    onClosing: (close) => {
        if (!device.idle) {
            close.accepted = false
            quitting = true
            device.disconnectFromBroker()
        }
    }

    Connections {
        target: device

        function onConnectionStateChanged() {
            if (window.quitting && device.idle)
                Qt.quit()
        }
    }

    DeviceSimulator {
        id: device

        Component.onCompleted: {
            const startupArguments = Qt.application.arguments
            if (startupArguments.indexOf("--drift") >= 0)
                drift = true
            if (startupArguments.indexOf("--connect") >= 0)
                connectToBroker()
        }
    }

    ScrollView {
        id: scroll

        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12

            GroupBox {
                Layout.fillWidth: true
                Layout.margins: 12
                Layout.bottomMargin: 0
                title: qsTr("Broker")

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    enabled: device.idle

                    Label { text: qsTr("URI") }
                    TextField {
                        Layout.fillWidth: true
                        text: device.brokerUri
                        onTextEdited: device.brokerUri = text
                    }

                    Label { text: qsTr("User") }
                    TextField {
                        Layout.fillWidth: true
                        text: device.username
                        onTextEdited: device.username = text
                    }

                    Label { text: qsTr("Password") }
                    TextField {
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        text: device.password
                        onTextEdited: device.password = text
                    }

                    Label { text: qsTr("CA file") }
                    TextField {
                        Layout.fillWidth: true
                        text: device.caFile
                        placeholderText: qsTr("System trust store")
                        onTextEdited: device.caFile = text
                    }

                    Label { text: qsTr("Serial") }
                    TextField {
                        Layout.fillWidth: true
                        text: device.serialNumber
                        onTextEdited: device.serialNumber = text
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12

                Button {
                    text: qsTr("Connect")
                    enabled: device.idle
                    onClicked: device.connectToBroker()
                }
                Button {
                    text: qsTr("Disconnect")
                    enabled: !device.idle
                    onClicked: device.disconnectFromBroker()
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    font.bold: true
                    color: device.connected ? "#1b7f3b" : "#9a5b00"
                    text: device.connectionState
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                visible: device.lastError.length > 0
                wrapMode: Text.Wrap
                color: "#b3261e"
                text: device.lastError
            }

            GroupBox {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                title: qsTr("Live readings")

                GridLayout {
                    anchors.fill: parent
                    columns: 3

                    Label { text: qsTr("Room") }
                    Slider {
                        Layout.fillWidth: true
                        from: 10
                        to: 35
                        stepSize: 0.1
                        value: device.currentTemperature
                        onMoved: device.currentTemperature = value
                    }
                    Label { text: qsTr("%1 °C").arg(device.currentTemperature.toFixed(1)) }

                    Label { text: qsTr("Humidity") }
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 0.5
                        value: device.currentHumidity
                        onMoved: device.currentHumidity = value
                    }
                    Label { text: qsTr("%1 %").arg(device.currentHumidity.toFixed(1)) }

                    Label { text: qsTr("Setpoint") }
                    Slider {
                        Layout.fillWidth: true
                        from: 10
                        to: 32
                        stepSize: 0.5
                        value: device.setpoint
                        onMoved: device.setpoint = value
                    }
                    Label { text: qsTr("%1 °C").arg(device.setpoint.toFixed(1)) }

                    Label { text: qsTr("Mode") }
                    ComboBox {
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        model: ["off", "heat", "cool", "auto"]
                        currentIndex: model.indexOf(device.mode)
                        onActivated: device.mode = currentText
                    }

                    Switch {
                        Layout.columnSpan: 3
                        text: qsTr("Fan always on")
                        checked: device.fanOn
                        onToggled: device.fanOn = checked
                    }
                    Switch {
                        Layout.columnSpan: 3
                        text: qsTr("Hold")
                        checked: device.hold
                        onToggled: device.hold = checked
                    }
                    Switch {
                        Layout.columnSpan: 3
                        text: qsTr("Drift room temperature every 2 s")
                        checked: device.drift
                        onToggled: device.drift = checked
                    }

                    Label {
                        Layout.columnSpan: 3
                        text: qsTr("Heating stage %1 · Cooling stage %2 · Fan %3")
                              .arg(device.heatingStage)
                              .arg(device.coolingStage)
                              .arg(device.fanRunning ? qsTr("running") : qsTr("idle"))
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                Layout.margins: 12
                Layout.topMargin: 0
                title: qsTr("Published %1 live messages").arg(device.publishedCount)

                Label {
                    anchors.fill: parent
                    wrapMode: Text.WrapAnywhere
                    font.family: "Consolas"
                    text: device.lastPayload.length > 0 ? device.lastPayload : qsTr("Nothing published yet")
                }
            }
        }
    }
}
