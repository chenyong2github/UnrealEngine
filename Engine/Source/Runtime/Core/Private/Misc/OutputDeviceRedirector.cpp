// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include "Stats/Stats.h"
#include "Misc/CoreStats.h"
#include "HAL/PlatformProcess.h"

/*-----------------------------------------------------------------------------
	FOutputDeviceRedirector.
-----------------------------------------------------------------------------*/

/** Initialization constructor. */
FOutputDeviceRedirector::FOutputDeviceRedirector()
:	MasterThreadID(FPlatformTLS::GetCurrentThreadId())
,	bEnableBacklog(false)
{
}

FOutputDeviceRedirector* FOutputDeviceRedirector::Get()
{
	static FOutputDeviceRedirector Singleton;
	return &Singleton;
}

/**
 * Adds an output device to the chain of redirections.	
 *
 * @param OutputDevice	output device to add
 */
void FOutputDeviceRedirector::AddOutputDevice( FOutputDevice* OutputDevice )
{
	if (OutputDevice)
	{
		bool bAdded = false;
		do
		{
			{
				FScopeLock ScopeLock(&OutputDevicesMutex);
				if (OutputDevicesLockCounter.GetValue() == 0)
				{
					if (OutputDevice->CanBeUsedOnMultipleThreads())
					{
						UnbufferedOutputDevices.AddUnique(OutputDevice);
					}
					else
					{
						BufferedOutputDevices.AddUnique(OutputDevice);
					}
					bAdded = true;
				}
			}
			if (!bAdded)
			{
				FPlatformProcess::Sleep(0);
			}
		} while (!bAdded);
	}
}

/**
 * Removes an output device from the chain of redirections.	
 *
 * @param OutputDevice	output device to remove
 */
void FOutputDeviceRedirector::RemoveOutputDevice( FOutputDevice* OutputDevice )
{
	bool bRemoved = false;
	do
	{
		{
			FScopeLock ScopeLock(&OutputDevicesMutex);
			if (OutputDevicesLockCounter.GetValue() == 0)
			{
				BufferedOutputDevices.Remove(OutputDevice);
				UnbufferedOutputDevices.Remove(OutputDevice);
				bRemoved = true;
			}
		}
		if (!bRemoved)
		{
			FPlatformProcess::Sleep(0);
		}
	} while (!bRemoved);
}

/**
 * Returns whether an output device is currently in the list of redirectors.
 *
 * @param	OutputDevice	output device to check the list against
 * @return	true if messages are currently redirected to the the passed in output device, false otherwise
 */
bool FOutputDeviceRedirector::IsRedirectingTo( FOutputDevice* OutputDevice )
{
	// For performance reasons whe're not using the FOutputDevicesLock here
	FScopeLock OutputDevicesLock(&OutputDevicesMutex);
	return BufferedOutputDevices.Contains(OutputDevice) || UnbufferedOutputDevices.Contains(OutputDevice);
}

void FOutputDeviceRedirector::InternalFlushThreadedLogs(bool bUseAllDevices)
{
	TLocalOutputDevicesArray LocalBufferedDevices;
	TLocalOutputDevicesArray LocalUnbufferedDevices;
	FOutputDevicesLock OutputDevicesLock(this, LocalBufferedDevices, LocalUnbufferedDevices);

	InternalFlushThreadedLogs(LocalBufferedDevices, bUseAllDevices);
}

/**
 * The unsynchronized version of FlushThreadedLogs.
 */
void FOutputDeviceRedirector::InternalFlushThreadedLogs(TLocalOutputDevicesArray& InBufferedDevices, bool bUseAllDevices)
{	
	if (BufferedLines.Num())
	{
		TArray<FBufferedLine, TInlineAllocator<64>> LocalBufferedLines;
		{
			FScopeLock ScopeLock(&BufferSynchronizationObject);
			LocalBufferedLines.AddUninitialized(BufferedLines.Num());
			for (int32 LineIndex = 0; LineIndex < BufferedLines.Num(); LineIndex++)
			{
				new(&LocalBufferedLines[LineIndex]) FBufferedLine(BufferedLines[LineIndex], FBufferedLine::EMoveCtor);
			}
			BufferedLines.Empty();
		}

		for (FBufferedLine& LocalBufferedLine : LocalBufferedLines)
		{
			for (FOutputDevice* OutputDevice : InBufferedDevices)
			{
				if (OutputDevice->CanBeUsedOnAnyThread() || bUseAllDevices)
				{
					OutputDevice->Serialize(*LocalBufferedLine.Data, LocalBufferedLine.Verbosity, LocalBufferedLine.Category, LocalBufferedLine.Time);
				}
			}
		}
	}
}

