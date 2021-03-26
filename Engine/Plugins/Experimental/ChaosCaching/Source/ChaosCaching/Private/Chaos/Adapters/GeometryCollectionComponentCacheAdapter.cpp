// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Adapters/GeometryCollectionComponentCacheAdapter.h"
#include "Chaos/ChaosCache.h"
#include "Chaos/ParticleHandle.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "PBDRigidsSolver.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/ChaosSolverActor.h"

FName FEnableStateEvent::EventName("GC_Enable");

namespace Chaos
{

	FComponentCacheAdapter::SupportType FGeometryCollectionCacheAdapter::SupportsComponentClass(UClass* InComponentClass) const
	{
		UClass* Desired = GetDesiredClass();
		if(InComponentClass == Desired)
		{
			return FComponentCacheAdapter::SupportType::Direct;
		}
		else if(InComponentClass->IsChildOf(Desired))
		{
			return FComponentCacheAdapter::SupportType::Derived;
		}

		return FComponentCacheAdapter::SupportType::None;
	}

	UClass* FGeometryCollectionCacheAdapter::GetDesiredClass() const
	{
		return UGeometryCollectionComponent::StaticClass();
	}

	uint8 FGeometryCollectionCacheAdapter::GetPriority() const
	{
		return EngineAdapterPriotityBegin;
	}

	void FGeometryCollectionCacheAdapter::Record_PostSolve(UPrimitiveComponent* InComp, const FTransform& InRootTransform, FPendingFrameWrite& OutFrame, Chaos::FReal InTime) const
	{
		using FClusterParticle = Chaos::FPBDRigidClusteredParticleHandle;
		using FRigidParticle = Chaos::FPBDRigidParticleHandle;

		UGeometryCollectionComponent*    Comp  = CastChecked<UGeometryCollectionComponent>(InComp);
		FGeometryCollectionPhysicsProxy* Proxy = Comp->GetPhysicsProxy();

		if(!Proxy)
		{
			return;
		}

		const Chaos::FPhysicsSolver* Solver         = Proxy->GetSolver<Chaos::FPhysicsSolver>();
		const FGeometryCollection*   RestCollection = Proxy->GetSimParameters().RestCollection;

		if(!RestCollection || !Solver)
		{
			return;
		}

		FGeometryDynamicCollection&            Collection       = Proxy->GetPhysicsCollection();
		const TManagedArray<int32>&            TransformIndices = RestCollection->TransformIndex;
		const TManagedArray<FTransform>&       Transforms       = Collection.Transform;
		const TArray<FBreakingData>& Breaks                     = Solver->GetEvolution()->GetRigidClustering().GetAllClusterBreakings();

		// A transform index exists for each 'real' (i.e. leaf node in the rest collection)
		const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);

		// Pre-alloc once for worst case.
		OutFrame.PendingParticleData.Reserve(NumTransforms);

		TArray<TGeometryParticleHandle<Chaos::FReal, 3>*> RelatedBreaks;
		RelatedBreaks.Reserve(Breaks.Num());
		for(const FBreakingData& Break : Breaks)
		{
			// Accessing the GT particle here to pull the proxy - while unsafe we're recording a proxy currently so it should remain valid.
			// No GT data is being read from the particle
			Chaos::FGeometryParticle* GTParticleUnsafe = Break.Particle->GTGeometryParticle();
			IPhysicsProxyBase*        BaseProxy        = GTParticleUnsafe->GetProxy();
			if(BaseProxy->GetType() == EPhysicsProxyType::GeometryCollectionType)
			{
				FGeometryCollectionPhysicsProxy* ConcreteProxy = static_cast<FGeometryCollectionPhysicsProxy*>(BaseProxy);

				if(ConcreteProxy == Proxy)
				{
					// The break particle belongs to our proxy
					RelatedBreaks.Add(Break.Particle);
				}
			}
		}

		for(int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
		{
			FClusterParticle* Handle = Proxy->GetParticles()[TransformIndex];
			
			if(Handle)
			{
				const FRigidParticle* Parent = Handle ? Handle->ClusterIds().Id : nullptr;
				const FClusterParticle* ParentAsCluster = Parent ? Parent->CastToClustered() : nullptr;
				const bool bParentIsInternalCluster = ParentAsCluster ? ParentAsCluster->InternalCluster() : false;
				const bool bParentIsActiveInternalCluster = bParentIsInternalCluster && !Parent->Disabled();

				const bool bParticleDisabled = Handle->Disabled();
				if(!bParticleDisabled || bParentIsActiveInternalCluster)
				{
					OutFrame.PendingParticleData.AddDefaulted();
					FPendingParticleWrite& Pending = OutFrame.PendingParticleData.Last();

					Pending.ParticleIndex = TransformIndex;
					Pending.PendingTransform = FTransform(Handle->R(), Handle->X()).GetRelativeTransform(InRootTransform);
				}

				int32 BreakIndex = RelatedBreaks.Find(Handle);
				if(BreakIndex != INDEX_NONE)
				{
					OutFrame.PushEvent(FEnableStateEvent::EventName, InTime, FEnableStateEvent(TransformIndex, true));
				}
			}
		}

		// Never going to change again till freed after writing to the cache so free up the extra space we reserved
		OutFrame.PendingParticleData.Shrink();
	}

