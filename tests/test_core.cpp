#include <QtTest>

#include "core/Catalog.h"
#include "core/Database.h"
#include "core/DbSession.h"
#include "core/QueryRunner.h"
#include "core/ResultCache.h"

#include <QEventLoop>
#include <QThread>

using namespace ducq;

class TestCore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testExecuteAndReadValues();
    void testDisplayLimitTruncation();
    void testNullValues();
    void testNumericTypes();
    void testErrorReporting();
    void testMultiStatement();
    void testCancel();
    void testCatalog();
    void testFileDatabasePersistence();
    void testTwoSessions();

private:
    void runAndWait(const QString &sql, qsizetype displayRowLimit = kDefaultDisplayRowLimit);
    void onResult(quintptr resultPtr, const QVector<ducq::ColumnMeta> &columns);
    void onFinished(quint64 queryId, qint64 elapsedMs, qsizetype totalRows, bool cancelled, const QString &error);

    Database m_database;
    QThread m_thread;
    QueryRunner *m_runner = nullptr;
    ResultCache m_cache;
    qsizetype m_currentDisplayLimit = kDefaultDisplayRowLimit;

    quint64 m_nextQueryId = 0;
    bool m_done = false;
    qsizetype m_totalRows = 0;
    bool m_cancelled = false;
    QString m_error;
};

void TestCore::initTestCase()
{
    qRegisterMetaType<QVector<ducq::ColumnMeta>>("QVector<ducq::ColumnMeta>");
    qRegisterMetaType<QVector<ducq::TableInfo>>("QVector<ducq::TableInfo>");

    QString error;
    QVERIFY2(m_database.open(QString(), false, &error), qPrintable(error));

    m_runner = new QueryRunner(&m_database);
    m_runner->moveToThread(&m_thread);
    m_thread.start();
    QMetaObject::invokeMethod(m_runner, "initialize", Qt::BlockingQueuedConnection);

    connect(m_runner, &QueryRunner::resultReady, this,
            [this](quint64, quintptr resultPtr, const QVector<ducq::ColumnMeta> &columns) {
                onResult(resultPtr, columns);
            });
    connect(m_runner, &QueryRunner::finished, this, &TestCore::onFinished);
}

void TestCore::cleanupTestCase()
{
    QMetaObject::invokeMethod(m_runner, "shutdown", Qt::BlockingQueuedConnection);
    m_thread.quit();
    QVERIFY(m_thread.wait(5000));
    delete m_runner;
    m_runner = nullptr;
}

void TestCore::runAndWait(const QString &sql, qsizetype displayRowLimit)
{
    m_cache.clear();
    m_done = false;
    m_totalRows = 0;
    m_cancelled = false;
    m_error.clear();
    m_currentDisplayLimit = displayRowLimit;

    const quint64 queryId = ++m_nextQueryId;
    const bool invoked = QMetaObject::invokeMethod(m_runner, "execute", Qt::QueuedConnection, Q_ARG(quint64, queryId),
                                                   Q_ARG(QString, sql));
    QVERIFY2(invoked, "invokeMethod(execute) should succeed");
    QTRY_COMPARE_WITH_TIMEOUT(m_done, true, 30000);
}

void TestCore::onResult(quintptr resultPtr, const QVector<ducq::ColumnMeta> &columns)
{
    m_cache.reset(reinterpret_cast<duckdb_result *>(resultPtr), columns, m_currentDisplayLimit);
}

void TestCore::onFinished(quint64, qint64, qsizetype totalRows, bool cancelled, const QString &error)
{
    m_totalRows = totalRows;
    m_cancelled = cancelled;
    m_error = error;
    m_done = true;
}

void TestCore::testExecuteAndReadValues()
{
    runAndWait(QStringLiteral("SELECT i, i::VARCHAR AS s FROM range(5000) t(i)"), 1000000);
    QVERIFY2(m_error.isEmpty(), qPrintable(m_error));
    QCOMPARE(m_totalRows, qsizetype(5000));
    QCOMPARE(m_cache.rowCount(), qsizetype(5000));
    QCOMPARE(m_cache.columnCount(), 2);
    QCOMPARE(m_cache.columns().at(0).name, QStringLiteral("i"));
    QCOMPARE(m_cache.columns().at(0).typeId, DUCKDB_TYPE_BIGINT);
    QVERIFY(m_cache.columns().at(0).numeric);
    QCOMPARE(m_cache.columns().at(1).name, QStringLiteral("s"));
    QCOMPARE(m_cache.columns().at(1).typeId, DUCKDB_TYPE_VARCHAR);
    QCOMPARE(m_cache.cell(0, 0).toLongLong(), 0LL);
    QCOMPARE(m_cache.cell(4999, 0).toLongLong(), 4999LL);
    QCOMPARE(m_cache.cell(123, 1).toString(), QStringLiteral("123"));
    QVERIFY(!m_cache.truncated());
}

