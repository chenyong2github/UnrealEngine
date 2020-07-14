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

namespace Chaos
{

	class FPhysicsSolverBase;

	/**
	 * Task responsible for processing the command buffer of a single solver and advancing it by
	 * a specified delta before completing.
	 */
	class CHAOS_API FPhysicsSolverAdvanceTask
	{
	public:

		FPhysicsSolverAdvanceTask(FPhysicsSolverBase& InSolver, TArray<TFunction<void()>>&& InQueue, FReal InDt);

		TStatId GetStatId() const;
		static ENamedThreads::Type GetDesiredThread();
		static ESubsequentsMode::Type GetSubsequentsMode();
		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);
		static void AdvanceSolver(FPhysicsSolverBase& Solver,TArray<TFunction<void()>>&& Queue,const FReal Dt);

	private:

		FPhysicsSolverBase& Solver;
		TArray<TFunction<void()>> Queue;
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
		void EnqueueCommandImmediate(const Lambda& Func)
		{
			//TODO: remove this check. Need to rename with _External
			//The important part is that we don't enqueue from sim code
			check(IsInGameThread());
			if(ThreadingMode == EThreadingModeTemp::SingleThread)
			{
				Func();
			}
			else
			{
				CommandQueue.Add(Func);
			}
		}

		//Ensures that any running tasks finish.
		void WaitOnPendingTasks_External()
		{
			if(PendingTasks && !PendingTasks->IsComplete())
			{
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(PendingTasks);
			}
		}

		//Need this until we have a better way to deal with pending commands that affect scene query structure
		void FlushCommands_External()
		{
			WaitOnPendingTasks_External();
			for(const auto& Command : CommandQueue)
			{
				Command();
			}
			CommandQueue.Empty();
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

		EThreadingModeTemp GetThreadingMode() const
		{
			return ThreadingMode;
		}

		FGraphEventRef AdvanceAndDispatch_External(FReal InDt)
		{
			//make sure any GT state is pushed into necessary buffer
			PushPhysicsState();

			//todo: handle dt etc..
			if(ThreadingMode == EThreadingModeTemp::SingleThread)
			{
				ensure(!PendingTasks || PendingTasks->IsComplete());	//if mode changed we should have already blocked
				ensure(CommandQueue.Num() == 0);	//commands execute right away. Once we add fixed dt this will change
				FPhysicsSolverAdvanceTask::AdvanceSolver(*this, MoveTemp(CommandQueue),InDt);
			}
			else
			{
				FGraphEventArray Prereqs;
				if(PendingTasks && !PendingTasks->IsComplete())
				{
					Prereqs.Add(PendingTasks);
				}

				PendingTasks = TGraphTask<FPhysicsSolverAdvanceTask>::CreateTask(&Prereqs).ConstructAndDispatchWhenReady(*this,MoveTemp(CommandQueue),InDt);
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

	protected:
		/** Mode that the results buffers should be set to (single, double, triple) */
		EMultiBufferMode BufferMode;
		
		EThreadingModeTemp ThreadingMode;

		/** Protected construction so callers still have to go through the module to create new instances */
		FPhysicsSolverBase(const EMultiBufferMode BufferingModeIn, const EThreadingModeTemp InThreadingMode, UObject* InOwner, ETraits InTraitIdx)
			: BufferMode(BufferingModeIn)
			, ThreadingMode(InThreadingMode)
			, Owner(InOwner)
			, TraitIdx(InTraitIdx)
		{}

		/** Only allow construction with valid parameters as well as restricting to module construction */
		virtual ~FPhysicsSolverBase() = default;

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
		virtual void PushPhysicsState() = 0;

#if CHAOS_CHECKED
		FName DebugName;
#endif

	FChaosMarshallingManager MarshallingManager;

	//
	// Commands
	//
	TArray<TFunction<void()>> CommandQueue;

	FGraphEventRef PendingTasks;

	private:

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
	};
}
