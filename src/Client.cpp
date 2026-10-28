#include "MqttExt/Client.h"

#include "CallbackRegistry.h"
#include "MqttExt/Topic.h"

#include <MQTTAsync.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QMap>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace MqttExt {

Q_LOGGING_CATEGORY(lcMqttExt, "mqttext")

namespace {

constexpr int MaxPayloadBytes = 268435455;
constexpr int MaxReconnectDelayMs = 24 * 60 * 60 * 1000;
constexpr int DisconnectTimeoutMs = 1000;

const QStringList& plainSchemes()
{
    static const QStringList schemes { QStringLiteral("tcp"), QStringLiteral("mqtt"), QStringLiteral("ws") };
    return schemes;
}

const QStringList& tlsSchemes()
{
    static const QStringList schemes { QStringLiteral("ssl"), QStringLiteral("tls"), QStringLiteral("mqtts"), QStringLiteral("wss") };
    return schemes;
}

bool isTlsUri(const QString& uri)
{
    return tlsSchemes().contains(QUrl(uri).scheme().toLower());
}

QByteArray nativePath(const QString& path)
{
    return QDir::toNativeSeparators(path).toUtf8();
}

QString pahoErrorText(int code)
{
    return QString::fromUtf8(MQTTAsync_strerror(code));
}

Client::ConnectError connectErrorFromCode(int code)
{
    switch (code) {
    case 1: return Client::ConnectError::ProtocolVersionRejected;
    case 2: return Client::ConnectError::ClientIdRejected;
    case 3: return Client::ConnectError::ServerUnavailable;
    case 4: return Client::ConnectError::BadCredentials;
    case 5: return Client::ConnectError::NotAuthorized;
    default: return Client::ConnectError::Transport;
    }
}

QString connectErrorText(int code, const QString& message)
{
    switch (code) {
    case 1: return QStringLiteral("The broker does not accept MQTT 3.1.1");
    case 2: return QStringLiteral("The broker rejected the client identifier");
    case 3: return QStringLiteral("The broker is unavailable");
    case 4: return QStringLiteral("The broker rejected the user name or password");
    case 5: return QStringLiteral("The client is not authorized to connect");
    default: return message.isEmpty() ? pahoErrorText(code) : message;
    }
}

QString validateOptions(const ConnectOptions& options)
{
    const QUrl url(options.serverUri);
    const QString scheme = url.scheme().toLower();
    if (options.serverUri.isEmpty() || !url.isValid() || url.host().isEmpty())
        return QStringLiteral("serverUri must be an absolute URI such as tcp://broker:1883");

    const bool tls = tlsSchemes().contains(scheme);
    if (!tls && !plainSchemes().contains(scheme))
        return QStringLiteral("Unsupported serverUri scheme \"%1\"").arg(scheme);
#ifndef MQTT_EXT_WITH_TLS
    if (tls)
        return QStringLiteral("MqttExt was built without TLS support");
#endif

    if (options.clientId.toUtf8().size() > 65535)
        return QStringLiteral("clientId is longer than 65535 bytes");
    if (options.username.isEmpty() && !options.password.isEmpty())
        return QStringLiteral("A password requires a username");
    if (options.keepAliveSeconds < 0 || options.keepAliveSeconds > 65535)
        return QStringLiteral("keepAliveSeconds must be between 0 and 65535");
    if (options.connectTimeoutSeconds <= 0)
        return QStringLiteral("connectTimeoutSeconds must be positive");
    if (options.maxQueuedMessages <= 0)
        return QStringLiteral("maxQueuedMessages must be positive");
    if (options.reconnectMinDelayMs <= 0 || options.reconnectMaxDelayMs < options.reconnectMinDelayMs
        || options.reconnectMaxDelayMs > MaxReconnectDelayMs)
        return QStringLiteral("reconnect delays must satisfy 0 < min <= max <= 24h");

    if (options.will) {
        if (!isValidTopicName(options.will->topic))
            return QStringLiteral("will topic \"%1\" is not a valid topic name").arg(options.will->topic);
        if (options.will->qos < 0 || options.will->qos > 2)
            return QStringLiteral("will qos must be 0, 1 or 2");
        if (options.will->payload.size() > MaxPayloadBytes)
            return QStringLiteral("will payload is too large");
    }

    if (tls) {
        const TlsOptions& t = options.tls;
        if (!t.caCertificates.isEmpty() && !t.caCertificatesFile.isEmpty())
            return QStringLiteral("Set either tls.caCertificates or tls.caCertificatesFile, not both");
        if (!t.caCertificatesFile.isEmpty() && !QFileInfo(t.caCertificatesFile).isFile())
            return QStringLiteral("tls.caCertificatesFile \"%1\" does not exist").arg(t.caCertificatesFile);
        if (t.clientCertificateFile.isEmpty() != t.privateKeyFile.isEmpty())
            return QStringLiteral("tls.clientCertificateFile and tls.privateKeyFile must be set together");
        if (!t.clientCertificateFile.isEmpty() && !QFileInfo(t.clientCertificateFile).isFile())
            return QStringLiteral("tls.clientCertificateFile \"%1\" does not exist").arg(t.clientCertificateFile);
        if (!t.privateKeyFile.isEmpty() && !QFileInfo(t.privateKeyFile).isFile())
            return QStringLiteral("tls.privateKeyFile \"%1\" does not exist").arg(t.privateKeyFile);
    }
    return {};
}

}

