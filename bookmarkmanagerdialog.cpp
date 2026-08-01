#include "bookmarkmanagerdialog.h"
#include "ui_bookmarkmanagerdialog.h"

#include <QListWidgetItem>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QStringList>
#include <algorithm>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

BookmarkManagerDialog::BookmarkManagerDialog(BookmarkManager *manager, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::BookmarkManagerDialog),
      bookmarkManager(manager)
{
    ui->setupUi(this);

    ui->tableBookmarks->setColumnCount(5);
    ui->tableBookmarks->setHorizontalHeaderLabels(
        QStringList() << tr("Label") << tr("Index") << tr("Bus") << tr("Frame ID") << tr("Timestamp"));
    ui->tableBookmarks->horizontalHeader()->setStretchLastSection(true);
    ui->tableBookmarks->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableBookmarks->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableBookmarks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableBookmarks->setSelectionMode(QAbstractItemView::ExtendedSelection);

    connect(ui->buttonJumpToBookmark, &QPushButton::clicked,
            this, &BookmarkManagerDialog::jumpToSelectedBookmark);
    connect(ui->buttonDeleteBookmark, &QPushButton::clicked,
            this, &BookmarkManagerDialog::deleteSelectedBookmark);

    connect(ui->tableBookmarks, &QTableWidget::itemSelectionChanged,
            this, &BookmarkManagerDialog::handleBookmarkSelectionChanged);
    connect(ui->tableBookmarks, &QTableWidget::itemDoubleClicked,
            this, &BookmarkManagerDialog::jumpToSelectedBookmark);
    connect(ui->buttonAllLabelsFilter, &QPushButton::clicked,
            this, &BookmarkManagerDialog::selectAllLabels);
    connect(ui->buttonNoLabelsFilter, &QPushButton::clicked,
            this, &BookmarkManagerDialog::clearAllLabels);

    connect(ui->buttonEditBookmark, &QPushButton::clicked,
            this, &BookmarkManagerDialog::editSelectedBookmarkLabel);

    connect(ui->listBookmarkLabels, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *) { editSelectedBookmarkLabel(); });

    connect(ui->listBookmarkLabels, &QListWidget::itemChanged,
            this, &BookmarkManagerDialog::filterBookmarksByCheckedLabels);

    connect(ui->buttonAllLabelsFilter, &QPushButton::clicked,
            this, &BookmarkManagerDialog::selectAllLabels);

    connect(ui->buttonNoLabelsFilter, &QPushButton::clicked,
            this, &BookmarkManagerDialog::clearAllLabels);

    if (bookmarkManager)
    {
        connect(bookmarkManager, &BookmarkManager::bookmarksChanged,
                this, &BookmarkManagerDialog::refreshBookmarksView);
    }

    refreshBookmarksView();
}

BookmarkManagerDialog::~BookmarkManagerDialog()
{
    delete ui;
}

int BookmarkManagerDialog::currentBookmarkIndex() const
{
    if (!ui->tableBookmarks) return -1;

    int row = ui->tableBookmarks->currentRow();
    if (row < 0) return -1;

    QTableWidgetItem *item = ui->tableBookmarks->item(row, 0);
    if (!item) return -1;

    bool ok = false;
    int bookmarkIndex = item->data(Qt::UserRole).toInt(&ok);
    return ok ? bookmarkIndex : -1;
}

void BookmarkManagerDialog::editSelectedBookmarkLabel()
{
    int row = ui->tableBookmarks->currentRow();
    if (row < 0) return;

    const auto &bookmarks = bookmarkManager->getBookmarks();
    if (row >= bookmarks.size()) return;

    bool ok = false;
    QString text = QInputDialog::getText(
        this,
        tr("Edit Bookmark Label"),
        tr("Label:"),
        QLineEdit::Normal,
        bookmarks[row].label,
        &ok
    ).trimmed();

    if (!ok) return;

    bookmarkManager->updateBookmarkLabel(row, text);
    refreshBookmarksView();
}

