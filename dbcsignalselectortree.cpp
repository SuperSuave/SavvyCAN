#include "dbcsignalselectortree.h"
#include "ui_dbcsignalselectortree.h"
#include "utility.h"
#include <QVariant>
#include <QDebug>

Q_DECLARE_METATYPE(DBC_SIGNAL*)

DbcSignalSelectorTree::DbcSignalSelectorTree(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DbcSignalSelectorTree),
    m_mode(MultiSelect),
    m_isUpdatingState(false)
{
    ui->setupUi(this);

    nodeIcon = QIcon(":/icons/images/node.png");
    messageIcon = QIcon(":/icons/images/message.png");
    signalIcon = QIcon(":/icons/images/signal.png");
    multiplexedSignalIcon = QIcon(":/icons/images/multiplexed-signal.png");
    multiplexorSignalIcon = QIcon(":/icons/images/multiplexor-signal.png");

    m_model = new QStandardItemModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setRecursiveFilteringEnabled(true);
    
    ui->treeView->setModel(m_proxyModel);
    ui->treeView->setIndentation(12);

    // Lighten the background of the tree view specifically so that the native dark checkboxes are clearly visible.
    // This avoids breaking the native tri-state checkbox rendering by overriding the indicator style.
    ui->treeView->setStyleSheet(
        "QTreeView {"
        "    background-color: #484848;"
        "    alternate-background-color: #505050;"
        "}"
    );

    connect(ui->searchLineEdit, &QLineEdit::textChanged, this, &DbcSignalSelectorTree::onSearchTextChanged);
    connect(m_model, &QStandardItemModel::itemChanged, this, &DbcSignalSelectorTree::onItemChanged);
    connect(ui->treeView, &QTreeView::doubleClicked, this, &DbcSignalSelectorTree::onDoubleClicked);

    populateTree();
}

DbcSignalSelectorTree::~DbcSignalSelectorTree()
{
    delete ui;
}

void DbcSignalSelectorTree::setSelectionMode(SelectionMode mode)
{
    m_mode = mode;
    populateTree(); // Repopulate to add/remove checkboxes
}

DbcSignalSelectorTree::SelectionMode DbcSignalSelectorTree::getSelectionMode() const
{
    return m_mode;
}

DBC_SIGNAL* DbcSignalSelectorTree::getSelectedSignal() const
{
    QModelIndex proxyIndex = ui->treeView->currentIndex();
    if (!proxyIndex.isValid()) return nullptr;
    
    QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    QStandardItem *item = m_model->itemFromIndex(sourceIndex);
    if (!item) return nullptr;
    
    QVariant data = item->data(Qt::UserRole);
    if (data.isValid()) {
        return data.value<DBC_SIGNAL*>();
    }
    return nullptr;
}

void DbcSignalSelectorTree::setSelectedSignal(DBC_SIGNAL *sig)
{
    if (m_signalItemMap.contains(sig)) {
        QStandardItem *item = m_signalItemMap[sig];
        QModelIndex sourceIndex = item->index();
        QModelIndex proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
        if (proxyIndex.isValid()) {
            ui->treeView->setCurrentIndex(proxyIndex);
            ui->treeView->scrollTo(proxyIndex);
        }
    }
}

void DbcSignalSelectorTree::onSearchTextChanged(const QString &text)
{
    m_proxyModel->setFilterWildcard("*" + text + "*");
    if (!text.isEmpty()) {
        ui->treeView->expandAll();
    } else {
        ui->treeView->collapseAll();
    }
}

void DbcSignalSelectorTree::onItemChanged(QStandardItem *item)
{
    if (m_isUpdatingState || m_mode == SingleSelect) return;

    m_isUpdatingState = true;
    
    Qt::CheckState state = item->checkState();
    
    // If it's a signal, notify and update parent
    QVariant data = item->data(Qt::UserRole);
    if (data.isValid()) {
        DBC_SIGNAL *sig = data.value<DBC_SIGNAL*>();
        if (state == Qt::Checked) emit signalChecked(sig);
        else if (state == Qt::Unchecked) emit signalUnchecked(sig);
        
        if (item->parent()) {
            updateParentCheckState(item->parent());
        }
    } else {
        // It's a node or message, update children
        updateChildrenCheckState(item, state);
        if (item->parent()) {
            updateParentCheckState(item->parent());
        }
    }

    m_isUpdatingState = false;
}

void DbcSignalSelectorTree::onDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    QStandardItem *item = m_model->itemFromIndex(sourceIndex);
    if (!item) return;
    
    QVariant data = item->data(Qt::UserRole);
    if (data.isValid()) {
        emit signalDoubleClicked(data.value<DBC_SIGNAL*>());
    }
}

void DbcSignalSelectorTree::updateChildrenCheckState(QStandardItem *parentItem, Qt::CheckState state)
{
    for (int i = 0; i < parentItem->rowCount(); ++i) {
        QStandardItem *child = parentItem->child(i);
        if (child->checkState() != state) {
            child->setCheckState(state);
            QVariant data = child->data(Qt::UserRole);
            if (data.isValid()) {
                DBC_SIGNAL *sig = data.value<DBC_SIGNAL*>();
                if (state == Qt::Checked) emit signalChecked(sig);
                else if (state == Qt::Unchecked) emit signalUnchecked(sig);
            }
            updateChildrenCheckState(child, state);
        }
    }
}

