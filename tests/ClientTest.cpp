#include "MqttExt/Client.h"

#include <QFile>
#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>
#include <QtTest>

#include <memory>

using namespace MqttExt;
using State = Client::State;
using ConnectError = Client::ConnectError;

Q_DECLARE_METATYPE(MqttExt::ConnectOptions)

namespace {

constexpr int ConnectTimeoutMs = 10000;
constexpr int MessageTimeoutMs = 5000;

struct Broker
{
    QString host;
    int tcpPort = 0;
    int tlsPort = 0;
    int wsPort = 0;
    int wssPort = 0;
    QString username;
    QString password;
    QByteArray caCertificates;
    QString caCertificatesFile;
    QByteArray untrustedCaCertificates;
    QString unavailableReason;

    QString uri(const QString& scheme, int port) const
    {
        return QStringLiteral("%1://%2:%3").arg(scheme, host).arg(port);
    }
};

QString environment(const char* name, const QString& fallback)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}

int environmentPort(const char* name, int fallback)
{
    bool ok = false;
    const int port = qEnvironmentVariable(name).toInt(&ok);
    return ok ? port : fallback;
}

Broker loadBroker()
{
    Broker broker;
    broker.host = environment("MQTT_EXT_TEST_HOST", QStringLiteral("localhost"));
    broker.tcpPort = environmentPort("MQTT_EXT_TEST_TCP_PORT", 18830);
    broker.tlsPort = environmentPort("MQTT_EXT_TEST_TLS_PORT", 18883);
    broker.wsPort = environmentPort("MQTT_EXT_TEST_WS_PORT", 19001);
    broker.wssPort = environmentPort("MQTT_EXT_TEST_WSS_PORT", 19443);

    const QString secrets = environment("MQTT_EXT_TEST_SECRETS_DIR", QStringLiteral(MQTT_EXT_SECRETS_DIR));
    QFile credentials(secrets + QStringLiteral("/credentials.txt"));
    if (!credentials.open(QIODevice::ReadOnly | QIODevice::Text)) {
        broker.unavailableReason = QStringLiteral("No broker credentials at %1. Start the test broker with "
                                                  "`docker compose -f broker/compose.yaml up -d`.")
                                       .arg(credentials.fileName());
        return broker;
    }
    while (!credentials.atEnd()) {
        const QString line = QString::fromUtf8(credentials.readLine()).trimmed();
        const int separator = line.indexOf(u':');
        if (separator > 0 && line.left(separator) == QLatin1String("tester")) {
            broker.username = line.left(separator);
            broker.password = line.mid(separator + 1);
        }
    }
    if (broker.password.isEmpty()) {
        broker.unavailableReason = QStringLiteral("%1 has no tester credentials").arg(credentials.fileName());
        return broker;
    }

    broker.caCertificatesFile = secrets + QStringLiteral("/certs/ca.crt");
    QFile ca(broker.caCertificatesFile);
    QFile untrustedCa(secrets + QStringLiteral("/certs/other-ca.crt"));
    if (!ca.open(QIODevice::ReadOnly) || !untrustedCa.open(QIODevice::ReadOnly)) {
        broker.unavailableReason = QStringLiteral("The broker certificates are missing from %1/certs").arg(secrets);
        return broker;
    }
    broker.caCertificates = ca.readAll();
    broker.untrustedCaCertificates = untrustedCa.readAll();
    return broker;
}

QString uniqueTopic()
{
    return QStringLiteral("mqtt-ext-test/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool waitForState(const Client& client, State state, int timeoutMs = ConnectTimeoutMs)
{
    return QTest::qWaitFor([&]() { return client.state() == state; }, timeoutMs);
}

bool waitForCount(const QSignalSpy& spy, int count, int timeoutMs = MessageTimeoutMs)
{
    return QTest::qWaitFor([&]() { return spy.count() >= count; }, timeoutMs);
}

Message messageAt(const QSignalSpy& spy, int index)
{
    return spy.at(index).at(0).value<Message>();
}

class ScriptedBroker : public QObject
{
public:
    ScriptedBroker(quint8 connackCode, quint8 subackCode)
        : m_connackCode(connackCode)
        , m_subackCode(subackCode)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handle(socket); });
            }
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString uri() const { return QStringLiteral("tcp://127.0.0.1:%1").arg(m_server.serverPort()); }

