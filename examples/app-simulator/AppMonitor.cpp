#include "AppMonitor.h"

#include "BrokerSecrets.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

const QString LiveFilter = QStringLiteral("devices/+/live");
const QString StatusFilter = QStringLiteral("devices/+/status");

}

AppMonitor::AppMonitor(QObject* parent)
    : QObject(parent)
    , m_brokerUri(QStringLiteral("ssl://localhost:18883"))
    , m_username(QStringLiteral("app-sim"))
    , m_password(BrokerSecrets::passwordFor(m_username))
    , m_caFile(BrokerSecrets::caCertificatesFile())
{
    m_client.subscribe(LiveFilter, 1);
    m_client.subscribe(StatusFilter, 1);

    connect(&m_client, &MqttExt::Client::stateChanged, this, [this](MqttExt::Client::State state) {
        if (state == MqttExt::Client::State::Connected)
            setLastError({});
        emit connectionStateChanged();
    });
    connect(&m_client, &MqttExt::Client::connectFailed, this,
            [this](MqttExt::Client::ConnectError, const QString& message) { setLastError(message); });
    connect(&m_client, &MqttExt::Client::connectionLost, this, &AppMonitor::setLastError);
    connect(&m_client, &MqttExt::Client::subscriptionFailed, this, [this](const QString& filter, const QString& reason) {
        setLastError(tr("Subscription to %1 failed: %2").arg(filter, reason));
    });
    connect(&m_client, &MqttExt::Client::messageReceived, this, &AppMonitor::handleMessage);
}

void AppMonitor::connectToBroker()
{
    MqttExt::ConnectOptions options;
    options.serverUri = m_brokerUri.trimmed();
    options.username = m_username;
    options.password = m_password;
    options.keepAliveSeconds = 30;
    options.tls.caCertificatesFile = m_caFile;

    m_client.setConnectOptions(options);
    m_client.connectToBroker();
}

void AppMonitor::disconnectFromBroker()
{
    m_client.disconnectFromBroker();
}

void AppMonitor::clearDevices()
{
    m_devices.clear();
    m_receivedCount = 0;
    emit devicesChanged();
}

QString AppMonitor::connectionState() const
{
    switch (m_client.state()) {
    case MqttExt::Client::State::Disconnected: return tr("Disconnected");
    case MqttExt::Client::State::Connecting: return tr("Connecting");
    case MqttExt::Client::State::Connected: return tr("Connected");
    case MqttExt::Client::State::Disconnecting: return tr("Disconnecting");
    }
    return {};
}

bool AppMonitor::connected() const
{
    return m_client.state() == MqttExt::Client::State::Connected;
}

bool AppMonitor::idle() const
{
    return m_client.state() == MqttExt::Client::State::Disconnected;
}

QVariantList AppMonitor::devices() const
{
    QVariantList list;
    for (const QVariantMap& device : m_devices)
        list.append(device);
    return list;
}

void AppMonitor::setLastError(const QString& error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void AppMonitor::handleMessage(const MqttExt::Message& message)
{
    const QStringList levels = message.topic.split(u'/');
    if (levels.size() != 3 || levels.at(0) != QLatin1String("devices"))
        return;

    const QString serialNumber = levels.at(1);
    const QString kind = levels.at(2);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    QVariantMap& device = m_devices[serialNumber];
    device.insert(QStringLiteral("sn"), serialNumber);

    if (kind == QLatin1String("status")) {
        device.insert(QStringLiteral("online"), message.payload == "online");
    } else if (kind == QLatin1String("live")) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(message.payload, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            setLastError(tr("Ignored malformed live data from %1: %2").arg(serialNumber, error.errorString()));
            return;
        }
        const QJsonObject live = document.object();
        device.insert(QStringLiteral("live"), live.toVariantMap());
        device.insert(QStringLiteral("receivedAt"), now);
        device.insert(QStringLiteral("retained"), message.retained);
        device.insert(QStringLiteral("latencyMs"), now - live.value(QStringLiteral("ts")).toInteger(now));
        device.insert(QStringLiteral("messages"), device.value(QStringLiteral("messages")).toInt() + 1);
    } else {
        return;
    }

    ++m_receivedCount;
    emit devicesChanged();
}
