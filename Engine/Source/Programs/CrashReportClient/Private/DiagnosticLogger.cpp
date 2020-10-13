// Copyright Epic Games, Inc. All Rights Reserved.

#include "DiagnosticLogger.h"
#include "Serialization/Archive.h"
#include "Logging/LogMacros.h"
#include "Serialization/Archive.h"
#include "Templates/UnrealTemplate.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(CrashReportClientDiagnosticLog, Log, All)


#if PLATFORM_WINDOWS && CRASH_REPORT_WITH_MTBF

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows.h"

/** Handle windows messages. */
LRESULT CALLBACK CrashReportWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// wParam is true if the user session is going away (and CRC is going to die)
	if (uMsg == WM_ENDSESSION && wParam == TRUE)
	{
		FDiagnosticLogger::Get().LogEvent(TEXT("CRC/EndSession"));
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

namespace DiagnosticLoggerUtils
{
	HWND Hwnd = NULL;

	/** Create a hidden Windows to intercept WM_ messages, especially the WM_ENDSESSION. */
	void InitPlatformSpecific()
	{
		// Register the window class.
		const wchar_t CLASS_NAME[] = L"CRC Window Message Interceptor";

		WNDCLASS wc = { };
		wc.lpfnWndProc = CrashReportWindowProc;
		wc.hInstance = hInstance;
		wc.lpszClassName = CLASS_NAME;

		RegisterClass(&wc);

		// Create a window to capture WM_ENDSESSION message (so that we can detect when CRC fails because the user is logging off/shutting down/restarting)
		Hwnd = CreateWindowEx(
			0,                              // Optional window styles.
			CLASS_NAME,                     // Window class
			L"CRC Message Loop Wnd",        // Window text
			WS_OVERLAPPEDWINDOW,            // Window style
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, // Size and position
			NULL,         // Parent window
			NULL,         // Menu
			hInstance,    // Instance handle
			NULL          // Additional application data
		);
	}

	/** Pump the message from the hidden Windows. */
	void TickPlatformSpecific()
	{
		if (Hwnd != NULL)
		{
			// Pump the messages.
			MSG Msg = { };
			while (::PeekMessage(&Msg, NULL, 0, 0, PM_REMOVE))
			{
				::TranslateMessage(&Msg);
				::DispatchMessage(&Msg);
			}
		}
	}
} // namespace DiagnosticLoggerUtils

#else

namespace DiagnosticLoggerUtils
{
	void InitPlatformSpecific(){}
	void TickPlatformSpecific(){}
} // namespace DiagnosticLoggerUtils

#endif


FDiagnosticLogger::FDiagnosticLogger()
	: NextTimestampUpdateTimeSeconds(FPlatformTime::Seconds())
{
	if (IsEnabled())
	{
		DiagnosticLoggerUtils::InitPlatformSpecific();

		// Ensure the Log directory exists.
		IFileManager::Get().MakeDirectory(*GetLogDir(), /*Tree*/true);

		// Delete the previous file (if any was left).
		IFileManager::Get().Delete(*GetLogPathname(), /*bRequireExits*/false);

		// Reserve the memory for the log string.
		DiagnosticInfo.Reset(MaxLogLen);

		// Open the file.
		LogFileAr.Reset(IFileManager::Get().CreateFileWriter(*GetLogPathname(), FILEWRITE_AllowRead));
	}
}

FDiagnosticLogger& FDiagnosticLogger::Get()
{
	static FDiagnosticLogger Instance;
	return Instance;
}

void FDiagnosticLogger::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	// Log the errors, especially the failed 'check()' with the callstack/message.
	if (Verbosity == ELogVerbosity::Error)
	{
		// Log but don't forward to UE logging system. The log is already originate from the logging system.
		LogEvent(TEXT("CRC/Error"), /*bForwardToUELog*/false);
		LogEvent(V, /*bForwardToUELog*/false);
	}
}

void FDiagnosticLogger::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, const double Time)
{
	Serialize(V, Verbosity, Category);
}

bool FDiagnosticLogger::CanBeUsedOnAnyThread() const
{
	return true;
}

bool FDiagnosticLogger::CanBeUsedOnMultipleThreads() const
{
	return true;
}

void FDiagnosticLogger::LogEvent(const TCHAR* Event, bool bForwardToUELog)
{
	if (IsEnabled())
	{
		FScopeLock ScopedLock(&LoggerLock);
		TGuardValue<bool> ReentrantGuard(bReentrantGuard, true);
		if (*ReentrantGuard) // Read the old value.
		{
			return; // Prevent renentrant logging.
		}

		AppendLog(Event);

		// Prevent error logs coming from the logging system to be duplicated.
		if (bForwardToUELog)
		{
			UE_LOG(CrashReportClientDiagnosticLog, Log, TEXT("%s"), Event);
		}
	}
}

void FDiagnosticLogger::LogEvent(const FString& Event)
{
	LogEvent(*Event);
}

void FDiagnosticLogger::Tick()
{
	if (IsEnabled())
	{
		// Tick the platform specific (basically to check if CRC is going to die).
		DiagnosticLoggerUtils::TickPlatformSpecific();

		FScopeLock ScopedLock(&LoggerLock);

		// To prevent LogEvent() executing if WriteToFile() below ends up firing an error log (like a disk is full error message logged).
		TGuardValue<bool> ReentrantGuard(bReentrantGuard, true);

		const double CurrTimeSecs = FPlatformTime::Seconds();
		if (CurrTimeSecs >= NextTimestampUpdateTimeSeconds)
		{
			// Update the timestamp every n seconds.
			constexpr double TimestampingPeriodSecs = 5;
			NextTimestampUpdateTimeSeconds = CurrTimeSecs + TimestampingPeriodSecs;

			// Timestamp the log.
			WriteToFile(FDateTime::UtcNow(), nullptr);
		}
	}
}

