#include "mainwindow.h"
#include "themes/thememanager.h"
#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QDebug>
#include <QFileOpenEvent>
#include <QSettings>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QFont>
#include "mcp/mcpserver.h"
#include "utils/logger.h"

class SavvyLensApplication : public QApplication
{
public:
    MainWindow *mainWindow;

    SavvyLensApplication(int &argc, char **argv) : QApplication(argc, argv)
    {
        mainWindow = nullptr;
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen)
        {
            QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(event);
            if (mainWindow) mainWindow->handleDroppedFile(openEvent->file());
        }

        return QApplication::event(event);
    }
};

static void applyDarkPalette(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette pal;

    pal.setColor(QPalette::Window,            QColor(45, 45, 45));
    pal.setColor(QPalette::WindowText,        QColor(220, 220, 220));
    pal.setColor(QPalette::Base,              QColor(30, 30, 30));
    pal.setColor(QPalette::AlternateBase,     QColor(53, 53, 53));
    pal.setColor(QPalette::ToolTipBase,       QColor(45, 45, 45));
    pal.setColor(QPalette::ToolTipText,       QColor(220, 220, 220));
    pal.setColor(QPalette::Text,              QColor(220, 220, 220));
    pal.setColor(QPalette::Button,            QColor(53, 53, 53));
    pal.setColor(QPalette::ButtonText,        QColor(220, 220, 220));
    pal.setColor(QPalette::BrightText,        QColor(255, 120, 120));
    pal.setColor(QPalette::Link,              QColor(42, 130, 218));
    pal.setColor(QPalette::Highlight,         QColor(42, 130, 218));
    pal.setColor(QPalette::HighlightedText,   QColor(255, 255, 255));
#if (QT_VERSION >= QT_VERSION_CHECK(5, 12, 0))
    pal.setColor(QPalette::PlaceholderText,   QColor(140, 140, 140));
#endif

    pal.setColor(QPalette::Light,             QColor(64, 64, 64));
    pal.setColor(QPalette::Midlight,          QColor(58, 58, 58));
    pal.setColor(QPalette::Dark,              QColor(25, 25, 25));
    pal.setColor(QPalette::Mid,               QColor(40, 40, 40));
    pal.setColor(QPalette::Shadow,            QColor(0, 0, 0));

    pal.setColor(QPalette::Disabled, QPalette::Text,            QColor(120, 120, 120));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText,      QColor(120, 120, 120));
    pal.setColor(QPalette::Disabled, QPalette::WindowText,      QColor(120, 120, 120));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(180, 180, 180));

    app.setPalette(pal);

    app.setStyleSheet(R"(
        QWidget {
            background-color: #2d2d2d;
            color: #dcdcdc;
        }

        QMainWindow, QDialog {
            background-color: #2d2d2d;
        }

        QToolTip {
            color: #e6e6e6;
            background-color: #353535;
            border: 1px solid #5f5f5f;
        }

        QMenuBar {
            background-color: #353535;
            color: #dcdcdc;
        }

        QMenuBar::item {
            background: transparent;
            padding: 4px 8px;
        }

        QMenuBar::item:selected {
            background: #409cff;
            color: white;
        }

        QMenu {
            background-color: #353535;
            color: #dcdcdc;
            border: 1px solid #5a5a5a;
        }

        QMenu::item {
            padding: 6px 24px 6px 24px;
            background-color: transparent;
        }

        QMenu::item:selected {
            background-color: #409cff;
            color: white;
        }

        QMenu::separator {
            height: 1px;
            background: #5a5a5a;
            margin: 4px 8px;
        }

        QToolBar {
            background-color: #353535;
            border-bottom: 1px solid #5a5a5a;
            spacing: 4px;
        }

        QToolButton {
            background-color: transparent;
            color: #dcdcdc;
            border: 1px solid transparent;
            padding: 6px;
            margin: 2px;
            border-radius: 4px;
        }

        QToolButton:hover {
            background-color: #454545;
            border-color: #606060;
        }

        QToolButton:pressed, QToolButton:checked {
            background-color: #409cff;
            color: white;
            border-color: #409cff;
        }

        QTabWidget::pane {
            border: 1px solid #5a5a5a;
            top: -1px;
            background: #2d2d2d;
        }

        QTabBar::tab {
            background: #353535;
            color: #cfcfcf;
            padding: 8px 14px;
            margin-right: 2px;
            border: 1px solid #5a5a5a;
            border-bottom: none;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }

        QTabBar::tab:selected {
            background: #2d2d2d;
            color: white;
        }

        QTabBar::tab:hover:!selected {
            background: #404040;
        }

        QTableView {
            background-color: #1e1e1e;
            alternate-background-color: #252525;
            color: #dcdcdc;
            gridline-color: #444444;
            selection-background-color: #409cff;
            selection-color: white;
            border: 1px solid #5a5a5a;
        }

        QHeaderView::section {
            background-color: #353535;
            color: #dcdcdc;
            padding: 6px;
            border: 1px solid #4a4a4a;
        }

        QTableCornerButton::section {
            background-color: #353535;
            border: 1px solid #4a4a4a;
        }

        QAbstractItemView {
            selection-background-color: #409cff;
            selection-color: white;
        }

        QLineEdit, QTextEdit, QPlainTextEdit, QListView, QTreeView {
            background-color: #1e1e1e;
            color: #dcdcdc;
            border: 1px solid #5a5a5a;
            selection-background-color: #409cff;
            selection-color: white;
        }

        QStatusBar {
            background-color: #353535;
            color: #cfcfcf;
        }
    )");
}

