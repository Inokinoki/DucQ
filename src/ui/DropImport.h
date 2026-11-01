#pragma once

#include <QFileInfo>
#include <QString>

namespace ducq {
namespace DropImport {

bool isSupportedFile(const QString &filePath);

// Sanitized identifier for the auto-created view.
QString viewNameForFile(const QFileInfo &fileInfo);

// "CREATE OR REPLACE VIEW ..." for csv/tsv/txt, parquet and json files;
// empty string for unsupported files. On success outViewName receives the
// view name.
QString buildCreateViewSql(const QString &filePath, QString *outViewName = nullptr);

QString buildPreviewSql(const QString &viewName);

} // namespace DropImport
} // namespace ducq
