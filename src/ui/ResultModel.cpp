#include "ResultModel.h"

#include "Theme.h"

namespace ducq {

ResultModel::ResultModel(qsizetype displayRowLimit, QObject *parent)
    : QAbstractTableModel(parent)
    , m_displayRowLimit(displayRowLimit)
{
}

int ResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(cache.rowCount());
}

int ResultModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : cache.columnCount();
}

QVariant ResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= cache.rowCount())
        return {};
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        const QVariant value = cache.cell(index.row(), index.column());
        return value.isValid() ? value : QVariant(QStringLiteral("NULL"));
    }
    if (role == Qt::ForegroundRole) {
        if (cache.isNull(index.row(), index.column()))
            return Theme::tokens().nullText;
        return {};
    }
    if (role == Qt::TextAlignmentRole) {
        const auto &columns = cache.columns();
        if (index.column() < columns.size() && columns.at(index.column()).numeric)
            return QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
        return QVariant(int(Qt::AlignLeft | Qt::AlignVCenter));
    }
    return {};
}

QVariant ResultModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    if (orientation == Qt::Horizontal) {
        const auto &columns = cache.columns();
        if (section >= 0 && section < columns.size())
            return columns.at(section).name;
        return {};
    }
    return section + 1;
}

void ResultModel::onResult(quintptr resultPtr, const QVector<ColumnMeta> &columns)
{
    beginResetModel();
    auto *result = reinterpret_cast<duckdb_result *>(resultPtr);
    cache.reset(result, columns, m_displayRowLimit);
    endResetModel();
}

void ResultModel::resetEmpty()
{
    beginResetModel();
    cache.reset(nullptr, {}, m_displayRowLimit);
    endResetModel();
}

QVariant ResultProxyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    // Positional row numbers; the proxy default maps back to source rows,
    // which reads like data once the view is sorted.
    if (orientation == Qt::Vertical && role == Qt::DisplayRole)
        return section + 1;
    return QSortFilterProxyModel::headerData(section, orientation, role);
}

bool ResultProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const auto *model = qobject_cast<const ResultModel *>(sourceModel());
    if (!model)
        return QSortFilterProxyModel::lessThan(left, right);
    const int column = left.column();
    const auto &columns = model->cache.columns();
    const bool numeric = column >= 0 && column < columns.size() && columns.at(column).numeric;
    const bool leftNull = model->cache.isNull(left.row(), column);
    const bool rightNull = model->cache.isNull(right.row(), column);
    if (leftNull || rightNull) {
        if (leftNull && rightNull)
            return false;
        return rightNull; // NULLs last (ascending; proxies invert for descending)
    }
    const QVariant leftValue = model->cache.cell(left.row(), column);
    const QVariant rightValue = model->cache.cell(right.row(), column);
    if (numeric)
        return leftValue.toDouble() < rightValue.toDouble();
    return leftValue.toString().compare(rightValue.toString(), Qt::CaseInsensitive) < 0;
}

} // namespace ducq
