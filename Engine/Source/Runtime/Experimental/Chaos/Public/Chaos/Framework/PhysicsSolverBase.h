// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Matrix.h"
#include "Misc/ScopeLock.h"
#include "ChaosLog.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Async/ParallelFor.h"
#include "Containers/Queue.h"
#include "Chaos/EvolutionTraits.h"
#include "Chaos/ChaosMarshallingManager.h"

class FChaosSolversModule;

DECLARE_MULTICAST_DELEGATE_OneParam(FSolverPreAdvance, Chaos::FReal);
DECLARE_MULTICAST_DELEGATE_OneParam(FSolverPreBuffer, Chaos::FReal);
DECLARE_MULTICAST_DELEGATE_OneParam(FSolverPostAdvance, Chaos::FReal);

namespace Chaos
{
	class FPhysicsSolverBase;
	struct FPendingSpatialDataQueue;
	class FPhysicsSceneGuard;
	class FChaosResultsManager;

	extern CHAOS_API int32 UseAsyncInterpolation;
	extern CHAOS_API int32 ForceDisableAsyncPhysics;

	struct CHAOS_API FSubStepInfo
	{
		FSubStepInfo()
			: PseudoFraction(1.0)
			, Step(1)
			, NumSteps(1)
		{
		}

		FSubStepInfo(const FReal InPseudoFraction, const int32 InStep, const int32 InNumSteps)
			: PseudoFraction(InPseudoFraction)
			, Step(InStep)
			, NumSteps(InNumSteps)
		{

		}

		//This is NOT Step / NumSteps, this is to allow for kinematic target interpolation which uses its own logic
		FReal PseudoFraction;
		int32 Step;
		int32 NumSteps;
	};

	/**
	 * Task responsible for processing the command buffer of a single solver and advancing it by
	 * a specified delta before completing.
	 */
	class CHAOS_API FPhysicsSolverAdvanceTask
	{
	public:

		FPhysicsSolverAdvanceTask(FPhysicsSolverBase& InSolver, FPushPhysicsData& PushData);

		TStatId GetStatId() const;
		static ENamedThreads::Type GetDesiredThread();
		static ESubsequentsMode::Type GetSubsequentsMode();
		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);
		void AdvanceSolver();

	private:

