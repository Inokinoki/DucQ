#include "ResultCache.h"

namespace ducq {

void ResultCache::destroyResult(duckdb_result *result)
{
    if (!result)
        return;
    duckdb_destroy_result(result);
    delete result;
}

void ResultCache::reset(duckdb_result *result, const QVector<ColumnMeta> &columns, qsizetype displayLimit)
{
    clear();
    m_columns = columns;
    if (!result)
        return;
    m_result = *result;
    *result = duckdb_result {};
    m_hasResult = true;
    m_totalRows = qsizetype(duckdb_row_count(&m_result));
    m_visibleRows = qMin(m_totalRows, displayLimit);
    m_truncated = m_totalRows > m_visibleRows;
}

void ResultCache::clear()
{
    if (m_hasResult) {
        duckdb_destroy_result(&m_result);
        m_hasResult = false;
    }
    m_result = duckdb_result {};
    m_columns.clear();
    m_totalRows = 0;
    m_visibleRows = 0;
    m_truncated = false;
}

bool ResultCache::isNull(int row, int column) const
{
    if (!m_hasResult || column < 0 || column >= m_columns.size() || row < 0 || row >= m_visibleRows)
        return false;
    // The accessors take a mutable pointer by signature; they never mutate.
    auto *result = const_cast<duckdb_result *>(&m_result);
    return duckdb_value_is_null(result, idx_t(column), idx_t(row));
}

QVariant ResultCache::cell(int row, int column) const
{
    if (!m_hasResult || column < 0 || column >= m_columns.size() || row < 0 || row >= m_visibleRows)
        return {};
    if (isNull(row, column))
        return {};

    auto *result = const_cast<duckdb_result *>(&m_result);
    const idx_t col = idx_t(column);
    const idx_t r = idx_t(row);
    const duckdb_type type = m_columns.at(column).typeId;
    switch (type) {
    case DUCKDB_TYPE_BOOLEAN:
        return QVariant(duckdb_value_boolean(result, col, r));
    case DUCKDB_TYPE_TINYINT:
        return QVariant(int(duckdb_value_int8(result, col, r)));
    case DUCKDB_TYPE_SMALLINT:
        return QVariant(int(duckdb_value_int16(result, col, r)));
    case DUCKDB_TYPE_INTEGER:
        return QVariant(duckdb_value_int32(result, col, r));
    case DUCKDB_TYPE_BIGINT:
        return QVariant(qint64(duckdb_value_int64(result, col, r)));
    case DUCKDB_TYPE_UTINYINT:
        return QVariant(uint(duckdb_value_uint8(result, col, r)));
    case DUCKDB_TYPE_USMALLINT:
        return QVariant(uint(duckdb_value_uint16(result, col, r)));
    case DUCKDB_TYPE_UINTEGER:
        return QVariant(duckdb_value_uint32(result, col, r));
    case DUCKDB_TYPE_UBIGINT:
        return QVariant(quint64(duckdb_value_uint64(result, col, r)));
    case DUCKDB_TYPE_FLOAT:
        return QVariant(duckdb_value_float(result, col, r));
    case DUCKDB_TYPE_DOUBLE:
        return QVariant(duckdb_value_double(result, col, r));
    default:
        break;
    }
    char *text = duckdb_value_varchar(result, col, r);
    const QString value = text ? QString::fromUtf8(text) : QString();
    if (text)
        duckdb_free(text);
    return value;
}

} // namespace ducq
