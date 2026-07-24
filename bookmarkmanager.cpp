#include "bookmarkmanager.h"
#include <limits>
#include <algorithm>

BookmarkManager::BookmarkManager(QObject *parent)
    : QObject(parent)
{
}

const QVector<FrameBookmark> &BookmarkManager::getBookmarks() const
{
    return bookmarks;
}

int BookmarkManager::count() const
{
    return bookmarks.count();
}

bool BookmarkManager::isEmpty() const
{
    return bookmarks.isEmpty();
}

void BookmarkManager::addBookmark(const FrameBookmark &bookmark)
{
    bookmarks.append(bookmark);
    emit bookmarksChanged();
}

void BookmarkManager::removeBookmark(int idx)
{
    if (idx < 0 || idx >= bookmarks.count()) return;
    bookmarks.removeAt(idx);
    emit bookmarksChanged();
}

void BookmarkManager::clear()
{
    if (bookmarks.isEmpty()) return;
    bookmarks.clear();
    emit bookmarksChanged();
}

QStringList BookmarkManager::getAllLabels() const
{
    QStringList labels;

    for (const FrameBookmark &bookmark : bookmarks) {
        const QString trimmed = bookmark.label.trimmed();
        if (!trimmed.isEmpty() && !labels.contains(trimmed, Qt::CaseInsensitive)) {
            labels.append(trimmed);
        }
    }

    std::sort(labels.begin(), labels.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });

    return labels;
}

bool BookmarkManager::labelExists(const QString &label) const
{
    const QString wanted = label.trimmed();
    if (wanted.isEmpty()) return false;

    for (const FrameBookmark &bookmark : bookmarks) {
        if (bookmark.label.trimmed().compare(wanted, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return false;
}

bool BookmarkManager::updateBookmarkLabel(int index, const QString &newLabel)
{
    if (index < 0 || index >= bookmarks.size())
        return false;

    QString finalLabel = newLabel.trimmed();
    if (finalLabel.isEmpty())
        finalLabel = "Bookmark";

    bookmarks[index].label = finalLabel;
    emit bookmarksChanged();
    return true;
}

QVector<BookmarkRecord> BookmarkManager::exportRecords() const
{
    QVector<BookmarkRecord> records;
    records.reserve(bookmarks.size());

    for (const FrameBookmark &bm : bookmarks)
    {
        BookmarkRecord rec;
        rec.timestampUS = static_cast<uint64_t>(bm.timestampMicros);
        rec.frameIndex = bm.originalIndex;
        rec.bus = bm.bus;
        rec.frameId = bm.frameId;
        rec.label = bm.label;
        records.append(rec);
    }

    return records;
}

void BookmarkManager::importRecords(const QVector<BookmarkRecord> &records,
                                    const QVector<CANFrame> *frameCache)
{
    bookmarks.clear();

    for (const BookmarkRecord &rec : records)
    {
        FrameBookmark bm;
        bm.timestampMicros = static_cast<qint64>(rec.timestampUS);
        bm.originalIndex = rec.frameIndex;
        bm.frameId = rec.frameId;
        bm.bus = rec.bus;
        bm.label = rec.label;
        bm.note.clear();

        int resolvedIndex = -1;

        if (frameCache && rec.frameIndex >= 0 && rec.frameIndex < frameCache->size())
        {
            const CANFrame &frame = frameCache->at(rec.frameIndex);
            if (frame.bus == rec.bus && frame.frameId() == rec.frameId)
                resolvedIndex = rec.frameIndex;
        }
        else if (frameCache && !frameCache->isEmpty())
        {
            quint64 bestDelta = std::numeric_limits<quint64>::max();

            for (int i = 0; i < frameCache->size(); ++i)
            {
                const CANFrame &frame = frameCache->at(i);

                if (frame.bus != rec.bus) continue;
                if (frame.frameId() != rec.frameId) continue;

                quint64 ts = static_cast<quint64>(frame.timeStamp().seconds()) * 1000000ULL +
                             static_cast<quint64>(frame.timeStamp().microSeconds());

                quint64 delta = (ts > rec.timestampUS) ? (ts - rec.timestampUS) : (rec.timestampUS - ts);

                if (delta < bestDelta)
                {
                    bestDelta = delta;
                    resolvedIndex = i;
                }
            }

            if (resolvedIndex < 0)
            {
                for (int i = 0; i < frameCache->size(); ++i)
                {
                    const CANFrame &frame = frameCache->at(i);
                    quint64 ts = static_cast<quint64>(frame.timeStamp().seconds()) * 1000000ULL +
                                 static_cast<quint64>(frame.timeStamp().microSeconds());

                    quint64 delta = (ts > rec.timestampUS) ? (ts - rec.timestampUS) : (rec.timestampUS - ts);

                    if (delta < bestDelta)
                    {
                        bestDelta = delta;
                        resolvedIndex = i;
                    }
                }
            }
        }

        if (resolvedIndex >= 0 && frameCache)
        {
            const CANFrame &frame = frameCache->at(resolvedIndex);
            bm.originalIndex = resolvedIndex;
            bm.frameId = frame.frameId();
            bm.bus = frame.bus;
            bm.timestampMicros =
                static_cast<qint64>(frame.timeStamp().seconds()) * 1000000LL +
                static_cast<qint64>(frame.timeStamp().microSeconds());
        }

        bookmarks.append(bm);
    }

    emit bookmarksChanged();
}