int main(int argc, char *argv[])
{
    // Initialize crash handler and Qt message logging
    Logger::init();

#ifdef QT_DEBUG
    //uncomment for verbose debug data in application output
    //qputenv("QT_FATAL_WARNINGS", "1");
    //qSetMessagePattern("Type: %{type}\nProduct Name: %{appname}\nFile: %{file}\nLine: %{line}\nMethod: %{function}\nThreadID: %{threadid}\nThreadPtr: %{qthreadptr}\nMessage: %{message}");
#endif

    SavvyLensApplication a(argc, argv);


    //Add a local path for Qt extensions, to allow for per-application extensions.
    a.addLibraryPath("plugins");

    //These things are used by QSettings to set up setting storage
    a.setOrganizationName("EVTV");
    a.setApplicationName("SavvyLens");
    a.setOrganizationDomain("evtv.me");

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings settings;

    QString localeString = settings.value("Main/Language").toString();
    if (localeString.isEmpty()) {
        QLocale sysLocale = QLocale::system();
        localeString = sysLocale.name();
        settings.setValue("Main/Language", localeString);
    }

    QTranslator translator;
    QLocale locale(localeString);
    QString lang = locale.name();
    QString shortLang = locale.name().left(2);

    if (QString translationDir = QCoreApplication::applicationDirPath() + "/translations";
        !translator.load("SavvyLens_" + lang, translationDir)) {
        translator.load("SavvyLens_" + shortLang, translationDir);
    }
    a.installTranslator(&translator);

    qInfo() << "Locale Value is:" << locale.name();

    int fontSize = settings.value("Main/FontSize", 9).toUInt();
    QFont sysFont = QFont();
    sysFont.setPointSize(fontSize);
    a.setFont(sysFont);

    ThemeManager::applyDarkTheme(a);

    a.mainWindow = new MainWindow();
    
    if (settings.value("MCP/Enable", true).toBool()) {
        MCPServer *mcpServer = new MCPServer(&a);
        QObject::connect(mcpServer, &MCPServer::clientCountChanged, a.mainWindow, &MainWindow::updateCopilotStatus);
        mcpServer->start(settings.value("MCP/Port", "8888").toInt());
    }
    
    a.mainWindow->show();
    
    if (argc > 1) {
        a.mainWindow->handleDroppedFile(QString(argv[1]));
    }

    int retCode = a.exec();

    delete a.mainWindow;
    a.mainWindow = nullptr;

    return retCode;
}