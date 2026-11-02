#pragma once

#include <QObject>
#include <QString>

#include "core/Database.h"

class QThread;

namespace ducq {

class QueryRunner;

// One open database together with its dedicated worker thread. Sessions are
// fully independent: a long query in one never blocks another, and closing
// one never touches the others.
class DbSession : public QObject {
    Q_OBJECT
public:
    explicit DbSession(int id, QObject *parent = nullptr);
    ~DbSession() override;

    int id() const { return m_id; }
    QueryRunner *runner() const { return m_runner; }

    // Opens the database and starts the worker thread; an empty path opens a
    // transient in-memory database.
    bool open(const QString &path, QString *errorMessage);
    // Stops the worker and closes the database. Safe to call repeatedly.
    void close();

    bool isOpen() const { return m_database.isOpen(); }
    bool isInMemory() const { return m_database.isInMemory(); }
    const QString &path() const { return m_database.path(); }
    bool isBusy() const { return m_pending > 0; }

public slots:
    void runSql(quint64 queryId, const QString &sql);
    void refreshCatalog();
    void cancel();

private:
    int m_id;
    Database m_database;
    QThread *m_thread = nullptr;
    QueryRunner *m_runner = nullptr;
    int m_pending = 0;
};

} // namespace ducq
