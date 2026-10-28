#include "DeviceSimulator.h"

#include "BrokerSecrets.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include <cmath>

namespace {

constexpr double StageHysteresis = 0.5;
constexpr double AmbientTemperature = 20.0;

double roundToTenth(double value)
{
    return std::round(value * 10.0) / 10.0;
}

const QStringList& modes()
{
    static const QStringList values { QStringLiteral("off"), QStringLiteral("heat"), QStringLiteral("cool"), QStringLiteral("auto") };
    return values;
}

}

DeviceSimulator::DeviceSimulator(QObject* parent)
    : QObject(parent)
    , m_brokerUri(QStringLiteral("ssl://localhost:18883"))
    , m_username(QStringLiteral("device-sim"))
    , m_password(BrokerSecrets::passwordFor(m_username))
    , m_caFile(BrokerSecrets::caCertificatesFile())
    , m_serialNumber(QStringLiteral("SIM-0001"))
{
    m_publishTimer.setSingleShot(true);
    m_publishTimer.setInterval(200);
    connect(&m_publishTimer, &QTimer::timeout, this, &DeviceSimulator::publishLive);

    m_driftTimer.setInterval(2000);
    connect(&m_driftTimer, &QTimer::timeout, this, &DeviceSimulator::stepDrift);

    connect(&m_client, &MqttExt::Client::stateChanged, this, [this](MqttExt::Client::State state) {
        if (state == MqttExt::Client::State::Connected) {
            setLastError({});
            publishStatus(true);
            publishLive();
        }
        emit connectionStateChanged();
    });
    connect(&m_client, &MqttExt::Client::connectFailed, this,
            [this](MqttExt::Client::ConnectError, const QString& message) { setLastError(message); });
    connect(&m_client, &MqttExt::Client::connectionLost, this, &DeviceSimulator::setLastError);
    connect(&m_client, &MqttExt::Client::publishFailed, this,
            [this](int, const QString& reason) { setLastError(tr("Publish failed: %1").arg(reason)); });
}

void DeviceSimulator::connectToBroker()
{
    MqttExt::ConnectOptions options;
    options.serverUri = m_brokerUri.trimmed();
    options.clientId = QStringLiteral("devsim-") + m_serialNumber;
    options.username = m_username;
    options.password = m_password;
    options.keepAliveSeconds = 30;
    options.tls.caCertificatesFile = m_caFile;
    options.will = MqttExt::Will { statusTopic(), "offline", 1, true };

    m_client.setConnectOptions(options);
    m_client.connectToBroker();
}

void DeviceSimulator::disconnectFromBroker()
{
    if (m_client.state() == MqttExt::Client::State::Connected)
        publishStatus(false);
    m_client.disconnectFromBroker();
}

QString DeviceSimulator::connectionState() const
{
    switch (m_client.state()) {
    case MqttExt::Client::State::Disconnected: return tr("Disconnected");
    case MqttExt::Client::State::Connecting: return tr("Connecting");
    case MqttExt::Client::State::Connected: return tr("Connected");
    case MqttExt::Client::State::Disconnecting: return tr("Disconnecting");
    }
    return {};
}

bool DeviceSimulator::connected() const
{
    return m_client.state() == MqttExt::Client::State::Connected;
}

bool DeviceSimulator::idle() const
{
    return m_client.state() == MqttExt::Client::State::Disconnected;
}

void DeviceSimulator::setCurrentTemperature(double value)
{
    value = roundToTenth(value);
    if (qFuzzyCompare(m_currentTemperature, value))
        return;
    m_currentTemperature = value;
    changeReadings();
}

void DeviceSimulator::setCurrentHumidity(double value)
{
    value = roundToTenth(qBound(0.0, value, 100.0));
    if (qFuzzyCompare(m_currentHumidity, value))
        return;
    m_currentHumidity = value;
    changeReadings();
}

