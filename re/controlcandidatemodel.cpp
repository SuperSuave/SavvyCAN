#include "controlcandidatemodel.h"

#include <algorithm>
#include <QStringList>
#include <QDebug>

ControlCandidateModel::ControlCandidateModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void ControlCandidateModel::setCandidates(const QVector<ControlCandidate> &candidates)
{
    beginResetModel();
    m_candidates = candidates;
    endResetModel();
}

const ControlCandidate &ControlCandidateModel::candidateAt(int row) const
{
    return m_candidates.at(row);
}

int ControlCandidateModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_candidates.size();
}

int ControlCandidateModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString ControlCandidateModel::joinStringVector(const QVector<QString> &values, const QString &separator)
{
    QStringList list;
    list.reserve(values.size());

    for (const QString &value : values)
        list << value;

    return list.join(separator);
}

QString ControlCandidateModel::formatStateValue(const QByteArray &state)
{
    if (state.isEmpty())
        return QString();

    QStringList parts;
    parts.reserve(state.size());

    for (unsigned char byte : state)
        parts << QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper();

    return parts.join(QStringLiteral(" "));
}

bool ControlCandidateModel::isIdleLikeState(const QByteArray &state)
{
    if (state.isEmpty())
        return false;

    int ffCount = 0;
    int zeroCount = 0;

    for (char ch : state) {
        const quint8 b = static_cast<quint8>(ch);
        if (b == 0xFF)
            ++ffCount;
        else if (b == 0x00)
            ++zeroCount;
    }

    return (ffCount >= state.size() - 1) || (zeroCount >= state.size() - 1);
}

QString ControlCandidateModel::formatExpandedSequenceDetails(const ControlCandidate &c) const
{
    QStringList lines;

    if (c.orderedStateRuns.isEmpty()) {
        if (!c.stableStates.isEmpty()) {
            lines << QObject::tr("Typical Pattern:");
            const QVector<QByteArray> representativeStates = selectRepresentativeStates(c, 3);
            for (const QByteArray &state : representativeStates)
                lines << QStringLiteral("  %1").arg(formatStateValue(state));
        }
        return lines.join(QStringLiteral("\n"));
    }

    if (looksLikeBoundedCompositeField(c)) {
        return QStringLiteral("Potential bounded composite field on %1 active bytes.")
            .arg(c.activeBytes.size());
    }

    if (looksLikeContinuousMessage(c)) {
        const ContinuousByteAnalysis analysis = findBestContinuousControlByte(c);
        const QString continuousDetails = formatContinuousMessageDetails(c, analysis);
        if (!continuousDetails.isEmpty())
            return continuousDetails;
    }

    QVector<QByteArray> representativeStates = selectRepresentativeStates(c, 3);
    representativeStates = orderTopStatesForDisplay(representativeStates, c);

    if (representativeStates.isEmpty())
        return lines.join(QStringLiteral("\n"));

    lines << QObject::tr("Typical Pattern:");
    for (const QByteArray &state : representativeStates)
        lines << QStringLiteral("  %1").arg(formatStateValue(state));

    return lines.join(QStringLiteral("\n"));
}

