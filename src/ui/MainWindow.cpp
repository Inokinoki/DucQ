#include "MainWindow.h"

#include "DropImport.h"
#include "ResultTabs.h"
#include "SchemaTree.h"
#include "SqlEditor.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "core/Catalog.h"
#include "core/DbSession.h"
#include "core/HistoryStore.h"
#include "core/QueryRunner.h"
#include "core/ResultCache.h"
#include "export/Exporter.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QTextCursor>
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
    setAcceptDrops(true);

    buildUi();
    restoreWindowState();
    m_translator = new QTranslator(this);
    buildLanguageMenu();

    {
        std::unique_ptr<QSettings> settings(ducq::AppSettings::create());
        m_history = ducq::HistoryStore::load(settings.get());
    }
    updateHistoryCombo();

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
    m_schemaFilter = new QLineEdit;
    m_schemaFilter->setClearButtonEnabled(true);
    m_schemaFilter->setAccessibleName(tr("Filter tables…"));
    connect(m_schemaFilter, &QLineEdit::textChanged, m_schemaTree, &SchemaTree::setFilter);
    auto *schemaPane = new QWidget;
    auto *schemaLayout = new QVBoxLayout(schemaPane);
    schemaLayout->setContentsMargins(4, 4, 0, 0);
    schemaLayout->setSpacing(2);
    schemaLayout->addWidget(m_schemaFilter);
    schemaLayout->addWidget(m_schemaTree, 1);

    m_editor = new SqlEditor;
    m_resultTabs = new ResultTabs(kDefaultDisplayRowLimit);
    m_sessionCombo = new QComboBox;
    // Size to the longest connection name so large system fonts don't clip
    // the text; the history combo (stretch=1) absorbs the rest.
    m_sessionCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_sessionCombo->setAccessibleName(tr("Active connection"));
    connect(m_sessionCombo, &QComboBox::activated, this, &MainWindow::switchSession);
    m_historyCombo = new QComboBox;
    m_historyCombo->setAccessibleName(tr("Query history"));
    // A large minimum here would squeeze the session combo on narrow windows
    // or large system fonts; entries stay readable via the popup and tooltip.
    m_historyCombo->setMinimumContentsLength(20);
    m_historyCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_historyCombo->setEnabled(false);
    connect(m_historyCombo, &QComboBox::activated, this, [this](int index) {
        if (index >= 0 && index < m_history.size()) {
            m_editor->replaceText(m_history.at(index));
            m_historyCombo->setToolTip(m_history.at(index));
        }
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

    m_verticalSplitter = new QSplitter(Qt::Vertical);
    m_verticalSplitter->addWidget(editorPane);
    m_verticalSplitter->addWidget(m_resultTabs);
    m_verticalSplitter->setStretchFactor(0, 2);
    m_verticalSplitter->setStretchFactor(1, 3);
    m_verticalSplitter->setSizes({280, 480});

    m_horizontalSplitter = new QSplitter(Qt::Horizontal);
    m_horizontalSplitter->addWidget(schemaPane);
    m_horizontalSplitter->addWidget(m_verticalSplitter);
    m_horizontalSplitter->setStretchFactor(0, 1);
    m_horizontalSplitter->setStretchFactor(1, 4);
    m_horizontalSplitter->setSizes({230, 1030});
    setCentralWidget(m_horizontalSplitter);

    // The progress bar and Cancel follow the tab the user is looking at.
    connect(m_resultTabs, &QTabWidget::currentChanged, this, [this](int) { updateBusyIndicator(); });

    connect(m_schemaTree, &SchemaTree::tableActivated, this, [this](const QString &qualifiedName) {
        m_editor->replaceText(QStringLiteral("SELECT * FROM %1 LIMIT 100;").arg(qualifiedName));
    });
    connect(m_schemaTree, &SchemaTree::columnActivated, this, [this](const QString &columnName) {
        m_editor->editorWidget()->insertPlainText(quoteIdentifier(columnName));
    });
    connect(m_schemaTree, &SchemaTree::refreshRequested, this, &MainWindow::refreshCatalog);

    m_fileMenu = menuBar()->addMenu(QString());
    m_newAction = m_fileMenu->addAction(QString());
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newDatabaseFile);
    m_openAction = m_fileMenu->addAction(QString());
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, [this] { openDatabaseFile(); });
    m_newMemoryAction = m_fileMenu->addAction(QString());
    connect(m_newMemoryAction, &QAction::triggered, this, &MainWindow::newInMemoryConnection);
    m_closeConnectionAction = m_fileMenu->addAction(QString());
    connect(m_closeConnectionAction, &QAction::triggered, this, &MainWindow::closeConnection);
    m_fileMenu->addSeparator();
    m_exportCsvAction = m_fileMenu->addAction(QString());
    m_exportCsvAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    connect(m_exportCsvAction, &QAction::triggered, this,
            [this] { exportCurrentResult(QStringLiteral("CSV")); });
    m_exportParquetAction = m_fileMenu->addAction(QString());
    connect(m_exportParquetAction, &QAction::triggered, this,
            [this] { exportCurrentResult(QStringLiteral("PARQUET")); });
    m_fileMenu->addSeparator();
    m_closeTabAction = m_fileMenu->addAction(QString());
    m_closeTabAction->setShortcut(QKeySequence::Close);
    connect(m_closeTabAction, &QAction::triggered, this,
            [this] { m_resultTabs->closeCurrentTab(); });
    m_fileMenu->addSeparator();
    m_quitAction = m_fileMenu->addAction(QString());
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::close);

    m_queryMenu = menuBar()->addMenu(QString());
    m_runAction = m_queryMenu->addAction(QString());
    m_runAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    connect(m_runAction, &QAction::triggered, this, &MainWindow::runEditorSql);

    auto *toolbar = addToolBar(QStringLiteral("main"));
    toolbar->setMovable(false);
    toolbar->addAction(m_openAction);
    toolbar->addAction(m_newAction);
    toolbar->addSeparator();
    m_refreshToolbarAction = toolbar->addAction(QString());
    m_refreshToolbarAction->setShortcut(QKeySequence::Refresh);
    connect(m_refreshToolbarAction, &QAction::triggered, this, &MainWindow::refreshCatalog);

    m_connectionLabel = new QLabel(this);
    m_messageLabel = new QLabel(this);
    m_statsLabel = new QLabel(this);
    m_cursorPosLabel = new QLabel(this);
    m_cursorPosLabel->setAccessibleName(tr("Cursor position"));
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setFixedWidth(120);
    // A full-height progress bar would inflate the whole status bar.
    m_progressBar->setFixedHeight(14);
    m_progressBar->setVisible(false);
    // autoRaise tool button: slim like a toolbar item, so the status bar
    // keeps its height when it appears.
    m_cancelButton = new QToolButton(this);
    m_cancelButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_cancelButton->setAutoRaise(true);
    m_cancelButton->setVisible(false);
    connect(m_cancelButton, &QToolButton::clicked, this, &MainWindow::cancelRunningQuery);

    statusBar()->addWidget(m_connectionLabel);
    statusBar()->addWidget(m_messageLabel, 1);
    statusBar()->addPermanentWidget(m_statsLabel);
    statusBar()->addPermanentWidget(m_cursorPosLabel);
    statusBar()->addPermanentWidget(m_progressBar);
    statusBar()->addPermanentWidget(m_cancelButton);

    // Ctrl+Tab cycles connections; Ctrl+Shift+Tab goes the other way.
    auto *nextConnectionShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this);
    connect(nextConnectionShortcut, &QShortcut::activated, this, [this] { cycleSession(1); });
    auto *prevConnectionShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this);
    connect(prevConnectionShortcut, &QShortcut::activated, this, [this] { cycleSession(-1); });
    connect(m_editor->editorWidget(), &QPlainTextEdit::cursorPositionChanged, this,
            &MainWindow::updateCursorPos);

    // Icons and menus need every widget above to exist first.
    applyIcons();
    buildViewMenu();
}

