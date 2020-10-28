// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "HAL/CriticalSection.h"
#include "Misc/DateTime.h"
#include "Modules/ModuleInterface.h"
#include "Templates/Atomic.h"

struct EDITORANALYTICSSESSION_API FEditorAnalyticsSession
{
	enum class EEventType : int32
	{
		Crashed = 0,
		GpuCrashed,
		Terminated,
		Shutdown,
		LogOut,
	};

	FString SessionId;

	FString AppId;
	FString AppVersion;
	FString UserId;

	FString ProjectName;
	FString ProjectID;
	FString ProjectDescription;
	FString ProjectVersion;
	FString EngineVersion;
	uint32 PlatformProcessID;
	uint32 MonitorProcessID; // Set to the CrashReportClientEditor PID when out-of-process reporting is used.
	TOptional<int32> ExitCode; // Set by CrashReportClientEditor after the Editor process exit when out-of-process reporting is used and reading the exit code is supported.
	TOptional<int32> MonitorExceptCode; // Set in CrashReportClientEditor process when an exception or an error is caught.
	TOptional<int32> MonitorExitCode; // Set in the Editor process when the Editor detects that CrashReportClientEditor process unexpectedly died.

	FDateTime StartupTimestamp; // Wall time (UTC) when the session started.
	FDateTime Timestamp; // Wall time (UTC) when the session was ended.
	FDateTime LastTickTimestamp; // Wall time (UTC) of the last engine tick recorded for the session.
	TOptional<FDateTime> DeathTimestamp; // Wall time (UTC) of the Editor death from CRC p.o.v. (when CRC observed the Editor death)
	volatile int32 SessionDuration = 0; // The session duration in seconds, computed using FPlatformTime::Seconds() rather than Timestamp - StartupTimestamp which can be affected by daylight saving.
	volatile int32 IdleSeconds = 0; // Can be updated from concurrent threads.
	volatile int32 Idle1Min = 0;
	volatile int32 Idle5Min = 0;
	volatile int32 Idle30Min = 0;
	volatile int32 TotalEditorInactivitySeconds = 0; // Account for user input and Editor process CPU usage. Add up gaps where the CPU was not used intensively and the user did not interact.
	FString CurrentUserActivity;
	TArray<FString> Plugins;
	float AverageFPS;

	uint64 SessionTickCount = 0; // Number of times the analytic session was ticked. Zero is the interesting value. If the Editor is hang during boot, some users may be prompt to kill it.
	uint64 EngineTickCount = 0;  // Number or times the engine was ticked.
	uint32 UserInteractionCount = 0; // Number of slate user interactions. Zero is the interesting value. If the Editor UI hang at start up, some users may be prompt to kill it.

	FString DesktopGPUAdapter;
	FString RenderingGPUAdapter;
	uint32 GPUVendorID;
	uint32 GPUDeviceID;
	uint32 GRHIDeviceRevision;
	FString GRHIAdapterInternalDriverVersion;
	FString GRHIAdapterUserDriverVersion;
	FString GRHIName;

	uint64 TotalPhysicalRAM;
	int32 CPUPhysicalCores;
	int32 CPULogicalCores;
	FString CPUVendor;
	FString CPUBrand;

	FString CommandLine;
	FString OSMajor;
	FString OSMinor;
	FString OSVersion;

	bool bIs64BitOS : 1;
	bool bCrashed : 1;
	bool bGPUCrashed : 1;
	bool bIsDebugger : 1;
	bool bWasEverDebugger : 1;
	bool bIsVanilla : 1;
	bool bIsTerminating : 1;
	bool bWasShutdown : 1;
	bool bIsUserLoggingOut: 1; // Also cover shutdown/reboot as logging out is part of the process. Logging out currently end up as an abnormal termination.
	bool bIsInPIE : 1;
	bool bIsInEnterprise : 1;
	bool bIsInVRMode : 1;
	bool bIsLowDriveSpace : 1;
	bool bIsCrcExeMissing: 1; // CrashReportClient executable is missing? To explain with MonitorProcessID would be zero.
	bool bIsDebuggerIgnored: 1; // True if GIgnoreDebugger is true.

	FEditorAnalyticsSession();

	/** 
	 * Save this session to stored values.
	 * @returns true if the session was successfully saved.
	 */
	bool Save();

