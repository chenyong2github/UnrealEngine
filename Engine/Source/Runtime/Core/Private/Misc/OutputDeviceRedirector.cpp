// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/OutputDeviceRedirector.h"

#include "Containers/DepletableMpscQueue.h"
#include "Experimental/ConcurrentLinearAllocator.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "HAL/Thread.h"
#include "Misc/App.h"
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

static constexpr uint64 CalculateRedirectorCacheLinePadding(const uint64 Size)
{
	return PLATFORM_CACHE_LINE_SIZE * FMath::DivideAndRoundUp<uint64>(Size, PLATFORM_CACHE_LINE_SIZE) - Size;
}

struct FOutputDeviceRedirectorState
{
	/** A custom lock to guard access to both buffered and unbuffered output devices. */
	FRWLock OutputDevicesLock;
	std::atomic<uint32> OutputDevicesLockState{0};
	uint8 OutputDevicesLockPadding[CalculateRedirectorCacheLinePadding(sizeof(OutputDevicesLock) + sizeof(OutputDevicesLockState))]{};

	/** A queue of lines logged by non-master threads. */
	TDepletableMpscQueue<FOutputDeviceLine, FOutputDeviceLinearAllocator> BufferedLines;
	uint8 BufferedLinesPadding[CalculateRedirectorCacheLinePadding(sizeof(BufferedLines))]{};

	/** Array of output devices to redirect to from the master thread. */
	TArray<FOutputDevice*> BufferedOutputDevices;

	/** Array of output devices to redirect to from the calling thread. */
	TArray<FOutputDevice*> UnbufferedOutputDevices;

	/** A queue of lines logged before the editor added its output device. */
	TArray<FBufferedLine> BacklogLines;
	FRWLock BacklogLock;

	/** An optional dedicated master thread for logging to buffered output devices. */
	FThread Thread;

	/** A lock to synchronize access to the thread. */
	FRWLock ThreadLock;

	/** An event to wake the dedicated master thread to process buffered lines. */
	FEventRef ThreadWakeEvent{EEventMode::AutoReset};

	/** A queue of events to trigger when the dedicated master thread is idle. */
	TDepletableMpscQueue<FEvent*, FOutputDeviceLinearAllocator> ThreadIdleEvents;

	/** The ID of the master thread. Logging from other threads will be buffered for processing by the master thread. */
	std::atomic<uint32> MasterThreadId{FPlatformTLS::GetCurrentThreadId()};

	/** Whether to keep the thread running. */
	std::atomic<bool> bThreadActive{false};

	/** Whether the backlog is enabled. */
	bool bEnableBacklog = false;

	bool IsMasterThread(const uint32 ThreadId) const
	{
		return ThreadId == MasterThreadId.load(std::memory_order_relaxed);
	}

	void ThreadLoop();

	void FlushBufferedLines();
};

/**
 * A scoped lock for readers of the OutputDevices arrays.
 *
 * The read lock:
 * - Must be locked to read the OutputDevices arrays.
 * - Must be locked to write to unbuffered output devices.
 * - Must not be entered when the thread holds a write or master lock.
 */
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

/**
 * A scoped lock for writers of the OutputDevices arrays.
 *
 * The write lock has the same access as the master lock, and:
 * - Must be locked to add or remove output devices.
 * - Must not be entered when the thread holds a read, write, or master lock.
 */
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

/**
 * A scoped lock for readers of the OutputDevices arrays that need to access master thread state.
 *
 * The master lock has the same access as the read lock, and:
 * - Must be locked to write to buffered output devices.
 * - Must be locked while calling FlushBufferedLines().
 * - Must not be entered when the thread holds a write or master lock.
 * - May be locked when the thread holds a read lock.
 */
struct FOutputDevicesMasterScopeLock
{
	FORCEINLINE explicit FOutputDevicesMasterScopeLock(FOutputDeviceRedirectorState& InState)
		: State(InState)
	{
		State.OutputDevicesLock.WriteLock();
	}

	FORCEINLINE ~FOutputDevicesMasterScopeLock()
	{
		State.OutputDevicesLock.WriteUnlock();
	}

	FOutputDeviceRedirectorState& State;
};

void FOutputDeviceRedirectorState::ThreadLoop()
{
	const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();
	{
		FOutputDevicesMasterScopeLock Lock(*this);
		MasterThreadId.store(ThreadId, std::memory_order_relaxed);
	}

	while (IsMasterThread(ThreadId))
	{
		ThreadWakeEvent->Wait();
		do
		{
			FOutputDevicesMasterScopeLock Lock(*this);
			FlushBufferedLines();
		}
		while (!BufferedLines.IsEmpty());
		ThreadIdleEvents.Deplete([](FEvent* Event) { Event->Trigger(); });
	}
}

void FOutputDeviceRedirectorState::FlushBufferedLines()
{
	if (BufferedLines.IsEmpty())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDeviceRedirector::FlushBufferedLines);

	BufferedLines.Deplete([this](UE::Private::FOutputDeviceLine&& Line)
	{
		const double Time = Line.Time;
		const TCHAR* const Data = Line.Data;
		const FName Category = Line.Category;
		const ELogVerbosity::Type Verbosity = Line.Verbosity;

		for (FOutputDevice* OutputDevice : BufferedOutputDevices)
		{
			OutputDevice->Serialize(Data, Verbosity, Category, Time);
		}
	});
}

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

