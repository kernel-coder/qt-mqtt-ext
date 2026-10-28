#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>

namespace MqttExt {

struct Message
{
    QString topic;
    QByteArray payload;
    int qos = 0;
    bool retained = false;
    bool duplicate = false;
};

}

Q_DECLARE_METATYPE(MqttExt::Message)