void MainWindow::buildViewMenu()
{
    m_viewMenu = menuBar()->addMenu(QString());
    m_themeMenu = m_viewMenu->addMenu(QString());
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);
    m_themeSystemAction = m_themeMenu->addAction(QString());
    m_themeSystemAction->setCheckable(true);
    m_themeSystemAction->setData(QStringLiteral("system"));
    m_themeLightAction = m_themeMenu->addAction(QString());
    m_themeLightAction->setCheckable(true);
    m_themeLightAction->setData(QStringLiteral("light"));
    m_themeDarkAction = m_themeMenu->addAction(QString());
    m_themeDarkAction->setCheckable(true);
    m_themeDarkAction->setData(QStringLiteral("dark"));
    for (QAction *action : { m_themeSystemAction, m_themeLightAction, m_themeDarkAction })
        m_themeGroup->addAction(action);

    const Theme::Mode mode = Theme::savedMode();
    QAction *current = mode == Theme::Mode::Light ? m_themeLightAction
        : mode == Theme::Mode::Dark               ? m_themeDarkAction
                                                  : m_themeSystemAction;
    current->setChecked(true);
    connect(m_themeGroup, &QActionGroup::triggered, this, &MainWindow::switchTheme);
}

void MainWindow::switchTheme(QAction *action)
{
    const QString value = action->data().toString();
    const Theme::Mode mode = value == QLatin1String("light") ? Theme::Mode::Light
        : value == QLatin1String("dark")                     ? Theme::Mode::Dark
                                                             : Theme::Mode::System;
    Theme::saveMode(mode);
    Theme::applyTheme(mode);
    refreshThemeDependentUi();
}

