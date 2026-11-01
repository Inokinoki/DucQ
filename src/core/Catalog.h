#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

#include "duckdb.h"

namespace ducq {

struct ColumnInfo {
    QString name;
    QString typeName;
};

struct TableInfo {
    QString schema;
    QString name;
    QString type;
    QVector<ColumnInfo> columns;
};

// Reads tables/views and their columns from information_schema. Expects to be
// called on the worker thread (any blocking query connection).
QVector<TableInfo> readCatalog(duckdb_connection connection);

inline QString quoteIdentifier(const QString &name)
{
    QString escaped = name;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

} // namespace ducq

Q_DECLARE_METATYPE(ducq::TableInfo)
Q_DECLARE_METATYPE(QVector<ducq::TableInfo>)