	void FGeometryCollectionCacheAdapter::Playback_PreSolve(UPrimitiveComponent*                               InComponent,
															UChaosCache*                                       InCache,
															Chaos::FReal                                       InTime,
															FPlaybackTickRecord&                               TickRecord,
															TArray<TPBDRigidParticleHandle<Chaos::FReal, 3>*>& OutUpdatedRigids) const
	{
		using FClusterParticle = Chaos::FPBDRigidClusteredParticleHandle;
		using FRigidParticle = Chaos::FPBDRigidParticleHandle;

		UGeometryCollectionComponent*    Comp  = CastChecked<UGeometryCollectionComponent>(InComponent);
		FGeometryCollectionPhysicsProxy* Proxy = Comp->GetPhysicsProxy();

		if(!Proxy)
		{
			return;
		}

		const FGeometryCollection* RestCollection = Proxy->GetSimParameters().RestCollection;
		Chaos::FPhysicsSolver*     Solver         = Proxy->GetSolver<Chaos::FPhysicsSolver>();

		if(!RestCollection || !Solver)
		{
			return;
		}

		FGeometryDynamicCollection&      Collection       = Proxy->GetPhysicsCollection();
		const TManagedArray<int32>&      TransformIndices = RestCollection->TransformIndex;
		const TManagedArray<FTransform>& Transforms       = Collection.Transform;
		TArray<FClusterParticle*>        Particles        = Proxy->GetParticles();

		FCacheEvaluationContext Context(TickRecord);
		Context.bEvaluateTransform = true;
		Context.bEvaluateCurves    = false;
		Context.bEvaluateEvents    = true;

		FCacheEvaluationResult EvaluatedResult = InCache->Evaluate(Context);

		const int32                      NumEventTracks = EvaluatedResult.Events.Num();
		const TArray<FCacheEventHandle>* EnableEvents   = EvaluatedResult.Events.Find(FEnableStateEvent::EventName);

		if(EnableEvents)
		{
			TMap<FClusterParticle*, TArray<FRigidParticle*>> NewClusters;
			for(const FCacheEventHandle& Handle : *EnableEvents)
			{
				if(FEnableStateEvent* Event = Handle.Get<FEnableStateEvent>())
				{
					if(Particles.IsValidIndex(Event->Index))
					{
						Chaos::FPBDRigidClusteredParticleHandle* ChildParticle = Particles[Event->Index];
						
						if(ChildParticle->ObjectState() != EObjectStateType::Kinematic)
						{
							// If a field or other external actor set the particle to static or dynamic we no longer apply the cache
							continue;
						}

						if(FRigidParticle* ClusterParent = ChildParticle->ClusterIds().Id)
						{
							if(FClusterParticle* Parent = ClusterParent->CastToClustered())
							{
								TArray<FRigidParticle*>& Cluster = NewClusters.FindOrAdd(Parent);
								Cluster.Add(ChildParticle);
							}
						}
						else
						{
							// This is a cluster parent
							ChildParticle->SetDisabled(!Event->bEnable);
						}
						
					}
				}
			}

			for(TPair<FClusterParticle*, TArray<FRigidParticle*>> Cluster : NewClusters)
			{
				TArray<FRigidParticle*>& ChildrenParticles = Cluster.Value;
				if (ChildrenParticles.Num())
				{
					FRigidParticle* ClusterHandle = nullptr;
					
					for (FRigidParticle* ChildHandle : ChildrenParticles)
					{
						if (FClusterParticle* ClusteredChildHandle = ChildHandle->CastToClustered())
						{
							if (ClusteredChildHandle->Disabled() && ClusteredChildHandle->ClusterIds().Id != nullptr)
							{
								if (ensure(!ClusterHandle || ClusteredChildHandle->ClusterIds().Id == ClusterHandle))
								{
									ClusterHandle = ClusteredChildHandle->ClusterIds().Id;
								}
								else
								{
									break; //shouldn't be here
								}
							}
						}
					}
					if (ClusterHandle)
					{
						Solver->GetEvolution()->GetRigidClustering().ReleaseClusterParticlesNoInternalCluster(ClusterHandle->CastToClustered(), nullptr, true);
					}
				}
			}
		}

		const int32 NumTransforms = EvaluatedResult.Transform.Num();
		for(int32 Index = 0; Index < NumTransforms; ++Index)
		{
			const int32      ParticleIndex      = EvaluatedResult.ParticleIndices[Index];
			const FTransform EvaluatedTransform = EvaluatedResult.Transform[Index];

			if(Particles.IsValidIndex(ParticleIndex))
			{
				Chaos::FPBDRigidClusteredParticleHandle* Handle = Particles[ParticleIndex];

				if(!Handle || Handle->ObjectState() != EObjectStateType::Kinematic)
				{
					// If a field or other external actor set the particle to static or dynamic we no longer apply the cache
					continue;
				}

				Handle->SetP(EvaluatedTransform.GetTranslation());
				Handle->SetQ(EvaluatedTransform.GetRotation());
				Handle->SetX(Handle->P());
				Handle->SetR(Handle->Q());
				
				if(FRigidParticle* ClusterParent = Handle->ClusterIds().Id)
				{
					if(FClusterParticle* Parent = ClusterParent->CastToClustered())
					{
						if(Parent->InternalCluster())
						{
							// This is an unmanaged particle. Because its children are kinematic it will be also.
							// however we need to update its position at least once to place it correctly.
							// The child was placed with:
							//     ChildT = ChildHandle->ChildToParent() * FTransform(ParentHandle->R(), ParentHandle->X());
							// When it was simulated, so we can work backwards to place the parent.
							// This will result in multiple transform sets happening to the parent but allows us to mostly ignore
							// that it exists, if it doesn't the child still gets set to the correct position.
							FTransform ChildTransform = Handle->ChildToParent();
							FTransform Result = ChildTransform.Inverse() * EvaluatedTransform;
							Parent->SetP(Result.GetTranslation());
							Parent->SetX(Result.GetTranslation());
							Parent->SetQ(Result.GetRotation());
							Parent->SetR(Result.GetRotation());
						}
					}
				}

				OutUpdatedRigids.Add(Handle);
			}
		}
	}

