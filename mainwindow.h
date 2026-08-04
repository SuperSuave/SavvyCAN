#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "config.h"
#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSet>
#include <QTimer>
#include <array>
#include <algorithm>
#include <limits>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include "canframemodel.h"
#include "can_structs.h"
#include "framefileio.h"
#include "dbc/dbchandler.h"
#include "bus_protocols/isotp_handler.h"
#include "framesenderobject.h"
#include "re/graphingwindow.h"
#include "re/frameinfowindow.h"
#include "frameplaybackwindow.h"
#include "bisectwindow.h"
#include "re/flowviewwindow.h"
#include "framesenderwindow.h"
#include "re/filecomparatorwindow.h"
#include "dbc/dbcmaineditor.h"
#include "mainsettingsdialog.h"
#include "re/discretestatewindow.h"
#include "scriptingwindow.h"
#include "re/rangestatewindow.h"
#include "dbc/dbcloadsavewindow.h"
#include "re/fuzzingwindow.h"
#include "re/udsscanwindow.h"
#include "re/sniffer/snifferwindow.h"
#include "re/isotp_interpreterwindow.h"
#include "motorcontrollerconfigwindow.h"
#include "signalviewerwindow.h"
#include "re/temporalgraphwindow.h"
#include "re/dbccomparatorwindow.h"
#include "re/udsfirmwareuploaderwindow.h"
#include "canbridgewindow.h"
#include "bookmarkmanager.h"
#include "bookmarkmanagerdialog.h"
#include "re/bookmarkeventanalyzer.h"
#include "re/controlanalysisdialog.h"

class CANConnection;
class ConnectionWindow;
class ISOTP_InterpreterWindow;
class ScriptingWindow;
class BookmarkManager;
class BookmarkManagerDialog;
class BookmarkEventAnalyzer;
class ControlStateDetector;
class ControlCandidateModel;
class ControlAnalysisDialog;
class QDockWidget;
class QLabel;
class QPlainTextEdit;
class QGroupBox;
class QWidget;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    static QString loadedFileName;
    static MainWindow *getReference();
    CANFrameModel * getCANFrameModel();
    ~MainWindow();

    void handleDroppedFile(const QString &filename);

public slots:
    void handleLoadFile();
    void handleSaveFile();
    void handleSaveFilteredFile();
    void handleSaveFilters();
    void handleLoadFilters();
    void handleContinousLogging();
    void showGraphingWindow();
    void showFrameDataAnalysis();
    void clearFrames();
    void expandAllRows();
    void collapseAllRows();
    void showPlaybackWindow();
    void showFlowViewWindow();
    void showFrameSenderWindow();
    void showSingleMultiWindow();
    void showRangeWindow();
    void showFuzzyScopeWindow();
    void showComparisonWindow();
    void showSettingsDialog();
    void showUDSFirmwareUploaderWindow();
    void showConnectionSettingsWindow();
    void showScriptingWindow();
    void showDBCFileWindow();
    void showFuzzingWindow();
    void showMCConfigWindow();
    void showUDSScanWindow();
    void showISOInterpreterWindow();
    void showSnifferWindow();
    void showBisectWindow();
    void showSignalViewer();
    void showTemporalGraphWindow();
    void showDBCComparisonWindow();
    void showCANBridgeWindow();
    void exitApp();
    void handleSaveDecoded();
    void handleSaveDecodedCsv();
    void connectionStatusUpdated(int conns);
    void gridClicked(QModelIndex);
    void gridDoubleClicked(const QModelIndex &idx);
    void gridContextMenuRequest(QPoint pos);
    void copyFromTable();
    void setupAddToNewGraph();
    void setupSendToLatestGraphWindow();
    void interpretToggled(bool);
    void overwriteToggled(bool);
    void presistentFiltersToggled(bool state);
    void logReceivedFrame(CANConnection*, QVector<CANFrame>);
    void tickGUIUpdate();
    void toggleCapture();
    void normalizeTiming();
    void updateFilterList();
    void filterListItemChanged(QListWidgetItem *item);
    void busFilterListItemChanged(QListWidgetItem *item);
    void filterSetAll();
    void filterClearAll();
    void headerClicked (int logicalIndex);
    void DBCSettingsUpdated();
    void showBookmarksWindow();

    /// Suggested by AI
    void deleteBookmarkByIndex(int bookmarkIndex);
    void jumpToBookmark(int bookmarkIndex);
    void jumpToOriginalIndex(int originalIndex);
    void copyOriginalIndex();
    void filterFrameFilterList(const QString &text);
    void setAutoBookmarkNewIdsActive(bool enabled);
    void autoBookmarkTimeoutExpired();
    void triggerTimedDiscoveryBookmark();
    void analyzeCurrentBookmarkOrSelection();
    void analyzeControlStatesForLoadedLog();
    void analyzeControlStatesForSelectedFrame();
    void showControlAnalysisWindow();
    void jumpToControlCandidate(int candidateIndex);
    void bookmarkControlCandidate(int candidateIndex);
    void updateEmbeddedControlDetailsForCurrentRow(const QModelIndex &current, const QModelIndex &previous);
    void jumpToSelectedEmbeddedControlCandidate();
    void bookmarkSelectedEmbeddedControlCandidate();

    void jumpToSelectedEventCorrelationCandidate();
    void bookmarkSelectedEventCorrelationCandidate();
    void refreshAnalysisTabsForCurrentSelection();

