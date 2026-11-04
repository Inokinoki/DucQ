#include "Catalog.h"

#include <QHash>

namespace ducq {
namespace {

QString valueString(duckdb_result *result, idx_t column, idx_t row)
{
    char *text = duckdb_value_varchar(result, column, row);
    const QString value = text ? QString::fromUtf8(text) : QString();
    if (text)
        duckdb_free(text);
    return value;
}

} // namespace

QVector<TableInfo> readCatalog(duckdb_connection connection)
{
    QVector<TableInfo> tables;

    duckdb_result tablesResult;
    const char *tablesSql =
        "SELECT table_catalog, table_schema, table_name, table_type FROM information_schema.tables "
        "WHERE table_schema NOT IN ('information_schema', 'pg_catalog') "
        "ORDER BY table_schema, table_name";
    if (duckdb_query(connection, tablesSql, &tablesResult) != DuckDBSuccess) {
        duckdb_destroy_result(&tablesResult);
        return tables;
    }
    const idx_t tableRowCount = duckdb_row_count(&tablesResult);
    tables.reserve(qsizetype(tableRowCount));
    // Key: catalog + unit-separator + schema + unit-separator + name (the unit
    // separator never appears in identifiers, so the join is unambiguous).
    const QChar kSep = QLatin1Char('\x1f');
    auto tableKey = [kSep](const QString &catalog, const QString &schema, const QString &name) {
        return catalog + kSep + schema + kSep + name;
    };
    QHash<QString, qsizetype> tableIndex;
    for (idx_t r = 0; r < tableRowCount; ++r) {
        TableInfo table;
        table.catalog = valueString(&tablesResult, 0, r);
        table.schema = valueString(&tablesResult, 1, r);
        table.name = valueString(&tablesResult, 2, r);
        table.type = valueString(&tablesResult, 3, r);
        tableIndex.insert(tableKey(table.catalog, table.schema, table.name), tables.size());
        tables.append(table);
    }
    duckdb_destroy_result(&tablesResult);

    duckdb_result columnsResult;
    const char *columnsSql =
        "SELECT table_catalog, table_schema, table_name, column_name, data_type FROM information_schema.columns "
        "WHERE table_schema NOT IN ('information_schema', 'pg_catalog') "
        "ORDER BY table_catalog, table_schema, table_name, ordinal_position";
    if (duckdb_query(connection, columnsSql, &columnsResult) == DuckDBSuccess) {
        const idx_t columnRowCount = duckdb_row_count(&columnsResult);
        for (idx_t r = 0; r < columnRowCount; ++r) {
            const QString catalog = valueString(&columnsResult, 0, r);
            const QString schema = valueString(&columnsResult, 1, r);
            const QString tableName = valueString(&columnsResult, 2, r);
            const auto it = tableIndex.constFind(tableKey(catalog, schema, tableName));
            if (it == tableIndex.cend())
                continue;
            ColumnInfo column;
            column.name = valueString(&columnsResult, 3, r);
            column.typeName = valueString(&columnsResult, 4, r);
            tables[*it].columns.append(column);
        }
    }
    duckdb_destroy_result(&columnsResult);
    return tables;
}

} // namespace ducq