void DeviceSimulator::setSetpoint(double value)
{
    value = roundToTenth(value);
    if (qFuzzyCompare(m_setpoint, value))
        return;
    m_setpoint = value;
    changeReadings();
}

void DeviceSimulator::setMode(const QString& value)
{
    if (!modes().contains(value) || m_mode == value)
        return;
    m_mode = value;
    changeReadings();
}

void DeviceSimulator::setFanOn(bool value)
{
    if (m_fanOn == value)
        return;
    m_fanOn = value;
    changeReadings();
}

void DeviceSimulator::setHold(bool value)
{
    if (m_hold == value)
        return;
    m_hold = value;
    changeReadings();
}

void DeviceSimulator::setDrift(bool value)
{
    if (drift() == value)
        return;
    if (value)
        m_driftTimer.start();
    else
        m_driftTimer.stop();
    emit driftChanged();
}

int DeviceSimulator::heatingStage() const
{
    const bool heating = m_mode == QLatin1String("heat") || m_mode == QLatin1String("auto");
    return heating && m_currentTemperature < m_setpoint - StageHysteresis ? 1 : 0;
}

int DeviceSimulator::coolingStage() const
{
    const bool cooling = m_mode == QLatin1String("cool") || m_mode == QLatin1String("auto");
    return cooling && m_currentTemperature > m_setpoint + StageHysteresis ? 1 : 0;
}

bool DeviceSimulator::fanRunning() const
{
    return m_fanOn || heatingStage() > 0 || coolingStage() > 0;
}

void DeviceSimulator::setLastError(const QString& error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void DeviceSimulator::changeReadings()
{
    emit readingsChanged();
    if (connected())
        m_publishTimer.start();
}

void DeviceSimulator::publishLive()
{
    if (!connected())
        return;

    const QJsonObject live {
        { QStringLiteral("sn"), m_serialNumber },
        { QStringLiteral("seq"), ++m_sequence },
        { QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch() },
        { QStringLiteral("current_temp"), m_currentTemperature },
        { QStringLiteral("current_humidity"), m_currentHumidity },
        { QStringLiteral("temp"), m_setpoint },
        { QStringLiteral("mode"), m_mode },
        { QStringLiteral("hold_status"), m_hold },
        { QStringLiteral("current_heating_stage"), heatingStage() },
        { QStringLiteral("current_cooling_stage"), coolingStage() },
        { QStringLiteral("current_fan_status"), fanRunning() ? 1 : 0 },
    };
    const QByteArray payload = QJsonDocument(live).toJson(QJsonDocument::Compact);
    if (m_client.publish(liveTopic(), payload, 1, true) < 0) {
        setLastError(tr("Could not publish to %1").arg(liveTopic()));
        return;
    }
    ++m_publishedCount;
    m_lastPayload = QString::fromUtf8(payload);
    emit publishedChanged();
}

void DeviceSimulator::publishStatus(bool online)
{
    const QByteArray payload = online ? QByteArrayLiteral("online") : QByteArrayLiteral("offline");
    if (m_client.publish(statusTopic(), payload, 1, true) < 0)
        setLastError(tr("Could not publish to %1").arg(statusTopic()));
}

void DeviceSimulator::stepDrift()
{
    double next = m_currentTemperature;
    if (heatingStage() > 0)
        next += 0.2;
    else if (coolingStage() > 0)
        next -= 0.2;
    else
        next += (AmbientTemperature - next) * 0.05;
    setCurrentTemperature(next);

    const double humidityStep = (QRandomGenerator::global()->bounded(11) - 5) / 10.0;
    setCurrentHumidity(qBound(20.0, m_currentHumidity + humidityStep, 80.0));
}

QString DeviceSimulator::liveTopic() const
{
    return QStringLiteral("devices/%1/live").arg(m_serialNumber);
}

QString DeviceSimulator::statusTopic() const
{
    return QStringLiteral("devices/%1/status").arg(m_serialNumber);
}
