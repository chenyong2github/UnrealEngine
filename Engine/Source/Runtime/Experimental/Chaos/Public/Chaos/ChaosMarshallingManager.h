// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Defines.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Chaos/Framework/PhysicsProxyBase.h"

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

struct FPushPhysicsData
{
	FDirtyPropertiesManager DirtyPropertiesManager;
	FDirtySet DirtyProxiesDataBuffer;
	FReal StartTime;
	FReal ExternalDt;
};

/** Manages data that gets marshaled from GT to PT.
	If fixed dt is used this will automatically break up changes into the appropriate bucket
*/
class CHAOS_API FChaosMarshallingManager
{
public:
	FChaosMarshallingManager();

	/** Grabs the producer data to write into. Should only be called by external thread */
	FPushPhysicsData* GetProducerData_External()
	{
		return ExternalQueue[0];
	}

	/** Step forward using the external delta time. This will bucket existing data as needed */
	void Step_External(FReal ExternalDT);

	/** Consumes data available based on start time and dt*/
	FPushPhysicsData* ConsumeData_Internal(FReal StartTime, FReal InternalDt);

	/** Frees the push data back into the pool. Internal thread should call this when finished processing data*/
	void FreeData_Internal(FPushPhysicsData* PushData);
	
private:
	bool bFixedDt;
	int32 SimStep;	//the step the sim is currently at
	FReal ExternalTime;	//the global time external thread is currently at
	FReal SimTime;	//the global time the sim is currently at
	TArray<FPushPhysicsData*> ExternalQueue;	//the data pushed from external thread with a time stamp
	TQueue<FPushPhysicsData*, EQueueMode::Spsc> InternalQueue;	//the data to process on sim thread with a time stamp
	TQueue<FPushPhysicsData*,EQueueMode::Spsc> PushDataPool;	//pool to grab more push data from to avoid expensive reallocs
	TArray<TUniquePtr<FPushPhysicsData>> BackingBuffer;	//all push data is cleaned up by this
	void PrepareExternalQueue();
};
}; // namespace Chaos