	bool FGeometryCollectionCacheAdapter::ValidForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const
	{
		UGeometryCollectionComponent*  GeomComponent = Cast<UGeometryCollectionComponent>(InComponent);

		if(!GeomComponent)
		{
			return false;
		}

		const UGeometryCollection* Collection = GeomComponent->RestCollection;

		if(!Collection || !Collection->GetGeometryCollection().IsValid())
		{
			return false;
		}

		// Really permissive check - as long as we can map all tracks to a particle in the geometry collection we'll allow this to play.
		// allows geometry changes without invalidating an entire cache on reimport or modification.
		const int32 NumTransforms = Collection->GetGeometryCollection()->Transform.Num();
		for(const int32 ParticleIndex : InCache->TrackToParticle)
		{
			if(ParticleIndex < 0 || ParticleIndex >= NumTransforms)
			{
				return false;
			}
		}

		return true;
	}

	FGuid FGeometryCollectionCacheAdapter::GetGuid() const
	{
		FGuid NewGuid;
		checkSlow(FGuid::Parse(TEXT("A3147746B50C47C883B93DBF85CBB589"), NewGuid));
		return NewGuid;
	}

	Chaos::FPhysicsSolver* FGeometryCollectionCacheAdapter::GetComponentSolver(UPrimitiveComponent* InComponent) const
	{
#if WITH_CHAOS

		// If the observed component is a Geometry Collection using a non-default Chaos solver..
		if (UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(InComponent))
		{
			if (AChaosSolverActor* SolverActor = GeometryCollectionComponent->GetPhysicsSolverActor())
			{
				return SolverActor->GetSolver();
			}			
		}

		// ..otherwise use the default solver.
		if (InComponent && InComponent->GetWorld())
		{
			UWorld* ComponentWorld = InComponent->GetWorld();

			if (FPhysScene* WorldScene = ComponentWorld->GetPhysicsScene())
			{
				return WorldScene->GetSolver();
			}
		}

#endif // WITH_CHAOS

		return nullptr;
	}

	bool FGeometryCollectionCacheAdapter::InitializeForRecord(UPrimitiveComponent* InComponent, UChaosCache* InCache) const
	{
		UGeometryCollectionComponent*    Comp     = CastChecked<UGeometryCollectionComponent>(InComponent);
		FGeometryCollectionPhysicsProxy* Proxy    = Comp->GetPhysicsProxy();

		if(!Proxy)
		{
			return false;
		}

		Chaos::FPhysicsSolver* Solver = Proxy->GetSolver<Chaos::FPhysicsSolver>();

		if(!Solver)
		{
			return false;
		}

		// We need breaking data to record cluster breaking information into the cache
		Solver->GetEvolution()->GetRigidClustering().SetGenerateClusterBreaking(true);

		return true;
	}

	bool FGeometryCollectionCacheAdapter::InitializeForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const
	{
		UGeometryCollectionComponent*    Comp  = CastChecked<UGeometryCollectionComponent>(InComponent);
		FGeometryCollectionPhysicsProxy* Proxy = Comp->GetPhysicsProxy();

		FGeometryDynamicCollection& Collection = Proxy->GetPhysicsCollection();

		for(int32& State : Collection.DynamicState)
		{
			State = (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		}

		return true;
	}

}    // namespace Chaos
