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
    // Same, plus SQL run on the worker connection right after it opens (used
    // for remote sessions: ATTACH + CONNECT before any user query) and a
    // label the UI shows for the connection (e.g. "host:port"). When the
    // bootstrap fails the session is left closed and errorMessage carries the
    // server's reply. remoteTarget() mirrors the connection the session was
    // created for, so the UI can dedupe reconnects.
    bool open(const QString &path, const QString &bootstrapSql, const QString &displayName,
              const QString &remoteTarget, QString *errorMessage);
    // Asynchronous variant for remote sessions: the engine opens as in open()
    // (fast, local), but the bootstrap SQL — typically a network ATTACH — is
    // queued on the worker thread instead of blocking the caller. Reports via
    // bootstrapFinished(); the session is only usable once that signal says
    // ok. Failure leaves the session closed (safe to delete).
    bool startOpen(const QString &path, const QString &bootstrapSql, const QString &displayName,
                   const QString &remoteTarget, QString *errorMessage);
    // Stops the worker and closes the database. Safe to call repeatedly.
    void close();
    // Runs a statement synchronously on the worker connection and reports
    // success/error (used for admin statements like quack_serve/quack_stop).
    // Blocks until the statement finishes; callers should ensure the session
    // is idle first.
    bool runSync(const QString &sql, QString *errorMessage);

    bool isOpen() const { return m_database.isOpen(); }
    bool isInMemory() const { return m_database.isInMemory(); }
    const QString &path() const { return m_database.path(); }
    bool isBusy() const { return m_pending > 0; }
    const QString &displayName() const { return m_displayName; }
    const QString &remoteTarget() const { return m_remoteTarget; }

signals:
    // Outcome of startOpen()'s bootstrap; mirrors QueryRunner::bootstrapDone.
    void bootstrapFinished(bool ok, const QString &error);

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
    QString m_displayName;
    QString m_remoteTarget;
};

} // namespace ducq
