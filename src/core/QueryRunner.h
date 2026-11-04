#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>

#include "duckdb.h"
#include "Catalog.h"
#include "ResultCache.h"

namespace ducq {

class Database;

// Cap applied by the UI layer (ResultCache) to the rows shown for one query.
inline constexpr qsizetype kDefaultDisplayRowLimit = 1000000;

// Drives query execution for one database on a dedicated worker thread.
// The runner object must be moved to that thread; all slots then run there
// serially, so one connection suffices. cancel() is the only method meant to
// be called from another thread (it relies on duckdb_interrupt).
class QueryRunner : public QObject {
    Q_OBJECT
public:
    explicit QueryRunner(Database *database, QObject *parent = nullptr);
    ~QueryRunner() override;

    // Thread-safe: sets the cancel flag and interrupts the running query.
    void cancel();

public slots:
    void initialize(); // runs in the worker thread: opens the connection
    void shutdown();   // runs in the worker thread: closes the connection
    void execute(quint64 queryId, const QString &sql);
    void refreshCatalog();
    // Runs SQL synchronously on the worker connection: session bootstrap
    // (ATTACH + CONNECT for a remote session) via DbSession::open, and admin
    // statements (quack_serve/quack_stop) via DbSession::runSync. Meant to be
    // invoked BlockingQueuedConnection; success and the error text land in
    // the members below, which are safe to read once the call returns.
    void bootstrap(const QString &sql);
    bool bootstrapOk() const { return m_bootstrapOk; }
    QString bootstrapError() const { return m_bootstrapError; }

signals:
    // resultPtr points to a heap duckdb_result; the receiver takes ownership.
    void resultReady(quint64 queryId, quintptr resultPtr, const QVector<ducq::ColumnMeta> &columns);
    void finished(quint64 queryId, qint64 elapsedMs, qsizetype totalRows, bool cancelled, const QString &error);
    void progressChanged(quint64 queryId, double percentage);
    void catalogReady(const QVector<ducq::TableInfo> &tables);

private:
    // Runs one prepared statement through the pending API; on success
    // *outResult holds a materialized result owned by the caller.
    duckdb_state runPendingStatement(duckdb_prepared_statement statement, duckdb_result *outResult, quint64 queryId,
                                     const QElapsedTimer &timer, QString *outError, bool *outCancelled);
    void emitProgressThrottled(quint64 queryId, const QElapsedTimer &timer, qint64 &lastEmitMs);
    [[nodiscard]] QVector<ColumnMeta> readColumnMeta(duckdb_result *result) const;

    Database *m_database = nullptr;
    duckdb_connection m_connection = nullptr;
    std::atomic_bool m_cancelRequested { false };
    bool m_bootstrapOk = true;
    QString m_bootstrapError;
};

} // namespace ducq