void MainWindow::applyIcons()
{
    m_openAction->setIcon(Theme::Icons::actionIcon(Theme::Icons::Icon::Open));
    m_newAction->setIcon(Theme::Icons::actionIcon(Theme::Icons::Icon::NewDatabase));
    m_refreshToolbarAction->setIcon(Theme::Icons::actionIcon(Theme::Icons::Icon::Refresh));
    m_cancelButton->setIcon(Theme::Icons::actionIcon(Theme::Icons::Icon::Cancel));
    m_runButton->setIcon(Theme::Icons::actionIcon(Theme::Icons::Icon::Run));
}

void MainWindow::refreshThemeDependentUi()
{
    m_editor->retheme();
    m_resultTabs->retheme();
    applyIcons();
    if (m_statusIsError)
        m_messageLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(Theme::tokens().error.name()));
}

void MainWindow::cycleSession(int direction)
{
    const int count = m_sessionCombo->count();
    if (count < 2)
        return;
    const int next = (m_sessionCombo->currentIndex() + direction + count) % count;
    if (DbSession *session = sessionById(m_sessionCombo->itemData(next).toInt()))
        activateSession(session);
}

void MainWindow::updateCursorPos()
{
    const QTextCursor cursor = m_editor->editorWidget()->textCursor();
    m_cursorPosLabel->setText(tr("Ln %L1, Col %L2").arg(cursor.blockNumber() + 1)
                                  .arg(cursor.positionInBlock() + 1));
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_persistState)
        saveWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::retranslateUi()
{
    m_runButton->setText(tr("Run"));
    m_runButton->setToolTip(tr("Run the SQL (Ctrl+⏎)"));
    m_sessionCombo->setToolTip(tr("Active connection (Ctrl+Tab cycles)"));
    m_historyCombo->setToolTip(tr("Query history"));
    m_schemaFilter->setPlaceholderText(tr("Filter tables…"));
    updateCursorPos();

    m_fileMenu->setTitle(tr("&File"));
    m_newAction->setText(tr("New Database…"));
    m_openAction->setText(tr("Open Database…"));
    m_newMemoryAction->setText(tr("New In-Memory Connection"));
    m_closeConnectionAction->setText(tr("Close Connection"));
    m_exportCsvAction->setText(tr("Export Current Results as CSV…"));
    m_exportParquetAction->setText(tr("Export Current Results as Parquet…"));
    m_closeTabAction->setText(tr("Close Result Tab"));
    m_quitAction->setText(tr("Exit"));
    m_queryMenu->setTitle(tr("&Query"));
    m_runAction->setText(tr("&Run SQL"));
    m_languageMenu->setTitle(tr("&Language"));
    m_refreshToolbarAction->setText(tr("Refresh Schema"));

    m_viewMenu->setTitle(tr("&View"));
    m_themeMenu->setTitle(tr("&Theme"));
    m_themeSystemAction->setText(tr("Follow System"));
    m_themeLightAction->setText(tr("Light"));
    m_themeDarkAction->setText(tr("Dark"));

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
    dialog.setDefaultSuffix(QStringLiteral("duckdb"));
    dialog.setNameFilter(tr("DuckDB databases (*.duckdb *.db);;All files (*)"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QStringList files = dialog.selectedFiles();
    if (files.isEmpty())
        return;
    const QString file = files.first();
    // "New" that targets an existing file silently opening it is confusing;
    // say so and let the user decide.
    if (QFileInfo::exists(file)) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("New DuckDB Database"),
            tr("%1 already exists. Open it as an existing database?").arg(QFileInfo(file).fileName()));
        if (answer != QMessageBox::Yes)
            return;
    }
    openDatabaseFile(file);
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
    const QString name = displayNameFor(m_activeSession);
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
    // With a selection, run just the selected statements; otherwise all SQL.
    const QString selection = m_editor->selectedText();
    const QString sql = (selection.isEmpty() ? m_editor->toPlainText() : selection).trimmed();
    if (sql.isEmpty()) {
        setStatusMessage(tr("No SQL to execute"));
        return;
    }
    runOnSession(m_activeSession, sql, {}, true);
}

