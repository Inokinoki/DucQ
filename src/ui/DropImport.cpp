#include "DropImport.h"

#include "core/Catalog.h"

namespace ducq {
namespace DropImport {
namespace {

QString readerForSuffix(const QString &suffix)
{
    if (suffix == QLatin1String("csv") || suffix == QLatin1String("tsv") || suffix == QLatin1String("txt"))
        return QStringLiteral("read_csv");
    if (suffix == QLatin1String("parquet"))
        return QStringLiteral("read_parquet");
    if (suffix == QLatin1String("json") || suffix == QLatin1String("jsonl") || suffix == QLatin1String("ndjson"))
        return QStringLiteral("read_json");
    return QString();
}

QString escapeSingleQuoted(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return value;
}

} // namespace

bool isSupportedFile(const QString &filePath)
{
    return !readerForSuffix(QFileInfo(filePath).suffix().toLower()).isEmpty();
}

QString viewNameForFile(const QFileInfo &fileInfo)
{
    const QString base = fileInfo.completeBaseName();
    QString name;
    name.reserve(base.size());
    for (const QChar c : base) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            name.append(c);
        else
            name.append(QLatin1Char('_'));
    }
    if (name.isEmpty() || name.at(0).isDigit())
        name.prepend(QLatin1Char('v'));
    return name;
}

QString buildCreateViewSql(const QString &filePath, QString *outViewName)
{
    const QFileInfo info(filePath);
    const QString reader = readerForSuffix(info.suffix().toLower());
    if (reader.isEmpty())
        return {};
    const QString viewName = viewNameForFile(info);
    if (outViewName)
        *outViewName = viewName;
    return QStringLiteral("CREATE OR REPLACE VIEW %1 AS SELECT * FROM %2('%3')")
        .arg(quoteIdentifier(viewName), reader, escapeSingleQuoted(info.absoluteFilePath()));
}

QString buildPreviewSql(const QString &viewName)
{
    return QStringLiteral("SELECT * FROM %1 LIMIT 100").arg(quoteIdentifier(viewName));
}

} // namespace DropImport
} // namespace ducq
