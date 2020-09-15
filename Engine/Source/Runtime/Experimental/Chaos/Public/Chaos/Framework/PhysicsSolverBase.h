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

	/**
	 * Task responsible for processing the command buffer of a single solver and advancing it by
	 * a specified delta before completing.
	 */
	class CHAOS_API FPhysicsSolverAdvanceTask
	{
	public:

		FPhysicsSolverAdvanceTask(FPhysicsSolverBase& InSolver, TArray<TFunction<void()>>&& InQueue, TArray<FPushPhysicsData*>&& PushData, FReal InDt);

		TStatId GetStatId() const;
		static ENamedThreads::Type GetDesiredThread();
		static ESubsequentsMode::Type GetSubsequentsMode();
		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);
		void AdvanceSolver();

	private:

		FPhysicsSolverBase& Solver;
		TArray<TFunction<void()>> Queue;
		TArray<FPushPhysicsData*> PushData;
		FReal Dt;
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

		bool HasPendingCommands() const { return CommandQueue.Num() > 0; }
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

		template <typename Lambda>
		FSimCallbackHandle& RegisterSimCallbackNoData(const Lambda& Func)
		{
			return MarshallingManager.RegisterSimCallback([&Func](const TArray<FSimCallbackData*>&){ Func();});
		}

		template <typename Lambda>
		void RegisterSimOneShotCallback(const Lambda& Func)
		{
			FSimCallbackHandle& Callback = MarshallingManager.RegisterSimCallback([Func](const TArray<FSimCallbackData*>&){ Func();});
			MarshallingManager.UnregisterSimCallback(Callback,true);
		}

		template <typename Lambda>
		FSimCallbackHandle& RegisterSimCallback(const Lambda& Func)
		{
			return MarshallingManager.RegisterSimCallback(Func);
		}

		void UnregisterSimCallback(FSimCallbackHandle& Handle)
		{
			MarshallingManager.UnregisterSimCallback(Handle);
		}

		// Used to marshal data for a callback associated with a specific external time
		FSimCallbackData& FindOrCreateCallbackProducerData(FSimCallbackHandle& Callback)
		{
			return MarshallingManager.GetProducerCallbackData_External(Callback);
		}

		template <typename Lambda>
		void EnqueueCommandImmediate(const Lambda& Func)
		{
			//TODO: remove this check. Need to rename with _External
			check(IsInGameThread());
			RegisterSimOneShotCallback(Func);
		}

		//Ensures that any running tasks finish.
		void WaitOnPendingTasks_External()
		{
			if(PendingTasks && !PendingTasks->IsComplete())
			{
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(PendingTasks);
			}
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

		FChaosMarshallingManager& GetMarshallingManager() { return MarshallingManager; }

		EThreadingModeTemp GetThreadingMode() const
		{
			return ThreadingMode;
		}

		FGraphEventRef AdvanceAndDispatch_External(FReal InDt)
		{
			const FReal DtWithPause = bPaused_External ? 0.0f : InDt;

			//make sure any GT state is pushed into necessary buffer
			PushPhysicsState(DtWithPause);

			TArray<FPushPhysicsData*> PushData = MarshallingManager.StepInternalTime_External(DtWithPause);
			SetExternalTimestampConsumed_External(MarshallingManager.GetExternalTimestampConsumed_External());

			if(PushData.Num())	//only kick off sim if enough dt passed
			{
				//todo: handle dt etc..
				if(ThreadingMode == EThreadingModeTemp::SingleThread)
				{
					ensure(!PendingTasks || PendingTasks->IsComplete());	//if mode changed we should have already blocked
					FPhysicsSolverAdvanceTask ImmediateTask(*this,MoveTemp(CommandQueue),MoveTemp(PushData), DtWithPause);
					ImmediateTask.AdvanceSolver();
				}
				else
				{
					FGraphEventArray Prereqs;
					if(PendingTasks && !PendingTasks->IsComplete())
					{
						Prereqs.Add(PendingTasks);
					}

					PendingTasks = TGraphTask<FPhysicsSolverAdvanceTask>::CreateTask(&Prereqs).ConstructAndDispatchWhenReady(*this,MoveTemp(CommandQueue), MoveTemp(PushData), DtWithPause);
				}
			}

			return PendingTasks;
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

		void ApplyCallbacks_Internal()
		{
			for(FSimCallbackHandlePT* Callback : SimCallbacks)
			{
				if(!Callback->bPendingDelete)
				{
					Callback->Handle->Func(Callback->IntervalData);
					MarshallingManager.FreeCallbackData_Internal(Callback);	//todo: split out for different sim phases, also wait for resim

					if(Callback->Handle->bRunOnceMore)
					{
						Callback->bPendingDelete = true;
					}
				}
			}

			//todo: need to split up for different phases of sim (always free when entire sim phase is finished)
			//typically one shot callbacks are added to end of array, so removing in reverse order should be O(1)
			//every so often a persistent callback is unregistered, so need to consider all callbacks
			//might be possible to improve this, but number of callbacks is expected to be small
			//one shot callbacks expect a FIFO so can't use RemoveAtSwap
			//might be worth splitting into two different buffers if this is too slow
			for(int32 Idx = SimCallbacks.Num()-1; Idx >= 0; --Idx)
			{
				FSimCallbackHandlePT* Callback = SimCallbacks[Idx];
				if(Callback->bPendingDelete)
				{
					MarshallingManager.FreeCallbackData_Internal(Callback);
					delete Callback->Handle;
					delete Callback;
					SimCallbacks.RemoveAt(Idx);
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

	protected:
		/** Mode that the results buffers should be set to (single, double, triple) */
		EMultiBufferMode BufferMode;
		
		EThreadingModeTemp ThreadingMode;

		/** Protected construction so callers still have to go through the module to create new instances */
		FPhysicsSolverBase(const EMultiBufferMode BufferingModeIn,const EThreadingModeTemp InThreadingMode,UObject* InOwner,ETraits InTraitIdx);

		/** Only allow construction with valid parameters as well as restricting to module construction */
		virtual ~FPhysicsSolverBase();

		static void DestroySolver(FPhysicsSolverBase& InSolver)
		{
			//block on any pending tasks
			InSolver.WaitOnPendingTasks_External();

			//make sure any pending commands are executed
			//we don't have a flush function because of dt concerns (don't want people flushing because commands end up in wrong dt)
			//but in this case we just need to ensure all resources are freed
			for(const auto& Command : InSolver.CommandQueue)
			{
				Command();
			}

			delete &InSolver;
		}

		FPhysicsSolverBase() = delete;
		FPhysicsSolverBase(const FPhysicsSolverBase& InCopy) = delete;
		FPhysicsSolverBase(FPhysicsSolverBase&& InSteal) = delete;
		FPhysicsSolverBase& operator =(const FPhysicsSolverBase& InCopy) = delete;
		FPhysicsSolverBase& operator =(FPhysicsSolverBase&& InSteal) = delete;

		virtual void AdvanceSolverBy(const FReal Dt) = 0;
		virtual void PushPhysicsState(const FReal Dt) = 0;
		virtual void ProcessPushedData_Internal(const TArray<FPushPhysicsData*>& PushDataArray) = 0;
		virtual void SetExternalTimestampConsumed_External(const int32 Timestamp) = 0;

#if CHAOS_CHECKED
		FName DebugName;
#endif

	FChaosMarshallingManager MarshallingManager;

	// The spatial operations not yet consumed by the internal sim. Use this to ensure any GT operations are seen immediately
	TUniquePtr<FPendingSpatialDataQueue> PendingSpatialOperations_External;

	//
	// Commands
	//
	TArray<TFunction<void()>> CommandQueue;

	TArray<FSimCallbackHandlePT*> SimCallbacks;

	FGraphEventRef PendingTasks;

	private:

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
		FRWLock QueryMaterialLock;

		friend FChaosSolversModule;
		friend FPhysicsSolverAdvanceTask;

		template<ELockType>
		friend struct TSolverQueryMaterialScope;

		ETraits TraitIdx;

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

	protected:
		/** Storage for events, see the Add/Remove pairs above for event timings */
		FSolverPreAdvance EventPreSolve;
		FSolverPreBuffer EventPreBuffer;
		FSolverPostAdvance EventPostSolve;
	};
}
