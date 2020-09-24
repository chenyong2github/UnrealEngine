// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Queue.h"
#include "Chaos/Defines.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/ParallelFor.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"

class FGeometryCollectionResults;

namespace Chaos
{
class FDirtyPropertiesManager;
class FPullPhysicsData;

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

struct FSimCallbackOutput
{
	FSimCallbackOutput(const FReal InInternalTime)
		: InternalTime(InInternalTime)
	{
	}

	/** The internal time of the sim when this output was generated */
	FReal InternalTime;

protected:

	// Do not delete directly, use FreeOutputData_External
	~FSimCallbackOutput() = default;
};

struct FSimCallbackInput
{
	FSimCallbackInput()
	: ExternalTime(-1)
	{
	}

	FReal GetExternalTime() const { return ExternalTime; }

protected:
	// Do not delete directly, use FreeInputData_Internal
	~FSimCallbackInput() = default;

private:
	/** The external time associated with this input */
	FReal ExternalTime;

	friend class ISimCallbackObject;
};

/**
 * Callback API used for executing code at different points in the simulation.
 * The external thread pushes input data at its own rate (typically once per game thread tick)
 * The internal thread consumes the relevant inputs based on timestamps.
 * For example, if a physics step is 40ms and we tick the game thread at 20ms, the callback would receive 2 inputs per callback (assuming data was pushed every game thread tick)
 * A callback can generate one output to be consumed by the external thread.
 * For example, you could apply a force to an object based on how close the object is to the ground. In this case the game thread may want to know how much force was applied.
 * 
 * This API is also used for resimulating.
 * Because of this, the input data is const and its lifetime is maintained by the internal thread.
 * It is expected that callbacks are "pure" in the sense that they rely only on the input data and affect the simulation in a repeatable and deterministic way.
 * This means that if the same inputs are passed into the callback, we expect the exact same output and that any simulation changes are the same.
 * We rely on this to cache results and skip callbacks when possible during a resim.
 * See functions for more details.
 */
class CHAOS_API ISimCallbackObject
{
public:

	//Destructor called on internal thread.
	virtual ~ISimCallbackObject() = default;
	ISimCallbackObject(const ISimCallbackObject&) = delete;	//not copyable

	/**
	* Called once per simulation interval.
	* Inputs passed in are ordered by time and are in the interval [SimStart, SimStart + DeltaSeconds]
	* Return output for external thread (optional, null means no output)
	*/
	void PreSimulate_Internal(const FReal SimStart, const FReal DeltaSeconds) const
	{
		//const_cast needed because c++ doesn't automatically promote const when double ptr
		//what we want is const T** = t where t = T**. As you can see this is becoming MORE const so it's safe
		auto ConstInputs = const_cast<const FSimCallbackInput**>(IntervalData.GetData());
		TArrayView<const FSimCallbackInput*> ConstInputsView(ConstInputs, IntervalData.Num());
		OnPreSimulate_Internal(SimStart, DeltaSeconds, ConstInputsView);
	}

	/**
	 * Free the output data. There is no API for allocating because that's done by the user directly in the callback.
	 * Note that allocation is done on the internal thread, but freeing is done on the external thread.
	 * A common pattern is to use a single producer single consumer thread safe queue to manage this.
	 *
	 * In the case of a resim, pending outputs can be thrown out if we know the callback will be re-run with old time stamps
	 */
	virtual void FreeOutputData_External(FSimCallbackOutput* Output) = 0;
	
protected:

	ISimCallbackObject()
	: bRunOnceMore(false)
	, bPendingDelete(false)
	, CurrentExternalInput_External(nullptr)
	, Solver(nullptr)
	{
	}


	/**
	 * Gets the current producer input data. This is what the external thread should be writing to
	 */
	FSimCallbackInput* GetProducerInputData_External();

private:
	
	/**
	 * Allocate/Free the input data.
	 * A common pattern is to use a single producer single consumer thread safe queue to manage this
	 * Note that allocation is done on the external thread, and freeing is done on the internal one
	 */
	virtual FSimCallbackInput* AllocateInputData_External() = 0;
	virtual void FreeInputData_Internal(FSimCallbackInput* Input) = 0;
	
	virtual FSimCallbackOutput* OnPreSimulate_Internal(const FReal SimStart, const FReal DeltaSeconds, const TArrayView<const FSimCallbackInput*>& Inputs) const = 0;

	friend class FPhysicsSolverBase;

