#include "Database.h"

#include "StaticExtensions.h"
#ifdef DUCQ_DYNAMIC_EXTENSIONS
#include "ExtensionLoader.h"
#endif

#include <QObject>

namespace ducq {

bool Database::open(const QString &path, bool readOnly, QString *errorMessage)
{
    close();
    registerStaticExtensions();

    duckdb_config config = nullptr;
    bool ownConfig = false;
    if (readOnly) {
        if (duckdb_create_config(&config) != DuckDBSuccess) {
            if (errorMessage)
                *errorMessage = QObject::tr("Failed to create the database configuration");
            return false;
        }
        duckdb_set_config(config, "access_mode", "READ_ONLY");
    }
#ifdef DUCQ_DYNAMIC_EXTENSIONS
    // allow_unsigned_extensions is startup-scoped: it must be set on the
    // config, a later SET on a connection is rejected as locked. Community
    // extensions are unsigned; official ones still pass signature checks, so
    // this only widens what can be loaded.
    if (!config) {
        if (duckdb_create_config(&config) != DuckDBSuccess) {
            if (errorMessage)
                *errorMessage = QObject::tr("Failed to create the database configuration");
            return false;
        }
        ownConfig = true;
    }
    duckdb_set_config(config, "allow_unsigned_extensions", "true");
#endif

    const QByteArray utf8 = path.toUtf8();
    char *openError = nullptr;
    const duckdb_state state = duckdb_open_ext(path.isEmpty() ? nullptr : utf8.constData(), &m_db, config,
                                              &openError);
    duckdb_destroy_config(&config);
    ownConfig = false; // config consumed above either way
    if (state != DuckDBSuccess) {
        if (errorMessage)
            *errorMessage = openError ? QString::fromUtf8(openError)
                                      : QObject::tr("Failed to open the database");
        if (openError)
            duckdb_free(openError);
        m_db = nullptr;
        return false;
    }
    m_path = path;
#ifdef DUCQ_DYNAMIC_EXTENSIONS
    // Pick up any *.duckdb_extension shipped next to the app or installed
    // into DuckDB's standard directory.
    ExtensionLoader::loadAll(m_db);
#endif
    return true;
}

void Database::close()
{
    if (m_db) {
        duckdb_close(&m_db);
        m_db = nullptr;
    }
    m_path.clear();
}

duckdb_connection Database::connect() const
{
    duckdb_connection connection = nullptr;
    if (m_db)
        duckdb_connect(m_db, &connection);
    return connection;
}

void Database::disconnect(duckdb_connection connection)
{
    if (connection)
        duckdb_disconnect(&connection);
}

} // namespace ducq