private:
    void handle(QTcpSocket* socket)
    {
        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();
        for (;;) {
            int remaining = 0;
            int multiplier = 1;
            int index = 1;
            bool complete = false;
            while (index < buffer.size() && index <= 4) {
                const quint8 byte = quint8(buffer.at(index++));
                remaining += (byte & 0x7f) * multiplier;
                multiplier *= 128;
                if ((byte & 0x80) == 0) {
                    complete = true;
                    break;
                }
            }
            if (!complete || buffer.size() < index + remaining)
                return;

            const quint8 type = quint8(buffer.at(0)) >> 4;
            const QByteArray body = buffer.mid(index, remaining);
            buffer.remove(0, index + remaining);

            if (type == 1)
                socket->write(QByteArray::fromRawData("\x20\x02\x00", 3) + char(m_connackCode));
            else if (type == 8)
                socket->write(QByteArray("\x90\x03", 2) + body.left(2) + char(m_subackCode));
            else if (type == 12)
                socket->write(QByteArray("\xd0\x00", 2));
            else if (type == 14)
                socket->disconnectFromHost();
        }
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    quint8 m_connackCode;
    quint8 m_subackCode;
};

}

#define REQUIRE_BROKER()                                                  \
    do {                                                                  \
        if (!m_broker.unavailableReason.isEmpty())                        \
            QSKIP(qPrintable(m_broker.unavailableReason));                \
    } while (false)

class ClientTest : public QObject
{
    Q_OBJECT

private:
    ConnectOptions options(const QString& uri) const
    {
        ConnectOptions result;
        result.serverUri = uri;
        result.username = m_broker.username;
        result.password = m_broker.password;
        result.tls.caCertificates = m_broker.caCertificates;
        result.connectTimeoutSeconds = 5;
        result.reconnectMinDelayMs = 100;
        result.reconnectMaxDelayMs = 500;
        return result;
    }

    ConnectOptions tcpOptions() const
    {
        return options(m_broker.uri(QStringLiteral("tcp"), m_broker.tcpPort));
    }

    std::unique_ptr<Client> connectedClient(const ConnectOptions& connectOptions)
    {
        auto client = std::make_unique<Client>();
        QSignalSpy failed(client.get(), &Client::connectFailed);
        client->setConnectOptions(connectOptions);
        client->connectToBroker();
        if (!waitForState(*client, State::Connected)) {
            m_lastConnectError = failed.isEmpty() ? QStringLiteral("timed out") : failed.last().at(1).toString();
            return nullptr;
        }
        return client;
    }

    bool subscribeAndWait(Client& client, const QString& filter, int qos = 1)
    {
        QSignalSpy granted(&client, &Client::subscribed);
        return client.subscribe(filter, qos) && waitForCount(granted, 1);
    }

    Broker m_broker;
    QString m_lastConnectError;

private slots:
    void initTestCase()
    {
        qRegisterMetaType<Message>();
        m_broker = loadBroker();
    }

