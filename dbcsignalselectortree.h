#ifndef DBCSIGNALSELECTORTREE_H
#define DBCSIGNALSELECTORTREE_H

#include <QWidget>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QIcon>
#include <QHash>
#include "dbc/dbchandler.h"

namespace Ui {
class DbcSignalSelectorTree;
}

class DbcSignalSelectorTree : public QWidget
{
    Q_OBJECT

public:
    enum SelectionMode { MultiSelect, SingleSelect };

    explicit DbcSignalSelectorTree(QWidget *parent = nullptr);
    ~DbcSignalSelectorTree();

    void setSelectionMode(SelectionMode mode);
    SelectionMode getSelectionMode() const;
    
    // For single select mode
    DBC_SIGNAL* getSelectedSignal() const;
    void setSelectedSignal(DBC_SIGNAL* sig);

    // For multi select mode
    void checkSignal(DBC_SIGNAL *sig);
    void uncheckSignal(DBC_SIGNAL *sig);
    void uncheckAll();

signals:
    void signalChecked(DBC_SIGNAL *sig);
    void signalUnchecked(DBC_SIGNAL *sig);
    void signalDoubleClicked(DBC_SIGNAL *sig);

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemChanged(QStandardItem *item);
    void onDoubleClicked(const QModelIndex &index);

private:
    Ui::DbcSignalSelectorTree *ui;
    SelectionMode m_mode;
    QStandardItemModel *m_model;
    QSortFilterProxyModel *m_proxyModel;

    QIcon nodeIcon;
    QIcon messageIcon;
    QIcon signalIcon;
    QIcon multiplexedSignalIcon;
    QIcon multiplexorSignalIcon;

    QHash<DBC_SIGNAL*, QStandardItem*> m_signalItemMap;

    void populateTree();
    void updateParentCheckState(QStandardItem *parentItem);
    void updateChildrenCheckState(QStandardItem *parentItem, Qt::CheckState state);
    bool m_isUpdatingState;
};

#endif // DBCSIGNALSELECTORTREE_H
