#include "ResultTabs.h"

#include "ResultModel.h"

#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QTableView>
#include <QVBoxLayout>

namespace ducq {
namespace {
constexpr int kMaxFinishedTabs = 10;
} // namespace

ResultTabs::ResultTabs(qsizetype displayRowLimit, QWidget *parent)
    : QTabWidget(parent)
    , m_displayRowLimit(displayRowLimit)
{
    setTabsClosable(true);
    setMovable(true);
    connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *widget = this->widget(index);
        if (ResultTab *tab = m_tabsByWidget.value(widget)) {
            m_tabs.remove(tab->queryId);
            m_tabsByWidget.remove(widget);
            delete tab;
        }
        removeTab(index);
    });
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
    tab->view = new QTableView(page);
    tab->view->setModel(tab->model);
    tab->view->setAlternatingRowColors(true);
    tab->view->setWordWrap(false);
    tab->view->setSelectionBehavior(QAbstractItemView::SelectRows);
    tab->view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    tab->view->horizontalHeader()->setDefaultSectionSize(150);
    tab->view->horizontalHeader()->setStretchLastSection(true);
    tab->view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    tab->statusLabel = new QLabel(tr("Running…"), page);
    tab->statusLabel->setContentsMargins(6, 2, 6, 4);

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
    if (finishedCount > kMaxFinishedTabs) {
        for (int i = 0; i < count() && finishedCount > kMaxFinishedTabs; ++i) {
            if (ResultTab *candidate = m_tabsByWidget.value(widget(i))) {
                if (candidate->finished && candidate != tab) {
                    m_tabs.remove(candidate->queryId);
                    m_tabsByWidget.remove(widget(i));
                    removeTab(i);
                    delete candidate;
                    --finishedCount;
                    --i;
                }
            }
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
        tab->statusLabel->setStyleSheet(QStringLiteral("color: #c62828;"));
        tab->statusLabel->setText(tab->error);
        return;
    }
    tab->statusLabel->setStyleSheet(tab->cancelled ? QStringLiteral("color: #b26a00;") : QString());
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

void ResultTabs::clearFinished()
{
    for (int i = count() - 1; i >= 0; --i) {
        if (ResultTab *tab = m_tabsByWidget.value(widget(i))) {
            if (tab->finished) {
                m_tabs.remove(tab->queryId);
                m_tabsByWidget.remove(widget(i));
                removeTab(i);
                delete tab;
            }
        }
    }
}

void ResultTabs::closeSessionTabs(int sessionId)
{
    for (int i = count() - 1; i >= 0; --i) {
        if (ResultTab *tab = m_tabsByWidget.value(widget(i))) {
            if (tab->sessionId == sessionId) {
                m_tabs.remove(tab->queryId);
                m_tabsByWidget.remove(widget(i));
                removeTab(i);
                delete tab;
            }
        }
    }
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

} // namespace ducq