class ClientPrivate
{
public:
    explicit ClientPrivate(Client* client);

    static ClientPrivate* get(Client* client) { return client->d.get(); }

    void setState(Client::State newState);
    void beginConnecting();
    void startConnectAttempt();
    void failAttempt(Client::ConnectError error, const QString& message);
    void scheduleReconnect();
    bool ensureHandle(QString* error);
    void destroyHandle();
    bool prepareTrustStore(QString* path, QString* error);
    void sendSubscribe(const QString& filter, int qos);
    void sendUnsubscribe(const QString& filter);
    QString effectiveClientId() const;

    void handleConnectSuccess(quintptr fromKey);
    void handleConnectFailure(quintptr fromKey, int code, const QString& message);
    void handleConnectionLost(quintptr fromKey, const QString& cause);
    void handleMessage(quintptr fromKey, const Message& message);
    void handleDisconnectComplete(quintptr fromKey, bool clean);
    void handleSubscribeSuccess(quintptr fromKey, int token, int grantedQos);
    void handleSubscribeFailure(quintptr fromKey, int token, int code, const QString& message);
    void handlePublishResult(quintptr fromKey, int token, bool ok, const QString& message);

    Client* q;
    ConnectOptions options;
    Client::State state = Client::State::Disconnected;
    bool wantConnected = false;
    MQTTAsync handle = nullptr;
    quintptr key = 0;
    QByteArray handleServerUri;
    QByteArray handleClientId;
    int handleMaxQueuedMessages = 0;
    QString generatedClientId;
    std::unique_ptr<QTemporaryFile> trustStoreFile;
    QByteArray trustStoreContents;
    QTimer* reconnectTimer;
    int nextReconnectDelayMs = 0;
    QMap<QString, int> subscriptions;
    QHash<int, QString> pendingSubscribes;
};

namespace {

void post(void* context, std::function<void(ClientPrivate*, quintptr)> handler)
{
    const quintptr key = CallbackRegistry::fromContext(context);
    CallbackRegistry::post(key, [key, handler = std::move(handler)](Client* client) {
        handler(ClientPrivate::get(client), key);
    });
}

void onConnectionLostCallback(void* context, char* cause)
{
    const QString text = cause ? QString::fromUtf8(cause) : QString();
    post(context, [text](ClientPrivate* d, quintptr key) { d->handleConnectionLost(key, text); });
}

int onMessageArrivedCallback(void* context, char* topicName, int topicLen, MQTTAsync_message* message)
{
    Message received;
    received.topic = topicLen > 0 ? QString::fromUtf8(topicName, topicLen) : QString::fromUtf8(topicName);
    received.payload = QByteArray(static_cast<const char*>(message->payload), message->payloadlen);
    received.qos = message->qos;
    received.retained = message->retained != 0;
    received.duplicate = message->dup != 0;
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);

