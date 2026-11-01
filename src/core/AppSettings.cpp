#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace ducq {

QSettings *AppSettings::create()
{
#ifdef Q_OS_MAC
    static const QString kPlistPath = [] {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            + QStringLiteral("/Library/Preferences");
        QDir().mkpath(dir);
        return dir + QStringLiteral("/cc.inoki.DucQ.plist");
    }();
    return new QSettings(kPlistPath, QSettings::NativeFormat);
#else
    return new QSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
#endif
}

} // namespace ducq
