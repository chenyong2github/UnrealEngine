// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SessionMonitorCommon.h"

enum class EAppCrashAction
{
	None,
	StopSession,
	RestartApp,
	RestartSession
};

struct FAppConfig
{
	/**
	 * Json field: "name" : string
	 * REQUIRED.
	 * Application name.
	 * It's used to identify the application in the logs, events, etc 
	 */
	std::string Name;
	/**
	 * REQUIRED.
	 * Json field: "executable" : string
	 * Path to the executable file.
	 * If it's a relative path, it will be resolved relative to the SessionMonitor's executable path
	 */
	std::string Exe;

	/**
	 * OPTIONAL
	 * Json field: "parameters" : string
	 * Parameters to pass to the application
	 */
	std::string Params;

	/**
	* OPTIONAL
	* Json field: "working_directory" : string
	* Working directory for the application. If not specified it will assume
	* the application executable file's directory
	*/
	std::string WorkingDirectory;

	/**
	* OPTIONAL
	* Json field: "initial_timeout" : integer
	* Heartbeat timeout in milliseconds. If the application doesn't report back to the SessionMonitor
	* with a 'heartbeat' message within this time window, it will be killed by the SessionMonitor.
	*/
	int InitialTimeoutMs = 1000 * 10;
	
	/**
	 * OPTIONAL
	 * Json field: "shutdown_timeout" : integer
	 * Time allowed for a graceful shutdown (in milliseconds). If the application doesn't
	 * shutdown within this time window, it will be killed by the SessionMonitor
	 */
	int ShutdownTimeoutMs = 1000 * 10;

	/**
	 * OPTIONAL
	 * Json field: "oncrash" : string
	 * What action to take if the application crashes. Valid options are:
	 * "None" : No action is taken
	 * "StopSession" : The entire session will be stopped
	 * "RestartApp" : Restart just this app
	 * "RestartSession" : Restart the entire session
	 */
	EAppCrashAction OnCrashAction = EAppCrashAction::None;

	/**
	 * OPTIONAL
	 * Json field: "monitored" : boolean
	 * If true (default), the application will be actively monitored by the SessionMonitor,
	 * and as such, the SessionMonitor will expect the 'heartbeat' messages from the application,
	 * Also, the application will have to able to shutdown gracefully after a 'exit' message
	 * If false, the application will be launched in unmonitored mode, and no messages will be expected from the application.
	 */
	bool bMonitored = true;

	/**
	 * OPTIONAL
	 * Json field: "parameter_prefix" : string
	 * Prefix for the 'PixelStreamingSessionMonitorPort=XXXX" parameter passed to the application by the
	 * SessionMonitor.
	 */
	std::string ParameterPrefix = "-";
};

std::vector<FAppConfig> ReadConfig(const std::string& ConfigFilename);
