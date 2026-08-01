#ifndef CONTROLSTATEDETECTOR_H
#define CONTROLSTATEDETECTOR_H

#include <QObject>
#include <QVector>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QPair>
#include "can_structs.h"

struct ControlStateKey
{
    int bus = -1;
    uint32_t frameId = 0;
    bool extended = false;

    bool operator==(const ControlStateKey &o) const
    {
        return bus == o.bus && frameId == o.frameId && extended == o.extended;
    }
};

inline uint qHash(const ControlStateKey &k, uint seed = 0)
{
    seed = qHash(k.bus, seed);
    seed = qHash(k.frameId, seed);
    seed = qHash(k.extended, seed);
    return seed;
}

struct ControlTransition
{
    QByteArray fromState;
    QByteArray toState;
    int count = 0;
};

struct ControlStateRun
{
    QByteArray state;
    int repeatCount = 0;
    quint64 timestampUS = 0;
};

struct ControlCandidate
{
    ControlStateKey key;
    QString patternType;
    double confidence = 0.0;

    double stateCompactnessScore = 0.0;
    double byteLocalityScore = 0.0;
    double transitionDensityScore = 0.0;
    double repeatabilityScore = 0.0;
    double bitNibbleActivityScore = 0.0;

    int uniqueStateCount = 0;
    int stateRunCount = 0;
    int distinctTransitionCount = 0;
    int totalTransitionCount = 0;

    QVector<int> activeBytes;
    QVector<QString> activeBits;
    QVector<QString> activeNibbles;
    QVector<QByteArray> stableStates;
    QVector<ControlTransition> transitions;
    QVector<ControlTransition> orderedTransitions;
    QVector<ControlStateRun> orderedStateRuns;
    QVector<int> exampleOriginalIndexes;
    QString reason;
};

class ControlStateDetector : public QObject
{
    Q_OBJECT
public:
    explicit ControlStateDetector(QObject *parent = nullptr);

    QVector<ControlCandidate> analyzeAll(const QVector<CANFrame> &frames) const;
    QVector<ControlCandidate> analyzeId(const QVector<CANFrame> &frames, const ControlStateKey &key) const;
    QVector<ControlCandidate> analyzeWindow(const QVector<CANFrame> &frames, int startIndex, int endIndex) const;

    QVector<QPair<int, int>> splitBurstWindows(const QVector<CANFrame> &frames,
                                               const ControlStateKey &key,
                                               quint64 gapThresholdUS = 30000) const;

private:
    struct Sample
    {
        int originalIndex = -1;
        quint64 timestampUS = 0;
        QByteArray payload;
    };

    using Grouped = QHash<ControlStateKey, QVector<Sample>>;

    static quint64 timestampOf(const CANFrame &f);
    static ControlStateKey keyOf(const CANFrame &f);

    Grouped groupFrames(const QVector<CANFrame> &frames, int startIndex, int endIndex) const;
    ControlCandidate analyzeGroup(const ControlStateKey &key, const QVector<Sample> &samples) const;

    QVector<ControlTransition> buildTransitions(const QVector<QByteArray> &states) const;
    QVector<int> changedByteIndices(const QVector<QByteArray> &states) const;
    QVector<QString> changedBitLabels(const QVector<QByteArray> &states) const;
    QVector<QString> changedNibbleLabels(const QVector<QByteArray> &states) const;

    QString classifyPattern(const QVector<QByteArray> &uniqueStates,
                            const QVector<ControlTransition> &transitions,
                            const QVector<int> &activeBytes) const;

    double scoreCandidate(const QVector<QByteArray> &uniqueStates,
                          int stateRunCount,
                          const QVector<ControlTransition> &transitions,
                          const QVector<int> &activeBytes,
                          const QVector<QString> &activeBits,
                          const QVector<QString> &activeNibbles) const;

    static QString hexPayload(const QByteArray &payload);

    static QString formatStateValue(const QByteArray &state);
    static QString formatTransitionGroup(const QVector<ControlTransition> &group);
    static QVector<QVector<ControlTransition>> splitTransitionGroups(const QVector<ControlTransition> &transitions);
    static QString formatExpandedSequenceDetails(const ControlCandidate &c);
};

#endif // CONTROLSTATEDETECTOR_H