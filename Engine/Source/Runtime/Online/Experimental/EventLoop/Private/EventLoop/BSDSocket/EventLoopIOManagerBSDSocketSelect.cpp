// Copyright Epic Games, Inc. All Rights Reserved.

#include "EventLoop/BSDSocket/EventLoopIOManagerBSDSocketSelect.h"
#include "EventLoop/BSDSocket/BSDSocketTypesPrivate.h"
#include "EventLoop/EventLoopLog.h"
#include "EventLoop/IEventLoop.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"
#include "Stats/Stats.h"

#if PLATFORM_HAS_BSD_SOCKETS

namespace UE::EventLoop {

class FIOManagerBSDSocketSelectImpl final : public IIOManager
{
public:
	FIOManagerBSDSocketSelectImpl(IEventLoop& EventLoop);
	virtual ~FIOManagerBSDSocketSelectImpl() = default;
	virtual bool Init() override;
	virtual void Shutdown() override;
	virtual void Notify() override;
	virtual void Poll(FTimespan WaitTime) override;

	void PollInternal(FTimespan WaitTime);

	FIOAccessBSDSocket& GetIOAccess()
	{
		return IOAccess;
	}

private:
	using FStorageType = FIOAccessBSDSocket::FStorageType;
	using FInternalHandleArryType = FStorageType::FInternalHandleArryType;
	using FInternalHandle = FStorageType::FInternalHandle;

	IEventLoop& EventLoop;
	FStorageType IORequestStorage;
	FIOAccessBSDSocket IOAccess;
	TAtomic<bool> bAsyncSignal;
};

FIOManagerBSDSocketSelectImpl::FIOManagerBSDSocketSelectImpl(IEventLoop& InEventLoop)
	: EventLoop(InEventLoop)
	, IOAccess(IORequestStorage)
	, bAsyncSignal(false)
{
}

bool FIOManagerBSDSocketSelectImpl::Init()
{
	IORequestStorage.Init();
	return true;
}

void FIOManagerBSDSocketSelectImpl::Shutdown()
{
}

void FIOManagerBSDSocketSelectImpl::Notify()
{
	bAsyncSignal = true;
}

void FIOManagerBSDSocketSelectImpl::Poll(FTimespan WaitTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_EventLoop_FIOManagerBSDSocketSelectImpl_Poll);

	// The base BSD socket polling implementation relies on using socket select which cannot be
	// woken by another thread. To mitigate this issue, many smaller calls to PollInternal are done
	// while no signal is received.

	const FTimespan MaxWaitTime = FTimespan::FromMilliseconds(10);
	const double StartTime = FPlatformTime::Seconds();

	for (;;)
	{
		const double CurrentTime = FPlatformTime::Seconds();
		const FTimespan CurrentDuration = FTimespan::FromSeconds(CurrentTime - StartTime);
		FTimespan CurrentWaitTime = FMath::Min(WaitTime - CurrentDuration, MaxWaitTime);

		// If a signal was received before polling, still poll for any ready events using a timeout of 0.
		if (bAsyncSignal)
		{
			CurrentWaitTime = FTimespan::Zero();
		}

		if (CurrentWaitTime.GetTicks() < 0)
		{
			break;
		}

		PollInternal(CurrentWaitTime);

		if (bAsyncSignal)
		{
			break;
		}
	}

	// Reset signal. Resetting is safe here as setting the async signal indicates that the poll should be exited.
	bAsyncSignal = false;
}

