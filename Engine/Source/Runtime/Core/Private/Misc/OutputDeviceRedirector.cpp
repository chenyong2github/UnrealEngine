// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/OutputDeviceRedirector.h"

#include "Containers/DepletableMpscQueue.h"
#include "Experimental/ConcurrentLinearAllocator.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <atomic>

/*-----------------------------------------------------------------------------
	FOutputDeviceRedirector.
-----------------------------------------------------------------------------*/

FBufferedLine::FBufferedLine(const TCHAR* InData, const FName& InCategory, ELogVerbosity::Type InVerbosity, double InTime)
	: Category(InCategory)
	, Time(InTime)
	, Verbosity(InVerbosity)
{
	int32 NumChars = FCString::Strlen(InData) + 1;
	void* Dest = FMemory::Malloc(sizeof(TCHAR) * NumChars);
	Data = (TCHAR*)FMemory::Memcpy(Dest, InData, sizeof(TCHAR) * NumChars);
}

FBufferedLine::~FBufferedLine()
{
	FMemory::Free(const_cast<TCHAR*>(Data));
}

namespace UE::Private
{

struct FOutputDeviceBlockAllocationTag : FDefaultBlockAllocationTag
{
	static constexpr const TCHAR* TagName = TEXT("OutputDeviceLinear");

	struct Allocator
	{
		static constexpr bool SupportsAlignment = false;

		FORCEINLINE static void* Malloc(SIZE_T Size, uint32 Alignment)
		{
			return FMemory::Malloc(Size, Alignment);
		}

		FORCEINLINE static void Free(void* Pointer, SIZE_T Size)
		{
			return FMemory::Free(Pointer);
		}
	};
};

struct FOutputDeviceLinearAllocator
{
	FORCEINLINE static void* Malloc(SIZE_T Size, uint32 Alignment)
	{
		return TConcurrentLinearAllocator<FOutputDeviceBlockAllocationTag>::Malloc(Size, Alignment);
	}

	FORCEINLINE static void Free(void* Pointer)
	{
		TConcurrentLinearAllocator<FOutputDeviceBlockAllocationTag>::Free(Pointer);
	}
};

struct FOutputDeviceLine
{
	const double Time;
	const TCHAR* Data;
	const FName Category;
	const ELogVerbosity::Type Verbosity;

	FOutputDeviceLine(const FBufferedLine&) = delete;
	FOutputDeviceLine& operator=(const FBufferedLine&) = delete;

	FORCEINLINE FOutputDeviceLine(const TCHAR* const InData, const FName InCategory, const ELogVerbosity::Type InVerbosity, const double InTime)
		: Time(InTime)
		, Data(CopyData(InData))
		, Category(InCategory)
		, Verbosity(InVerbosity)
	{
	}

	FORCEINLINE ~FOutputDeviceLine()
	{
		FOutputDeviceLinearAllocator::Free(const_cast<TCHAR*>(Data));
	}

private:
	FORCEINLINE static const TCHAR* CopyData(const TCHAR* const InData)
	{
		const int32 Len = FCString::Strlen(InData) + 1;
		void* const Dest = FOutputDeviceLinearAllocator::Malloc(sizeof(TCHAR) * Len, alignof(TCHAR));
		return static_cast<TCHAR*>(FMemory::Memcpy(Dest, InData, sizeof(TCHAR) * Len));
	}
};

struct FOutputDeviceRedirectorState
{
	FRWLock OutputDevicesLock;
	std::atomic<uint32> OutputDevicesLockState;
	uint8 OutputDevicesLockPadding[PLATFORM_CACHE_LINE_SIZE - sizeof(OutputDevicesLock) - sizeof(OutputDevicesLockState)]{};

	/** A FIFO of lines logged by non-master threads. */
	TDepletableMpscQueue<FOutputDeviceLine, FOutputDeviceLinearAllocator> BufferedLines;
	uint8 BufferedLinesPadding[PLATFORM_CACHE_LINE_SIZE - sizeof(BufferedLines)]{};

