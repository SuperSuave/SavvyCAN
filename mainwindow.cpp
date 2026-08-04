#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "bookmarkmanager.h"
#include "bookmarkmanagerdialog.h"
#include "re/controlanalysisdialog.h"
#include "re/controlcandidatemodel.h"
#include "re/controlstatedetector.h"
#include "re/bookmarkeventanalyzer.h"
#include "can_structs.h"
#include <limits>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QTableWidgetItem>
#include <QClipboard>
#include <QShortcut>
#include <QDateTime>
#include <QFileDialog>
#include <QDockWidget>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QItemSelectionModel>
#include <QSpinBox>
#include <QtSerialPort/QSerialPortInfo>
#include "connections/canconmanager.h"
#include "connections/connectionwindow.h"
#include "helpwindow.h"
#include "utility.h"
#include "filterutility.h"
#include "framebytedatadelegate.h"


#include <QSortFilterProxyModel>
#include <limits>
/*
Some notes on things I'd like to put into the program but haven't put on github (yet)

Allow scripts to read/write signals from DBC files
allow scripts to load DBC files in support of the script - maybe the graphing system too.
*/

static QString normalizedBookmarkLabel(const QString &tag)
{
    QString finalLabel = tag.trimmed();
    if (finalLabel.isEmpty()) finalLabel = "Bookmark";
    return finalLabel;
}

class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    NumericTableWidgetItem(double value, const QString &text)
        : QTableWidgetItem(text), numericValue(value)
    {
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        const NumericTableWidgetItem *otherItem =
                dynamic_cast<const NumericTableWidgetItem *>(&other);
        if (otherItem)
            return numericValue < otherItem->numericValue;

        return QTableWidgetItem::operator<(other);
    }

private:
    double numericValue = 0.0;
};

QString MainWindow::loadedFileName = "";
MainWindow *MainWindow::selfRef = nullptr;

MainWindow *MainWindow::getReference()
{
    return selfRef;
}

void MainWindow::onDbcNeedsRefresh(int idx)
{
    DBCFile *file = dbcHandler->getFileByIdx(idx);
    if (file) {
        statusBar()->showMessage(tr("DBC Update Available (Unsaved Changes in %1)").arg(file->getFilename()), 10000);
    }
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    QShortcut *bookmarkShortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    bookmarkShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    bookmarkShortcut->setAutoRepeat(false);
    connect(ui->actionAddBookmark, &QAction::triggered, this, &MainWindow::triggerQuickBookmark);
    connect(bookmarkShortcut, &QShortcut::activated, this, &MainWindow::triggerTimedDiscoveryBookmark);

    autoBookmarkTimer = new QTimer(this);
    autoBookmarkTimer->setSingleShot(true);
    connect(autoBookmarkTimer, &QTimer::timeout, this, &MainWindow::autoBookmarkTimeoutExpired);

    controlStateDetector = new ControlStateDetector(this);
    controlCandidateModel = new ControlCandidateModel(this);
    controlAnalysisDialog = new ControlAnalysisDialog(this);
    controlAnalysisDialog->setModel(controlCandidateModel);

    connect(controlAnalysisDialog, &ControlAnalysisDialog::jumpToCandidateRequested,
            this, &MainWindow::jumpToControlCandidate);
    connect(controlAnalysisDialog, &ControlAnalysisDialog::bookmarkCandidateRequested,
            this, &MainWindow::bookmarkControlCandidate);

    setupEmbeddedAnalysisViews();

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qRegisterMetaTypeStreamOperators<QVector<QString>>();
    qRegisterMetaTypeStreamOperators<QVector<int>>();
#endif

    useHex = true;
    useColorsByCanId = false;
    selfRef = this;

    this->setWindowTitle("SavvyLens " + QByteArray(VERSION));

    model = new CANFrameModel(this);

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    ui->canFramesView->setModel(proxyModel);

    ui->canFramesView->setItemDelegateForColumn(int(Column::Data),
                                                new FrameByteDataDelegate(ui->canFramesView));

    connect(ui->canFramesView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::updateInspectDock);

    connect(ui->canFramesView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &, const QModelIndex &) {
                refreshAnalysisTabsForCurrentSelection();
            });

    bookmarkManager = new BookmarkManager(this);
    bookmarkDialog = new BookmarkManagerDialog(bookmarkManager, this);

    QAction *analyzeBookmarkAction = new QAction(tr("Analyze Around Bookmark"), this);

    connect(analyzeBookmarkAction, &QAction::triggered, this, &MainWindow::analyzeCurrentBookmarkOrSelection);

    QAction *analyzeControlLoadedAction = new QAction(tr("Analyze Control States (Loaded Log)"), this);
    QAction *analyzeControlSelectedAction = new QAction(tr("Analyze Control States (Selected Frame)"), this);
    bookmarkEventAnalyzer = new BookmarkEventAnalyzer(this);      
    settingsDialog = new MainSettingsDialog(); //instantiate the settings dialog so it can initialize settings if this is the first run or the config file was deleted.
    settingsDialog->updateSettings(); //write out all the settings. If this is the first run it'll write defaults out.

    copilotStatusLabel = new QLabel("AI: Disconnected");
    copilotStatusLabel->setStyleSheet("QLabel { color : gray; }");
    ui->statusBar->addPermanentWidget(copilotStatusLabel);

    readSettings();

    QHeaderView *verticalHeader = ui->canFramesView->verticalHeader();
    verticalHeader->setSectionResizeMode(QHeaderView::Fixed);
    QSettings settings;
    int fontSize = settings.value("Main/FontSize", 9).toUInt();
    QFont sysFont;
    if(settings.value("Main/FontFixedWidth", false).toBool())
        sysFont = QFontDatabase::systemFont(QFontDatabase::FixedFont); //get default fixed width font
    else
        sysFont = QFont();  //get default font
    sysFont.setPointSize(fontSize);
    verticalHeader->setDefaultSectionSize(sysFont.pixelSize());
    verticalHeader->setFont(QFont());
    ui->canFramesView->setFont(sysFont);

    QHeaderView *HorzHdr = ui->canFramesView->horizontalHeader();
    HorzHdr->setFont(QFont());
    HorzHdr->setStretchLastSection(false); //causes the data column to automatically fill the tableview
    connect(HorzHdr, SIGNAL(sectionClicked(int)), this, SLOT(headerClicked(int)));

    lastGraphingWindow = nullptr;
    frameInfoWindow = nullptr;
    playbackWindow = nullptr;
    flowViewWindow = nullptr;
    frameSenderWindow = nullptr;
    dbcMainEditor = nullptr;
    comparatorWindow = nullptr;
    udsFirmwareUploaderWindow = nullptr;
    discreteStateWindow = nullptr;
    connectionWindow = nullptr;
    scriptingWindow = nullptr;
    rangeWindow = nullptr;
    dbcFileWindow = nullptr;
    fuzzingWindow = nullptr;
    udsScanWindow = nullptr;
    motorctrlConfigWindow = nullptr;
    isoWindow = nullptr;
    snifferWindow = nullptr;
    bisectWindow = nullptr;
    signalViewerWindow = nullptr;
    temporalGraphWindow = nullptr;
    dbcComparatorWindow = nullptr;
    canBridgeWindow = nullptr;
    dbcHandler = DBCHandler::getReference();
    connect(dbcHandler, &DBCHandler::fileNeedsRefresh, this, &MainWindow::onDbcNeedsRefresh);
    bDirty = false;
    inhibitFilterUpdate = false;
    rxFrames = 0;
    framesPerSec = 0;
    continuousLogging = false;
    continuousLogFlushCounter = 0;

    //handlers for all menu entries
    connect(ui->actionSetup, SIGNAL(triggered(bool)), SLOT(showConnectionSettingsWindow()));
    connect(ui->actionOpen_Log_File, &QAction::triggered, this, &MainWindow::handleLoadFile);
    connect(ui->actionGraph_Dta, &QAction::triggered, this, &MainWindow::showGraphingWindow);
    connect(ui->actionFrame_Data_Analysis, &QAction::triggered, this, &MainWindow::showFrameDataAnalysis);
    connect(ui->actionSave_Log_File, &QAction::triggered, this, &MainWindow::handleSaveFile);
    connect(ui->actionSave_Filtered_Log_File, &QAction::triggered, this, &MainWindow::handleSaveFilteredFile);
    connect(ui->actionLoad_Filter_Definition, &QAction::triggered, this, &MainWindow::handleLoadFilters);
    connect(ui->actionSave_Filter_Definition, &QAction::triggered, this, &MainWindow::handleSaveFilters);
    connect(ui->action_Playback, &QAction::triggered, this, &MainWindow::showPlaybackWindow);
    connect(ui->actionFlow_View, &QAction::triggered, this, &MainWindow::showFlowViewWindow);
    connect(ui->action_Custom, &QAction::triggered, this, &MainWindow::showFrameSenderWindow);
    connect(ui->actionExit_Application, &QAction::triggered, this, &MainWindow::exitApp);
    connect(ui->actionFuzzy_Scope, &QAction::triggered, this, &MainWindow::showFuzzyScopeWindow);
    connect(ui->actionRange_State_2, &QAction::triggered, this, &MainWindow::showRangeWindow);
    connect(ui->actionSave_Decoded_Frames, &QAction::triggered, this, &MainWindow::handleSaveDecoded);
    connect(ui->actionSave_Decoded_Frames_CSV, &QAction::triggered, this, &MainWindow::handleSaveDecodedCsv);
    connect(ui->actionSingle_Multi_State_2, &QAction::triggered, this, &MainWindow::showSingleMultiWindow);
    connect(ui->actionFile_Comparison, &QAction::triggered, this, &MainWindow::showComparisonWindow);
    connect(ui->actionDBC_Comparison, &QAction::triggered, this, &MainWindow::showDBCComparisonWindow);
    connect(ui->actionScripting_INterface, &QAction::triggered, this, &MainWindow::showScriptingWindow);
    connect(ui->actionPreferences, &QAction::triggered, this, &MainWindow::showSettingsDialog);
    connect(ui->actionUDS_Firmware_Update, &QAction::triggered, this, &MainWindow::showUDSFirmwareUploaderWindow);
    connect(ui->actionDBC_File_Manager, &QAction::triggered, this, &MainWindow::showDBCFileWindow);
    connect(ui->actionFuzzing, &QAction::triggered, this, &MainWindow::showFuzzingWindow);
    connect(ui->actionUDS_Scanner, &QAction::triggered, this, &MainWindow::showUDSScanWindow);
    connect(ui->actionISO_TP_Decoder, &QAction::triggered, this, &MainWindow::showISOInterpreterWindow);
    connect(ui->actionSniffer, &QAction::triggered, this, &MainWindow::showSnifferWindow);
    connect(ui->actionMotorControlConfig, &QAction::triggered, this, &MainWindow::showMCConfigWindow);
    connect(ui->actionCapture_Bisector, &QAction::triggered, this, &MainWindow::showBisectWindow);
    connect(ui->actionSignal_Viewer, &QAction::triggered, this, &MainWindow::showSignalViewer);
    connect(ui->actionSave_Continuous_Logfile, &QAction::triggered, this, &MainWindow::handleContinousLogging);
    connect(ui->actionTemporal_Graph, &QAction::triggered, this, &MainWindow::showTemporalGraphWindow);
    connect(ui->actionCAN_Bridge, &QAction::triggered, this, &MainWindow::showCANBridgeWindow);

    //handlers fror interactions with the main can frame view table
    connect(ui->canFramesView, &QAbstractItemView::clicked, this, &MainWindow::gridClicked);
    connect(ui->canFramesView, &QAbstractItemView::doubleClicked, this, &MainWindow::gridDoubleClicked);
    ui->canFramesView->setContextMenuPolicy(Qt::CustomContextMenu);

    copyAct = new QAction(tr("Copy"), this);
    copyAct->setShortcut(QKeySequence::Copy);
    connect(copyAct, &QAction::triggered, this, &MainWindow::copyFromTable);
    ui->canFramesView->addAction(copyAct);
    connect(ui->canFramesView, &QAbstractItemView::customContextMenuRequested, this, &MainWindow::gridContextMenuRequest);

    connect(model, &CANFrameModel::updatedFiltersList, this, &MainWindow::updateFilterList);
    connect(CANConManager::getInstance(), &CANConManager::framesReceived, model, &CANFrameModel::addFrames);
    //new implementation for continuous logging
    connect(CANConManager::getInstance(), &CANConManager::framesReceived, this, &MainWindow::logReceivedFrame);

    connect(ui->cbInterpret, &QAbstractButton::toggled, this, &MainWindow::interpretToggled);
    connect(ui->cbOverwrite, &QAbstractButton::toggled, this, &MainWindow::overwriteToggled);
    connect(ui->cbPersistentFilters, &QAbstractButton::toggled, this, &MainWindow::presistentFiltersToggled);
    connect(ui->listFilters, &QListWidget::itemChanged, this, &MainWindow::filterListItemChanged);
    connect(ui->listBusFilters, &QListWidget::itemChanged, this, &MainWindow::busFilterListItemChanged);

    connect(ui->btnCaptureToggle, &QAbstractButton::clicked, this, &MainWindow::toggleCapture);
    connect(ui->btnClearFrames, &QAbstractButton::clicked, this, &MainWindow::clearFrames);
    connect(ui->btnNormalize, &QAbstractButton::clicked, this, &MainWindow::normalizeTiming);
    connect(ui->btnFilterAll, &QAbstractButton::clicked, this, &MainWindow::filterSetAll);
    connect(ui->btnFilterNone, &QAbstractButton::clicked, this, &MainWindow::filterClearAll);
    connect(ui->frameFilterSearch, &QLineEdit::textChanged, this, &MainWindow::filterFrameFilterList);
    connect(ui->btnExpandAll, &QAbstractButton::clicked, this, &MainWindow::expandAllRows);
    connect(ui->btnCollapseAll, &QAbstractButton::clicked, this, &MainWindow::collapseAllRows);

    connect(ui->analyzeControlLoadedAction, &QAction::triggered,
            this, &MainWindow::analyzeControlStatesForLoadedLog);
    connect(ui->analyzeControlSelectedAction, &QAction::triggered,
            this, &MainWindow::analyzeControlStatesForSelectedFrame);

    // Bookmark Connectors
    connect(bookmarkDialog, &BookmarkManagerDialog::jumpToBookmarkRequested,
            this, &MainWindow::jumpToBookmark);
    connect(bookmarkDialog, &BookmarkManagerDialog::deleteBookmarkRequested,
            this, &MainWindow::deleteBookmarkByIndex);
    connect(ui->actionBookmarkEditor, &QAction::triggered,
            this, &MainWindow::showBookmarksWindow);
    connect(ui->checkBoxAlternateLabels, &QCheckBox::toggled,
            this, [this](bool checked) {
                quickBookmarkUseAlternatingLabels = checked;
                resetQuickBookmarkToggle();
                quickBookmarkAlternateState = false;
            });

    connect(ui->checkBoxAutoBookmark, &QCheckBox::toggled, this, &MainWindow::setAutoBookmarkNewIdsActive);

    lbStatusConnected.setText(tr("Connected to 0 buses"));
    lbHelp.setText(tr("Press F1 on any screen for help"));
    lbHelp.setAlignment(Qt::AlignCenter);
    QFont boldFont;
    boldFont.setBold(true);
    lbHelp.setFont(boldFont);
    updateFileStatus();
    //lbStatusDatabase.setText(tr("No DBC database loaded"));
    ui->statusBar->insertWidget(0, &lbStatusConnected, 1);
    ui->statusBar->insertWidget(1, &lbStatusFilename, 1);
    ui->statusBar->insertWidget(2, &lbHelp, 1);
    //ui->statusBar->addWidget(&lbStatusDatabase);
    ui->lblRemoteConn->setVisible(false);
    ui->lineRemoteKey->setVisible(false);

    ui->lbFPS->setText("0");
    ui->lbNumFrames->setText("0");

    // Prevent annoying accidental horizontal scrolling when filter list is populated with long interpreted message names
    ui->listFilters->horizontalScrollBar()->setEnabled(false);

    connect(&updateTimer, &QTimer::timeout, this, &MainWindow::tickGUIUpdate);
    updateTimer.setInterval(250);
    updateTimer.start();

    elapsedTime = new QElapsedTimer;
    elapsedTime->start();

    isConnected = false;
    allowCapture = false;

    //create a temporary frame to be able to capture the correct
    //default height of an item in the table. Need to do this in case
    //of scaling or font differences between different computers.
    CANFrame temp;
    temp.bus = 0;
    temp.setFrameId(0x100);
    temp.isReceived = true;
    temp.setTimeStamp(QCanBusFrame::TimeStamp(0, 100000000));
    model->addFrame(temp, true);
    ui->canFramesView->resizeRowToContents(0);      // Resize the row to fit the contents so we get a proper height value
    qApp->processEvents();
    tickGUIUpdate(); //force a GUI refresh so that the row exists to measure
    normalRowHeight = ui->canFramesView->rowHeight(0);
    if (normalRowHeight == 0) normalRowHeight = 30; //should not be necessary but provides a sane number if something stupid happened.
    qDebug() << "normal row height = " << normalRowHeight;
    model->clearFrames();

    ui->canFramesView->verticalHeader()->setDefaultSectionSize(normalRowHeight);    // Set the default height for all rows to the height that was calculated

    //connect(CANConManager::getInstance(), CANConManager::connectionStatusUpdated, this, MainWindow::connectionStatusUpdated);
    connect(CANConManager::getInstance(), SIGNAL(connectionStatusUpdated(int)), this, SLOT(connectionStatusUpdated(int)));

    //Automatically create the connection window so it can be updated even if we never opened it.
    connectionWindow = new ConnectionWindow();
    connect(this, SIGNAL(suspendCapturing(bool)), connectionWindow, SLOT(setSuspendAll(bool)));

    //these either are unfinished/not working or are not for general use. But,they exist
    //so if you want to enable them and play with them then go for it.
    //ui->actionFirmware_Update->setVisible(false);
    ui->actionMotorControlConfig->setVisible(false);
    ui->actionSingle_Multi_State_2->setVisible(false);

    installEventFilter(this);

}

MainWindow::~MainWindow()
{
    updateTimer.stop();
    killEmAll(); //Ride the lightning
    delete ui;
    delete model;
    delete elapsedTime;
    delete dbcHandler;
}

//kill every sub window that could be open. At the moment a hard coded list
//but eventually each window should be registered and be able to be iterated.
void MainWindow::killEmAll()
{
    foreach (GraphingWindow *win, graphWindows)
    {
        killWindow(win);
    }
    killWindow(frameInfoWindow);
    killWindow(playbackWindow);
    killWindow(flowViewWindow);
    killWindow(frameSenderWindow);
    killWindow(comparatorWindow);
    killWindow(dbcMainEditor);
    killWindow(settingsDialog);
    killWindow(discreteStateWindow);
    killWindow(scriptingWindow);
    killWindow(rangeWindow);
    killWindow(dbcFileWindow);
    killWindow(fuzzingWindow);
    killWindow(udsScanWindow);
    killWindow(isoWindow);
    killWindow(snifferWindow);
    killWindow(bisectWindow);
    killWindow(udsFirmwareUploaderWindow);
    killWindow(motorctrlConfigWindow);
    killWindow(signalViewerWindow);
    killWindow(temporalGraphWindow);
    killWindow(canBridgeWindow);

    //trying to kill this window can cause a fault to happen. It's closed last just in case.
    killWindow(connectionWindow);
}

//forcefully close the window, kill it, and salt the earth
//note, for some stupid reason this function causes a seg fault
//it seems that when it runs just before the program closes it'll
//fault out when trying to close the connection window. I assume
//this could be because that window has long running threads open and doesn't
//close quickly or maybe cleanly. Investigate.
void MainWindow::killWindow(QDialog *win)
{
    if (win)
    {
        win->close();
        delete win;
        win = nullptr;
    }
}

