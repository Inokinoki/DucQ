#include "Database.h"

#include "StaticExtensions.h"

namespace ducq {

bool Database::open(const QString &path, bool readOnly, QString *errorMessage)
{
    close();
    registerStaticExtensions();

    duckdb_config config = nullptr;
    if (readOnly) {
        if (duckdb_create_config(&config) != DuckDBSuccess) {
            if (errorMessage)
                *errorMessage = QStringLiteral("无法创建数据库配置");
            return false;
        }
        duckdb_set_config(config, "access_mode", "READ_ONLY");
    }

    const QByteArray utf8 = path.toUtf8();
    char *openError = nullptr;
    const duckdb_state state = duckdb_open_ext(path.isEmpty() ? nullptr : utf8.constData(), &m_db, config,
                                               &openError);
    duckdb_destroy_config(&config);
    if (state != DuckDBSuccess) {
        if (errorMessage)
            *errorMessage = openError ? QString::fromUtf8(openError) : QStringLiteral("无法打开数据库");
        if (openError)
            duckdb_free(openError);
        m_db = nullptr;
        return false;
    }
    m_path = path;
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
