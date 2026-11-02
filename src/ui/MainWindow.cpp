#include "MainWindow.h"

#include "DropImport.h"
#include "ResultTabs.h"
#include "SchemaTree.h"
#include "SqlEditor.h"
#include "core/AppSettings.h"
#include "core/Catalog.h"
#include "core/DbSession.h"
#include "core/QueryRunner.h"
#include "core/ResultCache.h"
#include "export/Exporter.h"

#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QThread>
#include <QToolBar>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace ducq {
namespace {

// Language codes and their native menu labels. The labels are intentionally
// not translated: every language is shown in its own script.
struct LanguageEntry {
    const char *code;
    const char *nativeName;
};

const LanguageEntry kLanguages[] = {
    {"en", "English"},
    {"zh_CN", "简体中文"},
    {"ja", "日本語"},
    {"ko", "한국어"},
    {"de", "Deutsch"},
    {"fr", "Français"},
    {"es", "Español"},
    {"pt_BR", "Português (Brasil)"},
    {"ru", "Русский"},
};

QString translationResourcePath(const QString &code)
{
    return QStringLiteral(":/i18n/ducq_%1.qm").arg(code);
}

} // namespace

MainWindow::MainWindow(const QString &startupDatabase)
{
    qRegisterMetaType<QVector<ducq::ColumnMeta>>("QVector<ducq::ColumnMeta>");
    qRegisterMetaType<QVector<ducq::TableInfo>>("QVector<ducq::TableInfo>");

    setWindowTitle(QStringLiteral("DucQ"));
    resize(1360, 860);
    setAcceptDrops(true);

    buildUi();
    m_translator = new QTranslator(this);
    buildLanguageMenu();

    // The first session: the startup database file if given, else in-memory.
    if (DbSession *first = createSession(startupDatabase))
        activateSession(first);

    retranslateUi();
    refreshCatalog();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    m_schemaTree = new SchemaTree;
    m_editor = new SqlEditor;
    m_resultTabs = new ResultTabs(kDefaultDisplayRowLimit);
    m_sessionCombo = new QComboBox;
    connect(m_sessionCombo, &QComboBox::activated, this, &MainWindow::switchSession);
    m_historyCombo = new QComboBox;
    m_historyCombo->setMinimumContentsLength(40);
    m_historyCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_historyCombo->setEnabled(false);
    connect(m_historyCombo, &QComboBox::activated, this, [this](int index) {
        if (index >= 0 && index < m_history.size())
            m_editor->setPlainText(m_history.at(index));
    });

    m_runButton = new QPushButton;
    connect(m_runButton, &QPushButton::clicked, this, &MainWindow::runEditorSql);
    connect(m_editor, &SqlEditor::executeRequested, this, &MainWindow::runEditorSql);

    auto *editorBar = new QWidget;
    auto *editorBarLayout = new QHBoxLayout(editorBar);
    editorBarLayout->setContentsMargins(6, 4, 6, 4);
    editorBarLayout->addWidget(m_runButton);
    editorBarLayout->addWidget(m_sessionCombo, 0);
    editorBarLayout->addWidget(m_historyCombo, 1);

    auto *editorPane = new QWidget;
    auto *editorLayout = new QVBoxLayout(editorPane);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);
    editorLayout->addWidget(editorBar);
    editorLayout->addWidget(m_editor, 1);

    auto *verticalSplitter = new QSplitter(Qt::Vertical);
    verticalSplitter->addWidget(editorPane);
    verticalSplitter->addWidget(m_resultTabs);
    verticalSplitter->setStretchFactor(0, 2);
    verticalSplitter->setStretchFactor(1, 3);
    verticalSplitter->setSizes({280, 480});

    auto *horizontalSplitter = new QSplitter(Qt::Horizontal);
    horizontalSplitter->addWidget(m_schemaTree);
    horizontalSplitter->addWidget(verticalSplitter);
    horizontalSplitter->setStretchFactor(0, 1);
    horizontalSplitter->setStretchFactor(1, 4);
    horizontalSplitter->setSizes({230, 1030});
    setCentralWidget(horizontalSplitter);

    connect(m_schemaTree, &SchemaTree::tableActivated, this, [this](const QString &qualifiedName) {
        m_editor->setPlainText(QStringLiteral("SELECT * FROM %1 LIMIT 100;").arg(qualifiedName));
    });
    connect(m_schemaTree, &SchemaTree::columnActivated, this, [this](const QString &columnName) {
        m_editor->editorWidget()->insertPlainText(quoteIdentifier(columnName));
    });
    connect(m_schemaTree, &SchemaTree::refreshRequested, this, &MainWindow::refreshCatalog);

    m_fileMenu = menuBar()->addMenu(QString());
    m_newAction = m_fileMenu->addAction(QString());
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newDatabaseFile);
    m_openAction = m_fileMenu->addAction(QString());
    connect(m_openAction, &QAction::triggered, this, [this] { openDatabaseFile(); });
    m_newMemoryAction = m_fileMenu->addAction(QString());
    connect(m_newMemoryAction, &QAction::triggered, this, &MainWindow::newInMemoryConnection);
    m_closeConnectionAction = m_fileMenu->addAction(QString());
    connect(m_closeConnectionAction, &QAction::triggered, this, &MainWindow::closeConnection);
    m_fileMenu->addSeparator();
    m_exportCsvAction = m_fileMenu->addAction(QString());
    connect(m_exportCsvAction, &QAction::triggered, this,
            [this] { exportCurrentResult(QStringLiteral("CSV")); });
    m_exportParquetAction = m_fileMenu->addAction(QString());
    connect(m_exportParquetAction, &QAction::triggered, this,
            [this] { exportCurrentResult(QStringLiteral("PARQUET")); });
    m_fileMenu->addSeparator();
    m_quitAction = m_fileMenu->addAction(QString());
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::close);

    auto *toolbar = addToolBar(QStringLiteral("main"));
    toolbar->setMovable(false);
    toolbar->addAction(m_openAction);
    toolbar->addAction(m_newAction);
    toolbar->addSeparator();
    m_refreshToolbarAction = toolbar->addAction(QString());
    connect(m_refreshToolbarAction, &QAction::triggered, this, &MainWindow::refreshCatalog);

    m_connectionLabel = new QLabel(this);
    m_messageLabel = new QLabel(this);
    m_statsLabel = new QLabel(this);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setFixedWidth(160);
    m_progressBar->setVisible(false);
    m_cancelButton = new QPushButton(this);
    m_cancelButton->setVisible(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::cancelRunningQuery);

    statusBar()->addWidget(m_connectionLabel);
    statusBar()->addWidget(m_messageLabel, 1);
    statusBar()->addPermanentWidget(m_statsLabel);
    statusBar()->addPermanentWidget(m_progressBar);
    statusBar()->addPermanentWidget(m_cancelButton);
}

