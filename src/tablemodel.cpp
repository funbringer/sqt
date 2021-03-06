#include "datatable.h"
#include "tablemodel.h"
#include <QBrush>
#include <QDateTime>

TableModel::TableModel(QObject *parent) :
    QAbstractItemModel(parent)
{
    _table = new DataTable();
}

TableModel::~TableModel()
{
    delete _table;
}

QModelIndex TableModel::parent(const QModelIndex &) const
{
    return QModelIndex();
}

int TableModel::rowCount(const QModelIndex &) const
{
    return _table->rowCount();
}

int TableModel::columnCount(const QModelIndex &) const
{
    return _table->columnCount();
}

QModelIndex TableModel::index(int row, int column, const QModelIndex &) const
{
    return createIndex(row, column);
}

QVariant TableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    //int sqlType = _table->getColumn(index.column()).sqlType();
    switch (role)
    {
    case Qt::SizeHintRole:
    {
        QVariant &res = _table->getRow(index.row())[index.column()];
        if (!res.isNull() && res.toString().length() > 200)
            return QSize(500, -1);
        return QVariant();
    }
    case Qt::TextAlignmentRole:
        return _table->getColumn(index.column()).hAlignment() + Qt::AlignVCenter;
    case Qt::BackgroundRole:
        if (_table->getRow(index.row())[index.column()].isNull())
            return QBrush(QColor(0, 0, 0, 15));
        return QVariant();
    case Qt::DisplayRole:
        QVariant &res = _table->getRow(index.row())[index.column()];
        if ((QMetaType::Type)res.type() == QMetaType::QTime)
        {
            return qvariant_cast<QTime>(res).toString("hh:mm:ss.zzz");
        }
        else if ((QMetaType::Type)res.type() == QMetaType::QDateTime)
        {
            QDateTime dt = qvariant_cast<QDateTime>(res);
            if (dt.time().msecsTo(QTime(0, 0)) == 0)
                return qvariant_cast<QDateTime>(res).toString("yyyy-MM-dd");
            return qvariant_cast<QDateTime>(res).toString("yyyy-MM-dd hh:mm:ss.zzz");
        }
        return _table->getRow(index.row())[index.column()];
    }
    return QVariant();
}

Qt::ItemFlags TableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0;

    return QAbstractItemModel::flags(index);
}

QVariant TableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();
    if (orientation == Qt::Horizontal)
        return _table->getColumn(section).name();
    return QString::number(section + 1);
}

void TableModel::take(DataTable *srcTable)
{
    // columns are not altered in another thread - no need to use mutex
    if (srcTable->columnCount() != columnCount())
    {
        clear();
        beginInsertColumns(QModelIndex(), 0, srcTable->columnCount() - 1);
        for (int c = 0; c < srcTable->columnCount(); ++c)
            _table->addColumn(new DataColumn(srcTable->getColumn(c)));
        endInsertColumns();
    }
    int rows = srcTable->rowCount();
    if (rows)
    {
        int rowcount = _table->rowCount();
        beginInsertRows(QModelIndex(), rowcount, rowcount + rows - 1);
        _table->takeRows(srcTable);
        endInsertRows();
    }
}

void TableModel::clear()
{
    beginResetModel();
    _table->clear();
    endResetModel();
}
