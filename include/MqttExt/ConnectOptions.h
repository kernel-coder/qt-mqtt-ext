#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace MqttExt {

struct Will
{
    QString topic;
    QByteArray payload;
    int qos = 1;
    bool retained = false;
};

struct TlsOptions
{
    QByteArray caCertificates;
    QString caCertificatesFile;
    QString clientCertificateFile;
    QString privateKeyFile;
    bool verifyPeer = true;
};

struct ConnectOptions
{
    QString serverUri;
    QString clientId;
    QString username;
    QString password;
    int keepAliveSeconds = 60;
    int connectTimeoutSeconds = 30;
    bool cleanSession = true;
    int maxQueuedMessages = 1000;
    std::optional<Will> will;
    TlsOptions tls;
    bool autoReconnect = true;
    int reconnectMinDelayMs = 1000;
    int reconnectMaxDelayMs = 60000;
};

}