void MainWindow::buildLanguageMenu()
{
    m_languageMenu = menuBar()->addMenu(QString());
    m_languageGroup = new QActionGroup(this);
    m_languageGroup->setExclusive(true);
    std::unique_ptr<QSettings> settings(ducq::AppSettings::create());
    const QString current = settings->value(QStringLiteral("language"), QStringLiteral("en")).toString();
    for (const LanguageEntry &entry : kLanguages) {
        auto *action = m_languageMenu->addAction(QString::fromUtf8(entry.nativeName));
        action->setCheckable(true);
        action->setData(QLatin1String(entry.code));
        action->setChecked(entry.code == current);
        m_languageGroup->addAction(action);
    }
    connect(m_languageGroup, &QActionGroup::triggered, this, &MainWindow::switchLanguage);
}

void MainWindow::switchLanguage(QAction *action)
{
    applyLanguage(action->data().toString(), true);
}

void MainWindow::applyLanguage(const QString &code, bool saveSetting)
{
    // The English .qm is absent on purpose: removing all translators falls
    // back to the (English) source strings.
    QCoreApplication::removeTranslator(m_translator);
    if (code != QLatin1String("en") && m_translator->load(translationResourcePath(code)))
        QCoreApplication::installTranslator(m_translator);

    if (saveSetting) {
        std::unique_ptr<QSettings> settings(ducq::AppSettings::create());
        settings->setValue(QStringLiteral("language"), code);
    }

    // LanguageChange makes Qt re-layout; our own texts are refreshed below.
    retranslateUi();
    m_resultTabs->retranslateUi();
    m_schemaTree->retranslateUi();
    m_editor->retranslateUi();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    m_runButton->setText(tr("Run (Ctrl+⏎)"));
    m_sessionCombo->setToolTip(tr("Active connection"));
    m_historyCombo->setToolTip(tr("Query history"));

    m_fileMenu->setTitle(tr("&File"));
    m_newAction->setText(tr("New Database…"));
    m_openAction->setText(tr("Open Database…"));
    m_newMemoryAction->setText(tr("New In-Memory Connection"));
    m_closeConnectionAction->setText(tr("Close Connection"));
    m_exportCsvAction->setText(tr("Export Current Results as CSV…"));
    m_exportParquetAction->setText(tr("Export Current Results as Parquet…"));
    m_quitAction->setText(tr("Exit"));
    m_languageMenu->setTitle(tr("&Language"));
    m_refreshToolbarAction->setText(tr("Refresh Schema"));

    m_cancelButton->setText(tr("Cancel"));
    for (DbSession *session : m_sessions) {
        const int index = m_sessionCombo->findData(session->id());
        if (index >= 0)
            m_sessionCombo->setItemText(index, displayNameFor(session));
    }
    updateConnectionLabel();
}

