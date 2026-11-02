#include "SchemaTree.h"

#include <QAction>
#include <QMenu>
#include <QSet>

namespace ducq {
namespace {
constexpr int kRoleSchema = Qt::UserRole;
constexpr int kRoleTable = Qt::UserRole + 1;
constexpr int kRoleColumnName = Qt::UserRole + 2;
} // namespace

SchemaTree::SchemaTree(QWidget *parent)
    : QTreeWidget(parent)
{
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
    retranslateUi();
}

void SchemaTree::retranslateUi()
{
    setHeaderLabels({ tr("Object"), tr("Type") });
}

void SchemaTree::setCatalog(const QVector<TableInfo> &tables)
{
    // Rebuilding the tree wipes expansion and selection; capture them first so
    // a schema refresh doesn't collapse what the user had open. Items are
    // identified by "schema" / "schema.table" keys.
    const bool hadContent = topLevelItemCount() > 0;
    QSet<QString> expanded;
    QStringList selectedKeys;
    if (hadContent) {
        for (int i = 0; i < topLevelItemCount(); ++i) {
            const QTreeWidgetItem *schemaItem = topLevelItem(i);
            const QString schema = schemaItem->text(0);
            if (schemaItem->isExpanded())
                expanded.insert(schema);
            for (int t = 0; t < schemaItem->childCount(); ++t) {
                const QTreeWidgetItem *tableItem = schemaItem->child(t);
                if (tableItem->isExpanded())
                    expanded.insert(schema + QLatin1Char('.') + tableItem->text(0));
            }
        }
        const QList<QTreeWidgetItem *> selection = selectedItems();
        selectedKeys.reserve(selection.size());
        for (const QTreeWidgetItem *item : selection) {
            const QString table = item->data(0, kRoleTable).toString();
            if (!table.isEmpty())
                selectedKeys << item->data(0, kRoleSchema).toString() + QLatin1Char('.') + table;
            else if (!item->parent())
                selectedKeys << item->text(0);
        }
    }

    clear();
    QHash<QString, QTreeWidgetItem *> itemsByKey;
    QHash<QString, QTreeWidgetItem *> schemaItems;
    for (const TableInfo &table : tables) {
        QTreeWidgetItem *schemaItem = schemaItems.value(table.schema);
        if (!schemaItem) {
            schemaItem = new QTreeWidgetItem(this);
            schemaItem->setText(0, table.schema);
            schemaItem->setText(1, QStringLiteral("schema"));
            schemaItem->setFirstColumnSpanned(false);
            schemaItems.insert(table.schema, schemaItem);
            itemsByKey.insert(table.schema, schemaItem);
        }
        auto *tableItem = new QTreeWidgetItem(schemaItem);
        tableItem->setText(0, table.name);
        tableItem->setText(1, table.type);
        tableItem->setData(0, kRoleSchema, table.schema);
        tableItem->setData(0, kRoleTable, table.name);
        itemsByKey.insert(table.schema + QLatin1Char('.') + table.name, tableItem);
        for (const ColumnInfo &column : table.columns) {
            auto *columnItem = new QTreeWidgetItem(tableItem);
            columnItem->setText(0, column.name);
            columnItem->setText(1, column.typeName);
            columnItem->setData(0, kRoleColumnName, column.name);
        }
    }

    // First fill: open all schemas. Afterwards restore what was expanded.
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *schemaItem = topLevelItem(i);
        schemaItem->setExpanded(!hadContent || expanded.contains(schemaItem->text(0)));
        for (int t = 0; t < schemaItem->childCount(); ++t) {
            QTreeWidgetItem *tableItem = schemaItem->child(t);
            tableItem->setExpanded(expanded.contains(schemaItem->text(0) + QLatin1Char('.')
                                                    + tableItem->text(0)));
        }
    }
    for (const QString &key : selectedKeys) {
        if (QTreeWidgetItem *item = itemsByKey.value(key))
            item->setSelected(true);
    }
    resizeColumnToContents(0);
}

void SchemaTree::showContextMenu(const QPoint &pos)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction *refresh = menu->addAction(tr("Refresh Schema"));
    connect(refresh, &QAction::triggered, this, &SchemaTree::refreshRequested);
    menu->popup(viewport()->mapToGlobal(pos));
}

} // namespace ducq
