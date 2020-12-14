// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreTypes.h"
#include "Math/RandomStream.h"
#include "Experimental/Containers/FAAArrayQueue.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include <atomic>

namespace LocalQueue_Impl
{
template<uint32 NumItems>
class TWorkStealingQueueBase2
{
	enum class ESlotState : uintptr_t
	{
		Free  = 0, //The slot is free and items can be put there
		Taken = 1, //The slot is in the proccess of beeing stolen 
	};

protected:
	//insert an item at the head position (this can only safe on a single thread, shared with Get) 
	inline bool Put(uintptr_t Item)
	{
		checkSlow(Item != uintptr_t(ESlotState::Free));
		checkSlow(Item != uintptr_t(ESlotState::Taken));

		uint32 Idx = (Head + 1) % NumItems;
		uintptr_t Slot = ItemSlots[Idx].Value.load(std::memory_order_relaxed);

		if (Slot == uintptr_t(ESlotState::Free) && ItemSlots[Idx].Value.compare_exchange_strong(Slot, Item, std::memory_order_relaxed))
		{
			Head++;
			checkSlow(Head % NumItems == Idx);
			return true;		
		}
		return false;
	}

	//remove an item at the head position in FIFO order (this can only safe on a single thread, shared with Put) 
	inline bool Get(uintptr_t& Item)
	{
		uint32 Idx = Head % NumItems;
		uintptr_t Slot = ItemSlots[Idx].Value.load(std::memory_order_relaxed);

		if (Slot > uintptr_t(ESlotState::Taken) && ItemSlots[Idx].Value.compare_exchange_strong(Slot, uintptr_t(ESlotState::Free), std::memory_order_relaxed))
		{
			Head--;
			checkSlow((Head + 1) % NumItems == Idx);
			Item = Slot;
			return true;
		}
		return false;
	}

	//remove an item at the tail position in LIFO order (this can be done from any thread including the one that accesses the head)
	inline bool Steal(uintptr_t& Item)
	{
		do
		{
			uint32 IdxVer = Tail.load(std::memory_order_relaxed);
			uint32 Idx = IdxVer % NumItems;
			uintptr_t Slot = ItemSlots[Idx].Value.load(std::memory_order_relaxed);

			if (Slot == uintptr_t(ESlotState::Free))
			{
				return false;
			}
			else if (Slot != uintptr_t(ESlotState::Taken) && ItemSlots[Idx].Value.compare_exchange_weak(Slot, uintptr_t(ESlotState::Taken), std::memory_order_relaxed))
			{
				if(IdxVer == Tail.load(std::memory_order_relaxed))
				{
					uint32 Prev = Tail.fetch_add(1, std::memory_order_relaxed);
					checkSlow(Prev % NumItems == Idx);
					ItemSlots[Idx].Value.store(uintptr_t(ESlotState::Free), std::memory_order_relaxed);
					Item = Slot;
					return true;
				}
				ItemSlots[Idx].Value.store(Slot, std::memory_order_relaxed);
			}
		} while(true);
	}

	/* Untested
	//add an item at the tail position (this can be done from any thread including the one that accesses the head)
	inline bool PutTail(uintptr_t Item)
	{
		do
		{
			uint32 IdxVer = Tail.load(std::memory_order_relaxed);
			uint32 Idx = (IdxVer - 1) % NumItems;
			uintptr_t Slot = ItemSlots[Idx].Value.load(std::memory_order_relaxed);

			if (Slot > uintptr_t(ESlotState::Taken))
			{
				return false;
			}
			else if (Slot == uintptr_t(ESlotState::Free) && ItemSlots[Idx].Value.compare_exchange_weak(Slot, uintptr_t(ESlotState::Taken), std::memory_order_relaxed))
			{
				if(IdxVer == Tail.load(std::memory_order_relaxed))
				{
					uint32 Prev = Tail.fetch_sub(1, std::memory_order_relaxed);
					checkSlow((Prev - 1) % NumItems == Idx);
					ItemSlots[Idx].Value.store(Item, std::memory_order_relaxed);
					return true;
				}
				ItemSlots[Idx].Value.store(uintptr_t(ESlotState::Free), std::memory_order_relaxed);
			}
		} while(true);
	}
	*/

private:
	struct FAlignedElement
	{
		alignas(PLATFORM_CACHE_LINE_SIZE * 2) std::atomic<uintptr_t> Value;
	};

