#pragma once

#include <QHash>
#include <QMainWindow>
#include <QStringList>

#include "core/Catalog.h"
#include "core/Database.h"
#include "core/ResultCache.h"

class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QProgressBar;
class QPushButton;
class QSplitter;
class QThread;

namespace ducq {

class QueryRunner;
class ResultTabs;
class SchemaTree;
class SqlEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void newDatabaseFile();
    void openDatabaseFile();
    void closeDatabase();
    void exportCurrentResult(const QString &format);
    void runEditorSql();
    void refreshCatalog();
    void cancelRunningQuery();

    void onResultReady(quint64 queryId, quintptr resultPtr, const QVector<ducq::ColumnMeta> &columns);
    void onQueryFinished(quint64 queryId, qint64 elapsedMs, qsizetype totalRows, bool cancelled, const QString &error);
    void onProgressChanged(quint64 queryId, double percentage);
    void onCatalogReady(const QVector<ducq::TableInfo> &tables);

private:
    void buildUi();
    void connectRunner();
    void openDatabase(const QString &path); // empty path -> in-memory
    bool ensureNotBusy();
    void setBusy(bool busy);
    void runSql(const QString &sql, const QString &followUpSql = {});
    void pushHistory(const QString &sql);
    void setStatusMessage(const QString &message);
    void updateConnectionLabel();

    Database m_database;
    QThread *m_workerThread = nullptr;
    QueryRunner *m_runner = nullptr;

    SchemaTree *m_schemaTree = nullptr;
    SqlEditor *m_editor = nullptr;
    ResultTabs *m_resultTabs = nullptr;
    QComboBox *m_historyCombo = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_cancelButton = nullptr;

    quint64 m_nextQueryId = 1;
    int m_pendingQueries = 0;
    QHash<quint64, QString> m_followUps;
    QStringList m_history;
};

} // namespace ducq