void FIOManagerBSDSocketSelectImpl::PollInternal(FTimespan WaitTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_EventLoop_FIOManagerBSDSocketSelectImpl_PollInternal);

	// Process queued actions.
	IORequestStorage.Update();

	// When there is no work, block the loop here.
	if (IORequestStorage.IsEmpty())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_EventLoop_FIOManagerBSDSocketSelectImpl_PollInternal_IdleSleep);
		FPlatformProcess::SleepNoStats(WaitTime.GetTotalSeconds());
		return;
	}

	fd_set SocketReadSet;
	fd_set SocketWriteSet;
	fd_set SocketExceptionSet;
	SOCKET MaxFd = INVALID_SOCKET;

	// Build the FD_SETs.
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_EventLoop_FIOManagerBSDSocketSelectImpl_PollInternal_BuildFdSets);
		FD_ZERO(&SocketReadSet);
		FD_ZERO(&SocketWriteSet);
		FD_ZERO(&SocketExceptionSet);

		for (TPair<FInternalHandle, FIORequestBSDSocket&>& IORequestEntry : IORequestStorage)
		{
			FIORequestBSDSocket& IORequest = IORequestEntry.Value;

			if (MaxFd == INVALID_SOCKET)
			{
				MaxFd = IORequest.Socket;
			}
			else
			{
				MaxFd = FMath::Max(MaxFd, IORequest.Socket);
			}

			if (EnumHasAnyFlags(IORequest.Flags, EIOFlags::Read))
			{
				FD_SET(IORequest.Socket, &SocketReadSet);
			}

			if (EnumHasAnyFlags(IORequest.Flags, EIOFlags::Write))
			{
				FD_SET(IORequest.Socket, &SocketWriteSet);
				FD_SET(IORequest.Socket, &SocketExceptionSet);
			}
		}
	}

#if PLATFORM_HAS_BSD_SOCKET_FEATURE_SELECT
	// convert WaitTime to a timeval
	timeval Time;
	Time.tv_sec = (int32)WaitTime.GetTotalSeconds();
	Time.tv_usec = WaitTime.GetFractionMicro();

	timeval* TimePointer = WaitTime.GetTicks() >= 0 ? &Time : nullptr;

	// Poll for socket status.
	int32 SelectStatus = 0;
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_EventLoop_FIOManagerBSDSocketSelectImpl_PollInternal_SocketSelect);
		SelectStatus = select(IntCastChecked<int>(MaxFd + 1), &SocketReadSet, &SocketWriteSet, NULL, TimePointer);
	}
	if (SelectStatus > 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_EventLoop_FIOManagerBSDSocketSelectImpl_PollInternal_HandleIOState);
		// Check request state and trigger callbacks if needed.
		for (TPair<FInternalHandle, FIORequestBSDSocket&>& IORequestEntry : IORequestStorage)
		{
			FIORequestBSDSocket& IORequest = IORequestEntry.Value;
			EIOFlags SignaledFlags = EIOFlags::None;

			if (EnumHasAnyFlags(IORequest.Flags, EIOFlags::Read) && FD_ISSET(IORequest.Socket, &SocketReadSet))
			{
				SignaledFlags |= EIOFlags::Read;
			}

			if (EnumHasAnyFlags(IORequest.Flags, EIOFlags::Write) &&
				(FD_ISSET(IORequest.Socket, &SocketWriteSet) || FD_ISSET(IORequest.Socket, &SocketExceptionSet)))
			{
				SignaledFlags |= EIOFlags::Write;
			}

			if (SignaledFlags != EIOFlags::None)
			{
				// Signal event status.
				IORequest.Callback(SignaledFlags);
			}
		}
	}
	else if (SelectStatus < 0)
	{
		// Todo: handle error by iterating set to see which socket caused the failure.
		UE_LOG(LogEventLoop, Error, TEXT("[FIOManagerBSDSocketSelectImpl::PollInternal] Select error"));
		EventLoop.RequestShutdown();
	}
#else
	UE_LOG(LogEventLoop, Fatal, TEXT("This platform doesn't support FIOManagerSocketBSDSelectImpl"));
#endif
}

//-----------------------
// Pimpl implementation.
//-----------------------

FIOManagerBSDSocketSelect::FIOManagerBSDSocketSelect(IEventLoop& EventLoop, FParams&&)
	: Impl(MakeShared<FIOManagerBSDSocketSelectImpl>(EventLoop))
{
}

bool FIOManagerBSDSocketSelect::Init()
{
	return Impl->Init();
}

void FIOManagerBSDSocketSelect::Shutdown()
{
	Impl->Shutdown();
}

void FIOManagerBSDSocketSelect::Notify()
{
	Impl->Notify();
}

void FIOManagerBSDSocketSelect::Poll(FTimespan WaitTime)
{
	Impl->Poll(WaitTime);
}

FIOManagerBSDSocketSelect::FIOAccess& FIOManagerBSDSocketSelect::GetIOAccess()
{
	return Impl->GetIOAccess();
}

/* UE::EventLoop */ }

#endif // PLATFORM_HAS_BSD_SOCKETS