void DbcSignalSelectorTree::updateParentCheckState(QStandardItem *parentItem)
{
    int checkedCount = 0;
    int uncheckedCount = 0;
    
    for (int i = 0; i < parentItem->rowCount(); ++i) {
        Qt::CheckState state = parentItem->child(i)->checkState();
        if (state == Qt::Checked) checkedCount++;
        else if (state == Qt::Unchecked) uncheckedCount++;
    }
    
    Qt::CheckState newState;
    if (checkedCount == parentItem->rowCount()) newState = Qt::Checked;
    else if (uncheckedCount == parentItem->rowCount()) newState = Qt::Unchecked;
    else newState = Qt::PartiallyChecked;
    
    if (parentItem->checkState() != newState) {
        parentItem->setCheckState(newState);
        if (parentItem->parent()) {
            updateParentCheckState(parentItem->parent());
        }
    }
}

void DbcSignalSelectorTree::checkSignal(DBC_SIGNAL *sig)
{
    if (m_signalItemMap.contains(sig)) {
        QStandardItem *item = m_signalItemMap[sig];
        if (item->checkState() != Qt::Checked) {
            item->setCheckState(Qt::Checked);
        }
    }
}

void DbcSignalSelectorTree::uncheckSignal(DBC_SIGNAL *sig)
{
    if (m_signalItemMap.contains(sig)) {
        QStandardItem *item = m_signalItemMap[sig];
        if (item->checkState() != Qt::Unchecked) {
            item->setCheckState(Qt::Unchecked);
        }
    }
}

void DbcSignalSelectorTree::uncheckAll()
{
    m_isUpdatingState = true;
    for (QStandardItem *item : m_signalItemMap.values()) {
        item->setCheckState(Qt::Unchecked);
    }
    // Update parents manually since we suppressed events
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QStandardItem *fileItem = m_model->item(i);
        for (int j = 0; j < fileItem->rowCount(); ++j) {
            QStandardItem *nodeItem = fileItem->child(j);
            for (int k = 0; k < nodeItem->rowCount(); ++k) {
                QStandardItem *msgItem = nodeItem->child(k);
                updateParentCheckState(msgItem);
            }
        }
    }
    m_isUpdatingState = false;
}

void DbcSignalSelectorTree::populateTree()
{
    m_isUpdatingState = true;
    m_model->clear();
    m_signalItemMap.clear();
    
    DBCHandler *handler = DBCHandler::getReference();
    if (!handler) {
        m_isUpdatingState = false;
        return;
    }

    for (int f = 0; f < handler->getFileCount(); ++f) {
        DBCFile *file = handler->getFileByIdx(f);
        if (!file) continue;
        
        QStandardItem *fileItem = new QStandardItem(file->getFilename());
        fileItem->setEditable(false);
        if (m_mode == MultiSelect) {
            fileItem->setCheckable(true);
            fileItem->setCheckState(Qt::Unchecked);
        }
        
        QList<QString> nodesToProcess;
        for (int n = 0; n < file->dbc_nodes.count(); ++n) {
            nodesToProcess.append(file->dbc_nodes[n].name);
        }
        if (!nodesToProcess.contains("Vector__XXX")) {
            nodesToProcess.append("Vector__XXX");
        }
        
        for (const QString &nodeName : nodesToProcess) {
            QStandardItem *nodeItem = nullptr;
            
            for (int m = 0; m < file->messageHandler->getCount(); ++m) {
                DBC_MESSAGE *msg = file->messageHandler->findMsgByIdx(m);
                if (msg->sender->name == nodeName) {
                    if (!nodeItem) {
                        nodeItem = new QStandardItem(nodeName);
                        nodeItem->setIcon(nodeIcon);
                        nodeItem->setEditable(false);
                        if (m_mode == MultiSelect) {
                            nodeItem->setCheckable(true);
                            nodeItem->setCheckState(Qt::Unchecked);
                        }
                    }
                    
                    QString msgInfo = Utility::formatCANID(msg->ID) + " " + msg->name;
                    QStandardItem *msgItem = new QStandardItem(msgInfo);
                    msgItem->setIcon(messageIcon);
                    msgItem->setEditable(false);
                    if (m_mode == MultiSelect) {
                        msgItem->setCheckable(true);
                        msgItem->setCheckState(Qt::Unchecked);
                    }
                    
                    for (int s = 0; s < msg->sigHandler->getCount(); ++s) {
                        DBC_SIGNAL *sig = msg->sigHandler->findSignalByIdx(s);
                        QString sigInfo = sig->name;
                        
                        QStandardItem *sigItem = new QStandardItem(sigInfo);
                        
                        if (sig->isMultiplexor) sigItem->setIcon(multiplexorSignalIcon);
                        else if (sig->isMultiplexed) sigItem->setIcon(multiplexedSignalIcon);
                        else sigItem->setIcon(signalIcon);
                        
                        sigItem->setEditable(false);
                        if (m_mode == MultiSelect) {
                            sigItem->setCheckable(true);
                            sigItem->setCheckState(Qt::Unchecked);
                        }
                        sigItem->setData(QVariant::fromValue(sig), Qt::UserRole);
                        m_signalItemMap[sig] = sigItem;
                        msgItem->appendRow(sigItem);
                    }
                    nodeItem->appendRow(msgItem);
                }
            }
            
            if (nodeItem) {
                fileItem->appendRow(nodeItem);
            }
        }
        
        if (fileItem->rowCount() > 0) {
            m_model->appendRow(fileItem);
        } else {
            delete fileItem;
        }
    }
    
    m_isUpdatingState = false;
}