void FOutputDeviceRedirector::FlushThreadedLogs(EOutputDeviceRedirectorFlushOptions Options)
{
	if (FReadScopeLock Lock(State->ThreadLock); State->bThreadActive.load(std::memory_order_relaxed))
	{
		if (!EnumHasAnyFlags(Options, EOutputDeviceRedirectorFlushOptions::Async))
		{
			FEventRef IdleEvent(EEventMode::ManualReset);
			if (State->ThreadIdleEvents.EnqueueAndReturnWasEmpty(IdleEvent.Get()))
			{
				State->ThreadWakeEvent->Trigger();
			}
			IdleEvent->Wait();
		}
		return;
	}

	UE::Private::FOutputDevicesMasterScopeLock Lock(*State);
	State->FlushBufferedLines();
}

void FOutputDeviceRedirector::PanicFlushThreadedLogs()
{
	Flush();
}

void FOutputDeviceRedirector::SerializeBacklog(FOutputDevice* OutputDevice)
{
	FReadScopeLock ScopeLock(State->BacklogLock);
	for (const FBufferedLine& BacklogLine : State->BacklogLines)
	{
		OutputDevice->Serialize(BacklogLine.Data, BacklogLine.Verbosity, BacklogLine.Category, BacklogLine.Time);
	}
}

void FOutputDeviceRedirector::EnableBacklog(bool bEnable)
{
	FWriteScopeLock ScopeLock(State->BacklogLock);
	State->bEnableBacklog = bEnable;
	if (!bEnable)
	{
		State->BacklogLines.Empty();
	}
}

void FOutputDeviceRedirector::SetCurrentThreadAsMasterThread()
{
	const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();

	if (UE::Private::FOutputDevicesMasterScopeLock Lock(*State); State->MasterThreadId.load(std::memory_order_relaxed) == ThreadId)
	{
		return;
	}
	else
	{
		State->MasterThreadId.store(ThreadId, std::memory_order_relaxed);
		State->FlushBufferedLines();
	}

	if (FWriteScopeLock Lock(State->ThreadLock); State->bThreadActive.load(std::memory_order_relaxed))
	{
		State->bThreadActive.store(false, std::memory_order_relaxed);
		State->ThreadWakeEvent->Trigger();
		State->Thread.Join();
	}
}

bool FOutputDeviceRedirector::TryStartDedicatedMasterThread()
{
	if (!FApp::ShouldUseThreadingForPerformance())
	{
		return false;
	}

	if (FWriteScopeLock Lock(State->ThreadLock); !State->bThreadActive.load(std::memory_order_relaxed))
	{
		State->bThreadActive.store(true, std::memory_order_relaxed);
		State->ThreadWakeEvent->Trigger();
		State->Thread = FThread(TEXT("OutputDeviceRedirector"), [State = State.Get()] { State->ThreadLoop(); });
	}
	return true;
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
		FWriteScopeLock ScopeLock(State->BacklogLock);
		State->BacklogLines.Emplace(Data, Category, Verbosity, RealTime);
	}

	const auto EnqueueLine = [this, Data, Category, Verbosity, RealTime]
	{
		if (State->BufferedLines.EnqueueAndReturnWasEmpty(Data, Category, Verbosity, RealTime) &&
			State->bThreadActive.load(std::memory_order_relaxed))
		{
			State->ThreadWakeEvent->Trigger();
		}
	};

	const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();
	if (!State->IsMasterThread(ThreadId) || State->BufferedOutputDevices.IsEmpty())
	{
		EnqueueLine();
	}
	else if (UE::Private::FOutputDevicesMasterScopeLock MasterLock(*State); !State->IsMasterThread(ThreadId))
	{
		EnqueueLine();
	}
	else
	{
		State->FlushBufferedLines();

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
	UE::Private::FOutputDevicesMasterScopeLock Lock(*State);
	State->FlushBufferedLines();

	for (FOutputDevice* OutputDevice : State->BufferedOutputDevices)
	{
		OutputDevice->Flush();
	}

	for (FOutputDevice* OutputDevice : State->UnbufferedOutputDevices)
	{
		OutputDevice->Flush();
	}
}

void FOutputDeviceRedirector::TearDown()
{
	SetCurrentThreadAsMasterThread();

	Flush();

	TArray<FOutputDevice*> LocalBufferedDevices;
	TArray<FOutputDevice*> LocalUnbufferedDevices;

	{
		UE::Private::FOutputDevicesWriteScopeLock Lock(*State);
		LocalBufferedDevices = MoveTemp(State->BufferedOutputDevices);
		LocalUnbufferedDevices = MoveTemp(State->UnbufferedOutputDevices);
		State->BufferedOutputDevices.Empty();
		State->UnbufferedOutputDevices.Empty();
	}

	for (FOutputDevice* OutputDevice : LocalBufferedDevices)
	{
		OutputDevice->TearDown();
	}

	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->TearDown();
	}
}

bool FOutputDeviceRedirector::IsBacklogEnabled() const
{
	FReadScopeLock Lock(State->BacklogLock);
	return State->bEnableBacklog;
}

CORE_API FOutputDeviceRedirector* GetGlobalLogSingleton()
{
	return FOutputDeviceRedirector::Get();
}
