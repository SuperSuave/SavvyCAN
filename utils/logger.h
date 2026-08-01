#ifndef LOGGER_H
#define LOGGER_H

#include <QString>

class Logger
{
public:
    // Initializes the log file, installs the Qt message handler, and registers crash signal handlers.
    static void init();
    
    // Returns the path to the current log file
    static QString getLogFilePath();
};

#endif // LOGGER_H