QVector<QByteArray> ControlCandidateModel::buildContinuousRepresentativeStates(
    const ControlCandidate &c,
    const ContinuousByteAnalysis &analysis,
    int maxStates) const
{
    QVector<QByteArray> result;

    if (analysis.byteIndex < 0 || c.orderedStateRuns.isEmpty() || maxStates <= 0)
        return result;

    const int byteIndex = analysis.byteIndex;

    QHash<int, QByteArray> representativeStateForValue;
    QHash<int, int> weightedCounts;
    QVector<int> firstSeenValues;

    for (const ControlStateRun &run : c.orderedStateRuns) {
        if (byteIndex >= run.state.size())
            continue;

        const int value = static_cast<unsigned char>(run.state.at(byteIndex));
        weightedCounts[value] += qMax(1, run.repeatCount);

        if (!representativeStateForValue.contains(value)) {
            representativeStateForValue.insert(value, run.state);
            firstSeenValues.append(value);
        }
    }

    QVector<int> orderedValues = firstSeenValues;

    std::sort(orderedValues.begin(), orderedValues.end(),
              [&](int a, int b) {
        const bool aIdle = isIdleActiveByteValue(a);
        const bool bIdle = isIdleActiveByteValue(b);

        if (aIdle != bIdle)
            return !aIdle; // active values first, idle last

        if (!aIdle && a != b)
            return a > b; // higher active value first, e.g. 0x40 before smaller active values

        const int countA = weightedCounts.value(a);
        const int countB = weightedCounts.value(b);
        if (countA != countB)
            return countA > countB;

        return firstSeenValues.indexOf(a) < firstSeenValues.indexOf(b);
    });

    for (int value : orderedValues) {
        if (!representativeStateForValue.contains(value))
            continue;

        result.append(representativeStateForValue.value(value));
        if (result.size() >= maxStates)
            break;
    }

    return result;
}

QString ControlCandidateModel::formatContinuousMessageDetails(
    const ControlCandidate &c,
    const ContinuousByteAnalysis &analysis) const
{
    if (analysis.byteIndex < 0)
        return QString();

    if (analysis.score < -500.0) {
        if (analysis.uniqueValueCount > 50 &&
            analysis.transitionCount > 100 &&
            (analysis.sequentialValueRatio > 0.8 ||
             analysis.arithmeticProgressionRatio > 0.8)) {
            return QStringLiteral(
                "Appears to be a counter/checksum field:\n"
                "  D%1 cycles through many values (likely not a human control)."
            ).arg(analysis.byteIndex);
        }

        return QString();
    }

    const QVector<QByteArray> representativeStates =
        buildContinuousRepresentativeStates(c, analysis, 3);

    if (representativeStates.isEmpty())
        return QString();

    QHash<int, int> weightedCounts;
    for (const ControlStateRun &run : c.orderedStateRuns) {
        if (analysis.byteIndex < run.state.size()) {
            const int value = static_cast<unsigned char>(run.state.at(analysis.byteIndex));
            weightedCounts[value] += qMax(1, run.repeatCount);
        }
    }

    QVector<int> orderedValues;
    for (const QByteArray &state : representativeStates) {
        if (analysis.byteIndex < state.size()) {
            const int value = static_cast<unsigned char>(state.at(analysis.byteIndex));
            if (!orderedValues.contains(value))
                orderedValues.append(value);
        }
    }

    std::sort(orderedValues.begin(), orderedValues.end(),
              [&](int a, int b) {
        const bool aIdle = isIdleActiveByteValue(a);
        const bool bIdle = isIdleActiveByteValue(b);

        if (aIdle != bIdle)
            return !aIdle;

        if (!aIdle && a != b)
            return a > b;

        return weightedCounts.value(a) > weightedCounts.value(b);
    });

    QSet<int> varyingBytes;
    const int byteCount = representativeStates.first().size();
    for (int i = 0; i < byteCount; ++i) {
        if (i == analysis.byteIndex)
            continue;

        int firstValue = -1;
        bool differs = false;

        for (const QByteArray &state : representativeStates) {
            if (i >= state.size())
                continue;

            const int value = static_cast<unsigned char>(state.at(i));
            if (firstValue < 0)
                firstValue = value;
            else if (value != firstValue) {
                differs = true;
                break;
            }
        }

        if (differs)
            varyingBytes.insert(i);
    }

    QStringList lines;
    lines << QStringLiteral("Control Byte: D%1").arg(analysis.byteIndex);

    lines << QStringLiteral("States:");

    const bool hasIdleState =
        std::any_of(orderedValues.constBegin(), orderedValues.constEnd(),
                    [&](int value) { return isIdleActiveByteValue(value); });

    const bool useBinaryLabels = (orderedValues.size() == 2 && hasIdleState);

    int stateNumber = orderedValues.size();
    for (int value : orderedValues) {
        const QString valueText =
            QStringLiteral("[%1]").arg(QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper());

        QString label;
        if (isIdleActiveByteValue(value)) {
            label = QStringLiteral("idle");
        } else if (useBinaryLabels) {
            label = QStringLiteral("active");
        } else {
            label = QStringLiteral("state %1").arg(stateNumber);
            --stateNumber;
        }

        lines << QStringLiteral("  %1 -> %2").arg(valueText, label);
    }

    lines << QStringLiteral("Representative Frames:");
    for (const QByteArray &state : representativeStates)
        lines << QStringLiteral("  %1").arg(formatMaskedFrameForDisplay(state,
                                                                        analysis.byteIndex,
                                                                        varyingBytes));

    return lines.join(QStringLiteral("\n"));
}

