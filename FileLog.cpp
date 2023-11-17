//
// Created by nfriehs on 11/11/23.
//

#include "FileLog.h"

// Define static members
bool FileLog::logIsEnabled = true;
bool FileLog::isInitialized = false;
std::string FileLog::LOG_FILENAME = "CDCsvParser.log";
std::string FileLog::TIMESTAMP_FORMAT = "yyyy-MM-dd HH:mm:ss.SSS";
int FileLog::maxLogLevel = 2;
