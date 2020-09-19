// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Queue.h"
#include "Chaos/Defines.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/ParallelFor.h"

namespace Chaos
{
class FDirtyPropertiesManager;

struct FDirtyProxy
{
	IPhysicsProxyBase* Proxy;
	FParticleDirtyData ParticleData;
	TArray<int32> ShapeDataIndices;

	FDirtyProxy(IPhysicsProxyBase* InProxy)
		: Proxy(InProxy)
	{
	}

	void SetDirtyIdx(int32 Idx)
	{
		Proxy->SetDirtyIdx(Idx);
	}

	void AddShape(int32 ShapeDataIdx)
	{
		ShapeDataIndices.Add(ShapeDataIdx);
	}

	void Clear(FDirtyPropertiesManager& Manager,int32 DataIdx,FShapeDirtyData* ShapesData)
	{
		ParticleData.Clear(Manager,DataIdx);
		for(int32 ShapeDataIdx : ShapeDataIndices)
		{
			ShapesData[ShapeDataIdx].Clear(Manager,ShapeDataIdx);
		}
	}
};

class FDirtySet
{
public:
	void Add(IPhysicsProxyBase* Base)
	{
		if(Base->GetDirtyIdx() == INDEX_NONE)
		{
			const int32 Idx = ProxiesData.Num();
			Base->SetDirtyIdx(Idx);
			ProxiesData.Add(Base);
		}
	}

	// Batch proxy insertion, does not check DirtyIdx.
	template< typename TProxiesArray>
	void AddMultipleUnsafe(TProxiesArray& ProxiesArray)
	{
		int32 Idx = ProxiesData.Num();
		ProxiesData.Append(ProxiesArray);

		for(IPhysicsProxyBase* Proxy : ProxiesArray)
		{
			Proxy->SetDirtyIdx(Idx++);
		}
	}


	void Remove(IPhysicsProxyBase* Base)
	{
		const int32 Idx = Base->GetDirtyIdx();
		if(Idx != INDEX_NONE)
		{
			if(Idx == ProxiesData.Num() - 1)
			{
				//last element so just pop
				ProxiesData.Pop(/*bAllowShrinking=*/false);
			} else
			{
				//update other proxy's idx
				ProxiesData.RemoveAtSwap(Idx);
				ProxiesData[Idx].SetDirtyIdx(Idx);
			}

			Base->ResetDirtyIdx();
		}
	}

	void Reset()
	{
		ProxiesData.Reset();
		ShapesData.Reset();
	}

	int32 NumDirtyProxies() const { return ProxiesData.Num(); }
	int32 NumDirtyShapes() const { return ShapesData.Num(); }

	FShapeDirtyData* GetShapesDirtyData(){ return ShapesData.GetData(); }

	template <typename Lambda>
	void ParallelForEachProxy(const Lambda& Func)
	{
		::ParallelFor(ProxiesData.Num(),[this,&Func](int32 Idx)
		{
			Func(Idx,ProxiesData[Idx]);
		});
	}

	template <typename Lambda>
	void ParallelForEachProxy(const Lambda& Func) const
	{
		::ParallelFor(ProxiesData.Num(),[this,&Func](int32 Idx)
		{
			Func(Idx,ProxiesData[Idx]);
		});
	}

	template <typename Lambda>
	void ForEachProxy(const Lambda& Func)
	{
		int32 Idx = 0;
		for(FDirtyProxy& Dirty : ProxiesData)
		{
			Func(Idx++,Dirty);
		}
	}

	template <typename Lambda>
	void ForEachProxy(const Lambda& Func) const
	{
		int32 Idx = 0;
		for(const FDirtyProxy& Dirty : ProxiesData)
		{
			Func(Idx++,Dirty);
		}
	}

