#include "Exporter.h"

#include "core/Catalog.h"

namespace ducq {
namespace Exporter {

QString buildCopySql(QString querySql, const QString &filePath, const QString &format)
{
    // Drop a trailing semicolon so the statement can be wrapped in COPY ().
    while (querySql.endsWith(QLatin1Char(';')) || querySql.endsWith(QLatin1Char('\n'))
           || querySql.endsWith(QLatin1Char(' ')) || querySql.endsWith(QLatin1Char('\t'))) {
        querySql.chop(1);
    }
    const QString options = format.compare(QLatin1String("PARQUET"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("FORMAT PARQUET")
        : QStringLiteral("FORMAT CSV, HEADER");
    return QStringLiteral("COPY (%1) TO '%2' (%3)").arg(querySql, escapeSingleQuoted(filePath), options);
}

} // namespace Exporter
} // namespace ducq
