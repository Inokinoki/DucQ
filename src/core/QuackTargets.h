#pragma once

#include <QStringList>

class QSettings;

namespace ducq {

// Recently used Quack server addresses ("host[:port]"), most-recent-first,
// deduplicated, capped. Only the address is stored — tokens never touch disk.
class QuackTargets {
public:
    static constexpr int kMaxEntries = 10;

    static QStringList load(QSettings *settings);
    static void save(QSettings *settings, const QStringList &targets);
    // Moves the entry to the front (deduplicated, capped); returns the list.
    static QStringList push(QStringList targets, const QString &server);
};

} // namespace ducq