	/** Array of output devices to redirect to from the master thread. */
	TArray<FOutputDevice*> BufferedOutputDevices;

	/** Array of output devices to redirect to from the calling thread. */
	TArray<FOutputDevice*> UnbufferedOutputDevices;

	/** The ID of the master thread. Logging from other threads will be buffered for processing by the master thread. */
	uint32 MasterThreadID = FPlatformTLS::GetCurrentThreadId();

	/** Objects used for synchronization via a scoped lock. */
	FCriticalSection SynchronizationObject;

	/** A FIFO backlog of messages logged before the editor had a chance to intercept them. */
	TArray<FBufferedLine> BacklogLines;

	/** Whether backlogging is enabled. */
	bool bEnableBacklog = false;

	bool IsInMasterThread() const
	{
		return MasterThreadID == FPlatformTLS::GetCurrentThreadId();
	}
};

struct FOutputDevicesReadScopeLock
{
	FORCEINLINE explicit FOutputDevicesReadScopeLock(FOutputDeviceRedirectorState& InState)
		: State(InState)
	{
		// Read locks add/sub by 2 to keep the LSB free for write locks to use.
		if (State.OutputDevicesLockState.fetch_add(2, std::memory_order_relaxed) & 1)
		{
			WaitForWriteLock();
		}
	}

	FORCENOINLINE void WaitForWriteLock()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDevicesReadScopeLock);
		// A write lock has set the LSB. Cancel this read lock and wait for the write.
		State.OutputDevicesLockState.fetch_sub(2, std::memory_order_relaxed);
		// This read lock will wait until the write lock exits.
		FReadScopeLock ScopeLock(State.OutputDevicesLock);
		// Acquire on this read lock because the write may have mutated state that we read.
		uint32 LockState = State.OutputDevicesLockState.fetch_add(2, std::memory_order_acquire);
		check((LockState & 1) == 0);
	}

	FORCEINLINE ~FOutputDevicesReadScopeLock()
	{
		State.OutputDevicesLockState.fetch_sub(2, std::memory_order_relaxed);
	}

	FOutputDeviceRedirectorState& State;
};

struct FOutputDevicesWriteScopeLock
{
	FORCEINLINE explicit FOutputDevicesWriteScopeLock(FOutputDeviceRedirectorState& InState)
		: State(InState)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDevicesWriteScopeLock);
		// Take the lock before modifying the state, to avoid contention on the LSB.
		State.OutputDevicesLock.WriteLock();
		// Set the LSB to flag to read locks that a write lock is waiting.
		uint32 LockState = State.OutputDevicesLockState.fetch_or(uint32(1), std::memory_order_relaxed);
		check((LockState & 1) == 0);
		if (LockState > 1)
		{
			// Wait for read locks to be cleared.
			do
			{
				FPlatformProcess::Sleep(0);
				LockState = State.OutputDevicesLockState.load(std::memory_order_relaxed);
			}
			while (LockState > 1);
		}
	}

	FORCEINLINE ~FOutputDevicesWriteScopeLock()
	{
		// Clear the LSB to allow read locks after the unlock below.
		uint32 LockState = State.OutputDevicesLockState.fetch_and(~uint32(1), std::memory_order_release);
		check((LockState & 1) == 1);
		State.OutputDevicesLock.WriteUnlock();
	}

	FOutputDeviceRedirectorState& State;
};

} // UE::Private

FOutputDeviceRedirector::FOutputDeviceRedirector()
	: State(MakePimpl<UE::Private::FOutputDeviceRedirectorState>())
{
}

FOutputDeviceRedirector* FOutputDeviceRedirector::Get()
{
	static FOutputDeviceRedirector Singleton;
	return &Singleton;
}

