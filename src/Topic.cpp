#include "MqttExt/Topic.h"

#include <QStringList>

namespace MqttExt {

namespace {

constexpr int MaxTopicBytes = 65535;

bool hasValidEncoding(const QString& value)
{
    if (value.isEmpty() || value.contains(QChar(u'\0')))
        return false;
    return value.toUtf8().size() <= MaxTopicBytes;
}

}

bool isValidTopicName(const QString& topic)
{
    return hasValidEncoding(topic) && !topic.contains(u'+') && !topic.contains(u'#');
}

bool isValidTopicFilter(const QString& filter)
{
    if (!hasValidEncoding(filter))
        return false;

    const QStringList levels = filter.split(u'/');
    for (int i = 0; i < levels.size(); ++i) {
        const QString& level = levels.at(i);
        if (level.contains(u'#') && (level != QLatin1String("#") || i != levels.size() - 1))
            return false;
        if (level.contains(u'+') && level != QLatin1String("+"))
            return false;
    }
    return true;
}

bool topicMatches(const QString& filter, const QString& topic)
{
    if (!isValidTopicFilter(filter) || !isValidTopicName(topic))
        return false;

    const QStringList filterLevels = filter.split(u'/');
    const QStringList topicLevels = topic.split(u'/');

    const QString& firstFilterLevel = filterLevels.first();
    if (topic.startsWith(u'$') && (firstFilterLevel == QLatin1String("#") || firstFilterLevel == QLatin1String("+")))
        return false;

    for (int i = 0; i < filterLevels.size(); ++i) {
        const QString& level = filterLevels.at(i);
        if (level == QLatin1String("#"))
            return true;
        if (i >= topicLevels.size())
            return false;
        if (level != QLatin1String("+") && level != topicLevels.at(i))
            return false;
    }
    return filterLevels.size() == topicLevels.size();
}

}