/**
 * Flushes lines buffered by secondary threads.
 */
void FOutputDeviceRedirector::FlushThreadedLogs()
{
	SCOPE_CYCLE_COUNTER(STAT_FlushThreadedLogs);
	check(IsInGameThread());
	InternalFlushThreadedLogs(true);
}

void FOutputDeviceRedirector::PanicFlushThreadedLogs()
{
//	SCOPE_CYCLE_COUNTER(STAT_FlushThreadedLogs);

	TLocalOutputDevicesArray LocalBufferedDevices;
	TLocalOutputDevicesArray LocalUnbufferedDevices;
	FOutputDevicesLock OutputDevicesLock(this, LocalBufferedDevices, LocalUnbufferedDevices);

	// Flush threaded logs, but use the safe version.
	InternalFlushThreadedLogs(LocalBufferedDevices, false);

	// Flush devices.
	for (FOutputDevice* OutputDevice : LocalBufferedDevices)
	{
		if (OutputDevice->CanBeUsedOnAnyThread())
		{
			OutputDevice->Flush();
		}
	}

	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->Flush();
	}
}

/**
 * Serializes the current backlog to the specified output device.
 * @param OutputDevice	- Output device that will receive the current backlog
 */
void FOutputDeviceRedirector::SerializeBacklog( FOutputDevice* OutputDevice )
{
	FScopeLock ScopeLock( &SynchronizationObject );

	for (int32 LineIndex = 0; LineIndex < BacklogLines.Num(); LineIndex++)
	{
		const FBufferedLine& BacklogLine = BacklogLines[ LineIndex ];
		OutputDevice->Serialize( *BacklogLine.Data, BacklogLine.Verbosity, BacklogLine.Category, BacklogLine.Time );
	}
}

/**
 * Enables or disables the backlog.
 * @param bEnable	- Starts saving a backlog if true, disables and discards any backlog if false
 */
void FOutputDeviceRedirector::EnableBacklog( bool bEnable )
{
	FScopeLock ScopeLock(&SynchronizationObject);

	bEnableBacklog = bEnable;
	if ( bEnableBacklog == false )
	{
		BacklogLines.Empty();
	}
}

/**
 * Sets the current thread to be the master thread that prints directly
 * (isn't queued up)
 */
void FOutputDeviceRedirector::SetCurrentThreadAsMasterThread()
{
	// make sure anything queued up is flushed out, this may be called from a background thread, so use the safe version.
	InternalFlushThreadedLogs( false );

	{
		FScopeLock ScopeLock(&SynchronizationObject);
		// set the current thread as the master thread
		MasterThreadID = FPlatformTLS::GetCurrentThreadId();
	}
}

void FOutputDeviceRedirector::LockOutputDevices(TLocalOutputDevicesArray& OutBufferedDevices, TLocalOutputDevicesArray& OutUnbufferedDevices)
{
	FScopeLock OutputDevicesLock(&OutputDevicesMutex);
	OutputDevicesLockCounter.Increment();
	OutBufferedDevices.Append(BufferedOutputDevices);
	OutUnbufferedDevices.Append(UnbufferedOutputDevices);
}

void FOutputDeviceRedirector::UnlockOutputDevices()
{
	FScopeLock OutputDevicesLock(&OutputDevicesMutex);
	int32 LockValue = OutputDevicesLockCounter.Decrement();
	check(LockValue >= 0);
}

