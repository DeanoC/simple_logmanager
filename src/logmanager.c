#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_thread/thread.h"

#include "al2o3_os/file.h"
#include "al2o3_os/filesystem.h"
#include "al2o3_os/thread.h"
#include "utils_simple_logmanager/logmanager.h"

#include <stdio.h> // sprintf
#include <time.h>

static void SimpleLogManager_ErrorMsg(char const *file, int line, const char *function, char const *msg);
static void SimpleLogManager_WarningMsg(char const *file, int line, const char *function, char const *msg);
static void SimpleLogManager_InfoMsg(char const *file, int line, const char *function, char const *msg);
static void SimpleLogManager_DebugMsg(char const *file, int line, const char *function, char const *msg);
static void SimpleLogManager_FailedAssert(char const *file, int line, const char *function, char const *msg);

// outString must be enough for a full time string (256 min)
static void GetTimeStamp(char * outString ) {
	time_t sysTime;
	time(&sysTime);
	strcpy(outString,ctime(&sysTime));
	size_t const timeLen = strlen(outString);
	ASSERT( timeLen < 256);

	for (size_t i = 0u; i < timeLen; ++i) {
		if( outString[i] == 10 || outString[i] == 14) {
			outString[i] = ' ';
		}
	}
}

typedef struct SimpleLogManager {
	Thread_Mutex logMutex;
	Os_FileHandle logFile;

	bool recordTimestamp;

	bool infoFileLineQuiet;
	bool warningFileLineQuiet;
	bool errorFileLineQuiet;

	bool infoQuiet;
	bool warningQuiet;
	bool errorQuiet;
	bool failedAssertQuiet;
	bool debugMsgQuiet;

	AL2O3_Logger_t oldLogger;
	AL2O3_Logger_t logger;

	char lastMessage[1024 * 64];
	char filePath[2048];

} SimpleLogManager;

SimpleLogManager* SimpleLogManagerSingleton;

static void SimpleLogManager_Msg(SimpleLogManager_Handle handle,
																 bool fileLineQuiet,
																 char const *level,
																 char const *file,
																 int line,
																 const char *function,
																 char const *msg) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;

	char buffer[sizeof(logManager->lastMessage)]; // TODO potential buffer overrun!
	if (file != NULL && fileLineQuiet == false) {
		sprintf(buffer, "%s: %s(%i) - %s: %s\n", level, file, line, function, msg);
	} else {
		sprintf(buffer, "%s: %s\n", level, msg);
	}

	if (logManager->recordTimestamp) {
		char buffer2[sizeof(logManager->lastMessage) + 256 + 2]; // TODO potential buffer overrun!
		char timeBuf[256];
		GetTimeStamp(timeBuf);
		sprintf(buffer2, "[%s] %s", timeBuf, buffer);
		strcpy(buffer, buffer2);
	}

	// we always use a mutex to protect the output. It would be nice
	// to short circuit for main thread only output but doing that safe
	// is involved. If this proves to be a performance bottleneck look
	// in smarter system.
	Thread_MutexAcquire(&logManager->logMutex);
	{
		strcpy(logManager->lastMessage, buffer);

		AL2O3_OutputDebug(buffer);

		if (Os_FileIsOpen(logManager->logFile)) {
			Os_FileWrite(logManager->logFile, buffer, strlen(buffer));
			Os_FileFlush(logManager->logFile);
		}
	}
	Thread_MutexRelease(&logManager->logMutex);
}

