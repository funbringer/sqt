#include "dbobjectsmodel.h"
#include "dbobject.h"
#include <QSettings>
#include <QDebug>
#include <QIcon>
#include <QJSEngine>
#include <QFile>
#include <QDir>
#include <QMap>
#include <QApplication>
#include <QRegularExpression>
#include "dbconnectionfactory.h"
#include "dbconnection.h"
#include <memory>
#include "datatable.h"
#include "odbcconnection.h"
#include <QUuid>
#include "dbosortfilterproxymodel.h"
#include "scripting.h"

#include <QJSEngine>
#include <QJSValueList>
#include <QQmlEngine>

DbObjectsModel::DbObjectsModel(QObject *parent) :
    QAbstractItemModel(parent)
{
    _rootItem = new DbObject();
    _rootItem->setData("root", DbObject::TypeRole);
}

DbObjectsModel::~DbObjectsModel()
{
    delete _rootItem;
}

QModelIndex DbObjectsModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();
    DbObject *parentItem = static_cast<DbObject*>(index.internalPointer())->parent();
    if (parentItem == _rootItem)
        return QModelIndex();
    return createIndex(parentItem->row(), 0, parentItem);
}

int DbObjectsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;

    DbObject *parentItem = parent.isValid() ?
                static_cast<DbObject*>(parent.internalPointer()) :
                _rootItem;

    return parentItem->childCount();
}

QModelIndex DbObjectsModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    DbObject *parentItem = parent.isValid() ?
                static_cast<DbObject*>(parent.internalPointer()) :
                _rootItem;

    DbObject *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QVariant DbObjectsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    return item->data(role);
}

bool DbObjectsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    item->setData(value, role);
    emit dataChanged(index, index);
    return true;
}

Qt::ItemFlags DbObjectsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0;

    return QAbstractItemModel::flags(index);
}

bool DbObjectsModel::insertRows(int pos, int count, const QModelIndex &parent)
{
    DbObject *parentItem = parent.isValid() ?
                static_cast<DbObject*>(parent.internalPointer()) :
                _rootItem;

    if (count > parentItem->childCount())
        return false;
    beginInsertRows(parent, pos, pos + count);
    for (int i = 0; i < count; ++i)
        parentItem->insertChild(pos);
    endInsertRows();
    return true;
}

bool DbObjectsModel::removeRows(int pos, int count, const QModelIndex &parent)
{
    DbObject *parentItem = parent.isValid() ?
                static_cast<DbObject*>(parent.internalPointer()) :
                _rootItem;

    if (pos + count > parentItem->childCount())
        return false;
    beginRemoveRows(parent, pos, pos + count);
    for (int i = 0; i < count; ++i)
        parentItem->removeChild(pos);
    endRemoveRows();
    return true;
}

bool DbObjectsModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return true;
    return static_cast<DbObject*>(parent.internalPointer())->data(DbObject::ParentRole).toBool();
}

bool DbObjectsModel::canFetchMore(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return false;
    DbObject *item = static_cast<DbObject*>(parent.internalPointer());
    return (!item->childCount() && item->data(DbObject::ParentRole).toBool());
}

void DbObjectsModel::fetchMore(const QModelIndex &parent)
{
    if (!parent.isValid())
        return;
    fillChildren(parent);
    DbObject *item = static_cast<DbObject*>(parent.internalPointer());
    item->setData(item->childCount() ? true : false, DbObject::ParentRole);
}

