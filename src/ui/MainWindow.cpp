#include "MainWindow.h"

#include "DropImport.h"
#include "ResultTabs.h"
#include "SchemaTree.h"
#include "SqlEditor.h"
#include "core/Catalog.h"
#include "core/QueryRunner.h"
#include "core/ResultCache.h"
#include "export/Exporter.h"

#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
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
#include <QThread>
#include <QToolBar>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>

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

MainWindow::MainWindow()
{
    qRegisterMetaType<QVector<ducq::ColumnMeta>>("QVector<ducq::ColumnMeta>");
    qRegisterMetaType<QVector<ducq::TableInfo>>("QVector<ducq::TableInfo>");

    setWindowTitle(QStringLiteral("DucQ"));
    resize(1360, 860);
    setAcceptDrops(true);

    buildUi();
    m_translator = new QTranslator(this);
    buildLanguageMenu();
    retranslateUi();

    m_workerThread = new QThread(this);
    m_runner = new QueryRunner(&m_database); // no parent: lives in the worker
    m_runner->moveToThread(m_workerThread);
    connectRunner();
    m_workerThread->start();

    // Start with an in-memory database so the app is usable immediately.
    QString error;
    if (!m_database.open(QString(), false, &error)) {
        QMessageBox::critical(this, QStringLiteral("DucQ"),
                              tr("Failed to initialize the in-memory database: %1").arg(error));
        close();
        return;
    }
    QMetaObject::invokeMethod(m_runner, "initialize", Qt::BlockingQueuedConnection);
    updateConnectionLabel();
    refreshCatalog();
}

MainWindow::~MainWindow()
{
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
    delete m_runner; // thread stopped, safe to delete from here
}

void MainWindow::buildUi()
{
    m_schemaTree = new SchemaTree;
    m_editor = new SqlEditor;
    m_resultTabs = new ResultTabs(kDefaultDisplayRowLimit);
    m_historyCombo = new QComboBox;
    m_historyCombo->setMinimumContentsLength(40);
    m_historyCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_historyCombo->setEnabled(false);
    connect(m_historyCombo, &QComboBox::activated, this, [this](int index) {
        if (index >= 0 && index < m_history.size())
            m_editor->setPlainText(m_history.at(index));
    });

    auto *runButton = new QPushButton;
    connect(runButton, &QPushButton::clicked, this, &MainWindow::runEditorSql);
    connect(m_editor, &SqlEditor::executeRequested, this, &MainWindow::runEditorSql);
    runButton->setObjectName(QStringLiteral("runButton"));
    m_runButton = runButton;

    auto *editorBar = new QWidget;
    auto *editorBarLayout = new QHBoxLayout(editorBar);
    editorBarLayout->setContentsMargins(6, 4, 6, 4);
    editorBarLayout->addWidget(runButton);
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
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openDatabaseFile);
    m_closeAction = m_fileMenu->addAction(QString());
    connect(m_closeAction, &QAction::triggered, this, &MainWindow::closeDatabase);
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
    const QString current = QSettings().value(QStringLiteral("language"), QStringLiteral("en")).toString();
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

    if (saveSetting)
        QSettings().setValue(QStringLiteral("language"), code);

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
    m_historyCombo->setToolTip(tr("Query history"));

    m_fileMenu->setTitle(tr("&File"));
    m_newAction->setText(tr("New Database…"));
    m_openAction->setText(tr("Open Database…"));
    m_closeAction->setText(tr("Close Database (back to in-memory)"));
    m_exportCsvAction->setText(tr("Export Current Results as CSV…"));
    m_exportParquetAction->setText(tr("Export Current Results as Parquet…"));
    m_quitAction->setText(tr("Exit"));
    m_languageMenu->setTitle(tr("&Language"));
    m_refreshToolbarAction->setText(tr("Refresh Schema"));

    m_cancelButton->setText(tr("Cancel"));
    updateConnectionLabel();
}

void MainWindow::connectRunner()
{
    connect(m_runner, &QueryRunner::resultReady, this, &MainWindow::onResultReady);
    connect(m_runner, &QueryRunner::finished, this, &MainWindow::onQueryFinished);
    connect(m_runner, &QueryRunner::progressChanged, this, &MainWindow::onProgressChanged);
    connect(m_runner, &QueryRunner::catalogReady, this, &MainWindow::onCatalogReady);
}

bool MainWindow::ensureNotBusy()
{
    if (m_pendingQueries == 0)
        return true;
    QMessageBox::information(this, QStringLiteral("DucQ"),
                             tr("A query is running. Wait for it to finish or cancel it first."));
    return false;
}

void MainWindow::newDatabaseFile()
{
    if (!ensureNotBusy())
        return;
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
    openDatabase(files.first());
}