AL2O3_EXTERN_C SimpleLogManager_Handle SimpleLogManager_Alloc() {
	ASSERT(SimpleLogManagerSingleton == NULL);

	SimpleLogManager* logManager = (SimpleLogManager*) MEMORY_CALLOC(1, sizeof(SimpleLogManager));
	if(logManager == NULL) return NULL;

	if( Thread_MutexCreate(&logManager->logMutex) == false) {
		MEMORY_FREE(logManager);
		return NULL;
	};
	
	char const logFilename[] = "log.log";
	Os_GetCurrentDir(logManager->filePath, sizeof(logManager->filePath));
	ASSERT(strlen(logManager->filePath) + sizeof(logFilename) < sizeof(logManager->filePath));
	strcat(logManager->filePath, logFilename);
	logManager->logFile = Os_FileOpen(logManager->filePath, Os_FM_Write);

	logManager->recordTimestamp = false;
	logManager->infoQuiet = false;
	logManager->warningQuiet = false;
	logManager->errorQuiet = false;
	logManager->failedAssertQuiet = false;
	logManager->debugMsgQuiet = false;

	logManager->infoFileLineQuiet = true;
	logManager->warningFileLineQuiet = false;
	logManager->errorFileLineQuiet = false;

	logManager->logger.errorMsg = &SimpleLogManager_ErrorMsg;
	logManager->logger.warningMsg = &SimpleLogManager_WarningMsg;
	logManager->logger.infoMsg = &SimpleLogManager_InfoMsg;
	logManager->logger.debugMsg = &SimpleLogManager_DebugMsg;
	logManager->logger.failedAssert = &SimpleLogManager_FailedAssert;
	memcpy(&logManager->oldLogger, &AL2O3_Logger, sizeof(AL2O3_Logger_t));
	memcpy(&AL2O3_Logger, &logManager->logger, sizeof(AL2O3_Logger_t));
	SimpleLogManagerSingleton = logManager;

	return logManager;
}

AL2O3_EXTERN_C void SimpleLogManager_Free(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;

	SimpleLogManager_CloseLogFile(handle);
	memcpy(&AL2O3_Logger, &logManager->oldLogger, sizeof(AL2O3_Logger_t));
	Thread_MutexDestroy(&logManager->logMutex);
	MEMORY_FREE(logManager);

	SimpleLogManagerSingleton = NULL;
}


AL2O3_EXTERN_C void SimpleLogManager_UseFileForLog(SimpleLogManager_Handle handle, char const* fileName) {

	ASSERT(handle != NULL);
	ASSERT(fileName != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;

	// check if we are opening the same file that is already open and do nada if so
	if (strcmp(fileName, logManager->filePath) == 0) {
		return;
	}

	if( Os_FileIsOpen(logManager->logFile)) {
			SimpleLogManager_CloseLogFile(handle);
	}

	logManager->logFile = Os_FileOpen(fileName, Os_FM_Write);
	if (logManager->logFile != 0 && Os_FileIsOpen(logManager->logFile)) {
		strcpy(logManager->filePath, fileName);
		LOGINFO("Opened log file %s", fileName);
	} else {
		LOGERROR("Failed to create log file %s", fileName);
	}
}

AL2O3_EXTERN_C void SimpleLogManager_CloseLogFile(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;
	if( Os_FileIsOpen(logManager->logFile)) {
		Os_FileClose(logManager->logFile);
		logManager->logFile = NULL;
		logManager->filePath[0] = 0;
	}
}

AL2O3_EXTERN_C void SimpleLogManager_SetRecordTimeStamp(SimpleLogManager_Handle handle, bool enable) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;
	logManager->recordTimestamp = enable;
}

AL2O3_EXTERN_C bool SimpleLogManager_GetRecordTimeStamp(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;
	return logManager->recordTimestamp;
}

AL2O3_EXTERN_C void SimpleLogManager_SetInfoFileLineQuiet(SimpleLogManager_Handle handle, bool enable) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;
	logManager->infoFileLineQuiet = enable;

}

AL2O3_EXTERN_C bool SimpleLogManager_IsInfoFileLineQuiet(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;
	return logManager->infoFileLineQuiet;
}

AL2O3_EXTERN_C void SimpleLogManager_SetWarningFileLineQuiet(SimpleLogManager_Handle handle, bool enable) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;
	logManager->warningFileLineQuiet = enable;

}

AL2O3_EXTERN_C bool SimpleLogManager_IsWarningFileLineQuiet(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;
	return logManager->warningFileLineQuiet;
}

AL2O3_EXTERN_C void SimpleLogManager_SetErrorFileLineQuiet(SimpleLogManager_Handle handle, bool enable) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;
	logManager->errorFileLineQuiet = enable;

}

AL2O3_EXTERN_C bool SimpleLogManager_IsErrorFileLineQuiet(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;
	return logManager->errorFileLineQuiet;
}

AL2O3_EXTERN_C void SimpleLogManager_SetInfoQuiet(SimpleLogManager_Handle handle, bool enable) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;
	logManager->infoQuiet = enable;
}

