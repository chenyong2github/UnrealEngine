// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Matrix.h"
#include "Misc/ScopeLock.h"
#include "ChaosLog.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Async/ParallelFor.h"

namespace Chaos
{
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

		void ChangeBufferMode(EMultiBufferMode InBufferMode);

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

		const UObject* GetOwner() const
		{ 
			return Owner; 
		}

		void SetOwner(const UObject* InOwner)
		{
			Owner = InOwner;
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

		/** Protected construction so callers still have to go through the module to create new instances */
		FPhysicsSolverBase(const EMultiBufferMode BufferingModeIn, UObject* InOwner)
			: BufferMode(BufferingModeIn)
			, Owner(InOwner)
		{}

		/** Only allow construction with valid parameters as well as restricting to module construction */
		FPhysicsSolverBase() = delete;
		FPhysicsSolverBase(const FPhysicsSolverBase& InCopy) = delete;
		FPhysicsSolverBase(FPhysicsSolverBase&& InSteal) = delete;
		FPhysicsSolverBase& operator =(const FPhysicsSolverBase& InCopy) = delete;
		FPhysicsSolverBase& operator =(FPhysicsSolverBase&& InSteal) = delete;


		//NOTE: if you want to make this virtual, you need to make sure AddProxy is still inlinable since it gets called every time we do a write
		//The easiest way is probably to have an FDirtySet* that we always write to, and then swap it into this generic buffer that is virtual
		FDoubleBuffer<FDirtySet> DirtyProxiesDataBuffer;

#if CHAOS_CHECKED
		FName DebugName;
#endif

	private:

		/** 
		 * Ptr to the engine object that is counted as the owner of this solver.
		 * Never used internally beyond how the solver is stored and accessed through the solver module.
		 * Nullptr owner means the solver is global or standalone.
		 * @see FChaosSolversModule::CreateSolver
		 */
		const UObject* Owner = nullptr;
	};
}