void FDiagnosticLogger::Close()
{
	if (LogFileAr)
	{
		LogFileAr->Close();
		LogFileAr.Reset();
	}
}

TMap<uint32, TTuple<FString, FDateTime>> FDiagnosticLogger::LoadAllLogs()
{
	TMap<uint32, TTuple<FString, FDateTime>> Logs;

	IFileManager::Get().IterateDirectory(*GetLogDir(), [&Logs](const TCHAR* Pathname, bool bIsDir)
	{
		if (!bIsDir)
		{
			FString Filename = FPaths::GetCleanFilename(Pathname);
			if (Filename.StartsWith(GetBaseFilename()) && Filename.EndsWith(TEXT(".log")))
			{
				uint32 ProcessID = GetLogProcessId(Filename);
				if (ProcessID == FPlatformProcess::GetCurrentProcessId() || !FPlatformProcess::IsApplicationRunning(ProcessID)) // Don't load the log of another running CrashReportClient.
				{
					int64 UtcUnixTimestamp = 0;
					FString MonitorLog;
					TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(Pathname));
					if (Ar)
					{
						*Ar << UtcUnixTimestamp;
						*Ar << MonitorLog;
						Logs.Emplace(ProcessID, MakeTuple(MoveTemp(MonitorLog), FDateTime::FromUnixTimestamp(UtcUnixTimestamp)));
					}
				}
			}
		}
		return true; // Continue iterating the directory.
	});

	return Logs;
}

void FDiagnosticLogger::ClearAllLogs()
{
	IFileManager::Get().IterateDirectory(*GetLogDir(), [](const TCHAR* Pathname, bool bIsDir)
	{
		if (!bIsDir)
		{
			FString Filename = FPaths::GetCleanFilename(Pathname);
			if (Filename.StartsWith(GetBaseFilename()) && Filename.EndsWith(TEXT(".log")))
			{
				uint32 ProcessID = GetLogProcessId(Filename);
				if (ProcessID == FPlatformProcess::GetCurrentProcessId() || !FPlatformProcess::IsApplicationRunning(ProcessID)) // Don't delete the log of another running CrashReportClient.
				{
					IFileManager::Get().Delete(Pathname);
				}
			}
		}
		return true; // Continue iterating the directory.
	});
}

const FString& FDiagnosticLogger::GetLogDir()
{
	static FString LogDir(FPlatformProcess::UserTempDir()); // This folder (and API) doesn't rely on the engine being initialized and can be use very early.
	return LogDir;
}

const TCHAR* FDiagnosticLogger::GetBaseFilename()
{
	return TEXT("UnrealCrcMiniLogV2");
}

const FString& FDiagnosticLogger::GetLogPathname()
{
	static FString LogPathname(GetLogDir() / FString::Printf(TEXT("%s_%s.log"), GetBaseFilename(), *LexToString(FPlatformProcess::GetCurrentProcessId())));
	return LogPathname;
}

uint32 FDiagnosticLogger::GetLogProcessId(const FString& Filename)
{
	// Parse the PID from a filename like: UnrealCrcMiniLogV2_939399.log
	int Start;
	int End;
	if (!Filename.FindChar(TEXT('_'), Start))
	{
		return 0;
	}
	else if (!Filename.FindChar(TEXT('.'), End))
	{
		return 0;
	}

	FString ProcessIdStr = Filename.Mid(Start + 1, End - Start);
	return FCString::Atoi(*ProcessIdStr);
}

void FDiagnosticLogger::AppendLog(const TCHAR* Event)
{
	// Add the separator if some text is already logged.
	if (DiagnosticInfo.Len())
	{
		DiagnosticInfo.Append(TEXT("|"));
	}

	// Rotate the log if it gets too long.
	int32 FreeLen = MaxLogLen - DiagnosticInfo.Len();
	int32 EventLen = FCString::Strlen(Event);
	if (EventLen > FreeLen)
	{
		if (EventLen > MaxLogLen)
		{
			DiagnosticInfo.Reset(MaxLogLen);
			EventLen = MaxLogLen;
		}
		else
		{
			DiagnosticInfo.RemoveAt(0, EventLen - FreeLen, /*bAllowShrinking*/false); // Free space, remove the chars from the oldest events (in front).
		}
	}

	// Append the log entry and dump the log to the file.
	DiagnosticInfo.AppendChars(Event, EventLen);
	WriteToFile(FDateTime::UtcNow(), &DiagnosticInfo);
}

void FDiagnosticLogger::WriteToFile(const FDateTime& Timestamp, const FString* Info)
{
	if (!LogFileAr)
	{
		return;
	}

	// Write the timestamp at the very beginning of the file.
	LogFileAr->Seek(0);
	int64 UnixTimestamp = Timestamp.ToUnixTimestamp();
	(*LogFileAr) << UnixTimestamp;

	// If the diagnostic information is supplied, write it all over previous data. (The diagnostic info never shrinks, so it always overwrite existing data).
	if (Info)
	{
		(*LogFileAr) << const_cast<FString&>(*Info);
	}

	// Flush to disk.
	LogFileAr->Flush();
}
