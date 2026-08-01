#include "logger.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <iostream>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#else
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <string.h>
#endif

// Global log file pointer
static QFile* g_logFile = nullptr;

static void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Format the message with a timestamp
    QString txt;
    QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    switch (type) {
        case QtDebugMsg:    txt = QString("[%1] DEBUG: %2").arg(time, msg); break;
        case QtInfoMsg:     txt = QString("[%1] INFO: %2").arg(time, msg); break;
        case QtWarningMsg:  txt = QString("[%1] WARNING: %2").arg(time, msg); break;
        case QtCriticalMsg: txt = QString("[%1] CRITICAL: %2").arg(time, msg); break;
        case QtFatalMsg:    txt = QString("[%1] FATAL: %2").arg(time, msg); break;
    }
    
    if (context.file && context.line > 0) {
        txt += QString(" (%1:%2)").arg(context.file).arg(context.line);
    }

    // Write to console
    std::cout << txt.toStdString() << std::endl;

    // Write to file
    if (g_logFile && g_logFile->isOpen()) {
        QTextStream out(g_logFile);
        out << txt << "\n";
        out.flush();
    }
    
    if (type == QtFatalMsg) {
        abort(); // Aborting will trigger our OS crash handler to write the stack trace
    }
}

#ifdef Q_OS_WIN
LONG WINAPI unhandledExceptionHandler(EXCEPTION_POINTERS *exceptionInfo)
{
    if (g_logFile && g_logFile->isOpen()) {
        QTextStream out(g_logFile);
        out << "\n\n=== APPLICATION CRASHED ===\n";
        out << "Exception Code: 0x" << QString::number(exceptionInfo->ExceptionRecord->ExceptionCode, 16) << "\n";
        out << "Stack Trace:\n";
        
        // Use dbghelp to dump the stack trace
        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        SymInitialize(process, NULL, TRUE);
        
        void* stack[100];
        unsigned short frames = CaptureStackBackTrace(0, 100, stack, NULL);
        SYMBOL_INFO* symbol = (SYMBOL_INFO *)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
        symbol->MaxNameLen = 255;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        
        for (unsigned short i = 0; i < frames; i++) {
            DWORD64 address = (DWORD64)(stack[i]);
            SymFromAddr(process, address, 0, symbol);
            out << i << ": " << symbol->Name << " - 0x" << QString::number(symbol->Address, 16) << "\n";
        }
        
        free(symbol);
        out.flush();
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
void posixSignalHandler(int sig)
{
    if (g_logFile && g_logFile->isOpen()) {
        int fd = g_logFile->handle();
        
        const char *msg1 = "\n\n=== APPLICATION CRASHED ===\nCaught signal: ";
        write(fd, msg1, strlen(msg1));
        
        // Simple async-signal-safe int to string
        char sigStr[16];
        int idx = 0;
        int temp = sig;
        if (temp == 0) {
            sigStr[idx++] = '0';
        } else {
            char rev[16];
            int r = 0;
            while (temp > 0) {
                rev[r++] = '0' + (temp % 10);
                temp /= 10;
            }
            while (r > 0) {
                sigStr[idx++] = rev[--r];
            }
        }
        sigStr[idx++] = '\n';
        write(fd, sigStr, idx);
        
        const char *msg2 = "Stack Trace:\n";
        write(fd, msg2, strlen(msg2));
        
        void *array[50];
        size_t size = backtrace(array, 50);
        // Write backtrace directly to the file descriptor
        backtrace_symbols_fd(array, size, fd);
    }
    
    // Reset signal to default and re-raise to actually terminate
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

void Logger::init()
{
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(logPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    // Use a timestamp to preserve older logs
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    QString logFilePath = dir.absoluteFilePath(QString("SavvyLens_%1.log").arg(timestamp));
    
    g_logFile = new QFile(logFilePath);
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(g_logFile);
        out << "=== SavvyLens Session Started: " << QDateTime::currentDateTime().toString() << " ===\n";
    }
    
    // Install Qt Message Handler
    qInstallMessageHandler(customMessageHandler);
    
    // Install OS crash handlers
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledExceptionHandler);
#else
    signal(SIGSEGV, posixSignalHandler);
    signal(SIGABRT, posixSignalHandler);
    signal(SIGFPE, posixSignalHandler);
    signal(SIGILL, posixSignalHandler);
#endif
}

QString Logger::getLogFilePath()
{
    return g_logFile ? g_logFile->fileName() : QString();
}
