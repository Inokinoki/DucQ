#include "SchemaTree.h"

#include <QAction>
#include <QMenu>

namespace ducq {
namespace {
constexpr int kRoleSchema = Qt::UserRole;
constexpr int kRoleTable = Qt::UserRole + 1;
constexpr int kRoleColumnName = Qt::UserRole + 2;
} // namespace

SchemaTree::SchemaTree(QWidget *parent)
    : QTreeWidget(parent)
{
    setHeaderLabels({ QStringLiteral("对象"), QStringLiteral("类型") });
    setColumnWidth(0, 200);
    setAlternatingRowColors(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTreeWidget::customContextMenuRequested, this, &SchemaTree::showContextMenu);
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        const QString table = item->data(0, kRoleTable).toString();
        if (!table.isEmpty()) {
            const QString schema = item->data(0, kRoleSchema).toString();
            emit tableActivated(quoteIdentifier(schema) + QLatin1Char('.') + quoteIdentifier(table));
            return;
        }
        const QString column = item->data(0, kRoleColumnName).toString();
        if (!column.isEmpty())
            emit columnActivated(column);
    });
}

void SchemaTree::setCatalog(const QVector<TableInfo> &tables)
{
    clear();
    QHash<QString, QTreeWidgetItem *> schemaItems;
    for (const TableInfo &table : tables) {
        QTreeWidgetItem *schemaItem = schemaItems.value(table.schema);
        if (!schemaItem) {
            schemaItem = new QTreeWidgetItem(this);
            schemaItem->setText(0, table.schema);
            schemaItem->setText(1, QStringLiteral("schema"));
            schemaItem->setFirstColumnSpanned(false);
            schemaItems.insert(table.schema, schemaItem);
            expandItem(schemaItem);
        }
        auto *tableItem = new QTreeWidgetItem(schemaItem);
        tableItem->setText(0, table.name);
        tableItem->setText(1, table.type);
        tableItem->setData(0, kRoleSchema, table.schema);
        tableItem->setData(0, kRoleTable, table.name);
        for (const ColumnInfo &column : table.columns) {
            auto *columnItem = new QTreeWidgetItem(tableItem);
            columnItem->setText(0, column.name);
            columnItem->setText(1, column.typeName);
            columnItem->setData(0, kRoleColumnName, column.name);
        }
    }
    resizeColumnToContents(0);
}

void SchemaTree::showContextMenu(const QPoint &pos)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction *refresh = menu->addAction(QStringLiteral("刷新模式"));
    connect(refresh, &QAction::triggered, this, &SchemaTree::refreshRequested);
    menu->popup(viewport()->mapToGlobal(pos));
}

} // namespace ducq
