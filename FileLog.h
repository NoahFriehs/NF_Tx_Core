//
// Created by nfriehs on 11/5/23.
//

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

    static void setMaxLogLevel(int maxLogLevel) {
        if (maxLogLevel < 0) {
            std::cerr << "Invalid max log level " << maxLogLevel << std::endl;
            return;
        }
        maxLogLevel = maxLogLevel;
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
            case 2:
                return "VERBOSE";
            case 3:
                return "DEBUG";
            case 4:
                return "INFO";
            case 5:
                return "WARN";
            case 6:
                return "ERROR";
            default:
                return "UNKNOWN";
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

    static void v(const std::string &tag, const std::string &message) {
        if (!logIsEnabled) return;
        if (2 < maxLogLevel) return;  // VERBOSE
        std::cout << "VERBOSE " << tag << ": " << message << std::endl;
        if (!isInitialized) return;
        writeToFile(2, tag, message);
    }

    static void d(const std::string &tag, const std::string &message) {
        if (!logIsEnabled) return;
        if (3 < maxLogLevel) return;  // DEBUG
        std::cout << "DEBUG " << tag << ": " << message << std::endl;
        if (!isInitialized) return;
        writeToFile(3, tag, message);
    }

    static void i(const std::string &tag, const std::string &message) {
        if (!logIsEnabled) return;
        if (4 < maxLogLevel) return;  // INFO
        std::cout << "INFO " << tag << ": " << message << std::endl;
        if (!isInitialized) return;
        writeToFile(4, tag, "Info: " + message);
    }

    static void w(const std::string &tag, const std::string &message) {
        if (!logIsEnabled) return;
        if (5 < maxLogLevel) return;  // WARN
        std::cerr << "WARNING " << tag << ": " << message << std::endl;
        if (!isInitialized) return;
        writeToFile(5, tag, "Warning: " + message);
    }

    static void e(const std::string &tag, const std::string &message) {
        if (!logIsEnabled) return;
        if (6 < maxLogLevel) return;  // ERROR
        std::cerr << "ERROR " << tag << ": " << message << std::endl;
        if (!isInitialized) return;
        writeToFile(6, tag, "Error: " + message);
    }


};


#endif //NF_TX_CORE_FILELOG_H
