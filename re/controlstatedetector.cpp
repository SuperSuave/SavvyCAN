#include "controlstatedetector.h"

#include <algorithm>

ControlStateDetector::ControlStateDetector(QObject *parent)
    : QObject(parent)
{
}

quint64 ControlStateDetector::timestampOf(const CANFrame &f)
{
    const auto ts = f.timeStamp();
    return (quint64(ts.seconds()) * 1000000ULL) + (quint64(ts.microSeconds()) % 1000000ULL);
}

ControlStateKey ControlStateDetector::keyOf(const CANFrame &f)
{
    ControlStateKey key;
    key.bus = f.bus;
    key.frameId = f.frameId();
    key.extended = f.hasExtendedFrameFormat();
    return key;
}

QString ControlStateDetector::hexPayload(const QByteArray &payload)
{
    if (payload.isEmpty())
        return QString();

    QString out;
    out.reserve(payload.size() * 3);
    for (int i = 0; i < payload.size(); ++i) {
        if (i > 0)
            out += QLatin1Char(' ');
        out += QStringLiteral("%1").arg(quint8(payload.at(i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    return out;
}

ControlStateDetector::Grouped ControlStateDetector::groupFrames(const QVector<CANFrame> &frames,
                                                                int startIndex,
                                                                int endIndex) const
{
    Grouped grouped;

    if (frames.isEmpty())
        return grouped;

    startIndex = qMax(0, startIndex);
    endIndex = qMin(endIndex, frames.size() - 1);
    if (startIndex > endIndex)
        return grouped;

    for (int i = startIndex; i <= endIndex; ++i) {
        const CANFrame &frame = frames.at(i);

        Sample sample;
        sample.originalIndex = frame.originalIndex;
        sample.timestampUS = timestampOf(frame);
        sample.payload = frame.payload();

        grouped[keyOf(frame)].append(sample);
    }

    return grouped;
}

QVector<ControlTransition> ControlStateDetector::buildTransitions(const QVector<QByteArray> &states) const
{
    QVector<ControlTransition> out;
    for (int i = 1; i < states.size(); ++i) {
        bool found = false;
        for (ControlTransition &t : out) {
            if (t.fromState == states.at(i - 1) && t.toState == states.at(i)) {
                t.count++;
                found = true;
                break;
            }
        }

        if (!found) {
            ControlTransition t;
            t.fromState = states.at(i - 1);
            t.toState = states.at(i);
            t.count = 1;
            out.append(t);
        }
    }
    return out;
}

QVector<int> ControlStateDetector::changedByteIndices(const QVector<QByteArray> &states) const
{
    QVector<int> out;
    if (states.size() < 2)
        return out;

    const int maxLen = states.first().size();
    for (int i = 0; i < maxLen; ++i) {
        const char first = states.first().at(i);
        bool changed = false;
        for (int s = 1; s < states.size(); ++s) {
            if (states.at(s).size() <= i || states.at(s).at(i) != first) {
                changed = true;
                break;
            }
        }
        if (changed)
            out.append(i);
    }
    return out;
}

QVector<QString> ControlStateDetector::changedBitLabels(const QVector<QByteArray> &states) const
{
    QVector<QString> out;
    if (states.size() < 2)
        return out;

    const int maxLen = states.first().size();
    for (int byteIdx = 0; byteIdx < maxLen; ++byteIdx) {
        quint8 accum = 0;
        const quint8 base = quint8(states.first().at(byteIdx));
        for (int s = 1; s < states.size(); ++s) {
            if (states.at(s).size() <= byteIdx)
                continue;
            accum |= quint8(base ^ quint8(states.at(s).at(byteIdx)));
        }

        for (int bit = 0; bit < 8; ++bit) {
            if (accum & (1u << bit))
                out.append(QStringLiteral("B%1.%2").arg(byteIdx + 1).arg(bit));
        }
    }

    return out;
}

QVector<QString> ControlStateDetector::changedNibbleLabels(const QVector<QByteArray> &states) const
{
    QVector<QString> out;
    if (states.size() < 2)
        return out;

    const int maxLen = states.first().size();
    for (int byteIdx = 0; byteIdx < maxLen; ++byteIdx) {
        quint8 lowAccum = 0;
        quint8 highAccum = 0;
        const quint8 base = quint8(states.first().at(byteIdx));

        for (int s = 1; s < states.size(); ++s) {
            if (states.at(s).size() <= byteIdx)
                continue;

            const quint8 diff = quint8(base ^ quint8(states.at(s).at(byteIdx)));
            lowAccum |= (diff & 0x0F);
            highAccum |= (diff & 0xF0);
        }

        if (highAccum)
            out.append(QStringLiteral("B%1.hi").arg(byteIdx + 1));
        if (lowAccum)
            out.append(QStringLiteral("B%1.lo").arg(byteIdx + 1));
    }

    return out;
}

QString ControlStateDetector::classifyPattern(const QVector<QByteArray> &uniqueStates,
                                              const QVector<ControlTransition> &transitions,
                                              const QVector<int> &activeBytes) const
{
    Q_UNUSED(transitions);

    if (activeBytes.size() == 1) {
        if (uniqueStates.size() <= 2)
            return QStringLiteral("Binary Byte");
        if (uniqueStates.size() <= 5)
            return QStringLiteral("Encoded Nibble or Byte");
        return QStringLiteral("Single Byte Variable");
    }

    if (activeBytes.size() <= 2 && uniqueStates.size() <= 6)
        return QStringLiteral("Small Multi-byte Control");

    if (uniqueStates.size() <= 8)
        return QStringLiteral("Multi-state Control");

    return QStringLiteral("Complex or Continuous");
}

double ControlStateDetector::scoreCandidate(const QVector<QByteArray> &uniqueStates,
                                            int stateRunCount,
                                            const QVector<ControlTransition> &transitions,
                                            const QVector<int> &activeBytes,
                                            const QVector<QString> &activeBits,
                                            const QVector<QString> &activeNibbles) const
{
    const int uniq = uniqueStates.size();

    int totalTransitions = 0;
    int strongestTransitionCount = 0;
    for (const ControlTransition &t : transitions) {
        totalTransitions += t.count;
        if (t.count > strongestTransitionCount)
            strongestTransitionCount = t.count;
    }

    double stateCompactness = 0.0;
    if (uniq == 2) stateCompactness = 1.0;
    else if (uniq == 3) stateCompactness = 0.92;
    else if (uniq == 4) stateCompactness = 0.84;
    else if (uniq == 5) stateCompactness = 0.74;
    else if (uniq == 6) stateCompactness = 0.62;
    else if (uniq == 7) stateCompactness = 0.50;
    else if (uniq == 8) stateCompactness = 0.38;
    else stateCompactness = 0.20;

    double byteLocality = 0.0;
    if (activeBytes.size() == 1) byteLocality = 1.0;
    else if (activeBytes.size() == 2) byteLocality = 0.82;
    else if (activeBytes.size() == 3) byteLocality = 0.62;
    else if (activeBytes.size() == 4) byteLocality = 0.42;
    else if (activeBytes.isEmpty()) byteLocality = 0.0;
    else byteLocality = 0.20;

    double transitionDensity = 0.0;
    if (stateRunCount > 1)
        transitionDensity = qBound(0.0, double(totalTransitions) / double(stateRunCount - 1), 1.0);

    double repeatability = 0.0;
    if (totalTransitions > 0)
        repeatability = qBound(0.0, double(strongestTransitionCount) / double(totalTransitions), 1.0);

    double bitNibbleActivity = 0.0;
    if (!activeBytes.isEmpty()) {
        const double bitsPerByte = double(activeBits.size()) / double(activeBytes.size());
        const double nibblesPerByte = double(activeNibbles.size()) / double(activeBytes.size());

        double bitScore = 0.0;
        if (bitsPerByte <= 2.0) bitScore = 1.0;
        else if (bitsPerByte <= 4.0) bitScore = 0.75;
        else if (bitsPerByte <= 6.0) bitScore = 0.45;
        else bitScore = 0.20;

        double nibbleScore = 0.0;
        if (nibblesPerByte <= 1.0) nibbleScore = 1.0;
        else if (nibblesPerByte <= 1.5) nibbleScore = 0.75;
        else nibbleScore = 0.45;

        bitNibbleActivity = (bitScore * 0.5) + (nibbleScore * 0.5);
    }

    const double score =
            (stateCompactness * 0.32) +
            (byteLocality * 0.24) +
            (transitionDensity * 0.14) +
            (repeatability * 0.22) +
            (bitNibbleActivity * 0.08);

    return qBound(0.0, score, 1.0);
}

ControlCandidate ControlStateDetector::analyzeGroup(const ControlStateKey &key,
                                                    const QVector<Sample> &samples) const
{
    ControlCandidate c;
    c.key = key;
    if (samples.isEmpty())
        return c;

    QVector<Sample> sorted = samples;
    std::sort(sorted.begin(), sorted.end(), [](const Sample &a, const Sample &b) {
        if (a.timestampUS != b.timestampUS)
            return a.timestampUS < b.timestampUS;
        return a.originalIndex < b.originalIndex;
    });

    QVector<QByteArray> stateRuns;
    c.orderedStateRuns.clear();

    for (const Sample &s : sorted) {
        if (stateRuns.isEmpty() || stateRuns.last() != s.payload) {
            stateRuns.append(s.payload);
            c.exampleOriginalIndexes.append(s.originalIndex);

            ControlStateRun run;
            run.state = s.payload;
            run.repeatCount = 1;
            run.timestampUS = s.timestampUS;
            c.orderedStateRuns.append(run);
        } else {
            c.orderedStateRuns.last().repeatCount++;
        }
    }

    QVector<QByteArray> uniqueStates;
    uniqueStates.reserve(stateRuns.size());
    for (const QByteArray &payload : stateRuns) {
        if (!uniqueStates.contains(payload))
            uniqueStates.append(payload);
    }

    c.stableStates = uniqueStates;
    c.uniqueStateCount = uniqueStates.size();
    c.stateRunCount = stateRuns.size();

    c.orderedTransitions.clear();
    for (int i = 1; i < stateRuns.size(); ++i) {
        ControlTransition t;
        t.fromState = stateRuns.at(i - 1);
        t.toState = stateRuns.at(i);
        t.count = 1;
        c.orderedTransitions.append(t);
    }

    c.transitions = buildTransitions(stateRuns);
    c.distinctTransitionCount = c.transitions.size();

    c.totalTransitionCount = 0;
    int strongestTransitionCount = 0;
    for (const ControlTransition &t : c.transitions) {
        c.totalTransitionCount += t.count;
        if (t.count > strongestTransitionCount)
            strongestTransitionCount = t.count;
    }

    c.activeBytes = changedByteIndices(uniqueStates);
    c.activeBits = changedBitLabels(uniqueStates);
    c.activeNibbles = changedNibbleLabels(uniqueStates);
    c.patternType = classifyPattern(uniqueStates, c.transitions, c.activeBytes);
    c.confidence = scoreCandidate(uniqueStates,
                                  c.stateRunCount,
                                  c.transitions,
                                  c.activeBytes,
                                  c.activeBits,
                                  c.activeNibbles);

    if (c.uniqueStateCount == 2) c.stateCompactnessScore = 1.0;
    else if (c.uniqueStateCount == 3) c.stateCompactnessScore = 0.92;
    else if (c.uniqueStateCount == 4) c.stateCompactnessScore = 0.84;
    else if (c.uniqueStateCount == 5) c.stateCompactnessScore = 0.74;
    else if (c.uniqueStateCount == 6) c.stateCompactnessScore = 0.62;
    else if (c.uniqueStateCount == 7) c.stateCompactnessScore = 0.50;
    else if (c.uniqueStateCount == 8) c.stateCompactnessScore = 0.38;
    else c.stateCompactnessScore = 0.20;

    if (c.activeBytes.size() == 1) c.byteLocalityScore = 1.0;
    else if (c.activeBytes.size() == 2) c.byteLocalityScore = 0.82;
    else if (c.activeBytes.size() == 3) c.byteLocalityScore = 0.62;
    else if (c.activeBytes.size() == 4) c.byteLocalityScore = 0.42;
    else if (c.activeBytes.isEmpty()) c.byteLocalityScore = 0.0;
    else c.byteLocalityScore = 0.20;

    if (c.stateRunCount > 1)
        c.transitionDensityScore = qBound(0.0, double(c.totalTransitionCount) / double(c.stateRunCount - 1), 1.0);
    else
        c.transitionDensityScore = 0.0;

    if (c.totalTransitionCount > 0)
        c.repeatabilityScore = qBound(0.0, double(strongestTransitionCount) / double(c.totalTransitionCount), 1.0);
    else
        c.repeatabilityScore = 0.0;

    if (!c.activeBytes.isEmpty()) {
        const double bitsPerByte = double(c.activeBits.size()) / double(c.activeBytes.size());
        const double nibblesPerByte = double(c.activeNibbles.size()) / double(c.activeBytes.size());

        double bitScore = 0.0;
        if (bitsPerByte <= 2.0) bitScore = 1.0;
        else if (bitsPerByte <= 4.0) bitScore = 0.75;
        else if (bitsPerByte <= 6.0) bitScore = 0.45;
        else bitScore = 0.20;

        double nibbleScore = 0.0;
        if (nibblesPerByte <= 1.0) nibbleScore = 1.0;
        else if (nibblesPerByte <= 1.5) nibbleScore = 0.75;
        else nibbleScore = 0.45;

        c.bitNibbleActivityScore = (bitScore * 0.5) + (nibbleScore * 0.5);
    } else {
        c.bitNibbleActivityScore = 0.0;
    }

    QString localityText;
    if (c.activeBytes.size() == 1)
        localityText = QStringLiteral("single-byte control");
    else if (c.activeBytes.size() == 2)
        localityText = QStringLiteral("two-byte cluster");
    else if (c.activeBytes.size() <= 4)
        localityText = QStringLiteral("tight byte cluster");
    else
        localityText = QStringLiteral("wide byte spread");

    QString stateShapeText;
    if (c.uniqueStateCount == 2)
        stateShapeText = QStringLiteral("binary-like");
    else if (c.uniqueStateCount <= 5)
        stateShapeText = QStringLiteral("small state set");
    else if (c.uniqueStateCount <= 8)
        stateShapeText = QStringLiteral("moderate state set");
    else
        stateShapeText = QStringLiteral("large state set");

    QString transitionText;
    if (c.repeatabilityScore >= 0.75)
        transitionText = QStringLiteral("strong repeated transition pattern");
    else if (c.repeatabilityScore >= 0.45)
        transitionText = QStringLiteral("partially repeated transition pattern");
    else if (c.totalTransitionCount > 0)
        transitionText = QStringLiteral("diffuse transition pattern");
    else
        transitionText = QStringLiteral("no repeated transition pattern");

    QStringList reasonParts;
    reasonParts << QStringLiteral("%1 unique states").arg(c.uniqueStateCount);
    reasonParts << QStringLiteral("%1 total transitions").arg(c.totalTransitionCount);
    reasonParts << QStringLiteral("%1 active byte%2").arg(c.activeBytes.size()).arg(c.activeBytes.size() == 1 ? QString() : QStringLiteral("s"));
    reasonParts << stateShapeText;
    reasonParts << localityText;
    reasonParts << transitionText;

    c.reason = reasonParts.join(QStringLiteral(", "));
    return c;
}

QVector<ControlCandidate> ControlStateDetector::analyzeAll(const QVector<CANFrame> &frames) const
{
    Grouped grouped = groupFrames(frames, 0, frames.size() - 1);

    QVector<ControlCandidate> out;
    out.reserve(grouped.size());

    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        ControlCandidate candidate = analyzeGroup(it.key(), it.value());
        if (candidate.stableStates.size() > 1 && !candidate.transitions.isEmpty())
            out.append(candidate);
    }

    std::sort(out.begin(), out.end(), [](const ControlCandidate &a, const ControlCandidate &b) {
        if (!qFuzzyCompare(a.confidence, b.confidence))
            return a.confidence > b.confidence;

        if (a.activeBytes.size() != b.activeBytes.size())
            return a.activeBytes.size() < b.activeBytes.size();

        if (a.stableStates.size() != b.stableStates.size())
            return a.stableStates.size() < b.stableStates.size();

        if (a.key.bus != b.key.bus)
            return a.key.bus < b.key.bus;

        if (a.key.frameId != b.key.frameId)
            return a.key.frameId < b.key.frameId;

        return a.key.extended < b.key.extended;
    });

    return out;
}

QVector<ControlCandidate> ControlStateDetector::analyzeId(const QVector<CANFrame> &frames,
                                                          const ControlStateKey &key) const
{
    QVector<ControlCandidate> out;
    if (frames.isEmpty())
        return out;

    Grouped grouped = groupFrames(frames, 0, frames.size() - 1);
    auto it = grouped.constFind(key);
    if (it == grouped.constEnd())
        return out;

    ControlCandidate candidate = analyzeGroup(key, it.value());
    if (candidate.stableStates.size() > 1 && !candidate.transitions.isEmpty())
        out.append(candidate);

    return out;
}

QVector<ControlCandidate> ControlStateDetector::analyzeWindow(const QVector<CANFrame> &frames,
                                                              int startIndex,
                                                              int endIndex) const
{
    Grouped grouped = groupFrames(frames, startIndex, endIndex);

    QVector<ControlCandidate> out;
    out.reserve(grouped.size());

    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        ControlCandidate candidate = analyzeGroup(it.key(), it.value());
        if (candidate.stableStates.size() > 1 && !candidate.transitions.isEmpty())
            out.append(candidate);
    }

    std::sort(out.begin(), out.end(), [](const ControlCandidate &a, const ControlCandidate &b) {
        if (!qFuzzyCompare(a.confidence, b.confidence))
            return a.confidence > b.confidence;

        if (a.activeBytes.size() != b.activeBytes.size())
            return a.activeBytes.size() < b.activeBytes.size();

        if (a.stableStates.size() != b.stableStates.size())
            return a.stableStates.size() < b.stableStates.size();

        if (a.key.bus != b.key.bus)
            return a.key.bus < b.key.bus;

        if (a.key.frameId != b.key.frameId)
            return a.key.frameId < b.key.frameId;

        return a.key.extended < b.key.extended;
    });

    return out;
}

QVector<QPair<int, int>> ControlStateDetector::splitBurstWindows(const QVector<CANFrame> &frames,
                                                                 const ControlStateKey &key,
                                                                 quint64 gapThresholdUS) const
{
    QVector<QPair<int, int>> windows;
    if (frames.isEmpty())
        return windows;

    int burstStart = -1;
    int previousMatchIndex = -1;
    quint64 previousTimestampUS = 0;

    for (int i = 0; i < frames.size(); ++i) {
        const CANFrame &frame = frames.at(i);
        if (!(keyOf(frame) == key))
            continue;

        const quint64 ts = timestampOf(frame);

        if (burstStart < 0) {
            burstStart = i;
            previousMatchIndex = i;
            previousTimestampUS = ts;
            continue;
        }

        const quint64 gap = ts - previousTimestampUS;
        if (gap > gapThresholdUS) {
            windows.append(qMakePair(burstStart, previousMatchIndex));
            burstStart = i;
        }

        previousMatchIndex = i;
        previousTimestampUS = ts;
    }

    if (burstStart >= 0 && previousMatchIndex >= burstStart) {
        windows.append(qMakePair(burstStart, previousMatchIndex));
    }

    return windows;
}