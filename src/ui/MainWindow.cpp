#include "MainWindow.h"

#include "DropImport.h"
#include "ResultTabs.h"
#include "SchemaTree.h"
#include "SqlEditor.h"
#include "core/Catalog.h"
#include "core/QueryRunner.h"
#include "core/ResultCache.h"
#include "export/Exporter.h"

#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
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
#include <QSplitter>
#include <QStatusBar>
#include <QThread>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace ducq {

MainWindow::MainWindow()
{
    qRegisterMetaType<QVector<ducq::ColumnMeta>>("QVector<ducq::ColumnMeta>");
    qRegisterMetaType<QVector<ducq::TableInfo>>("QVector<ducq::TableInfo>");

    setWindowTitle(QStringLiteral("DucQ"));
    resize(1360, 860);
    setAcceptDrops(true);

    buildUi();

    m_workerThread = new QThread(this);
    m_runner = new QueryRunner(&m_database); // no parent: lives in the worker
    m_runner->moveToThread(m_workerThread);
    connectRunner();
    m_workerThread->start();

    // Start with an in-memory database so the app is usable immediately.
    QString error;
    if (!m_database.open(QString(), false, &error)) {
        QMessageBox::critical(this, QStringLiteral("DucQ"), QStringLiteral("初始化内存数据库失败：%1").arg(error));
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
    m_historyCombo->setToolTip(QStringLiteral("查询历史"));
    m_historyCombo->setEnabled(false);
    connect(m_historyCombo, &QComboBox::activated, this, [this](int index) {
        if (index >= 0 && index < m_history.size())
            m_editor->setPlainText(m_history.at(index));
    });

    auto *runButton = new QPushButton(QStringLiteral("执行 (Ctrl+⏎)"));
    connect(runButton, &QPushButton::clicked, this, &MainWindow::runEditorSql);
    connect(m_editor, &SqlEditor::executeRequested, this, &MainWindow::runEditorSql);

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

    auto *horizontalSplitter = new QSplitter(Qt::Horizontal);
    horizontalSplitter->addWidget(m_schemaTree);
    horizontalSplitter->addWidget(verticalSplitter);
    horizontalSplitter->setStretchFactor(0, 1);
    horizontalSplitter->setStretchFactor(1, 4);
    setCentralWidget(horizontalSplitter);

    connect(m_schemaTree, &SchemaTree::tableActivated, this, [this](const QString &qualifiedName) {
        m_editor->setPlainText(QStringLiteral("SELECT * FROM %1 LIMIT 100;").arg(qualifiedName));
    });
    connect(m_schemaTree, &SchemaTree::columnActivated, this, [this](const QString &columnName) {
        m_editor->editorWidget()->insertPlainText(quoteIdentifier(columnName));
    });
    connect(m_schemaTree, &SchemaTree::refreshRequested, this, &MainWindow::refreshCatalog);

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    QAction *newAction = fileMenu->addAction(QStringLiteral("新建数据库…"));
    connect(newAction, &QAction::triggered, this, &MainWindow::newDatabaseFile);
    QAction *openAction = fileMenu->addAction(QStringLiteral("打开数据库…"));
    connect(openAction, &QAction::triggered, this, &MainWindow::openDatabaseFile);
    QAction *closeAction = fileMenu->addAction(QStringLiteral("关闭数据库（回到内存库）"));
    connect(closeAction, &QAction::triggered, this, &MainWindow::closeDatabase);
    fileMenu->addSeparator();
    QAction *exportCsv = fileMenu->addAction(QStringLiteral("导出当前结果为 CSV…"));
    connect(exportCsv, &QAction::triggered, this, [this] { exportCurrentResult(QStringLiteral("CSV")); });
    QAction *exportParquet = fileMenu->addAction(QStringLiteral("导出当前结果为 Parquet…"));
    connect(exportParquet, &QAction::triggered, this, [this] { exportCurrentResult(QStringLiteral("PARQUET")); });
    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(QStringLiteral("退出"));
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);

    auto *toolbar = addToolBar(QStringLiteral("main"));
    toolbar->setMovable(false);
    toolbar->addAction(openAction);
    toolbar->addAction(newAction);
    toolbar->addSeparator();
    QAction *refreshAction = toolbar->addAction(QStringLiteral("刷新模式"));
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshCatalog);

    m_connectionLabel = new QLabel(this);
    m_messageLabel = new QLabel(this);
    m_statsLabel = new QLabel(this);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setFixedWidth(160);
    m_progressBar->setVisible(false);
    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_cancelButton->setVisible(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::cancelRunningQuery);

    statusBar()->addWidget(m_connectionLabel);
    statusBar()->addWidget(m_messageLabel, 1);
    statusBar()->addPermanentWidget(m_statsLabel);
    statusBar()->addPermanentWidget(m_progressBar);
    statusBar()->addPermanentWidget(m_cancelButton);
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
                             QStringLiteral("查询进行中，请等待完成或点击取消后再操作。"));
    return false;
}