	template <typename T>
	friend class TPBDRigidsSolver;

	friend class FChaosMarshallingManager;

	bool bRunOnceMore;
	bool bPendingDelete;

	TArray<FSimCallbackInput*> IntervalData; //storage for current interval input data.
	FSimCallbackInput* CurrentExternalInput_External;
	FPhysicsSolverBase* Solver;

	//putting this here so that user classes don't have to bother with non-default constructor
	void SetSolver_External(FPhysicsSolverBase* InSolver) { Solver = InSolver;}
};

struct FSimCallbackNoOutput : public FSimCallbackOutput
{
	void Reset() {}
};

/** Simple callback command object. Commands are typically passed in as lambdas and there's no need for data management. Should not be used directly, see FPhysicsSolverBase::EnqueueCommand */
class FSimCallbackCommandObject : public ISimCallbackObject
{
public:
	FSimCallbackCommandObject(const TFunction<void()>& InFunc)
		: Func(InFunc)
	{}

	virtual void FreeOutputData_External(FSimCallbackOutput* Output)
	{
		//data management handled by command passed in (data should be copied by value as commands run async and memory lifetime is hard to predict)
		check(false);
	}

private:

	virtual FSimCallbackInput* AllocateInputData_External()
	{
		//data management handled by command passed in (data should be copied by value as commands run async and memory lifetime is hard to predict)
		check(false);
		return nullptr;
	}

	virtual void FreeInputData_Internal(FSimCallbackInput* Input)
	{
		//data management handled by command passed in (data should be copied by value as commands run async and memory lifetime is hard to predict)
		check(false);
	}

	virtual FSimCallbackOutput* OnPreSimulate_Internal(const FReal SimStart, const FReal DeltaSeconds, const TArrayView<const FSimCallbackInput*>& Inputs) const override
	{
		Func();
		return nullptr;
	}

	TFunction<void()> Func;
};

/** Simple templated implementation that uses lock free queues to manage memory */
template <typename TInputType, typename TOutputType = FSimCallbackNoOutput>
class TSimCallbackObject : public ISimCallbackObject
{
public:

	/*TOutputType* NewOutputData_Internal(const FReal InternalTime)
	{
		return NewDataHelper(InternalTime, OutputBacking, OutputPool);
	}*/

	virtual void FreeOutputData_External(FSimCallbackOutput* Output) override
	{
		auto Concrete = static_cast<TOutputType*>(Output);
		Concrete->Reset();
		OutputPool.Enqueue(Concrete);
	}

	/**
	 * Gets the current producer input data. This is what the external thread should be writing to
	 */
	TInputType* GetProducerInputData_External()
	{
		return static_cast<TInputType*>(ISimCallbackObject::GetProducerInputData_External());
	}

private:

	template <typename T>
	T* NewDataHelper(TArray<TUniquePtr<T>>& Backing, TQueue<T*, EQueueMode::Spsc>& Queue)
	{
		T* Result;
		if (!Queue.Dequeue(Result))
		{
			Backing.Emplace(new T());
			Result = Backing.Last().Get();
		}

		return Result;
	}

	virtual void FreeInputData_Internal(FSimCallbackInput* Input) override
	{
		auto Concrete = static_cast<TInputType*>(Input);
		Concrete->Reset();
		InputPool.Enqueue(Concrete);
	}

	TInputType* NewInputData_External()
	{
		return NewDataHelper(InputBacking, InputPool);
	}

	virtual FSimCallbackInput* AllocateInputData_External() override
	{
		return NewInputData_External();
	}

	TQueue<TInputType*, EQueueMode::Spsc> InputPool;
	TArray<TUniquePtr<TInputType>> InputBacking;

	TQueue<TOutputType*, EQueueMode::Spsc> OutputPool;
	TArray<TUniquePtr<TOutputType>> OutputBacking;
};

struct FSimCallbackInputAndObject
{
	ISimCallbackObject* CallbackObject;
	FSimCallbackInput* Input;
};

struct FPushPhysicsData
{
	FDirtyPropertiesManager DirtyPropertiesManager;
	FDirtySet DirtyProxiesDataBuffer;
	FReal StartTime;

	TArray<ISimCallbackObject*> SimCallbackObjectsToAdd;	//callback object registered at this specific time
	TArray<ISimCallbackObject*> SimCallbackObjectsToRemove;	//callback object removed at this specific time
	TArray<FSimCallbackInputAndObject> SimCallbackInputs; //set of callback inputs pushed at this specific time