    void rejectsInvalidOptions_data()
    {
        QTest::addColumn<ConnectOptions>("connectOptions");

        ConnectOptions base;
        base.serverUri = QStringLiteral("tcp://localhost:1");

        ConnectOptions unsupportedScheme = base;
        unsupportedScheme.serverUri = QStringLiteral("http://localhost:1883");
        QTest::newRow("unsupported scheme") << unsupportedScheme;

        ConnectOptions missingHost = base;
        missingHost.serverUri = QStringLiteral("tcp://");
        QTest::newRow("missing host") << missingHost;

        ConnectOptions emptyUri = base;
        emptyUri.serverUri.clear();
        QTest::newRow("empty uri") << emptyUri;

        ConnectOptions passwordOnly = base;
        passwordOnly.password = QStringLiteral("secret");
        QTest::newRow("password without username") << passwordOnly;

        ConnectOptions missingCaFile = base;
        missingCaFile.serverUri = QStringLiteral("ssl://localhost:1");
        missingCaFile.tls.caCertificatesFile = QStringLiteral("does-not-exist.pem");
        QTest::newRow("missing CA file") << missingCaFile;

        ConnectOptions twoCaSources = base;
        twoCaSources.serverUri = QStringLiteral("ssl://localhost:1");
        twoCaSources.tls.caCertificates = "-----BEGIN CERTIFICATE-----";
        twoCaSources.tls.caCertificatesFile = QStringLiteral("ca.pem");
        QTest::newRow("two CA sources") << twoCaSources;

        ConnectOptions certificateWithoutKey = base;
        certificateWithoutKey.serverUri = QStringLiteral("ssl://localhost:1");
        certificateWithoutKey.tls.clientCertificateFile = QStringLiteral("client.pem");
        QTest::newRow("certificate without key") << certificateWithoutKey;

        ConnectOptions wildcardWill = base;
        wildcardWill.will = Will { QStringLiteral("devices/+/status"), "offline", 1, true };
        QTest::newRow("wildcard will topic") << wildcardWill;

        ConnectOptions badDelays = base;
        badDelays.reconnectMinDelayMs = 0;
        QTest::newRow("zero reconnect delay") << badDelays;

        ConnectOptions noQueue = base;
        noQueue.maxQueuedMessages = 0;
        QTest::newRow("zero queued messages") << noQueue;
    }

    void rejectsInvalidOptions()
    {
        QFETCH(ConnectOptions, connectOptions);

        Client client;
        QSignalSpy failed(&client, &Client::connectFailed);
        client.setConnectOptions(connectOptions);
        client.connectToBroker();

        QCOMPARE(client.state(), State::Connecting);
        QCOMPARE(failed.count(), 0);
        QVERIFY(waitForCount(failed, 1));
        QCOMPARE(failed.at(0).at(0).value<ConnectError>(), ConnectError::InvalidOptions);
        QCOMPARE(client.state(), State::Disconnected);

        QTest::qWait(300);
        QCOMPARE(failed.count(), 1);
    }

    void validatesPublishAndSubscribeArguments()
    {
        Client client;
        QCOMPARE(client.publish(QStringLiteral("mqtt-ext-test/x"), "payload"), -1);

        QVERIFY(!client.subscribe(QStringLiteral("a/#/b")));
        QVERIFY(!client.subscribe(QStringLiteral("a"), 3));
        QVERIFY(client.subscribe(QStringLiteral("a/+"), 0));
        QCOMPARE(client.subscriptions(), QStringList { QStringLiteral("a/+") });

        client.unsubscribe(QStringLiteral("a/+"));
        QVERIFY(client.subscriptions().isEmpty());
    }

    void roundTripsMessages()
    {
        REQUIRE_BROKER();
        const QString base = uniqueTopic();

        auto subscriber = connectedClient(tcpOptions());
        QVERIFY2(subscriber, qPrintable(m_lastConnectError));
        QSignalSpy messages(subscriber.get(), &Client::messageReceived);
        QVERIFY(subscribeAndWait(*subscriber, base + QStringLiteral("/#"), 2));

        auto publisher = connectedClient(tcpOptions());
        QVERIFY2(publisher, qPrintable(m_lastConnectError));
        QSignalSpy published(publisher.get(), &Client::published);

        const QByteArray binary("\x00\x01\xff\x00payload", 11);
        const QString unicodeTopic = base + QStringLiteral("/café");
        QCOMPARE(publisher->publish(base + QStringLiteral("/binary"), binary, 0), 0);
        const int qos1Token = publisher->publish(unicodeTopic, "qos1", 1);
        const int qos2Token = publisher->publish(base + QStringLiteral("/qos2"), "qos2", 2);
        QVERIFY(qos1Token > 0);
        QVERIFY(qos2Token > 0);

        QVERIFY(waitForCount(messages, 3));
        QVERIFY(waitForCount(published, 3));

        QHash<QString, Message> byTopic;
        for (int i = 0; i < messages.count(); ++i)
            byTopic.insert(messageAt(messages, i).topic, messageAt(messages, i));

        QCOMPARE(byTopic.value(base + QStringLiteral("/binary")).payload, binary);
        QCOMPARE(byTopic.value(base + QStringLiteral("/binary")).qos, 0);
        QCOMPARE(byTopic.value(unicodeTopic).payload, QByteArray("qos1"));
        QCOMPARE(byTopic.value(unicodeTopic).qos, 1);
        QCOMPARE(byTopic.value(base + QStringLiteral("/qos2")).qos, 2);
        for (const Message& message : byTopic)
            QVERIFY(!message.retained);

        QList<int> tokens;
        for (int i = 0; i < published.count(); ++i)
            tokens << published.at(i).at(0).toInt();
        QVERIFY(tokens.contains(qos1Token));
        QVERIFY(tokens.contains(qos2Token));
    }