void MainWindow::exitApp()
{
    this->close();
    QApplication::quit(); //forces the whole application to terminate when the main window is closed
}


//the close event can be trapped and ignored so put unsaved warnings in here so the user can abort the program closing if they forgot to save things.
void MainWindow::closeEvent(QCloseEvent *event)
{

    QMessageBox::StandardButton confirmDialog;

    for (int i = 0; i < dbcHandler->getFileCount(); i++)
    {
        DBCFile *file = dbcHandler->getFileByIdx(i);
        if (file->getDirtyFlag())
        {
            confirmDialog = QMessageBox::question(this, "Unsaved DBC", "DBC File:\n" + file->getFilename() + "\nAppears to have unsaved changes\nReally close without saving?", QMessageBox::Yes|QMessageBox::No);
            if (confirmDialog != QMessageBox::Yes)
            {
                event->ignore();
                return;
            }
        }
    }

    removeEventFilter(this);
    writeSettings();
    exitApp();
    event->accept();
}

bool MainWindow::getSelectedFrameInfo(CANFrame &outFrame, QModelIndex *outIndex)
{
    if (!ui || !ui->canFramesView || !model)
        return false;

    const QModelIndex proxyIndex = ui->canFramesView->currentIndex();
    if (!proxyIndex.isValid())
        return false;

    auto proxy = qobject_cast<QSortFilterProxyModel *>(ui->canFramesView->model());
    if (!proxy)
        return false;

    const QModelIndex sourceIndex = proxy->mapToSource(proxyIndex);
    if (!sourceIndex.isValid())
        return false;

    const QVector<CANFrame> *allFrames = model->getListReference();
    if (!allFrames)
        return false;

    const int row = sourceIndex.row();
    if (row < 0 || row >= allFrames->size())
        return false;

    outFrame = allFrames->at(row);
    if (outIndex)
        *outIndex = sourceIndex;
    return true;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj);

    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

        if (keyEvent->isAutoRepeat())
            return true;

        switch (keyEvent->key())
        {
        case Qt::Key_F1:
            if (event->type() == QEvent::KeyRelease)
                HelpWindow::getRef()->showHelp("mainscreen.md");
            return true;

        default:
            break;
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::updateSettings()
{
    readUpdateableSettings();
    emit settingsUpdated();
}

void MainWindow::readSettings()
{
    QSettings settings;

if (settings.value("Main/SaveRestorePositions", false).toBool())
{
    resize(settings.value("Main/WindowSize", QSize(1280, 850)).toSize());
    move(Utility::constrainedWindowPos(
        settings.value("Main/WindowPos", QPoint(100, 100)).toPoint()));

    ui->canFramesView->setColumnWidth(int(Column::TimeStamp),
        settings.value("Main/TimeColumn", 125).toUInt());
    ui->canFramesView->setColumnWidth(int(Column::FrameId),
        settings.value("Main/IDColumn", 70).toUInt());
    ui->canFramesView->setColumnWidth(int(Column::Extended),
        settings.value("Main/ExtColumn", 40).toUInt());
    ui->canFramesView->setColumnWidth(int(Column::Remote),
        settings.value("Main/RemColumn", 40).toUInt());
    ui->canFramesView->setColumnWidth(int(Column::Direction),
        settings.value("Main/DirColumn", 40).toUInt());
    ui->canFramesView->setColumnWidth(int(Column::Bus),
        settings.value("Main/BusColumn", 40).toUInt());
    ui->canFramesView->setColumnWidth(int(Column::Length),
        settings.value("Main/LengthColumn", 40).toUInt());
    ui->canFramesView->setColumnWidth(int(Column::ASCII),
        settings.value("Main/AsciiColumn", 50).toUInt());
    ui->canFramesView->setColumnWidth(int(Column::Data),
        settings.value("Main/DataColumn", 150).toUInt());
    // ui->canFramesView->setColumnWidth(int(Column::Idx),
    //     settings.value("Main/OGIDColumn", 90).toUInt());
}
else
{
    resize(QSize(1280, 850));
    move(Utility::constrainedWindowPos(QPoint(100, 100)));

    ui->canFramesView->setColumnWidth(int(Column::TimeStamp), 125);
    ui->canFramesView->setColumnWidth(int(Column::FrameId), 70);
    ui->canFramesView->setColumnWidth(int(Column::Extended), 40);
    ui->canFramesView->setColumnWidth(int(Column::Remote), 40);
    ui->canFramesView->setColumnWidth(int(Column::Direction), 40);
    ui->canFramesView->setColumnWidth(int(Column::Bus), 40);
    ui->canFramesView->setColumnWidth(int(Column::Length), 40);
    ui->canFramesView->setColumnWidth(int(Column::ASCII), 50);
    ui->canFramesView->setColumnWidth(int(Column::Data), 150);
    // ui->canFramesView->setColumnWidth(int(Column::Idx), 90);
}

    if (settings.value("Main/AutoScroll", false).toBool())
    {
        ui->cbAutoScroll->setChecked(true);
        ui->btnCaptureToggle->setText("Restart Capture");
    }

    quickBookmarkLabel = "Bookmark";
    quickBookmarkAlternateLabel = "Alternate Bookmark";
    quickBookmarkUseAlternatingLabels = false;
    quickBookmarkAlternateState = false;

    ui->quickBookmarkLabel->setText(quickBookmarkLabel);
    ui->quickBookmarkAlternateLabel->setText(quickBookmarkAlternateLabel);

    ui->checkBoxAlternateLabels->blockSignals(true);
    ui->checkBoxAlternateLabels->setChecked(false);
    ui->checkBoxAlternateLabels->blockSignals(false);

    settings.remove("Main/QuickBookmarkLabel");
    settings.remove("Main/QuickBookmarkAlternateLabel");
    settings.remove("Main/QuickBookmarkUseAlternatingLabels");

    int fontSize = settings.value("Main/FontSize", 9).toUInt();
    QFont newFont = QFont(ui->canFramesView->font());
    newFont.setPointSize(fontSize);
    ui->canFramesView->setFont(newFont);

    readUpdateableSettings();
}


/*
 * TODO: The way the frame timing mode is specified is DEAD STUPID. There shouldn't be three boolean values
 * for this. Instead switch it all to an ENUM or something sane.
*/
void MainWindow::readUpdateableSettings()
{
    QSettings settings;
    useHex = settings.value("Main/UseHex", true).toBool();
    model->setHexMode(useHex);
    Utility::decimalMode = !useHex;

    useColorsByCanId = settings.value("Main/ColorsByCanId", false).toBool();
    model->setUseColorsByCanId(useColorsByCanId);

    bool tempBool;
    TimeStyle ts = TS_MICROS;
    tempBool = settings.value("Main/TimeSeconds", false).toBool();
    if (tempBool) ts = TS_SECONDS;
    tempBool = settings.value("Main/TimeClock", false).toBool();
    if (tempBool) ts = TS_CLOCK;
    tempBool = settings.value("Main/TimeMillis", false).toBool();
    if (tempBool) ts = TS_MILLIS;
    model->setTimeStyle(ts);

    useFiltered = settings.value("Main/UseFiltered", false).toBool();
    model->setTimeFormat(settings.value("Main/TimeFormat", "MMM-dd HH:mm:ss.zzz").toString());
    ignoreDBCColors = settings.value("Main/IgnoreDBCColors", false).toBool();
    model->setIgnoreDBCColors(ignoreDBCColors);
    int bpl = settings.value("Main/BytesPerLine", 8).toInt();
    model->setBytesPerLine(bpl);

    CSVAbsTime = settings.value("Main/CSVAbsTime", false).toBool();

    if (settings.value("Main/FilterLabeling", false).toBool())
        ui->listFilters->setMaximumWidth(100);
    else
        ui->listFilters->setMaximumWidth(100);
    updateFilterList();    
}    


void MainWindow::writeSettings()
{
    QSettings settings;

    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        settings.setValue("Main/WindowSize", size());
        settings.setValue("Main/WindowPos", pos());
    }

    settings.setValue("Main/TimeColumn",
        ui->canFramesView->columnWidth(int(Column::TimeStamp)));
    settings.setValue("Main/IDColumn",
        ui->canFramesView->columnWidth(int(Column::FrameId)));
    settings.setValue("Main/ExtColumn",
        ui->canFramesView->columnWidth(int(Column::Extended)));
    settings.setValue("Main/RemColumn",
        ui->canFramesView->columnWidth(int(Column::Remote)));
    settings.setValue("Main/DirColumn",
        ui->canFramesView->columnWidth(int(Column::Direction)));
    settings.setValue("Main/BusColumn",
        ui->canFramesView->columnWidth(int(Column::Bus)));
    settings.setValue("Main/LengthColumn",
        ui->canFramesView->columnWidth(int(Column::Length)));
    settings.setValue("Main/AsciiColumn",
        ui->canFramesView->columnWidth(int(Column::ASCII)));
    settings.setValue("Main/DataColumn",
        ui->canFramesView->columnWidth(int(Column::Data)));
    settings.setValue("Main/OGIDColumn",
        ui->canFramesView->columnWidth(int(Column::Idx)));
}


void MainWindow::updateConnectionSettings(QString connectionType, QString port, int speed0, int speed1)
{
    Q_UNUSED(connectionType);
    Q_UNUSED(port);
    Q_UNUSED(speed0);
    Q_UNUSED(speed1);
    //connType = connectionType;
    //portName = port;

    //canSpeed0 = speed0;
    //canSpeed1 = speed1;
    if (isConnected)
    {
        //emit updateBaudRates(speed0, speed1);
    }
}

void MainWindow::headerClicked(int logicalIndex)
{
    //ui->canFramesView->sortByColumn(logicalIndex);
    model->sortByColumn(logicalIndex);

    manageRowExpansion();
}

void MainWindow::expandAllRows()
{
    bool goAhead = false;
    int numRows = ui->canFramesView->model()->rowCount();

    if (numRows > 20000)
    {
        QMessageBox::StandardButton confirmDialog;
        confirmDialog = QMessageBox::question(this, "Really?", "It's not recommended to use this\non more than 20000 frames.\nIt can take a long time.\n\nYou have been warned!\nStill do it?",
                                  QMessageBox::Yes|QMessageBox::No);

        if (confirmDialog == QMessageBox::Yes) goAhead = true;
    }
    else goAhead = true;

    if (goAhead)
    {
        ui->canFramesView->resizeRowsToContents();

        rowExpansionActive = true;
    }
}

int64_t MainWindow::selectedFrameTimestamp()
{
    QModelIndex proxyIdx = ui->canFramesView->currentIndex();
    if (!proxyIdx.isValid()) return -1;
    QSortFilterProxyModel *proxy = qobject_cast<QSortFilterProxyModel*>(ui->canFramesView->model());
    int row = proxy ? proxy->mapToSource(proxyIdx).row() : proxyIdx.row();
    const QVector<CANFrame> *filtered = model->getFilteredListReference();
    if (row < 0 || row >= filtered->count()) return -1;
    const CANFrame &f = filtered->at(row);
    return f.timeStamp().seconds() * 1000000LL + f.timeStamp().microSeconds();
}

void MainWindow::scrollToNearestTimestamp(int64_t timestamp)
{
    const QVector<CANFrame> *filtered = model->getFilteredListReference();
    if (timestamp < 0 || filtered->isEmpty()) return;
    int lo = 0, hi = filtered->count() - 1, best = 0;
    int64_t bestDiff = std::numeric_limits<int64_t>::max();
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        const CANFrame &f = filtered->at(mid);
        int64_t ts = f.timeStamp().seconds() * 1000000LL + f.timeStamp().microSeconds();
        int64_t diff = qAbs(ts - timestamp);
        if (diff < bestDiff) { bestDiff = diff; best = mid; }
        if (ts < timestamp) lo = mid + 1;
        else hi = mid - 1;
    }
    QSortFilterProxyModel *proxy = qobject_cast<QSortFilterProxyModel*>(ui->canFramesView->model());
    QModelIndex sourceIdx = model->index(best, 0);
    QModelIndex viewIdx = proxy ? proxy->mapFromSource(sourceIdx) : sourceIdx;
    ui->canFramesView->setCurrentIndex(viewIdx);
    ui->canFramesView->scrollTo(viewIdx, QAbstractItemView::PositionAtCenter);
}

void MainWindow::manageRowExpansion()
{
    int numRows = ui->canFramesView->model()->rowCount();
    if(numRows < 20000)
    {
        if(rowExpansionActive && model->getInterpretMode())
            ui->canFramesView->resizeRowsToContents();
    }
    else
    {
        disableAutoRowExpansion();
    }
}

void MainWindow::disableAutoRowExpansion()
{
    rowExpansionActive = false;
}

void MainWindow::collapseAllRows()
{
    bool goAhead = false;
    int numRows = ui->canFramesView->model()->rowCount();

    if (numRows > 50000)
    {
        QMessageBox::StandardButton confirmDialog;
        confirmDialog = QMessageBox::question(this, "Really?", "It's not recommended to use this\non more than 50000 frames.\nIt can take a long time.\n\nYou have been warned!\nStill do it?",
                                  QMessageBox::Yes|QMessageBox::No);

        if (confirmDialog == QMessageBox::Yes) goAhead = true;
    }
    else goAhead = true;

    if (goAhead)
    {
        for (int i = 0; i < numRows; i++) ui->canFramesView->setRowHeight(i, normalRowHeight);

        rowExpansionActive = false;
    }
}

void MainWindow::gridClicked(QModelIndex idx)
{
    //qDebug() << "Grid Clicked";
    if (ui->canFramesView->rowHeight(idx.row()) > normalRowHeight)
    {
        ui->canFramesView->setRowHeight(idx.row(), normalRowHeight);
    }
    else {
        ui->canFramesView->resizeRowToContents(idx.row());
    }
}

void MainWindow::gridDoubleClicked(const QModelIndex &idx)
{
    qDebug() << "Grid double clicked";
    if (!idx.isValid()) return;

    auto proxy = qobject_cast<QSortFilterProxyModel *>(ui->canFramesView->model());
    if (!proxy) return;

    QModelIndex sourceIndex = proxy->mapToSource(idx);
    if (!sourceIndex.isValid()) return;

    const CANFrame *frame = model->getFilteredFrameRef(sourceIndex.row());
    if (!frame) return;

    emit sendCenterTimeID(frame->frameId(), frame->timeStamp().microSeconds() / 1000000.0);
}

void MainWindow::gridContextMenuRequest(QPoint pos)
{
    QModelIndex idx = ui->canFramesView->indexAt(pos);
    qDebug() << "Pos" << pos << " Row " << idx.row() << " Col " << idx.column();
    if (!idx.isValid()) return;

    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    menu->addAction(copyAct);
    menu->addSeparator();
    menu->addAction(tr("Analyze Control Type"), this, &MainWindow::analyzeControlStatesForSelectedFrame);
    menu->addAction(tr("Analyze Around This Frame"), this, &MainWindow::analyzeCurrentBookmarkOrSelection);

    if (idx.column() == 8) // DATA column
    {
        contextMenuPosition = pos;
        menu->addSeparator();
        menu->addAction(tr("Add to a new graphing window"), this, SLOT(setupAddToNewGraph()));
        menu->addAction(tr("Add to latest graphing window"), this, SLOT(setupSendToLatestGraphWindow()));
    }

    menu->popup(ui->canFramesView->viewport()->mapToGlobal(pos));
}

void MainWindow::copyFromTable()
{
    copySelection();
}

void MainWindow::copySelection()
{
    QItemSelectionModel *selectionModel = ui->canFramesView->selectionModel();
    QModelIndexList selectedIndexes = selectionModel->selectedIndexes();

    if(selectedIndexes.isEmpty())
        return;

    // QModelIndex::operator< sorts by row and then by column.
    std::sort(selectedIndexes.begin(), selectedIndexes.end());

    QString selectedText;
    int currentRow = -1;
    int lastRow = selectedIndexes.last().row();

    for(const QModelIndex &current : selectedIndexes)
    {
        if (currentRow != -1 && current.row() != currentRow)
        {
            // remove last tab
            if (selectedText.endsWith(QLatin1Char('\t')))
                selectedText.chop(1);
            selectedText.append(QLatin1Char('\n'));
        }
        currentRow = current.row();

        QString cellText = current.data(Qt::DisplayRole).toString();

        // Replace newlines within a cell to avoid breaking the table structure in Excel
        cellText.replace(QLatin1Char('\n'), QLatin1String("  "));

        selectedText.append(cellText);

        if (current.row() != lastRow || current != selectedIndexes.last())
        {
            selectedText.append(QLatin1Char('\t'));
        }
    }

    // remove last tab if it exists
    if (selectedText.endsWith(QLatin1Char('\t')))
        selectedText.chop(1);

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(selectedText);
}

QString MainWindow::getSignalNameFromPosition(QPoint pos)
{
    //there's a bit of an issue to solve. The data column is one big string even if there are a number
    //of signals in there. So, the basic idea is to find out how tall the font is and where the user
    //clicked within the cell. Then find out which line that puts us over.
    QModelIndex idx = ui->canFramesView->indexAt(pos); //figure out where in the view we clicked (row, column)
    int fontHeight = ui->canFramesView->fontMetrics().height();
    int cellBaseY = ui->canFramesView->rowViewportPosition(idx.row());
    int lineOffset = (pos.y() - cellBaseY) / fontHeight;
    qDebug() << "Offset: " << lineOffset;
    QString lineText = idx.data().toString().split("\n")[lineOffset];
    qDebug() << "Line Text: " << lineText;
    return lineText.split(":")[0];
}

uint32_t MainWindow::getMessageIDFromPosition(QPoint pos)
{
    QModelIndex idx = ui->canFramesView->indexAt(pos); //figure out where in the view we clicked (row, column)
    QString idText = ui->canFramesView->model()->index(idx.row(), 1).data().toString();
    return Utility::ParseStringToNum(idText);
}

void MainWindow::setupAddToNewGraph()
{
    showGraphingWindow(); //creates a new window and sets it as latest
    setupSendToLatestGraphWindow(); //then call the other function to finish
}


void MainWindow::setupSendToLatestGraphWindow()
{
    if (!lastGraphingWindow) showGraphingWindow();
    GraphParams param;
    QString signalName = getSignalNameFromPosition(contextMenuPosition);
    param.ID = getMessageIDFromPosition(contextMenuPosition);
    DBC_MESSAGE *msg = dbcHandler->findMessage(param.ID);
    if(msg)
    {
        DBC_SIGNAL *sig = msg->sigHandler->findSignalByName(signalName);
        if(sig)
        {
            param.associatedSignal = sig;
            param.bias = sig->bias;
            param.intelFormat = sig->intelByteOrder;
            param.isSigned = sig->valType == SIGNED_INT ? true : false;
            param.numBits = sig->signalSize;
            param.scale = sig->factor;
            param.startBit = sig->startBit;
            param.stride = 1;
            param.graphName = sig->name;
            param.lineColor = QColor(QRandomGenerator::global()->bounded(160), QRandomGenerator::global()->bounded(160), QRandomGenerator::global()->bounded(160));
            param.lineWidth = 1;
            param.fillColor = QColor(128, 128, 128, 0);
            param.mask = 0xFFFFFFFFFFFFFFFFull;
            param.drawOnlyPoints = false;
            param.pointType = 0;

            lastGraphingWindow->createGraph(param); //add the new graph to the window
        }
        else
        {
            QMessageBox msgbox;
            QString boxmsg = "Cannot find ID 0x" + QStringLiteral("%1").arg(param.ID, 3, 16, QLatin1Char('0')) + " in DBC message " + msg->name + ". Not adding graph.";
            msgbox.setText(boxmsg);
            msgbox.exec();
        }
    }
    else
    {
        QMessageBox msgbox;
        QString boxmsg = "Cannot find ID 0x" + QStringLiteral("%1").arg(param.ID, 3, 16, QLatin1Char('0')) + " in DBC file(s). Not adding graph.";
        msgbox.setText(boxmsg);
        msgbox.exec();
    }
}
void MainWindow::interpretToggled(bool state)
{
    model->setInterpretMode(state);
    //ui->canFramesView->resizeRowsToContents();   //a VERY costly operation!
}