void FOutputDeviceRedirector::Serialize( const TCHAR* Data, ELogVerbosity::Type Verbosity, const class FName& Category, const double Time )
{
	const double RealTime = Time == -1.0f ? FPlatformTime::Seconds() - GStartTime : Time;

	TLocalOutputDevicesArray LocalBufferedDevices;
	TLocalOutputDevicesArray LocalUnbufferedDevices;
	FOutputDevicesLock OutputDevicesLock(this, LocalBufferedDevices, LocalUnbufferedDevices);

#if PLATFORM_DESKTOP
	// this is for errors which occur after shutdown we might be able to salvage information from stdout 
	if ((BufferedOutputDevices.Num() == 0) && GIsRequestingExit)
	{
#if PLATFORM_WINDOWS
		_tprintf(_T("%s\n"), Data);
#else
		FGenericPlatformMisc::LocalPrint(Data);
		// printf("%s\n", TCHAR_TO_ANSI(Data));
#endif
		return;
	}
#endif

	// Serialize directly to any output devices which don't require buffering
	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->Serialize(Data, Verbosity, Category, RealTime);
	}


	if ( bEnableBacklog )
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		new(BacklogLines)FBufferedLine( Data, Category, Verbosity, RealTime );
	}

	if(FPlatformTLS::GetCurrentThreadId() != MasterThreadID || LocalBufferedDevices.Num() == 0)
	{
		FScopeLock ScopeLock(&BufferSynchronizationObject);
		new(BufferedLines)FBufferedLine( Data, Category, Verbosity, RealTime );
	}
	else
	{
		// Flush previously buffered lines from secondary threads.
		InternalFlushThreadedLogs(LocalBufferedDevices, true);

		for (FOutputDevice* OutputDevice : LocalBufferedDevices)
		{
			OutputDevice->Serialize(Data, Verbosity, Category, RealTime);
		}
	}
}

void FOutputDeviceRedirector::Serialize( const TCHAR* Data, ELogVerbosity::Type Verbosity, const class FName& Category )
{
	Serialize( Data, Verbosity, Category, -1.0 );
}

/**
 * Passes on the flush request to all current output devices.
 */
void FOutputDeviceRedirector::Flush()
{
	TLocalOutputDevicesArray LocalBufferedDevices;
	TLocalOutputDevicesArray LocalUnbufferedDevices;
	FOutputDevicesLock OutputDevicesLock(this, LocalBufferedDevices, LocalUnbufferedDevices);

	if(FPlatformTLS::GetCurrentThreadId() == MasterThreadID)
	{
		// Flush previously buffered lines from secondary threads.
		// Since we already hold a lock on SynchronizationObject, call the unsynchronized version.
		InternalFlushThreadedLogs(true);

		for (FOutputDevice* OutputDevice : LocalBufferedDevices)
		{
			OutputDevice->Flush();
		}
	}

	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->Flush();
	}
}

/**
 * Closes output device and cleans up. This can't happen in the destructor
 * as we might have to call "delete" which cannot be done for static/ global
 * objects.
 */
void FOutputDeviceRedirector::TearDown()
{
	FScopeLock SyncLock(&SynchronizationObject);
	check(FPlatformTLS::GetCurrentThreadId() == MasterThreadID);

	TLocalOutputDevicesArray LocalBufferedDevices;
	TLocalOutputDevicesArray LocalUnbufferedDevices;
	
	{
		// We need to lock the mutex here so that it gets unlocked after we empty the devices arrays
		FScopeLock GlobalOutputDevicesLock(&OutputDevicesMutex);
		LockOutputDevices(LocalBufferedDevices, LocalUnbufferedDevices);
		BufferedOutputDevices.Empty();
		UnbufferedOutputDevices.Empty();	
	}

	// Flush previously buffered lines from secondary threads.
	InternalFlushThreadedLogs(LocalBufferedDevices, false);

	for (FOutputDevice* OutputDevice : LocalBufferedDevices)
	{
		if (OutputDevice->CanBeUsedOnAnyThread())
		{
			OutputDevice->Flush();
		}
		OutputDevice->TearDown();
	}	

	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->Flush();
		OutputDevice->TearDown();
	}

	UnlockOutputDevices();
}

CORE_API FOutputDeviceRedirector* GetGlobalLogSingleton()
{
	return FOutputDeviceRedirector::Get();
}

