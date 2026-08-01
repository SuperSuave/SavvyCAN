#ifndef BOOKMARKEVENTANALYZER_H
#define BOOKMARKEVENTANALYZER_H

#include <QObject>
#include <QVector>
#include <QHash>
#include <QString>
#include <QByteArray>
#include "can_structs.h"

class BookmarkEventAnalyzer : public QObject
{
    Q_OBJECT
public:
    explicit BookmarkEventAnalyzer(QObject *parent = nullptr);

    struct FrameKey
    {
        uint32_t frameId = 0;
        int bus = 0;
        bool extended = false;

        bool operator==(const FrameKey &other) const
        {
            return frameId == other.frameId &&
                   bus == other.bus &&
                   extended == other.extended;
        }
    };

    struct ByteBaselineInfo
    {
        int byteIndex = -1;
        int sampleCount = 0;
        quint8 dominantValue = 0;
        int dominantCount = 0;
        double dominance = 0.0;
        bool dominantIsIdleSentinel = false;
        bool changedNearEvent = false;
        quint8 eventBeforeValue = 0;
        quint8 eventAfterValue = 0;
        bool hasEventTransition = false;
    };

    struct FlipCandidate
    {
        FrameKey key;
        int byteIndex = -1;
        quint8 beforeValue = 0;
        quint8 afterValue = 0;

        int beforeCount = 0;
        int afterCount = 0;
        int eventFlipCount = 0;

        double supportScore = 0.0;
        double localStability = 0.0;
        double localNoise = 0.0;
        double idleNoise = 0.0;
        double idleStability = 0.0;
        double score = 0.0;

        int nearestOriginalIndex = -1;
        int nearestDistance = 0;

        QVector<ByteBaselineInfo> baselineBytes;
        QString reason;
    };

    struct CrossIdCandidate
    {
        FrameKey key;

        int beforeCount = 0;
        int afterCount = 0;
        int totalEventCount = 0;

        int payloadChangeCount = 0;
        double appearanceShift = 0.0;
        double payloadVolatility = 0.0;
        double idleNoise = 0.0;
        double idleStability = 0.0;
        double score = 0.0;

        bool appearedOnlyAfter = false;
        bool disappearedAfter = false;

        int nearestOriginalIndex = -1;
        int nearestDistance = 0;

        QVector<int> changedBytes;
        QVector<ByteBaselineInfo> baselineBytes;
        QString reason;
    };

    struct BookmarkAnalysisResult
    {
        CANFrame anchorFrame;
        int originalIndex = -1;
        int sameIdRadius = 0;
        int crossIdWindowBefore = 0;
        int crossIdWindowAfter = 0;

        QVector<FlipCandidate> sameIdCandidates;
        QVector<CrossIdCandidate> crossIdCandidates;
    };

    BookmarkAnalysisResult analyze(const QVector<CANFrame> &frames,
                                   int originalIndex,
                                   int sameIdRadius,
                                   int crossIdWindowBefore,
                                   int crossIdWindowAfter) const;

    QString describeSameIdReason(const FlipCandidate &c) const;
    QString describeCrossIdReason(const CrossIdCandidate &c) const;
    QString formatBaselineSummary(const QVector<ByteBaselineInfo> &bytes) const;

private:
    struct EventByteStats
    {
        int byteIndex = -1;

        int beforeCount = 0;
        int afterCount = 0;
        int beforeTransitions = 0;
        int afterTransitions = 0;
        int eventFlipCount = 0;

        quint8 beforeValue = 0;
        quint8 afterValue = 0;

        bool hasBeforeValue = false;
        bool hasAfterValue = false;
        bool hasTransition = false;

        quint8 lastBeforeSeen = 0;
        quint8 lastAfterSeen = 0;
        bool hasLastBeforeSeen = false;
        bool hasLastAfterSeen = false;

        int nearestOriginalIndex = -1;
        int nearestDistance = 0;
    };

    struct EventFrameStats
    {
        FrameKey key;
        int anchorOriginalIndex = -1;

        int beforeCount = 0;
        int afterCount = 0;
        int matchedFramesBefore = 0;
        int matchedFramesAfter = 0;

        int nearestOriginalIndex = -1;
        int nearestDistance = 0;

        EventByteStats bytes[64];
    };

    struct ByteIdleStats
    {
        int samples = 0;
        int changes = 0;
    };

    struct CrossIdEventStats
    {
        FrameKey key;
        int anchorOriginalIndex = -1;
        int beforeCount = 0;
        int afterCount = 0;
        int beforePayloadTransitions = 0;
        int afterPayloadTransitions = 0;
        int payloadChangeCount = 0;
        int nearestOriginalIndex = -1;
        int nearestDistance = std::numeric_limits<int>::max();