void MainWindow::runOnSession(DbSession *session, const QString &sql, const QString &followUpSql,
                              bool recordHistory)
{
    if (!session)
        return;
    const quint64 queryId = m_nextQueryId++;
    if (!followUpSql.isEmpty())
        m_followUps.insert(queryId, followUpSql);
    if (recordHistory)
        m_pendingHistory.insert(queryId, sql);
    m_resultTabs->startQueryTab(session->id(), queryId, sql);
    ++m_busyCount;
    m_runningQueries.insert(session->id(), queryId);
    m_lastStartedSid = session->id();
    updateBusyIndicator();
    setStatusMessage(tr("Running — %1").arg(displayNameFor(session)));
    session->runSql(queryId, sql);
}

void MainWindow::updateHistoryCombo()
{
    m_historyCombo->clear();
    for (const QString &entry : m_history) {
        QString display = entry.simplified();
        if (display.size() > 60)
            display = display.left(60) + QStringLiteral("…");
        m_historyCombo->addItem(display);
        m_historyCombo->setItemData(m_historyCombo->count() - 1, entry, Qt::ToolTipRole);
    }
    m_historyCombo->setEnabled(!m_history.isEmpty());
}

void MainWindow::saveHistory()
{
    std::unique_ptr<QSettings> settings(ducq::AppSettings::create());
    ducq::HistoryStore::save(settings.get(), m_history);
}

void MainWindow::refreshCatalog()
{
    if (m_activeSession)
        m_activeSession->refreshCatalog();
}

void MainWindow::cancelRunningQuery()
{
    // Cancel the session the indicator is bound to (see updateBusyIndicator).
    if (DbSession *session = sessionById(m_busyIndicatorSid))
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
    if (m_busyCount <= 0)
        m_busyCount = 0;
    m_runningQueries.remove(sessionId);
    updateBusyIndicator();
    m_resultTabs->markFinished(queryId, elapsedMs, totalRows, cancelled, error);
    // History candidates are dropped unless the query succeeded.
    const QString recordedSql = m_pendingHistory.take(queryId);
    const QString followUp = m_followUps.take(queryId);

    if (cancelled) {
        setStatusMessage(tr("Query cancelled"));
        return;
    }
    if (!error.isEmpty()) {
        setStatusError(error);
        return;
    }
    if (!recordedSql.isEmpty()) {
        m_history = ducq::HistoryStore::push(m_history, recordedSql);
        updateHistoryCombo();
        saveHistory();
    }
    setStatusMessage(tr("Done: %L1 rows · %L2 ms").arg(totalRows).arg(elapsedMs));
    if (DbSession *session = sessionById(sessionId)) {
        if (!followUp.isEmpty()) {
            runOnSession(session, followUp);
            return;
        }
        session->refreshCatalog();
    }
}

