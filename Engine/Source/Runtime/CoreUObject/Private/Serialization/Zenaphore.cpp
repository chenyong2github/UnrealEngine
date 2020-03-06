// Copyright Epic Games, Inc. All Rights Reserved.

#include "Zenaphore.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"

FZenaphore::FZenaphore()
{
	Event = FPlatformProcess::GetSynchEventFromPool(true);
}

FZenaphore::~FZenaphore()
{
	FPlatformProcess::ReturnSynchEventToPool(Event);
}

void FZenaphore::NotifyOne()
{
	for (;;)
	{
		FZenaphoreWaiterNode* Waiter = HeadWaiter.Load();
		if (!Waiter)
		{
			return;
		}
		if (HeadWaiter.CompareExchange(Waiter, Waiter->Next))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ZenaphoreTrigger);
			FScopeLock Lock(&Mutex);
			Waiter->bTriggered = true;
			Event->Trigger();
			return;
		}
	}
}

void FZenaphore::NotifyAll()
{
	FZenaphoreWaiterNode* Waiter = HeadWaiter.Load();
	while (Waiter)
	{
		if (HeadWaiter.CompareExchange(Waiter, Waiter->Next))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ZenaphoreTrigger);
			FScopeLock Lock(&Mutex);
			Waiter->bTriggered = true;
			Event->Trigger();
		}
	}
}

void FZenaphoreWaiter::Wait()
{
	if (SpinCount == 0)
	{
		FZenaphoreWaiterNode* OldHeadWaiter = nullptr;
		WaiterNode.bTriggered = false;
		WaiterNode.Next = nullptr;
		while (!Outer.HeadWaiter.CompareExchange(OldHeadWaiter, &WaiterNode))
		{
			WaiterNode.Next = OldHeadWaiter;
		}
		++SpinCount;
	}
	else
	{
		for (;;)
		{
			Outer.Event->Wait(INT32_MAX, true);
			FScopeLock Lock(&Outer.Mutex);
			if (WaiterNode.bTriggered)
			{
				Outer.Event->Reset();
				break;
			}
		}
		SpinCount = 0;
	}
}
