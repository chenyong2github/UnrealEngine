// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "CrashReportClientDefines.h"
#include "Containers/Map.h"
#include "Templates/Tuple.h"

class FArchive;

/**
 * Write a mini log of important events such as the crash GUID(s) to attach to the editor summary analytic event. This is to diagnose abnormal terminations
 * of the Editor or crash in CRC itself. Each log entry is expected to be small and concise. Each instance creates a single log file identified by the CRC
 * process ID. When CRC (compiled with MTBF support) is about to exit, it reloads the existing log files and pass them to EditorSessionSummarySender. When
 * the summary sender is about to send a session, it check the session status, if an error occurred, it tries to match a mini-log and if the corresponding
 * log is found, it is attached to the session summary.
 */
class FDiagnosticLogger : public FOutputDevice
{
public:
	/** Return the logger. */
	static FDiagnosticLogger& Get();

	/** Returns whether the logger is enabled or not. When disabled, it doesn't log anything. */
	static bool IsEnabled()
	{
		// Only log if MTBF is enabled. In this mode, the mini-log created is attached to the Editor session summary to diagnose problems in CRC or help
		// figure out Editor abnormal terminations.
		return CRASH_REPORT_WITH_MTBF != 0;
	}

	//~ FOutputDevice interface
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, const double Time) override;
	virtual bool CanBeUsedOnAnyThread() const override;
	virtual bool CanBeUsedOnMultipleThreads() const override;

	/** Log a small events to help diagnose abnormal shutdown or bugs in CRC itself. The event text is expected to be short and concise. */
	void LogEvent(const TCHAR* Event, bool bForwardToUELog = true);

	/** Log a small events to help diagnose abnormal shutdown or bugs in CRC itself. The event text is expected to be short and concise. */
	void LogEvent(const FString& Event);

	/** Tick the logger to update CRC timestamp. The goal is to estimate the time of death of CRC. */
	void Tick();

	/** Close the file used by the diagnostic logger. */
	void Close();

	/** Load the diagnostic log file of this instance and all the other ones left by dead instances. */
	static TMap<uint32, TTuple<FString, FDateTime>> LoadAllLogs();

	/** Delete the diagnostic log file of this instance (if closed) and all other ones left by dead instances. */
	static void ClearAllLogs();

private:
	/** Maximum length of the diagnostic log. */
	static constexpr int32 MaxLogLen = 8 * 1024;

	FDiagnosticLogger();

	static const FString& GetLogDir();
	static const TCHAR* GetBaseFilename();
	static const FString& GetLogPathname();
	static uint32 GetLogProcessId(const FString& Filename);

	/** Append a log entry to the log buffer, rotate the buffer if full and flush it to file. */
	void AppendLog(const TCHAR* Event);

	/**
	 * Write the diagnostic info into the file.
	 * @param Timestamp The CRC timestamp, written at the beginning of the file.
	 * @param Info The diagnostic info to write in the file. If null only update the timestamp.
	 */
	void WriteToFile(const FDateTime& Timestamp, const FString* Info);

private:
	/** Serialize write access in the file */
	FCriticalSection LoggerLock;

	/** This is the string containing all the logged informations. */
	FString DiagnosticInfo;

	/** File used to write the diagnostic */
	TUniquePtr<FArchive> LogFileAr;

	/** The period at which the log timestamp is updated. During the first minute, timestamp every 5 seconds, then after the first minute, every minutes. */
	double NextTimestampUpdateTimeSeconds;

	/** Prevent a reentrency in the logger. */
	bool bReentrantGuard = false;
};