    post(context, [received](ClientPrivate* d, quintptr key) { d->handleMessage(key, received); });
    return 1;
}

void onConnectSuccessCallback(void* context, MQTTAsync_successData*)
{
    post(context, [](ClientPrivate* d, quintptr key) { d->handleConnectSuccess(key); });
}

void onConnectFailureCallback(void* context, MQTTAsync_failureData* response)
{
    const int code = response ? response->code : MQTTASYNC_FAILURE;
    const QString message = response && response->message ? QString::fromUtf8(response->message) : QString();
    post(context, [code, message](ClientPrivate* d, quintptr key) { d->handleConnectFailure(key, code, message); });
}

void onDisconnectSuccessCallback(void* context, MQTTAsync_successData*)
{
    post(context, [](ClientPrivate* d, quintptr key) { d->handleDisconnectComplete(key, true); });
}

void onDisconnectFailureCallback(void* context, MQTTAsync_failureData*)
{
    post(context, [](ClientPrivate* d, quintptr key) { d->handleDisconnectComplete(key, false); });
}

void onSubscribeSuccessCallback(void* context, MQTTAsync_successData* response)
{
    const int token = response->token;
    const int grantedQos = response->alt.qos;
    post(context, [token, grantedQos](ClientPrivate* d, quintptr key) { d->handleSubscribeSuccess(key, token, grantedQos); });
}

void onSubscribeFailureCallback(void* context, MQTTAsync_failureData* response)
{
    const int token = response->token;
    const int code = response->code;
    const QString message = response->message ? QString::fromUtf8(response->message) : QString();
    post(context, [token, code, message](ClientPrivate* d, quintptr key) { d->handleSubscribeFailure(key, token, code, message); });
}

void onPublishSuccessCallback(void* context, MQTTAsync_successData* response)
{
    const int token = response->token;
    post(context, [token](ClientPrivate* d, quintptr key) { d->handlePublishResult(key, token, true, {}); });
}

void onPublishFailureCallback(void* context, MQTTAsync_failureData* response)
{
    const int token = response->token;
    const int code = response->code;
    const QString message = response->message ? QString::fromUtf8(response->message) : QString();
    post(context, [token, code, message](ClientPrivate* d, quintptr key) {
        d->handlePublishResult(key, token, false, message.isEmpty() ? pahoErrorText(code) : message);
    });
}

}

ClientPrivate::ClientPrivate(Client* client)
    : q(client)
    , generatedClientId(QStringLiteral("mqttext") + QUuid::createUuid().toString(QUuid::Id128).left(16))
    , reconnectTimer(new QTimer(client))
{
    reconnectTimer->setSingleShot(true);
    QObject::connect(reconnectTimer, &QTimer::timeout, client, [this]() { startConnectAttempt(); });
}

void ClientPrivate::setState(Client::State newState)
{
    if (state == newState)
        return;
    state = newState;
    qCDebug(lcMqttExt) << "State" << newState;
    emit q->stateChanged(newState);
}

void ClientPrivate::beginConnecting()
{
    nextReconnectDelayMs = 0;
    setState(Client::State::Connecting);
    reconnectTimer->start(0);
}

QString ClientPrivate::effectiveClientId() const
{
    return options.clientId.isEmpty() ? generatedClientId : options.clientId;
}

