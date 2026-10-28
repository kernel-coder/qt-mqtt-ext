#include "MqttExt/Topic.h"

#include <QtTest>

using namespace MqttExt;

class TopicTest : public QObject
{
    Q_OBJECT

private slots:
    void validatesTopicNames_data()
    {
        QTest::addColumn<QString>("topic");
        QTest::addColumn<bool>("valid");

        QTest::newRow("single level") << "home" << true;
        QTest::newRow("multi level") << "devices/sn/live" << true;
        QTest::newRow("empty levels") << "/home//live/" << true;
        QTest::newRow("unicode") << QStringLiteral("home/café") << true;
        QTest::newRow("system topic") << "$SYS/broker/version" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("plus wildcard") << "home/+/live" << false;
        QTest::newRow("hash wildcard") << "home/#" << false;
        QTest::newRow("embedded null") << QString(QStringLiteral("home") + QChar(u'\0') + QStringLiteral("x")) << false;
        QTest::newRow("too long") << QString(65536, u'a') << false;
        QTest::newRow("max length") << QString(65535, u'a') << true;
    }

    void validatesTopicNames()
    {
        QFETCH(QString, topic);
        QFETCH(bool, valid);
        QCOMPARE(isValidTopicName(topic), valid);
    }

    void validatesTopicFilters_data()
    {
        QTest::addColumn<QString>("filter");
        QTest::addColumn<bool>("valid");

        QTest::newRow("plain") << "home/live" << true;
        QTest::newRow("hash alone") << "#" << true;
        QTest::newRow("plus alone") << "+" << true;
        QTest::newRow("trailing hash") << "home/v1/#" << true;
        QTest::newRow("plus levels") << "home/+/devices/+" << true;
        QTest::newRow("leading separator plus") << "/+" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("hash not last") << "home/#/live" << false;
        QTest::newRow("hash inside level") << "home/live#" << false;
        QTest::newRow("plus inside level") << "home/li+ve" << false;
        QTest::newRow("double hash") << "home/##" << false;
    }

    void validatesTopicFilters()
    {
        QFETCH(QString, filter);
        QFETCH(bool, valid);
        QCOMPARE(isValidTopicFilter(filter), valid);
    }

    void matchesTopics_data()
    {
        QTest::addColumn<QString>("filter");
        QTest::addColumn<QString>("topic");
        QTest::addColumn<bool>("matches");

        QTest::newRow("exact") << "a/b" << "a/b" << true;
        QTest::newRow("different") << "a/b" << "a/c" << false;
        QTest::newRow("longer topic") << "a/b" << "a/b/c" << false;
        QTest::newRow("shorter topic") << "a/b/c" << "a/b" << false;
        QTest::newRow("plus") << "a/+/c" << "a/b/c" << true;
        QTest::newRow("plus empty level") << "a/+/c" << "a//c" << true;
        QTest::newRow("plus does not span") << "a/+" << "a/b/c" << false;
        QTest::newRow("hash children") << "a/#" << "a/b/c" << true;
        QTest::newRow("hash includes parent") << "a/#" << "a" << true;
        QTest::newRow("hash everything") << "#" << "a/b" << true;
        QTest::newRow("hash skips system") << "#" << "$SYS/x" << false;
        QTest::newRow("plus skips system") << "+/x" << "$SYS/x" << false;
        QTest::newRow("explicit system") << "$SYS/#" << "$SYS/x" << true;
        QTest::newRow("leading separator") << "+/a" << "/a" << true;
        QTest::newRow("case sensitive") << "a/B" << "a/b" << false;
        QTest::newRow("invalid filter") << "a/#/b" << "a/x/b" << false;
        QTest::newRow("invalid topic") << "a/+" << "a/+" << false;
    }

    void matchesTopics()
    {
        QFETCH(QString, filter);
        QFETCH(QString, topic);
        QFETCH(bool, matches);
        QCOMPARE(topicMatches(filter, topic), matches);
    }
};

QTEST_GUILESS_MAIN(TopicTest)
#include "TopicTest.moc"