void MainWindow::overwriteToggled(bool state)
{
    if (state)
    {
        QMessageBox::StandardButton confirmDialog;
        confirmDialog = QMessageBox::question(this, "Danger Will Robinson", "Enabling Overwrite mode will\ndelete your captured frames\nand replace them with one\nframe per ID.\n\nAre you ready to do that?",
                                      QMessageBox::Yes|QMessageBox::No);
        if (confirmDialog == QMessageBox::Yes)
        {
            model->setOverwriteMode(true);
        }
        else ui->cbOverwrite->setCheckState(Qt::Unchecked);
    }
    else
    {
        rowExpansionActive = false;
        model->setOverwriteMode(false);
    }
}

void MainWindow::presistentFiltersToggled(bool state)
{
    if (state)
    {
        model->setClearMode(true);
    }
    else
    {
        model->setClearMode(false);
    }
}

void MainWindow::updateFilterList()
{
    if (model == nullptr) return;
    const QMap<int, bool> *filters = model->getFiltersReference();
    const QMap<int, bool> *busFilters = model->getBusFiltersReference();
    if (filters == nullptr || busFilters == nullptr) return;

    qDebug() << "updateFilterList called on MainWindow";

    inhibitFilterUpdate = true;

    ui->listFilters->clear();
    ui->listBusFilters->clear();

    if (filters->isEmpty()) return;

    QMap<int, bool>::const_iterator filterIter;
    for (filterIter = filters->begin(); filterIter != filters->end(); ++filterIter)
    {
        /*QListWidgetItem *thisItem = */FilterUtility::createCheckableFilterItem(filterIter.key(), filterIter.value(), ui->listFilters);
    }

    if (busFilters->isEmpty()) return;

    for (filterIter = busFilters->begin(); filterIter != busFilters->end(); ++filterIter)
    {
        /*QListWidgetItem *thisItem = */ FilterUtility::createCheckableBusFilterItem(filterIter.key(), filterIter.value(), ui->listBusFilters);
    }
    inhibitFilterUpdate = false;
}

void MainWindow::filterListItemChanged(QListWidgetItem *item)
{
    if (inhibitFilterUpdate) return;

    int ID = FilterUtility::getIdAsInt(item);
    bool isSet = (item->checkState() == Qt::Checked);

    int64_t savedTs = selectedFrameTimestamp();
    model->setFilterState(ID, isSet);
    scrollToNearestTimestamp(savedTs);
    manageRowExpansion();
}

void MainWindow::busFilterListItemChanged(QListWidgetItem *item)
{
    if (inhibitFilterUpdate) return;

    int ID = FilterUtility::getIdAsInt(item);
    bool isSet = (item->checkState() == Qt::Checked);

    int64_t savedTs = selectedFrameTimestamp();
    model->setBusFilterState(ID, isSet);
    scrollToNearestTimestamp(savedTs);
    manageRowExpansion();
}

void MainWindow::filterSetAll()
{
    inhibitFilterUpdate = true;
    for (int i = 0; i < ui->listFilters->count(); i++)
        ui->listFilters->item(i)->setCheckState(Qt::Checked);
    inhibitFilterUpdate = false;
    if (ui->frameFilterSearch)
        ui->frameFilterSearch->clear();

    int64_t savedTs = selectedFrameTimestamp();
    model->setAllFilters(true);
    scrollToNearestTimestamp(savedTs);
    manageRowExpansion();
}

void MainWindow::filterClearAll()
{
    inhibitFilterUpdate = true;
    for (int i = 0; i < ui->listFilters->count(); i++)
        ui->listFilters->item(i)->setCheckState(Qt::Unchecked);
    inhibitFilterUpdate = false;
    model->setAllFilters(false);
    if (ui->frameFilterSearch)
        ui->frameFilterSearch->clear();
    MainWindow::clearInspectDock();
}

void MainWindow::filterFrameFilterList(const QString &text)
{
    const QString needle = text.trimmed();

    for (int i = 0; i < ui->listFilters->count(); ++i)
    {
        QListWidgetItem *item = ui->listFilters->item(i);
        if (!item) continue;

        const bool match = needle.isEmpty() ||
                           item->text().contains(needle, Qt::CaseInsensitive);

        item->setHidden(!match);
    }
}

void MainWindow::logReceivedFrame(CANConnection* conn, QVector<CANFrame> frames)
{
    Q_UNUSED(conn);

    processAutoBookmarks(frames);

    if (continuousLogging)
    {
        FrameFileIO::writeContinuousNative(&frames, 0);
    }
}

void MainWindow::tickGUIUpdate()
{
    rxFrames = model->sendBulkRefresh();
    //if(rxFrames>0)
    //{
        int elapsed = elapsedTime->elapsed();
        if(elapsed) {
            framesPerSec = (framesPerSec + (rxFrames * 1000 / elapsed)) / 2;
            elapsedTime->restart();
        }
        else
            framesPerSec = 0;

        ui->lbNumFrames->setText(QString::number(model->rowCount()));
        if (rxFrames > 0 && /*allowCapture && */ ui->cbAutoScroll->isChecked())
                ui->canFramesView->scrollToBottom();
        ui->lbFPS->setText(QString::number(framesPerSec));
        if (rxFrames > 0)
        {
            bDirty = true;
            emit framesUpdated(rxFrames); //anyone care that frames were updated?
            manageRowExpansion();
        }

        if (model->needsFilterRefresh()) updateFilterList();

        if (continuousLogging)
        {
//            const QVector<CANFrame> *modelFrames = model->getListReference();
//            FrameFileIO::writeContinuousNative(modelFrames, modelFrames->count() - rxFrames);

            continuousLogFlushCounter++;
            if ((continuousLogFlushCounter % 3) == 0)
            {
                if (ui->lblContMsg->text().length() > 2)
                {
                    ui->lblContMsg->setText("");
                }
                else
                {
                    ui->lblContMsg->setText("LOGGING");
                }
            }
            if (continuousLogFlushCounter > 8)
            {
                continuousLogFlushCounter = 0;
                FrameFileIO::flushContinuousNative();
            }
        }

        rxFrames = 0;
    //}
}

void MainWindow::gotFrames(int framesSinceLastUpdate)
{
    rxFrames += framesSinceLastUpdate;
    emit frameUpdateRapid(framesSinceLastUpdate);
}

void MainWindow::addFrameToDisplay(CANFrame &frame, bool autoRefresh = false)
{
    model->addFrame(frame, autoRefresh);
    if (autoRefresh)
    {
        if (ui->cbAutoScroll->isChecked()) ui->canFramesView->scrollToBottom();
        ui->lbNumFrames->setText(QString::number(model->rowCount()));
    }
}

//A sub-window is sending us a center on timestamp and ID signal
//try to find the relevant frame in the list and focus on it.

void MainWindow::gotCenterTimeID(uint32_t ID, double timestamp)
{
    if (!model) return;

    const int rawRow = model->getIndexFromTimeID(ID, timestamp);
    if (rawRow < 0) return;

    const int filteredRow = model->findFilteredRowByOriginalIndex(rawRow);
    if (filteredRow < 0) return; // frame exists in raw list but is hidden by filters

    auto *proxy = qobject_cast<QSortFilterProxyModel *>(ui->canFramesView->model());
    if (!proxy) return;

    const QModelIndex sourceIndex = model->index(filteredRow, 0);
    if (!sourceIndex.isValid()) return;

    const QModelIndex proxyIndex = proxy->mapFromSource(sourceIndex);
    if (!proxyIndex.isValid()) return;

    ui->canFramesView->setCurrentIndex(proxyIndex);
    ui->canFramesView->selectRow(proxyIndex.row());
    ui->canFramesView->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
}

void MainWindow::clearFrames()
{
    ui->canFramesView->scrollToTop();
    model->clearFrames();
    CANConManager::getInstance()->resetTimeBasis();
    ui->lbNumFrames->setText(QString::number(model->rowCount()));
    bDirty = false;
    loadedFileName = "";
    updateFileStatus();
    emit framesUpdated(-1);
}

void MainWindow::normalizeTiming()
{
    model->normalizeTiming();
    model->setTimeStyle(TS_SECONDS);
    emit framesUpdated(-2); //claim an all new set of frames because every frame was updated.
}

void MainWindow::handleLoadFile()
{
    QString filename;
    QVector<CANFrame> tempFrames;

    QMessageBox::StandardButton confirmDialog;

    bool loadResult = FrameFileIO::loadFrameFile(filename, &tempFrames, bookmarkManager);

    if (!loadResult)
    {
        if (tempFrames.count() > 0) //only ask if at least one frame was decoded.
        {
            confirmDialog = QMessageBox::question(this, "Error Loading", "Do you want to salvage what could be loaded?",
                                      QMessageBox::Yes|QMessageBox::No);
            if (confirmDialog == QMessageBox::Yes) {
                loadResult = true;
            }
        }
    }

    if (loadResult)
    {
        disableAutoRowExpansion();
        ui->canFramesView->scrollToTop();
        model->clearFrames();
        model->insertFrames(tempFrames);
        loadedFileName = filename;
        model->recalcOverwrite();
        ui->lbNumFrames->setText(QString::number(model->rowCount()));
        if (ui->cbAutoScroll->isChecked()) ui->canFramesView->scrollToBottom();

        updateFileStatus();
        emit framesUpdated(-1);
    }
}

void MainWindow::handleDroppedFile(const QString &filename)
{
    QProgressDialog progress(qApp->activeWindow());
    progress.setWindowModality(Qt::WindowModal);
    progress.setLabelText("Loading file...");
    progress.setCancelButton(nullptr);
    progress.setRange(0,0);
    progress.setMinimumDuration(0);
    progress.show();

    QVector<CANFrame> loadedFrames;
    bool loadResult = false;

    if (FrameFileIO::isNativeCSVFile(filename))
    {
        loadResult = FrameFileIO::loadNativeCSVFile(filename, &loadedFrames, bookmarkManager);
    }
    else
    {
        loadResult = FrameFileIO::autoDetectLoadFile(filename, &loadedFrames);
        if (loadResult && bookmarkManager) bookmarkManager->clear();
    }

    progress.cancel();
    
    if (!loadResult)
    {
        if (loadedFrames.count() > 0) //only ask if at least one frame was decoded.
        {
            QMessageBox::StandardButton confirmDialog = QMessageBox::question(this, "Error Loading", "Do you want to salvage what could be loaded?",
                                      QMessageBox::Yes|QMessageBox::No);
            if (confirmDialog == QMessageBox::Yes)
            {
                loadResult = true;
            }
        }
    }

    if (loadResult)
    {
        disableAutoRowExpansion();
        ui->canFramesView->scrollToTop();
        model->clearFrames();
        model->insertFrames(loadedFrames);
        loadedFileName = filename;
        model->recalcOverwrite();
        ui->lbNumFrames->setText(QString::number(model->rowCount()));
        if (ui->cbAutoScroll->isChecked()) ui->canFramesView->scrollToBottom();

        updateFileStatus();
        emit framesUpdated(-1);
    }
}

void MainWindow::handleSaveFile()
{
    QString filename;

    if (FrameFileIO::saveFrameFile(filename, model->getListReference(), bookmarkManager))
    {
        loadedFileName = filename;
        updateFileStatus();
    }
}

void MainWindow::handleContinousLogging()
{
    continuousLogging = !continuousLogging;

    if (continuousLogging)
    {
        ui->actionSave_Continuous_Logfile->setText(tr("Cease Continuous Logging"));
        FrameFileIO::openContinuousNative();
    }
    else
    {
        ui->actionSave_Continuous_Logfile->setText(tr("Start Continuous Logging"));
        ui->lblContMsg->setText("");
        FrameFileIO::closeContinuousNative();
    }
}

void MainWindow::handleSaveFilteredFile()
{
    QString filename;

    if (FrameFileIO::saveFrameFile(filename, model->getFilteredListReference(), nullptr))
    {
        loadedFileName = filename;
        updateFileStatus();
    }
}

void MainWindow::handleSaveFilters()
{
    QString filename;
    QFileDialog dialog(this);
    QSettings settings;

    QStringList filters;
    filters.append(QString(tr("Filter list (*.ftl)")));

    dialog.setDirectory(settings.value("Filters/LoadSaveDirectory", dialog.directory().path()).toString());
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(filters);
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setAcceptMode(QFileDialog::AcceptSave);

    if (dialog.exec() == QDialog::Accepted)
    {
        filename = dialog.selectedFiles()[0];
        if (!filename.contains('.')) filename += ".ftl";
        if (dialog.selectedNameFilter() == filters[0]) model->saveFilterFile(filename);
        settings.setValue("Filters/LoadSaveDirectory", dialog.directory().path());
    }
}

void MainWindow::handleLoadFilters()
{
    QString filename;
    QFileDialog dialog(this);
    QSettings settings;

    QStringList filters;
    filters.append(QString(tr("Filter List (*.ftl)")));

    dialog.setDirectory(settings.value("Filters/LoadSaveDirectory", dialog.directory().path()).toString());
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilters(filters);
    dialog.setViewMode(QFileDialog::Detail);

    if (dialog.exec() == QDialog::Accepted)
    {
        filename = dialog.selectedFiles()[0];
        //right now there is only one file type that can be loaded here so just do it.
        model->loadFilterFile(filename);
        settings.setValue("Filters/LoadSaveDirectory", dialog.directory().path());
    }
}

void MainWindow::handleSaveDecoded()
{
    handleSaveDecodedMethod(false);
}

void MainWindow::handleSaveDecodedCsv()
{
    handleSaveDecodedMethod(true);
}

void MainWindow::handleSaveDecodedMethod(bool csv)
{
    QString filename;
    QFileDialog dialog(this);
    QSettings settings;

    QStringList filters;
    if (!csv) filters.append(QString(tr("Text File (*.txt *.TXT)")));
    else filters.append(QString(tr("CSV File (*.csv *.CSV)")));

    dialog.setDirectory(settings.value("FileIO/LoadSaveDirectory", dialog.directory().path()).toString());
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(filters);
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setAcceptMode(QFileDialog::AcceptSave);

    if (dialog.exec() == QDialog::Accepted)
    {
        filename = dialog.selectedFiles()[0];
        if (!filename.contains('.'))
        {
            if (!csv) filename += ".txt";
            else filename += ".csv";
        }

        if(csv)
            saveDecodedTextFileAsColumns(filename);
        else
            saveDecodedTextFile(filename);

        settings.setValue("FileIO/LoadSaveDirectory", dialog.directory().path());
    }
}

void MainWindow::saveDecodedTextFileAsColumns(QString filename)
{
    QFile *outFile = new QFile(filename);
    const QVector<CANFrame> *frames = model->getFilteredListReference();

    //const unsigned char *data;
    int dataLen;
    const CANFrame *frame;

    if (!outFile->open(QIODevice::WriteOnly | QIODevice::Text))
        return;
/*
Time: 205.173000   ID: 0x20E Std Bus: 0 Len: 8
Data Bytes: 88 10 00 13 BB 00 06 00
    SignalName	Value
*/
    QList<QPair<uint32_t, int>> msgsAndColumns;
    int columnsAdded = 0;
    int dataStartCol = 0;

    QString builderString;
    if (CSVAbsTime)
    {
        builderString += tr("Year") + "," + tr("Month") + "," + tr("Day") + "," + tr("Hour") + "," + tr("Minute") + "," + tr("Second") + "," + tr("Ms") + ",";
        dataStartCol += 7;
    }
    else
    {
        //time
        builderString += tr("Time") + ",";
        dataStartCol++;
    }
    //id
    builderString += tr("ID") + ",";
    dataStartCol++;
    //if (frame->hasExtendedFrameFormat()) builderString += tr(" Ext ");
    //else builderString += tr(" Std ");
    //bus
    builderString += tr("Bus") + ",";
    dataStartCol++;
    //len
    builderString += tr("DataLen") + ",";
    dataStartCol++;

    columnsAdded = dataStartCol;

    //loop through all the frames and the message data therein
    for (int c = 0; c < frames->count(); c++)
    {
        frame = &frames->at(c);
        //data = reinterpret_cast<const unsigned char *>(frame->payload().constData());
        dataLen = frame->payload().count();

        //add all column names
        if (dbcHandler != nullptr)
        {
            DBC_MESSAGE *msg = dbcHandler->findMessage(*frame);
            if (msg != nullptr)
            {
                bool found = false;
                for (int j = 0; j < msg->sigHandler->getCount(); j++)
                {
                    if(j==0)
                    {
                        for(int m=0; m<msgsAndColumns.count(); m++)
                        {
                            if(msgsAndColumns[m].first == msg->ID)
                                found = true;
                        }
                        if(found == false)
                            msgsAndColumns.append(QPair<uint32_t,int>(msg->ID, columnsAdded));
                    }

                    if(found == false)
                    {
                        QString temp;
                        if (msg->sigHandler->findSignalByIdx(j)->processAsText(*frame, temp))
                        {
                            builderString.append(msg->sigHandler->findSignalByIdx(j)->name);
                            builderString.append(",");
                            columnsAdded++;
                        }
                    }
                }
            }
        }
    }

    //add EOL
    builderString += "\n";
    //write out the header row
    outFile->write(builderString.toUtf8());

        //builderString = tr("Data Bytes: ");
        //for (int temp = 0; temp < dataLen; temp++)
        //{
        //    builderString += Utility::formatNumber(data[temp]) + " ";
        //}
        //builderString += "\n";
        //outFile->write(builderString.toUtf8());

    int dataColumnsAdded = 0;
    builderString = "";
    for (int c = 0; c < frames->count(); c++)
    {
        dataColumnsAdded = 0;
        frame = &frames->at(c);
        //data = reinterpret_cast<const unsigned char *>(frame->payload().constData());
        dataLen = frame->payload().count();

        QString builderString;
        if (CSVAbsTime)
        {
            QDateTime dt = QDateTime::fromMSecsSinceEpoch(frame->timeStamp().microSeconds() / 1000);
            builderString += QString::number(dt.date().year()) + "," + QString::number(dt.date().month()) + ",";
            builderString += QString::number(dt.date().day()) + "," + QString::number(dt.time().hour()) + ",";
            builderString += QString::number(dt.time().minute()) + "," + QString::number(dt.time().second()) + ",";
            builderString += QString::number(dt.time().msec()) + ",";
            dataColumnsAdded += 7;
        }
        else {
            builderString += QString::number((frame->timeStamp().microSeconds() / 1000000.0), 'f', 6) + ",";
            dataColumnsAdded++;
        }
        //id
        builderString += Utility::formatCANID(frame->frameId(), frame->hasExtendedFrameFormat()) + ",";
        dataColumnsAdded++;
        //if (frame->hasExtendedFrameFormat()) builderString += tr(" Ext ");
        //else builderString += tr(" Std ");
        //bus
        builderString += QString::number(frame->bus) + ",";
        dataColumnsAdded++;
        //len
        builderString += QString::number(dataLen) + ",";
        dataColumnsAdded++;

        if (dbcHandler != nullptr)
        {
            DBC_MESSAGE *msg = dbcHandler->findMessage(*frame);
            if (msg != nullptr)
            {
                for (int j = 0; j < msg->sigHandler->getCount(); j++)
                {
                    if(j==0)
                    {
                        for(int i = 0; i<msgsAndColumns.count(); i++)
                        {
                            if(msgsAndColumns[i].first == msg->ID)
                            {
                                int startCol = msgsAndColumns[i].second;
                                while(dataColumnsAdded < startCol)
                                {
                                    builderString += ",";
                                    dataColumnsAdded++;
                                }
                            }
                        }
                    }

                    QString temp;
                    if (msg->sigHandler->findSignalByIdx(j)->processAsText(*frame, temp, false, false))
                    {
                        builderString.append(temp);
                        builderString.append(",");
                        dataColumnsAdded++;
                    }
                }
            }
            builderString.append("\n");
            outFile->write(builderString.toUtf8());
        }
    }
    outFile->close();
}