QString ControlCandidateModel::formatMaskedFrameForDisplay(const QByteArray &state,
                                                           int highlightByte,
                                                           const QSet<int> &maskBytes) const
{
    QStringList parts;
    parts.reserve(state.size());

    for (int i = 0; i < state.size(); ++i) {
        QString part;

        if (maskBytes.contains(i) && i != highlightByte) {
            part = QStringLiteral("XX");
        } else {
            const int value = static_cast<unsigned char>(state.at(i));
            part = QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
        }

        if (i == highlightByte)
            part = QStringLiteral("[%1]").arg(part);

        parts << part;
    }

    return parts.join(QStringLiteral(" "));
}

QVector<QByteArray> ControlCandidateModel::selectRepresentativeStates(const ControlCandidate &c,
                                                                      int maxStates) const
{
    QVector<QByteArray> result;

    if (!c.orderedStateRuns.isEmpty()) {
        QHash<QByteArray, int> counts;
        QHash<QByteArray, int> firstSeenIndex;

        for (int i = 0; i < c.orderedStateRuns.size(); ++i) {
            const ControlStateRun &run = c.orderedStateRuns.at(i);
            counts[run.state] += qMax(1, run.repeatCount);

            if (!firstSeenIndex.contains(run.state))
                firstSeenIndex.insert(run.state, i);
        }

        QVector<QByteArray> rankedStates;
        rankedStates.reserve(counts.size());

        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
            rankedStates.append(it.key());

        std::sort(rankedStates.begin(), rankedStates.end(),
                  [&](const QByteArray &a, const QByteArray &b) {
            const int countA = counts.value(a);
            const int countB = counts.value(b);
            if (countA != countB)
                return countA > countB;

            return firstSeenIndex.value(a) < firstSeenIndex.value(b);
        });

        const int takeCount = qMin(maxStates, rankedStates.size());
        for (int i = 0; i < takeCount; ++i)
            result.append(rankedStates.at(i));

        return result;
    }

    const int takeCount = qMin(maxStates, c.stableStates.size());
    for (int i = 0; i < takeCount; ++i)
        result.append(c.stableStates.at(i));

    return result;
}

QVector<QByteArray> ControlCandidateModel::orderTopStatesForDisplay(
    const QVector<QByteArray> &topStates,
    const ControlCandidate &c) const
{
    if (topStates.isEmpty())
        return {};

    const int activeByte = c.activeBytes.isEmpty() ? -1 : c.activeBytes.first();

    auto activeByteValue = [&](const QByteArray &state) -> int {
        if (activeByte < 0 || activeByte >= state.size())
            return -1;
        return static_cast<unsigned char>(state.at(activeByte));
    };

    QVector<QByteArray> activeStates;
    QVector<QByteArray> idleStates;

    for (const QByteArray &state : topStates) {
        const int value = activeByteValue(state);
        if (isIdleActiveByteValue(value))
            idleStates.append(state);
        else
            activeStates.append(state);
    }

    std::sort(activeStates.begin(), activeStates.end(),
              [&](const QByteArray &a, const QByteArray &b) {
        const int va = activeByteValue(a);
        const int vb = activeByteValue(b);

        if (va != vb)
            return va > vb;

        return topStates.indexOf(a) < topStates.indexOf(b);
    });

    QVector<QByteArray> result = activeStates;
    for (const QByteArray &state : idleStates)
        result.append(state);

    return result;
}