    void deliversAndClearsRetainedMessages()
    {
        REQUIRE_BROKER();
        const QString topic = uniqueTopic();

        auto publisher = connectedClient(tcpOptions());
        QVERIFY2(publisher, qPrintable(m_lastConnectError));
        QSignalSpy published(publisher.get(), &Client::published);
        QVERIFY(publisher->publish(topic, "state-1", 1, true) > 0);
        QVERIFY(waitForCount(published, 1));

        auto subscriber = connectedClient(tcpOptions());
        QVERIFY2(subscriber, qPrintable(m_lastConnectError));
        QSignalSpy messages(subscriber.get(), &Client::messageReceived);
        QVERIFY(subscribeAndWait(*subscriber, topic));
        QVERIFY(waitForCount(messages, 1));
        QCOMPARE(messageAt(messages, 0).payload, QByteArray("state-1"));
        QVERIFY(messageAt(messages, 0).retained);

        QVERIFY(publisher->publish(topic, QByteArray(), 1, true) > 0);
        QVERIFY(waitForCount(published, 2));

        auto late = connectedClient(tcpOptions());
        QVERIFY2(late, qPrintable(m_lastConnectError));
        QSignalSpy lateMessages(late.get(), &Client::messageReceived);
        QVERIFY(subscribeAndWait(*late, topic));
        QTest::qWait(500);
        QCOMPARE(lateMessages.count(), 0);
    }

    void reportsRejectedCredentials()
    {
        REQUIRE_BROKER();
        ConnectOptions wrongPassword = tcpOptions();
        wrongPassword.password = QStringLiteral("wrong");
        wrongPassword.autoReconnect = false;

        Client client;
        QSignalSpy failed(&client, &Client::connectFailed);
        client.setConnectOptions(wrongPassword);
        client.connectToBroker();

        QVERIFY(waitForCount(failed, 1, ConnectTimeoutMs));
        const auto error = failed.at(0).at(0).value<ConnectError>();
        QVERIFY2(error == ConnectError::BadCredentials || error == ConnectError::NotAuthorized,
                 qPrintable(failed.at(0).at(1).toString()));
        QCOMPARE(client.state(), State::Disconnected);

        QTest::qWait(500);
        QCOMPARE(failed.count(), 1);
    }

    void retriesUntilCredentialsAreReplaced()
    {
        REQUIRE_BROKER();
        ConnectOptions wrongPassword = tcpOptions();
        wrongPassword.password = QStringLiteral("wrong");

        Client client;
        QSignalSpy failed(&client, &Client::connectFailed);
        client.setConnectOptions(wrongPassword);
        client.connectToBroker();

        QVERIFY(waitForCount(failed, 2, ConnectTimeoutMs));
        QCOMPARE(client.state(), State::Connecting);

        client.setConnectOptions(tcpOptions());
        QVERIFY(waitForState(client, State::Connected));
    }

    void mapsConnectRefusals_data()
    {
        QTest::addColumn<int>("returnCode");
        QTest::addColumn<ConnectError>("expected");

        QTest::newRow("unacceptable protocol version") << 1 << ConnectError::ProtocolVersionRejected;
        QTest::newRow("identifier rejected") << 2 << ConnectError::ClientIdRejected;
        QTest::newRow("server unavailable") << 3 << ConnectError::ServerUnavailable;
        QTest::newRow("bad user name or password") << 4 << ConnectError::BadCredentials;
        QTest::newRow("not authorized") << 5 << ConnectError::NotAuthorized;
    }