	void AddShape(IPhysicsProxyBase* Proxy,int32 ShapeIdx)
	{
		Add(Proxy);
		FDirtyProxy& Dirty = ProxiesData[Proxy->GetDirtyIdx()];
		for(int32 NewShapeIdx = Dirty.ShapeDataIndices.Num(); NewShapeIdx <= ShapeIdx; ++NewShapeIdx)
		{
			const int32 ShapeDataIdx = ShapesData.Add(FShapeDirtyData(NewShapeIdx));
			Dirty.AddShape(ShapeDataIdx);
		}
	}

	void SetNumDirtyShapes(IPhysicsProxyBase* Proxy,int32 NumShapes)
	{
		Add(Proxy);
		FDirtyProxy& Dirty = ProxiesData[Proxy->GetDirtyIdx()];

		if(NumShapes < Dirty.ShapeDataIndices.Num())
		{
			Dirty.ShapeDataIndices.SetNum(NumShapes);
		} else
		{
			for(int32 NewShapeIdx = Dirty.ShapeDataIndices.Num(); NewShapeIdx < NumShapes; ++NewShapeIdx)
			{
				const int32 ShapeDataIdx = ShapesData.Add(FShapeDirtyData(NewShapeIdx));
				Dirty.AddShape(ShapeDataIdx);
			}
		}
	}

private:
	TArray<FDirtyProxy> ProxiesData;
	TArray<FShapeDirtyData> ShapesData;
};

class FChaosMarshallingManager;

struct FSimCallbackData
{
	void Init(const FReal InTime)
	{
		StartTime = InTime;
	}

	union FData
	{
		void* VoidPtr;
		FReal Real;
		int32 Int;
	};
	FData Data;	//Set by user to point at external data. Data must remain valid until Deletion callback

	FReal GetStartTime() const { return StartTime; }
private:
	FReal StartTime;
};

struct FSimCallbackHandle;

struct FSimCallbackHandlePT
{
	FSimCallbackHandlePT(FSimCallbackHandle* InHandle)
	: Handle(InHandle)
	, bPendingDelete(false)
	{
	}

	FSimCallbackHandle* Handle;
	TArray<FSimCallbackData*> IntervalData;
	bool bPendingDelete;
};

struct FSimCallbackHandle
{
	template <typename Lambda>
	FSimCallbackHandle(const Lambda& InFunc)
	: Func(InFunc)
	, PTHandle(nullptr)
	, LatestCallbackData(nullptr)
	, bRunOnceMore(false)
	{
	}

	TFunction<void(const TArray<FSimCallbackData*>& IntervalData)> Func;
	TFunction<void(const TArray<FSimCallbackData*>& IntervalData)> FreeExternal;
	FSimCallbackHandlePT* PTHandle;	//Should only be used by solver

private:
	friend FChaosMarshallingManager;
	FSimCallbackData* LatestCallbackData;

	//some functions return by reference, make sure user doesn't accidentally make a copy
	FSimCallbackHandle(const FSimCallbackHandle& Other) = delete;

public:
	bool bRunOnceMore;	//Should only be used by solver
};

struct FSimCallbackDataPair
{
	FSimCallbackHandle* Callback;
	FSimCallbackData* Data;
};

struct FPushPhysicsData
{
	FDirtyPropertiesManager DirtyPropertiesManager;
	FDirtySet DirtyProxiesDataBuffer;
	FReal StartTime;
	TArray<FSimCallbackHandle*> SimCallbacksToAdd;	//callback registered at this specific time
	TArray<FSimCallbackHandle*> SimCallbacksToRemove;	//callback removed at this specific time
	TArray<FSimCallbackDataPair> SimCallbackDataPairs;	//the set of callback data pairs pushed at this specific time

	void Reset();
};

/** Manages data that gets marshaled from GT to PT using a timestamp
*/
class CHAOS_API FChaosMarshallingManager
{
public:
	FChaosMarshallingManager();

	/** Grabs the producer data to write into. Should only be called by external thread */
	FPushPhysicsData* GetProducerData_External()
	{
		return ProducerData;
	}