ControlCandidateModel::ContinuousByteAnalysis
ControlCandidateModel::analyzeContinuousByte(const QVector<ControlStateRun> &runs,
                                             int byteIndex) const
{

    ContinuousByteAnalysis analysis;
    analysis.byteIndex = byteIndex;

    if (runs.isEmpty())
        return analysis;

    QHash<int, int> valueCounts;
    QVector<int> observedValues;
    observedValues.reserve(runs.size());

    int transitionCount = 0;
    int repeatedRunCount = 0;
    int totalFrames = 0;
    int monotonicSteps = 0;
    bool hasPreviousValue = false;
    int previousValue = -1;

    for (const ControlStateRun &run : runs) {
        if (byteIndex < 0 || byteIndex >= run.state.size())
            continue;

        const int value = static_cast<unsigned char>(run.state.at(byteIndex));
        const int repeatCount = qMax(1, run.repeatCount);

        valueCounts[value] += repeatCount;
        totalFrames += repeatCount;

        if (repeatCount > 1)
            ++repeatedRunCount;

        observedValues.append(value);

        if (hasPreviousValue) {
            if (value != previousValue)
                ++transitionCount;

            const int delta = (value - previousValue + 256) % 256;
            if (delta == 1 || delta == 0x10 || delta == 0x0F)
                ++monotonicSteps;
        }

        previousValue = value;
        hasPreviousValue = true;
    }

    analysis.uniqueValueCount = valueCounts.size();
    analysis.transitionCount = transitionCount;
    analysis.repeatedRunCount = repeatedRunCount;
    analysis.averageRunLength = runs.isEmpty()
                                ? 0.0
                                : static_cast<double>(totalFrames) / runs.size();
    analysis.monotonicStepRatio = runs.size() > 1
                                  ? static_cast<double>(monotonicSteps) / (runs.size() - 1)
                                  : 0.0;

    if (analysis.uniqueValueCount < 2 || analysis.transitionCount == 0) {
        analysis.score = -1e9;
        analysis.orderedValues.clear();
        analysis.sequentialValueRatio = 0.0;
        analysis.arithmeticProgressionRatio = 0.0;
        return analysis;
    }

    QVector<int> uniqueValues;
    uniqueValues.reserve(valueCounts.size());
    for (auto it = valueCounts.constBegin(); it != valueCounts.constEnd(); ++it)
        uniqueValues.append(it.key());

    QVector<int> sortedValues = uniqueValues;
    std::sort(sortedValues.begin(), sortedValues.end());

    int sequentialPairs = 0;
    for (int i = 1; i < sortedValues.size(); ++i) {
        const int delta = sortedValues.at(i) - sortedValues.at(i - 1);
        if (delta == 1)
            ++sequentialPairs;
    }

    analysis.sequentialValueRatio = sortedValues.size() > 1
                                    ? static_cast<double>(sequentialPairs) / (sortedValues.size() - 1)
                                    : 0.0;

    QVector<int> deltas;
    deltas.reserve(qMax(0, sortedValues.size() - 1));

    for (int i = 1; i < sortedValues.size(); ++i)
        deltas.append(sortedValues.at(i) - sortedValues.at(i - 1));

    int dominantDeltaCount = 0;
    if (!deltas.isEmpty()) {
        QHash<int, int> deltaCounts;
        for (int delta : deltas)
            deltaCounts[delta]++;

        for (auto it = deltaCounts.constBegin(); it != deltaCounts.constEnd(); ++it)
            dominantDeltaCount = qMax(dominantDeltaCount, it.value());
    }

    analysis.arithmeticProgressionRatio = deltas.isEmpty()
                                          ? 0.0
                                          : static_cast<double>(dominantDeltaCount) / deltas.size();

    std::sort(uniqueValues.begin(), uniqueValues.end(),
              [&](int a, int b) {
        const bool aIdle = isIdleActiveByteValue(a);
        const bool bIdle = isIdleActiveByteValue(b);

        if (aIdle != bIdle)
            return !aIdle;

        if (!aIdle && a != b)
            return a > b;

        const int countA = valueCounts.value(a);
        const int countB = valueCounts.value(b);
        if (countA != countB)
            return countA > countB;

        return a > b;
    });

    analysis.orderedValues = uniqueValues;

    double score = 0.0;

    if (analysis.uniqueValueCount == 2)
        score += 12.0;
    else if (analysis.uniqueValueCount == 3)
        score += 9.0;
    else if (analysis.uniqueValueCount == 4)
        score += 5.0;
    else
        score -= analysis.uniqueValueCount * 2.5;

    if (analysis.transitionCount >= 2 && analysis.transitionCount <= 64)
        score += 8.0;
    else
        score -= qAbs(analysis.transitionCount - 16) * 0.5;

    score += analysis.repeatedRunCount * 1.5;
    score += qMin(analysis.averageRunLength, 6.0);

    if (analysis.monotonicStepRatio > 0.35)
        score -= 20.0 * analysis.monotonicStepRatio;

    if (analysis.sequentialValueRatio > 0.6)
        score -= 12.0 * analysis.sequentialValueRatio;

    if (analysis.arithmeticProgressionRatio > 0.6)
        score -= 14.0 * analysis.arithmeticProgressionRatio;

    bool hasIdleLikeValue = false;
    for (int value : uniqueValues) {
        if (isIdleActiveByteValue(value)) {
            hasIdleLikeValue = true;
            break;
        }
    }

    if (hasIdleLikeValue)
        score += 2.0;

    analysis.score = score;
    
    return analysis;
}