void ClientPrivate::startConnectAttempt()
{
    if (!wantConnected || state != Client::State::Connecting)
        return;

    const QString invalid = validateOptions(options);
    if (!invalid.isEmpty()) {
        failAttempt(Client::ConnectError::InvalidOptions, invalid);
        return;
    }

    QString error;
    if (!ensureHandle(&error)) {
        failAttempt(Client::ConnectError::InvalidOptions, error);
        return;
    }

    const QByteArray username = options.username.toUtf8();
    const QByteArray password = options.password.toUtf8();

    MQTTAsync_connectOptions connectOptions = MQTTAsync_connectOptions_initializer;
    connectOptions.keepAliveInterval = options.keepAliveSeconds;
    connectOptions.cleansession = options.cleanSession ? 1 : 0;
    connectOptions.connectTimeout = options.connectTimeoutSeconds;
    connectOptions.MQTTVersion = MQTTVERSION_3_1_1;
    connectOptions.automaticReconnect = 0;
    connectOptions.username = username.isEmpty() ? nullptr : username.constData();
    connectOptions.password = password.isEmpty() ? nullptr : password.constData();
    connectOptions.onSuccess = onConnectSuccessCallback;
    connectOptions.onFailure = onConnectFailureCallback;
    connectOptions.context = CallbackRegistry::toContext(key);

    MQTTAsync_willOptions will = MQTTAsync_willOptions_initializer;
    QByteArray willTopic;
    if (options.will) {
        willTopic = options.will->topic.toUtf8();
        will.topicName = willTopic.constData();
        will.message = nullptr;
        will.qos = options.will->qos;
        will.retained = options.will->retained ? 1 : 0;
        if (!options.will->payload.isEmpty()) {
            will.payload.len = int(options.will->payload.size());
            will.payload.data = options.will->payload.constData();
        }
        connectOptions.will = &will;
    }

#ifdef MQTT_EXT_WITH_TLS
    MQTTAsync_SSLOptions ssl = MQTTAsync_SSLOptions_initializer;
    QByteArray trustStore;
    QByteArray keyStore;
    QByteArray privateKey;
    if (isTlsUri(options.serverUri)) {
        QString trustStorePath;
        if (!prepareTrustStore(&trustStorePath, &error)) {
            failAttempt(Client::ConnectError::Transport, error);
            return;
        }
        trustStore = nativePath(trustStorePath);
        keyStore = nativePath(options.tls.clientCertificateFile);
        privateKey = nativePath(options.tls.privateKeyFile);
        ssl.trustStore = trustStore.isEmpty() ? nullptr : trustStore.constData();
        ssl.keyStore = keyStore.isEmpty() ? nullptr : keyStore.constData();
        ssl.privateKey = privateKey.isEmpty() ? nullptr : privateKey.constData();
        ssl.enableServerCertAuth = options.tls.verifyPeer ? 1 : 0;
        ssl.verify = options.tls.verifyPeer ? 1 : 0;
        connectOptions.ssl = &ssl;
    }
#endif

    qCDebug(lcMqttExt) << "Connecting to" << options.serverUri << "as" << effectiveClientId();
    const int rc = MQTTAsync_connect(handle, &connectOptions);
    if (rc != MQTTASYNC_SUCCESS)
        failAttempt(Client::ConnectError::Transport, pahoErrorText(rc));
}

void ClientPrivate::failAttempt(Client::ConnectError error, const QString& message)
{
    qCWarning(lcMqttExt) << "Connecting to" << options.serverUri << "failed:" << message;
    if (wantConnected && options.autoReconnect && error != Client::ConnectError::InvalidOptions) {
        scheduleReconnect();
    } else {
        wantConnected = false;
        reconnectTimer->stop();
        setState(Client::State::Disconnected);
    }
    emit q->connectFailed(error, message);
}

void ClientPrivate::scheduleReconnect()
{
    const int base = nextReconnectDelayMs > 0 ? nextReconnectDelayMs : options.reconnectMinDelayMs;
    const int jitter = int(QRandomGenerator::global()->bounded(base / 5 + 1));
    reconnectTimer->start(base + jitter);
    nextReconnectDelayMs = int(qMin<qint64>(qint64(base) * 2, options.reconnectMaxDelayMs));
}