    void mapsConnectRefusals()
    {
        QFETCH(int, returnCode);
        QFETCH(ConnectError, expected);

        ScriptedBroker broker(quint8(returnCode), 0);
        QVERIFY(broker.listen());

        ConnectOptions connectOptions;
        connectOptions.serverUri = broker.uri();
        connectOptions.autoReconnect = false;

        Client client;
        QSignalSpy failed(&client, &Client::connectFailed);
        client.setConnectOptions(connectOptions);
        client.connectToBroker();

        QVERIFY(waitForCount(failed, 1, ConnectTimeoutMs));
        QCOMPARE(failed.at(0).at(0).value<ConnectError>(), expected);
        QCOMPARE(client.state(), State::Disconnected);
    }

    void reportsRefusedSubscription()
    {
        ScriptedBroker broker(0, 0x80);
        QVERIFY(broker.listen());

        ConnectOptions connectOptions;
        connectOptions.serverUri = broker.uri();
        auto client = connectedClient(connectOptions);
        QVERIFY2(client, qPrintable(m_lastConnectError));

        QSignalSpy refused(client.get(), &Client::subscriptionFailed);
        QSignalSpy granted(client.get(), &Client::subscribed);
        QVERIFY(client->subscribe(QStringLiteral("forbidden/topic"), 1));

        QVERIFY(waitForCount(refused, 1));
        QCOMPARE(refused.at(0).at(0).toString(), QStringLiteral("forbidden/topic"));
        QCOMPARE(granted.count(), 0);
        QVERIFY(!client->subscriptions().contains(QStringLiteral("forbidden/topic")));
    }

    void resubscribesAfterReconnect()
    {
        REQUIRE_BROKER();
        const QString topic = uniqueTopic();

        ConnectOptions shared = tcpOptions();
        shared.clientId = QStringLiteral("mqttexttest") + QUuid::createUuid().toString(QUuid::Id128).left(12);

        auto first = connectedClient(shared);
        QVERIFY2(first, qPrintable(m_lastConnectError));
        QSignalSpy lost(first.get(), &Client::connectionLost);
        QSignalSpy granted(first.get(), &Client::subscribed);
        QSignalSpy messages(first.get(), &Client::messageReceived);
        QVERIFY(first->subscribe(topic, 1));
        QVERIFY(waitForCount(granted, 1));

        ConnectOptions intruderOptions = shared;
        intruderOptions.autoReconnect = false;
        Client intruder;
        intruder.setConnectOptions(intruderOptions);
        intruder.connectToBroker();

        QVERIFY(waitForCount(lost, 1, ConnectTimeoutMs));
        QVERIFY(waitForCount(granted, 2, ConnectTimeoutMs));
        QCOMPARE(first->state(), State::Connected);
        QVERIFY(waitForState(intruder, State::Disconnected));

        auto publisher = connectedClient(tcpOptions());
        QVERIFY2(publisher, qPrintable(m_lastConnectError));
        QVERIFY(publisher->publish(topic, "after-reconnect", 1) > 0);
        QVERIFY(waitForCount(messages, 1));
        QCOMPARE(messageAt(messages, 0).payload, QByteArray("after-reconnect"));
    }

    void publishesWillWhenConnectionDropsWithoutDisconnect()
    {
        REQUIRE_BROKER();
        const QString topic = uniqueTopic();

        auto watcher = connectedClient(tcpOptions());
        QVERIFY2(watcher, qPrintable(m_lastConnectError));
        QSignalSpy messages(watcher.get(), &Client::messageReceived);
        QVERIFY(subscribeAndWait(*watcher, topic));

        ConnectOptions withWill = tcpOptions();
        withWill.clientId = QStringLiteral("mqttexttest") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        withWill.will = Will { topic, "gone", 1, false };
        withWill.autoReconnect = false;
        auto dropped = connectedClient(withWill);
        QVERIFY2(dropped, qPrintable(m_lastConnectError));

        ConnectOptions takeover = tcpOptions();
        takeover.clientId = withWill.clientId;
        takeover.autoReconnect = false;
        auto replacement = connectedClient(takeover);
        QVERIFY2(replacement, qPrintable(m_lastConnectError));

        QVERIFY(waitForState(*dropped, State::Disconnected));
        QVERIFY(waitForCount(messages, 1));
        QCOMPARE(messageAt(messages, 0).payload, QByteArray("gone"));
    }

