#pragma once
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

// Local bounded diagnostics: counts/state only, no input content or screenshots.
inline void mouseDiagnostic(const QString &message)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/QtScrcpy";
    QDir().mkpath(directory);
    QFile file(directory + "/mouse-diagnostics.log");
    const bool rotate = file.size() > 1024 * 1024;
    if (file.open(QIODevice::WriteOnly | (rotate ? QIODevice::Truncate : QIODevice::Append))) {
        file.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " " + message + "\n").toUtf8());
    }
}