DbSession *MainWindow::createSession(const QString &path)
{
    auto *session = new DbSession(m_nextSessionId++, this);
    QString error;
    if (!session->open(path, &error)) {
        QMessageBox::critical(this, tr("Open Failed"), error);
        delete session;
        return nullptr;
    }
    // The runner only exists once the session is open.
    connect(session->runner(), &QueryRunner::resultReady, this,
            [this, session](quint64 qid, quintptr resultPtr, const QVector<ducq::ColumnMeta> &columns) {
                onResultReady(session->id(), qid, resultPtr, columns);
            });
    connect(session->runner(), &QueryRunner::finished, this,
            [this, session](quint64 qid, qint64 elapsedMs, qsizetype totalRows, bool cancelled,
                            const QString &error) {
                onQueryFinished(session->id(), qid, elapsedMs, totalRows, cancelled, error);
            });
    connect(session->runner(), &QueryRunner::progressChanged, this,
            [this, session](quint64, double percentage) {
                onProgressChanged(session->id(), percentage);
            });
    connect(session->runner(), &QueryRunner::catalogReady, this,
            [this, session](const QVector<ducq::TableInfo> &tables) {
                onCatalogReady(session->id(), tables);
            });

    m_sessions.append(session);
    m_sessionCombo->addItem(displayNameFor(session), session->id());
    return session;
}

void MainWindow::activateSession(DbSession *session)
{
    if (!session)
        return;
    m_activeSession = session;
    const int index = m_sessionCombo->findData(session->id());
    if (index >= 0)
        m_sessionCombo->setCurrentIndex(index);
    updateConnectionLabel();
    refreshCatalog();
}

QString MainWindow::displayNameFor(DbSession *session) const
{
    if (session->isInMemory())
        return tr("In-memory database");
    return QFileInfo(session->path()).fileName();
}

DbSession *MainWindow::sessionById(int sessionId) const
{
    for (DbSession *session : m_sessions) {
        if (session->id() == sessionId)
            return session;
    }
    return nullptr;
}

bool MainWindow::ensureSessionIdle(DbSession *session)
{
    if (!session || !session->isBusy())
        return true;
    QMessageBox::information(this, QStringLiteral("DucQ"),
                             tr("A query is running. Wait for it to finish or cancel it first."));
    return false;
}