ControlCandidateModel::ContinuousByteAnalysis
ControlCandidateModel::findBestContinuousControlByte(const ControlCandidate &c) const
{
    ContinuousByteAnalysis best;

    if (c.orderedStateRuns.isEmpty())
        return best;

    const int byteCount = c.orderedStateRuns.first().state.size();

    qDebug() << "Continuous byte scoring:";
    for (int byteIndex = 0; byteIndex < byteCount; ++byteIndex) {
        const ContinuousByteAnalysis analysis =
            analyzeContinuousByte(c.orderedStateRuns, byteIndex);

        qDebug() << " byte" << byteIndex
                << "unique=" << analysis.uniqueValueCount
                << "transitions=" << analysis.transitionCount
                << "repeatedRuns=" << analysis.repeatedRunCount
                << "avgRun=" << analysis.averageRunLength
                << "monoRatio=" << analysis.monotonicStepRatio
                << "seqRatio=" << analysis.sequentialValueRatio
                << "apRatio=" << analysis.arithmeticProgressionRatio
                << "score=" << analysis.score;

        if (analysis.score > best.score)
            best = analysis;
    }

    for (int byteIndex = 0; byteIndex < byteCount; ++byteIndex) {
        const ContinuousByteAnalysis analysis =
            analyzeContinuousByte(c.orderedStateRuns, byteIndex);

        if (analysis.score > best.score)
            best = analysis;
    }

    qDebug() << "Continuous byte scoring:";
    for (int byteIndex = 0; byteIndex < byteCount; ++byteIndex) {
        const ContinuousByteAnalysis analysis =
            analyzeContinuousByte(c.orderedStateRuns, byteIndex);

        qDebug() << " byte" << byteIndex
                << "unique=" << analysis.uniqueValueCount
                << "transitions=" << analysis.transitionCount
                << "repeatedRuns=" << analysis.repeatedRunCount
                << "avgRun=" << analysis.averageRunLength
                << "monoRatio=" << analysis.monotonicStepRatio
                << "score=" << analysis.score;

        if (analysis.score > best.score)
            best = analysis;
    }
    
    return best;
}

