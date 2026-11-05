#include "DbSession.h"

#include "QueryRunner.h"

#include <QMetaObject>
#include <QThread>

namespace ducq {

DbSession::DbSession(int id, QObject *parent)
    : QObject(parent)
    , m_id(id)
{
}

DbSession::~DbSession()
{
    close();
}

bool DbSession::open(const QString &path, QString *errorMessage)
{
    return open(path, QString(), QString(), QString(), errorMessage);
}

bool DbSession::open(const QString &path, const QString &bootstrapSql, const QString &displayName,
                     const QString &remoteTarget, QString *errorMessage)
{
    close();
    if (!m_database.open(path, false, errorMessage))
        return false;

    m_thread = new QThread(this);
    m_runner = new QueryRunner(&m_database); // no parent: lives in the worker
    m_runner->moveToThread(m_thread);
    connect(m_runner, &QueryRunner::finished, this,
            [this](quint64, qint64, qsizetype, bool, const QString &) { --m_pending; });
    m_thread->start();
    QMetaObject::invokeMethod(m_runner, "initialize", Qt::BlockingQueuedConnection);
    if (!bootstrapSql.isEmpty()) {
        QMetaObject::invokeMethod(m_runner, "bootstrap", Qt::BlockingQueuedConnection,
                                  Q_ARG(QString, bootstrapSql));
        if (!m_runner->bootstrapOk()) {
            if (errorMessage)
                *errorMessage = m_runner->bootstrapError();
            close();
            return false;
        }
    }
    m_displayName = displayName;
    m_remoteTarget = remoteTarget;
    return true;
}

bool DbSession::startOpen(const QString &path, const QString &bootstrapSql, const QString &displayName,
                          const QString &remoteTarget, QString *errorMessage)
{
    close();
    if (!m_database.open(path, false, errorMessage))
        return false;

    m_thread = new QThread(this);
    m_runner = new QueryRunner(&m_database); // no parent: lives in the worker
    m_runner->moveToThread(m_thread);
    connect(m_runner, &QueryRunner::finished, this,
            [this](quint64, qint64, qsizetype, bool, const QString &) { --m_pending; });
    connect(m_runner, &QueryRunner::bootstrapDone, this, &DbSession::onBootstrapDone);
    m_thread->start();
    QMetaObject::invokeMethod(m_runner, "initialize", Qt::BlockingQueuedConnection);
    // Unlike open(), the bootstrap SQL is queued, not awaited: this call
    // returns before the ATTACH has even started, and bootstrapFinished
    // carries the outcome once the worker gets to it.
    m_bootstrapSql = bootstrapSql;
    m_bootstrapping = true;
    QMetaObject::invokeMethod(m_runner, "bootstrapAsync", Qt::QueuedConnection,
                              Q_ARG(QString, bootstrapSql));
    m_displayName = displayName;
    m_remoteTarget = remoteTarget;
    m_bootstrapSql = bootstrapSql;
    return true;
}

void DbSession::onBootstrapDone(bool ok, const QString &error)
{
    m_bootstrapping = false;
    emit bootstrapFinished(ok, error);
}

bool DbSession::runSync(const QString &sql, QString *errorMessage)
{
    if (!m_runner) {
        if (errorMessage)
            *errorMessage = tr("Session is not open");
        return false;
    }
    QMetaObject::invokeMethod(m_runner, "bootstrap", Qt::BlockingQueuedConnection,
                              Q_ARG(QString, sql));
    if (m_runner->bootstrapOk())
        return true;
    if (errorMessage)
        *errorMessage = m_runner->bootstrapError();
    return false;
}

void DbSession::close()
{
    if (m_runner) {
        if (m_thread && m_thread->isRunning()) {
            // Interrupt a running query first: the blocking shutdown queues
            // behind whatever the worker is executing and would otherwise
            // block this thread until the statement finishes.
            m_runner->cancel();
            QMetaObject::invokeMethod(m_runner, "shutdown", Qt::BlockingQueuedConnection);
            m_thread->quit();
            m_thread->wait();
        }
        delete m_runner;
        m_runner = nullptr;
    }
    delete m_thread;
    m_thread = nullptr;
    m_database.close();
    m_pending = 0;
    m_displayName.clear();
    m_remoteTarget.clear();
    m_bootstrapSql.clear();
    m_bootstrapping = false;
}

void DbSession::runSql(quint64 queryId, const QString &sql)
{
    if (!m_runner)
        return;
    ++m_pending;
    QMetaObject::invokeMethod(m_runner, "execute", Qt::QueuedConnection, Q_ARG(quint64, queryId),
                              Q_ARG(QString, sql));
}

void DbSession::refreshCatalog()
{
    if (m_runner)
        QMetaObject::invokeMethod(m_runner, "refreshCatalog", Qt::QueuedConnection);
}

void DbSession::cancel()
{
    if (m_runner)
        m_runner->cancel();
}

} // namespace ducq