void MainWindow::newDatabaseFile()
{
    QFileDialog dialog(this, tr("New DuckDB Database"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
    dialog.setDefaultSuffix(QStringLiteral("duckdb"));
    dialog.setNameFilter(tr("DuckDB databases (*.duckdb *.db);;All files (*)"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QStringList files = dialog.selectedFiles();
    if (files.isEmpty())
        return;
    openDatabaseFile(files.first());
}

void MainWindow::openDatabaseFile()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Open DuckDB Database"), QString(),
                                                      tr("DuckDB databases (*.duckdb *.db *.ddb);;All files (*)"));
    if (!file.isEmpty())
        openDatabaseFile(file);
}

void MainWindow::openDatabaseFile(const QString &file)
{
    // Reuse an existing connection to the same file instead of opening a
    // second one (DuckDB allows only one writer per file).
    const QString absolute = QFileInfo(file).absoluteFilePath();
    for (DbSession *session : m_sessions) {
        if (!session->isInMemory() && QFileInfo(session->path()).absoluteFilePath() == absolute) {
            activateSession(session);
            setStatusMessage(tr("Already open in another connection: %1").arg(QFileInfo(file).fileName()));
            return;
        }
    }
    if (DbSession *session = createSession(file))
        activateSession(session);
}

void MainWindow::newInMemoryConnection()
{
    if (DbSession *session = createSession(QString()))
        activateSession(session);
}

void MainWindow::closeConnection()
{
    DbSession *session = m_activeSession;
    if (!session || !ensureSessionIdle(session))
        return;
    m_resultTabs->closeSessionTabs(session->id());
    const int index = m_sessionCombo->findData(session->id());
    if (index >= 0)
        m_sessionCombo->removeItem(index);
    m_sessions.removeOne(session);
    session->deleteLater();
    m_activeSession = nullptr;

    if (m_sessions.isEmpty()) {
        // Never leave the app without a database: fall back to in-memory.
        if (DbSession *fresh = createSession(QString()))
            activateSession(fresh);
        return;
    }
    activateSession(m_sessions.last());
}

void MainWindow::switchSession(int comboIndex)
{
    const int sessionId = m_sessionCombo->itemData(comboIndex).toInt();
    if (DbSession *session = sessionById(sessionId); session && session != m_activeSession)
        activateSession(session);
}

void MainWindow::updateConnectionLabel()
{
    if (!m_activeSession) {
        m_connectionLabel->setText(tr("Not connected"));
        setWindowTitle(QStringLiteral("DucQ"));
        return;
    }
    const QString name = m_activeSession->isInMemory()
        ? tr("In-memory database")
        : QFileInfo(m_activeSession->path()).fileName();
    m_connectionLabel->setText(name);
    setWindowTitle(QStringLiteral("DucQ — %1").arg(name));
}

void MainWindow::exportCurrentResult(const QString &format)
{
    const ResultTab *tab = m_resultTabs->currentTab();
    if (!tab || !tab->finished || tab->sql.isEmpty()) {
        QMessageBox::information(this, tr("Export"), tr("Select a finished result tab first."));
        return;
    }
    DbSession *session = sessionById(tab->sessionId);
    if (!session)
        return;
    const QString filter =
        format == QLatin1String("PARQUET") ? tr("Parquet (*.parquet)") : tr("CSV (*.csv)");
    QString file = QFileDialog::getSaveFileName(this, tr("Export Results"), QString(), filter);
    if (file.isEmpty())
        return;
    QFileInfo info(file);
    if (info.suffix().isEmpty())
        file += format == QLatin1String("PARQUET") ? QStringLiteral(".parquet") : QStringLiteral(".csv");
    runOnSession(session, Exporter::buildCopySql(tab->sql, file, format));
}

void MainWindow::runEditorSql()
{
    const QString sql = m_editor->toPlainText().trimmed();
    if (sql.isEmpty()) {
        setStatusMessage(tr("No SQL to execute"));
        return;
    }
    runOnSession(m_activeSession, sql);
    pushHistory(sql);
}

void MainWindow::runOnSession(DbSession *session, const QString &sql, const QString &followUpSql)
{
    if (!session)
        return;
    const quint64 queryId = m_nextQueryId++;
    if (!followUpSql.isEmpty())
        m_followUps.insert(queryId, followUpSql);
    m_resultTabs->startQueryTab(session->id(), queryId, sql);
    ++m_busyCount;
    m_lastStartedSid = session->id();
    setBusy(true);
    setStatusMessage(tr("Running…"));
    session->runSql(queryId, sql);
}

void MainWindow::pushHistory(const QString &sql)
{
    m_history.removeAll(sql);
    m_history.prepend(sql);
    if (m_history.size() > 20)
        m_history.resize(20);
    m_historyCombo->clear();
    for (const QString &entry : m_history) {
        QString display = entry.simplified();
        if (display.size() > 60)
            display = display.left(60) + QStringLiteral("…");
        m_historyCombo->addItem(display);
    }
    m_historyCombo->setEnabled(!m_history.isEmpty());
}

void MainWindow::refreshCatalog()
{
    if (m_activeSession)
        m_activeSession->refreshCatalog();
}

void MainWindow::cancelRunningQuery()
{
    if (DbSession *session = sessionById(m_lastStartedSid))
        session->cancel();
}

void MainWindow::onResultReady(int sessionId, quint64 queryId, quintptr resultPtr,
                               const QVector<ColumnMeta> &columns)
{
    auto *result = reinterpret_cast<duckdb_result *>(resultPtr);
    if (!m_resultTabs->deliverResult(sessionId, queryId, resultPtr, columns))
        ResultCache::destroyResult(result);
}

void MainWindow::onQueryFinished(int sessionId, quint64 queryId, qint64 elapsedMs, qsizetype totalRows,
                                 bool cancelled, const QString &error)
{
    --m_busyCount;
    if (m_busyCount <= 0) {
        m_busyCount = 0;
        setBusy(false);
    }
    m_resultTabs->markFinished(queryId, elapsedMs, totalRows, cancelled, error);

    if (cancelled) {
        setStatusMessage(tr("Query cancelled"));
        return;
    }
    if (!error.isEmpty()) {
        setStatusMessage(error);
        return;
    }
    setStatusMessage(tr("Done: %L1 rows · %L2 ms").arg(totalRows).arg(elapsedMs));
    if (DbSession *session = sessionById(sessionId)) {
        const QString followUp = m_followUps.take(queryId);
        if (!followUp.isEmpty()) {
            runOnSession(session, followUp);
            return;
        }
        session->refreshCatalog();
    }
}

void MainWindow::onProgressChanged(int sessionId, double percentage)
{
    Q_UNUSED(sessionId);
    m_progressBar->setValue(int(qBound(0.0, percentage, 100.0)));
}

void MainWindow::onCatalogReady(int sessionId, const QVector<TableInfo> &tables)
{
    if (!m_activeSession || sessionId != m_activeSession->id())
        return;
    m_schemaTree->setCatalog(tables);
    setStatusMessage(tr("Schema refreshed: %1 tables/views").arg(tables.size()));
}

void MainWindow::setBusy(bool busy)
{
    m_progressBar->setVisible(busy);
    m_progressBar->setValue(0);
    m_cancelButton->setVisible(busy);
}

void MainWindow::setStatusMessage(const QString &message)
{
    m_messageLabel->setText(message);
}

void MainWindow::executeScriptAtStartup(const QString &scriptPath)
{
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatusMessage(tr("Cannot read script: %1").arg(scriptPath));
        return;
    }
    const QString sql = QString::fromUtf8(scriptFile.readAll());
    m_editor->setPlainText(sql);
    runOnSession(m_activeSession, sql);
    pushHistory(sql);
}

void MainWindow::scheduleScreenshot(const QString &filePath, int delayMs)
{
    QTimer::singleShot(qMax(delayMs, 0), this, [this, filePath] {
        const QPixmap shot = grab();
        if (!shot.save(filePath))
            qWarning("DucQ: failed to save screenshot to %s", qPrintable(filePath));
        close();
    });
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    for (const QUrl &url : event->mimeData()->urls()) {
        const QString file = url.toLocalFile();
        if (!file.isEmpty() && DropImport::isSupportedFile(file)) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    int imported = 0;
    for (const QUrl &url : event->mimeData()->urls()) {
        const QString file = url.toLocalFile();
        if (file.isEmpty())
            continue;
        QString viewName;
        const QString viewSql = DropImport::buildCreateViewSql(file, &viewName);
        if (viewSql.isEmpty()) {
            setStatusMessage(tr("Skipped unsupported file: %1").arg(QFileInfo(file).fileName()));
            continue;
        }
        runOnSession(m_activeSession, viewSql, DropImport::buildPreviewSql(viewName));
        ++imported;
    }
    if (imported > 0)
        event->acceptProposedAction();
}

} // namespace ducq