void MainWindow::openDatabaseFile()
{
    if (!ensureNotBusy())
        return;
    const QString file =
        QFileDialog::getOpenFileName(this, tr("Open DuckDB Database"), QString(),
                                     tr("DuckDB databases (*.duckdb *.db *.ddb);;All files (*)"));
    if (!file.isEmpty())
        openDatabase(file);
}

void MainWindow::closeDatabase()
{
    if (!ensureNotBusy())
        return;
    openDatabase(QString());
}

void MainWindow::openDatabase(const QString &path)
{
    QMetaObject::invokeMethod(m_runner, "shutdown", Qt::BlockingQueuedConnection);
    QString error;
    if (!m_database.open(path, false, &error)) {
        QMessageBox::critical(this, tr("Open Failed"), error);
        m_database.open(QString(), false, nullptr);
    }
    QMetaObject::invokeMethod(m_runner, "initialize", Qt::BlockingQueuedConnection);
    updateConnectionLabel();
    refreshCatalog();
}

void MainWindow::updateConnectionLabel()
{
    if (!m_database.isOpen()) {
        m_connectionLabel->setText(tr("Not connected"));
        return;
    }
    if (m_database.isInMemory())
        m_connectionLabel->setText(tr("In-memory database"));
    else
        m_connectionLabel->setText(m_database.path());
    setWindowTitle(QStringLiteral("DucQ — %1").arg(m_database.isInMemory()
                                                       ? tr("In-memory database")
                                                       : QFileInfo(m_database.path()).fileName()));
}

void MainWindow::exportCurrentResult(const QString &format)
{
    if (!ensureNotBusy())
        return;
    const ResultTab *tab = m_resultTabs->currentTab();
    if (!tab || !tab->finished || tab->sql.isEmpty()) {
        QMessageBox::information(this, tr("Export"), tr("Select a finished result tab first."));
        return;
    }
    const QString filter =
        format == QLatin1String("PARQUET") ? tr("Parquet (*.parquet)") : tr("CSV (*.csv)");
    QString file = QFileDialog::getSaveFileName(this, tr("Export Results"), QString(), filter);
    if (file.isEmpty())
        return;
    QFileInfo info(file);
    if (info.suffix().isEmpty())
        file += format == QLatin1String("PARQUET") ? QStringLiteral(".parquet") : QStringLiteral(".csv");
    runSql(Exporter::buildCopySql(tab->sql, file, format));
}

void MainWindow::runEditorSql()
{
    const QString sql = m_editor->toPlainText().trimmed();
    if (sql.isEmpty()) {
        setStatusMessage(tr("No SQL to execute"));
        return;
    }
    runSql(sql);
    pushHistory(sql);
}

void MainWindow::runSql(const QString &sql, const QString &followUpSql)
{
    if (!m_database.isOpen()) {
        QString error;
        if (!m_database.open(QString(), false, &error)) {
            QMessageBox::critical(this, QStringLiteral("DucQ"), error);
            return;
        }
        QMetaObject::invokeMethod(m_runner, "initialize", Qt::BlockingQueuedConnection);
        updateConnectionLabel();
    }
    const quint64 queryId = m_nextQueryId++;
    if (!followUpSql.isEmpty())
        m_followUps.insert(queryId, followUpSql);
    m_resultTabs->startQueryTab(queryId, sql);
    ++m_pendingQueries;
    setBusy(true);
    setStatusMessage(tr("Running…"));
    QMetaObject::invokeMethod(m_runner, "execute", Qt::QueuedConnection, Q_ARG(quint64, queryId),
                              Q_ARG(QString, sql));
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
    QMetaObject::invokeMethod(m_runner, "refreshCatalog", Qt::QueuedConnection);
}

void MainWindow::cancelRunningQuery()
{
    m_runner->cancel();
}

void MainWindow::onResultReady(quint64 queryId, quintptr resultPtr, const QVector<ColumnMeta> &columns)
{
    auto *result = reinterpret_cast<duckdb_result *>(resultPtr);
    if (!m_resultTabs->deliverResult(queryId, resultPtr, columns))
        ResultCache::destroyResult(result);
}

void MainWindow::onQueryFinished(quint64 queryId, qint64 elapsedMs, qsizetype totalRows, bool cancelled,
                                 const QString &error)
{
    --m_pendingQueries;
    if (m_pendingQueries <= 0) {
        m_pendingQueries = 0;
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
    const auto followUp = m_followUps.take(queryId);
    if (!followUp.isEmpty()) {
        runSql(followUp);
        return;
    }
    refreshCatalog();
}

void MainWindow::onProgressChanged(quint64 queryId, double percentage)
{
    Q_UNUSED(queryId);
    m_progressBar->setValue(int(qBound(0.0, percentage, 100.0)));
}

void MainWindow::onCatalogReady(const QVector<TableInfo> &tables)
{
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
        runSql(viewSql, DropImport::buildPreviewSql(viewName));
        ++imported;
    }
    if (imported > 0)
        event->acceptProposedAction();
}

} // namespace ducq