void MainWindow::newDatabaseFile()
{
    if (!ensureNotBusy())
        return;
    QFileDialog dialog(this, QStringLiteral("新建 DuckDB 数据库"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
    dialog.setDefaultSuffix(QStringLiteral("duckdb"));
    dialog.setNameFilter(QStringLiteral("DuckDB 数据库 (*.duckdb *.db);;所有文件 (*)"));
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
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("打开 DuckDB 数据库"), QString(),
                                                      QStringLiteral("DuckDB 数据库 (*.duckdb *.db *.ddb);;所有文件 (*)"));
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
        QMessageBox::critical(this, QStringLiteral("打开失败"), error);
        m_database.open(QString(), false, nullptr);
    }
    QMetaObject::invokeMethod(m_runner, "initialize", Qt::BlockingQueuedConnection);
    updateConnectionLabel();
    refreshCatalog();
}

void MainWindow::updateConnectionLabel()
{
    if (!m_database.isOpen()) {
        m_connectionLabel->setText(QStringLiteral("未连接"));
        return;
    }
    if (m_database.isInMemory())
        m_connectionLabel->setText(QStringLiteral("内存数据库"));
    else
        m_connectionLabel->setText(m_database.path());
    setWindowTitle(QStringLiteral("DucQ — %1").arg(m_database.isInMemory()
                                                       ? QStringLiteral("内存数据库")
                                                       : QFileInfo(m_database.path()).fileName()));
}

void MainWindow::exportCurrentResult(const QString &format)
{
    if (!ensureNotBusy())
        return;
    const ResultTab *tab = m_resultTabs->currentTab();
    if (!tab || !tab->finished || tab->sql.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("请先选中一个已完成查询的结果页签。"));
        return;
    }
    const QString filter = format == QLatin1String("PARQUET") ? QStringLiteral("Parquet (*.parquet)")
                                                              : QStringLiteral("CSV (*.csv)");
    QString file = QFileDialog::getSaveFileName(this, QStringLiteral("导出结果"), QString(), filter);
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
        setStatusMessage(QStringLiteral("没有可执行的 SQL"));
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
    setStatusMessage(QStringLiteral("执行中…"));
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
    Q_UNUSED(queryId);
    Q_UNUSED(result);
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
        setStatusMessage(QStringLiteral("查询已取消"));
        return;
    }
    if (!error.isEmpty()) {
        setStatusMessage(error);
        return;
    }
    setStatusMessage(QStringLiteral("完成：%L1 行 · %L2 ms").arg(totalRows).arg(elapsedMs));
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
    setStatusMessage(QStringLiteral("模式已刷新：%1 个表/视图").arg(tables.size()));
}

void MainWindow::setBusy(bool busy)
{
    m_progressBar->setVisible(busy);
    m_progressBar->setValue(0);
    m_cancelButton->setVisible(busy);
    m_statsLabel->setText(busy ? QString() : m_statsLabel->text());
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
            setStatusMessage(QStringLiteral("跳过不支持的文件：%1").arg(QFileInfo(file).fileName()));
            continue;
        }
        runSql(viewSql, DropImport::buildPreviewSql(viewName));
        ++imported;
    }
    if (imported > 0)
        event->acceptProposedAction();
}

} // namespace ducq