void FOutputDeviceRedirector::AddOutputDevice(FOutputDevice* OutputDevice)
{
	if (OutputDevice)
	{
		UE::Private::FOutputDevicesWriteScopeLock ScopeLock(*State);
		if (OutputDevice->CanBeUsedOnMultipleThreads())
		{
			State->UnbufferedOutputDevices.AddUnique(OutputDevice);
		}
		else
		{
			State->BufferedOutputDevices.AddUnique(OutputDevice);
		}
	}
}

void FOutputDeviceRedirector::RemoveOutputDevice(FOutputDevice* OutputDevice)
{
	if (OutputDevice)
	{
		UE::Private::FOutputDevicesWriteScopeLock ScopeLock(*State);
		State->BufferedOutputDevices.Remove(OutputDevice);
		State->UnbufferedOutputDevices.Remove(OutputDevice);
	}
}

bool FOutputDeviceRedirector::IsRedirectingTo(FOutputDevice* OutputDevice)
{
	UE::Private::FOutputDevicesReadScopeLock Lock(*State);
	return State->BufferedOutputDevices.Contains(OutputDevice) || State->UnbufferedOutputDevices.Contains(OutputDevice);
}

void FOutputDeviceRedirector::FlushBufferedLines(TConstArrayView<FOutputDevice*> BufferedDevices, bool bUseAllDevices)
{	
	if (State->BufferedLines.IsEmpty())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDeviceRedirector::FlushBufferedLines);

	State->BufferedLines.Deplete([BufferedDevices, bUseAllDevices](UE::Private::FOutputDeviceLine&& Line)
	{
		const double Time = Line.Time;
		const TCHAR* const Data = Line.Data;
		const FName Category = Line.Category;
		const ELogVerbosity::Type Verbosity = Line.Verbosity;

		for (FOutputDevice* OutputDevice : BufferedDevices)
		{
			if (bUseAllDevices || OutputDevice->CanBeUsedOnAnyThread())
			{
				OutputDevice->Serialize(Data, Verbosity, Category, Time);
			}
		}
	});
}

void FOutputDeviceRedirector::FlushThreadedLogs()
{
	check(State->IsInMasterThread());
	UE::Private::FOutputDevicesReadScopeLock Lock(*State);
	FlushBufferedLines(State->BufferedOutputDevices, /*bUseAllDevices*/ true);
}

void FOutputDeviceRedirector::PanicFlushThreadedLogs()
{
	UE::Private::FOutputDevicesReadScopeLock Lock(*State);
	FlushBufferedLines(State->BufferedOutputDevices, /*bUseAllDevices*/ State->IsInMasterThread());

	for (FOutputDevice* OutputDevice : State->BufferedOutputDevices)
	{
		if (OutputDevice->CanBeUsedOnAnyThread())
		{
			OutputDevice->Flush();
		}
	}

	for (FOutputDevice* OutputDevice : State->UnbufferedOutputDevices)
	{
		OutputDevice->Flush();
	}
}

void FOutputDeviceRedirector::SerializeBacklog(FOutputDevice* OutputDevice)
{
	FScopeLock ScopeLock(&State->SynchronizationObject);
	for (const FBufferedLine& BacklogLine : State->BacklogLines)
	{
		OutputDevice->Serialize(BacklogLine.Data, BacklogLine.Verbosity, BacklogLine.Category, BacklogLine.Time);
	}
}

void FOutputDeviceRedirector::EnableBacklog(bool bEnable)
{
	FScopeLock ScopeLock(&State->SynchronizationObject);
	State->bEnableBacklog = bEnable;
	if (!bEnable)
	{
		State->BacklogLines.Empty();
	}
}

void FOutputDeviceRedirector::SetCurrentThreadAsMasterThread()
{
	UE::Private::FOutputDevicesReadScopeLock Lock(*State);
	FlushBufferedLines(State->BufferedOutputDevices, /*bUseAllDevices*/ State->IsInMasterThread());

	FScopeLock ScopeLock(&State->SynchronizationObject);
	State->MasterThreadID = FPlatformTLS::GetCurrentThreadId();
}