bool DbObjectsModel::fillChildren(const QModelIndex &parent)
{
    DbObject *parentNode = parent.isValid() ?
                static_cast<DbObject*>(parent.internalPointer()) :
                _rootItem;
    QString type = parentNode->data(DbObject::TypeRole).toString();

    if (type == "root")  //servers
    {
        QSettings settings;
        int size = settings.beginReadArray("servers");
        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);
            beginInsertRows(parent, i, i);
            parentNode->insertChild(i);
            DbObject *newItem = parentNode->child(i);
            newItem->setData(QUuid::createUuid(), DbObject::IdRole);
            newItem->setData(settings.value("connection_string").toString(), DbObject::DataRole);
            newItem->setData(settings.value("name").toString(), Qt::DisplayRole);
            newItem->setData(settings.value("user").toString(), DbObject::NameRole);
            newItem->setData("connection", DbObject::TypeRole);
            //newItem->setData(QString::number(std::intptr_t(newItem)), DbObject::IdRole); // id to use within connections storage
            newItem->setData(false, DbObject::ParentRole);
            newItem->setData(QIcon(":img/server.png"), Qt::DecorationRole);
            endInsertRows();
        }
        settings.endArray();
        return true;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    ScopeGuard<void(*)()> cursorGuard(QApplication::restoreOverrideCursor);

    int insertPosition = parentNode->childCount();
    try
    {
        DataTable *table = nodeChildren(parent);
        // remove current node in case of error during scripting
        if (!table)
        {
            if (type != "connection" && parent.data(DbObject::IdRole).isValid())
                removeRow(parent.row(), parent.parent());
            return false;
        }

        // http://msdn.microsoft.com/en-us/library/ms403629(v=sql.105).aspx
        int typeInd = table->getColumnOrd("node_type");
        int textInd = table->getColumnOrd("ui_name");
        int nameInd = table->getColumnOrd("name");
        int idInd = table->getColumnOrd("id");
        int iconInd = table->getColumnOrd("icon");
        int sort1Ind = table->getColumnOrd("sort1");
        int sort2Ind = table->getColumnOrd("sort2");
        int multiselectInd = table->getColumnOrd("allow_multiselect");

        for (int i = 0; i < table->rowCount(); ++i)
        {
            DataRow &r = table->getRow(i);
            std::unique_ptr<DbObject> newItem(new DbObject(parentNode));
            newItem->setData(r[textInd].toString(), Qt::DisplayRole);
            if (idInd >= 0 && !r[idInd].isNull())
                newItem->setData(r[idInd].toString(), DbObject::IdRole);
            if (nameInd >= 0 && !r[nameInd].isNull())
                newItem->setData(r[nameInd].toString(), DbObject::NameRole);
            if (iconInd >= 0 && !r[iconInd].isNull())
                newItem->setData(QIcon(QApplication::applicationDirPath() + "/decor/" + r[iconInd].toString()), Qt::DecorationRole);

            // children detection
            newItem->setData(Scripting::getScript(
                                 dbConnection(parent).get(),
                                 Scripting::Context::Tree,
                                 r[typeInd].toString()) != nullptr, DbObject::ParentRole);

            if (sort1Ind >= 0 && !r[sort1Ind].isNull())
                newItem->setData(r[sort1Ind], DbObject::Sort1Role);
            if (sort2Ind >= 0 && !r[sort2Ind].isNull())
                newItem->setData(r[sort2Ind], DbObject::Sort2Role);
            if (multiselectInd >= 0 && !r[multiselectInd].isNull())
                newItem->setData(r[sort2Ind].toBool(), DbObject::MultiselectRole);
            if (typeInd >= 0)
            {
                QString value = r[typeInd].toString();
                newItem->setData(value, DbObject::TypeRole);
                if (value == "database")
                {
                    // find connection string donor (top-level connection)
                    DbObject *parent = newItem->parent();
                    while (parent && parent->data(DbObject::TypeRole).toString() != "connection")
                        parent = parent->parent();

                    // initialize database-specific connection
                    if (parent)
                    {
                        QString cs = DbConnectionFactory::connection(QString::number(std::intptr_t(parent)))->connectionString();
                        QString id = QString::number(std::intptr_t(newItem.get()));
                        auto db = DbConnectionFactory::createConnection(id, cs, newItem->data(DbObject::NameRole).toString());
                        connect(db.get(), &DbConnection::error, this, &DbObjectsModel::error);
                        connect(db.get(), &DbConnection::message, this, &DbObjectsModel::message);
                    }
                }
            }

            beginInsertRows(parent, insertPosition, insertPosition);
            parentNode->appendChild(newItem.release());
            endInsertRows();
            ++insertPosition;
        }
    }
    catch (const QString &err)
    {
        emit error(err);
    }

    parentNode->setData(parentNode->childCount() > 0, DbObject::ParentRole);
    return true;
}

std::shared_ptr<DbConnection> DbObjectsModel::dbConnection(const QModelIndex &index)
{
    std::shared_ptr<DbConnection> con;
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    while (!con && item)
    {
        QString type = item->data(DbObject::TypeRole).toString();
        if (type == "connection" || type == "database")
        {
            con = DbConnectionFactory::connection(QString::number(std::intptr_t(item)));
            break; // con may be nullptr
        }
        else
            item = item->parent();
    }
    return con;
}

QVariant DbObjectsModel::parentNodeProperty(const QModelIndex &index, QString type)
{
    QVariant envValue;
    QStringList parts = type.split('.', QString::SkipEmptyParts);
    if (parts.size() > 2 || parts.isEmpty())
        return envValue;
    QString searchType = parts.at(0);
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    do
    {
        if (item->data(DbObject::TypeRole) == searchType)
        {
            if (parts.size() == 1 || !parts.at(1).compare("id", Qt::CaseInsensitive))
                envValue = item->data(DbObject::IdRole);
            else if (!parts.at(1).compare("name", Qt::CaseInsensitive))
                envValue = item->data(DbObject::NameRole);
            else
                break;
        }
        else
            item = item->parent();
    }
    while (!envValue.isValid() && item);
    return envValue;
}

