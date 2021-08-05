// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataRequest.h"

#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "HAL/Event.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeRWLock.h"
#include "Templates/RefCounting.h"
#include <atomic>

namespace UE::DerivedData::Private
{

/** A one-time-use event to work around the lack of condition variables. */
struct FRequestBarrierEvent : public FRefCountBase
{
	FEventRef Event{EEventMode::ManualReset};
};

class FRequestOwnerShared final : public FRequestOwnerSharedBase
{
public:
	explicit FRequestOwnerShared(EPriority Priority);
	~FRequestOwnerShared() final = default;

	void Begin(IRequest* Request) final;
	TRefCountPtr<IRequest> End(IRequest* Request) final;

	void BeginBarrier() final;
	void EndBarrier() final;

	inline EPriority GetPriority() const final { return Priority; }
	inline bool IsCanceled() const final { return bIsCanceled; }

	void SetPriority(EPriority Priority) final;
	void Cancel() final;
	void Wait() final;
	bool Poll() const final;

	void KeepAlive() final;
	void Destroy() final;

private:
	mutable FRWLock Lock;
	TArray<TRefCountPtr<IRequest>, TInlineAllocator<1>> Requests;
	TRefCountPtr<FRequestBarrierEvent> BarrierEvent;
	uint32 BarrierCount{0};
	EPriority Priority{EPriority::Normal};
	uint8 bPriorityChangedInBarrier : 1;
	uint8 bBeginExecuted : 1;
	bool bIsCanceled{false};
	bool bKeepAlive{false};
};

FRequestOwnerShared::FRequestOwnerShared(EPriority NewPriority)
	: Priority(NewPriority)
	, bPriorityChangedInBarrier(false)
	, bBeginExecuted(false)
{
	AddRef(); // Release is called by Destroy.
}

void FRequestOwnerShared::Begin(IRequest* Request)
{
	AddRef();
	EPriority NewPriority;
	{
		FWriteScopeLock WriteLock(Lock);
		checkf(BarrierCount > 0 || !bBeginExecuted,
			TEXT("At least one FRequestBarrier must be in scope when beginning a request after the first request. ")
			TEXT("The overload of End that invokes a callback handles this automatically for most use cases."));
		check(Request);
		Requests.Add(Request);
		bBeginExecuted = true;
		if (!bPriorityChangedInBarrier)
		{
			return;
		}
		NewPriority = Priority;
	}
	// Loop until priority is stable. Another thread may be changing the priority concurrently.
	for (EPriority CheckPriority; ; NewPriority = CheckPriority)
	{
		Request->SetPriority(NewPriority);
		CheckPriority = (FReadScopeLock(Lock), Priority);
		if (CheckPriority == NewPriority)
		{
			break;
		}
	}
}

TRefCountPtr<IRequest> FRequestOwnerShared::End(IRequest* Request)
{
	ON_SCOPE_EXIT { Release(); };
	FWriteScopeLock WriteLock(Lock);
	check(Request);
	TRefCountPtr<IRequest>* RequestPtr = Requests.FindByKey(Request);
	check(RequestPtr);
	TRefCountPtr<IRequest> RequestRef = MoveTemp(*RequestPtr);
	Requests.RemoveAtSwap(UE_PTRDIFF_TO_INT32(RequestPtr - Requests.GetData()), 1, /*bAllowShrinking*/ false);
	return RequestRef;
}

void FRequestOwnerShared::BeginBarrier()
{
	AddRef();
	FWriteScopeLock WriteLock(Lock);
	++BarrierCount;
}

void FRequestOwnerShared::EndBarrier()
{
	ON_SCOPE_EXIT { Release(); };
	TRefCountPtr<FRequestBarrierEvent> LocalBarrierEvent;
	if (FWriteScopeLock WriteLock(Lock); --BarrierCount == 0)
	{
		bPriorityChangedInBarrier = false;
		LocalBarrierEvent = MoveTemp(BarrierEvent);
		BarrierEvent = nullptr;
	}
	if (LocalBarrierEvent)
	{
		LocalBarrierEvent->Event->Trigger();
	}
}

void FRequestOwnerShared::SetPriority(EPriority NewPriority)
{
	TArray<TRefCountPtr<IRequest>, TInlineAllocator<16>> LocalRequests;
	if (FWriteScopeLock WriteLock(Lock); Priority == NewPriority)
	{
		return;
	}
	else
	{
		Priority = NewPriority;
		LocalRequests = Requests;
		bPriorityChangedInBarrier = (BarrierCount > 0);
	}

	for (IRequest* Request : LocalRequests)
	{
		Request->SetPriority(NewPriority);
	}
}

void FRequestOwnerShared::Cancel()
{
	for (;;)
	{
		TRefCountPtr<IRequest> LocalRequest;
		TRefCountPtr<FRequestBarrierEvent> LocalBarrierEvent;

		{
			FWriteScopeLock WriteLock(Lock);
			bIsCanceled = true;

			if (!Requests.IsEmpty())
			{
				LocalRequest = Requests.Last();
			}
			else if (BarrierCount > 0)
			{
				LocalBarrierEvent = BarrierEvent;
				if (!LocalBarrierEvent)
				{
					LocalBarrierEvent = BarrierEvent = new FRequestBarrierEvent;
				}
			}
			else
			{
				return;
			}
		}

		if (LocalRequest)
		{
			LocalRequest->Cancel();
		}
		else
		{
			LocalBarrierEvent->Event->Wait();
		}
	}
}

void FRequestOwnerShared::Wait()
{
	for (;;)
	{
		TRefCountPtr<IRequest> LocalRequest;
		TRefCountPtr<FRequestBarrierEvent> LocalBarrierEvent;

		{
			FWriteScopeLock WriteLock(Lock);

			if (!Requests.IsEmpty())
			{
				LocalRequest = Requests.Last();
			}
			else if (BarrierCount > 0)
			{
				LocalBarrierEvent = BarrierEvent;
				if (!LocalBarrierEvent)
				{
					LocalBarrierEvent = BarrierEvent = new FRequestBarrierEvent;
				}
			}
			else
			{
				return;
			}
		}

		if (LocalRequest)
		{
			LocalRequest->Wait();
		}
		else
		{
			LocalBarrierEvent->Event->Wait();
		}
	}
}

bool FRequestOwnerShared::Poll() const
{
	FReadScopeLock ReadLock(Lock);
	return Requests.IsEmpty() && BarrierCount == 0;
}

void FRequestOwnerShared::KeepAlive()
{
	FWriteScopeLock WriteLock(Lock);
	bKeepAlive = true;
}

void FRequestOwnerShared::Destroy()
{
	const bool bLocalKeepAlive = (FWriteScopeLock(Lock), bKeepAlive);
	if (!bLocalKeepAlive)
	{
		Cancel();
	}
	Release();
}

} // UE::DerivedData::Private

namespace UE::DerivedData
{

FRequestOwner::FRequestOwner(EPriority Priority)
	: Owner(new Private::FRequestOwnerShared(Priority))
{
}

} // UE::DerivedData
