// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrashReportClientApp.h"
#include "Windows/WindowsHWrapper.h"

#if CRASH_REPORT_WITH_MTBF
#include "EditorAnalyticsSession.h"
#endif

/**
 * WinMain, called when the application is started
 */
int WINAPI WinMain(_In_ HINSTANCE hInInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int nCmdShow)
{
	hInstance = hInInstance;

#if CRASH_REPORT_WITH_MTBF && !PLATFORM_SEH_EXCEPTIONS_DISABLED
	// Try to record if CrashReportClientEditor is crashing. Analytics shows that good number of Editor exit code are reported delayed, hinting
	// that CRCEditor was not running anymore. Try figuring out if it crashed. Suspecting that the Editor crash reporter/handler code is crashing could also
	// inadvertedly cause a crash in CRCEditor.
	__try
	{
		RunCrashReportClient(GetCommandLineW());
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		uint64 MonitoredEditorPid;
		if (FParse::Value(GetCommandLineW(), TEXT("-MONITOR="), MonitoredEditorPid))
		{
			FTimespan Timeout = FTimespan::FromSeconds(5);
			if (FEditorAnalyticsSession::Lock(Timeout))
			{
				FEditorAnalyticsSession MonitoredSession;
				if (FEditorAnalyticsSession::FindSession(MonitoredEditorPid, MonitoredSession))
				{
					MonitoredSession.SaveMonitorExceptCode(GetExceptionCode());
				}
				FEditorAnalyticsSession::Unlock();
			}
		}
	}
#else
	RunCrashReportClient(GetCommandLineW());
#endif

	return 0;
}
