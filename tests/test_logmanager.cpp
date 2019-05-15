#include "al2o3_platform/platform.h"
#include "al2o3_catch2/catch2.hpp"
#include "utils_simple_logmanager/logmanager.h"
#include "al2o3_os/file.h"
#include "al2o3_os/filesystem.h"

TEST_CASE("Alloc/Free", "[SimpleLogManager]") {
	auto slm = SimpleLogManager_Alloc();
	REQUIRE(slm);
	SimpleLogManager_Free(slm);
}

TEST_CASE("Quiet settings take", "[SimpleLogManager]") {
	auto slm = SimpleLogManager_Alloc();
	REQUIRE(slm);
	// defaults
	REQUIRE(!SimpleLogManager_IsFailedAssertQuiet(slm));
	REQUIRE(!SimpleLogManager_IsInfoQuiet(slm));
	REQUIRE(!SimpleLogManager_IsDebugMsgQuiet(slm));
	REQUIRE(!SimpleLogManager_IsErrorQuiet(slm));
	REQUIRE(!SimpleLogManager_IsWarningQuiet(slm));
	REQUIRE(SimpleLogManager_IsFileLineQuiet(slm));
	SimpleLogManager_SetFailedAssertQuiet(slm, true);
	SimpleLogManager_SetInfoQuiet(slm, true);
	SimpleLogManager_SetDebugMsgQuiet(slm, true);
	SimpleLogManager_SetErrorQuiet(slm, true);
	SimpleLogManager_SetWarningQuiet(slm, true);
	SimpleLogManager_SetFileLineQuiet(slm, false);
	REQUIRE(SimpleLogManager_IsFailedAssertQuiet(slm));
	REQUIRE(SimpleLogManager_IsInfoQuiet(slm));
	REQUIRE(SimpleLogManager_IsDebugMsgQuiet(slm));
	REQUIRE(SimpleLogManager_IsErrorQuiet(slm));
	REQUIRE(SimpleLogManager_IsWarningQuiet(slm));
	REQUIRE(!SimpleLogManager_IsFileLineQuiet(slm));

	SimpleLogManager_Free(slm);
}

TEST_CASE("Default log file OK", "[SimpleLogManager]") {
	char filePath[2048];
	char const logFilename[] = "log.log";
	Os_GetCurrentDir(filePath, sizeof(filePath));
	ASSERT( strlen(filePath) + sizeof(logFilename) < sizeof(filePath));
	strcat(filePath, logFilename);

	// delete any old log first
	if( Os_FileExists(filePath) ) {
		Os_FileDelete(filePath);
	}

	auto slm = SimpleLogManager_Alloc();
	REQUIRE(slm);
	LOGINFO("test default");

	REQUIRE(Os_FileExists(filePath));

	SimpleLogManager_Free(slm);
}

TEST_CASE("Custom log file OK", "[SimpleLogManager]") {
	char filePath[2048];
	char const logFilename[] = "custom_test.log";
	Os_GetCurrentDir(filePath, sizeof(filePath));
	ASSERT( strlen(filePath) + sizeof(logFilename) < sizeof(filePath));
	strcat(filePath, logFilename);

	// delete any old log first
	if( Os_FileExists(filePath) ) {
		Os_FileDelete(filePath);
	}

	auto slm = SimpleLogManager_Alloc();
	REQUIRE(slm);
	LOGINFO("test default");

	SimpleLogManager_UseFileForLog(slm, filePath);
	LOGINFO("test custom");

	REQUIRE(Os_FileExists(filePath));

	SimpleLogManager_Free(slm);
}