void MainWindow::saveDecodedTextFile(QString filename)
{
    QFile *outFile = new QFile(filename);
    const QVector<CANFrame> *frames = model->getFilteredListReference();

    const unsigned char *data;
    int dataLen;
    const CANFrame *frame;

    if (!outFile->open(QIODevice::WriteOnly | QIODevice::Text))
        return;
/*
Time: 205.173000   ID: 0x20E Std Bus: 0 Len: 8
Data Bytes: 88 10 00 13 BB 00 06 00
    SignalName	Value
*/
    for (int c = 0; c < frames->count(); c++)
    {
        frame = &frames->at(c);
        data = reinterpret_cast<const unsigned char *>(frame->payload().constData());
        dataLen = frame->payload().count();

        QString builderString;
        builderString += tr("Time: ") + QString::number((frame->timeStamp().microSeconds() / 1000000.0), 'f', 6);
        builderString += tr("    ID: ") + Utility::formatCANID(frame->frameId(), frame->hasExtendedFrameFormat());
        if (frame->hasExtendedFrameFormat()) builderString += tr(" Ext ");
        else builderString += tr(" Std ");
        builderString += tr("Bus: ") + QString::number(frame->bus);
        builderString += " Len: " + QString::number(dataLen) + "\n";
        outFile->write(builderString.toUtf8());

        builderString = tr("Data Bytes: ");
        for (int temp = 0; temp < dataLen; temp++)
        {
            builderString += Utility::formatNumber(data[temp]) + " ";
        }
        builderString += "\n";
        outFile->write(builderString.toUtf8());

        builderString = "";
        if (dbcHandler != nullptr)
        {
            DBC_MESSAGE *msg = dbcHandler->findMessage(*frame);
            if (msg != nullptr)
            {
                for (int j = 0; j < msg->sigHandler->getCount(); j++)
                {

                    QString temp;
                    if (msg->sigHandler->findSignalByIdx(j)->processAsText(*frame, temp))
                    {
                        builderString.append("\t" + temp);
                        builderString.append("\n");
                    }
                }
            }
            builderString.append("\n");
            outFile->write(builderString.toUtf8());
        }
    }
    outFile->close();
}

void MainWindow::toggleCapture()
{
    allowCapture = !allowCapture;
    if (allowCapture) {
        ui->btnCaptureToggle->setText("Suspend Capturing");
    } else {
        ui->btnCaptureToggle->setText("Restart Capturing");
        setAutoBookmarkNewIdsActive(false);
    }

    emit suspendCapturing(!allowCapture);
}

void MainWindow::connectionStatusUpdated(int conns)
{
    lbStatusConnected.setText(tr("Connected to ") + QString::number(conns) + tr(" buses"));
}

void MainWindow::updateFileStatus()
{
    QString output;
    if (model->rowCount() == 0)
    {
        output = tr("No packets loaded");
    }
    else
    {
        if (loadedFileName.length() > 2)
        {
            output = loadedFileName + " loaded";
        }
        else
        {
            output = tr("No file loaded");
        }

        if (bDirty)
        {
            output += " (X)";
        }
    }
    lbStatusFilename.setText(output);
}

CANFrameModel* MainWindow::getCANFrameModel()
{
    return model;
}


/*
 * All functions past this point set up the various other windows that can be opened
*/

void MainWindow::showSettingsDialog()
{
    if (!settingsDialog)
    {
        settingsDialog = new MainSettingsDialog();
        connect (settingsDialog, SIGNAL(updatedSettings()), this, SLOT(readUpdateableSettings()));
    }
    settingsDialog->show();
}

//always gets unfiltered list. You ask for the graphs so there is no need to send filtered frames
//now always creates a new window. This allows for multiple independent graphing windows
void MainWindow::showGraphingWindow()
{
/* could only allow the latest window to have these centering signals.
   if (lastGraphingWindow)
    {
        disconnect(lastGraphingWindow, SIGNAL(sendCenterTimeID(uint32_t,double)), this, SLOT(gotCenterTimeID(int32_t,double)));
        disconnect(this, SIGNAL(sendCenterTimeID(uint32_t,double)), lastGraphingWindow, SLOT(gotCenterTimeID(int32_t,double)));
        if (flowViewWindow)
        {
            disconnect(lastGraphingWindow, SIGNAL(sendCenterTimeID(uint32_t,double)), flowViewWindow, SLOT(gotCenterTimeID(int32_t,double)));
            disconnect(flowViewWindow, SIGNAL(sendCenterTimeID(uint32_t,double)), lastGraphingWindow, SLOT(gotCenterTimeID(int32_t,double)));
        }
    }
*/
    lastGraphingWindow = new GraphingWindow(model->getListReference());
    graphWindows.append(lastGraphingWindow);

    connect(lastGraphingWindow, SIGNAL(sendCenterTimeID(uint32_t,double)), this, SLOT(gotCenterTimeID(uint32_t,double)));
    connect(this, SIGNAL(sendCenterTimeID(uint32_t,double)), lastGraphingWindow, SLOT(gotCenterTimeID(uint32_t,double)));

    if (flowViewWindow) //connect the two external windows together
    {
        connect(lastGraphingWindow, SIGNAL(sendCenterTimeID(uint32_t,double)), flowViewWindow, SLOT(gotCenterTimeID(uint32_t,double)));
        connect(flowViewWindow, SIGNAL(sendCenterTimeID(uint32_t,double)), lastGraphingWindow, SLOT(gotCenterTimeID(uint32_t,double)));
    }

    lastGraphingWindow->show();
}

void MainWindow::showTemporalGraphWindow()
{
    //only create an instance of the object if we dont have one. Otherwise just display the existing one.
    if (!temporalGraphWindow)
    {
        const QVector<CANFrame> *frames;
        if (!useFiltered)
            frames = model->getListReference();
        else
            frames = model->getFilteredListReference();

        if(frames->count() > 2000)
        {
            QMessageBox::StandardButton confirmDialog;
            confirmDialog = QMessageBox::question(this, "Danger Will Robinson", "There are a lot of frames (>2000) to plot, this may take a while or crash the app. Crash likely with more than 10k frames. Continue?",
                                          QMessageBox::Yes|QMessageBox::No);
            if (confirmDialog == QMessageBox::No)
            {
                return;
            }
        }

        temporalGraphWindow = new TemporalGraphWindow(frames);
    }

    temporalGraphWindow->show();
}

void MainWindow::showFrameDataAnalysis()
{
    //only create an instance of the object if we dont have one. Otherwise just display the existing one.
    if (!frameInfoWindow)
    {
        if (!useFiltered)
            frameInfoWindow = new FrameInfoWindow(model->getListReference());
        else
            frameInfoWindow = new FrameInfoWindow(model->getFilteredListReference());
    }
    frameInfoWindow->show();
}

void MainWindow::analyzeFrameData(QString frameId)
{
    showFrameDataAnalysis();
    if (frameInfoWindow) {
        frameInfoWindow->selectID(frameId);
    }
}

FrameInfoWindow* MainWindow::getFrameInfoWindow()
{
    return frameInfoWindow;
}

SnifferWindow* MainWindow::getSnifferWindow() const
{
    return snifferWindow;
}

BisectWindow* MainWindow::getBisectWindow() const
{
    return bisectWindow;
}

FlowViewWindow* MainWindow::getFlowViewWindow() const
{
    return flowViewWindow;
}

FuzzingWindow* MainWindow::getFuzzingWindow() const
{
    return fuzzingWindow;
}

UDSScanWindow* MainWindow::getUDSScanWindow() const
{
    return udsScanWindow;
}

ISOTP_InterpreterWindow* MainWindow::getISOTPWindow() const
{
    return isoWindow;
}

FrameSenderWindow* MainWindow::getFrameSenderWindow() const
{
    return frameSenderWindow;
}

SignalViewerWindow* MainWindow::getSignalViewerWindow() const
{
    return signalViewerWindow;
}

FramePlaybackWindow* MainWindow::getPlaybackWindow() const
{
    return playbackWindow;
}

ConnectionWindow* MainWindow::getConnectionWindow() const
{
    return connectionWindow;
}

GraphingWindow* MainWindow::getGraphingWindow() const
{
    return lastGraphingWindow;
}

void MainWindow::updateCopilotStatus(int count)
{
    if (count > 0) {
        copilotStatusLabel->setText(QString("AI: %1 Connected").arg(count));
        copilotStatusLabel->setStyleSheet("QLabel { color : #409cff; font-weight: bold; }");
    } else {
        copilotStatusLabel->setText("AI: Disconnected");
        copilotStatusLabel->setStyleSheet("QLabel { color : gray; }");
    }
}

void MainWindow::showISOInterpreterWindow()
{
    if (!isoWindow)
    {
        if (!useFiltered)
            isoWindow = new ISOTP_InterpreterWindow(model->getListReference());
        else
            isoWindow = new ISOTP_InterpreterWindow(model->getFilteredListReference());
    }
    isoWindow->show();
}

void MainWindow::showSnifferWindow()
{
    if (!snifferWindow)
        snifferWindow = new SnifferWindow(this);
    snifferWindow->show();
}

void MainWindow::showBisectWindow()
{
    if (!bisectWindow)
    {
        bisectWindow = new BisectWindow(model->getListReference());
    }
    bisectWindow->show();
}

void MainWindow::showCANBridgeWindow()
{
    if (!canBridgeWindow)
    {
        canBridgeWindow = new CANBridgeWindow(model->getListReference());
    }
    canBridgeWindow->show();
}

void MainWindow::showFrameSenderWindow()
{
    if (!frameSenderWindow)
    {
        if (!useFiltered)
            frameSenderWindow = new FrameSenderWindow(model->getListReference());
        else
            frameSenderWindow = new FrameSenderWindow(model->getFilteredListReference());
    }
    frameSenderWindow->show();
}

void MainWindow::showPlaybackWindow()
{
    if (!playbackWindow)
    {
        if (!useFiltered)
            playbackWindow = new FramePlaybackWindow(model->getListReference());
        else
            playbackWindow = new FramePlaybackWindow(model->getFilteredListReference());
    }
    playbackWindow->show();
}

void MainWindow::showUDSFirmwareUploaderWindow()
{
    if (!udsFirmwareUploaderWindow)
    {
        udsFirmwareUploaderWindow = new UDSFirmwareUploaderWindow(model->getListReference());
    }
    udsFirmwareUploaderWindow->show();
}

void MainWindow::showComparisonWindow()
{
    if (!comparatorWindow)
    {
        comparatorWindow = new FileComparatorWindow();
    }
    comparatorWindow->show();
}

void MainWindow::showDBCComparisonWindow()
{
    if (!dbcComparatorWindow)
    {
        dbcComparatorWindow = new DBCComparatorWindow();
    }
    dbcComparatorWindow->show();
}

void MainWindow::showSingleMultiWindow()
{
    if (!discreteStateWindow)
    {
        discreteStateWindow = new DiscreteStateWindow(model->getListReference());
    }
    discreteStateWindow->show();
}

void MainWindow::showFuzzingWindow()
{
    if (!fuzzingWindow)
    {
        fuzzingWindow = new FuzzingWindow(model->getListReference());
    }
    fuzzingWindow->show();
}

void MainWindow::showMCConfigWindow()
{
    if (!motorctrlConfigWindow)
    {
        motorctrlConfigWindow = new MotorControllerConfigWindow(model->getListReference());
        //connect(motorctrlConfigWindow, SIGNAL(sendCANFrame(const CANFrame*,int)), worker, SLOT(sendFrame(const CANFrame*,int)));
        //connect(motorctrlConfigWindow, SIGNAL(sendFrameBatch(const QList<CANFrame>*)), worker, SLOT(sendFrameBatch(const QList<CANFrame>*)));
    }
    motorctrlConfigWindow->show();
}

void MainWindow::showUDSScanWindow()
{
    if (!udsScanWindow)
    {
        udsScanWindow = new UDSScanWindow(model->getListReference());
    }
    udsScanWindow->show();
}

void MainWindow::showScriptingWindow()
{
    if (!scriptingWindow)
    {
        scriptingWindow = new ScriptingWindow(model->getListReference());
    }
    scriptingWindow->show();
}

void MainWindow::showRangeWindow()
{
    if (!rangeWindow)
    {
        rangeWindow = new RangeStateWindow(model->getListReference());
    }
    rangeWindow->show();
}

void MainWindow::showFuzzyScopeWindow()
{
    //not done yet
}

void MainWindow::showFlowViewWindow()
{
    if (!flowViewWindow)
    {
        if (!useFiltered)
            flowViewWindow = new FlowViewWindow(model->getListReference());
        else
            flowViewWindow = new FlowViewWindow(model->getFilteredListReference());
        connect(flowViewWindow, SIGNAL(sendCenterTimeID(uint32_t,double)), this, SLOT(gotCenterTimeID(int32_t,double)));
        connect(this, SIGNAL(sendCenterTimeID(uint32_t,double)), flowViewWindow, SLOT(gotCenterTimeID(int32_t,double)));
    }

    if (lastGraphingWindow)
    {
        connect(lastGraphingWindow, SIGNAL(sendCenterTimeID(uint32_t,double)), flowViewWindow, SLOT(gotCenterTimeID(int32_t,double)));
        connect(flowViewWindow, SIGNAL(sendCenterTimeID(uint32_t,double)), lastGraphingWindow, SLOT(gotCenterTimeID(int32_t,double)));
    }

    flowViewWindow->show();
}


void MainWindow::DBCSettingsUpdated()
    {
    updateFilterList();
    model->sendRefresh();
    }

void MainWindow::showDBCFileWindow()
{
    if (!dbcFileWindow)
    {
        dbcFileWindow = new DBCLoadSaveWindow(model->getListReference());
        connect(dbcFileWindow, &DBCLoadSaveWindow::updatedDBCSettings, this, &MainWindow::DBCSettingsUpdated);
    }
    dbcFileWindow->show();
}

void MainWindow::showSignalViewer()
{
    if (!signalViewerWindow)
    {
        if (!useFiltered)
            signalViewerWindow = new SignalViewerWindow(model->getListReference());
        else
            signalViewerWindow = new SignalViewerWindow(model->getFilteredListReference());
    }
    signalViewerWindow->show();
}

void MainWindow::showConnectionSettingsWindow()
{
    if (!connectionWindow)
    {
        connectionWindow = new ConnectionWindow();
    }
    connectionWindow->show();
}

/// All AI Suggestions Below

void MainWindow::addBookmarkSmart(const QString &tag)
{
    if (allowCapture)
        addBookmarkAtTail(tag);
    else
        addBookmarkAtCurrentSelection(tag);
}

void MainWindow::addBookmarkAtCurrentSelection()
{
    addBookmarkAtCurrentSelection(quickBookmarkLabel);
}

void MainWindow::addBookmarkAtCurrentSelection(const QString &tag)
{
    if (!bookmarkManager || !model) return;

    QModelIndex sourceIndex;
    CANFrame frame;
    if (!getSelectedFrameInfo(frame, &sourceIndex)) return;

    FrameBookmark bookmark;
    bookmark.originalIndex = frame.originalIndex;
    bookmark.frameId = frame.frameId();
    bookmark.bus = frame.bus;
    bookmark.timestampMicros = frame.timeStamp().microSeconds();

    QString finalLabel = tag.trimmed();
    if (finalLabel.isEmpty()) finalLabel = tr("Bookmark");
    bookmark.label = finalLabel;
    bookmark.note.clear();

    bookmarkManager->addBookmark(bookmark);
    if (bookmarkDialog) bookmarkDialog->refreshBookmarksView();
    statusBar()->showMessage(tr("Added %1 Bookmark").arg(finalLabel), 3000);
}

void MainWindow::addBookmarkAtTail(const QString &tag)
{
    if (!bookmarkManager || !model) return;

    const QVector<CANFrame> *frames = model->getListReference();
    if (!frames || frames->isEmpty()) {
        statusBar()->showMessage(tr("No frames to bookmark"), 1500);
        return;
    }

    const CANFrame &frame = frames->last();

    FrameBookmark bm;
    bm.originalIndex = frame.originalIndex;
    bm.bus = frame.bus;
    bm.frameId = frame.frameId();
    bm.timestampMicros = frame.timeStamp().microSeconds();

    QString finalLabel = tag.trimmed();
    if (finalLabel.isEmpty()) finalLabel = tr("Bookmark");
    bm.label = finalLabel;
    bm.note.clear();

    bookmarkManager->addBookmark(bm);
    if (bookmarkDialog) bookmarkDialog->refreshBookmarksView();
    statusBar()->showMessage(tr("Bookmark added: %1").arg(finalLabel), 1500);
}

void MainWindow::jumpToBookmark(int bookmarkIndex)
{
    if (!bookmarkManager || !model) return;
    if (bookmarkIndex < 0 || bookmarkIndex >= bookmarkManager->getBookmarks().size()) return;

    const FrameBookmark bm = bookmarkManager->getBookmarks().at(bookmarkIndex);
    if (!selectFrameByOriginalIndex(bm.originalIndex))
    {
        statusBar()->showMessage(tr("Bookmarked frame could not be found or is hidden by filters"), 2500);
    }
}

bool MainWindow::selectFrameByOriginalIndex(int originalIndex)
{
    if (!model || !ui || !ui->canFramesView)
        return false;

    const int filteredRow = model->findFilteredRowByOriginalIndex(originalIndex);
    if (filteredRow < 0)
        return false;

    auto proxy = qobject_cast<QSortFilterProxyModel*>(ui->canFramesView->model());
    if (!proxy)
        return false;

    const QModelIndex sourceIndex = model->index(filteredRow, 0);
    if (!sourceIndex.isValid())
        return false;

    const QModelIndex proxyIndex = proxy->mapFromSource(sourceIndex);
    if (!proxyIndex.isValid())
        return false;

    ui->canFramesView->setCurrentIndex(proxyIndex);
    ui->canFramesView->selectRow(proxyIndex.row());
    ui->canFramesView->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
    return true;
}

void MainWindow::jumpToOriginalIndex(int originalIndex)
{
    if (originalIndex < 0)
        return;

    if (!selectFrameByOriginalIndex(originalIndex))
    {
        statusBar()->showMessage(tr("Frame could not be found or is hidden by filters"), 2500);
    }
}

void MainWindow::copyOriginalIndex()
{
    CANFrame frame;
    QModelIndex sourceIndex;
    if (!getSelectedFrameInfo(frame, &sourceIndex)) return;

    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) return;

    clipboard->setText(QString::number(frame.originalIndex +1));
}

void MainWindow::deleteBookmarkByIndex(int bookmarkIndex)
{
    Q_UNUSED(bookmarkIndex);
}

void MainWindow::showBookmarksWindow()
{
    if (!bookmarkDialog) return;

    bookmarkDialog->refreshBookmarksView();
    bookmarkDialog->show();
    bookmarkDialog->raise();
    bookmarkDialog->activateWindow();
}

