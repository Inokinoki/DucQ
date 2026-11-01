#pragma once

#include <QTreeWidget>
#include <QVector>

#include "core/Catalog.h"

class QMenu;

namespace ducq {

class SchemaTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit SchemaTree(QWidget *parent = nullptr);

public slots:
    void setCatalog(const QVector<ducq::TableInfo> &tables);

signals:
    void tableActivated(const QString &qualifiedName);
    void columnActivated(const QString &columnName);
    void refreshRequested();

private:
    void showContextMenu(const QPoint &pos);
};

} // namespace ducq
