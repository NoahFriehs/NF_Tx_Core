
#ifndef NF_TX_CORE_FILELOG_H
#define NF_TX_CORE_FILELOG_H


#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <string>
#include <sstream>

class FileLog {
private:
    static bool logIsEnabled;
    static bool isInitialized;
    static std::string LOG_FILENAME;
    static std::string TIMESTAMP_FORMAT;
    static int maxLogLevel;

    static std::string dateTimeFormatter() {
        auto now = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count() % 1000;

        char buffer[80];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);

        std::stringstream ss;
        ss << buffer << "." << std::setfill('0') << std::setw(3) << milliseconds;

        return ss.str();
    }


    static void createLogFileIfNeeded() {
        std::ifstream logFile(LOG_FILENAME);
        if (!logFile) {
            // File does not exist, create it.
            std::ofstream createFile(LOG_FILENAME);
            createFile.close();
        } else {
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(logFile, line)) {
                lines.push_back(line);
            }
            logFile.close();

            if (lines.size() > 1000) {
                // Truncate the file by keeping the most recent 500 lines.
                std::ofstream logFileOut(LOG_FILENAME, std::ios::trunc);
                if (logFileOut) {
                    for (std::size_t i = lines.size() - 500; i < lines.size(); ++i) {
                        logFileOut << lines[i] << std::endl;
                    }
                    logFileOut.close();
                }
            }
        }
    }

public:
    //! Log level values. These mirror the `android.util.Log` constants so a
    //! level chosen on the Kotlin side can cross the JNI boundary unchanged
    //! (see `library.cpp`, which inits the core with LOG_DEBUG).
    //!
    //! The level passed to `init`/`setMaxLogLevel` works like the
    //! *min* level of `android.util.Log` (log this level and above), despite
    //! the legacy "max" name: `LOG_VERBOSE` (2) logs everything,
    //! `LOG_ERROR` (6) only errors.
    static constexpr int LOG_VERBOSE = 2;
    static constexpr int LOG_DEBUG = 3;
    static constexpr int LOG_INFO = 4;
    static constexpr int LOG_WARN = 5;
    static constexpr int LOG_ERROR = 6;

    //! Initialize the log
    static void
    init(const std::string &logFilename = "", bool logEnabled = true, int maxLogLevel_ = -1) {
        logIsEnabled = logEnabled;
        if (!logIsEnabled) return;
        LOG_FILENAME = logFilename.empty() ? LOG_FILENAME : logFilename;
        maxLogLevel = maxLogLevel_ < 0 ? maxLogLevel : maxLogLevel_;
        createLogFileIfNeeded();
        isInitialized = true;
        std::cout << "Initialized" << std::endl;
        i("FileLog", "Initialized");
    }

    //! Set the min log level (legacy name, see LOG_VERBOSE). The level and
    //! everything more severe is logged; more verbose levels are dropped.
    static void setMaxLogLevel(int maxLogLevel) {
        if (maxLogLevel < LOG_VERBOSE || maxLogLevel > LOG_ERROR) {
            std::cerr << "Invalid log level " << maxLogLevel << std::endl;
            return;
        }
        if (maxLogLevel != FileLog::maxLogLevel) {
            i("FileLog", "Max log level set to " + std::to_string(maxLogLevel));
        }
        FileLog::maxLogLevel = maxLogLevel;
    }

    static int getMaxLogLevel() {
        return maxLogLevel;
    }

    static void setLogEnabled(bool logEnabled) {
        logIsEnabled = logEnabled;
    }

    static bool getLogEnabled() {
        return logIsEnabled;
    }

    static std::string getLogFilename() {
        return LOG_FILENAME;
    }

    static void setLogFilename(const std::string &logFilename) {
        LOG_FILENAME = logFilename;
        createLogFileIfNeeded();
    }

    static int getLogSize() {
        std::ifstream logFile(LOG_FILENAME);
        if (!logFile) {
            std::cerr << "Error: Could not open log file" << std::endl;
            return 0;
        }

        int lineCount = 0;
        std::string line;
        while (std::getline(logFile, line)) {
            lineCount++;
        }

        return lineCount;
    }

    static std::string getLog() {
        std::ifstream logFile(LOG_FILENAME);
        if (!logFile) {
            std::cerr << "Error: Could not open log file" << std::endl;
            return "";
        }

        std::string logContent((std::istreambuf_iterator<char>(logFile)),
                               std::istreambuf_iterator<char>());
        return logContent;
    }

    static void clearLog() {
        std::ofstream logFile(LOG_FILENAME);
        if (!logFile) {
            std::cerr << "Error: Could not open log file" << std::endl;
            return;
        }

        logFile.close();
    }

    static std::string logLevelToString(int logLevel) {
        switch (logLevel) {
            case LOG_VERBOSE: return "VERBOSE";
            case LOG_DEBUG: return "DEBUG";
            case LOG_INFO: return "INFO";
            case LOG_WARN: return "WARN";
            case LOG_ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }

    static void writeToFile(int logLevel, const std::string &tag, const std::string &message) {
        std::ofstream logFile(LOG_FILENAME, std::ios::app);
        if (!logFile) {
            std::cerr << "Error: Could not open log file" << std::endl;
            return;
        }

        std::string timeStamp = dateTimeFormatter();
        std::string logLevelString = logLevelToString(logLevel);
        logFile << timeStamp << " " << logLevelString << " " << tag << ": " << message << std::endl;
    }

    //! Log a message with the given tag in the VERBOSE log level
    static void v(const std::string &tag, const std::string &message) {
        if (!logIsEnabled) return;
        if (LOG_VERBOSE < maxLogLevel) return;
        std::cout << "VERBOSE " << tag << ": " << message << std::endl;
        if (!isInitialized) return;
        writeToFile(LOG_VERBOSE, tag, message);
    }

    //! Log a message with the given tag in the DEBUG log level
    static void d(const std::string &tag, const std::string &message) {
        if (!logIsEnabled) return;
        if (LOG_DEBUG < maxLogLevel) return;
        std::cout << "DEBUG " << tag << ": " << message << std::endl;
        if (!isInitialized) return;
        writeToFile(LOG_DEBUG, tag, message);
    }

    //! Log a message with the given tag in the INFO log level
    static void i(const std::string &tag, const std::string &message) {
        if (!logIsEnabled) return;
        if (LOG_INFO < maxLogLevel) return;
        std::cout << "INFO " << tag << ": " << message << std::endl;
        if (!isInitialized) return;
        writeToFile(LOG_INFO, tag, "Info: " + message);
    }

    //! Log a message with the given tag in the WARN log level
    static void w(const std::string &tag, const std::string &message) {
        if (!logIsEnabled) return;
        if (LOG_WARN < maxLogLevel) return;
        std::cerr << "WARNING " << tag << ": " << message << std::endl;
        if (!isInitialized) return;
        writeToFile(LOG_WARN, tag, "Warning: " + message);
    }

    //! Log a message with the given tag in the ERROR log level
    static void e(const std::string &tag, const std::string &message) {
        if (!logIsEnabled) return;
        if (LOG_ERROR < maxLogLevel) return;
        std::cerr << "ERROR " << tag << ": " << message << std::endl;
        if (!isInitialized) return;
        writeToFile(LOG_ERROR, tag, "Error: " + message);
    }


};


#endif //NF_TX_CORE_FILELOG_H