void TestCore::testDisplayLimitTruncation()
{
    runAndWait(QStringLiteral("SELECT i FROM range(1000) t(i)"), 10);
    QVERIFY2(m_error.isEmpty(), qPrintable(m_error));
    QCOMPARE(m_totalRows, qsizetype(1000));
    QCOMPARE(m_cache.rowCount(), qsizetype(10));
    QVERIFY(m_cache.truncated());
    QCOMPARE(m_cache.totalRowCount(), qsizetype(1000));
}

void TestCore::testNullValues()
{
    runAndWait(QStringLiteral("SELECT CASE WHEN i % 2 = 0 THEN i END AS v FROM range(4) t(i)"), 1000000);
    QVERIFY2(m_error.isEmpty(), qPrintable(m_error));
    QCOMPARE(m_totalRows, qsizetype(4));
    QVERIFY(!m_cache.isNull(0, 0));
    QCOMPARE(m_cache.cell(0, 0).toLongLong(), 0LL);
    QVERIFY(m_cache.isNull(1, 0));
    QVERIFY(m_cache.cell(1, 0).isNull());
    QVERIFY(!m_cache.isNull(2, 0));
    QVERIFY(m_cache.isNull(3, 0));
}

void TestCore::testNumericTypes()
{
    runAndWait(QStringLiteral(
                   "SELECT 1::TINYINT a, 2::SMALLINT b, 3::INTEGER c, 4::BIGINT d, 0.5::FLOAT e, 0.25::DOUBLE f"),
               1000000);
    QVERIFY2(m_error.isEmpty(), qPrintable(m_error));
    QCOMPARE(m_cache.rowCount(), qsizetype(1));
    QVERIFY(m_cache.columns().at(0).numeric);
    QVERIFY(m_cache.columns().at(5).numeric);
    QCOMPARE(m_cache.cell(0, 0).toInt(), 1);
    QCOMPARE(m_cache.cell(0, 3).toLongLong(), 4LL);
    QCOMPARE(m_cache.cell(0, 5).toDouble(), 0.25);
    QVERIFY(m_cache.isNull(0, 0) == false);
}

void TestCore::testErrorReporting()
{
    runAndWait(QStringLiteral("SELECT * FROM this_table_does_not_exist"), 1000000);
    QVERIFY(!m_error.isEmpty());
    QVERIFY(!m_cancelled);
    QCOMPARE(m_cache.rowCount(), qsizetype(0));
    QVERIFY(!m_cache.hasResult());
}

void TestCore::testMultiStatement()
{
    runAndWait(QStringLiteral(
                   "CREATE TABLE mt(i INTEGER); INSERT INTO mt VALUES (7); INSERT INTO mt VALUES (9); SELECT * FROM mt ORDER BY i"),
               1000000);
    QVERIFY2(m_error.isEmpty(), qPrintable(m_error));
    QCOMPARE(m_totalRows, qsizetype(2));
    QCOMPARE(m_cache.cell(0, 0).toInt(), 7);
    QCOMPARE(m_cache.cell(1, 0).toInt(), 9);
}

void TestCore::testCancel()
{
    m_cache.clear();
    m_done = false;
    m_cancelled = false;
    m_error.clear();

    const quint64 queryId = ++m_nextQueryId;
    const bool invoked = QMetaObject::invokeMethod(m_runner, "execute", Qt::QueuedConnection, Q_ARG(quint64, queryId),
                                                   Q_ARG(QString, QStringLiteral("SELECT sum(i) FROM range(1000000000000) t(i)")));
    QVERIFY2(invoked, "invokeMethod(execute) should succeed");
    QTimer::singleShot(300, this, [this] { m_runner->cancel(); });
    QTRY_COMPARE_WITH_TIMEOUT(m_done, true, 30000);
    QVERIFY2(m_cancelled, "query should be reported as cancelled");
    QVERIFY2(m_error.isEmpty(), qPrintable(m_error));
}