AL2O3_EXTERN_C bool SimpleLogManager_IsInfoQuiet(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager *logManager = (SimpleLogManager *) handle;
	return logManager->infoQuiet;
}

AL2O3_EXTERN_C void SimpleLogManager_SetWarningQuiet(SimpleLogManager_Handle handle, bool enable) {
	ASSERT(handle != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;
	logManager->warningQuiet = enable;
}

AL2O3_EXTERN_C bool SimpleLogManager_IsWarningQuiet(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;
	return logManager->warningQuiet;
}

AL2O3_EXTERN_C void SimpleLogManager_SetErrorQuiet(SimpleLogManager_Handle handle, bool enable) {
	ASSERT(handle != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;
	logManager->errorQuiet  = enable;
}

AL2O3_EXTERN_C bool SimpleLogManager_IsErrorQuiet(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;
	return logManager->errorQuiet;
}

AL2O3_EXTERN_C void SimpleLogManager_SetFailedAssertQuiet(SimpleLogManager_Handle handle, bool enable) {
	ASSERT(handle != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;
	logManager->failedAssertQuiet = enable;
}

AL2O3_EXTERN_C bool SimpleLogManager_IsFailedAssertQuiet(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;
	return logManager->failedAssertQuiet;
}

AL2O3_EXTERN_C void SimpleLogManager_SetDebugMsgQuiet(SimpleLogManager_Handle handle, bool enable) {
	ASSERT(handle != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;
	logManager->debugMsgQuiet = enable;
}

AL2O3_EXTERN_C bool SimpleLogManager_IsDebugMsgQuiet(SimpleLogManager_Handle handle) {
	ASSERT(handle != NULL);
	SimpleLogManager* logManager = (SimpleLogManager*)handle;
	return logManager->debugMsgQuiet;
}

void SimpleLogManager_InfoMsg(char const *file, int line, const char *function, char const *msg) {
	ASSERT(SimpleLogManagerSingleton);
	if (SimpleLogManager_IsInfoQuiet(SimpleLogManagerSingleton)) {
		return;
	}

	SimpleLogManager_Msg(SimpleLogManagerSingleton,
											 SimpleLogManagerSingleton->infoFileLineQuiet,
											 "INFO ",
											 file,
											 line,
											 function,
											 msg);
}

void SimpleLogManager_WarningMsg(char const *file, int line, const char *function, char const *msg) {
	ASSERT(SimpleLogManagerSingleton);
	if (SimpleLogManager_IsWarningQuiet(SimpleLogManagerSingleton)) {
		return;
	}

	SimpleLogManager_Msg(SimpleLogManagerSingleton,
											 SimpleLogManagerSingleton->warningFileLineQuiet,
											 "WARN ",
											 file,
											 line,
											 function,
											 msg);
}

void SimpleLogManager_ErrorMsg(char const *file, int line, const char *function, char const *msg) {
	ASSERT(SimpleLogManagerSingleton);
	if (SimpleLogManager_IsErrorQuiet(SimpleLogManagerSingleton)) {
		return;
	}

	SimpleLogManager_Msg(SimpleLogManagerSingleton,
											 SimpleLogManagerSingleton->errorFileLineQuiet,
											 "ERROR",
											 file,
											 line,
											 function,
											 msg);
	AL2O3_DEBUG_BREAK();
}

void SimpleLogManager_DebugMsg(char const *file, int line, const char *function, char const *msg) {
	ASSERT(SimpleLogManagerSingleton);
	if (SimpleLogManager_IsDebugMsgQuiet(SimpleLogManagerSingleton)) {
		return;
	}

	SimpleLogManager_Msg(SimpleLogManagerSingleton, false, "DEBUG", file, line, function, msg);
}

void SimpleLogManager_FailedAssert(char const *file, int line, const char *function, char const *msg) {
	ASSERT(SimpleLogManagerSingleton);
	if (SimpleLogManager_IsFailedAssertQuiet(SimpleLogManagerSingleton)) {
		return;
	}

	// asserts always want file line etc. even if turned off
	SimpleLogManager_Msg(SimpleLogManagerSingleton, false, "ASSERT", file, line, function, msg);
	AL2O3_DEBUG_BREAK();
}