void MainWindow::triggerTimedDiscoveryBookmark()
{
    triggerQuickBookmark();

    if (ui->checkBoxAutoBookmark && ui->checkBoxAutoBookmark->isChecked()) {
        armAutoBookmarkWindow(autoBookmarkDurationMs);
    }
}

void MainWindow::triggerQuickBookmark()
{
    const QString currentLabel =
        normalizedBookmarkLabel(ui->quickBookmarkLabel->text());
    const QString currentAltLabel =
        normalizedBookmarkLabel(ui->quickBookmarkAlternateLabel->text());
    const bool alternating =
        ui->checkBoxAlternateLabels && ui->checkBoxAlternateLabels->isChecked();

    QString tag;

    if (alternating)
    {
        tag = quickBookmarkAlternateState ? currentAltLabel : currentLabel;
        quickBookmarkAlternateState = !quickBookmarkAlternateState;
    }
    else
    {
        tag = currentLabel;
    }

    addBookmarkSmart(tag);
    statusBar()->showMessage(tr("Bookmark added: %1").arg(tag), 2000);
}

void MainWindow::resetQuickBookmarkToggle()
{
    quickBookmarkAlternateState = false;
    statusBar()->showMessage(
        tr("Quick bookmark toggle reset to %1")
            .arg(normalizedBookmarkLabel(quickBookmarkLabel)),
        1500);
}

void MainWindow::armAutoBookmarkWindow(int durationMs)
{
    autoBookmarkSeenIds.clear();
    autoBookmarkKnownIds.clear();

    if (model) {
        const QVector<CANFrame> *frames = model->getListReference();
        if (frames) {
            for (const CANFrame &frame : *frames) {
                autoBookmarkKnownIds.insert(makeAutoBookmarkKey(frame));
            }
        }
    }

    autoBookmarkNewIdsActive = true;
    autoBookmarkTimer->start(durationMs);

    statusBar()->showMessage(
        tr("Discovering new IDs for %1 ms").arg(durationMs), 1000);
}

void MainWindow::autoBookmarkTimeoutExpired()
{
    setAutoBookmarkNewIdsActive(false);
}

void MainWindow::setAutoBookmarkNewIdsActive(bool enabled)
{
    autoBookmarkNewIdsActive = enabled;

    if (enabled)
    {
        statusBar()->showMessage(tr("Discover New IDs armed"), 1500);
        return;
    }

    autoBookmarkTimer->stop();
    autoBookmarkSeenIds.clear();
    statusBar()->showMessage(tr("Discover New IDs disarmed"), 1500);
}

quint64 MainWindow::makeAutoBookmarkKey(const CANFrame &frame) const
{
    const quint64 busPart = static_cast<quint64>(static_cast<quint32>(frame.bus)) << 32;
    const quint64 extPart = frame.hasExtendedFrameFormat() ? (1ULL << 31) : 0ULL;
    const quint64 idPart = static_cast<quint64>(frame.frameId() & 0x1FFFFFFFU);
    return busPart | extPart | idPart;
}

bool MainWindow::findLatestFrameByBusIdAndFormat(int bus, uint32_t frameId, bool extended, CANFrame &outFrame) const
{
    if (!model) return false;

    const QVector<CANFrame> *frames = model->getListReference();
    if (!frames || frames->isEmpty()) return false;

    for (int i = frames->size() - 1; i >= 0; --i)
    {
        const CANFrame &frame = frames->at(i);
        if (frame.bus == bus &&
            frame.frameId() == frameId &&
            frame.hasExtendedFrameFormat() == extended)
        {
            outFrame = frame;
            return true;
        }
    }

    return false;
}

void MainWindow::processAutoBookmarks(const QVector<CANFrame> &frames)
{
    if (!allowCapture || !autoBookmarkNewIdsActive || !bookmarkManager) return;

    bool addedAny = false;

    for (const CANFrame &incomingFrame : frames)
    {
        if (incomingFrame.frameType() != QCanBusFrame::DataFrame) continue;

        const quint64 key = makeAutoBookmarkKey(incomingFrame);
        if (autoBookmarkKnownIds.contains(key)) continue;

        autoBookmarkKnownIds.insert(key);

        CANFrame storedFrame;
        if (!findLatestFrameByBusIdAndFormat(
                incomingFrame.bus,
                incomingFrame.frameId(),
                incomingFrame.hasExtendedFrameFormat(),
                storedFrame))
        {
            continue;
        }

        FrameBookmark bm;
        bm.originalIndex = storedFrame.originalIndex;
        bm.bus = storedFrame.bus;
        bm.frameId = storedFrame.frameId();
        bm.timestampMicros =
                static_cast<qint64>(storedFrame.timeStamp().seconds()) * 1000000LL +
                static_cast<qint64>(storedFrame.timeStamp().microSeconds());
        bm.label = tr("NEW ID 0x%1 BUS %2")
                .arg(storedFrame.frameId(), 0, 16)
                .arg(storedFrame.bus);
        bm.note.clear();

        bookmarkManager->addBookmark(bm);
        addedAny = true;
    }

    if (addedAny)
    {
        if (bookmarkDialog)
            bookmarkDialog->refreshBookmarksView();

        statusBar()->showMessage(tr("Discovered %1 new CAN IDs"), 2000);
    }
}

BookmarkEventAnalyzer::FrameKey BookmarkEventAnalyzer::makeFrameKey(const CANFrame &frame) const
{
    FrameKey key;
    key.bus = frame.bus;
    key.frameId = frame.frameId();
    key.extended = frame.hasExtendedFrameFormat();
    return key;
}

void BookmarkEventAnalyzer::accumulateCrossIdEventFrame(
    CrossIdEventStats &stats,
    const CANFrame &frame,
    bool isBeforeSide) const
{
    const int distance = qAbs(frame.originalIndex - stats.anchorOriginalIndex);
    if (distance < stats.nearestDistance)
    {
        stats.nearestDistance = distance;
        stats.nearestOriginalIndex = frame.originalIndex;
    }

    const QByteArray payload = frame.payload();

    if (isBeforeSide)
    {
        stats.beforeCount++;
        if (stats.hasLastBefore && stats.lastBeforePayload != payload)
            stats.beforePayloadTransitions++;
        stats.lastBeforePayload = payload;
        stats.hasLastBefore = true;
    }
    else
    {
        stats.afterCount++;
        if (stats.hasLastAfter && stats.lastAfterPayload != payload)
            stats.afterPayloadTransitions++;
        stats.lastAfterPayload = payload;
        stats.hasLastAfter = true;
    }
}

void MainWindow::analyzeCurrentBookmarkOrSelection()
{
    CANFrame frame;
    QModelIndex sourceIndex;
    if (!getSelectedFrameInfo(frame, &sourceIndex)) {
        statusBar()->showMessage(tr("Select a frame first"), 2000);
        return;
    }

    const int originalIndex = frame.originalIndex;
    if (originalIndex < 0) {
        statusBar()->showMessage(tr("Invalid original frame index"), 2000);
        return;
    }

    QSettings settings;
    int sameIdRadius = settings.value("Analysis/SameIdRadius", 5).toInt();
    int crossIdBefore = settings.value("Analysis/CrossIdWindowBefore", 300).toInt();
    int crossIdAfter = settings.value("Analysis/CrossIdWindowAfter", 300).toInt();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Event Analysis Settings"));
    dlg.setModal(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);

    QLabel *intro = new QLabel(
        tr("Choose how much context to inspect around the selected event."), &dlg);
    intro->setWordWrap(true);
    mainLayout->addWidget(intro);

    QFormLayout *form = new QFormLayout();

    QSpinBox *sameIdSpin = new QSpinBox(&dlg);
    sameIdSpin->setRange(1, 5000);
    sameIdSpin->setValue(sameIdRadius);
    sameIdSpin->setToolTip(tr("How many matching frames of the same ID to inspect before and after the event."));

    QSpinBox *crossBeforeSpin = new QSpinBox(&dlg);
    crossBeforeSpin->setRange(1, 50000);
    crossBeforeSpin->setValue(crossIdBefore);
    crossBeforeSpin->setToolTip(tr("How many nearby raw frames to inspect before the event for other IDs."));

    QSpinBox *crossAfterSpin = new QSpinBox(&dlg);
    crossAfterSpin->setRange(1, 50000);
    crossAfterSpin->setValue(crossIdAfter);
    crossAfterSpin->setToolTip(tr("How many nearby raw frames to inspect after the event for other IDs."));

    form->addRow(tr("Same-ID radius:"), sameIdSpin);
    form->addRow(tr("Cross-ID frames before:"), crossBeforeSpin);
    form->addRow(tr("Cross-ID frames after:"), crossAfterSpin);

    mainLayout->addLayout(form);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    mainLayout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;

    sameIdRadius = sameIdSpin->value();
    crossIdBefore = crossBeforeSpin->value();
    crossIdAfter = crossAfterSpin->value();

    settings.setValue("Analysis/SameIdRadius", sameIdRadius);
    settings.setValue("Analysis/CrossIdWindowBefore", crossIdBefore);
    settings.setValue("Analysis/CrossIdWindowAfter", crossIdAfter);

    const BookmarkEventAnalyzer::BookmarkAnalysisResult result =
        analyzeBookmarkEvent(originalIndex, sameIdRadius, crossIdBefore, crossIdAfter);

    if (result.sameIdCandidates.isEmpty() && result.crossIdCandidates.isEmpty()) {
        statusBar()->showMessage(tr("No analysis results found around this frame"), 2500);
        clearEmbeddedEventCorrelation();
        return;
    }

    refreshEmbeddedEventCorrelation(result);
}

BookmarkEventAnalyzer::BookmarkAnalysisResult MainWindow::analyzeBookmarkEvent(
        int originalIndex,
        int sameIdRadius,
        int crossIdWindowBefore,
        int crossIdWindowAfter) const
{
    BookmarkEventAnalyzer::BookmarkAnalysisResult result;
    if (!model || !bookmarkEventAnalyzer) return result;

    const QVector<CANFrame> *frames = model->getListReference();
    if (!frames) return result;

    return bookmarkEventAnalyzer->analyze(*frames,
                                          originalIndex,
                                          sameIdRadius,
                                          crossIdWindowBefore,
                                          crossIdWindowAfter);
}

uint qHash(const BookmarkEventAnalyzer::FrameKey &key, uint seed) noexcept
{
    seed = ::qHash(key.frameId, seed);
    seed = ::qHash(key.bus, seed);
    seed = ::qHash(key.extended, seed);
    return seed;
}

BookmarkEventAnalyzer::BookmarkAnalysisResult BookmarkEventAnalyzer::analyze(
        const QVector<CANFrame> &frames,
        int originalIndex,
        int sameIdRadius,
        int crossIdWindowBefore,
        int crossIdWindowAfter) const
{
    BookmarkAnalysisResult result;

    if (originalIndex < 0 || originalIndex >= frames.size())
        return result;

    result.anchorFrame = frames[originalIndex];
    result.originalIndex = originalIndex;
    result.sameIdRadius = sameIdRadius;
    result.crossIdWindowBefore = crossIdWindowBefore;
    result.crossIdWindowAfter = crossIdWindowAfter;

    result.sameIdCandidates = analyzeSameIdAroundBookmark(frames, originalIndex, sameIdRadius);
    result.crossIdCandidates = analyzeCrossIdAroundBookmark(
            frames, originalIndex, crossIdWindowBefore, crossIdWindowAfter);

    return result;
}

QVector<BookmarkEventAnalyzer::CrossIdCandidate> BookmarkEventAnalyzer::analyzeCrossIdAroundBookmark(
    const QVector<CANFrame> &frames,
    int originalIndex,
    int windowBefore,
    int windowAfter) const
{
    QVector<CrossIdCandidate> out;
    if (originalIndex < 0 || originalIndex >= frames.size()) return out;

    const CANFrame anchor = frames.at(originalIndex);
    if (anchor.frameType() != QCanBusFrame::DataFrame) return out;

    const FrameKey anchorKey = makeFrameKey(anchor);
    QHash<FrameKey, CrossIdEventStats> eventStats;

    const int start = qMax(0, originalIndex - windowBefore);
    const int end = qMin(frames.size() - 1, originalIndex + windowAfter);

    for (int i = start; i <= end; i++)
    {
        if (i == originalIndex) continue;

        const CANFrame frame = frames.at(i);
        if (frame.frameType() != QCanBusFrame::DataFrame) continue;

        const FrameKey key = makeFrameKey(frame);
        if (key == anchorKey) continue;

        CrossIdEventStats &stats = eventStats[key];
        stats.key = key;
        stats.anchorOriginalIndex = originalIndex;
        accumulateCrossIdEventFrame(stats, frame, i < originalIndex);
    }

    for (auto it = eventStats.constBegin(); it != eventStats.constEnd(); ++it)
    {
        const FrameKey key = it.key();
        const CrossIdEventStats &stats = it.value();

        const FrameIdleStats *idleStats = nullptr;
        auto idleIt = idleBaseline.constFind(key);
        if (idleIt != idleBaseline.constEnd())
            idleStats = &idleIt.value();

        const CrossIdScoreFeatures features =
            buildCrossIdScoreFeatures(stats, idleStats, windowBefore, windowAfter);

        const int payloadChangeCount =
            stats.beforePayloadTransitions + stats.afterPayloadTransitions;

        const bool appearedOnlyAfter = (features.exclusiveAfter > 0.5);
        const bool disappearedAfter = (features.exclusiveBefore > 0.5);
        const bool countChanged = (stats.beforeCount != stats.afterCount);
        const bool hasAppearanceShift = (features.appearanceShift > 0.01);
        const bool hasPayloadChange = (payloadChangeCount > 0);
        const bool hasMeaningfulScore = (scoreCrossIdCandidate(features) > 0.15);

        // Minimal filter only: remove obvious no-op IDs that look the same
        // on both sides of the bookmark window and contribute essentially nothing.
        if (!appearedOnlyAfter &&
            !disappearedAfter &&
            !countChanged &&
            !hasAppearanceShift &&
            !hasPayloadChange &&
            !hasMeaningfulScore)
        {
            continue;
        }

        CrossIdCandidate c;
        c.key = key;
        c.beforeCount = stats.beforeCount;
        c.afterCount = stats.afterCount;
        c.totalEventCount = stats.beforeCount + stats.afterCount;
        c.payloadChangeCount = payloadChangeCount;

        c.appearanceShift = features.appearanceShift;
        c.payloadVolatility = features.payloadVolatility;
        c.idleStability = features.idleStability;
        c.idleNoise = qBound(0.0, 1.0 - features.idleStability, 1.0);
        c.appearedOnlyAfter = appearedOnlyAfter;
        c.disappearedAfter = disappearedAfter;
        c.score = scoreCrossIdCandidate(features);
        c.nearestOriginalIndex = stats.nearestOriginalIndex;
        c.nearestDistance = stats.nearestDistance;

        out.append(c);
    }

    std::sort(out.begin(), out.end(), [](const CrossIdCandidate &a, const CrossIdCandidate &b) {
        if (a.score != b.score) return a.score > b.score;

        if (a.appearedOnlyAfter != b.appearedOnlyAfter)
            return a.appearedOnlyAfter && !b.appearedOnlyAfter;

        if (a.disappearedAfter != b.disappearedAfter)
            return a.disappearedAfter && !b.disappearedAfter;

        if (a.payloadChangeCount != b.payloadChangeCount)
            return a.payloadChangeCount > b.payloadChangeCount;

        if (a.appearanceShift != b.appearanceShift)
            return a.appearanceShift > b.appearanceShift;

        if (a.afterCount != b.afterCount)
            return a.afterCount > b.afterCount;

        if (a.totalEventCount != b.totalEventCount)
            return a.totalEventCount > b.totalEventCount;

        if (a.key.bus != b.key.bus) return a.key.bus < b.key.bus;
        return a.key.frameId < b.key.frameId;
    });

    return out;
}

void MainWindow::analyzeControlStatesForLoadedLog()
{
    if (!controlStateDetector || !controlCandidateModel || !model)
        return;

    const QVector<CANFrame> *frames = useFiltered
        ? model->getFilteredListReference()
        : model->getListReference();

    if (!frames || frames->isEmpty()) {
        statusBar()->showMessage(tr("No frames loaded"), 2000);
        return;
    }

    controlCandidateModel->setCandidates(controlStateDetector->analyzeAll(*frames));
    refreshEmbeddedControlAnalysis();

    if (ui->tabAnalysis && ui->tabControlStates)
        ui->tabAnalysis->setCurrentWidget(ui->tabControlStates);

    statusBar()->showMessage(
        tr("Loaded %1 control-state candidates").arg(controlCandidateModel->rowCount()),
        2500);
}

void MainWindow::analyzeControlStatesForSelectedFrame()
{
    if (!controlStateDetector || !controlCandidateModel || !model || !ui || !ui->canFramesView) {
        return;
    }

    const QModelIndex currentIndex = ui->canFramesView->currentIndex();
    if (!currentIndex.isValid()) {
        statusBar()->showMessage(tr("Select a frame first"), 2000);
        return;
    }

    QAbstractItemModel *viewModel = ui->canFramesView->model();
    if (!viewModel) {
        statusBar()->showMessage(tr("No frame model available"), 2000);
        return;
    }

    const int frameIdCol = static_cast<int>(Column::FrameId);
    const int busCol = static_cast<int>(Column::Bus);
    const int extendedCol = static_cast<int>(Column::Extended);

    const QModelIndex frameIdIndex = currentIndex.sibling(currentIndex.row(), frameIdCol);
    if (!frameIdIndex.isValid()) {
        statusBar()->showMessage(tr("Could not read Frame ID from selected row"), 2000);
        return;
    }

    bool ok = false;
    QString frameIdText = viewModel->data(frameIdIndex, Qt::DisplayRole).toString().trimmed();
    frameIdText.remove(QLatin1Char(' '));
    if (frameIdText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        frameIdText.remove(0, 2);
    }

    const uint32_t frameId = frameIdText.toUInt(&ok, 16);
    if (!ok) {
        statusBar()->showMessage(
            tr("Selected row does not contain a valid hexadecimal Frame ID"),
            2500);
        return;
    }

    uint32_t bus = 0;
    const QModelIndex busIndex = currentIndex.sibling(currentIndex.row(), busCol);
    if (busIndex.isValid()) {
        bool busOk = false;
        const QString busText = viewModel->data(busIndex, Qt::DisplayRole).toString().trimmed();
        const uint32_t parsedBus = busText.toUInt(&busOk, 10);
        if (busOk) {
            bus = parsedBus;
        }
    }

    bool extended = false;
    const QModelIndex extendedIndex = currentIndex.sibling(currentIndex.row(), extendedCol);
    if (extendedIndex.isValid()) {
        const QVariant extValue = viewModel->data(extendedIndex, Qt::DisplayRole);
        if (extValue.userType() == QMetaType::Bool) {
            extended = extValue.toBool();
        } else {
            const QString extText = extValue.toString().trimmed().toLower();
            extended = (extText == QStringLiteral("1") ||
                        extText == QStringLiteral("true") ||
                        extText == QStringLiteral("yes") ||
                        extText == QStringLiteral("ext") ||
                        extText == QStringLiteral("extended"));
        }
    }

    const QVector<CANFrame> *frames = useFiltered
        ? model->getFilteredListReference()
        : model->getListReference();

    if (!frames || frames->isEmpty()) {
        statusBar()->showMessage(tr("No frames loaded"), 2000);
        return;
    }

    ControlStateKey selectedKey;
    selectedKey.bus = bus;
    selectedKey.frameId = frameId;
    selectedKey.extended = extended;

    const QVector<ControlCandidate> candidates =
        controlStateDetector->analyzeId(*frames, selectedKey);

    controlCandidateModel->setCandidates(candidates);
    refreshEmbeddedControlAnalysis();

    if (ui->tabAnalysis && ui->tabControlStates) {
        ui->tabAnalysis->setCurrentWidget(ui->tabControlStates);
    }

    statusBar()->showMessage(
        tr("Loaded %1 control-state candidate(s) for ID 0x%2 from the current %3 list")
            .arg(controlCandidateModel->rowCount())
            .arg(QString::number(selectedKey.frameId, 16).toUpper())
            .arg(useFiltered ? tr("filtered") : tr("loaded")),
        4000);
}