    void destructionDisconnectsWithoutPublishingWill()
    {
        REQUIRE_BROKER();
        const QString topic = uniqueTopic();

        auto watcher = connectedClient(tcpOptions());
        QVERIFY2(watcher, qPrintable(m_lastConnectError));
        QSignalSpy messages(watcher.get(), &Client::messageReceived);
        QVERIFY(subscribeAndWait(*watcher, topic));

        ConnectOptions withWill = tcpOptions();
        withWill.will = Will { topic, "gone", 1, false };
        auto destroyed = connectedClient(withWill);
        QVERIFY2(destroyed, qPrintable(m_lastConnectError));
        destroyed.reset();

        QTest::qWait(1000);
        QCOMPARE(messages.count(), 0);
    }

    void disconnectsGracefullyWithoutWill()
    {
        REQUIRE_BROKER();
        const QString topic = uniqueTopic();

        auto watcher = connectedClient(tcpOptions());
        QVERIFY2(watcher, qPrintable(m_lastConnectError));
        QSignalSpy messages(watcher.get(), &Client::messageReceived);
        QVERIFY(subscribeAndWait(*watcher, topic));

        ConnectOptions withWill = tcpOptions();
        withWill.will = Will { topic, "gone", 1, false };
        auto leaving = connectedClient(withWill);
        QVERIFY2(leaving, qPrintable(m_lastConnectError));
        QSignalSpy states(leaving.get(), &Client::stateChanged);

        leaving->disconnectFromBroker();
        QCOMPARE(leaving->state(), State::Disconnecting);
        QVERIFY(waitForState(*leaving, State::Disconnected));
        QCOMPARE(states.count(), 2);

        QTest::qWait(1000);
        QCOMPARE(messages.count(), 0);
    }

    void reconnectsAfterDisconnect()
    {
        REQUIRE_BROKER();
        auto client = connectedClient(tcpOptions());
        QVERIFY2(client, qPrintable(m_lastConnectError));

        client->disconnectFromBroker();
        QVERIFY(waitForState(*client, State::Disconnected));
        client->connectToBroker();
        QVERIFY(waitForState(*client, State::Connected));
    }

    void cancelsPendingConnect_data()
    {
        QTest::addColumn<QString>("serverUri");
        QTest::addColumn<bool>("letAttemptStart");

        QTest::newRow("before the attempt starts") << QString() << false;
        QTest::newRow("while the attempt is in flight") << QStringLiteral("tcp://10.255.255.1:1883") << true;
    }

    void cancelsPendingConnect()
    {
        REQUIRE_BROKER();
        QFETCH(QString, serverUri);
        QFETCH(bool, letAttemptStart);

        ConnectOptions connectOptions = tcpOptions();
        if (!serverUri.isEmpty())
            connectOptions.serverUri = serverUri;

        Client client;
        QSignalSpy states(&client, &Client::stateChanged);
        QSignalSpy failed(&client, &Client::connectFailed);
        client.setConnectOptions(connectOptions);
        client.connectToBroker();
        if (letAttemptStart)
            QTest::qWait(200);

        client.disconnectFromBroker();
        QCOMPARE(client.state(), State::Disconnected);

        QTest::qWait(1500);
        QCOMPARE(client.state(), State::Disconnected);
        for (int i = 0; i < states.count(); ++i)
            QVERIFY(states.at(i).at(0).value<State>() != State::Connected);
        QCOMPARE(failed.count(), 0);
    }

    void roundTripsOverTransport_data()
    {
        QTest::addColumn<QString>("scheme");
        QTest::addColumn<int>("port");
        QTest::addColumn<bool>("caFromFile");

        const Broker broker = loadBroker();
        QTest::newRow("tcp") << QStringLiteral("tcp") << broker.tcpPort << false;
        QTest::newRow("ssl with CA bytes") << QStringLiteral("ssl") << broker.tlsPort << false;
        QTest::newRow("mqtts with CA file") << QStringLiteral("mqtts") << broker.tlsPort << true;
        QTest::newRow("ws") << QStringLiteral("ws") << broker.wsPort << false;
        QTest::newRow("wss with CA file") << QStringLiteral("wss") << broker.wssPort << true;
    }