bool ClientPrivate::ensureHandle(QString* error)
{
    const QByteArray serverUri = options.serverUri.toUtf8();
    const QByteArray clientId = effectiveClientId().toUtf8();
    if (handle && serverUri == handleServerUri && clientId == handleClientId
        && options.maxQueuedMessages == handleMaxQueuedMessages)
        return true;

    destroyHandle();

    MQTTAsync_createOptions createOptions = MQTTAsync_createOptions_initializer;
    createOptions.MQTTVersion = MQTTVERSION_3_1_1;
    createOptions.sendWhileDisconnected = 0;
    createOptions.maxBufferedMessages = options.maxQueuedMessages;

    const quintptr newKey = CallbackRegistry::add(q);
    MQTTAsync newHandle = nullptr;
    int rc = MQTTAsync_createWithOptions(&newHandle, serverUri.constData(), clientId.constData(),
                                         MQTTCLIENT_PERSISTENCE_NONE, nullptr, &createOptions);
    if (rc == MQTTASYNC_SUCCESS) {
        rc = MQTTAsync_setCallbacks(newHandle, CallbackRegistry::toContext(newKey),
                                    onConnectionLostCallback, onMessageArrivedCallback, nullptr);
        if (rc != MQTTASYNC_SUCCESS)
            MQTTAsync_destroy(&newHandle);
    }
    if (rc != MQTTASYNC_SUCCESS) {
        CallbackRegistry::remove(newKey);
        *error = pahoErrorText(rc);
        return false;
    }

    handle = newHandle;
    key = newKey;
    handleServerUri = serverUri;
    handleClientId = clientId;
    handleMaxQueuedMessages = options.maxQueuedMessages;
    return true;
}

void ClientPrivate::destroyHandle()
{
    pendingSubscribes.clear();
    if (!handle)
        return;
    CallbackRegistry::remove(key);
    key = 0;
    MQTTAsync_destroy(&handle);
    handle = nullptr;
    handleServerUri.clear();
    handleClientId.clear();
    handleMaxQueuedMessages = 0;
}

bool ClientPrivate::prepareTrustStore(QString* path, QString* error)
{
    if (!options.tls.caCertificatesFile.isEmpty()) {
        *path = options.tls.caCertificatesFile;
        return true;
    }
    if (options.tls.caCertificates.isEmpty()) {
        trustStoreFile.reset();
        trustStoreContents.clear();
        path->clear();
        return true;
    }
    if (trustStoreFile && trustStoreContents == options.tls.caCertificates) {
        *path = trustStoreFile->fileName();
        return true;
    }

    auto file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/mqtt-ext-ca-XXXXXX.pem"));
    if (!file->open() || file->write(options.tls.caCertificates) != options.tls.caCertificates.size() || !file->flush()) {
        *error = QStringLiteral("Could not write the CA certificates to a temporary file: %1").arg(file->errorString());
        return false;
    }
    file->close();

    trustStoreFile = std::move(file);
    trustStoreContents = options.tls.caCertificates;
    *path = trustStoreFile->fileName();
    return true;
}

void ClientPrivate::sendSubscribe(const QString& filter, int qos)
{
    const QByteArray encoded = filter.toUtf8();
    MQTTAsync_responseOptions response = MQTTAsync_responseOptions_initializer;
    response.onSuccess = onSubscribeSuccessCallback;
    response.onFailure = onSubscribeFailureCallback;
    response.context = CallbackRegistry::toContext(key);

    const int rc = MQTTAsync_subscribe(handle, encoded.constData(), qos, &response);
    if (rc == MQTTASYNC_SUCCESS) {
        pendingSubscribes.insert(response.token, filter);
        return;
    }

    const QString reason = pahoErrorText(rc);
    qCWarning(lcMqttExt) << "Subscribing to" << filter << "failed:" << reason;
    QMetaObject::invokeMethod(q, [client = q, filter, reason]() { emit client->subscriptionFailed(filter, reason); }, Qt::QueuedConnection);
}

