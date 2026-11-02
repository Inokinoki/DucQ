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
    return true;
}

void DbSession::close()
{
    if (m_runner) {
        if (m_thread && m_thread->isRunning()) {
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
