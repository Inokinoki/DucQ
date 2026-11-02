#include "ResultTabs.h"

#include "ResultModel.h"
#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QShortcut>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>

namespace ducq {
namespace {
constexpr int kMaxFinishedTabs = 10;

// Quotes a value for TSV output (Excel-compatible) when needed.
QString tsvEscape(QString value)
{
    if (value.contains(QLatin1Char('\t')) || value.contains(QLatin1Char('\n'))
        || value.contains(QLatin1Char('"'))) {
        value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + value + QLatin1Char('"');
    }
    return value;
}
} // namespace

ResultTabs::ResultTabs(qsizetype displayRowLimit, QWidget *parent)
    : QTabWidget(parent)
    , m_displayRowLimit(displayRowLimit)
{
    setTabsClosable(true);
    setMovable(true);
    connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) { removeTabAt(index); });
}

void ResultTabs::startQueryTab(int sessionId, quint64 queryId, const QString &sql)
{
    auto *tab = new ResultTab;
    tab->sessionId = sessionId;
    tab->queryId = queryId;
    tab->number = ++m_counter;
    tab->sql = sql;
    auto *page = new QWidget;
    tab->model = new ResultModel(m_displayRowLimit, page);
    tab->proxy = new ResultProxyModel(page);
    tab->proxy->setSourceModel(tab->model);
    tab->proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    tab->view = new QTableView(page);
    tab->view->setModel(tab->proxy);
    tab->view->setAlternatingRowColors(true);
    tab->view->setWordWrap(false);
    tab->view->setSelectionBehavior(QAbstractItemView::SelectRows);
    tab->view->setSortingEnabled(true);
    // Results start in query order; sorting happens on the first header click.
    tab->view->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    tab->view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    tab->view->horizontalHeader()->setDefaultSectionSize(150);
    tab->view->horizontalHeader()->setStretchLastSection(true);
    tab->view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    tab->statusLabel = new QLabel(tr("Running…"), page);
    tab->statusLabel->setContentsMargins(6, 2, 6, 4);

    tab->view->setContextMenuPolicy(Qt::CustomContextMenu);
    QTableView *view = tab->view;
    connect(view, &QTableView::customContextMenuRequested, this, [this, view](const QPoint &pos) {
        showContextMenu(view->viewport()->mapToGlobal(pos));
    });
    auto *copyShortcut = new QShortcut(QKeySequence::Copy, tab->view);
    connect(copyShortcut, &QShortcut::activated, this, [this] { copySelection(false); });

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tab->view, 1);
    layout->addWidget(tab->statusLabel);

    const int index = addTab(page, tr("Result %1").arg(tab->number));
    setCurrentIndex(index);
    m_tabs.insert(queryId, tab);
    m_tabsByWidget.insert(page, tab);

    // Keep the tab bar from growing without bounds.
    int finishedCount = 0;
    for (ResultTab *existing : m_tabs) {
        if (existing->finished)
            ++finishedCount;
    }
    for (int i = 0; i < count() && finishedCount > kMaxFinishedTabs; ++i) {
        const ResultTab *candidate = m_tabsByWidget.value(widget(i));
        if (candidate && candidate->finished && candidate != tab) {
            removeTabAt(i);
            --finishedCount;
            --i;
        }
    }
}

ResultTab *ResultTabs::tabFor(quint64 queryId)
{
    return m_tabs.value(queryId);
}

bool ResultTabs::deliverResult(int sessionId, quint64 queryId, quintptr resultPtr,
                               const QVector<ColumnMeta> &columns)
{
    ResultTab *tab = tabFor(queryId);
    if (!tab || tab->sessionId != sessionId)
        return false;
    tab->model->onResult(resultPtr, columns);
    return true;
}

void ResultTabs::markFinished(quint64 queryId, qint64 elapsedMs, qsizetype totalRows, bool cancelled,
                              const QString &error)
{
    ResultTab *tab = tabFor(queryId);
    if (!tab)
        return;
    tab->finished = true;
    tab->cancelled = cancelled;
    tab->error = error;
    tab->elapsedMs = elapsedMs;
    tab->totalRows = totalRows;
    renderStatus(tab);
}

void ResultTabs::renderStatus(ResultTab *tab)
{
    if (!tab->error.isEmpty()) {
        tab->statusLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(Theme::tokens().error.name()));
        tab->statusLabel->setText(tab->error);
        return;
    }
    tab->statusLabel->setStyleSheet(tab->cancelled
        ? QStringLiteral("color: %1;").arg(Theme::tokens().cancelled.name())
        : QString());
    if (tab->cancelled) {
        tab->statusLabel->setText(tr("Cancelled · %L1 ms").arg(tab->elapsedMs));
        return;
    }
    const qsizetype displayedRows = tab->model->cache.rowCount();
    const bool truncated = tab->model->cache.truncated();
    QString text = tr("%L1 rows · %L2 ms").arg(displayedRows).arg(tab->elapsedMs);
    if (truncated)
        text += tr(" (of %L1 rows, showing first %L2)").arg(tab->totalRows).arg(displayedRows);
    tab->statusLabel->setText(text);
}