void MainWindow::jumpToControlCandidate(int candidateIndex)
{
    if (!controlCandidateModel)
        return;

    if (candidateIndex < 0 || candidateIndex >= controlCandidateModel->rowCount())
        return;

    const ControlCandidate c = controlCandidateModel->candidateAt(candidateIndex);
    if (c.exampleOriginalIndexes.isEmpty())
        return;

    jumpToOriginalIndex(c.exampleOriginalIndexes.first());
}

void MainWindow::bookmarkControlCandidate(int candidateIndex)
{
    if (!controlCandidateModel || !bookmarkManager || !model)
        return;

    if (candidateIndex < 0 || candidateIndex >= controlCandidateModel->rowCount())
        return;

    const ControlCandidate c = controlCandidateModel->candidateAt(candidateIndex);
    if (c.exampleOriginalIndexes.isEmpty())
        return;

    const int originalIndex = c.exampleOriginalIndexes.first();

    const QVector<CANFrame> *frames = model->getListReference();
    if (!frames || originalIndex < 0 || originalIndex >= frames->size())
        return;

    const CANFrame &frame = frames->at(originalIndex);

    FrameBookmark bm;
    bm.originalIndex = originalIndex;
    bm.timestampMicros = static_cast<quint64>(frame.timeStamp().seconds()) * 1000000ULL
                       + static_cast<quint64>(frame.timeStamp().microSeconds());
    bm.frameId = frame.frameId();
    bm.bus = frame.bus;
    bm.label = QStringLiteral("Control Candidate 0x%1")
                   .arg(frame.frameId(), 0, 16)
                   .toUpper();
    bm.note = c.patternType + QStringLiteral(": ") + c.reason;

    bookmarkManager->addBookmark(bm);
    if (bookmarkDialog)
        bookmarkDialog->refreshBookmarksView();
}

void MainWindow::showControlAnalysisWindow()
{
    if (!controlAnalysisDialog)
        return;

    controlAnalysisDialog->refreshCandidates();
    controlAnalysisDialog->show();
    controlAnalysisDialog->raise();
    controlAnalysisDialog->activateWindow();
}

QString MainWindow::describeFlipStrength(double score) const
{
    if (score >= 9.0) return tr("very likely related");
    if (score >= 7.0) return tr("likely related");
    if (score >= 5.0) return tr("possibly related");
    if (score >= 3.5) return tr("weak match");
    return tr("unlikely / noisy");
}

QString MainWindow::describeIdleNoise(double idleNoise) const
{
    if (idleNoise <= 0.02) return tr("usually stable");
    if (idleNoise <= 0.10) return tr("changes a little during normal driving");
    return tr("changes often during normal driving");
}

QString BookmarkEventAnalyzer::describeSameIdReason(const FlipCandidate &c) const
{
    QStringList reasons;

    if (c.supportScore >= 0.80)
        reasons << tr("strong support on both sides of event");
    else if (c.supportScore >= 0.45)
        reasons << tr("moderate support near event");
    else
        reasons << tr("limited support near event");

    if (c.localStability >= 0.85)
        reasons << tr("stable before/after values");
    else if (c.localStability >= 0.55)
        reasons << tr("some local variation");
    else
        reasons << tr("noisy near event");

    if (c.eventFlipCount <= 1)
        reasons << tr("clean transition");
    else if (c.eventFlipCount <= 3)
        reasons << tr("a few nearby flips");
    else
        reasons << tr("many nearby flips");

    return reasons.join(tr(", "));
}

QString BookmarkEventAnalyzer::describeCrossIdReason(const CrossIdCandidate &c) const
{
    QStringList reasons;

    if (c.appearedOnlyAfter)
        reasons << tr("appears only after event");
    else if (c.disappearedAfter)
        reasons << tr("present before but missing after");
    else if (c.appearanceShift >= 0.60)
        reasons << tr("strong increase after event");
    else if (c.appearanceShift >= 0.25)
        reasons << tr("moderate increase after event");
    else if (c.appearanceShift >= 0.08)
        reasons << tr("slight increase after event");
    else
        reasons << tr("activity persists on both sides of event");

    if (c.payloadVolatility >= 0.75)
        reasons << tr("payload changes heavily");
    else if (c.payloadVolatility >= 0.35)
        reasons << tr("payload changes near event");
    else if (c.payloadChangeCount > 0)
        reasons << tr("at least one payload change seen");
    else
        reasons << tr("payload mostly stable");

    if (c.totalEventCount <= 2)
        reasons << tr("seen only a few times near event");
    else if (c.totalEventCount >= 10)
        reasons << tr("very active in event window");
    else if (c.totalEventCount >= 5)
        reasons << tr("repeats several times near event");

    if (!c.appearedOnlyAfter &&
        !c.disappearedAfter &&
        c.payloadChangeCount == 0 &&
        c.appearanceShift < 0.08)
    {
        reasons << tr("kept as a low-contrast nearby candidate");
    }

    return reasons.join(tr(", "));
}

QVector<BookmarkEventAnalyzer::FlipCandidate> BookmarkEventAnalyzer::analyzeSameIdAroundBookmark(
    const QVector<CANFrame> &frames,
    int originalIndex,
    int sameIdRadius) const
{
    QVector<FlipCandidate> empty;
    if (originalIndex < 0 || originalIndex >= frames.size()) return empty;

    const CANFrame anchor = frames.at(originalIndex);
    if (anchor.frameType() != QCanBusFrame::DataFrame) return empty;

    const FrameKey anchorKey = makeFrameKey(anchor);
    QHash<FrameKey, EventFrameStats> eventStats;

    int beforeFound = 0;
    for (int i = originalIndex - 1; i >= 0 && beforeFound < sameIdRadius; --i)
    {
        const CANFrame frame = frames.at(i);
        if (frame.frameType() != QCanBusFrame::DataFrame) continue;
        if (!(makeFrameKey(frame) == anchorKey)) continue;

        EventFrameStats &stats = eventStats[anchorKey];
        stats.key = anchorKey;
        accumulateEventFrame(stats, frame, true, originalIndex);
        beforeFound++;
    }

    int afterFound = 0;
    for (int i = originalIndex + 1; i < frames.size() && afterFound < sameIdRadius; ++i)
    {
        const CANFrame frame = frames.at(i);
        if (frame.frameType() != QCanBusFrame::DataFrame) continue;
        if (!(makeFrameKey(frame) == anchorKey)) continue;

        EventFrameStats &stats = eventStats[anchorKey];
        stats.key = anchorKey;
        accumulateEventFrame(stats, frame, false, originalIndex);
        afterFound++;
    }

    return rankFlipCandidates(eventStats, sameIdRadius);
}

void BookmarkEventAnalyzer::accumulateEventFrame(EventFrameStats &stats,
                                      const CANFrame &frame,
                                      bool isBeforeSide,
                                      int anchorOriginalIndex) const
{
    const int distance = qAbs(frame.originalIndex - anchorOriginalIndex);
    if (distance < stats.nearestDistance)
    {
        stats.nearestDistance = distance;
        stats.nearestOriginalIndex = frame.originalIndex;
    }

    const int dlc = qMin(frame.payload().size(), 64);
    if (isBeforeSide) stats.matchedFramesBefore++;
    else stats.matchedFramesAfter++;

    for (int i = 0; i < dlc; i++)
    {
        EventByteStats &b = stats.bytes[i];
        const quint8 value = static_cast<quint8>(frame.payload().at(i));

        if (isBeforeSide)
        {
            if (!b.hasBeforeValue)
            {
                b.beforeValue = value;
                b.hasBeforeValue = true;
            }

            if (b.hasLastBeforeSeen && b.lastBeforeSeen != value)
                b.beforeTransitions++;

            b.lastBeforeSeen = value;
            b.hasLastBeforeSeen = true;
            b.beforeCount++;
        }
        else
        {
            if (!b.hasAfterValue)
            {
                b.afterValue = value;
                b.hasAfterValue = true;
            }

            if (b.hasLastAfterSeen && b.lastAfterSeen != value)
                b.afterTransitions++;

            b.lastAfterSeen = value;
            b.hasLastAfterSeen = true;
            b.afterCount++;
        }
    }
}

QVector<BookmarkEventAnalyzer::FlipCandidate> BookmarkEventAnalyzer::rankFlipCandidates(
    const QHash<FrameKey, EventFrameStats> &eventStats,
    int sameIdRadius) const
{
    QVector<FlipCandidate> out;

    for (auto it = eventStats.constBegin(); it != eventStats.constEnd(); ++it)
    {
        const FrameKey &key = it.key();
        const EventFrameStats &stats = it.value();

        const FrameIdleStats *idleStats = nullptr;
        auto idleIt = idleBaseline.constFind(key);
        if (idleIt != idleBaseline.constEnd())
            idleStats = &idleIt.value();

        for (int byteIdx = 0; byteIdx < 64; ++byteIdx)
        {
            const EventByteStats &eb = stats.bytes[byteIdx];
            if (!eb.hasBeforeValue || !eb.hasAfterValue)
                continue;

            if (eb.beforeValue == eb.afterValue)
                continue;

            const ByteIdleStats *idleByteStats = nullptr;
            if (idleStats && byteIdx < idleStats->maxDlcSeen)
                idleByteStats = &idleStats->bytes[byteIdx];

            const SameIdScoreFeatures features =
                    buildSameIdScoreFeatures(eb, idleByteStats, sameIdRadius);

            if (features.eventDelta <= 0.0)
                continue;

            const double score = scoreSameIdCandidate(features);

            double rawIdleNoise = 0.0;
            if (idleByteStats && idleByteStats->samples > 1)
            {
                rawIdleNoise = clamp01(
                    safeRatio(static_cast<double>(idleByteStats->changes),
                              static_cast<double>(qMax(1, idleByteStats->samples - 1))));
            }

            FlipCandidate c;
            c.key = key;
            c.byteIndex = byteIdx;
            c.beforeValue = eb.beforeValue;
            c.afterValue = eb.afterValue;
            c.beforeCount = eb.beforeCount;
            c.afterCount = eb.afterCount;
            c.eventFlipCount = eb.beforeTransitions + eb.afterTransitions;
            c.nearestOriginalIndex = stats.nearestOriginalIndex;
            c.nearestDistance = stats.nearestDistance;
            c.supportScore = features.supportScore;
            c.localStability = features.localStability;
            c.localNoise = qBound(0.0, 1.0 - features.localStability, 1.0);
            c.idleStability = features.idleStability;
            c.idleNoise = rawIdleNoise;
            c.score = score;

            out.append(c);
        }
    }

    std::sort(out.begin(), out.end(), [](const FlipCandidate &a, const FlipCandidate &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.idleNoise != b.idleNoise) return a.idleNoise < b.idleNoise;
        if (a.key.bus != b.key.bus) return a.key.bus < b.key.bus;
        if (a.key.frameId != b.key.frameId) return a.key.frameId < b.key.frameId;
        return a.byteIndex < b.byteIndex;
    });

    return out;
}

BookmarkEventAnalyzer::SameIdScoreFeatures BookmarkEventAnalyzer::buildSameIdScoreFeatures(
        const EventByteStats &eb,
        const ByteIdleStats *idleByteStats,
        int sameIdRadius) const
{
    SameIdScoreFeatures f;
    f.eventDelta = (eb.hasBeforeValue && eb.hasAfterValue && eb.beforeValue != eb.afterValue) ? 1.0 : 0.0;
    f.supportScore = computeSameIdSupportScore(eb, sameIdRadius);
    f.localStability = computeSameIdLocalStability(eb);

    // Idle is optional. If no baseline exists, do not reward or penalize.
    f.idleStability = (idleByteStats && idleByteStats->samples > 1)
            ? computeSameIdIdleStability(idleByteStats)
            : 0.0;

    return f;
}

double BookmarkEventAnalyzer::clamp01(double value)
{
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

double BookmarkEventAnalyzer::safeRatio(double num, double denom)
{
    if (denom <= 0.0) return 0.0;
    return num / denom;
}

double BookmarkEventAnalyzer::computeSameIdSupportScore(const EventByteStats &eb, int sameIdRadius) const
{
    const double support = static_cast<double>(qMin(eb.beforeCount, eb.afterCount));
    return clamp01(safeRatio(support, static_cast<double>(qMax(1, sameIdRadius))));
}

double BookmarkEventAnalyzer::computeSameIdLocalStability(const EventByteStats &eb) const
{
    const int transitionCount = eb.beforeTransitions + eb.afterTransitions;
    const int opportunityCount =
        qMax(0, eb.beforeCount - 1) +
        qMax(0, eb.afterCount - 1);

    const double localNoise = clamp01(safeRatio(static_cast<double>(transitionCount),
                                                static_cast<double>(qMax(1, opportunityCount))));
    return 1.0 - localNoise;
}

double BookmarkEventAnalyzer::computeSameIdIdleStability(const ByteIdleStats *idleByteStats) const
{
    if (!idleByteStats) return 0.0;
    if (idleByteStats->samples <= 1) return 0.0;

    const double idleNoise = clamp01(
        safeRatio(static_cast<double>(idleByteStats->changes),
                  static_cast<double>(idleByteStats->samples - 1)));
    return 1.0 - idleNoise;
}

double BookmarkEventAnalyzer::scoreSameIdCandidate(const SameIdScoreFeatures &f) const
{
    const double raw =
            (3.5 * f.eventDelta) +
            (2.0 * f.supportScore) +
            (3.0 * f.localStability) +
            (0.0 * f.idleStability);

    return raw * (0.5 + 0.5 * clamp01(f.windowConfidence));
}


BookmarkEventAnalyzer::CrossIdScoreFeatures BookmarkEventAnalyzer::buildCrossIdScoreFeatures(
        const CrossIdEventStats &stats,
        const FrameIdleStats *idleStats,
        int windowBefore,
        int windowAfter) const
{
    CrossIdScoreFeatures f;
    f.appearanceShift = computeCrossIdAppearanceShift(stats, windowBefore, windowAfter);
    f.exclusiveAfter = (stats.beforeCount == 0 && stats.afterCount > 0) ? 1.0 : 0.0;
    f.exclusiveBefore = (stats.beforeCount > 0 && stats.afterCount == 0) ? 1.0 : 0.0;
    f.payloadVolatility = computeCrossIdPayloadVolatility(stats);

    // Idle is optional. If no baseline exists, it contributes nothing.
    f.idleStability = (idleStats != nullptr) ? computeCrossIdIdleStability(idleStats) : 0.0;
    return f;
}

double BookmarkEventAnalyzer::scoreCrossIdCandidate(const CrossIdScoreFeatures &f) const
{
    return (3.0 * f.appearanceShift) +
           (0.9 * f.exclusiveAfter) +
           (0.3 * f.exclusiveBefore) +
           (2.0 * f.payloadVolatility) +
           (0.0 * f.idleStability);
}

double BookmarkEventAnalyzer::computeCrossIdAppearanceShift(const CrossIdEventStats &stats,
                                                 int windowBefore,
                                                 int windowAfter) const
{
    const double beforeRate = static_cast<double>(stats.beforeCount) / static_cast<double>(qMax(1, windowBefore));
    const double afterRate = static_cast<double>(stats.afterCount) / static_cast<double>(qMax(1, windowAfter));
    return clamp01(afterRate - beforeRate);
}

double BookmarkEventAnalyzer::computeCrossIdPayloadVolatility(const CrossIdEventStats &stats) const
{
    const int payloadChangeCount = stats.beforePayloadTransitions + stats.afterPayloadTransitions;
    const int opportunities = qMax(1, (stats.beforeCount + stats.afterCount) - 1);
    return clamp01(static_cast<double>(payloadChangeCount) / static_cast<double>(opportunities));
}

double BookmarkEventAnalyzer::computeCrossIdIdleStability(const FrameIdleStats *idleStats) const
{
    if (!idleStats) return 0.0;

    int byteChanges = 0;
    int byteSamples = 0;
    for (int i = 0; i < 64; i++)
    {
        const ByteIdleStats &ib = idleStats->bytes[i];
        byteChanges += ib.changes;
        byteSamples += qMax(0, ib.samples - 1);
    }

    if (byteSamples <= 0) return 0.0;

    const double idleNoise = static_cast<double>(byteChanges) / static_cast<double>(byteSamples);
    return qBound(0.0, 1.0 - idleNoise, 1.0);
}


void MainWindow::clearInspectDock()
{
    ui->lblInspectContext->setText(tr("No frame selected"));
    ui->lblInspectChanged->setText(tr("Changed: -"));
    ui->txtInspectNeighborhood->clear();
    ui->txtInspectBits->clear();
}

void MainWindow::populateInspectDock(const QModelIndex &sourceIndex)
{
    if (!model || !sourceIndex.isValid()) return;

    const CANFrame *selectedFrame = model->getFilteredFrameRef(sourceIndex.row());
    if (!selectedFrame) return;

    QString context = QString("ID 0x%1  Bus %2")
                          .arg(selectedFrame->frameId(),
                               selectedFrame->hasExtendedFrameFormat() ? 8 : 3,
                               16,
                               QLatin1Char('0'))
                          .arg(selectedFrame->bus);

    if (selectedFrame->originalIndex >= 0)
        context += tr("  Original IDX %1").arg(selectedFrame->originalIndex + 1);

    ui->lblInspectContext->setText(context.toUpper());
    ui->txtInspectNeighborhood->setPlainText(formatNeighborhoodText(sourceIndex, 2));
    ui->txtInspectBits->setPlainText(formatPayloadBits(*selectedFrame));

    CANFrame prevFrame;
    if (findPreviousFrameWithSameId(sourceIndex, prevFrame))
        ui->lblInspectChanged->setText(formatChangedSummary(*selectedFrame, prevFrame));
    else
        ui->lblInspectChanged->setText(tr("Changed: first occurrence"));
}

void MainWindow::updateInspectDock(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);

    if (!current.isValid())
    {
        clearInspectDock();
        return;
    }

    auto *proxy = qobject_cast<QSortFilterProxyModel *>(ui->canFramesView->model());
    if (!proxy)
    {
        clearInspectDock();
        return;
    }

    const QModelIndex sourceIndex = proxy->mapToSource(current);
    if (!sourceIndex.isValid())
    {
        clearInspectDock();
        return;
    }

    populateInspectDock(sourceIndex);
}

QString MainWindow::formatPayloadHex(const CANFrame &frame) const
{
    QStringList parts;
    const QByteArray payload = frame.payload();
    parts.reserve(payload.size());

    for (int i = 0; i < payload.size(); ++i) {
        parts << QString("%1")
                    .arg(static_cast<unsigned char>(payload.at(i)), 2, 16, QLatin1Char('0'))
                    .toUpper();
    }

    return parts.join(' ');
}

QString MainWindow::formatPayloadBits(const CANFrame &frame) const
{
    QStringList lines;
    const QByteArray payload = frame.payload();
    lines.reserve(payload.size());

    for (int i = 0; i < payload.size(); ++i) {
        const unsigned char b = static_cast<unsigned char>(payload.at(i));
        QString bits;
        for (int bit = 7; bit >= 0; --bit)
            bits.append((b & (1 << bit)) ? '1' : '0');

        lines << QString("B%1: %2").arg(i).arg(bits);
    }

    return lines.join('\n');
}

