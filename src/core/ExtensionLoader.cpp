#include "ExtensionLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>

#include "duckdb.h"

namespace ducq {
namespace ExtensionLoader {

namespace {

// Runs a statement, discarding the result.
bool run(duckdb_connection connection, const QString &sql)
{
    duckdb_result result;
    const duckdb_state state = duckdb_query(connection, sql.toUtf8().constData(), &result);
    duckdb_destroy_result(&result);
    return state == DuckDBSuccess;
}

// LOAD takes a path literal; double any single quotes in it.
QString quoteLiteral(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

// Extension name from a file name: foo.duckdb_extension -> foo
QString nameForFile(const QString &fileName)
{
    static const QString suffix = QStringLiteral(".duckdb_extension");
    if (fileName.endsWith(suffix))
        return fileName.left(fileName.size() - suffix.size());
    return fileName;
}

// Collects *.duckdb_extension files, recursively (DuckDB's own layout is
// <home>/extensions/<version>/<platform>/<name>.duckdb_extension).
QStringList findExtensionFiles(const QString &root)
{
    QStringList found;
    QDirIterator it(root, QStringList { QStringLiteral("*.duckdb_extension") }, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        found << it.next();
    return found;
}

} // namespace

QStringList searchPaths()
{
    QStringList paths;
    // Next to the executable: the place to ship extensions with the app.
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty())
        paths << appDir + QStringLiteral("/extensions");
    // DuckDB's standard install directory.
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!home.isEmpty())
        paths << home + QStringLiteral("/.duckdb/extensions");
    return paths;
}

QStringList loadAll(duckdb_database database)
{
    if (!database)
        return {};

    QStringList loaded;
    duckdb_connection connection = nullptr;
    if (duckdb_connect(database, &connection) != DuckDBSuccess)
        return loaded;
    // allow_unsigned_extensions was set on the database config at open time
    // (startup-scoped setting); official extensions still pass signature
    // checks.

    QStringList seen;
    for (const QString &root : searchPaths()) {
        const QDir dir(root);
        if (!dir.exists())
            continue;
        for (const QString &file : findExtensionFiles(root)) {
            const QString name = nameForFile(QFileInfo(file).fileName());
            if (seen.contains(name))
                continue;
            seen << name;
            if (run(connection, QStringLiteral("LOAD ") + quoteLiteral(file)))
                loaded << name;
        }
    }
    duckdb_disconnect(&connection);
    return loaded;
}

} // namespace ExtensionLoader
} // namespace ducq
