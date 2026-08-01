#include "bookmarkeventanalyzer.h"

#include <algorithm>
#include <QMap>
#include <QStringList>
#include <QtGlobal>

BookmarkEventAnalyzer::BookmarkEventAnalyzer(QObject *parent)
    : QObject(parent)
{
}

QString BookmarkEventAnalyzer::formatByteValue(quint8 value)
{
    return QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

BookmarkEventAnalyzer::ByteBaselineInfo BookmarkEventAnalyzer::analyzePreEventByteBaseline(
        const QVector<CANFrame> &frames,
        const FrameKey &key,
        int anchorOriginalIndex,
        int byteIndex,
        int lookbackLimit,
        quint8 eventBeforeValue,
        quint8 eventAfterValue,
        bool hasEventTransition) const
{
    ByteBaselineInfo out;
    out.byteIndex = byteIndex;
    out.eventBeforeValue = eventBeforeValue;
    out.eventAfterValue = eventAfterValue;
    out.hasEventTransition = hasEventTransition;

    if (byteIndex < 0) return out;
    if (anchorOriginalIndex <= 0 || anchorOriginalIndex > frames.size()) return out;

    QMap<int, int> counts;
    const int start = qMax(0, anchorOriginalIndex - qMax(1, lookbackLimit));

    for (int i = start; i < anchorOriginalIndex; ++i)
    {
        const CANFrame &frame = frames.at(i);
        if (frame.frameType() != QCanBusFrame::DataFrame) continue;
        if (frame.frameId() != key.frameId) continue;
        if (frame.bus != key.bus) continue;
        if (frame.hasExtendedFrameFormat() != key.extended) continue;

        const QByteArray payload = frame.payload();
        if (byteIndex >= payload.size()) continue;

        const quint8 value = static_cast<quint8>(payload.at(byteIndex));
        counts[int(value)]++;
        out.sampleCount++;
    }

    if (out.sampleCount <= 0) return out;

    int bestValue = 0;
    int bestCount = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
    {
        if (it.value() > bestCount)
        {
            bestValue = it.key();
            bestCount = it.value();
        }
    }

    out.dominantValue = static_cast<quint8>(bestValue);
    out.dominantCount = bestCount;
    out.dominance = safeRatio(static_cast<double>(bestCount),
                              static_cast<double>(out.sampleCount));
    out.dominantIsIdleSentinel = (out.dominantValue == 0x00 || out.dominantValue == 0xFF);
    out.changedNearEvent = hasEventTransition &&
                           (out.eventBeforeValue != out.eventAfterValue);

    return out;
}

QVector<BookmarkEventAnalyzer::ByteBaselineInfo>
BookmarkEventAnalyzer::analyzePreEventBaselinesForSameId(
        const QVector<CANFrame> &frames,
        const FrameKey &key,
        int anchorOriginalIndex,
        const QVector<EventByteStats> &eventBytes,
        int lookbackLimit) const
{
    QVector<ByteBaselineInfo> out;

    for (const EventByteStats &eb : eventBytes)
    {
        if (!eb.hasTransition) continue;

        out.append(analyzePreEventByteBaseline(frames,
                                               key,
                                               anchorOriginalIndex,
                                               eb.byteIndex,
                                               lookbackLimit,
                                               eb.beforeValue,
                                               eb.afterValue,
                                               eb.hasTransition));
    }

    std::sort(out.begin(), out.end(), [](const ByteBaselineInfo &a, const ByteBaselineInfo &b) {
        if (a.dominantIsIdleSentinel != b.dominantIsIdleSentinel)
            return a.dominantIsIdleSentinel > b.dominantIsIdleSentinel;
        if (!qFuzzyCompare(a.dominance, b.dominance))
            return a.dominance > b.dominance;
        return a.byteIndex < b.byteIndex;
    });

    return out;
}

QVector<BookmarkEventAnalyzer::ByteBaselineInfo>
BookmarkEventAnalyzer::analyzePreEventBaselinesForCrossId(
        const QVector<CANFrame> &frames,
        const FrameKey &key,
        int anchorOriginalIndex,
        const QVector<int> &changedBytes,
        int lookbackLimit) const
{
    QVector<ByteBaselineInfo> out;

    for (int byteIndex : changedBytes)
    {
        out.append(analyzePreEventByteBaseline(frames,
                                               key,
                                               anchorOriginalIndex,
                                               byteIndex,
                                               lookbackLimit,
                                               0,
                                               0,
                                               false));
    }

    std::sort(out.begin(), out.end(), [](const ByteBaselineInfo &a, const ByteBaselineInfo &b) {
        if (a.dominantIsIdleSentinel != b.dominantIsIdleSentinel)
            return a.dominantIsIdleSentinel > b.dominantIsIdleSentinel;
        if (!qFuzzyCompare(a.dominance, b.dominance))
            return a.dominance > b.dominance;
        return a.byteIndex < b.byteIndex;
    });

    return out;
}

QString BookmarkEventAnalyzer::formatBaselineSummary(const QVector<ByteBaselineInfo> &bytes) const
{
    if (bytes.isEmpty()) return QString();

    QStringList lines;
    lines << QStringLiteral("Pre-event baseline:");

    for (const ByteBaselineInfo &b : bytes)
    {
        QString line = QStringLiteral("  Byte %1: %2 dominant (%3/%4, %5%)")
                .arg(b.byteIndex)
                .arg(formatByteValue(b.dominantValue))
                .arg(b.dominantCount)
                .arg(b.sampleCount)
                .arg(QString::number(b.dominance * 100.0, 'f', 1));

        if (b.dominantIsIdleSentinel)
            line += QStringLiteral("  [idle sentinel]");

        if (b.hasEventTransition)
        {
            line += QStringLiteral("  event %1 -> %2")
                    .arg(formatByteValue(b.eventBeforeValue))
                    .arg(formatByteValue(b.eventAfterValue));
        }

        lines << line;
    }

    return lines.join(QLatin1Char('\n'));
}