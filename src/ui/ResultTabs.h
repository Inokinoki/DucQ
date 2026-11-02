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
    int sessionId = 0;
    quint64 queryId = 0;
    int number = 0; // stable display number, used for retranslation
    QString sql;
    ResultModel *model = nullptr;
    QTableView *view = nullptr;
    QLabel *statusLabel = nullptr;
    bool finished = false;
    bool cancelled = false;
    QString error;
    qint64 elapsedMs = 0;
    qsizetype totalRows = 0;
};

// One tab per executed query: model + table view + a status line. Results are
// routed by query id; tabs closed before delivery cause the result to be
// destroyed by the sender side.
class ResultTabs : public QTabWidget {
    Q_OBJECT
public:
    explicit ResultTabs(qsizetype displayRowLimit, QWidget *parent = nullptr);

    void startQueryTab(int sessionId, quint64 queryId, const QString &sql);
    // Takes ownership of resultPtr when a live tab exists (matching session
    // and query); returns false otherwise so the caller can destroy it.
    bool deliverResult(int sessionId, quint64 queryId, quintptr resultPtr,
                       const QVector<ducq::ColumnMeta> &columns);
    // Displayed-row counts come from the tab's ResultCache (single source of
    // truth for the display cap); totalRows comes from the engine.
    void markFinished(quint64 queryId, qint64 elapsedMs, qsizetype totalRows, bool cancelled, const QString &error);
    const ResultTab *currentTab() const;
    void clearFinished();
    void closeSessionTabs(int sessionId);
    void retranslateUi();
    // Re-applies theme-dependent colors to the per-tab status lines.
    void retheme();

private:
    void renderStatus(ResultTab *tab);
    ResultTab *tabFor(quint64 queryId);

    QHash<quint64, ResultTab *> m_tabs;
    QHash<QWidget *, ResultTab *> m_tabsByWidget;
    qsizetype m_displayRowLimit;
    int m_counter = 0;
};

} // namespace ducq
