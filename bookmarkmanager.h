#ifndef BOOKMARKMANAGER_H
#define BOOKMARKMANAGER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <cstdint>
#include "can_structs.h"

struct FrameBookmark
{
    qint64 timestampMicros = 0;
    int originalIndex = -1;
    uint32_t frameId = 0;
    int bus = -1;
    QString label;
    QString note;
};

struct BookmarkRecord
{
    uint64_t timestampUS = 0;
    int frameIndex = -1;
    int bus = 0;
    uint32_t frameId = 0;
    QString label;
};

class BookmarkManager : public QObject
{
    Q_OBJECT

public:
    explicit BookmarkManager(QObject *parent = nullptr);

    const QVector<FrameBookmark> &getBookmarks() const;
    int count() const;
    bool isEmpty() const;

    void addBookmark(const FrameBookmark &bookmark);
    void removeBookmark(int idx);
    void clear();

    QVector<BookmarkRecord> exportRecords() const;
    void importRecords(const QVector<BookmarkRecord> &records, const QVector<CANFrame> *frameCache);

    QStringList getAllLabels() const;
    bool labelExists(const QString &label) const;
    bool updateBookmarkLabel(int index, const QString &newLabel);

signals:
    void bookmarksChanged();

private:
    QVector<FrameBookmark> bookmarks;
};

#endif // BOOKMARKMANAGER_H