#pragma once

#include <MqttExt/Client.h>

#include <QMap>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class AppMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString brokerUri MEMBER m_brokerUri NOTIFY settingsChanged)
    Q_PROPERTY(QString username MEMBER m_username NOTIFY settingsChanged)
    Q_PROPERTY(QString password MEMBER m_password NOTIFY settingsChanged)
    Q_PROPERTY(QString caFile MEMBER m_caFile NOTIFY settingsChanged)

    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionStateChanged)
    Q_PROPERTY(bool idle READ idle NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int receivedCount READ receivedCount NOTIFY devicesChanged)

public:
    explicit AppMonitor(QObject* parent = nullptr);

    Q_INVOKABLE void connectToBroker();
    Q_INVOKABLE void disconnectFromBroker();
    Q_INVOKABLE void clearDevices();

    QString connectionState() const;
    bool connected() const;
    bool idle() const;
    QString lastError() const { return m_lastError; }

    QVariantList devices() const;
    int receivedCount() const { return m_receivedCount; }

signals:
    void settingsChanged();
    void connectionStateChanged();
    void lastErrorChanged();
    void devicesChanged();

private:
    void setLastError(const QString& error);
    void handleMessage(const MqttExt::Message& message);

    MqttExt::Client m_client;

    QString m_brokerUri;
    QString m_username;
    QString m_password;
    QString m_caFile;
    QString m_lastError;

    QMap<QString, QVariantMap> m_devices;
    int m_receivedCount = 0;
};