	void Reset();
};

/** Manages data that gets marshaled from GT to PT using a timestamp
*/
class CHAOS_API FChaosMarshallingManager
{
public:
	FChaosMarshallingManager();
	~FChaosMarshallingManager();

	/** Grabs the producer data to write into. Should only be called by external thread */
	FPushPhysicsData* GetProducerData_External()
	{
		return ProducerData;
	}

	void RegisterSimCallbackObject_External(ISimCallbackObject* SimCallbackObject)
	{
		GetProducerData_External()->SimCallbackObjectsToAdd.Add(SimCallbackObject);
	}

	void UnregisterSimCallbackObject_External(ISimCallbackObject* SimCallbackObject, bool bRunOnceMore = false)
	{
		SimCallbackObject->bRunOnceMore = bRunOnceMore;
		GetProducerData_External()->SimCallbackObjectsToRemove.Add(SimCallbackObject);
	}

	void AddSimCallbackInputData_External(ISimCallbackObject* SimCallbackObject, FSimCallbackInput* InputData)
	{
		GetProducerData_External()->SimCallbackInputs.Add(FSimCallbackInputAndObject{ SimCallbackObject, InputData });
	}
	/** Step forward using the external delta time. Should only be called by external thread */
	void Step_External(FReal ExternalDT);

	/** Step the internal time forward and get any push data associated with the time. Should only be called by external thread */
	TArray<FPushPhysicsData*> StepInternalTime_External(FReal InternalDt);

	/** Frees the push data back into the pool. Internal thread should call this when finished processing data*/
	void FreeData_Internal(FPushPhysicsData* PushData);

	/** Frees the pull data back into the pool. External thread should call this when finished processing data*/
	void FreePullData_External(FPullPhysicsData* PullData);

	/** Returns the timestamp associated with inputs consumed. Note the simulation may be pending, but any data associated with timestamp <= returned value has been passed */
	int32 GetExternalTimestampConsumed_External() const { return InternalTimestamp; }

	/** Returns the timestamp associated with inputs enqueued. */
	int32 GetExternalTimestamp_External() const { return ExternalTimestamp; }

	/** Returns the amount of external time pushed so far. Any external commands or events should be associated with this time */
	FReal GetExternalTime_External() const { return ExternalTime; }

	/** Used to delay marshalled data. This is mainly used for testing at the moment */
	void SetTickDelay_External(int32 InDelay) { Delay = InDelay; }

	/** Returns the current pull data being written to. This holds the results of dirty data to be read later by external thread*/
	FPullPhysicsData* GetCurrentPullData_Internal() { return CurPullData; }

	/** Hands pull data off to external thread */
	void FinalizePullData_Internal();

	/** Pops and returns the earliest pull data available. nullptr means results are not ready or no work is pending */
	FPullPhysicsData* PopPullData_External()
	{
		FPullPhysicsData* Result = nullptr;
		PullDataQueue.Dequeue(Result);
		return Result;
	}
		
private:
	FReal ExternalTime;	//the global time external thread is currently at
	int32 ExternalTimestamp; //the global timestamp external thread is currently at (1 per frame)
	FReal SimTime;	//the global time the sim is at (once Step_External is called this time advances, even though the actual sim work has yet to be done)
	int32 InternalTimestamp;	//the global timestamp the sim is at (consumes 1 or more frames per internal tick)
	
	//push
	FPushPhysicsData* ProducerData;
	TArray<FPushPhysicsData*> ExternalQueue;	//the data pushed from external thread with a time stamp
	TQueue<FPushPhysicsData*,EQueueMode::Spsc> PushDataPool;	//pool to grab more push data from to avoid expensive reallocs
	TArray<TUniquePtr<FPushPhysicsData>> BackingBuffer;	//all push data is cleaned up by this

	//pull
	FPullPhysicsData* CurPullData;	//the current pull data sim is writing to
	TQueue<FPullPhysicsData*,EQueueMode::Spsc> PullDataQueue;	//the results the simulation has written to. Consumed by external thread
	TQueue<FPullPhysicsData*,EQueueMode::Spsc> PullDataPool;	//the pull data pool to avoid reallocs. Pushed by external thread, popped by internal
	TArray<TUniquePtr<FPullPhysicsData>> BackingPullBuffer;		//all pull data is cleaned up by this

	int32 Delay;

	void PrepareExternalQueue();
	void PreparePullData();
};
}; // namespace Chaos