void TestCore::testCatalog()
{
    runAndWait(QStringLiteral("CREATE TABLE cat_t(a INTEGER, b VARCHAR); CREATE VIEW cat_v AS SELECT * FROM cat_t"),
               1000000);
    QVERIFY2(m_error.isEmpty(), qPrintable(m_error));

    QMetaObject::invokeMethod(m_runner, "refreshCatalog", Qt::QueuedConnection);

    QVector<TableInfo> tables;
    QEventLoop loop;
    connect(m_runner, &QueryRunner::catalogReady, &loop, [&](const QVector<TableInfo> &result) {
        tables = result;
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();

    bool foundTable = false;
    bool foundView = false;
    for (const TableInfo &table : tables) {
        if (table.name == QLatin1String("cat_t")) {
            foundTable = true;
            QCOMPARE(table.columns.size(), 2);
            QCOMPARE(table.columns.at(0).name, QStringLiteral("a"));
            QCOMPARE(table.columns.at(1).typeName, QStringLiteral("VARCHAR"));
        } else if (table.name == QLatin1String("cat_v")) {
            foundView = true;
        }
    }
    QVERIFY2(foundTable, "cat_t should be listed in the catalog");
    QVERIFY2(foundView, "cat_v should be listed in the catalog");
}

void TestCore::testFileDatabasePersistence()
{
    const QString path = QStringLiteral("./test_ducq_persist.duckdb");

    {
        Database fileDb;
        QString error;
        QVERIFY2(fileDb.open(path, false, &error), qPrintable(error));
        duckdb_connection connection = fileDb.connect();
        QVERIFY(connection != nullptr);
        duckdb_result result;
        QCOMPARE(duckdb_query(connection, "CREATE TABLE ft(i INTEGER); INSERT INTO ft VALUES (41), (42);", &result),
                 DuckDBSuccess);
        duckdb_destroy_result(&result);
        Database::disconnect(connection);
    }

    {
        Database fileDb;
        QString error;
        QVERIFY2(fileDb.open(path, false, &error), qPrintable(error));
        duckdb_connection connection = fileDb.connect();
        QVERIFY(connection != nullptr);
        duckdb_result result;
        QCOMPARE(duckdb_query(connection, "SELECT sum(i) FROM ft", &result), DuckDBSuccess);
        char *text = duckdb_value_varchar(&result, 0, 0);
        QCOMPARE(QString::fromUtf8(text ? text : ""), QStringLiteral("83"));
        if (text)
            duckdb_free(text);
        duckdb_destroy_result(&result);
        Database::disconnect(connection);
    }

    QFile::remove(path);
}

void TestCore::testTwoSessions()
{
    // Two independent sessions, each with its own worker thread: queries run
    // concurrently and results are attributed to the right session.
    DbSession sessionA(1);
    DbSession sessionB(2);
    QString error;
    QVERIFY(sessionA.runner() == nullptr); // the runner only exists after open()
    QVERIFY2(sessionA.open(QString(), &error), qPrintable(error));
    QVERIFY(sessionA.runner() != nullptr);
    QVERIFY2(sessionB.open(QString(), &error), qPrintable(error));

    quint64 doneA = 0;
    quint64 doneB = 0;
    qint64 valueA = -1;
    qint64 valueB = -1;
    connect(sessionA.runner(), &QueryRunner::resultReady, &sessionA,
            [&doneA, &valueA](quint64, quintptr resultPtr, const QVector<ducq::ColumnMeta> &columns) {
                ResultCache cache;
                cache.reset(reinterpret_cast<duckdb_result *>(resultPtr), columns, kDefaultDisplayRowLimit);
                valueA = cache.cell(0, 0).toLongLong();
                doneA = 1;
            });
    connect(sessionB.runner(), &QueryRunner::resultReady, &sessionB,
            [&doneB, &valueB](quint64, quintptr resultPtr, const QVector<ducq::ColumnMeta> &columns) {
                ResultCache cache;
                cache.reset(reinterpret_cast<duckdb_result *>(resultPtr), columns, kDefaultDisplayRowLimit);
                valueB = cache.cell(0, 0).toLongLong();
                doneB = 1;
            });

    sessionA.runSql(++m_nextQueryId, QStringLiteral("SELECT 41;"));
    sessionB.runSql(++m_nextQueryId, QStringLiteral("SELECT 42;"));
    QTRY_COMPARE_WITH_TIMEOUT(doneA, quint64(1), 30000);
    QTRY_COMPARE_WITH_TIMEOUT(doneB, quint64(1), 30000);
    QCOMPARE(valueA, 41);
    QCOMPARE(valueB, 42);

    sessionA.close();
    sessionB.close();
}

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