		FPhysicsSolverBase& Solver;
		FPushPhysicsData* PushData;
	};


	class FPersistentPhysicsTask;

	enum class ELockType: uint8;

	//todo: once refactor is done use just one enum
	enum class EThreadingModeTemp: uint8
	{
		DedicatedThread,
		TaskGraph,
		SingleThread
	};

	class CHAOS_API FPhysicsSolverBase
	{
	public:

#define EVOLUTION_TRAIT(Trait) case ETraits::Trait: Func((TPBDRigidsSolver<Trait>&)(*this)); return;
		template <typename Lambda>
		void CastHelper(const Lambda& Func)
		{
			switch(TraitIdx)
			{
#include "Chaos/EvolutionTraits.inl"
			}
		}
#undef EVOLUTION_TRAIT

		template <typename Traits>
		TPBDRigidsSolver<Traits>& CastChecked()
		{
			check(TraitIdx == TraitToIdx<Traits>());
			return (TPBDRigidsSolver<Traits>&)(*this);
		}

		void ChangeBufferMode(EMultiBufferMode InBufferMode);

		void AddDirtyProxy(IPhysicsProxyBase * ProxyBaseIn)
		{
			MarshallingManager.GetProducerData_External()->DirtyProxiesDataBuffer.Add(ProxyBaseIn);
		}
		void RemoveDirtyProxy(IPhysicsProxyBase * ProxyBaseIn)
		{
			MarshallingManager.GetProducerData_External()->DirtyProxiesDataBuffer.Remove(ProxyBaseIn);
		}

		// Batch dirty proxies without checking DirtyIdx.
		template <typename TProxiesArray>
		void AddDirtyProxiesUnsafe(TProxiesArray& ProxiesArray)
		{
			MarshallingManager.GetProducerData_External()->DirtyProxiesDataBuffer.AddMultipleUnsafe(ProxiesArray);
		}

		void AddDirtyProxyShape(IPhysicsProxyBase* ProxyBaseIn, int32 ShapeIdx)
		{
			MarshallingManager.GetProducerData_External()->DirtyProxiesDataBuffer.AddShape(ProxyBaseIn,ShapeIdx);
		}

		void SetNumDirtyShapes(IPhysicsProxyBase* Proxy, int32 NumShapes)
		{
			MarshallingManager.GetProducerData_External()->DirtyProxiesDataBuffer.SetNumDirtyShapes(Proxy,NumShapes);
		}

		/** Creates a new sim callback object of the type given. Caller expected to free using FreeSimCallbackObject_External*/
		template <typename TSimCallbackObjectType>
		TSimCallbackObjectType* CreateAndRegisterSimCallbackObject_External(bool bContactModification = false)
		{
			auto NewCallbackObject = new TSimCallbackObjectType();
			RegisterSimCallbackObject_External(NewCallbackObject, bContactModification);
			return NewCallbackObject;
		}

		void UnregisterAndFreeSimCallbackObject_External(ISimCallbackObject* SimCallbackObject)
		{
			MarshallingManager.UnregisterSimCallbackObject_External(SimCallbackObject);
		}

		template <typename Lambda>
		void RegisterSimOneShotCallback(const Lambda& Func)
		{
			//do we need a pool to avoid allocations?
			auto CommandObject = new FSimCallbackCommandObject(Func);
			RegisterSimCallbackObject_External(CommandObject);
			MarshallingManager.UnregisterSimCallbackObject_External(CommandObject, true);
		}

		template <typename Lambda>
		void EnqueueCommandImmediate(const Lambda& Func)
		{
			//TODO: remove this check. Need to rename with _External
			check(IsInGameThread());
			RegisterSimOneShotCallback(Func);
		}

		//Used as helper for GT to go from unique idx back to gt particle
		//If GT deletes a particle, this function will return null (that's a good thing when consuming async outputs as GT may have already deleted the particle we care about)
		//Note: if the physics solver has been advanced after the particle was freed on GT, the index may have been freed and reused.
		//In this case instead of getting a nullptr you will get an unrelated (wrong) GT particle
		//Because of this we keep the index alive for as long as the async callback can lag behind. This way as long as you immediately consume the output, you will always be sure the unique index was not released.
		//Practically the flow should always be like this:
		//advance the solver and trigger callbacks. Callbacks write to outputs. Consume the outputs on GT and use this function _before_ advancing solver again
		TGeometryParticle<FReal, 3>* UniqueIdxToGTParticle_External(const FUniqueIdx& UniqueIdx) const
		{
			TGeometryParticle<FReal, 3>* Result = nullptr;
			if (ensure(UniqueIdx.Idx < UniqueIdxToGTParticles.Num()))	//asking for particle on index that has never been allocated
			{
				Result = UniqueIdxToGTParticles[UniqueIdx.Idx];
			}

			return Result;
		}

		//Ensures that any running tasks finish.
		void WaitOnPendingTasks_External()
		{
			if(PendingTasks && !PendingTasks->IsComplete())
			{
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(PendingTasks);
			}
		}

		virtual bool AreAnyTasksPending() const
		{
			return false;
		}

		bool IsPendingTasksComplete() const
		{
			if (PendingTasks && !PendingTasks->IsComplete())
			{
				return false;
			}

			return true;
		}

		const UObject* GetOwner() const
		{ 
			return Owner; 
		}

		void SetOwner(const UObject* InOwner)
		{
			Owner = InOwner;
		}

		void SetThreadingMode_External(EThreadingModeTemp InThreadingMode)
		{
			if(InThreadingMode != ThreadingMode)
			{
				if(InThreadingMode == EThreadingModeTemp::SingleThread)
				{
					WaitOnPendingTasks_External();
				}
				ThreadingMode = InThreadingMode;
			}
		}
		
		void MarkShuttingDown()
		{
			bIsShuttingDown = true;
		}

		void EnableAsyncMode(FReal FixedDt)
		{
			if (AsyncDt != FixedDt)
			{
				AccumulatedTime = 0;
			}

			AsyncDt = FixedDt;
		}

		void DisableAsyncMode()
		{
			AsyncDt = -1;
		}

		FChaosMarshallingManager& GetMarshallingManager() { return MarshallingManager; }

		EThreadingModeTemp GetThreadingMode() const
		{
			return ThreadingMode;
		}

		FGraphEventRef AdvanceAndDispatch_External(FReal InDt)
		{
			const FReal DtWithPause = bPaused_External ? 0.0f : InDt;
			FReal InternalDt = DtWithPause;
			int32 NumSteps = 1;

			if(IsUsingFixedDt())
			{
				AccumulatedTime += DtWithPause;
				InternalDt = AsyncDt;
				NumSteps = FMath::FloorToInt(AccumulatedTime / InternalDt);
				AccumulatedTime -= InternalDt * NumSteps;
			}

			FGraphEventRef BlockingTasks = PendingTasks;

			if(InDt > 0)
			{
				ExternalSteps++;	//we use this to average forces. It assumes external dt is about the same. 0 dt should be ignored as it typically has nothing to do with force
			}

			if(NumSteps > 0)
			{
				//make sure any GT state is pushed into necessary buffer
				PushPhysicsState(InternalDt, NumSteps, FMath::Max(ExternalSteps,1));
				ExternalSteps = 0;
			}

			while(FPushPhysicsData* PushData = MarshallingManager.StepInternalTime_External())
			{
				if (ThreadingMode == EThreadingModeTemp::SingleThread)
				{
					ensure(!PendingTasks || PendingTasks->IsComplete());	//if mode changed we should have already blocked
					FPhysicsSolverAdvanceTask ImmediateTask(*this, *PushData);
#if !UE_BUILD_SHIPPING
					if (bStealAdvanceTasksForTesting)
					{
						StolenSolverAdvanceTasks.Emplace(MoveTemp(ImmediateTask));
					}
					else
					{
						ImmediateTask.AdvanceSolver();
					}
#else
					ImmediateTask.AdvanceSolver();
#endif
				}
				else
				{
					FGraphEventArray Prereqs;
					if(PendingTasks && !PendingTasks->IsComplete())
					{
						Prereqs.Add(PendingTasks);
					}

					PendingTasks = TGraphTask<FPhysicsSolverAdvanceTask>::CreateTask(&Prereqs).ConstructAndDispatchWhenReady(*this, *PushData);
					if (IsUsingAsyncResults() == false)
					{
						BlockingTasks = PendingTasks;	//block right away
					}
				}

				if(IsUsingAsyncResults() == false)
				{
					break;	//non async can only process one step at a time
				}
			}

			return BlockingTasks;
		}

#if CHAOS_CHECKED
		void SetDebugName(const FName& Name)
		{
			DebugName = Name;
		}

		const FName& GetDebugName() const
		{
			return DebugName;
		}
#endif

		void ApplyCallbacks_Internal(const FReal SimTime, const FReal Dt)
		{
			for (ISimCallbackObject* Callback : SimCallbackObjects)
			{
				if (!Callback->bPendingDelete)
				{
					//if we're shutting down, we only want to run callbacks that are "run once more". This generally means it's a one shot command that may free resources
					if(!bIsShuttingDown || Callback->bRunOnceMore)
					{
						Callback->PreSimulate_Internal(SimTime, Dt);
					}
				}
			}
		}

		void FreeCallbacksData_Internal(float SimTime, float DeltaTime)
		{
			//final post solve call. TODO: move this out of here, just putting it here for now because we're forced to call callbacks manually during destroy
			for(ISimCallbackObject* Callback : ContactModifiers)
			{
				if (!Callback->bPendingDelete)
				{
					//if we're shutting down, we only want to run callbacks that are "run once more". This generally means it's a one shot command that may free resources
					if (!bIsShuttingDown || Callback->bRunOnceMore)
					{
						Callback->PostSimulate_Internal(SimTime, DeltaTime);
					}
				}
			}

			for (ISimCallbackObject* Callback : SimCallbackObjects)
			{
				if (Callback->bRunOnceMore)
				{
					Callback->bPendingDelete = true;
				}

				Callback->SetCurrentInput_Internal(nullptr);
			}

			//typically one shot callbacks are added to end of array, so removing in reverse order should be O(1)
			//every so often a persistent callback is unregistered, so need to consider all callbacks
			//might be possible to improve this, but number of callbacks is expected to be small
			//one shot callbacks expect a FIFO so can't use RemoveAtSwap
			//might be worth splitting into two different buffers if this is too slow

			for (int32 Idx = ContactModifiers.Num() - 1; Idx >= 0; --Idx)
			{
				ISimCallbackObject* Callback = ContactModifiers[Idx];
				if (Callback->bPendingDelete)
				{
					//will also be in SimCallbackObjects so we'll delete it in that loop
					ContactModifiers.RemoveAt(Idx);
				}
			}

			for (int32 Idx = SimCallbackObjects.Num() - 1; Idx >= 0; --Idx)
			{
				ISimCallbackObject* Callback = SimCallbackObjects[Idx];
				if (Callback->bPendingDelete)
				{
					delete Callback;
					SimCallbackObjects.RemoveAt(Idx);
				}
			}
		}

		void UpdateParticleInAccelerationStructure_External(TGeometryParticle<FReal,3>* Particle,bool bDelete);

		bool IsPaused_External() const
		{
			return bPaused_External;
		}

		void SetIsPaused_External(bool bShouldPause)
		{
			bPaused_External = bShouldPause;
		}

		/** Used to update external thread data structures. RigidFunc allows per dirty rigid code to execute. Include PhysicsSolverBaseImpl.h to call this function*/
		template <typename RigidLambda>
		void PullPhysicsStateForEachDirtyProxy_External(const RigidLambda& RigidFunc);

		bool IsUsingAsyncResults() const
		{
			return !ForceDisableAsyncPhysics && AsyncDt >= 0;
		}

		bool IsUsingFixedDt() const
		{
			return IsUsingAsyncResults() && UseAsyncInterpolation;
		}

	protected:
		/** Mode that the results buffers should be set to (single, double, triple) */
		EMultiBufferMode BufferMode;
		
		EThreadingModeTemp ThreadingMode;

		/** Protected construction so callers still have to go through the module to create new instances */
		FPhysicsSolverBase(const EMultiBufferMode BufferingModeIn,const EThreadingModeTemp InThreadingMode,UObject* InOwner,ETraits InTraitIdx);

		/** Only allow construction with valid parameters as well as restricting to module construction */
		virtual ~FPhysicsSolverBase();

		static void DestroySolver(FPhysicsSolverBase& InSolver);

		FPhysicsSolverBase() = delete;
		FPhysicsSolverBase(const FPhysicsSolverBase& InCopy) = delete;
		FPhysicsSolverBase(FPhysicsSolverBase&& InSteal) = delete;
		FPhysicsSolverBase& operator =(const FPhysicsSolverBase& InCopy) = delete;
		FPhysicsSolverBase& operator =(FPhysicsSolverBase&& InSteal) = delete;

		virtual void AdvanceSolverBy(const FReal Dt, const FSubStepInfo& SubStepInfo = FSubStepInfo()) = 0;
		virtual void PushPhysicsState(const FReal Dt, const int32 NumSteps, const int32 NumExternalSteps) = 0;
		virtual void ProcessPushedData_Internal(FPushPhysicsData& PushDataArray) = 0;
		virtual void SetExternalTimestampConsumed_Internal(const int32 Timestamp) = 0;

#if CHAOS_CHECKED
		FName DebugName;
#endif

	FChaosMarshallingManager MarshallingManager;
	TUniquePtr<FChaosResultsManager> PullResultsManager;

	// The spatial operations not yet consumed by the internal sim. Use this to ensure any GT operations are seen immediately
	TUniquePtr<FPendingSpatialDataQueue> PendingSpatialOperations_External;

	TArray<ISimCallbackObject*> SimCallbackObjects;
	TArray<ISimCallbackObject*> ContactModifiers;

	FGraphEventRef PendingTasks;

	private:

		//This is private because the user should never create their own callback object
		//The lifetime management should always be done by solver to ensure callbacks are accessing valid memory on async tasks
		void RegisterSimCallbackObject_External(ISimCallbackObject* SimCallbackObject, bool bContactModification = false)
		{
			ensure(SimCallbackObject->Solver == nullptr);	//double register?
			SimCallbackObject->SetSolver_External(this);
			SimCallbackObject->SetContactModification(bContactModification);
			MarshallingManager.RegisterSimCallbackObject_External(SimCallbackObject);
		}

		/** 
		 * Whether this solver is paused. Paused solvers will still 'tick' however they will receive a Dt of zero so they can still
		 * build acceleration structures or accept inputs from external threads 
		 */
		bool bPaused_External;

		/** 
		 * Ptr to the engine object that is counted as the owner of this solver.
		 * Never used internally beyond how the solver is stored and accessed through the solver module.
		 * Nullptr owner means the solver is global or standalone.
		 * @see FChaosSolversModule::CreateSolver
		 */
		const UObject* Owner = nullptr;

		//TODO: why is this needed? seems bad to read from solver directly, should be buffered
		FRWLock SimMaterialLock;
		
		/** Scene lock object for external threads (non-physics) */
		TUniquePtr<FPhysicsSceneGuard> ExternalDataLock_External;

		friend FChaosSolversModule;
		friend FPhysicsSolverAdvanceTask;

		template<ELockType>
		friend struct TSolverSimMaterialScope;

		ETraits TraitIdx;

		bool bIsShuttingDown;
		FReal AsyncDt;
		FReal AccumulatedTime;
		int32 ExternalSteps;
		TArray<TGeometryParticle<FReal, 3>*> UniqueIdxToGTParticles;

	public:
		/** Events */
		/** Pre advance is called before any physics processing or simulation happens in a given physics update */
		FDelegateHandle AddPreAdvanceCallback(FSolverPreAdvance::FDelegate InDelegate);
		bool            RemovePreAdvanceCallback(FDelegateHandle InHandle);

		/** Pre buffer happens after the simulation has been advanced (particle positions etc. will have been updated) but GT results haven't been prepared yet */
		FDelegateHandle AddPreBufferCallback(FSolverPreAdvance::FDelegate InDelegate);
		bool            RemovePreBufferCallback(FDelegateHandle InHandle);

		/** Post advance happens after all processing and results generation has been completed */
		FDelegateHandle AddPostAdvanceCallback(FSolverPostAdvance::FDelegate InDelegate);
		bool            RemovePostAdvanceCallback(FDelegateHandle InHandle);

		/** Get the lock used for external data manipulation. A better API would be to use scoped locks so that getting a write lock is non-const */
		//NOTE: this is a const operation so that you can acquire a read lock on a const solver. The assumption is that non-const write operations are already marked non-const
		FPhysicsSceneGuard& GetExternalDataLock_External() const { return *ExternalDataLock_External; }

	protected:
		/** Storage for events, see the Add/Remove pairs above for event timings */
		FSolverPreAdvance EventPreSolve;
		FSolverPreBuffer EventPreBuffer;
		FSolverPostAdvance EventPostSolve;

		void TrackGTParticle_External(TGeometryParticle<FReal, 3>& Particle);
		void ClearGTParticle_External(TGeometryParticle<FReal, 3>& Particle);
		

#if !UE_BUILD_SHIPPING
	// Solver testing utility
	private:
		// instead of running advance task in single threaded, put in array for manual execution control for unit tests.
		bool bStealAdvanceTasksForTesting;
		TArray<FPhysicsSolverAdvanceTask> StolenSolverAdvanceTasks;
	public:
		void SetStealAdvanceTasks_ForTesting(bool bInStealAdvanceTasksForTesting);
		void PopAndExecuteStolenAdvanceTask_ForTesting();
#endif
	};
}