bool ControlCandidateModel::isIdleActiveByteValue(int value) const
{
    return value == 0xFF || value == 0x00;
}

bool ControlCandidateModel::looksLikeBoundedCompositeField(const ControlCandidate &c) const
{
    if (c.orderedStateRuns.size() < 20)
        return false;

    if (c.activeBytes.size() < 3 || c.activeBytes.size() > 4)
        return false;

    QSet<QByteArray> uniqueFields;
    int transitions = 0;
    int repeatedRuns = 0;
    QByteArray previousField;
    bool hasPrevious = false;

    for (const ControlStateRun &run : c.orderedStateRuns) {
        QByteArray field;
        for (int byteIndex : c.activeBytes) {
            if (byteIndex >= 0 && byteIndex < run.state.size())
                field.append(run.state.at(byteIndex));
        }

        if (field.isEmpty())
            continue;

        uniqueFields.insert(field);

        if (qMax(1, run.repeatCount) > 1)
            ++repeatedRuns;

        if (hasPrevious && field != previousField)
            ++transitions;

        previousField = field;
        hasPrevious = true;
    }

    const int uniqueCount = uniqueFields.size();

    if (uniqueCount < 32)
        return false;

    if (uniqueCount > 128)
        return false;

    if (transitions < 40)
        return false;

    if (repeatedRuns < 10)
        return false;

    return true;
}

bool ControlCandidateModel::looksLikeContinuousMessage(const ControlCandidate &c) const
{
    if (c.orderedStateRuns.size() < 6)
        return false;

    if (c.activeBytes.size() > 2)
        return true;

    int totalRepeats = 0;
    for (const ControlStateRun &run : c.orderedStateRuns)
        totalRepeats += qMax(1, run.repeatCount);

    return totalRepeats >= 12;
}

QVariant ControlCandidateModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_candidates.size())
        return {};

    const auto &c = m_candidates.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case BusCol:
            return c.key.bus;
        case IdCol:
            return QStringLiteral("0x%1").arg(c.key.frameId, 0, 16).toUpper();
        case ExtCol:
            return c.key.extended ? tr("Y") : tr("N");
        case PatternCol:
            return c.patternType;
        case ConfidenceCol:
            return QStringLiteral("%1%").arg(c.confidence * 100.0, 0, 'f', 1);
        case StatesCol:
            return c.uniqueStateCount;
        case TransitionsCol:
            return c.distinctTransitionCount;
        case ActiveBytesCol:
        {
            QStringList bytes;
            for (int b : c.activeBytes)
                bytes << QStringLiteral("D%1").arg(b + 1);
            return bytes.join(QStringLiteral(", "));
        }
        case ReasonCol:
            return c.reason;
        default:
            return {};
        }
    }

    if (role == Qt::UserRole) {
        switch (index.column()) {
        case BusCol:
            return c.key.bus;
        case IdCol:
            return qulonglong(c.key.frameId);
        case ExtCol:
            return c.key.extended ? 1 : 0;
        case PatternCol:
            return c.patternType;
        case ConfidenceCol:
            return c.confidence;
        case StatesCol:
            return c.uniqueStateCount;
        case TransitionsCol:
            return c.distinctTransitionCount;
        case ActiveBytesCol:
            return c.activeBytes.size();
        case ReasonCol:
            return c.reason;
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        QStringList lines;
        lines << QStringLiteral("Pattern: %1").arg(c.patternType);
        lines << QStringLiteral("Confidence: %1%").arg(c.confidence * 100.0, 0, 'f', 1);
        lines << QStringLiteral("Unique states: %1").arg(c.uniqueStateCount);
        lines << QStringLiteral("State runs: %1").arg(c.stateRunCount);
        lines << QStringLiteral("Distinct transition pairs: %1").arg(c.distinctTransitionCount);
        lines << QStringLiteral("Total observed transitions: %1").arg(c.totalTransitionCount);
        lines << QStringLiteral("State compactness: %1%").arg(c.stateCompactnessScore * 100.0, 0, 'f', 1);
        lines << QStringLiteral("Byte locality: %1%").arg(c.byteLocalityScore * 100.0, 0, 'f', 1);
        lines << QStringLiteral("Transition density: %1%").arg(c.transitionDensityScore * 100.0, 0, 'f', 1);
        lines << QStringLiteral("Repeatability: %1%").arg(c.repeatabilityScore * 100.0, 0, 'f', 1);
        lines << QStringLiteral("Bit/nibble activity: %1%").arg(c.bitNibbleActivityScore * 100.0, 0, 'f', 1);

        if (!c.activeBytes.isEmpty()) {
            QStringList bytes;
            for (int b : c.activeBytes)
                bytes << QStringLiteral("D%1").arg(b + 1);
            lines << QStringLiteral("Active bytes: %1").arg(bytes.join(QStringLiteral(", ")));
        }

        if (!c.activeBits.isEmpty())
            lines << QStringLiteral("Active bits: %1").arg(joinStringVector(c.activeBits, QStringLiteral(", ")));

        if (!c.activeNibbles.isEmpty())
            lines << QStringLiteral("Active nibbles: %1").arg(joinStringVector(c.activeNibbles, QStringLiteral(", ")));

        if (!c.reason.isEmpty())
            lines << QStringLiteral("Reason: %1").arg(c.reason);

        return lines.join(QStringLiteral("\n"));
    }

    return {};
}

