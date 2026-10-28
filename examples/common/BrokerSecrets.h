#pragma once

#include <QDir>
#include <QFile>
#include <QString>

namespace BrokerSecrets {

inline QString directory()
{
    return QDir::cleanPath(QStringLiteral(SIMULATOR_SECRETS_DIR));
}

inline QString passwordFor(const QString& username)
{
    QFile file(directory() + QStringLiteral("/credentials.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const int separator = line.indexOf(u':');
        if (separator > 0 && line.left(separator) == username)
            return line.mid(separator + 1);
    }
    return {};
}

inline QString caCertificatesFile()
{
    const QString path = directory() + QStringLiteral("/certs/ca.crt");
    return QFile::exists(path) ? path : QString();
}

}