public slots:
    void analyzeFrameData(QString frameId);
    void updateCopilotStatus(int count);
    FrameInfoWindow* getFrameInfoWindow();
    SnifferWindow* getSnifferWindow() const;
    BisectWindow* getBisectWindow() const;
    FlowViewWindow* getFlowViewWindow() const;
    FuzzingWindow* getFuzzingWindow() const;
    UDSScanWindow* getUDSScanWindow() const;
    ISOTP_InterpreterWindow* getISOTPWindow() const;
    FrameSenderWindow* getFrameSenderWindow() const;
    SignalViewerWindow* getSignalViewerWindow() const;
    GraphingWindow* getGraphingWindow() const;
    FramePlaybackWindow* getPlaybackWindow() const;
    ConnectionWindow* getConnectionWindow() const;
    void gotFrames(int);
    void updateSettings();
    void readUpdateableSettings();
    void gotCenterTimeID(uint32_t ID, double timestamp);
    void updateConnectionSettings(QString connectionType, QString port, int speed0, int speed1);

signals:
    void sendCANFrame(const CANFrame *, int);
    void suspendCapturing(bool);

    //-1 = frames cleared, -2 = a new file has been loaded (so all frames are different), otherwise # of new frames
    void framesUpdated(int numFrames); //something has updated the frame list (send at gui update frequency)
    void frameUpdateRapid(int numFrames);
    void settingsUpdated();
    void sendCenterTimeID(uint32_t ID, double timestamp);

