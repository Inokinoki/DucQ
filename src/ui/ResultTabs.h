#pragma once

#include <QHash>
#include <QTabWidget>
#include <QVector>

#include "core/ResultCache.h"

class QTableView;
class QLabel;

namespace ducq {

class ResultModel;

struct ResultTab {
    quint64 queryId = 0;
    QString sql;
    ResultModel *model = nullptr;
    QTableView *view = nullptr;
    QLabel *statusLabel = nullptr;
    bool finished = false;
};

// One tab per executed query: model + table view + a status line. Results are
// routed by query id; tabs closed before delivery cause the result to be
// destroyed by the sender side.
class ResultTabs : public QTabWidget {
    Q_OBJECT
public:
    explicit ResultTabs(qsizetype displayRowLimit, QWidget *parent = nullptr);

    void startQueryTab(quint64 queryId, const QString &sql);
    // Takes ownership of resultPtr when a live tab exists; returns false
    // otherwise so the caller can destroy it.
    bool deliverResult(quint64 queryId, quintptr resultPtr, const QVector<ducq::ColumnMeta> &columns);
    // Displayed-row counts come from the tab's ResultCache (single source of
    // truth for the display cap); totalRows comes from the engine.
    void markFinished(quint64 queryId, qint64 elapsedMs, qsizetype totalRows, bool cancelled, const QString &error);
    const ResultTab *currentTab() const;
    void clearFinished();

private:
    ResultTab *tabFor(quint64 queryId);

    QHash<quint64, ResultTab *> m_tabs;
    QHash<QWidget *, ResultTab *> m_tabsByWidget;
    qsizetype m_displayRowLimit;
    int m_counter = 0;
};

} // namespace ducq
