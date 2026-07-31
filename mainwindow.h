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

class CANConnection;
class ConnectionWindow;
class ISOTP_InterpreterWindow;
class ScriptingWindow;
class BookmarkManager;
class BookmarkManagerDialog;
class QDockWidget;
class QLabel;
class QPlainTextEdit;
class QGroupBox;
class QWidget;

enum SIMP_COL
{
    SC_COL_EN = 0,
    SC_COL_BUS = 1,
    SC_COL_ID = 2,
    SC_COL_EXT = 3,
    SC_COL_REM = 4,
    SC_COL_DATA = 5,
    SC_COL_INTERVAL = 6,
    SC_COL_COUNT = 7,
};

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
    void onSenderCellChanged(int, int);
    void showBookmarksWindow();

    /// Suggested by AI
    void deleteBookmarkByIndex(int bookmarkIndex);
    void jumpToBookmark(int bookmarkIndex);
    void jumpToOriginalIndex();
    void copyOriginalIndex();
    void filterFrameFilterList(const QString &text);
    void setAutoBookmarkNewIdsActive(bool enabled);
    void autoBookmarkTimeoutExpired();
    void triggerTimedDiscoveryBookmark();
    void analyzeCurrentBookmarkOrSelection();

public slots:
    void analyzeFrameData(QString frameId);
    void updateMCPStatus(int count);
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
    QLabel *mcpStatusLabel;
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
    bool inhibitSenderChanged;

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

    QString describeFlipStrength(double score) const;
    QString describeIdleNoise(double idleNoise) const;

    struct FrameKey
    {
        int bus = -1;
        uint32_t frameId = 0;
        bool extended = false;

        bool operator==(const FrameKey &other) const
        {
            return bus == other.bus &&
                   frameId == other.frameId &&
                   extended == other.extended;
        }
    };

    friend inline uint qHash(const FrameKey &key, uint seed = 0)
    {
        seed = qHash(key.bus, seed);
        seed = qHash(key.frameId, seed);
        seed = qHash(key.extended, seed);
        return seed;
    }

    struct EventByteStats
    {
        bool hasBeforeValue = false;
        bool hasAfterValue = false;
        quint8 beforeValue = 0;
        quint8 afterValue = 0;
        int beforeCount = 0;
        int afterCount = 0;

        int beforeTransitions = 0;
        int afterTransitions = 0;
        int crossTransitions = 0;

        quint8 lastBeforeSeen = 0;
        quint8 lastAfterSeen = 0;
        bool hasLastBeforeSeen = false;
        bool hasLastAfterSeen = false;
    };

    struct ByteIdleStats
    {
        int samples = 0;
        int changes = 0;
        quint8 lastValue = 0;
        bool hasLastValue = false;
    };

    struct FrameIdleStats
    {
        int totalFrames = 0;
        int maxDlcSeen = 0;
        std::array<ByteIdleStats, 64> bytes;
    };

    struct EventFrameStats
    {
        FrameKey key;
        int matchedFramesBefore = 0;
        int matchedFramesAfter = 0;
        std::array<EventByteStats, 64> bytes;

        int nearestOriginalIndex = -1;
        int nearestDistance = std::numeric_limits<int>::max();
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

        int nearestOriginalIndex = -1;
        int nearestDistance = std::numeric_limits<int>::max();

        double supportScore = 0.0;
        double localNoise = 0.0;
        double localStability = 0.0;
        double idleNoise = 0.0;
        double idleStability = 0.0;

        double score = 0.0;
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

        bool appearedOnlyAfter = false;
        bool disappearedAfter = false;

        double score = 0.0;

        int nearestOriginalIndex = -1;
        int nearestDistance = std::numeric_limits<int>::max();
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



    struct CrossIdEventStats
    {
        FrameKey key;
        int beforeCount = 0;
        int afterCount = 0;

        bool hasLastBefore = false;
        bool hasLastAfter = false;
        QByteArray lastBeforePayload;
        QByteArray lastAfterPayload;

        int beforePayloadTransitions = 0;
        int afterPayloadTransitions = 0;

        int anchorOriginalIndex = -1;
        int nearestOriginalIndex = -1;
        int nearestDistance = std::numeric_limits<int>::max();
    };

    struct SameIdScoreFeatures
    {
        double eventDelta = 0.0;
        double supportScore = 0.0;
        double localStability = 0.0;
        double idleStability = 0.0;
        double windowConfidence = 0.0;
    };

    struct CrossIdScoreFeatures
    {
        double appearanceShift = 0.0;
        double postEventPersistence = 0.0;
        double exclusiveAfter = 0.0;
        double exclusiveBefore = 0.0;
        double payloadVolatility = 0.0;
        double idleStability = 0.0;
    };

    FrameKey makeFrameKey(const CANFrame &frame) const;
    BookmarkAnalysisResult analyzeBookmarkEvent(int originalIndex, int sameIdRadius, int crossIdWindowBefore, int crossIdWindowAfter) const;

    QVector<FlipCandidate> analyzeSameIdAroundBookmark(const QVector<CANFrame> &frames, int originalIndex, int sameIdRadius) const;

    QVector<CrossIdCandidate> analyzeCrossIdAroundBookmark(const QVector<CANFrame> &frames, int originalIndex, int windowBefore, int windowAfter) const;

    void accumulateCrossIdEventFrame(CrossIdEventStats &stats, const CANFrame &frame, bool isBeforeSide) const;
    void accumulateEventFrame(EventFrameStats &stats, const CANFrame &frame, bool isBeforeSide, int anchorOriginalIndex) const;
    QVector<FlipCandidate> rankFlipCandidates(const QHash<FrameKey, EventFrameStats> &eventStats, int sameIdRadius) const;

    // Optional future enhancement: live or offline-learned idle baseline.
    // Not required, but leaving the state hook here makes later extension easy.
    QHash<FrameKey, FrameIdleStats> idleBaseline;
    bool idleBaselineAvailable = false;

    // byteinspector -- Helped by AI
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

    static double clamp01(double value);
    static double safeRatio(double num, double denom);

    SameIdScoreFeatures buildSameIdScoreFeatures(const EventByteStats &eb, const ByteIdleStats *idleByteStats, int sameIdRadius) const;

    CrossIdScoreFeatures buildCrossIdScoreFeatures(const CrossIdEventStats &stats, const FrameIdleStats *idleStats, int windowBefore, int windowAfter) const;

    double scoreSameIdCandidate(const SameIdScoreFeatures &f) const;
    double scoreCrossIdCandidate(const CrossIdScoreFeatures &f) const;
    double computeSameIdSupportScore(const EventByteStats& stats, int sameIdRadius) const;
    double computeSameIdLocalStability(const EventByteStats& stats) const;
    double computeSameIdIdleStability(const ByteIdleStats* idleStats) const;
    double scoreSameIdCandidate(const EventByteStats& stats, const ByteIdleStats* idleStats, int sameIdRadius) const;

    double computeCrossIdAppearanceShift(const CrossIdEventStats& stats, int windowBefore, int windowAfter) const;
    double computeCrossIdPayloadVolatility(const CrossIdEventStats& stats) const;
    double computeCrossIdIdleStability(const FrameIdleStats* idleStats) const;
    double scoreCrossIdCandidate(const CrossIdEventStats& stats, const FrameIdleStats* idleStats, int windowBefore, int windowAfter) const;

    void showBookmarkAnalysisDialog(const BookmarkAnalysisResult &result);
    void jumpToAnalysisFrameKey(const FrameKey &key);
    void jumpToAnalysisFrameIndex(int originalIndex);
    void graphAnalysisFrameKey(const FrameKey &key);
    void graphAnalysisOriginalIndex(int originalIndex);



    QString describeSameIdReason(const FlipCandidate &c) const;
    QString describeCrossIdReason(const CrossIdCandidate &c) const;


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
    void killEmAll();
    void killWindow(QDialog *win);
    void readSettings();
    void writeSettings();
    bool eventFilter(QObject *obj, QEvent *event);
    void manageRowExpansion();
    void disableAutoRowExpansion();
    void createSenderRow();
    void processSenderCellChange(int line, int col);
};

#endif // MAINWINDOW_H