bool MainWindow::findPreviousFrameWithSameId(const QModelIndex &sourceIndex, CANFrame &outFrame) const
{
    if (!model) return false;

    const CANFrame *current = model->getFilteredFrameRef(sourceIndex.row());
    if (!current) return false;

    const QVector<CANFrame> *frames = model->getListReference();
    if (!frames) return false;

    const int rawRow = current->originalIndex;
    if (rawRow <= 0 || rawRow > frames->size()) return false;

    for (int i = rawRow - 1; i >= 0; --i) {
        const CANFrame &candidate = frames->at(i);
        if (candidate.frameId() == current->frameId() &&
            candidate.bus == current->bus &&
            candidate.hasExtendedFrameFormat() == current->hasExtendedFrameFormat()) {
            outFrame = candidate;
            return true;
        }
    }

    return false;
}

QString MainWindow::formatChangedSummary(const CANFrame &frame, const CANFrame &previousFrame) const
{
    const QByteArray a = frame.payload();
    const QByteArray b = previousFrame.payload();
    const int count = qMin(a.size(), b.size());

    QStringList changed;
    for (int i = 0; i < count; i++)
    {
        if (a.at(i) != b.at(i))
            changed << QString("B%1").arg(i);
    }

    if (a.size() != b.size())
    {
        for (int i = count; i < a.size(); i++)
            changed << QString("B%1").arg(i);
    }

    if (changed.isEmpty())
        return tr("Changed: none");

    return tr("Changed: %1 (%2/%3 bytes)")
        .arg(changed.join(", "))
        .arg(changed.size())
        .arg(a.size());
}

QVector<int> MainWindow::findSameIdNeighborRows(const QModelIndex &sourceIndex, int radius) const
{
    QVector<int> rows;
    if (!model) return rows;

    const CANFrame *centerFrame = model->getFilteredFrameRef(sourceIndex.row());
    if (!centerFrame) return rows;

    const QVector<CANFrame> *frames = model->getListReference();
    if (!frames) return rows;

    const int centerRow = centerFrame->originalIndex;
    if (centerRow < 0 || centerRow >= frames->size()) return rows;

    const CANFrame &center = frames->at(centerRow);

    QVector<int> before;
    for (int i = centerRow - 1; i >= 0 && before.size() < radius; --i) {
        const CANFrame &candidate = frames->at(i);
        if (candidate.frameId() == center.frameId() &&
            candidate.bus == center.bus &&
            candidate.hasExtendedFrameFormat() == center.hasExtendedFrameFormat()) {
            before.prepend(i);
        }
    }

    QVector<int> after;
    for (int i = centerRow + 1; i < frames->size() && after.size() < radius; ++i) {
        const CANFrame &candidate = frames->at(i);
        if (candidate.frameId() == center.frameId() &&
            candidate.bus == center.bus &&
            candidate.hasExtendedFrameFormat() == center.hasExtendedFrameFormat()) {
            after.append(i);
        }
    }

    rows = before;
    rows.append(centerRow);
    rows += after;
    return rows;
}

QString MainWindow::formatNeighborhoodText(const QModelIndex &sourceIndex, int radius) const
{
    if (!model) return tr("(no frame data)");

    const CANFrame *centerFrame = model->getFilteredFrameRef(sourceIndex.row());
    if (!centerFrame) return tr("(no frame data)");

    const QVector<CANFrame> *frames = model->getListReference();
    if (!frames) return tr("(no frame data)");

    const QVector<int> rows = findSameIdNeighborRows(sourceIndex, radius);
    if (rows.isEmpty()) return tr("(no matching context)");

    const int centerRow = centerFrame->originalIndex;
    const int centerPos = rows.indexOf(centerRow);
    if (centerPos < 0) return tr("(context error)");

    int payloadFieldWidth = 0;
    QStringList payloadTexts;
    payloadTexts.reserve(rows.size());

    for (int row : rows) {
        if (row < 0 || row >= frames->size()) continue;
        const QString payloadText = formatPayloadHex(frames->at(row));
        payloadTexts.append(payloadText);
        payloadFieldWidth = qMax(payloadFieldWidth, payloadText.size());
    }

    QStringList lines;

    for (int i = 0; i < rows.size(); ++i) {
        const int row = rows.at(i);
        if (row < 0 || row >= frames->size()) continue;

        const CANFrame &frame = frames->at(row);

        const int delta = i - centerPos;
        QString tag;
        if (delta == 0) tag = "Cur";
        else if (delta < 0) tag = QString::number(delta);
        else tag = QString("+%1").arg(delta);

        const QString idxText = QString("[%1]").arg(frame.originalIndex);

        lines << QString("%1   %2   %3")
                    .arg(payloadTexts.at(i), -payloadFieldWidth)
                    .arg(tag, -4)
                    .arg(idxText, -8);
    }

    return lines.join('\n');
}


void MainWindow::showBookmarkAnalysisDialog(const BookmarkEventAnalyzer::BookmarkAnalysisResult &result)
{
    QDialog *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Event Analysis"));
    dlg->setModal(false);
    dlg->resize(1180, 780);

    QVBoxLayout *mainLayout = new QVBoxLayout(dlg);

    QLabel *summaryLabel = new QLabel(
        tr("Selected frame: Bus %1, ID %2, frame %3\n"
           "Windows: same-ID radius %4, cross-ID before %5, after %6")
            .arg(result.anchorFrame.bus)
            .arg(Utility::formatCANID(result.anchorFrame.frameId(),
                                      result.anchorFrame.hasExtendedFrameFormat()))
            .arg(result.originalIndex)
            .arg(result.sameIdRadius)
            .arg(result.crossIdWindowBefore)
            .arg(result.crossIdWindowAfter),
        dlg);
    summaryLabel->setWordWrap(true);
    summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(summaryLabel);

    QSplitter *splitter = new QSplitter(Qt::Vertical, dlg);
    mainLayout->addWidget(splitter, 1);

    QTabWidget *tabs = new QTabWidget(splitter);

    QWidget *sameIdPage = new QWidget(tabs);
    QVBoxLayout *sameIdLayout = new QVBoxLayout(sameIdPage);
    QTableWidget *sameIdTable = new QTableWidget(sameIdPage);
    sameIdLayout->addWidget(sameIdTable);
    tabs->addTab(sameIdPage, tr("Same-ID Byte Changes"));

    QWidget *crossIdPage = new QWidget(tabs);
    QVBoxLayout *crossIdLayout = new QVBoxLayout(crossIdPage);
    QTableWidget *crossIdTable = new QTableWidget(crossIdPage);
    crossIdLayout->addWidget(crossIdTable);
    tabs->addTab(crossIdPage, tr("Cross-ID Activity"));

    QWidget *detailPane = new QWidget(splitter);
    QVBoxLayout *detailLayout = new QVBoxLayout(detailPane);
    QLabel *detailLabel = new QLabel(tr("Details"), detailPane);
    QPlainTextEdit *detailsText = new QPlainTextEdit(detailPane);
    detailsText->setReadOnly(true);
    detailLayout->addWidget(detailLabel);
    detailLayout->addWidget(detailsText);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    sameIdTable->setColumnCount(10);
    sameIdTable->setHorizontalHeaderLabels(QStringList()
        << tr("Rank")
        << tr("Bus")
        << tr("ID")
        << tr("Byte")
        << tr("Before")
        << tr("After")
        << tr("Score")
        << tr("Support")
        << tr("Stability")
        << tr("Idle"));
    sameIdTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sameIdTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    sameIdTable->setSelectionMode(QAbstractItemView::SingleSelection);
    sameIdTable->setAlternatingRowColors(true);
    sameIdTable->verticalHeader()->setVisible(false);
    sameIdTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    sameIdTable->setSortingEnabled(false);

    crossIdTable->setColumnCount(9);
    crossIdTable->setHorizontalHeaderLabels(QStringList()
        << tr("Rank")
        << tr("Bus")
        << tr("ID")
        << tr("Score")
        << tr("Before")
        << tr("After")
        << tr("Total")
        << tr("Shift")
        << tr("Volatility"));
    crossIdTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    crossIdTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    crossIdTable->setSelectionMode(QAbstractItemView::SingleSelection);
    crossIdTable->setAlternatingRowColors(true);
    crossIdTable->verticalHeader()->setVisible(false);
    crossIdTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    crossIdTable->setSortingEnabled(false);

    sameIdTable->setRowCount(result.sameIdCandidates.size());
    for (int row = 0; row < result.sameIdCandidates.size(); row++)
    {
        const BookmarkEventAnalyzer::FlipCandidate &c = result.sameIdCandidates.at(row);

        QString detail = tr("Bus %1, ID %2, byte %3 changed %4 -> %5\n"
                            "Score %6, %7\n"
                            "Why: %8\n"
                            "Support %9\n"
                            "Local stability %10, local noise %11\n"
                            "Idle stability %12, idle noise %13 (%14)\n"
                            "Before count %15, after count %16, nearby flips %17\n"
                            "Nearest frame to bookmark: %18 (distance %19)")
                .arg(c.key.bus)
                .arg(Utility::formatCANID(c.key.frameId, c.key.extended))
                .arg(c.byteIndex)
                .arg(c.beforeValue, 2, 16, QLatin1Char('0')).toUpper()
                .arg(c.afterValue, 2, 16, QLatin1Char('0')).toUpper()
                .arg(c.score, 0, 'f', 2)
                .arg(describeFlipStrength(c.score))
                .arg(bookmarkEventAnalyzer ? bookmarkEventAnalyzer->describeSameIdReason(c) : QString())
                .arg(c.supportScore, 0, 'f', 2)
                .arg(c.localStability, 0, 'f', 2)
                .arg(c.localNoise, 0, 'f', 3)
                .arg(c.idleStability, 0, 'f', 2)
                .arg(c.idleNoise, 0, 'f', 3)
                .arg(describeIdleNoise(c.idleNoise))
                .arg(c.beforeCount)
                .arg(c.afterCount)
                .arg(c.eventFlipCount)
                .arg(c.nearestOriginalIndex)
                .arg(c.nearestDistance == std::numeric_limits<int>::max() ? -1 : c.nearestDistance);

        QTableWidgetItem *rankItem = new NumericTableWidgetItem(row + 1, QString::number(row + 1));
        rankItem->setData(Qt::UserRole, detail);
        rankItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);
        rankItem->setData(Qt::UserRole + 2, c.key.bus);
        rankItem->setData(Qt::UserRole + 3, QVariant::fromValue<quint32>(c.key.frameId));
        rankItem->setData(Qt::UserRole + 4, c.key.extended);
        rankItem->setData(Qt::UserRole + 5, c.byteIndex);
        sameIdTable->setItem(row, 0, rankItem);

        sameIdTable->setItem(row, 1, new NumericTableWidgetItem(c.key.bus, QString::number(c.key.bus)));
        sameIdTable->setItem(row, 2, new QTableWidgetItem(Utility::formatCANID(c.key.frameId, c.key.extended)));
        sameIdTable->setItem(row, 3, new NumericTableWidgetItem(c.byteIndex, QString::number(c.byteIndex)));
        sameIdTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("%1").arg(c.beforeValue, 2, 16, QLatin1Char('0')).toUpper()));
        sameIdTable->setItem(row, 5, new QTableWidgetItem(QStringLiteral("%1").arg(c.afterValue, 2, 16, QLatin1Char('0')).toUpper()));
        sameIdTable->setItem(row, 6, new NumericTableWidgetItem(c.score, QString::number(c.score, 'f', 2)));
        sameIdTable->setItem(row, 7, new NumericTableWidgetItem(c.supportScore, QString::number(c.supportScore, 'f', 2)));
        sameIdTable->setItem(row, 8, new NumericTableWidgetItem(c.localStability, QString::number(c.localStability, 'f', 2)));
        sameIdTable->setItem(row, 9, new NumericTableWidgetItem(c.idleNoise, QString::number(c.idleNoise, 'f', 3)));
    }

    crossIdTable->setRowCount(result.crossIdCandidates.size());
    for (int row = 0; row < result.crossIdCandidates.size(); row++)
    {
        const BookmarkEventAnalyzer::CrossIdCandidate &c = result.crossIdCandidates.at(row);

        QString detail = tr("Bus %1, ID %2\n"
                            "Score %3\n"
                            "Why: %4\n"
                            "Before %5, after %6, total %7\n"
                            "Appearance shift %8\n"
                            "Payload changes %9, payload volatility %10\n"
                            "Idle stability %11, idle noise %12 (%13)\n"
                            "Appeared only after: %14\n"
                            "Disappeared after: %15\n"
                            "Nearest frame to bookmark: %16 (distance %17)")
                .arg(c.key.bus)
                .arg(Utility::formatCANID(c.key.frameId, c.key.extended))
                .arg(c.score, 0, 'f', 2)
                .arg(bookmarkEventAnalyzer ? bookmarkEventAnalyzer->describeCrossIdReason(c) : QString())
                .arg(c.beforeCount)
                .arg(c.afterCount)
                .arg(c.totalEventCount)
                .arg(c.appearanceShift, 0, 'f', 2)
                .arg(c.payloadChangeCount)
                .arg(c.payloadVolatility, 0, 'f', 2)
                .arg(c.idleStability, 0, 'f', 2)
                .arg(c.idleNoise, 0, 'f', 3)
                .arg(describeIdleNoise(c.idleNoise))
                .arg(c.appearedOnlyAfter ? tr("yes") : tr("no"))
                .arg(c.disappearedAfter ? tr("yes") : tr("no"))
                .arg(c.nearestOriginalIndex)
                .arg(c.nearestDistance == std::numeric_limits<int>::max() ? -1 : c.nearestDistance);

        QTableWidgetItem *rankItem = new NumericTableWidgetItem(row + 1, QString::number(row + 1));
        rankItem->setData(Qt::UserRole, detail);
        rankItem->setData(Qt::UserRole + 1, c.key.bus);
        rankItem->setData(Qt::UserRole + 2, QVariant::fromValue<quint32>(c.key.frameId));
        rankItem->setData(Qt::UserRole + 3, c.key.extended);
        rankItem->setData(Qt::UserRole + 4, c.nearestOriginalIndex);
        crossIdTable->setItem(row, 0, rankItem);

        crossIdTable->setItem(row, 1, new NumericTableWidgetItem(c.key.bus, QString::number(c.key.bus)));
        crossIdTable->setItem(row, 2, new QTableWidgetItem(Utility::formatCANID(c.key.frameId, c.key.extended)));
        crossIdTable->setItem(row, 3, new NumericTableWidgetItem(c.score, QString::number(c.score, 'f', 2)));
        crossIdTable->setItem(row, 4, new NumericTableWidgetItem(c.beforeCount, QString::number(c.beforeCount)));
        crossIdTable->setItem(row, 5, new NumericTableWidgetItem(c.afterCount, QString::number(c.afterCount)));
        crossIdTable->setItem(row, 6, new NumericTableWidgetItem(c.totalEventCount, QString::number(c.totalEventCount)));
        crossIdTable->setItem(row, 7, new NumericTableWidgetItem(c.appearanceShift, QString::number(c.appearanceShift, 'f', 2)));
        crossIdTable->setItem(row, 8, new NumericTableWidgetItem(c.payloadVolatility, QString::number(c.payloadVolatility, 'f', 2)));
    }

    sameIdTable->setSortingEnabled(true);
    crossIdTable->setSortingEnabled(true);
    sameIdTable->sortItems(6, Qt::DescendingOrder);
    crossIdTable->sortItems(3, Qt::DescendingOrder);

    auto clearCurrentSelection = [detailsText]()
    {
        detailsText->clear();
    };

    auto updateSameIdDetails = [sameIdTable, detailsText]()
    {
        const int row = sameIdTable->currentRow();
        if (row < 0)
        {
            detailsText->clear();
            return;
        }

        QTableWidgetItem *item = sameIdTable->item(row, 0);
        if (!item)
        {
            detailsText->clear();
            return;
        }

        detailsText->setPlainText(item->data(Qt::UserRole).toString());
    };

    auto updateCrossIdDetails = [crossIdTable, detailsText]()
    {
        const int row = crossIdTable->currentRow();
        if (row < 0)
        {
            detailsText->clear();
            return;
        }

        QTableWidgetItem *item = crossIdTable->item(row, 0);
        if (!item)
        {
            detailsText->clear();
            return;
        }

        detailsText->setPlainText(item->data(Qt::UserRole).toString());
    };

    connect(sameIdTable, &QTableWidget::currentCellChanged, dlg,
            [updateSameIdDetails](int, int, int, int)
    {
        updateSameIdDetails();
    });

    connect(crossIdTable, &QTableWidget::currentCellChanged, dlg,
            [updateCrossIdDetails](int, int, int, int)
    {
        updateCrossIdDetails();
    });

    connect(tabs, &QTabWidget::currentChanged, dlg,
            [tabs, sameIdPage, crossIdPage, updateSameIdDetails, updateCrossIdDetails](int)
    {
        if (tabs->currentWidget() == sameIdPage)
            updateSameIdDetails();
        else if (tabs->currentWidget() == crossIdPage)
            updateCrossIdDetails();
    });

    connect(sameIdTable, &QTableWidget::cellDoubleClicked, dlg,
            [this, sameIdTable](int row, int)
    {
        QTableWidgetItem *item = sameIdTable->item(row, 0);
        if (!item)
            return;

        const int originalIndex = item->data(Qt::UserRole + 1).toInt();
        if (originalIndex < 0)
        {
            statusBar()->showMessage(tr("Could not find a nearby frame for this candidate"), 2500);
            return;
        }

        if (!selectFrameByOriginalIndex(originalIndex))
            statusBar()->showMessage(tr("Matching frame is hidden by filters"), 2500);
    });

    connect(crossIdTable, &QTableWidget::cellDoubleClicked, dlg,
            [this, crossIdTable](int row, int)
    {
        QTableWidgetItem *item = crossIdTable->item(row, 0);
        if (!item)
            return;

        const int originalIndex = item->data(Qt::UserRole + 4).toInt();
        if (originalIndex < 0)
        {
            statusBar()->showMessage(tr("Could not find a nearby frame for this ID"), 2500);
            return;
        }

        if (!selectFrameByOriginalIndex(originalIndex))
            statusBar()->showMessage(tr("Matching frame is hidden by filters"), 2500);
    });

    QDialogButtonBox *buttons = new QDialogButtonBox(dlg);
    QPushButton *copyButton = buttons->addButton(tr("Copy Details"), QDialogButtonBox::ActionRole);
    QPushButton *jumpButton = buttons->addButton(tr("Jump to ID"), QDialogButtonBox::ActionRole);
    QPushButton *graphButton = buttons->addButton(tr("Graph ID"), QDialogButtonBox::ActionRole);
    QPushButton *closeButton = buttons->addButton(QDialogButtonBox::Close);

    connect(copyButton, &QPushButton::clicked, dlg,
            [detailsText]()
    {
        QClipboard *clipboard = QApplication::clipboard();
        if (clipboard)
            clipboard->setText(detailsText->toPlainText());
    });

    connect(jumpButton, &QPushButton::clicked, dlg,
            [this, tabs, sameIdPage, sameIdTable, crossIdTable]()
    {
        if (tabs->currentWidget() == sameIdPage)
        {
            const int row = sameIdTable->currentRow();
            if (row < 0) return;

            QTableWidgetItem *item = sameIdTable->item(row, 0);
            if (!item) return;

            const int originalIndex = item->data(Qt::UserRole + 1).toInt();
            if (originalIndex < 0)
            {
                statusBar()->showMessage(tr("Could not find a nearby frame for this candidate"), 2500);
                return;
            }

            if (!selectFrameByOriginalIndex(originalIndex))
                statusBar()->showMessage(tr("Matching frame is hidden by filters"), 2500);
        }
        else
        {
            const int row = crossIdTable->currentRow();
            if (row < 0) return;

            QTableWidgetItem *item = crossIdTable->item(row, 0);
            if (!item) return;

            const int originalIndex = item->data(Qt::UserRole + 4).toInt();
            if (originalIndex < 0)
            {
                statusBar()->showMessage(tr("Could not find a nearby frame for this ID"), 2500);
                return;
            }

            if (!selectFrameByOriginalIndex(originalIndex))
                statusBar()->showMessage(tr("Matching frame is hidden by filters"), 2500);
        }
    });

    connect(graphButton, &QPushButton::clicked, dlg,
            [this, tabs, sameIdPage, sameIdTable, crossIdTable]()
    {
        if (tabs->currentWidget() == sameIdPage)
        {
            const int row = sameIdTable->currentRow();
            if (row < 0) return;

            QTableWidgetItem *item = sameIdTable->item(row, 0);
            if (!item) return;

            const int originalIndex = item->data(Qt::UserRole + 1).toInt();
            if (originalIndex < 0)
            {
                statusBar()->showMessage(tr("Could not find a nearby frame to graph for this candidate"), 2500);
                return;
            }

            graphFrameByOriginalIndex(originalIndex);
        }
        else
        {
            const int row = crossIdTable->currentRow();
            if (row < 0) return;

            QTableWidgetItem *item = crossIdTable->item(row, 0);
            if (!item) return;

            const int originalIndex = item->data(Qt::UserRole + 4).toInt();
            if (originalIndex < 0)
            {
                statusBar()->showMessage(tr("Could not find a nearby frame to graph for this ID"), 2500);
                return;
            }

            graphFrameByOriginalIndex(originalIndex);
        }
    });

    connect(closeButton, &QPushButton::clicked, dlg, &QDialog::close);
    mainLayout->addWidget(buttons);

    if (sameIdTable->rowCount() > 0)
    {
        tabs->setCurrentWidget(sameIdPage);
        sameIdTable->setCurrentCell(0, 0);
        updateSameIdDetails();
    }
    else if (crossIdTable->rowCount() > 0)
    {
        tabs->setCurrentWidget(crossIdPage);
        crossIdTable->setCurrentCell(0, 0);
        updateCrossIdDetails();
    }
    else
    {
        clearCurrentSelection();
    }

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::jumpToAnalysisFrameIndex(int originalIndex)
{
    if (originalIndex < 0)
        return;

    selectFrameByOriginalIndex(originalIndex);
}