void ClientPrivate::sendUnsubscribe(const QString& filter)
{
    if (state != Client::State::Connected || !handle)
        return;
    const QByteArray encoded = filter.toUtf8();
    const int rc = MQTTAsync_unsubscribe(handle, encoded.constData(), nullptr);
    if (rc != MQTTASYNC_SUCCESS)
        qCWarning(lcMqttExt) << "Unsubscribing from" << filter << "failed:" << pahoErrorText(rc);
}

void ClientPrivate::handleConnectSuccess(quintptr fromKey)
{
    if (fromKey != key || state != Client::State::Connecting)
        return;
    nextReconnectDelayMs = 0;
    pendingSubscribes.clear();
    for (auto it = subscriptions.cbegin(); it != subscriptions.cend(); ++it)
        sendSubscribe(it.key(), it.value());
    setState(Client::State::Connected);
}

void ClientPrivate::handleConnectFailure(quintptr fromKey, int code, const QString& message)
{
    if (fromKey != key || state != Client::State::Connecting)
        return;
    failAttempt(connectErrorFromCode(code), connectErrorText(code, message));
}

void ClientPrivate::handleConnectionLost(quintptr fromKey, const QString& cause)
{
    if (fromKey != key)
        return;
    if (state == Client::State::Disconnecting) {
        handleDisconnectComplete(fromKey, false);
        return;
    }
    if (state != Client::State::Connected)
        return;

    pendingSubscribes.clear();
    const QString reason = cause.isEmpty() ? QStringLiteral("Connection lost") : cause;
    qCWarning(lcMqttExt) << "Connection to" << options.serverUri << "lost:" << reason;
    if (wantConnected && options.autoReconnect) {
        setState(Client::State::Connecting);
        scheduleReconnect();
    } else {
        wantConnected = false;
        setState(Client::State::Disconnected);
    }
    emit q->connectionLost(reason);
}

void ClientPrivate::handleMessage(quintptr fromKey, const Message& message)
{
    if (fromKey != key)
        return;
    emit q->messageReceived(message);
}

void ClientPrivate::handleDisconnectComplete(quintptr fromKey, bool clean)
{
    if (fromKey != key || state != Client::State::Disconnecting)
        return;
    if (!clean)
        destroyHandle();
    pendingSubscribes.clear();
    setState(Client::State::Disconnected);
    if (wantConnected)
        beginConnecting();
}

void ClientPrivate::handleSubscribeSuccess(quintptr fromKey, int token, int grantedQos)
{
    if (fromKey != key)
        return;
    const QString filter = pendingSubscribes.take(token);
    if (filter.isEmpty())
        return;
    if (!subscriptions.contains(filter)) {
        sendUnsubscribe(filter);
        return;
    }
    if (grantedQos == MQTT_BAD_SUBSCRIBE) {
        subscriptions.remove(filter);
        emit q->subscriptionFailed(filter, QStringLiteral("The broker refused the subscription"));
        return;
    }
    emit q->subscribed(filter, grantedQos);
}

void ClientPrivate::handleSubscribeFailure(quintptr fromKey, int token, int code, const QString& message)
{
    if (fromKey != key)
        return;
    const QString filter = pendingSubscribes.take(token);
    if (filter.isEmpty())
        return;
    if (code == MQTT_BAD_SUBSCRIBE) {
        subscriptions.remove(filter);
        emit q->subscriptionFailed(filter, QStringLiteral("The broker refused the subscription"));
        return;
    }
    emit q->subscriptionFailed(filter, message.isEmpty() ? pahoErrorText(code) : message);
}

void ClientPrivate::handlePublishResult(quintptr fromKey, int token, bool ok, const QString& message)
{
    if (fromKey != key)
        return;
    if (ok)
        emit q->published(token);
    else
        emit q->publishFailed(token, message);
}

