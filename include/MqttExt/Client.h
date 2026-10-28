#pragma once

#include "MqttExt/ConnectOptions.h"
#include "MqttExt/Message.h"

#include <QObject>
#include <QStringList>

#include <memory>

namespace MqttExt {

class ClientPrivate;

class Client : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)

public:
    enum class State { Disconnected, Connecting, Connected, Disconnecting };
    Q_ENUM(State)

    enum class ConnectError {
        InvalidOptions,
        Transport,
        ProtocolVersionRejected,
        ClientIdRejected,
        ServerUnavailable,
        BadCredentials,
        NotAuthorized
    };
    Q_ENUM(ConnectError)

    explicit Client(QObject* parent = nullptr);
    ~Client() override;

    State state() const;

    ConnectOptions connectOptions() const;
    void setConnectOptions(const ConnectOptions& options);

    void connectToBroker();
    void disconnectFromBroker();

    int publish(const QString& topic, const QByteArray& payload, int qos = 1, bool retained = false);

    bool subscribe(const QString& filter, int qos = 1);
    void unsubscribe(const QString& filter);
    QStringList subscriptions() const;

signals:
    void stateChanged(MqttExt::Client::State state);
    void connectFailed(MqttExt::Client::ConnectError error, const QString& message);
    void connectionLost(const QString& cause);
    void messageReceived(const MqttExt::Message& message);
    void subscribed(const QString& filter, int grantedQos);
    void subscriptionFailed(const QString& filter, const QString& reason);
    void published(int token);
    void publishFailed(int token, const QString& reason);

private:
    friend class ClientPrivate;
    std::unique_ptr<ClientPrivate> d;
};

}