private:
    Ui::MainWindow *ui;
    QAction *copyAct;
    static MainWindow *selfRef;

    //canbus related data
    CANFrameModel *model;
    DBCHandler *dbcHandler;
    QByteArray inputBuffer;
    QTimer updateTimer;
    QElapsedTimer *elapsedTime;
    QLabel *copilotStatusLabel;
    FrameSenderObject *frameSender;
    int framesPerSec;
    int rxFrames;
    bool inhibitFilterUpdate;
    bool useHex;
    bool useColorsByCanId;
    bool allowCapture;
    bool ignoreDBCColors;
    bool CSVAbsTime;
    bool bDirty; //have frames been added or subtracted since the last save/load?
    bool useFiltered; //should sub-windows use the unfiltered or filtered frames list?

    bool continuousLogging;
    int continuousLogFlushCounter;

    //References to other windows we can display

    //Graph window is allowed to instantiate more than once. All the rest are not (yet).
    GraphingWindow *lastGraphingWindow;
    QList<GraphingWindow *> graphWindows;

    FrameInfoWindow *frameInfoWindow;
    FramePlaybackWindow *playbackWindow;
    FlowViewWindow *flowViewWindow;
    FrameSenderWindow *frameSenderWindow;
    DBCMainEditor *dbcMainEditor;
    FileComparatorWindow *comparatorWindow;
    MainSettingsDialog *settingsDialog;
    DiscreteStateWindow *discreteStateWindow;
    UDSFirmwareUploaderWindow *udsFirmwareUploaderWindow;
    ConnectionWindow *connectionWindow;
    ScriptingWindow *scriptingWindow;
    RangeStateWindow *rangeWindow;
    DBCLoadSaveWindow *dbcFileWindow;
    FuzzingWindow *fuzzingWindow;
    UDSScanWindow *udsScanWindow;
    ISOTP_InterpreterWindow *isoWindow;
    SnifferWindow* snifferWindow;
    MotorControllerConfigWindow *motorctrlConfigWindow;
    BisectWindow* bisectWindow;
    SignalViewerWindow *signalViewerWindow;
    TemporalGraphWindow *temporalGraphWindow;
    DBCComparatorWindow *dbcComparatorWindow;
    CANBridgeWindow *canBridgeWindow;

    //various private storage
    QLabel lbStatusConnected;
    QLabel lbStatusFilename;
    QLabel lbStatusDatabase;
    QLabel lbHelp;
    int normalRowHeight;
    bool isConnected;
    QPoint contextMenuPosition;
    bool rowExpansionActive = false;

    //bookmarking -- Helped by AI
    BookmarkManager *bookmarkManager;
    BookmarkManagerDialog *bookmarkDialog;
    BookmarkEventAnalyzer *bookmarkEventAnalyzer = nullptr;
    ControlStateDetector *controlStateDetector = nullptr;
    ControlCandidateModel *controlCandidateModel = nullptr;
    ControlAnalysisDialog *controlAnalysisDialog = nullptr;

    QString quickBookmarkLabel = "Bookmark";
    QString quickBookmarkAlternateLabel = "Alternate Bookmark";
    bool quickBookmarkUseAlternatingLabels = false;
    bool quickBookmarkAlternateState = false; // false = A next, true = B next

    void triggerQuickBookmark();
    void resetQuickBookmarkToggle();

    void addBookmarkSmart(const QString &tag);
    void addBookmarkAtTail(const QString &tag);
    void addBookmarkAtCurrentSelection();
    void addBookmarkAtCurrentSelection(const QString &tag);

    quint64 makeAutoBookmarkKey(const CANFrame &frame) const;
    void processAutoBookmarks(const QVector<CANFrame> &frames);
    bool findLatestFrameByBusIdAndFormat(int bus, uint32_t frameId, bool extended, CANFrame &outFrame) const;
    void armAutoBookmarkWindow(int durationMs);

    bool autoBookmarkNewIdsActive = false;
    QSet<quint64> autoBookmarkKnownIds;
    QSet<quint64> autoBookmarkSeenIds;
    QTimer *autoBookmarkTimer = nullptr;
    int autoBookmarkDurationMs = 2000;

    BookmarkEventAnalyzer::BookmarkAnalysisResult analyzeBookmarkEvent(
            int originalIndex,
            int sameIdRadius,
            int crossIdWindowBefore,
            int crossIdWindowAfter) const;

    bool resolveFrameByOriginalIndex(int originalIndex, CANFrame &outFrame) const;
    bool bookmarkFrameByOriginalIndex(int originalIndex, const QString &label, const QString &note);

    void showBookmarkAnalysisDialog(
            const BookmarkEventAnalyzer::BookmarkAnalysisResult &result);
    void jumpToAnalysisFrameKey(
            const BookmarkEventAnalyzer::FrameKey &key);
    void jumpToAnalysisFrameIndex(int originalIndex);
    void graphAnalysisFrameKey(
            const BookmarkEventAnalyzer::FrameKey &key);
    void graphFrameByOriginalIndex(int originalIndex);

    BookmarkEventAnalyzer::BookmarkAnalysisResult lastBookmarkAnalysisResult;

    // byteinspector
    QDockWidget *inspectDock = nullptr;
    QWidget *inspectPaneWidget = nullptr;
    QSortFilterProxyModel *proxyModel = nullptr;
    void clearInspectDock();
    void updateInspectDock(const QModelIndex &current, const QModelIndex &previous);
    void populateInspectDock(const QModelIndex &sourceIndex);

    QString formatPayloadHex(const CANFrame &frame) const;
    QString formatPayloadBits(const CANFrame &frame) const;
    QString formatChangedSummary(const CANFrame &frame, const CANFrame &previousFrame) const;
    QString formatNeighborhoodText(const QModelIndex &sourceIndex, int radius = 2) const;
    QVector<int> findSameIdNeighborRows(const QModelIndex &sourceIndex, int radius) const;
    bool findPreviousFrameWithSameId(const QModelIndex &sourceIndex, CANFrame &outFrame) const;

    void setupEmbeddedAnalysisViews();
    void refreshEmbeddedControlAnalysis();
    void refreshEmbeddedEventCorrelation(
            const BookmarkEventAnalyzer::BookmarkAnalysisResult &result);
    void clearEmbeddedEventCorrelation();
    void updateEmbeddedControlDetailsTextForRow(int row);
    int currentEventCorrelationCandidateIndex() const;

    QString describeFlipStrength(double score) const;
    QString describeIdleNoise(double idleNoise) const;


    //private methods
    QString getSignalNameFromPosition(QPoint pos);
    uint32_t getMessageIDFromPosition(QPoint pos);
    bool getSelectedFrameInfo(CANFrame &outFrame, QModelIndex *outIndex = nullptr);
    bool selectFrameByOriginalIndex(int originalIndex);
    void copySelection();
    void handleSaveDecodedMethod(bool csv);
    void saveDecodedTextFile(QString);
    void saveDecodedTextFileAsColumns(QString);
    void addFrameToDisplay(CANFrame &, bool);
    void updateFileStatus();
    void closeEvent(QCloseEvent *event);
    void onDbcNeedsRefresh(int idx);

private slots:
    void killEmAll();
    void killWindow(QDialog *win);
    void readSettings();
    void writeSettings();
    bool eventFilter(QObject *obj, QEvent *event);
    void manageRowExpansion();
    void disableAutoRowExpansion();
    int64_t selectedFrameTimestamp();
    void scrollToNearestTimestamp(int64_t timestamp);
};

#endif // MAINWINDOW_H