void MainWindow::onProgressChanged(int sessionId, double percentage)
{
    if (m_busyIndicatorSid < 0)
        updateBusyIndicator();
    if (sessionId != m_busyIndicatorSid)
        return; // other sessions keep their progress inside their own tabs
    m_progressBar->setValue(int(qBound(0.0, percentage, 100.0)));
}

void MainWindow::onCatalogReady(int sessionId, const QVector<TableInfo> &tables)
{
    if (!m_activeSession || sessionId != m_activeSession->id())
        return;
    m_schemaTree->setCatalog(tables);
    setStatusMessage(tr("Schema refreshed: %1 tables/views").arg(tables.size()));
}

void MainWindow::updateBusyIndicator()
{
    // Prefer the session of the result tab the user is looking at; fall back
    // to the most recently started query so Cancel always targets something
    // that is actually running.
    int boundSid = -1;
    if (const ResultTab *tab = m_resultTabs->currentTab()) {
        if (m_runningQueries.contains(tab->sessionId))
            boundSid = tab->sessionId;
    }
    if (boundSid < 0) {
        if (m_runningQueries.contains(m_lastStartedSid))
            boundSid = m_lastStartedSid;
        else if (!m_runningQueries.isEmpty())
            boundSid = m_runningQueries.constBegin().key();
    }

    const bool busy = !m_runningQueries.isEmpty();
    if (busy) {
        if (boundSid != m_busyIndicatorSid) {
            m_progressBar->setValue(0);
            m_busyIndicatorSid = boundSid;
        }
        if (DbSession *session = sessionById(m_busyIndicatorSid))
            m_cancelButton->setToolTip(tr("Cancel the running query on %1").arg(displayNameFor(session)));
    } else {
        m_busyIndicatorSid = -1;
    }
    m_progressBar->setVisible(busy);
    m_cancelButton->setVisible(busy);
}

void MainWindow::setStatusMessage(const QString &message)
{
    m_messageLabel->setText(message);
    m_messageLabel->setToolTip(QString());
    m_messageLabel->setStyleSheet(QString());
    m_statusIsError = false;
}

void MainWindow::setStatusError(const QString &message)
{
    m_messageLabel->setText(message);
    // The status bar truncates long messages; the tooltip keeps them readable.
    m_messageLabel->setToolTip(message);
    m_messageLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::tokens().error.name()));
    m_statusIsError = true;
}

void MainWindow::restoreWindowState()
{
    std::unique_ptr<QSettings> settings(ducq::AppSettings::create());
    const QByteArray geometry = settings->value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    else
        resize(1360, 860);
    const QByteArray horizontal = settings->value(QStringLiteral("window/hSplitter")).toByteArray();
    if (!horizontal.isEmpty())
        m_horizontalSplitter->restoreState(horizontal);
    const QByteArray vertical = settings->value(QStringLiteral("window/vSplitter")).toByteArray();
    if (!vertical.isEmpty())
        m_verticalSplitter->restoreState(vertical);
}

void MainWindow::saveWindowState()
{
    std::unique_ptr<QSettings> settings(ducq::AppSettings::create());
    settings->setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings->setValue(QStringLiteral("window/hSplitter"), m_horizontalSplitter->saveState());
    settings->setValue(QStringLiteral("window/vSplitter"), m_verticalSplitter->saveState());
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
    runOnSession(m_activeSession, sql, {}, true);
}

void MainWindow::scheduleScreenshot(const QString &filePath, int delayMs)
{
    // A throwaway window must not clobber the user's saved layout.
    m_persistState = false;
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
