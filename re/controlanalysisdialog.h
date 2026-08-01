#ifndef CONTROLANALYSISDIALOG_H
#define CONTROLANALYSISDIALOG_H

#include <QDialog>
#include <QModelIndex>

class QPlainTextEdit;
class QTableView;
class ControlCandidateModel;

class ControlAnalysisDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ControlAnalysisDialog(QWidget *parent = nullptr);

    void setModel(ControlCandidateModel *model);
    void refreshCandidates();

signals:
    void jumpToCandidateRequested(int candidateIndex);
    void bookmarkCandidateRequested(int candidateIndex);

private slots:
    void handleDoubleClick(const QModelIndex &index);
    void handleContextMenu(const QPoint &pos);
    void jumpToSelectedCandidate();
    void bookmarkSelectedCandidate();
    void updateDetailsForCurrentRow(const QModelIndex &current, const QModelIndex &previous);

private:
    int currentCandidateIndex() const;
    void updateDetailsTextForRow(int row);

    QTableView *m_view = nullptr;
    QPlainTextEdit *m_detailsText = nullptr;
    ControlCandidateModel *m_model = nullptr;
};

#endif // CONTROLANALYSISDIALOG_H