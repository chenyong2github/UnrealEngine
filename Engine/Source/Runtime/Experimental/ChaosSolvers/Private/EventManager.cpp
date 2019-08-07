// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EventManager.h"

namespace Chaos
{
	void FEventManager::Reset()
	{
		for (FEventContainerPtr Container : EventContainers)
		{
			delete Container;
			Container = nullptr;
		}
		EventContainers.Reset();
	}

	void FEventManager::UnregisterEvent(const FEventID& EventID)
	{
		if (EventID < EventContainers.Num())
		{
			delete EventContainers[EventID];
			EventContainers.RemoveAt(EventID);
		}
	}

	void FEventManager::UnregisterHandler(const FEventID& EventID, const void* InHandler)
	{
		checkf(EventID < EventContainers.Num(), TEXT("Unregistering event Handler for an event ID that does not exist"));
		EventContainers[EventID]->UnregisterHandler(InHandler);
	}

	void FEventManager::FillProducerData(const Chaos::FPBDRigidsSolver* Solver)
	{
		for (FEventContainerPtr EventContainer : EventContainers)
		{
			if (EventContainer)
			{
				EventContainer->InjectProducerData(Solver);
			}
		}
	}

	void FEventManager::FlipBuffersIfRequired()
	{
		if (BufferMode == EMultiBufferMode::Double)
		{
			ResourceLock.WriteLock();
		}

		for (FEventContainerPtr EventContainer : EventContainers)
		{
			if (EventContainer)
			{
				EventContainer->FlipBufferIfRequired();
			}
		}

		if (BufferMode == EMultiBufferMode::Double)
		{
			ResourceLock.WriteUnlock();
		}
	}

	void FEventManager::DispatchEvents()
	{
		if (BufferMode == EMultiBufferMode::Double)
		{
			ResourceLock.ReadLock();
		}

		for (FEventContainerPtr EventContainer : EventContainers)
		{
			if (EventContainer)
			{
				EventContainer->DispatchConsumerData();
			}
		}

		if (BufferMode == EMultiBufferMode::Double)
		{
			ResourceLock.ReadUnlock();
		}

	}

	void FEventManager::InternalRegisterInjector(const FEventID& EventID, const FEventContainerPtr& Container)
	{
		if (EventID > EventContainers.Num())
		{
			for (int i = EventContainers.Num(); i < EventID; i++)
			{
				EventContainers.Push(nullptr);
			}
		}

		EventContainers.EmplaceAt(EventID, Container);
	}

	int32 FEventManager::EncodeCollisionIndex(int32 ActualCollisionIndex, bool bSwapOrder)
	{
		return bSwapOrder ? (ActualCollisionIndex | (1 << 31)) : ActualCollisionIndex;
	}

	int32 FEventManager::DecodeCollisionIndex(int32 EncodedCollisionIdx, bool& bSwapOrder)
	{
		bSwapOrder = EncodedCollisionIdx & (1 << 31);
		return EncodedCollisionIdx & ~(1 << 31);
	}


}
