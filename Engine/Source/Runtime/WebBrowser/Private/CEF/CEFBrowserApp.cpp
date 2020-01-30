// Copyright Epic Games, Inc. All Rights Reserved.

#include "CEF/CEFBrowserApp.h"

#if WITH_CEF3

FCEFBrowserApp::FCEFBrowserApp()
	: MessagePumpCountdown(0)
{
}

void FCEFBrowserApp::OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> CommandLine)
{
}

void FCEFBrowserApp::OnBeforeCommandLineProcessing(const CefString& ProcessType, CefRefPtr< CefCommandLine > CommandLine)
{
	CommandLine->AppendSwitch("enable-gpu");
	CommandLine->AppendSwitch("enable-gpu-compositing");
	CommandLine->AppendSwitch("enable-begin-frame-scheduling");
}

void FCEFBrowserApp::OnRenderProcessThreadCreated(CefRefPtr<CefListValue> ExtraInfo)
{
	RenderProcessThreadCreatedDelegate.ExecuteIfBound(ExtraInfo);
}

#if !PLATFORM_LINUX
void FCEFBrowserApp::OnScheduleMessagePumpWork(int64 delay_ms)
{
	FScopeLock Lock(&MessagePumpCountdownCS);

	// As per CEF documentation, if delay_ms is <= 0, then the call to CefDoMessageLoopWork should happen reasonably soon.  If delay_ms is > 0, then the call
	//  to CefDoMessageLoopWork should be scheduled to happen after the specified delay and any currently pending scheduled call should be canceled.
	if(delay_ms < 0)
	{
		delay_ms = 0;
	}
	MessagePumpCountdown = delay_ms;
}
#endif

void FCEFBrowserApp::TickMessagePump(float DeltaTime, bool bForce)
{
#if PLATFORM_LINUX
	CefDoMessageLoopWork();
	return;
#endif

	bool bPump = false;
	{
		FScopeLock Lock(&MessagePumpCountdownCS);
		
		// count down in order to call message pump
		if (MessagePumpCountdown >= 0)
		{
			MessagePumpCountdown -= DeltaTime * 1000;
			if (MessagePumpCountdown <= 0)
			{
				bPump = true;
			}
			
			if (bPump || bForce)
			{
				// -1 indicates that no countdown is currently happening
				MessagePumpCountdown = -1;
			}
		}
	}
	
	if (bPump || bForce)
	{
		CefDoMessageLoopWork();
	}
}

#endif
