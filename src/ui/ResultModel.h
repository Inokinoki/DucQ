#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <QSortFilterProxyModel>

#include "core/ResultCache.h"

namespace ducq {

// Virtualized table model over a ResultCache: cells are converted on demand
// by the result accessors, so huge results scroll without materializing
// QStrings for every row.
class ResultModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ResultModel(qsizetype displayRowLimit, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

public slots:
    // Takes ownership of a heap duckdb_result handed over by the worker.
    void onResult(quintptr resultPtr, const QVector<ducq::ColumnMeta> &columns);
    void resetEmpty();

public:
    ResultCache cache;

private:
    qsizetype m_displayRowLimit;
};

// Click-to-sort proxy over a ResultModel: numeric columns compare as
// numbers, text case-insensitively, NULLs always last (ascending).
class ResultProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    // Positional row numbers; the proxy default maps back to source rows,
    // which reads like data once the view is sorted.
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

} // namespace ducq
