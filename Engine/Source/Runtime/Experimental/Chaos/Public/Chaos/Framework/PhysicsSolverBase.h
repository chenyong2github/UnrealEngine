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
			DirtyProxiesDataBuffer.AccessProducerBuffer()->Add(ProxyBaseIn);
		}
		void RemoveDirtyProxy(IPhysicsProxyBase * ProxyBaseIn)
		{
			DirtyProxiesDataBuffer.AccessProducerBuffer()->Remove(ProxyBaseIn);
		}

		// Batch dirty proxies without checking DirtyIdx.
		template <typename TProxiesArray>
		void AddDirtyProxiesUnsafe(TProxiesArray& ProxiesArray)
		{
			DirtyProxiesDataBuffer.AccessProducerBuffer()->AddMultipleUnsafe(ProxiesArray);
		}

		void AddDirtyProxyShape(IPhysicsProxyBase* ProxyBaseIn, int32 ShapeIdx)
		{
			DirtyProxiesDataBuffer.AccessProducerBuffer()->AddShape(ProxyBaseIn,ShapeIdx);
		}

		void SetNumDirtyShapes(IPhysicsProxyBase* Proxy, int32 NumShapes)
		{
			DirtyProxiesDataBuffer.AccessProducerBuffer()->SetNumDirtyShapes(Proxy,NumShapes);
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


		//NOTE: if you want to make this virtual, you need to make sure AddProxy is still inlinable since it gets called every time we do a write
		//The easiest way is probably to have an FDirtySet* that we always write to, and then swap it into this generic buffer that is virtual
		FDoubleBuffer<FDirtySet> DirtyProxiesDataBuffer;

#if CHAOS_CHECKED
		FName DebugName;
#endif

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