	/**
	 *  Load a session with the given session ID from stored values.
	 * @returns true if the session was found and successfully loaded.
	 */
	bool Load(const FString& InSessionID);

	/**
	 * Delete the stored values of this session.
	 * Does not modify the actual session object.
	 * @returns true if the session was successfully deleted.
	 */
	bool Delete() const;

	/**
	 * Retrieve a list of session IDs that are currently stored locally.
	 * @returns true if the session IDs were successfully retrieved.
	 */
	static bool GetStoredSessionIDs(TArray<FString>& OutSessions);

	/**
	 * Read all stored sessions into the given array. This function only loads the sessions that are
	 * compatible with the current session format. Between releases, the format of the stored session
	 * can change. A newer engine will load sessions from a previous engine as long as the session format
	 * did not change.
	 * @returns true if the sessions were successfully loaded.
	 * @see CleanupOutdatedIncompatibleSessions()
	 */
	static bool LoadAllStoredSessions(TArray<FEditorAnalyticsSession>& OutSessions);

	/**
	 * Save the given session IDs to storage.
	 * @returns true if the session IDs were successfully saved.
	 */
	static bool SaveStoredSessionIDs(const TArray<FString>& InSessions);

	/**
	 * Delete sessions that are incompatible with the current session format and too old to be sent even if the Editor
	 * version corresponding to the format was used again. This is a maintenance function to prevent accumulating dead
	 * sessions over years.
	 * @param MaxAge Threshold to delete sessions that are older than MaxAge. Incompatible sessions are kept around for
	 *               some time because many users continue using older versions of the Editor that would be able to send
	 *               those sessions that a newer Editor version wouldn't.
	 */
	static void CleanupOutdatedIncompatibleSessions(const FTimespan& MaxAge);

	/**
	 * Try to acquire the local storage lock without blocking.
	 * @return true if the lock was acquired successfully.
	 */
	static bool TryLock() { return Lock(FTimespan::Zero()); }

	/**
	 * Acquire a lock for local storage.
	 * @returns true if the lock was acquired successfully.
	 */
	static bool Lock(FTimespan Timeout = FTimespan::Zero());

	/**
	 * Unlock the local storage.
	 */
	static void Unlock();

	/** Is the local storage already locked? */
	static bool IsLocked();

	/**
	 * Append an event to the session log. The function is meant to record concurrent events, especially during a crash
	 * with minimum contention. The logger appends and persists the events of interest locklessly on spot as opposed to
	 * overriding existing values in the key-store. Appending is better because it prevent dealing with event ordering
	 * on the spot (no synchronization needed) and will preserve more information.
	 *
	 * @note They key-store is not easily usable in a 'lockless' fashion. On Windows, the OS provides thread safe API
	 *       to modify the registry (add/update). On Mac/Linux, the key-store is a simple file and without synchronization,
	 *       concurrent writes will likely corrupt the file.
	 */
	void LogEvent(EEventType EventTpe, const FDateTime& Timestamp);

	/**
	 * Find the sessions for which the PlatformProcessID matches the specified session process id.
	 * @param InSessionProcessId The session with this process ID.
	 * @param[out] The found session if any (undefined if not found)
	 * @return True if a session for the specified process id was found, false otherwise.
	 */
	static bool FindSession(const uint32 InSessionProcessId, FEditorAnalyticsSession& OutSession);

	/**
	 * Persist the Editor exit code corresponding of this session in the key-store value.
	 * @return true if the exit code is persisted with the session data.
	 * @node This function should only be called by the out of process monitor when the process exit value is known.
	 */
	bool SaveExitCode(int32 ExitCode, const FDateTime& ApproximativeDeathTimeUtc);

	/**
	 * Set the exception code that caused the monitor application to crash. When the monitoring application raises
	 * and catches an unexpected exception, it tries to save the exception code it in the session before dying.
	 * @return true if the exception code is persisted with the session data.
	 * @note This is to diagnose the cases where CrashReportClientEditor send the summary event delayed and without
	 *       the Editor exit code.
	 */
	bool SaveMonitorExceptCode(int32 ExceptCode);

private:
	static FSystemWideCriticalSection* StoredValuesLock;

	/** 
	 * Has this session already been saved? 
	 * If not, then the first save will write out session invariant details such as hardware specs.
	 */
	bool bAlreadySaved : 1;
};

class FEditorAnalyticsSessionModule : public IModuleInterface
{

};