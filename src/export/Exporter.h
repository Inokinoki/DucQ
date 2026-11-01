#pragma once

#include <QString>

namespace ducq {
namespace Exporter {

// Builds a COPY (<query>) TO '<file>' statement; format is "CSV" or "PARQUET".
QString buildCopySql(QString querySql, const QString &filePath, const QString &format);

} // namespace Exporter
} // namespace ducq
