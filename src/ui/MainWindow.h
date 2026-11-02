#pragma once

#include <QHash>
#include <QMainWindow>
#include <QStringList>

#include "core/Catalog.h"
#include "core/ResultCache.h"

class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QActionGroup;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QSplitter;
class QThread;
class QTranslator;

namespace ducq {

class DbSession;
class QueryRunner;
class ResultTabs;
class SchemaTree;
class SqlEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &startupDatabase = {});
    ~MainWindow() override;

    // Runs the given SQL script in the active session after startup.
    void executeScriptAtStartup(const QString &scriptPath);
    // Grabs the main window into a PNG after delayMs, then closes the app.
    // Works on the offscreen platform too, which makes it usable in CI.
    void scheduleScreenshot(const QString &filePath, int delayMs);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void newDatabaseFile();
    void openDatabaseFile();
    void openDatabaseFile(const QString &file);
    void newInMemoryConnection();
    void closeConnection();
    void switchSession(int sessionId);
    void exportCurrentResult(const QString &format);
    void runEditorSql();
    void refreshCatalog();
    void cancelRunningQuery();
    void switchLanguage(QAction *action);

    void onResultReady(int sessionId, quint64 queryId, quintptr resultPtr,
                       const QVector<ducq::ColumnMeta> &columns);
    void onQueryFinished(int sessionId, quint64 queryId, qint64 elapsedMs, qsizetype totalRows, bool cancelled,
                         const QString &error);
    void onProgressChanged(int sessionId, double percentage);
    void onCatalogReady(int sessionId, const QVector<ducq::TableInfo> &tables);

private:
    void buildUi();
    void buildViewMenu();
    void buildLanguageMenu();
    void retranslateUi();
    void applyLanguage(const QString &code, bool saveSetting);
    void switchTheme(QAction *action);
    void applyIcons();
    void refreshThemeDependentUi();

    DbSession *createSession(const QString &path);
    void activateSession(ducq::DbSession *session);
    void closeActiveConnection();
    DbSession *sessionById(int sessionId) const;
    QString displayNameFor(ducq::DbSession *session) const;
    bool ensureSessionIdle(ducq::DbSession *session);
    void runOnSession(ducq::DbSession *session, const QString &sql, const QString &followUpSql = {},
                      bool recordHistory = false);
    void updateHistoryCombo();
    void saveHistory();
    void setStatusMessage(const QString &message);
    void setStatusError(const QString &message);
    void updateBusyIndicator();
    void restoreWindowState();
    void saveWindowState();
    void updateConnectionLabel();

    QTranslator *m_translator = nullptr;
    SchemaTree *m_schemaTree = nullptr;
    SqlEditor *m_editor = nullptr;
    ResultTabs *m_resultTabs = nullptr;
    QComboBox *m_sessionCombo = nullptr;
    QComboBox *m_historyCombo = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_runButton = nullptr;
    QSplitter *m_horizontalSplitter = nullptr;
    QSplitter *m_verticalSplitter = nullptr;

    QMenu *m_fileMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_themeMenu = nullptr;
    QMenu *m_languageMenu = nullptr;
    QActionGroup *m_languageGroup = nullptr;
    QActionGroup *m_themeGroup = nullptr;
    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_newMemoryAction = nullptr;
    QAction *m_closeConnectionAction = nullptr;
    QAction *m_exportCsvAction = nullptr;
    QAction *m_exportParquetAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_refreshToolbarAction = nullptr;
    QAction *m_themeSystemAction = nullptr;
    QAction *m_themeLightAction = nullptr;
    QAction *m_themeDarkAction = nullptr;

    QList<ducq::DbSession *> m_sessions;
    ducq::DbSession *m_activeSession = nullptr;
    int m_nextSessionId = 1;
    quint64 m_nextQueryId = 1; // globally unique across sessions
    int m_busyCount = 0;
    QHash<int, quint64> m_runningQueries; // sessionId -> running queryId
    int m_busyIndicatorSid = -1;          // session the progress bar / cancel act on
    int m_lastStartedSid = -1;
    QHash<quint64, QString> m_followUps;
    QHash<quint64, QString> m_pendingHistory; // queryId -> sql, recorded on success
    QStringList m_history;
    bool m_persistState = true; // off for headless screenshots
    bool m_statusIsError = false; // re-colored on theme switches
};

} // namespace ducq
