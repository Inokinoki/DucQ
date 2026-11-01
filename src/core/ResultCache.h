#pragma once

#include <QMetaType>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QtGlobal>

#include "duckdb.h"

namespace ducq {

struct ColumnMeta {
    QString name;
    duckdb_type typeId = DUCKDB_TYPE_INVALID;
    bool numeric = false;
};

// Owns a materialized duckdb_result and serves cell values to the UI lazily.
// All methods must be called from the thread that owns the cache (the UI
// thread): the result is produced on a worker thread and handed over through
// a queued signal. Uses the result-based duckdb_value_* accessors, which are
// safe for any internal vector layout.
class ResultCache {
public:
    ResultCache() = default;
    ~ResultCache() { clear(); }

    ResultCache(const ResultCache &) = delete;
    ResultCache &operator=(const ResultCache &) = delete;

    // Takes ownership of a heap-allocated duckdb_result (or nullptr to clear).
    // displayLimit caps how many rows the model exposes.
    void reset(duckdb_result *result, const QVector<ColumnMeta> &columns, qsizetype displayLimit);
    void clear();

    // Destroys a result that was handed over but could not be delivered.
    static void destroyResult(duckdb_result *result);

    qsizetype rowCount() const { return m_visibleRows; }
    qsizetype totalRowCount() const { return m_totalRows; }
    int columnCount() const { return m_columns.size(); }
    const QVector<ColumnMeta> &columns() const { return m_columns; }
    bool truncated() const { return m_truncated; }
    bool hasResult() const { return m_hasResult; }

    bool isNull(int row, int column) const;
    QVariant cell(int row, int column) const;

private:
    duckdb_result m_result {};
    bool m_hasResult = false;
    QVector<ColumnMeta> m_columns;
    qsizetype m_totalRows = 0;
    qsizetype m_visibleRows = 0;
    bool m_truncated = false;
};

} // namespace ducq

Q_DECLARE_METATYPE(ducq::ColumnMeta)
Q_DECLARE_METATYPE(QVector<ducq::ColumnMeta>)