void FOutputDeviceRedirector::Serialize(const TCHAR* const Data, const ELogVerbosity::Type Verbosity, const FName& Category, const double Time)
{
	const double RealTime = Time == -1.0f ? FPlatformTime::Seconds() - GStartTime : Time;

	UE::Private::FOutputDevicesReadScopeLock Lock(*State);

#if PLATFORM_DESKTOP
	// this is for errors which occur after shutdown we might be able to salvage information from stdout
	if (State->BufferedOutputDevices.IsEmpty() && IsEngineExitRequested())
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
	for (FOutputDevice* OutputDevice : State->UnbufferedOutputDevices)
	{
		OutputDevice->Serialize(Data, Verbosity, Category, RealTime);
	}

	if (State->bEnableBacklog)
	{
		FScopeLock ScopeLock(&State->SynchronizationObject);
		new(State->BacklogLines)FBufferedLine(Data, Category, Verbosity, RealTime);
	}

	if (!State->IsInMasterThread() || State->BufferedOutputDevices.IsEmpty())
	{
		State->BufferedLines.Enqueue(Data, Category, Verbosity, RealTime);
	}
	else
	{
		FlushBufferedLines(State->BufferedOutputDevices, /*bUseAllDevices*/ true);

		for (FOutputDevice* OutputDevice : State->BufferedOutputDevices)
		{
			OutputDevice->Serialize(Data, Verbosity, Category, RealTime);
		}
	}
}

void FOutputDeviceRedirector::Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category)
{
	Serialize(Data, Verbosity, Category, -1.0);
}

void FOutputDeviceRedirector::RedirectLog(const FName& Category, ELogVerbosity::Type Verbosity, const TCHAR* Data)
{
	Serialize(Data, Verbosity, Category, -1.0);
}

void FOutputDeviceRedirector::RedirectLog(const FLazyName& Category, ELogVerbosity::Type Verbosity, const TCHAR* Data)
{
	Serialize(Data, Verbosity, Category, -1.0);
}

void FOutputDeviceRedirector::Flush()
{
	UE::Private::FOutputDevicesReadScopeLock Lock(*State);

	if (State->IsInMasterThread())
	{
		FlushBufferedLines(State->BufferedOutputDevices, /*bUseAllDevices*/ true);

		for (FOutputDevice* OutputDevice : State->BufferedOutputDevices)
		{
			OutputDevice->Flush();
		}
	}

	for (FOutputDevice* OutputDevice : State->UnbufferedOutputDevices)
	{
		OutputDevice->Flush();
	}
}

void FOutputDeviceRedirector::TearDown()
{
	FScopeLock SyncLock(&State->SynchronizationObject);
	check(State->IsInMasterThread());

	TArray<FOutputDevice*> LocalBufferedDevices;
	TArray<FOutputDevice*> LocalUnbufferedDevices;

	{
		// We need to lock the mutex here so that it gets unlocked after we empty the devices arrays
		UE::Private::FOutputDevicesWriteScopeLock Lock(*State);
		LocalBufferedDevices = MoveTemp(State->BufferedOutputDevices);
		LocalUnbufferedDevices = MoveTemp(State->UnbufferedOutputDevices);
		State->BufferedOutputDevices.Empty();
		State->UnbufferedOutputDevices.Empty();
	}

	FlushBufferedLines(LocalBufferedDevices, /*bUseAllDevices*/ true);

	for (FOutputDevice* OutputDevice : LocalBufferedDevices)
	{
		OutputDevice->Flush();
		OutputDevice->TearDown();
	}

	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->Flush();
		OutputDevice->TearDown();
	}
}

bool FOutputDeviceRedirector::IsBacklogEnabled() const
{
	return State->bEnableBacklog;
}

CORE_API FOutputDeviceRedirector* GetGlobalLogSingleton()
{
	return FOutputDeviceRedirector::Get();
}