	alignas(PLATFORM_CACHE_LINE_SIZE * 2) uint32 Head { ~0u };
	alignas(PLATFORM_CACHE_LINE_SIZE * 2) std::atomic_uint Tail { 0 };
	alignas(PLATFORM_CACHE_LINE_SIZE * 2) FAlignedElement ItemSlots[NumItems] = {};
};

template<typename Type, uint32 NumItems>
class TWorkStealingQueue2 final : protected TWorkStealingQueueBase2<NumItems>
{
	using PointerType = Type*;

public:
	inline bool Put(PointerType Item)
	{
		return TWorkStealingQueueBase2<NumItems>::Put(reinterpret_cast<uintptr_t>(Item));
	}

	inline bool Get(PointerType& Item)
	{
		return TWorkStealingQueueBase2<NumItems>::Get(reinterpret_cast<uintptr_t&>(Item));
	}

	inline bool Steal(PointerType& Item)
	{
		return TWorkStealingQueueBase2<NumItems>::Steal(reinterpret_cast<uintptr_t&>(Item));
	}
};
}

/********************************************************************************************************************************************
 * A LocalQueueRegistry is a collection of LockFree queues that store pointers to Items, there are ThreadLocal LocalQueues with LocalItems. *
 * LocalQueues can only be Enqueued and Dequeued by the current Thread they were installed on. But Items can be stolen from any Thread      *
 * There is a global OverflowQueue than is used when a LocalQueue goes out of scope to dump all the remaining Items in                      *
 * or when a Thread has no LocalQueue installed or when the LocalQueue is at capacity. A new LocalQueue is registers itself always.         *
 * A Dequeue Operation can only be done starting from a LocalQueue, than the GlobalQueue will be checked.                                   *
 * Finally Items might get Stolen from other LocalQueues that are registered with the LocalQueueRegistry.                                   *
 ********************************************************************************************************************************************/
template<typename ItemType, uint32 NumPriorities, uint32 NumLocalItems = 1024>
class TLocalQueueRegistry
{
public:
	class TLocalQueue;

	// FOutOfWork is used to track the time while a worker is waiting for work
	// this happens after a worker was unable to aquire any task from the queues and until it finds work again or it goes into drowsing state.
	class FOutOfWork
	{
	private:
		template<typename, uint32, uint32>
		friend class TLocalQueueRegistry;

		std::atomic_int& NumWorkersLookingForWork;
		bool ActivelyLookingForWork = false;

		static uint32 WorkerLookingForWorkTraceId;

		inline FOutOfWork(std::atomic_int& InNumWorkersLookingForWork) : NumWorkersLookingForWork(InNumWorkersLookingForWork)
		{
#if CPUPROFILERTRACE_ENABLED
			if (WorkerLookingForWorkTraceId == 0) 
			{
				WorkerLookingForWorkTraceId = FCpuProfilerTrace::OutputEventType("TaskWorkerIsLookingForWork");
			}
#endif
		}

	public:
		inline ~FOutOfWork()
		{
			Stop();
		}

		inline bool Start()
		{
			if (!ActivelyLookingForWork)
			{
#if CPUPROFILERTRACE_ENABLED
				FCpuProfilerTrace::OutputBeginEvent(WorkerLookingForWorkTraceId);
#endif
				NumWorkersLookingForWork.fetch_add(1, std::memory_order_relaxed);
				ActivelyLookingForWork = true;
				return true;
			}
			return false;
		}

		inline bool Stop()
		{
			if (ActivelyLookingForWork)
			{
#if CPUPROFILERTRACE_ENABLED
				FCpuProfilerTrace::OutputEndEvent();
#endif
				NumWorkersLookingForWork.fetch_sub(1, std::memory_order_release);
				ActivelyLookingForWork = false;
				return true;
			}
			return false;
		}
	};

private:
	using FLocalQueueType	 = LocalQueue_Impl::TWorkStealingQueue2<ItemType, NumLocalItems>;
	using FOverflowQueueType = FAAArrayQueue<ItemType>;
	using DequeueHazard		 = typename FOverflowQueueType::DequeueHazard;

private:
	// FLocalQueueCollection is a read only collection of LocalQueues registerd with this Registry
	struct FLocalQueueCollection
	{
		TArray<TLocalQueue*, TInlineAllocator<32>> LocalQueues;
		TLocalQueue* RemovedQueue = nullptr;

		FLocalQueueCollection(FLocalQueueCollection* Previous) : LocalQueues(Previous->LocalQueues)
		{
		}

		FLocalQueueCollection() = default;

		~FLocalQueueCollection()
		{
			//if the registry also request deletion of a queue (removal case)
			if (RemovedQueue)
			{
				RemovedQueue->~TLocalQueue();
				FMemory::Free(RemovedQueue);
			}
		}
	};
	using FStealHazard = THazardPointer<FLocalQueueCollection, true>;

public:
	class TLocalQueue
	{
		template<typename, uint32, uint32>
		friend class TLocalQueueRegistry;

