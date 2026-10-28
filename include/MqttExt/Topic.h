#pragma once

#include <QString>

namespace MqttExt {

bool isValidTopicName(const QString& topic);
bool isValidTopicFilter(const QString& filter);
bool topicMatches(const QString& filter, const QString& topic);

}