QVariant ControlCandidateModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case BusCol:
        return tr("Bus");
    case IdCol:
        return tr("ID");
    case ExtCol:
        return tr("Ext");
    case PatternCol:
        return tr("Pattern");
    case ConfidenceCol:
        return tr("Conf");
    case StatesCol:
        return tr("States");
    case TransitionsCol:
        return tr("Transitions");
    case ActiveBytesCol:
        return tr("Active Bytes");
    case ReasonCol:
        return tr("Reason");
    default:
        return {};
    }
}

void ControlCandidateModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();

    std::sort(m_candidates.begin(), m_candidates.end(),
              [column](const ControlCandidate &lhs, const ControlCandidate &rhs) {
        switch (column) {
        case BusCol:
            return lhs.key.bus < rhs.key.bus;
        case IdCol:
            return lhs.key.frameId < rhs.key.frameId;
        case ExtCol:
            return lhs.key.extended < rhs.key.extended;
        case PatternCol:
            return lhs.patternType < rhs.patternType;
        case ConfidenceCol:
            if (!qFuzzyCompare(lhs.confidence, rhs.confidence))
                return lhs.confidence < rhs.confidence;
            if (lhs.activeBytes.size() != rhs.activeBytes.size())
                return lhs.activeBytes.size() < rhs.activeBytes.size();
            return lhs.uniqueStateCount < rhs.uniqueStateCount;
        case StatesCol:
            if (lhs.uniqueStateCount != rhs.uniqueStateCount)
                return lhs.uniqueStateCount < rhs.uniqueStateCount;
            return lhs.stateRunCount < rhs.stateRunCount;
        case TransitionsCol:
            if (lhs.totalTransitionCount != rhs.totalTransitionCount)
                return lhs.totalTransitionCount < rhs.totalTransitionCount;
            return lhs.distinctTransitionCount < rhs.distinctTransitionCount;
        case ActiveBytesCol:
            return lhs.activeBytes.size() < rhs.activeBytes.size();
        case ReasonCol:
            return lhs.reason < rhs.reason;
        default:
            return lhs.key.frameId < rhs.key.frameId;
        }
    });

    if (order == Qt::DescendingOrder)
        std::reverse(m_candidates.begin(), m_candidates.end());

    endResetModel();
}