        bool hasLastBefore = false;
        bool hasLastAfter = false;
        QByteArray lastBeforePayload;
        QByteArray lastAfterPayload;

        QVector<int> changedBytes;
    };

    struct FrameIdleStats
    {
        int samples = 0;
        int changes = 0;
        int maxDlcSeen = 0;
        ByteIdleStats bytes[64];
    };

    struct SameIdScoreFeatures
    {
        double eventDelta = 0.0;
        double supportScore = 0.0;
        double localStability = 0.0;
        double localNoise = 0.0;
        double idleStability = 0.0;
        double idleNoise = 0.0;
        double windowConfidence = 0.0;
    };

    struct CrossIdScoreFeatures
    {
        double appearanceShift = 0.0;
        double payloadVolatility = 0.0;
        double idleStability = 0.0;
        double idleNoise = 0.0;
        double exclusiveAfter = 0.0;
        double exclusiveBefore = 0.0;
    };

    // Optional future enhancement: live or offline-learned idle baseline.
    // Not required, but leaving the state hook here makes later extension easy.
    QHash<FrameKey, FrameIdleStats> idleBaseline;
    bool idleBaselineAvailable = false;

    static double clamp01(double value);
    static double safeRatio(double num, double denom);
    static QString formatByteValue(quint8 value);

    FrameKey makeFrameKey(const CANFrame &frame) const;

    void accumulateEventFrame(EventFrameStats &stats,
                              const CANFrame &frame,
                              bool isBeforeSide,
                              int anchorOriginalIndex) const;

    void accumulateCrossIdEventFrame(CrossIdEventStats &stats,
                                     const CANFrame &frame,
                                     bool isBeforeSide) const;

    QVector<FlipCandidate> analyzeSameIdAroundBookmark(const QVector<CANFrame> &frames,
                                                       int originalIndex,
                                                       int sameIdRadius) const;

    QVector<CrossIdCandidate> analyzeCrossIdAroundBookmark(const QVector<CANFrame> &frames,
                                                           int originalIndex,
                                                           int windowBefore,
                                                           int windowAfter) const;

    QVector<FlipCandidate> rankFlipCandidates(const QHash<FrameKey, EventFrameStats> &eventStats,
                                              int sameIdRadius) const;

    SameIdScoreFeatures buildSameIdScoreFeatures(const EventByteStats &eb,
                                                const ByteIdleStats *idleByteStats,
                                                int sameIdRadius) const;

    double computeSameIdSupportScore(const EventByteStats &eb,
                                     int sameIdRadius) const;

    double computeSameIdLocalStability(const EventByteStats &eb) const;
    double computeSameIdIdleStability(const ByteIdleStats *idleByteStats) const;
    double scoreSameIdCandidate(const SameIdScoreFeatures &f) const;

    CrossIdScoreFeatures buildCrossIdScoreFeatures(const CrossIdEventStats &stats,
                                                   const FrameIdleStats *idleStats,
                                                   int windowBefore,
                                                   int windowAfter) const;

    double scoreCrossIdCandidate(const CrossIdScoreFeatures &f) const;

    double computeCrossIdAppearanceShift(const CrossIdEventStats &stats,
                                         int windowBefore,
                                         int windowAfter) const;

    double computeCrossIdPayloadVolatility(const CrossIdEventStats &stats) const;
    double computeCrossIdIdleStability(const FrameIdleStats *idleStats) const;

    ByteBaselineInfo analyzePreEventByteBaseline(const QVector<CANFrame> &frames,
                                                 const FrameKey &key,
                                                 int anchorOriginalIndex,
                                                 int byteIndex,
                                                 int lookbackLimit,
                                                 quint8 eventBeforeValue,
                                                 quint8 eventAfterValue,
                                                 bool hasEventTransition) const;

    QVector<ByteBaselineInfo> analyzePreEventBaselinesForSameId(
            const QVector<CANFrame> &frames,
            const FrameKey &key,
            int anchorOriginalIndex,
            const QVector<EventByteStats> &eventBytes,
            int lookbackLimit) const;

    QVector<ByteBaselineInfo> analyzePreEventBaselinesForCrossId(
            const QVector<CANFrame> &frames,
            const FrameKey &key,
            int anchorOriginalIndex,
            const QVector<int> &changedBytes,
            int lookbackLimit) const;
};


uint qHash(const BookmarkEventAnalyzer::FrameKey &key, uint seed = 0) noexcept;

#endif // BOOKMARKEVENTANALYZER_H