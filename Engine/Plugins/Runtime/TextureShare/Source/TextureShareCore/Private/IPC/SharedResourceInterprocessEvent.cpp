// Copyright Epic Games, Inc. All Rights Reserved.

#include "SharedResourceInterprocessEvent.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/Stats2.h"

#include "TextureShareCoreLog.h"

//////////////////////////////////////////////////////////////////////////////////////////////
static HANDLE ImplNewInterprocessEvent(const TCHAR* EventName)
{
	// First try to open exist event
	const DWORD AccessRights = SYNCHRONIZE | EVENT_MODIFY_STATE;
	HANDLE EventHandle = OpenEvent(AccessRights, false, EventName);
	if (NULL == EventHandle)
	{
		// Finally try create new event handle
		EventHandle = CreateEvent(nullptr, true, 0, EventName);
		if (NULL == EventHandle)
		{
			const DWORD ErrNo = GetLastError();
			UE_LOG(LogTextureShareCore, Warning, TEXT("NewInterprocessEvent(AccessRights=0x%08x, bInherit=false, Name='%s') failed with LastError = %d"),
				AccessRights,
				EventName,
				ErrNo);
		}
	}

	return EventHandle;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FTextureShareEventWin
//////////////////////////////////////////////////////////////////////////////////////////////
FTextureShareEventWin::FTextureShareEventWin(const FString& InEventName)
	: Event(nullptr)
	, EventName(InEventName)
{ }

FTextureShareEventWin::~FTextureShareEventWin()
{
	if (Event != nullptr)
	{
		CloseHandle(Event);
	}
}

bool FTextureShareEventWin::Create(bool bIsManualReset)
{
	Event = ImplNewInterprocessEvent(*EventName);

	return IsEnabled();
}

void FTextureShareEventWin::Trigger()
{
	TriggerForStats();

	check(Event);
	SetEvent(Event);
}

void FTextureShareEventWin::Reset()
{
	ResetForStats();

	check(Event);
	ResetEvent(Event);
}

bool FTextureShareEventWin::Wait(uint32 WaitTime, const bool bIgnoreThreadIdleStats)
{
	check(Event);

	WaitForStats();

#if TEXTURESHARECORE_RHI
	CSV_SCOPED_WAIT(WaitTime);
	FThreadIdleStats::FScopeIdle Scope(bIgnoreThreadIdleStats);
#endif

	return (WaitForSingleObject(Event, WaitTime) == WAIT_OBJECT_0);
}

#include "Windows/HideWindowsPlatformTypes.h"