	public:
		TLocalQueue(TLocalQueueRegistry& InRegistry) : Registry(&InRegistry)
		{
			checkSlow(Registry);
			StealHazard = FStealHazard(Registry->QueueCollection, Registry->HazardsCollection);
			Registry->AddLocalQueue(StealHazard, this);
			for (int PriorityIndex = 0; PriorityIndex < NumPriorities; PriorityIndex++)
			{
				DequeueHazards[PriorityIndex] = Registry->OverflowQueues[PriorityIndex].getHeadHazard();
			}
		}

		static TLocalQueue* AllocateLocalQueue(TLocalQueueRegistry& InRegistry)
		{
			void* Memory = FMemory::Malloc(sizeof(TLocalQueue), 128u);
			return new (Memory) TLocalQueue(InRegistry);
		}

		//delete a queue
		//WorkeOwned means that the queue will not be automatically deleted when removal succeded.
		//It is a special case where the memory for the local queues is alloated linearly by the Scheduler for improved stealing performance.
		static void DeleteLocalQueue(TLocalQueue* Queue, bool WorkerOwned = false)
		{
			TLocalQueueRegistry* Registry = Queue->Registry;
			Queue->Registry = nullptr;

			for (int32 PriorityIndex = 0; PriorityIndex < NumPriorities; PriorityIndex++)
			{
				while (true)
				{
					ItemType* Item;
					if (!Queue->LocalQueues[PriorityIndex].Get(Item))
					{
						break;
					}
					Registry->OverflowQueues[PriorityIndex].enqueue(Item);
				}
			}
			Registry->DeleteLocalQueue(Queue->StealHazard, Queue, WorkerOwned);
		}

		//add an item to the local queue and overflow into the global queue if full
		// returns true if we should wake a worker
		inline bool Enqueue(ItemType* Item, uint32 PriorityIndex)
		{
			checkSlow(Registry);
			checkSlow(PriorityIndex < NumPriorities);
			checkSlow(Item != nullptr);

			if (!LocalQueues[PriorityIndex].Put(Item))
			{
				Registry->OverflowQueues[PriorityIndex].enqueue(Item);
				return Registry->AnyWorkerLookingForWork();
			}
			return (LocalTasksSinceLastDequeue++ != 0) && Registry->AnyWorkerLookingForWork();
		}

		inline ItemType* DequeueLocal()
		{
			LocalTasksSinceLastDequeue = 0;
			for (int32 PriorityIndex = 0; PriorityIndex < NumPriorities; PriorityIndex++)
			{
				ItemType* Item;
				if (LocalQueues[PriorityIndex].Get(Item))
				{
					return Item;
				}
			}
			return nullptr;
		}

		inline ItemType* DequeueGlobal()
		{
			if (Registry->NumActiveWorkers.load(std::memory_order_relaxed) >= (2 * Registry->NumWorkersLookingForWork.load(std::memory_order_relaxed) - 1))
			{
				for (int32 PriorityIndex = 0; PriorityIndex < NumPriorities; PriorityIndex++)
				{
					ItemType* Item = Registry->OverflowQueues[PriorityIndex].dequeue(DequeueHazards[PriorityIndex]);
					if (Item)
					{
						return Item;
					}
				}
			}
			return nullptr;
		}

		inline ItemType* DequeueSteal()
		{
			if (Registry->NumActiveWorkers.load(std::memory_order_relaxed) >= (2 * Registry->NumWorkersLookingForWork.load(std::memory_order_relaxed) - 1))
			{
				if (CachedRandomIndex == InvalidIndex)
				{
					CachedRandomIndex = Random.GetUnsignedInt();
				}

				ItemType* Result = Registry->StealItem(StealHazard, CachedRandomIndex, CachedPriorityIndex);
				if (Result)
				{
					return Result;
				}
			}
			return nullptr;
		}

	private:
		static constexpr uint32	InvalidIndex = ~0u;
		FLocalQueueType			LocalQueues[NumPriorities];
		DequeueHazard			DequeueHazards[NumPriorities];
		FStealHazard			StealHazard;
		TLocalQueueRegistry*	Registry;
		FRandomStream			Random;
		uint32					CachedRandomIndex = InvalidIndex;
		uint32					CachedPriorityIndex = 0;
		uint32					LocalTasksSinceLastDequeue = 0;
	};

