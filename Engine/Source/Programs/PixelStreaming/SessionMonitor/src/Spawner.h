// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SessionMonitorCommon.h"

// Forward declarations
struct FAppConfig;

//! Wrapper for Win32 HANDLEs, so we can have automatic cleanup
class FWin32Handle
{
public:
	FWin32Handle();
	explicit FWin32Handle(HANDLE Handle_);
	FWin32Handle(const FWin32Handle&) = delete;
	FWin32Handle(FWin32Handle&& Other);
	~FWin32Handle();
	void Close();
	FWin32Handle& operator=(const FWin32Handle&) = delete;
	FWin32Handle& operator=(FWin32Handle&& Other);
	bool IsValid() const;

	//! Blocks waiting for the handle to be signaled by the OS
	// \param Ms Time to wait in milliseconds. Passing 0 will query the state and not block
	// \return True if signaled, false otherwise
	bool Wait(unsigned Ms = INFINITE) const;

	HANDLE GetNativeHandle() const;

private:
	HANDLE Handle = NULL;
};


class FSpawner
{
public:
	FSpawner(const FAppConfig* Cfg, uint16_t SessionMonitorPort);
	~FSpawner();

	// \param ExitCallback
	//	Callback, where the 'int' parameter will be the Process exit code
	bool Launch(std::function<void(int)> ExitCallback);

private:

	const std::string& GetAppName() const;
	void Kill();

	const FAppConfig* Cfg;
	uint16_t SessionMonitorPort;
	std::thread FinishDetectionThread;
	FWin32Handle ProcessHandle;
	FWin32Handle ProcessMainThreadHandle;
};