void MainWindow::graphAnalysisFrameKey(const BookmarkEventAnalyzer::FrameKey &key)
{
    if (!model)
        return;

    const QVector<CANFrame> *frames = model->getListReference();
    if (!frames || frames->isEmpty())
        return;

    for (int i = 0; i < frames->size(); i++)
    {
        const CANFrame &frame = frames->at(i);
        if (frame.bus != key.bus)
            continue;
        if (frame.frameId() != key.frameId)
            continue;
        if (frame.hasExtendedFrameFormat() != key.extended)
            continue;

        showGraphingWindow();
        emit sendCenterTimeID(frame.frameId(), frame.timeStamp().microSeconds() / 1000000.0);
        selectFrameByOriginalIndex(frame.originalIndex);
        return;
    }

    statusBar()->showMessage(tr("Could not find a frame to graph for the selected ID"), 2500);
}

void MainWindow::setupEmbeddedAnalysisViews()
{
    if (ui->tableControlAnalysis) {
        ui->tableControlAnalysis->setModel(controlCandidateModel);
        ui->tableControlAnalysis->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->tableControlAnalysis->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->tableControlAnalysis->setAlternatingRowColors(true);
        ui->tableControlAnalysis->setSortingEnabled(false);
        ui->tableControlAnalysis->setContextMenuPolicy(Qt::CustomContextMenu);
        ui->tableControlAnalysis->horizontalHeader()->setStretchLastSection(true);
        ui->tableControlAnalysis->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ui->tableControlAnalysis->verticalHeader()->setVisible(false);
        ui->tableControlAnalysis->setColumnWidth(ControlCandidateModel::ReasonCol, 420);

        connect(ui->tableControlAnalysis, &QTableView::doubleClicked,
                this, [this](const QModelIndex &index) {
                    if (!index.isValid()) return;
                    jumpToControlCandidate(index.row());
                });

        connect(ui->tableControlAnalysis, &QWidget::customContextMenuRequested,
                this, [this](const QPoint &) {
                    // You can add a menu later if desired.
                    // For now, keep behavior simple and avoid a dead signal hookup.
                });

        if (ui->tableControlAnalysis->selectionModel()) {
            connect(ui->tableControlAnalysis->selectionModel(), &QItemSelectionModel::currentRowChanged,
                    this, &MainWindow::updateEmbeddedControlDetailsForCurrentRow);
        }
    }

    if (ui->textControlAnalysis) {
        ui->textControlAnalysis->setReadOnly(true);
        ui->textControlAnalysis->setLineWrapMode(QPlainTextEdit::NoWrap);
        ui->textControlAnalysis->setPlainText(tr("No control-state analysis loaded."));
    }

    if (ui->eventCorrelationView) {
        ui->eventCorrelationView->setColumnCount(10);
        ui->eventCorrelationView->setHorizontalHeaderLabels(
            QStringList() << tr("Score")
                          << tr("Bus")
                          << tr("ID")
                          << tr("Before")
                          << tr("After")
                          << tr("Shift")
                          << tr("Volatility")
                          << tr("Idle Stability")
                          << tr("Distance")
                          << tr("Reason"));
        ui->eventCorrelationView->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->eventCorrelationView->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->eventCorrelationView->setAlternatingRowColors(true);
        ui->eventCorrelationView->setSortingEnabled(true);
        ui->eventCorrelationView->horizontalHeader()->setStretchLastSection(true);
        ui->eventCorrelationView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ui->eventCorrelationView->verticalHeader()->setVisible(false);

        connect(ui->eventCorrelationView, &QTableWidget::cellDoubleClicked,
                this, [this](int, int) {
                    jumpToSelectedEventCorrelationCandidate();
                });
    }

    if (ui->btnEventCorrelationJump) {
        ui->btnEventCorrelationJump->setText(tr("Jump To"));
        connect(ui->btnEventCorrelationJump, &QPushButton::clicked,
                this, &MainWindow::jumpToSelectedEventCorrelationCandidate);
    }

    if (ui->btnEventCorrelationBookmark) {
        ui->btnEventCorrelationBookmark->setText(tr("Bookmark"));
        connect(ui->btnEventCorrelationBookmark, &QPushButton::clicked,
                this, &MainWindow::bookmarkSelectedEventCorrelationCandidate);
    }

    refreshEmbeddedControlAnalysis();
    clearEmbeddedEventCorrelation();
}

void MainWindow::refreshEmbeddedControlAnalysis()
{
    if (!ui->tableControlAnalysis || !controlCandidateModel)
        return;

    ui->tableControlAnalysis->clearSelection();
    ui->tableControlAnalysis->resizeColumnsToContents();
    ui->tableControlAnalysis->setColumnWidth(ControlCandidateModel::ReasonCol, 420);

    if (controlCandidateModel->rowCount() > 0) {
        ui->tableControlAnalysis->selectRow(0);
        updateEmbeddedControlDetailsTextForRow(0);
    } else if (ui->textControlAnalysis) {
        ui->textControlAnalysis->setPlainText(tr("No control-state candidates."));
    }
}

void MainWindow::updateEmbeddedControlDetailsForCurrentRow(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);

    if (!current.isValid()) {
        if (ui->textControlAnalysis)
            ui->textControlAnalysis->setPlainText(tr("No control-state candidate selected."));
        return;
    }

    updateEmbeddedControlDetailsTextForRow(current.row());
}

void MainWindow::updateEmbeddedControlDetailsTextForRow(int row)
{
    if (!controlCandidateModel || !ui->textControlAnalysis ||
        row < 0 || row >= controlCandidateModel->rowCount()) {
        if (ui->textControlAnalysis)
            ui->textControlAnalysis->clear();
        return;
    }

    const ControlCandidate &candidate = controlCandidateModel->candidateAt(row);
    ui->textControlAnalysis->setPlainText(
        controlCandidateModel->formatExpandedSequenceDetails(candidate));
    }

void MainWindow::jumpToSelectedEmbeddedControlCandidate()
{
    if (!ui->tableControlAnalysis || !ui->tableControlAnalysis->selectionModel())
        return;

    const QModelIndex idx = ui->tableControlAnalysis->selectionModel()->currentIndex();
    if (idx.isValid())
        jumpToControlCandidate(idx.row());
}

void MainWindow::bookmarkSelectedEmbeddedControlCandidate()
{
    if (!ui->tableControlAnalysis || !ui->tableControlAnalysis->selectionModel())
        return;

    const QModelIndex idx = ui->tableControlAnalysis->selectionModel()->currentIndex();
    if (idx.isValid())
        bookmarkControlCandidate(idx.row());
}

void MainWindow::refreshEmbeddedEventCorrelation(const BookmarkEventAnalyzer::BookmarkAnalysisResult &result)
{
    lastBookmarkAnalysisResult = result;

    if (!ui->eventCorrelationView)
        return;

    ui->eventCorrelationView->setSortingEnabled(false);
    ui->eventCorrelationView->clearContents();
    ui->eventCorrelationView->setRowCount(result.crossIdCandidates.size());

    for (int row = 0; row < result.crossIdCandidates.size(); ++row) {
        const BookmarkEventAnalyzer::CrossIdCandidate &c = result.crossIdCandidates.at(row);

        auto *scoreItem = new NumericTableWidgetItem(c.score, QString::number(c.score, 'f', 3));
        auto *busItem = new NumericTableWidgetItem(c.key.bus, QString::number(c.key.bus));
        auto *idItem = new NumericTableWidgetItem(
            c.key.frameId,
            QStringLiteral("0x%1").arg(c.key.frameId, c.key.extended ? 8 : 3, 16, QLatin1Char('0')).toUpper());
        auto *beforeItem = new NumericTableWidgetItem(c.beforeCount, QString::number(c.beforeCount));
        auto *afterItem = new NumericTableWidgetItem(c.afterCount, QString::number(c.afterCount));
        auto *shiftItem = new NumericTableWidgetItem(c.appearanceShift, QString::number(c.appearanceShift, 'f', 3));
        auto *volItem = new NumericTableWidgetItem(c.payloadVolatility, QString::number(c.payloadVolatility, 'f', 3));
        auto *idleItem = new NumericTableWidgetItem(c.idleStability, QString::number(c.idleStability, 'f', 3));
        auto *distItem = new NumericTableWidgetItem(
            c.nearestDistance == std::numeric_limits<int>::max() ? 999999 : c.nearestDistance,
            c.nearestDistance == std::numeric_limits<int>::max() ? tr("-") : QString::number(c.nearestDistance));
        auto *reasonItem = new QTableWidgetItem(
            bookmarkEventAnalyzer ? bookmarkEventAnalyzer->describeCrossIdReason(c) : QString());

        scoreItem->setData(Qt::UserRole, row);
        scoreItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);

        busItem->setData(Qt::UserRole, row);
        idItem->setData(Qt::UserRole, row);
        beforeItem->setData(Qt::UserRole, row);
        afterItem->setData(Qt::UserRole, row);
        shiftItem->setData(Qt::UserRole, row);
        volItem->setData(Qt::UserRole, row);
        idleItem->setData(Qt::UserRole, row);
        distItem->setData(Qt::UserRole, row);
        reasonItem->setData(Qt::UserRole, row);

        busItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);
        idItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);
        beforeItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);
        afterItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);
        shiftItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);
        volItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);
        idleItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);
        distItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);
        reasonItem->setData(Qt::UserRole + 1, c.nearestOriginalIndex);

        ui->eventCorrelationView->setItem(row, 0, scoreItem);
        ui->eventCorrelationView->setItem(row, 1, busItem);
        ui->eventCorrelationView->setItem(row, 2, idItem);
        ui->eventCorrelationView->setItem(row, 3, beforeItem);
        ui->eventCorrelationView->setItem(row, 4, afterItem);
        ui->eventCorrelationView->setItem(row, 5, shiftItem);
        ui->eventCorrelationView->setItem(row, 6, volItem);
        ui->eventCorrelationView->setItem(row, 7, idleItem);
        ui->eventCorrelationView->setItem(row, 8, distItem);
        ui->eventCorrelationView->setItem(row, 9, reasonItem);
    }

    ui->eventCorrelationView->setSortingEnabled(true);
    ui->eventCorrelationView->sortItems(0, Qt::DescendingOrder);
    ui->eventCorrelationView->resizeColumnsToContents();

    if (result.crossIdCandidates.isEmpty()) {
        ui->eventCorrelationView->clearSelection();
        if (ui->eventCorrelationView)
            ui->eventCorrelationView->clear();
    } else {
        ui->eventCorrelationView->setCurrentCell(0, 0);
    }
}

void MainWindow::clearEmbeddedEventCorrelation()
{
    lastBookmarkAnalysisResult = BookmarkEventAnalyzer::BookmarkAnalysisResult();

    if (ui->eventCorrelationView) {
        ui->eventCorrelationView->setSortingEnabled(false);
        ui->eventCorrelationView->clearContents();
        ui->eventCorrelationView->setRowCount(0);
        ui->eventCorrelationView->setSortingEnabled(true);
    }
}

void MainWindow::bookmarkSelectedEventCorrelationCandidate()
{
    const int candidateIndex = currentEventCorrelationCandidateIndex();
    if (candidateIndex < 0 || candidateIndex >= lastBookmarkAnalysisResult.crossIdCandidates.size()) {
        statusBar()->showMessage(tr("No event correlation candidate is selected"), 2500);
        return;
    }

    const BookmarkEventAnalyzer::CrossIdCandidate &c =
            lastBookmarkAnalysisResult.crossIdCandidates.at(candidateIndex);

    if (c.nearestOriginalIndex < 0) {
        statusBar()->showMessage(tr("Selected candidate has no nearby frame to bookmark"), 2500);
        return;
    }

    const QString label = QStringLiteral("Event Correlation: 0x%1")
            .arg(c.key.frameId, 0, 16)
            .toUpper();
    const QString note = bookmarkEventAnalyzer
            ? bookmarkEventAnalyzer->describeCrossIdReason(c)
            : QString();

    if (!bookmarkFrameByOriginalIndex(c.nearestOriginalIndex, label, note)) {
        statusBar()->showMessage(tr("Could not create bookmark for the selected frame"), 2500);
        return;
    }

    statusBar()->showMessage(
            tr("Bookmark added for frame %1").arg(c.nearestOriginalIndex),
            2000);
}

void MainWindow::jumpToSelectedEventCorrelationCandidate()
{
    const int candidateIndex = currentEventCorrelationCandidateIndex();
    if (candidateIndex < 0 || candidateIndex >= lastBookmarkAnalysisResult.crossIdCandidates.size()) {
        statusBar()->showMessage(tr("No event correlation candidate is selected"), 2500);
        return;
    }

    const BookmarkEventAnalyzer::CrossIdCandidate &c =
            lastBookmarkAnalysisResult.crossIdCandidates.at(candidateIndex);

    if (c.nearestOriginalIndex < 0) {
        statusBar()->showMessage(tr("Selected candidate has no nearby frame to jump to"), 2500);
        return;
    }

    if (!selectFrameByOriginalIndex(c.nearestOriginalIndex)) {
        statusBar()->showMessage(tr("Matching frame is hidden by filters"), 2500);
        return;
    }

    statusBar()->showMessage(
            tr("Jumped to nearby frame %1").arg(c.nearestOriginalIndex),
            2000);
}

void MainWindow::refreshAnalysisTabsForCurrentSelection()
{
    CANFrame frame;
    QModelIndex sourceIndex;
    if (!getSelectedFrameInfo(frame, &sourceIndex)) {
        clearEmbeddedEventCorrelation();
        return;
    }

    if (frame.originalIndex < 0) {
        clearEmbeddedEventCorrelation();
        return;
    }

    QSettings settings;
    const int sameIdRadius = settings.value("Analysis/SameIdRadius", 5).toInt();
    const int crossIdBefore = settings.value("Analysis/CrossIdWindowBefore", 300).toInt();
    const int crossIdAfter = settings.value("Analysis/CrossIdWindowAfter", 300).toInt();

    const BookmarkEventAnalyzer::BookmarkAnalysisResult result =
        analyzeBookmarkEvent(frame.originalIndex, sameIdRadius, crossIdBefore, crossIdAfter);

    refreshEmbeddedEventCorrelation(result);
}

static int crossIdCandidateIndexFromTable(QTableWidget *table, int visualRow)
{
    if (!table || visualRow < 0) return -1;

    QTableWidgetItem *item = table->item(visualRow, 0);
    if (!item) return -1;

    bool ok = false;
    const int idx = item->data(Qt::UserRole).toInt(&ok);
    return ok ? idx : -1;
}

static int analysisOriginalIndexFromTable(QTableWidget *table, int visualRow)
{
    if (!table || visualRow < 0) return -1;

    QTableWidgetItem *item = table->item(visualRow, 0);
    if (!item) return -1;

    bool ok = false;
    const int originalIndex = item->data(Qt::UserRole + 1).toInt(&ok);
    return ok ? originalIndex : -1;
}

int MainWindow::currentEventCorrelationCandidateIndex() const
{
    if (!ui || !ui->eventCorrelationView)
        return -1;

    const int visualRow = ui->eventCorrelationView->currentRow();
    const int visualCol = ui->eventCorrelationView->currentColumn();
    if (visualRow < 0)
        return -1;

    QTableWidgetItem *item = ui->eventCorrelationView->item(visualRow, visualCol >= 0 ? visualCol : 0);
    if (!item)
        item = ui->eventCorrelationView->item(visualRow, 0);
    if (!item)
        return -1;

    bool ok = false;
    const int candidateIndex = item->data(Qt::UserRole).toInt(&ok);
    return ok ? candidateIndex : -1;
}

bool MainWindow::resolveFrameByOriginalIndex(int originalIndex, CANFrame &outFrame) const
{
    if (!model)
        return false;

    const QVector<CANFrame> *frames = model->getListReference();
    if (!frames)
        return false;

    for (const CANFrame &frame : *frames)
    {
        if (frame.originalIndex == originalIndex)
        {
            outFrame = frame;
            return true;
        }
    }

    return false;
}

bool MainWindow::bookmarkFrameByOriginalIndex(int originalIndex, const QString &label, const QString &note)
{
    if (!bookmarkManager)
        return false;

    CANFrame frame;
    if (!resolveFrameByOriginalIndex(originalIndex, frame))
        return false;

    FrameBookmark bm;
    bm.originalIndex = frame.originalIndex;
    bm.timestampMicros = quint64(frame.timeStamp().seconds()) * 1000000ULL
            + frame.timeStamp().microSeconds();
    bm.frameId = frame.frameId();
    bm.bus = frame.bus;
    bm.label = label;
    bm.note = note;

    bookmarkManager->addBookmark(bm);

    if (bookmarkDialog)
        bookmarkDialog->refreshBookmarksView();

    return true;
}

void MainWindow::graphFrameByOriginalIndex(int originalIndex)
{
    CANFrame frame;
    if (!resolveFrameByOriginalIndex(originalIndex, frame))
    {
        statusBar()->showMessage(tr("Could not find a nearby frame to graph for this candidate"), 2500);
        return;
    }

    showGraphingWindow();
    emit sendCenterTimeID(frame.frameId(),
                          double(frame.timeStamp().seconds())
                              + (double(frame.timeStamp().microSeconds()) / 1000000.0));

    if (!selectFrameByOriginalIndex(originalIndex))
        statusBar()->showMessage(tr("Matching frame is hidden by filters"), 2500);
}