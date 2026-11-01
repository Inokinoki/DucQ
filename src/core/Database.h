#pragma once

#include <QString>

#include "duckdb.h"

namespace ducq {

// RAII wrapper around a duckdb_database instance. The handle is safe to share
// across threads; create one duckdb_connection per using thread via connect().
class Database {
public:
    Database() = default;
    ~Database() { close(); }

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Opens a database file; an empty path opens a transient in-memory
    // database. Registers the statically linked extensions on first use.
    bool open(const QString &path, bool readOnly, QString *errorMessage);
    void close();

    bool isOpen() const { return m_db != nullptr; }
    bool isInMemory() const { return m_path.isEmpty(); }
    const QString &path() const { return m_path; }
    duckdb_database handle() const { return m_db; }

    // Creates a new connection; call from the thread that will use it.
    duckdb_connection connect() const;
    static void disconnect(duckdb_connection connection);

private:
    duckdb_database m_db = nullptr;
    QString m_path;
};

} // namespace ducq
