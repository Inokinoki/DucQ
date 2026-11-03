#include "ResultModel.h"

#include "Theme.h"

namespace ducq {
namespace {

// Display name for a column header tooltip. The C API in this DuckDB build
// has no type-name function, so map the common ids ourselves.
QString typeNameFor(duckdb_type type)
{
    switch (type) {
    case DUCKDB_TYPE_BOOLEAN: return QStringLiteral("BOOLEAN");
    case DUCKDB_TYPE_TINYINT: return QStringLiteral("TINYINT");
    case DUCKDB_TYPE_SMALLINT: return QStringLiteral("SMALLINT");
    case DUCKDB_TYPE_INTEGER: return QStringLiteral("INTEGER");
    case DUCKDB_TYPE_BIGINT: return QStringLiteral("BIGINT");
    case DUCKDB_TYPE_UTINYINT: return QStringLiteral("UTINYINT");
    case DUCKDB_TYPE_USMALLINT: return QStringLiteral("USMALLINT");
    case DUCKDB_TYPE_UINTEGER: return QStringLiteral("UINTEGER");
    case DUCKDB_TYPE_UBIGINT: return QStringLiteral("UBIGINT");
    case DUCKDB_TYPE_FLOAT: return QStringLiteral("FLOAT");
    case DUCKDB_TYPE_DOUBLE: return QStringLiteral("DOUBLE");
    case DUCKDB_TYPE_DECIMAL: return QStringLiteral("DECIMAL");
    case DUCKDB_TYPE_HUGEINT: return QStringLiteral("HUGEINT");
    case DUCKDB_TYPE_UHUGEINT: return QStringLiteral("UHUGEINT");
    case DUCKDB_TYPE_VARCHAR: return QStringLiteral("VARCHAR");
    case DUCKDB_TYPE_BLOB: return QStringLiteral("BLOB");
    case DUCKDB_TYPE_DATE: return QStringLiteral("DATE");
    case DUCKDB_TYPE_TIME: return QStringLiteral("TIME");
    case DUCKDB_TYPE_TIME_TZ: return QStringLiteral("TIME WITH TIME ZONE");
    case DUCKDB_TYPE_TIMESTAMP: return QStringLiteral("TIMESTAMP");
    case DUCKDB_TYPE_TIMESTAMP_S: return QStringLiteral("TIMESTAMP_S");
    case DUCKDB_TYPE_TIMESTAMP_MS: return QStringLiteral("TIMESTAMP_MS");
    case DUCKDB_TYPE_TIMESTAMP_NS: return QStringLiteral("TIMESTAMP_NS");
    case DUCKDB_TYPE_TIMESTAMP_TZ: return QStringLiteral("TIMESTAMP WITH TIME ZONE");
    case DUCKDB_TYPE_INTERVAL: return QStringLiteral("INTERVAL");
    case DUCKDB_TYPE_UUID: return QStringLiteral("UUID");
    case DUCKDB_TYPE_LIST: return QStringLiteral("LIST");
    case DUCKDB_TYPE_ARRAY: return QStringLiteral("ARRAY");
    case DUCKDB_TYPE_STRUCT: return QStringLiteral("STRUCT");
    case DUCKDB_TYPE_MAP: return QStringLiteral("MAP");
    case DUCKDB_TYPE_UNION: return QStringLiteral("UNION");
    case DUCKDB_TYPE_ENUM: return QStringLiteral("ENUM");
    case DUCKDB_TYPE_BIT: return QStringLiteral("BIT");
    case DUCKDB_TYPE_BIGNUM: return QStringLiteral("BIGNUM");
    default: return QString();
    }
}

// Values longer than this get a tooltip with the full text.
constexpr int kTooltipThreshold = 64;
// Tooltips are capped: rendering megabyte-sized strings stalls the widget.
constexpr int kTooltipMaxLength = 2000;

} // namespace

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
    if (role == Qt::ToolTipRole) {
        // Clipped cells stay readable: surface the value on hover, capped so
        // a huge cell cannot stall the tooltip.
        if (cache.isNull(index.row(), index.column()))
            return {};
        QString text = cache.cell(index.row(), index.column()).toString();
        if (text.size() <= kTooltipThreshold)
            return {};
        if (text.size() > kTooltipMaxLength)
            text = text.left(kTooltipMaxLength) + QStringLiteral("…");
        return text;
    }
    return {};
}

QVariant ResultModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::ToolTipRole) {
        const auto &columns = cache.columns();
        if (section >= 0 && section < columns.size())
            return typeNameFor(columns.at(section).typeId);
        return {};
    }
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
