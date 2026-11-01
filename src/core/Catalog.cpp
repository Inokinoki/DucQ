#include "Catalog.h"

#include <QHash>
#include <QPair>

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
        "SELECT table_schema, table_name, table_type FROM information_schema.tables "
        "WHERE table_schema NOT IN ('information_schema', 'pg_catalog') "
        "ORDER BY table_schema, table_name";
    if (duckdb_query(connection, tablesSql, &tablesResult) != DuckDBSuccess) {
        duckdb_destroy_result(&tablesResult);
        return tables;
    }
    const idx_t tableRowCount = duckdb_row_count(&tablesResult);
    tables.reserve(qsizetype(tableRowCount));
    QHash<QPair<QString, QString>, qsizetype> tableIndex;
    for (idx_t r = 0; r < tableRowCount; ++r) {
        TableInfo table;
        table.schema = valueString(&tablesResult, 0, r);
        table.name = valueString(&tablesResult, 1, r);
        table.type = valueString(&tablesResult, 2, r);
        tableIndex.insert({ table.schema, table.name }, tables.size());
        tables.append(table);
    }
    duckdb_destroy_result(&tablesResult);

    duckdb_result columnsResult;
    const char *columnsSql =
        "SELECT table_schema, table_name, column_name, data_type FROM information_schema.columns "
        "WHERE table_schema NOT IN ('information_schema', 'pg_catalog') "
        "ORDER BY table_schema, table_name, ordinal_position";
    if (duckdb_query(connection, columnsSql, &columnsResult) == DuckDBSuccess) {
        const idx_t columnRowCount = duckdb_row_count(&columnsResult);
        for (idx_t r = 0; r < columnRowCount; ++r) {
            const QString schema = valueString(&columnsResult, 0, r);
            const QString tableName = valueString(&columnsResult, 1, r);
            const auto it = tableIndex.constFind({ schema, tableName });
            if (it == tableIndex.cend())
                continue;
            ColumnInfo column;
            column.name = valueString(&columnsResult, 2, r);
            column.typeName = valueString(&columnsResult, 3, r);
            tables[*it].columns.append(column);
        }
    }
    duckdb_destroy_result(&columnsResult);
    return tables;
}

} // namespace ducq