Client::Client(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<ClientPrivate>(this))
{
}

Client::~Client()
{
    d->reconnectTimer->stop();
    d->destroyHandle();
}

Client::State Client::state() const
{
    return d->state;
}

ConnectOptions Client::connectOptions() const
{
    return d->options;
}

void Client::setConnectOptions(const ConnectOptions& options)
{
    d->options = options;
}

void Client::connectToBroker()
{
    d->wantConnected = true;
    if (d->state == State::Disconnected)
        d->beginConnecting();
}

void Client::disconnectFromBroker()
{
    d->wantConnected = false;
    d->reconnectTimer->stop();

    switch (d->state) {
    case State::Disconnected:
    case State::Disconnecting:
        return;
    case State::Connecting:
        d->destroyHandle();
        d->setState(State::Disconnected);
        return;
    case State::Connected:
        break;
    }

    MQTTAsync_disconnectOptions options = MQTTAsync_disconnectOptions_initializer;
    options.timeout = DisconnectTimeoutMs;
    options.onSuccess = onDisconnectSuccessCallback;
    options.onFailure = onDisconnectFailureCallback;
    options.context = CallbackRegistry::toContext(d->key);
    if (MQTTAsync_disconnect(d->handle, &options) != MQTTASYNC_SUCCESS) {
        d->destroyHandle();
        d->setState(State::Disconnected);
        return;
    }
    d->setState(State::Disconnecting);
}

int Client::publish(const QString& topic, const QByteArray& payload, int qos, bool retained)
{
    if (d->state != State::Connected) {
        qCWarning(lcMqttExt) << "Cannot publish to" << topic << "while" << d->state;
        return -1;
    }
    if (!isValidTopicName(topic) || qos < 0 || qos > 2 || payload.size() > MaxPayloadBytes) {
        qCWarning(lcMqttExt) << "Rejected publish to" << topic << "with qos" << qos << "and" << payload.size() << "bytes";
        return -1;
    }

    const QByteArray encodedTopic = topic.toUtf8();
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = payload.isEmpty() ? nullptr : const_cast<char*>(payload.constData());
    message.payloadlen = int(payload.size());
    message.qos = qos;
    message.retained = retained ? 1 : 0;

    MQTTAsync_responseOptions response = MQTTAsync_responseOptions_initializer;
    response.onSuccess = onPublishSuccessCallback;
    response.onFailure = onPublishFailureCallback;
    response.context = CallbackRegistry::toContext(d->key);

    const int rc = MQTTAsync_sendMessage(d->handle, encodedTopic.constData(), &message, &response);
    if (rc == MQTTASYNC_MAX_BUFFERED_MESSAGES) {
        qCWarning(lcMqttExt) << "Publishing to" << topic << "failed: the queue already holds"
                             << d->options.maxQueuedMessages << "unsent messages";
        return -1;
    }
    if (rc != MQTTASYNC_SUCCESS) {
        qCWarning(lcMqttExt) << "Publishing to" << topic << "failed:" << pahoErrorText(rc);
        return -1;
    }
    return response.token;
}

bool Client::subscribe(const QString& filter, int qos)
{
    if (!isValidTopicFilter(filter) || qos < 0 || qos > 2) {
        qCWarning(lcMqttExt) << "Rejected subscription to" << filter << "with qos" << qos;
        return false;
    }
    const auto existing = d->subscriptions.constFind(filter);
    if (existing != d->subscriptions.cend() && existing.value() == qos)
        return true;

    d->subscriptions.insert(filter, qos);
    if (d->state == State::Connected)
        d->sendSubscribe(filter, qos);
    return true;
}

void Client::unsubscribe(const QString& filter)
{
    if (d->subscriptions.remove(filter) == 0)
        return;
    d->sendUnsubscribe(filter);
}

QStringList Client::subscriptions() const
{
    return d->subscriptions.keys();
}

}
