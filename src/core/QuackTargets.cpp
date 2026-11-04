#include "QuackTargets.h"

#include <QSettings>

namespace ducq {

namespace {
constexpr char kKey[] = "quack/recentTargets";
}

QStringList QuackTargets::load(QSettings *settings)
{
    return settings->value(QLatin1String(kKey)).toStringList();
}

void QuackTargets::save(QSettings *settings, const QStringList &targets)
{
    if (targets.isEmpty())
        settings->remove(QLatin1String(kKey));
    else
        settings->setValue(QLatin1String(kKey), targets);
}

QStringList QuackTargets::push(QStringList targets, const QString &server)
{
    targets.removeAll(server);
    targets.prepend(server);
    while (targets.size() > kMaxEntries)
        targets.removeLast();
    return targets;
}

} // namespace ducq
