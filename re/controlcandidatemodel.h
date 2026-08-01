#ifndef CONTROLCANDIDATEMODEL_H
#define CONTROLCANDIDATEMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QByteArray>
#include "controlstatedetector.h"

class ControlCandidateModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        BusCol = 0,
        IdCol,
        ExtCol,
        PatternCol,
        ConfidenceCol,
        StatesCol,
        TransitionsCol,
        ActiveBytesCol,
        ReasonCol,
        ColumnCount
    };

    explicit ControlCandidateModel(QObject *parent = nullptr);

    void setCandidates(const QVector<ControlCandidate> &candidates);
    const ControlCandidate &candidateAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    QString formatExpandedSequenceDetails(const ControlCandidate &c) const;

private:
    static QString formatStateValue(const QByteArray &payload);
    static QString joinStringVector(const QVector<QString> &values, const QString &separator);
    static bool isIdleLikeState(const QByteArray &state);

    QVector<ControlCandidate> m_candidates;


    struct ContinuousByteAnalysis {
        int byteIndex = -1;
        int uniqueValueCount = 0;
        int transitionCount = 0;
        int repeatedRunCount = 0;
        double averageRunLength = 0.0;
        double monotonicStepRatio = 0.0;
        double score = -1e9;
        double sequentialValueRatio = 0.0;
        double arithmeticProgressionRatio = 0.0;
        QVector<int> orderedValues;
    };

    bool looksLikeBoundedCompositeField(const ControlCandidate &c) const;

    QVector<QByteArray> selectRepresentativeStates(const ControlCandidate &c, int maxStates = 3) const;
    QVector<QByteArray> orderTopStatesForDisplay(const QVector<QByteArray> &topStates,
                                                 const ControlCandidate &c) const;

    ContinuousByteAnalysis analyzeContinuousByte(const QVector<ControlStateRun> &runs,
                                                 int byteIndex) const;
    ContinuousByteAnalysis findBestContinuousControlByte(const ControlCandidate &c) const;

    bool isIdleActiveByteValue(int value) const;
    bool looksLikeContinuousMessage(const ControlCandidate &c) const;

    QVector<QByteArray> buildContinuousRepresentativeStates(const ControlCandidate &c,
                                                            const ContinuousByteAnalysis &analysis,
                                                            int maxStates = 3) const;


    QString formatContinuousMessageDetails(const ControlCandidate &c,
                                           const ContinuousByteAnalysis &analysis) const;

    QString formatMaskedFrameForDisplay(const QByteArray &state,
                                        int highlightByte,
                                        const QSet<int> &maskBytes) const;

};

#endif // CONTROLCANDIDATEMODEL_H