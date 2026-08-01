#include "controlanalysisdialog.h"
#include "controlcandidatemodel.h"

#include <QTableView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QMenu>
#include <QAction>
#include <QItemSelectionModel>
#include <QPlainTextEdit>
#include <QLabel>
#include <QSplitter>

ControlAnalysisDialog::ControlAnalysisDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Control State Analysis"));
    resize(1100, 760);

    auto *layout = new QVBoxLayout(this);

    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_view = new QTableView(this);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setAlternatingRowColors(true);
    m_view->setSortingEnabled(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->verticalHeader()->setVisible(false);
    m_view->setWordWrap(false);
    m_view->setTextElideMode(Qt::ElideRight);
    splitter->addWidget(m_view);

    auto *detailsPane = new QWidget(this);
    auto *detailsLayout = new QVBoxLayout(detailsPane);
    detailsLayout->setContentsMargins(0, 0, 0, 0);

    auto *detailsLabel = new QLabel(tr("Candidate Details"), detailsPane);
    m_detailsText = new QPlainTextEdit(detailsPane);
    m_detailsText->setReadOnly(true);
    m_detailsText->setLineWrapMode(QPlainTextEdit::NoWrap);

    detailsLayout->addWidget(detailsLabel);
    detailsLayout->addWidget(m_detailsText);
    splitter->addWidget(detailsPane);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    layout->addWidget(splitter);

    auto *buttonRow = new QHBoxLayout();
    auto *jumpBtn = new QPushButton(tr("Jump To"), this);
    auto *bookmarkBtn = new QPushButton(tr("Bookmark"), this);
    auto *closeBtn = new QPushButton(tr("Close"), this);

    buttonRow->addWidget(jumpBtn);
    buttonRow->addWidget(bookmarkBtn);
    buttonRow->addStretch();
    buttonRow->addWidget(closeBtn);
    layout->addLayout(buttonRow);

    connect(jumpBtn, &QPushButton::clicked, this, &ControlAnalysisDialog::jumpToSelectedCandidate);
    connect(bookmarkBtn, &QPushButton::clicked, this, &ControlAnalysisDialog::bookmarkSelectedCandidate);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_view, &QTableView::doubleClicked, this, &ControlAnalysisDialog::handleDoubleClick);
    connect(m_view, &QWidget::customContextMenuRequested, this, &ControlAnalysisDialog::handleContextMenu);
}

void ControlAnalysisDialog::setModel(ControlCandidateModel *model)
{
    if (m_view->selectionModel()) {
        disconnect(m_view->selectionModel(), &QItemSelectionModel::currentRowChanged,
                   this, &ControlAnalysisDialog::updateDetailsForCurrentRow);
    }

    m_model = model;
    m_view->setModel(model);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(ControlCandidateModel::ConfidenceCol, Qt::DescendingOrder);
    m_view->resizeColumnsToContents();
    m_view->setColumnWidth(ControlCandidateModel::ReasonCol, 420);

    if (m_view->selectionModel()) {
        connect(m_view->selectionModel(), &QItemSelectionModel::currentRowChanged,
                this, &ControlAnalysisDialog::updateDetailsForCurrentRow);
    }

    if (m_model && m_model->rowCount() > 0) {
        m_view->selectRow(0);
        updateDetailsTextForRow(0);
    } else if (m_detailsText) {
        m_detailsText->clear();
    }
}

void ControlAnalysisDialog::refreshCandidates()
{
    if (!m_model)
        return;

    const int previousRow = currentCandidateIndex();

    m_view->sortByColumn(ControlCandidateModel::ConfidenceCol, Qt::DescendingOrder);
    m_view->resizeColumnsToContents();
    m_view->setColumnWidth(ControlCandidateModel::ReasonCol, 420);

    if (m_model->rowCount() > 0) {
        const int row = (previousRow >= 0 && previousRow < m_model->rowCount()) ? previousRow : 0;
        m_view->selectRow(row);
        updateDetailsTextForRow(row);
    } else if (m_detailsText) {
        m_detailsText->clear();
    }
}

void ControlAnalysisDialog::handleDoubleClick(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    emit jumpToCandidateRequested(index.row());
}

void ControlAnalysisDialog::handleContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *jumpAct = menu.addAction(tr("Jump to candidate"));
    QAction *bookmarkAct = menu.addAction(tr("Bookmark candidate"));

    connect(jumpAct, &QAction::triggered, this, &ControlAnalysisDialog::jumpToSelectedCandidate);
    connect(bookmarkAct, &QAction::triggered, this, &ControlAnalysisDialog::bookmarkSelectedCandidate);

    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

int ControlAnalysisDialog::currentCandidateIndex() const
{
    if (!m_view || !m_view->selectionModel())
        return -1;

    const QModelIndex idx = m_view->selectionModel()->currentIndex();
    return idx.isValid() ? idx.row() : -1;
}

void ControlAnalysisDialog::jumpToSelectedCandidate()
{
    const int row = currentCandidateIndex();
    if (row >= 0)
        emit jumpToCandidateRequested(row);
}

void ControlAnalysisDialog::bookmarkSelectedCandidate()
{
    const int row = currentCandidateIndex();
    if (row >= 0)
        emit bookmarkCandidateRequested(row);
}

void ControlAnalysisDialog::updateDetailsForCurrentRow(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous)

    if (!current.isValid()) {
        if (m_detailsText)
            m_detailsText->clear();
        return;
    }

    updateDetailsTextForRow(current.row());
}

void ControlAnalysisDialog::updateDetailsTextForRow(int row)
{
    if (!m_model || !m_detailsText || row < 0 || row >= m_model->rowCount()) {
        if (m_detailsText)
            m_detailsText->clear();
        return;
    }

    const ControlCandidate &candidate = m_model->candidateAt(row);
    m_detailsText->setPlainText(m_model->formatExpandedSequenceDetails(candidate));
}