	TLocalQueueRegistry()
	{
		QueueCollection.store(new FLocalQueueCollection(), std::memory_order_relaxed);
	}

private:
	// add a queue to the Registry
	void AddLocalQueue(FStealHazard& Hazard, TLocalQueue* QueueToAdd)
	{
		NumActiveWorkers.fetch_add(1, std::memory_order_relaxed);
		while(true)
		{
			FLocalQueueCollection* Previous = Hazard.Get();
			checkSlow(Previous->RemovedQueue == nullptr);
			FLocalQueueCollection* Copy = new FLocalQueueCollection(Previous);
			Copy->LocalQueues.Add(QueueToAdd);
			if (!QueueCollection.compare_exchange_strong(Previous, Copy, std::memory_order_release, std::memory_order_relaxed))
			{
				delete Copy;
				continue;
			}
			HazardsCollection.Delete(Previous);
			Hazard.Retire();
			return;
		}
	}

	// remove a queue from the Registry
	void DeleteLocalQueue(FStealHazard& Hazard, TLocalQueue* QueueToRemove, bool WorkerOwned)
	{
		NumActiveWorkers.fetch_sub(1, std::memory_order_relaxed);
		while(true)
		{
			FLocalQueueCollection* Previous = Hazard.Get();
			checkSlow(Previous->RemovedQueue == nullptr);
			FLocalQueueCollection* Copy = new FLocalQueueCollection(Previous);
			int NumRemoved = Copy->LocalQueues.Remove(QueueToRemove);
			checkSlow(NumRemoved == 1);
			if (!QueueCollection.compare_exchange_strong(Previous, Copy, std::memory_order_release, std::memory_order_relaxed))
			{
				delete Copy;
				continue;
			}
			if (!WorkerOwned)
			{
				checkSlow(Previous->RemovedQueue == nullptr);
				Previous->RemovedQueue = QueueToRemove;
			}		
			HazardsCollection.Delete(Previous);
			Hazard.Retire();
			return;
		}
	}

	// StealItem tries to steal an Item from a Registered LocalQueue
	ItemType* StealItem(FStealHazard& Hazard, uint32& CachedRandomIndex, uint32& CachedPriorityIndex)
	{
		FLocalQueueCollection* Queues = Hazard.Get();
		uint32 NumQueues = Queues->LocalQueues.Num();
		CachedRandomIndex = CachedRandomIndex % NumQueues;

		for(uint32 i = 0; i < NumQueues; i++)
		{
			TLocalQueue* LocalQueue = Queues->LocalQueues[CachedRandomIndex];
			for(uint32 j = 0; j < NumPriorities; j++)
			{	
				ItemType* Item;
				if (LocalQueue->LocalQueues[CachedPriorityIndex].Steal(Item))
				{
					Hazard.Retire();
					return Item;
				}
				CachedPriorityIndex = ++CachedPriorityIndex < NumPriorities ? CachedPriorityIndex : 0;
			}
			CachedRandomIndex = ++CachedRandomIndex < NumQueues ? CachedRandomIndex : 0;
		}
		CachedPriorityIndex = 0;
		CachedRandomIndex = TLocalQueue::InvalidIndex;
		Hazard.Retire();
		return nullptr;
	}

public:
	// enqueue an Item directy into the Global OverflowQueue
	// returns true if we should wake a worker for stealing
	bool Enqueue(ItemType* Item, uint32 PriorityIndex)
	{
		check(PriorityIndex < NumPriorities);
		check(Item != nullptr);

		OverflowQueues[PriorityIndex].enqueue(Item);

		return AnyWorkerLookingForWork();
	}

	// grab an Item directy from the Global OverflowQueue
	ItemType* Dequeue()
	{
		for (int32 PriorityIndex = 0; PriorityIndex < NumPriorities; PriorityIndex++)
		{
			ItemType* Result = OverflowQueues[PriorityIndex].dequeue();
			if (Result)
			{
				return Result;
			}
		}
		return nullptr;
	}

	inline FOutOfWork GetOutOfWorkScope()
	{
		return FOutOfWork(NumWorkersLookingForWork);
	}

private:
	inline bool AnyWorkerLookingForWork() const
	{
		return NumWorkersLookingForWork.load(std::memory_order_acquire) == 0;
	}

	FOverflowQueueType	  OverflowQueues[NumPriorities];
	FHazardPointerCollection		  HazardsCollection;
	std::atomic<FLocalQueueCollection*>	QueueCollection;
	std::atomic_int NumWorkersLookingForWork { 0 };
	std::atomic_int NumActiveWorkers { 0 };
};

template<typename ItemType, uint32 NumPriorities, uint32 NumLocalItems>
uint32 TLocalQueueRegistry<ItemType, NumPriorities, NumLocalItems>::FOutOfWork::WorkerLookingForWorkTraceId = 0;