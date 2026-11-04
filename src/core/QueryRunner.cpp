#include "QueryRunner.h"

#include "Catalog.h"
#include "Database.h"

#include <QThread>
#include <cmath>

namespace ducq {

QueryRunner::QueryRunner(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{
}

QueryRunner::~QueryRunner()
{
    if (m_connection)
        Database::disconnect(m_connection);
}

void QueryRunner::cancel()
{
    m_cancelRequested = true;
    if (m_connection)
        duckdb_interrupt(m_connection);
}

void QueryRunner::initialize()
{
    if (!m_connection)
        m_connection = m_database->connect();
}

void QueryRunner::shutdown()
{
    if (m_connection) {
        Database::disconnect(m_connection);
        m_connection = nullptr;
    }
}

void QueryRunner::bootstrap(const QString &sql)
{
    m_bootstrapOk = false;
    m_bootstrapError.clear();
    if (!m_connection) {
        m_bootstrapError = tr("Database connection not initialized");
        return;
    }
    duckdb_extracted_statements extracted = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    const idx_t statementCount = duckdb_extract_statements(m_connection, utf8.constData(), &extracted);
    if (statementCount == 0) {
        const char *extractError = duckdb_extract_statements_error(extracted);
        m_bootstrapError = extractError ? QString::fromUtf8(extractError) : tr("Failed to parse the SQL");
    } else {
        for (idx_t i = 0; i < statementCount; ++i) {
            duckdb_prepared_statement statement = nullptr;
            if (duckdb_prepare_extracted_statement(m_connection, extracted, i, &statement) != DuckDBSuccess) {
                const char *prepareError = duckdb_prepare_error(statement);
                m_bootstrapError = tr("Statement %1/%2: %3")
                                       .arg(i + 1)
                                       .arg(int(statementCount))
                                       .arg(prepareError ? QString::fromUtf8(prepareError) : tr("prepare failed"));
                break;
            }
            duckdb_result result {};
            const duckdb_state state = duckdb_execute_prepared(statement, &result);
            if (state != DuckDBSuccess) {
                const char *execError = duckdb_result_error(&result);
                m_bootstrapError = tr("Statement %1/%2: %3")
                                       .arg(i + 1)
                                       .arg(int(statementCount))
                                       .arg(execError ? QString::fromUtf8(execError) : tr("Execution failed"));
            }
            duckdb_destroy_prepare(&statement);
            duckdb_destroy_result(&result);
            if (state != DuckDBSuccess)
                break;
            m_bootstrapOk = true;
        }
    }
    duckdb_destroy_extracted(&extracted);
    if (!m_bootstrapOk && m_bootstrapError.isEmpty())
        m_bootstrapError = tr("Execution failed");
}

void QueryRunner::emitProgressThrottled(quint64 queryId, const QElapsedTimer &timer, qint64 &lastEmitMs)
{
    const qint64 now = timer.elapsed();
    if (now - lastEmitMs < 100)
        return;
    lastEmitMs = now;
    double percentage = 0.0;
    if (m_connection) {
        const duckdb_query_progress_type progress = duckdb_query_progress(m_connection);
        if (!std::isnan(progress.percentage))
            percentage = qBound(0.0, progress.percentage, 100.0);
    }
    emit progressChanged(queryId, percentage);
}

QVector<ColumnMeta> QueryRunner::readColumnMeta(duckdb_result *result) const
{
    QVector<ColumnMeta> columns;
    const idx_t columnCount = duckdb_column_count(result);
    columns.reserve(int(columnCount));
    for (idx_t c = 0; c < columnCount; ++c) {
        ColumnMeta meta;
        const char *name = duckdb_column_name(result, c);
        meta.name = name ? QString::fromUtf8(name) : QString();
        duckdb_logical_type logicalType = duckdb_column_logical_type(result, c);
        meta.typeId = duckdb_get_type_id(logicalType);
        duckdb_destroy_logical_type(&logicalType);
        switch (meta.typeId) {
        case DUCKDB_TYPE_TINYINT:
        case DUCKDB_TYPE_SMALLINT:
        case DUCKDB_TYPE_INTEGER:
        case DUCKDB_TYPE_BIGINT:
        case DUCKDB_TYPE_UTINYINT:
        case DUCKDB_TYPE_USMALLINT:
        case DUCKDB_TYPE_UINTEGER:
        case DUCKDB_TYPE_UBIGINT:
        case DUCKDB_TYPE_HUGEINT:
        case DUCKDB_TYPE_UHUGEINT:
        case DUCKDB_TYPE_FLOAT:
        case DUCKDB_TYPE_DOUBLE:
        case DUCKDB_TYPE_DECIMAL:
            meta.numeric = true;
            break;
        default:
            meta.numeric = false;
            break;
        }
        columns.append(meta);
    }
    return columns;
}

duckdb_state QueryRunner::runPendingStatement(duckdb_prepared_statement statement, duckdb_result *outResult,
                                              quint64 queryId, const QElapsedTimer &timer, QString *outError,
                                              bool *outCancelled)
{
    *outCancelled = false;
    duckdb_pending_result pending = nullptr;
    if (duckdb_pending_prepared(statement, &pending) != DuckDBSuccess) {
        const char *error = duckdb_prepare_error(statement);
        if (outError)
            *outError = error ? QString::fromUtf8(error) : tr("Failed to prepare statement");
        return DuckDBError;
    }

    duckdb_state resultState = DuckDBSuccess;
    qint64 lastEmitMs = timer.elapsed() - 1000;
    for (;;) {
        const duckdb_pending_state state = duckdb_pending_execute_task(pending);
        if (duckdb_pending_execution_is_finished(state)) {
            if (state == DUCKDB_PENDING_ERROR) {
                const char *error = duckdb_pending_error(pending);
                if (outError)
                    *outError = error ? QString::fromUtf8(error) : tr("Execution failed");
                resultState = DuckDBError;
            }
            break;
        }
        if (m_cancelRequested && m_connection)
            duckdb_interrupt(m_connection);
        emitProgressThrottled(queryId, timer, lastEmitMs);
    }

    if (resultState == DuckDBSuccess) {
        resultState = duckdb_execute_pending(pending, outResult);
        if (resultState != DuckDBSuccess) {
            const char *error = duckdb_result_error(outResult);
            if (outError)
                *outError = error ? QString::fromUtf8(error) : tr("Execution failed");
        }
    }
    duckdb_destroy_pending(&pending);

    if (resultState == DuckDBError && m_cancelRequested) {
        // Interrupted by us: report as a cancellation rather than an error.
        if (outError)
            outError->clear();
        *outCancelled = true;
    }
    return resultState;
}

void QueryRunner::execute(quint64 queryId, const QString &sql)
{
    QElapsedTimer timer;
    timer.start();
    // m_cancelRequested is deliberately NOT reset here: a cancel() landing
    // between runSql() and this point must still cancel this query. It is
    // cleared once this execution is done (see the end of this function).

    QString error;
    bool cancelled = false;
    bool hasShownResult = false;
    duckdb_result shownResult {};
    QVector<ColumnMeta> shownColumns;
    qsizetype totalRows = 0;

    if (!m_connection) {
        error = tr("Database connection not initialized");
    } else {
        duckdb_extracted_statements extracted = nullptr;
        const QByteArray utf8 = sql.toUtf8();
        const idx_t statementCount = duckdb_extract_statements(m_connection, utf8.constData(), &extracted);
        if (statementCount == 0) {
            const char *extractError = duckdb_extract_statements_error(extracted);
            error = extractError ? QString::fromUtf8(extractError) : tr("Failed to parse the SQL");
        } else {
            for (idx_t i = 0; i < statementCount; ++i) {
                if (m_cancelRequested) {
                    cancelled = true;
                    break;
                }
                duckdb_prepared_statement statement = nullptr;
                if (duckdb_prepare_extracted_statement(m_connection, extracted, i, &statement) != DuckDBSuccess) {
                    const char *prepareError = duckdb_prepare_error(statement);
                    error = tr("Statement %1/%2: %3")
                                .arg(i + 1)
                                .arg(int(statementCount))
                                .arg(prepareError ? QString::fromUtf8(prepareError) : tr("prepare failed"));
                    break;
                }
                duckdb_result statementResult {};
                QString statementError;
                bool statementCancelled = false;
                const duckdb_state state = runPendingStatement(statement, &statementResult, queryId, timer,
                                                               &statementError, &statementCancelled);
                duckdb_destroy_prepare(&statement);
                if (state != DuckDBSuccess) {
                    error = tr("Statement %1/%2: %3")
                                .arg(i + 1)
                                .arg(int(statementCount))
                                .arg(statementError.isEmpty() ? tr("Execution failed") : statementError);
                    cancelled = statementCancelled;
                    if (cancelled)
                        error.clear();
                    break;
                }
                // Show the last row-producing statement of the script.
                if (hasShownResult)
                    duckdb_destroy_result(&shownResult);
                shownResult = statementResult;
                hasShownResult = true;
                shownColumns = readColumnMeta(&shownResult);
                totalRows = qsizetype(duckdb_row_count(&shownResult));
            }
        }
        duckdb_destroy_extracted(&extracted);
    }

    if (hasShownResult) {
        auto *heapResult = new duckdb_result(shownResult);
        shownResult = duckdb_result {};
        emit resultReady(queryId, reinterpret_cast<quintptr>(heapResult), shownColumns);
    }
    emit finished(queryId, timer.elapsed(), totalRows, cancelled, error);
    m_cancelRequested = false;
}

void QueryRunner::refreshCatalog()
{
    QVector<TableInfo> tables;
    if (m_connection)
        tables = readCatalog(m_connection);
    emit catalogReady(tables);
}

} // namespace ducq