    void roundTripsOverTransport()
    {
        REQUIRE_BROKER();
        QFETCH(QString, scheme);
        QFETCH(int, port);
        QFETCH(bool, caFromFile);

        ConnectOptions connectOptions = options(m_broker.uri(scheme, port));
        if (caFromFile) {
            connectOptions.tls.caCertificates.clear();
            connectOptions.tls.caCertificatesFile = m_broker.caCertificatesFile;
        }

        auto client = connectedClient(connectOptions);
        QVERIFY2(client, qPrintable(m_lastConnectError));
        const QString topic = uniqueTopic();
        QSignalSpy messages(client.get(), &Client::messageReceived);
        QVERIFY(subscribeAndWait(*client, topic));
        QVERIFY(client->publish(topic, scheme.toUtf8(), 1) > 0);
        QVERIFY(waitForCount(messages, 1));
        QCOMPARE(messageAt(messages, 0).payload, scheme.toUtf8());
    }

    void rejectsUntrustedServerCertificate_data()
    {
        QTest::addColumn<QString>("scheme");
        QTest::addColumn<int>("port");

        const Broker broker = loadBroker();
        QTest::newRow("ssl") << QStringLiteral("ssl") << broker.tlsPort;
        QTest::newRow("wss") << QStringLiteral("wss") << broker.wssPort;
    }

    void rejectsUntrustedServerCertificate()
    {
        REQUIRE_BROKER();
        QFETCH(QString, scheme);
        QFETCH(int, port);

        ConnectOptions untrusted = options(m_broker.uri(scheme, port));
        untrusted.tls.caCertificates = m_broker.untrustedCaCertificates;
        untrusted.autoReconnect = false;

        Client client;
        QSignalSpy failed(&client, &Client::connectFailed);
        client.setConnectOptions(untrusted);
        client.connectToBroker();

        QVERIFY(waitForCount(failed, 1, ConnectTimeoutMs));
        QCOMPARE(failed.at(0).at(0).value<ConnectError>(), ConnectError::Transport);
        QCOMPARE(client.state(), State::Disconnected);
    }

    void connectsToUntrustedServerWhenVerificationIsDisabled()
    {
        REQUIRE_BROKER();
        ConnectOptions unverified = options(m_broker.uri(QStringLiteral("ssl"), m_broker.tlsPort));
        unverified.tls.caCertificates = m_broker.untrustedCaCertificates;
        unverified.tls.verifyPeer = false;

        auto client = connectedClient(unverified);
        QVERIFY2(client, qPrintable(m_lastConnectError));
    }

    void survivesDeletionDuringMessageBurst()
    {
        REQUIRE_BROKER();
        const QString topic = uniqueTopic();

        auto subscriber = connectedClient(tcpOptions());
        QVERIFY2(subscriber, qPrintable(m_lastConnectError));
        QSignalSpy messages(subscriber.get(), &Client::messageReceived);
        QVERIFY(subscribeAndWait(*subscriber, topic, 0));

        auto publisher = connectedClient(tcpOptions());
        QVERIFY2(publisher, qPrintable(m_lastConnectError));
        for (int i = 0; i < 500; ++i)
            QVERIFY(publisher->publish(topic, QByteArray::number(i), 0) >= 0);

        QVERIFY(waitForCount(messages, 50));
        subscriber.reset();
        QTest::qWait(500);
        QCOMPARE(publisher->state(), State::Connected);
    }

    void rejectsPublishesBeyondQueueLimit()
    {
        REQUIRE_BROKER();
        ConnectOptions limited = tcpOptions();
        limited.maxQueuedMessages = 1;
        auto client = connectedClient(limited);
        QVERIFY2(client, qPrintable(m_lastConnectError));

        const QString topic = uniqueTopic();
        const QByteArray payload(1024, 'x');
        bool rejected = false;
        for (int i = 0; i < 100000 && !rejected; ++i)
            rejected = client->publish(topic, payload, 0) < 0;

        QVERIFY(rejected);
        QCOMPARE(client->state(), State::Connected);
        QVERIFY(QTest::qWaitFor([&]() { return client->publish(topic, payload, 0) >= 0; }, MessageTimeoutMs));
    }
};

QTEST_GUILESS_MAIN(ClientTest)
#include "ClientTest.moc"
