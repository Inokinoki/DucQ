#include "HistoryStore.h"

#include <QSettings>

namespace ducq {

namespace {
constexpr char kKey[] = "history";
}

QStringList HistoryStore::load(QSettings *settings)
{
    return settings->value(QLatin1String(kKey)).toStringList();
}

void HistoryStore::save(QSettings *settings, const QStringList &history)
{
    if (history.isEmpty())
        settings->remove(QLatin1String(kKey));
    else
        settings->setValue(QLatin1String(kKey), history);
}

QStringList HistoryStore::push(QStringList history, const QString &sql)
{
    history.removeAll(sql);
    history.prepend(sql);
    while (history.size() > kMaxEntries)
        history.removeLast();
    return history;
}

} // namespace ducq
