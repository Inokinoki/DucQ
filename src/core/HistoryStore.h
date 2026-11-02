#pragma once

#include <QStringList>

class QSettings;

namespace ducq {

// Persistent query history: most-recent-first, deduplicated, capped. Stored
// as a plain string array under the "history" key so it survives restarts.
class HistoryStore {
public:
    static constexpr int kMaxEntries = 50;

    static QStringList load(QSettings *settings);
    static void save(QSettings *settings, const QStringList &history);
    // Moves the entry to the front (deduplicated, capped); returns the list.
    static QStringList push(QStringList history, const QString &sql);
};

} // namespace ducq
