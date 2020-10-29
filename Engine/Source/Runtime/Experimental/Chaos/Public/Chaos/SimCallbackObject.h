// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Queue.h"
#include "Chaos/Defines.h"
#include "Chaos/SimCallbackInput.h"
#include "Chaos/CollisionResolutionTypes.h"

namespace Chaos
{
class FPhysicsSolverBase;

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
	void PreSimulate_Internal(const FReal SimStart, const FReal DeltaSeconds)
	{
		//const_cast needed because c++ doesn't automatically promote const when double ptr
		//what we want is const T** = t where t = T**. As you can see this is becoming MORE const so it's safe
		auto ConstInputs = const_cast<const FSimCallbackInput**>(IntervalData.GetData());
		TArrayView<const FSimCallbackInput*> ConstInputsView(ConstInputs, IntervalData.Num());
		OnPreSimulate_Internal(SimStart, DeltaSeconds, ConstInputsView);
	}

	/**
	* Called once per simulation step.
	* Inputs passed in are for the entire interval containing the step. So you may see multiple inputs for one step, or multiple steps for one input
	* 
	* NOTE: you must explicitly request contact modification when registering the callback for this to be called
	*/
	void ContactModification_Internal(const FReal SimTime, const FReal DeltaSeconds, const TArrayView<FPBDCollisionConstraintHandleModification>& Modifications)
	{
		//const_cast needed because c++ doesn't automatically promote const when double ptr
		//what we want is const T** = t where t = T**. As you can see this is becoming MORE const so it's safe
		auto ConstInputs = const_cast<const FSimCallbackInput**>(IntervalData.GetData());
		TArrayView<const FSimCallbackInput*> ConstInputsView(ConstInputs, IntervalData.Num());
		OnContactModification_Internal(SimTime, DeltaSeconds, ConstInputsView, Modifications);
	}

	/**
	* Called after simulation interval has finished.
	* Inputs passed in are ordered by time and are in the interval [SimStart, SimStart + DeltaSeconds]
	*
	* NOTE: (this currently requires contact modification to trigger), TODO: fix this
	*/
	void PostSimulate_Internal(const FReal SimTime, const FReal DeltaSeconds)
	{
		//const_cast needed because c++ doesn't automatically promote const when double ptr
		//what we want is const T** = t where t = T**. As you can see this is becoming MORE const so it's safe
		auto ConstInputs = const_cast<const FSimCallbackInput**>(IntervalData.GetData());
		TArrayView<const FSimCallbackInput*> ConstInputsView(ConstInputs, IntervalData.Num());
		OnPostSimulate_Internal(SimTime, DeltaSeconds, ConstInputsView);
	}

	/**
	 * Free the output data. There is no API for allocating because that's done by the user directly in the callback.
	 * Note that allocation is done on the internal thread, but freeing is done on the external thread.
	 * A common pattern is to use a single producer single consumer thread safe queue to manage this.
	 *
	 * In the case of a resim, pending outputs can be thrown out if we know the callback will be re-run with old time stamps
	 */
	virtual void FreeOutputData_External(FSimCallbackOutput* Output) = 0;

	FPhysicsSolverBase* GetSolver() { return Solver; }
	
protected:

	ISimCallbackObject()
	: bRunOnceMore(false)
	, bPendingDelete(false)
	, bContactModification(false)
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
	
	virtual FSimCallbackOutput* OnPreSimulate_Internal(const FReal SimStart, const FReal DeltaSeconds, const TArrayView<const FSimCallbackInput*>& Inputs) = 0;

	virtual void OnContactModification_Internal(const FReal SimTime, const FReal DeltaSeconds, const TArrayView<const FSimCallbackInput*>& Inputs, const TArrayView<FPBDCollisionConstraintHandleModification>& Modifications)
	{
		//registered for contact modification, but implementation is missing
		check(false);
	}

	virtual void OnPostSimulate_Internal(const float SimTime, const float Dt, const TArrayView<const Chaos::FSimCallbackInput*>& IntervalInputs)
	{
	}

	friend class FPhysicsSolverBase;

	template <typename T>
	friend class TPBDRigidsSolver;

	friend class FChaosMarshallingManager;

	bool bRunOnceMore;
	bool bPendingDelete;
	bool bContactModification;

	TArray<FSimCallbackInput*> IntervalData; //storage for current interval input data.
	FSimCallbackInput* CurrentExternalInput_External;
	FPhysicsSolverBase* Solver;

	//putting this here so that user classes don't have to bother with non-default constructor
	void SetSolver_External(FPhysicsSolverBase* InSolver) { Solver = InSolver;}
	void SetContactModification(bool InContactModification) { bContactModification = InContactModification; }
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

	virtual FSimCallbackOutput* OnPreSimulate_Internal(const FReal SimStart, const FReal DeltaSeconds, const TArrayView<const FSimCallbackInput*>& Inputs) override
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

	TOutputType* NewOutputData_Internal(const FReal InternalTime)
	{
		auto NewOutput = NewDataHelper(OutputBacking, OutputPool);
		NewOutput->InternalTime = InternalTime;
		return NewOutput;
	}

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

}; // namespace Chaos