bool DbObjectsModel::addServer(QString name, QString connectionString)
{
    QModelIndex parentIndex;
    int ind = _rootItem->childCount();
    // prevent duplicate server name
    for (int i = 0; i < ind; ++i)
    {
        if (_rootItem->child(i)->data(Qt::DisplayRole).toString() == name)
            return false;
    }

    DbObject *new_connection = new DbObject(_rootItem);
    beginInsertRows(parentIndex, ind, ind);
    new_connection->setData(connectionString, DbObject::DataRole);
    new_connection->setData(name, Qt::DisplayRole);
    new_connection->setData("connection", DbObject::TypeRole);
    new_connection->setData(false, DbObject::ParentRole);
    new_connection->setData(QIcon(":img/server.png"), Qt::DecorationRole);
    _rootItem->appendChild(new_connection);
    endInsertRows();

    saveConnectionSettings();
    return true;
}

bool DbObjectsModel::removeConnection(QModelIndex &index)
{
    if (!index.isValid())
        return false;
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    if (item->data(DbObject::TypeRole) != "connection")
        return false;

    removeRows(index.row(), 1);
    saveConnectionSettings();
    return true;
}

bool DbObjectsModel::alterConnection(QModelIndex &index, QString name, QString connectionString)
{
    if (!index.isValid())
        return false;
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    if (item->data(DbObject::TypeRole) != "connection")
        return false;

    setData(index, name, Qt::DisplayRole);
    setData(index, connectionString, DbObject::DataRole);
    saveConnectionSettings();
    return true;
}

void DbObjectsModel::saveConnectionSettings()
{
    QSettings settings;
    int count = _rootItem->childCount();
    settings.beginWriteArray("servers", count);
    for (int i = 0; i < count; ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue("connection_string", _rootItem->child(i)->data(DbObject::DataRole).toString());
        settings.setValue("name", _rootItem->child(i)->data(Qt::DisplayRole).toString());
        settings.setValue("user", _rootItem->child(i)->data(DbObject::NameRole).toString());
    }
    settings.endArray();
}

DataTable *DbObjectsModel::nodeChildren(const QModelIndex &obj)
{
    _curIndex = obj;
    DbObject *parentItem = static_cast<DbObject*>(obj.internalPointer());
    QString type = parentItem->data(DbObject::TypeRole).toString();

    DataTable *table = nullptr;
    std::shared_ptr<DbConnection> con = dbConnection(obj);
    try
    {
        con->clearResultsets();
        if (!con->open())
            throw QString("");
        Scripting::Script *s = Scripting::getScript(con.get(), Scripting::Context::Tree, type);
        if (!s)
            throw tr("script to make %1 content not found").arg(type);

        QString query = s->body;
        if (s->type == Scripting::Script::Type::SQL)
        {
            QRegularExpression expr("\\$(\\w+\\.\\w+)\\$");
            QRegularExpressionMatchIterator i = expr.globalMatch(query);
            QStringList macros;
            // search for parameters within query text
            while (i.hasNext())
            {
                QRegularExpressionMatch match = i.next();
                if (!macros.contains(match.captured(1)))
                    macros << match.captured(1);
            }
            // replace parameters with values
            foreach (QString macro, macros)
            {
                QString value = parentNodeProperty(obj, macro).toString();
                query = query.replace("$" + macro + "$", value.isEmpty() ? "NULL" : value);
            }

            con->execute(query);
        }
        else if (s->type == Scripting::Script::Type::QS)
        {

            QJSEngine e;
            qmlRegisterType<DataTable>();
            QQmlEngine::setObjectOwnership(con.get(), QQmlEngine::CppOwnership);
            QJSValue cn = e.newQObject(con.get());
            e.globalObject().setProperty("__connection", cn);

            QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
            QJSValue objModel = e.newQObject(this);
            e.globalObject().setProperty("__model", objModel);

            e.installExtensions(QJSEngine::ConsoleExtension);

            QJSValue exec_fn = e.evaluate(R"(
                        function(query) {
                            return __connection.execute(query, Array.prototype.slice.call(arguments, 1));
                        })");
            e.globalObject().setProperty("exec", exec_fn);

            QJSValue return_fn = e.evaluate(R"(
                                            function(resultset) {
                                                __connection.appendResultset(resultset);
                                            })");
            e.globalObject().setProperty("return", return_fn);

            QJSValue env_fn = e.evaluate(R"(
                                         function(objectType) {
                                            return __model.parentDbObject(objectType);
                                         })");
            e.globalObject().setProperty("env", env_fn);

            QJSValue execRes = e.evaluate(query);
            if (execRes.isError())
                throw tr("error at line %1: %2").arg(execRes.property("lineNumber").toInt()).arg(execRes.toString());
        }

        // connection object owns resultsets
        if (!con->_resultsets.empty())
            table = con->_resultsets.last();

    }
    catch (const QString &)
    {
        // TODO romove try/catch or implement catch
        //setData(parent, parentItem->childCount() > 0, DbObject::ParentRole);
        throw;
    }
    return table;
}

QVariant DbObjectsModel::parentNodeProperty(QString type)
{
    return parentNodeProperty(_curIndex, type);
}
