// Copyright Epic Games, Inc. All Rights Reserved.

#include "EventManager.h"

namespace Chaos
{
	template <typename Traits>
	void TEventManager<Traits>::Reset()
	{
		ContainerLock.WriteLock();
		for (TEventContainerBasePtr<Traits> Container : EventContainers)
		{
			delete Container;
			Container = nullptr;
		}
		EventContainers.Reset();
		ContainerLock.WriteUnlock();
	}

	template <typename Traits>
	void TEventManager<Traits>::UnregisterEvent(const FEventID& EventID)
	{
		ContainerLock.WriteLock();
		if (EventID < EventContainers.Num())
		{
			delete EventContainers[EventID];
			EventContainers.RemoveAt(EventID);
		}
		ContainerLock.WriteUnlock();
	}

	template <typename Traits>
	void TEventManager<Traits>::UnregisterHandler(const FEventID& EventID, const void* InHandler)
	{
		ContainerLock.WriteLock();
		checkf(EventID < EventContainers.Num(), TEXT("Unregistering event Handler for an event ID that does not exist"));
		EventContainers[EventID]->UnregisterHandler(InHandler);
		ContainerLock.WriteUnlock();
	}

	template <typename Traits>
	void TEventManager<Traits>::FillProducerData(const Chaos::TPBDRigidsSolver<Traits>* Solver)
	{
		ContainerLock.ReadLock();
		for (TEventContainerBasePtr<Traits> EventContainer : EventContainers)
		{
			if (EventContainer)
			{
				EventContainer->InjectProducerData(Solver);
			}
		}
		ContainerLock.ReadUnlock();
	}

	template <typename Traits>
	void TEventManager<Traits>::FlipBuffersIfRequired()
	{
		if (BufferMode == EMultiBufferMode::Double)
		{
			ResourceLock.WriteLock();
		}

		ContainerLock.ReadLock();
		for (TEventContainerBasePtr<Traits> EventContainer : EventContainers)
		{
			if (EventContainer)
			{
				EventContainer->FlipBufferIfRequired();
			}
		}
		ContainerLock.ReadUnlock();

		if (BufferMode == EMultiBufferMode::Double)
		{
			ResourceLock.WriteUnlock();
		}
	}

	template <typename Traits>
	void TEventManager<Traits>::DispatchEvents()
	{
		if (BufferMode == EMultiBufferMode::Double)
		{
			ResourceLock.ReadLock();
		}

		ContainerLock.ReadLock();
		for (TEventContainerBasePtr<Traits> EventContainer : EventContainers)
		{
			if (EventContainer)
			{
				EventContainer->DispatchConsumerData();
			}
		}
		ContainerLock.ReadUnlock();

		if (BufferMode == EMultiBufferMode::Double)
		{
			ResourceLock.ReadUnlock();
		}

	}

	template <typename Traits>
	void TEventManager<Traits>::InternalRegisterInjector(const FEventID& EventID, const TEventContainerBasePtr<Traits>& Container)
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

	template <typename Traits>
	int32 TEventManager<Traits>::EncodeCollisionIndex(int32 ActualCollisionIndex, bool bSwapOrder)
	{
		return bSwapOrder ? (ActualCollisionIndex | (1 << 31)) : ActualCollisionIndex;
	}

	template <typename Traits>
	int32 TEventManager<Traits>::DecodeCollisionIndex(int32 EncodedCollisionIdx, bool& bSwapOrder)
	{
		bSwapOrder = EncodedCollisionIdx & (1 << 31);
		return EncodedCollisionIdx & ~(1 << 31);
	}

#define EVOLUTION_TRAIT(Trait) template class CHAOSSOLVERS_API TEventManager<Trait>;
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
}