void BookmarkManagerDialog::refreshLabelsView()
{
    if (!ui->listBookmarkLabels) return;

    ui->listBookmarkLabels->clear();
    if (!bookmarkManager) return;

    QSet<QString> labels;
    const QVector<FrameBookmark> &bookmarks = bookmarkManager->getBookmarks();

    for (const FrameBookmark &bm : bookmarks)
    {
        if (!bm.label.trimmed().isEmpty())
            labels.insert(bm.label.trimmed());
    }

    QList<QString> sortedLabels = labels.values();
    std::sort(sortedLabels.begin(), sortedLabels.end());

    for (const QString &label : sortedLabels)
    {
        QListWidgetItem *item = new QListWidgetItem(label, ui->listBookmarkLabels);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
}

void BookmarkManagerDialog::refreshBookmarksView()
{
    if (!ui->tableBookmarks) return;

    ui->tableBookmarks->clearContents();
    ui->tableBookmarks->setRowCount(0);

    if (!bookmarkManager) {
        refreshLabelsView();
        return;
    }

    const QVector<FrameBookmark> &bookmarks = bookmarkManager->getBookmarks();
    ui->tableBookmarks->setRowCount(bookmarks.size());

    for (int row = 0; row < bookmarks.size(); row++) {
        const FrameBookmark &bm = bookmarks.at(row);

        auto *labelItem = new QTableWidgetItem(bm.label);
        labelItem->setData(Qt::UserRole, row);

        ui->tableBookmarks->setItem(row, 0, labelItem);
        ui->tableBookmarks->setItem(row, 1, new QTableWidgetItem(QString::number(bm.originalIndex +1)));
        ui->tableBookmarks->setItem(row, 2, new QTableWidgetItem(QString::number(bm.bus)));
        ui->tableBookmarks->setItem(row, 3, new QTableWidgetItem(QString("0x%1").arg(bm.frameId, 0, 16)));
        ui->tableBookmarks->setItem(row, 4, new QTableWidgetItem(QString::number(bm.timestampMicros / 1000000.0, 'f', 6)));
    }

    ui->tableBookmarks->resizeColumnsToContents();
    refreshLabelsView();
}

void BookmarkManagerDialog::jumpToSelectedBookmark()
{
    int idx = currentBookmarkIndex();
    if (idx < 0) return;
    emit jumpToBookmarkRequested(idx);
}

void BookmarkManagerDialog::deleteSelectedBookmark()
{
    if (!bookmarkManager) return;

    QItemSelectionModel *sel = ui->tableBookmarks->selectionModel();
    if (!sel) return;

    QModelIndexList rows = sel->selectedRows();
    if (rows.isEmpty()) return;

    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &a, const QModelIndex &b) {
                  return a.row() > b.row();
              });

    for (const QModelIndex &index : rows)
    {
        bookmarkManager->removeBookmark(index.row());
        emit deleteBookmarkRequested(index.row());
    }

    refreshBookmarksView();
}

void BookmarkManagerDialog::handleBookmarkSelectionChanged()
{
}

void BookmarkManagerDialog::selectAllLabels()
{
    ui->listBookmarkLabels->blockSignals(true);

    for (int i = 0; i < ui->listBookmarkLabels->count(); ++i)
    {
        QListWidgetItem *item = ui->listBookmarkLabels->item(i);
        if (item) item->setCheckState(Qt::Checked);
    }

    ui->listBookmarkLabels->blockSignals(false);
    filterBookmarksByCheckedLabels();
}

void BookmarkManagerDialog::clearAllLabels()
{
    ui->listBookmarkLabels->blockSignals(true);

    for (int i = 0; i < ui->listBookmarkLabels->count(); ++i)
    {
        QListWidgetItem *item = ui->listBookmarkLabels->item(i);
        if (item) item->setCheckState(Qt::Unchecked);
    }

    ui->listBookmarkLabels->blockSignals(false);
    filterBookmarksByCheckedLabels();
}

void BookmarkManagerDialog::filterBookmarksByCheckedLabels()
{
    QSet<QString> enabledLabels;

    for (int i = 0; i < ui->listBookmarkLabels->count(); ++i)
    {
        QListWidgetItem *item = ui->listBookmarkLabels->item(i);
        if (item && item->checkState() == Qt::Checked)
            enabledLabels.insert(item->text());
    }

    for (int row = 0; row < ui->tableBookmarks->rowCount(); ++row)
    {
        QTableWidgetItem *labelItem = ui->tableBookmarks->item(row, 0);
        const QString label = labelItem ? labelItem->text() : QString();

        const bool showRow = enabledLabels.contains(label);
        ui->tableBookmarks->setRowHidden(row, !showRow);
    }
}