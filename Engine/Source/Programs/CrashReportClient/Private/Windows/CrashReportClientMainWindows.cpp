// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrashReportClientApp.h"
#include "Windows/WindowsHWrapper.h"
#include "CrashReportClientDefines.h"

#if CRASH_REPORT_WITH_MTBF
#include "EditorAnalyticsSession.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformAtomics.h"
#include "Serialization/Archive.h"
#endif

#if CRASH_REPORT_WITH_MTBF
extern void LogCrcEvent(const TCHAR*);

LONG WINAPI UnhandledCrashReportException(EXCEPTION_POINTERS* ExceptionInfo)
{
	// Most significant bit in exception code is for fatal exceptions. Report those but not other types.
	if ((ExceptionInfo->ExceptionRecord->ExceptionCode & 0x80000000L) != 0)
	{
		// The logger is thread safe and will log all exceptions (unless it happened before the engine was initialized)
		TCHAR CrashEventLog[64];
		FCString::Sprintf(CrashEventLog, TEXT("CRC/Crash:%d"), ExceptionInfo->ExceptionRecord->ExceptionCode);
		LogCrcEvent(CrashEventLog);

		// Try to write the exit code in the appropriate field if the session was created. The first crashing thread
		// incrementing the counter wins the race and can write its exception code.
		static volatile int32 CrashCount = 0;
		if (FPlatformAtomics::InterlockedIncrement(&CrashCount) == 1)
		{
			uint64 MonitoredEditorPid;
			if (FParse::Value(GetCommandLineW(), TEXT("-MONITOR="), MonitoredEditorPid))
			{
				FTimespan Timeout = FTimespan::FromSeconds(1);
				if (FEditorAnalyticsSession::Lock(Timeout)) // This lock is reentrant for the same process.
				{
					FEditorAnalyticsSession MonitoredSession;
					if (FEditorAnalyticsSession::FindSession(MonitoredEditorPid, MonitoredSession))
					{
						MonitoredSession.SaveMonitorExceptCode(ExceptionInfo->ExceptionRecord->ExceptionCode);
					}
					FEditorAnalyticsSession::Unlock();
				}
			}
		}
	}

	// The exception was logged but not handled, continue searching for a real handler.
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

/**
 * WinMain, called when the application is started
 */
int WINAPI WinMain(_In_ HINSTANCE hInInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int nCmdShow)
{
	hInstance = hInInstance;

#if CRASH_REPORT_WITH_MTBF // For the Editor only.
	FString Arguments(::GetCommandLineW());
	if (Arguments.Contains(TEXT("-MONITOR=")) && !Arguments.Contains(TEXT("-RespawnedInstance")))
	{
		uint64 ChildPipe = 0;
		FParse::Value(GetCommandLineW(), TEXT("-READ="), ChildPipe);

		// Parse the process ID of the Editor that spawned this CRC.
		uint32 MonitoredEditorPid = 0;
		if (FParse::Value(GetCommandLineW(), TEXT("-MONITOR="), MonitoredEditorPid))
		{
			TCHAR RespawnExePathname[MAX_PATH];
			GetModuleFileName(NULL, RespawnExePathname, MAX_PATH);
			FString RespawnExeArguments(Arguments);
			RespawnExeArguments.Append(" -RespawnedInstance");
			uint32 RespawnPid = 0;

			// Respawn itself to sever the process grouping with the Editor. If the user kills the Editor process group in task manager,
			// CRC will not die at the same time, will be able to capture the Editor exit code and send the MTBF report to correctly
			// identify the Editor 'AbnormalShutdown' as 'Killed' instead.
			FProcHandle Handle = FPlatformProcess::CreateProc(
				RespawnExePathname,
				*RespawnExeArguments,
				true, false, false,
				&RespawnPid, 0,
				nullptr,
				reinterpret_cast<void*>(ChildPipe), // Ensure the child process inherit this pipe handle that was previously inherited from its parent.
				nullptr);

			if (Handle.IsValid())
			{
				FString PidPathname = FString::Printf(TEXT("%sue4-crc-pid-%d"), FPlatformProcess::UserTempDir(), MonitoredEditorPid);
				if (TUniquePtr<FArchive> Ar = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*PidPathname, FILEWRITE_EvenIfReadOnly)))
				{
					*Ar << RespawnPid;
				}
			}
		}

		return 0; // Exit this intermediate instance, the Editor is waiting for it to continue.
	}

	// In case of CRC crashing, this handler is going to be called.
	AddVectoredExceptionHandler(0, UnhandledCrashReportException);
#endif

	RunCrashReportClient(GetCommandLineW());
	return 0;
}