	FSimCallbackData& GetProducerCallbackData_External(FSimCallbackHandle& Handle)
	{
		if(Handle.LatestCallbackData == nullptr)
		{
			Handle.LatestCallbackData = CreateCallbackData_External();
			GetProducerData_External()->SimCallbackDataPairs.Add( FSimCallbackDataPair{&Handle,Handle.LatestCallbackData});
		}
		
		return *Handle.LatestCallbackData;
	}

	template <typename Lambda>
	FSimCallbackHandle& RegisterSimCallback(const Lambda& Func)
	{
		FSimCallbackHandle* Handle = new FSimCallbackHandle(Func);
		GetProducerData_External()->SimCallbacksToAdd.Add(Handle);
		return *Handle;
	}

	void UnregisterSimCallback(FSimCallbackHandle& Handle, bool bEndOfInterval = false)
	{
		Handle.bRunOnceMore = bEndOfInterval;
		GetProducerData_External()->SimCallbacksToRemove.Add(&Handle);
	}

	/** Step forward using the external delta time. Should only be called by external thread */
	void Step_External(FReal ExternalDT);

	/** Step the internal time forward and get any push data associated with the time. Should only be called by external thread */
	TArray<FPushPhysicsData*> StepInternalTime_External(FReal InternalDt);

	/** Frees the push data back into the pool. Internal thread should call this when finished processing data*/
	void FreeData_Internal(FPushPhysicsData* PushData);

	/** Frees the callback push data back into the pool. Internal thread should call this when callback will no longer be used with this specific data*/
	void FreeCallbackData_Internal(FSimCallbackHandlePT* Callback);

	/** Returns the timestamp associated with inputs consumed. Note the simulation may be pending, but any data associated with timestamp <= returned value has been passed */
	int32 GetExternalTimestampConsumed_External() const { return InternalTimestamp; }

	/** Returns the timestamp associated with inputs enqueued. */
	int32 GetExternalTimestamp_External() const { return ExternalTimestamp; }

	/** Returns the amount of external time pushed so far. Any external commands or events should be associated with this time */
	FReal GetExternalTime_External() const { return ExternalTime; }

	/** Used to delay marshalled data. This is mainly used for testing at the moment */
	void SetTickDelay_External(int32 InDelay) { Delay = InDelay; }
	
private:
	FReal ExternalTime;	//the global time external thread is currently at
	int32 ExternalTimestamp; //the global timestamp external thread is currently at (1 per frame)
	FReal SimTime;	//the global time the sim is at (once Step_External is called this time advances, even though the actual sim work has yet to be done)
	int32 InternalTimestamp;	//the global timestamp the sim is at (consumes 1 or more frames per internal tick)
	FPushPhysicsData* ProducerData;
	TArray<FPushPhysicsData*> ExternalQueue;	//the data pushed from external thread with a time stamp
	TQueue<FPushPhysicsData*,EQueueMode::Spsc> PushDataPool;	//pool to grab more push data from to avoid expensive reallocs
	TArray<TUniquePtr<FPushPhysicsData>> BackingBuffer;	//all push data is cleaned up by this
	TQueue<FSimCallbackData*,EQueueMode::Spsc> CallbackDataPool;	//pool to grab more callback data to avoid expensive reallocs
	TArray<TUniquePtr<FSimCallbackData>> CallbackDataBacking;	//callback data actually backed by this

	int32 Delay;

	void PrepareExternalQueue();

	FSimCallbackData* CreateCallbackData_External()
	{
		FSimCallbackData* NewData;
		if(!CallbackDataPool.Dequeue(NewData))
		{
			CallbackDataBacking.Add(MakeUnique<FSimCallbackData>());
			NewData = CallbackDataBacking.Last().Get();
		}
		
		NewData->Init(ExternalTime);

		return NewData;
	}
};
}; // namespace Chaos
