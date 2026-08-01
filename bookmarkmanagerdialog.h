#ifndef BOOKMARKMANAGERDIALOG_H
#define BOOKMARKMANAGERDIALOG_H

#include <QDialog>
#include <QWidget>
#include "bookmarkmanager.h"

namespace Ui {
class BookmarkManagerDialog;
}

class BookmarkManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BookmarkManagerDialog(BookmarkManager *manager, QWidget *parent = nullptr);
    ~BookmarkManagerDialog();

    void refreshBookmarksView();

signals:
    void jumpToBookmarkRequested(int bookmarkIndex);
    void deleteBookmarkRequested(int bookmarkIndex);

private slots:
    void jumpToSelectedBookmark();
    void deleteSelectedBookmark();
    void handleBookmarkSelectionChanged();
    void selectAllLabels();
    void clearAllLabels();
    void editSelectedBookmarkLabel();
    void filterBookmarksByCheckedLabels();

private:
    Ui::BookmarkManagerDialog *ui;
    BookmarkManager *bookmarkManager;

    int currentBookmarkIndex() const;
    void refreshLabelsView();
};

#endif // BOOKMARKMANAGERDIALOG_H