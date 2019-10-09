// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SessionMonitorPCH.h"
#include "Logging.h"
#include "Spawner.h"
#include "StringUtils.h"
#include "Utils.h"
#include "Config.h"

//
// FWin32Handle
//

FWin32Handle::FWin32Handle()
{
}

FWin32Handle::FWin32Handle(HANDLE Handle_)
    : Handle(Handle_)
{
}

FWin32Handle::FWin32Handle(FWin32Handle&& Other)
    : Handle(Other.Handle)
{
	Other.Handle = NULL;
}

FWin32Handle::~FWin32Handle()
{
	Close();
}

void FWin32Handle::Close()
{
	if (Handle)
	{
		if (!::CloseHandle(Handle))
		{
			EG_LOG(LogDefault, Error, "Failed to close Handle: %s", Win32ErrorMsg().c_str());
		}
		Handle = NULL;
	}
}
FWin32Handle& FWin32Handle::operator=(FWin32Handle&& Other)
{
	if (this != &Other)
	{
		Close();
		Handle = Other.Handle;
		Other.Handle = NULL;
	}

	return *this;
}

bool FWin32Handle::IsValid() const
{
	return Handle != NULL;
}

//! Blocks waiting for the handle to be signaled by the OS
// \param Ms Time to wait in milliseconds. Passing 0 will query the state and not block
// \return True if signaled, false otherwise
bool FWin32Handle::Wait(unsigned Ms) const
{
	if (!IsValid())
		return false;
	DWORD Res = WaitForSingleObject(Handle, Ms);
	if (Res == WAIT_OBJECT_0)
		return true;
	else
		return false;
}

HANDLE FWin32Handle::GetNativeHandle() const
{
	return Handle;
}


//
// FSpawner
//

FSpawner::FSpawner(const FAppConfig* Cfg_, uint16_t SessionMonitorPort_)
	: Cfg(Cfg_)
	, SessionMonitorPort(SessionMonitorPort_)
{
	APPLOG(Log, "Creating Spawner");
}

FSpawner::~FSpawner()
{
	APPLOG(Log, "Destroying Spawner");
	Kill();
}

const std::string& FSpawner::GetAppName() const
{
	return Cfg->Name;
}

bool FSpawner::Launch(std::function<void(int)> ExitCallback)
{
	CHECK_MAINTHREAD();
	if (ProcessHandle.IsValid())
	{
		APPLOG(Warning, "Spawner already has an app running");
		return false;
	}

	std::string CmdLine = std::string("\"") + Cfg->Exe + "\" " + Cfg->Params;
	if (Cfg->bMonitored && SessionMonitorPort)
	{
		CmdLine += std::string(" ") + Cfg->ParameterPrefix +  std::string("PixelStreamingSessionMonitorPort=") + std::to_string(SessionMonitorPort);
	}

	APPLOG(Log, "Launching Spawner: %s", CmdLine.c_str());

	STARTUPINFO Si;
	ZeroMemory(&Si, sizeof(STARTUPINFO));
	Si.cb = sizeof(STARTUPINFO);
	PROCESS_INFORMATION Pi;
	memset(&Pi, 0, sizeof(Pi));
	std::wstring WorkingDir = Widen(Cfg->WorkingDirectory);

	if (!CreateProcessW(
		NULL,                                               // lpApplicationName
		(LPWSTR)Widen(CmdLine).c_str(),                     // lpCommandLine
		NULL,                                               // lpProcessAttributes
		NULL,                                               // lpThreadAttributes
		TRUE,                                               // bInheritHandles
		CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,    // dwCreationFlags
		NULL,                                               // lpEnvironment
		WorkingDir.size() ? WorkingDir.c_str() : NULL,      // lpCurrentDirectory
		&Si,                                                // lpStartupInfo
		&Pi                                                 // lpProcessInformation
	))
	{
		APPLOG(Error, "Launching failed. Reason=%s", Win32ErrorMsg("CreateProcess").c_str());
		return false;
	}

	ProcessHandle = FWin32Handle(Pi.hProcess);
	ProcessMainThreadHandle = FWin32Handle(Pi.hThread);

	FinishDetectionThread = std::thread([this, Func(std::move(ExitCallback))]()
	{
		ProcessHandle.Wait();
		DWORD Code = EXIT_FAILURE;
		if (!GetExitCodeProcess(ProcessHandle.GetNativeHandle(), &Code))
		{
			APPLOG(Error, "Failed to get exit code. Reason=%s", Win32ErrorMsg("GetExitCodeProcess").c_str());
		}

		Func(Code);
		ProcessHandle = FWin32Handle();
		ProcessMainThreadHandle = FWin32Handle();
	});

	return true;
}

void FSpawner::Kill()
{
	CHECK_MAINTHREAD();
	if (ProcessHandle.IsValid())
	{
		TerminateProcess(ProcessHandle.GetNativeHandle(), EXIT_FAILURE);
	}

	if (FinishDetectionThread.joinable())
	{
		FinishDetectionThread.join();
	}

	ProcessHandle = FWin32Handle();
	ProcessMainThreadHandle = FWin32Handle();
}