const ResultTab *ResultTabs::currentTab() const
{
    return m_tabsByWidget.value(currentWidget());
}

void ResultTabs::closeCurrentTab()
{
    removeTabAt(currentIndex());
}

void ResultTabs::removeTabAt(int index)
{
    if (index < 0 || index >= count())
        return;
    QWidget *page = widget(index);
    if (ResultTab *tab = m_tabsByWidget.value(page)) {
        m_tabs.remove(tab->queryId);
        m_tabsByWidget.remove(page);
        removeTab(index);
        delete tab;
        return;
    }
    removeTab(index);
}

void ResultTabs::closeSessionTabs(int sessionId)
{
    for (int i = count() - 1; i >= 0; --i) {
        if (ResultTab *tab = m_tabsByWidget.value(widget(i))) {
            if (tab->sessionId == sessionId)
                removeTabAt(i);
        }
    }
}

void ResultTabs::showContextMenu(const QPoint &globalPos)
{
    const ResultTab *tab = currentTab();
    if (!tab || !tab->view)
        return;
    QMenu menu(this);
    const QModelIndex current = tab->view->currentIndex();
    QAction *copyCell = menu.addAction(tr("Copy Cell"));
    copyCell->setEnabled(current.isValid());
    QAction *copyRow = menu.addAction(tr("Copy Row"));
    copyRow->setEnabled(current.isValid());
    const QModelIndexList selected = tab->view->selectionModel()->selectedIndexes();
    QAction *copySel = menu.addAction(tr("Copy Selection"));
    copySel->setEnabled(!selected.isEmpty());
    QAction *copySelHead = menu.addAction(tr("Copy Selection with Headers"));
    copySelHead->setEnabled(!selected.isEmpty());

    QAction *chosen = menu.exec(globalPos);
    if (chosen == copyCell && current.isValid()) {
        QApplication::clipboard()->setText(tsvEscape(cellText(currentTab(), current)));
    } else if (chosen == copyRow && current.isValid()) {
        // Selection is row-based; fall back to a single-cell selection if not.
        const int row = current.row();
        QItemSelection rowSelection;
        rowSelection.select(tab->view->model()->index(row, 0),
                            tab->view->model()->index(row, tab->view->model()->columnCount() - 1));
        tab->view->selectionModel()->select(rowSelection, QItemSelectionModel::SelectCurrent);
        copySelection(false);
    } else if (chosen == copySel) {
        copySelection(false);
    } else if (chosen == copySelHead) {
        copySelection(true);
    }
}

QString ResultTabs::cellText(const ResultTab *tab, const QModelIndex &index) const
{
    const QModelIndex source = tab->proxy->mapToSource(index);
    if (tab->model->cache.isNull(source.row(), source.column()))
        return QString();
    return tab->model->cache.cell(source.row(), source.column()).toString();
}

void ResultTabs::copySelection(bool withHeaders)
{
    const ResultTab *tab = currentTab();
    if (!tab || !tab->view)
        return;
    const QItemSelectionModel *selection = tab->view->selectionModel();
    QModelIndexList indexes = selection->selectedIndexes();
    if (indexes.isEmpty())
        return;
    std::sort(indexes.begin(), indexes.end());
    int minRow = indexes.first().row();
    int maxRow = indexes.last().row();
    int minCol = INT_MAX;
    int maxCol = INT_MIN;
    for (const QModelIndex &index : indexes) {
        minCol = qMin(minCol, index.column());
        maxCol = qMax(maxCol, index.column());
    }

    const QAbstractItemModel *model = tab->view->model();
    QString out;
    if (withHeaders) {
        for (int col = minCol; col <= maxCol; ++col) {
            if (col > minCol)
                out += QLatin1Char('\t');
            out += tsvEscape(model->headerData(col, Qt::Horizontal).toString());
        }
        out += QLatin1Char('\n');
    }
    for (int row = minRow; row <= maxRow; ++row) {
        bool rowHasSelection = false;
        QString line;
        for (int col = minCol; col <= maxCol; ++col) {
            const QModelIndex index = model->index(row, col);
            if (!selection->isSelected(index))
                continue;
            rowHasSelection = true;
            if (!line.isEmpty())
                line += QLatin1Char('\t');
            line += tsvEscape(cellText(currentTab(), index));
        }
        if (rowHasSelection) {
            out += line;
            out += QLatin1Char('\n');
        }
    }
    QApplication::clipboard()->setText(out);
}

void ResultTabs::retranslateUi()
{
    for (int i = 0; i < count(); ++i) {
        if (ResultTab *tab = m_tabsByWidget.value(widget(i))) {
            setTabText(i, tr("Result %1").arg(tab->number));
            if (tab->finished)
                renderStatus(tab);
            else
                tab->statusLabel->setText(tr("Running…"));
        }
    }
}

void ResultTabs::retheme()
{
    retranslateUi(); // renderStatus reads the theme tokens at call time
}

} // namespace ducq
