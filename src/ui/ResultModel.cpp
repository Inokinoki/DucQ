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

} // namespace ducq
