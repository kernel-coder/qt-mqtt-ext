#pragma once

#include <MqttExt/Client.h>

#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class DeviceSimulator : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString brokerUri MEMBER m_brokerUri NOTIFY settingsChanged)
    Q_PROPERTY(QString username MEMBER m_username NOTIFY settingsChanged)
    Q_PROPERTY(QString password MEMBER m_password NOTIFY settingsChanged)
    Q_PROPERTY(QString caFile MEMBER m_caFile NOTIFY settingsChanged)
    Q_PROPERTY(QString serialNumber MEMBER m_serialNumber NOTIFY settingsChanged)

    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionStateChanged)
    Q_PROPERTY(bool idle READ idle NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    Q_PROPERTY(double currentTemperature READ currentTemperature WRITE setCurrentTemperature NOTIFY readingsChanged)
    Q_PROPERTY(double currentHumidity READ currentHumidity WRITE setCurrentHumidity NOTIFY readingsChanged)
    Q_PROPERTY(double setpoint READ setpoint WRITE setSetpoint NOTIFY readingsChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY readingsChanged)
    Q_PROPERTY(bool fanOn READ fanOn WRITE setFanOn NOTIFY readingsChanged)
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY readingsChanged)
    Q_PROPERTY(bool drift READ drift WRITE setDrift NOTIFY driftChanged)
    Q_PROPERTY(int heatingStage READ heatingStage NOTIFY readingsChanged)
    Q_PROPERTY(int coolingStage READ coolingStage NOTIFY readingsChanged)
    Q_PROPERTY(bool fanRunning READ fanRunning NOTIFY readingsChanged)

    Q_PROPERTY(int publishedCount READ publishedCount NOTIFY publishedChanged)
    Q_PROPERTY(QString lastPayload READ lastPayload NOTIFY publishedChanged)

public:
    explicit DeviceSimulator(QObject* parent = nullptr);

    Q_INVOKABLE void connectToBroker();
    Q_INVOKABLE void disconnectFromBroker();

    QString connectionState() const;
    bool connected() const;
    bool idle() const;
    QString lastError() const { return m_lastError; }

    double currentTemperature() const { return m_currentTemperature; }
    void setCurrentTemperature(double value);
    double currentHumidity() const { return m_currentHumidity; }
    void setCurrentHumidity(double value);
    double setpoint() const { return m_setpoint; }
    void setSetpoint(double value);
    QString mode() const { return m_mode; }
    void setMode(const QString& value);
    bool fanOn() const { return m_fanOn; }
    void setFanOn(bool value);
    bool hold() const { return m_hold; }
    void setHold(bool value);
    bool drift() const { return m_driftTimer.isActive(); }
    void setDrift(bool value);

    int heatingStage() const;
    int coolingStage() const;
    bool fanRunning() const;

    int publishedCount() const { return m_publishedCount; }
    QString lastPayload() const { return m_lastPayload; }

signals:
    void settingsChanged();
    void connectionStateChanged();
    void lastErrorChanged();
    void readingsChanged();
    void driftChanged();
    void publishedChanged();

private:
    void setLastError(const QString& error);
    void changeReadings();
    void publishLive();
    void publishStatus(bool online);
    void stepDrift();
    QString liveTopic() const;
    QString statusTopic() const;

    MqttExt::Client m_client;
    QTimer m_publishTimer;
    QTimer m_driftTimer;

    QString m_brokerUri;
    QString m_username;
    QString m_password;
    QString m_caFile;
    QString m_serialNumber;
    QString m_lastError;

    double m_currentTemperature = 20.5;
    double m_currentHumidity = 45.0;
    double m_setpoint = 22.0;
    QString m_mode = QStringLiteral("heat");
    bool m_fanOn = false;
    bool m_hold = false;

    int m_sequence = 0;
    int m_publishedCount = 0;
    QString m_lastPayload;
};
