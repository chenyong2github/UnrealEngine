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
FName FBreakingEvent::EventName("GC_Breaking");
FName FCollisionEvent::EventName("GC_Collision");
FName FTrailingEvent::EventName("GC_Trailing");

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
		const FTransform WorldToComponent = Comp->GetComponentTransform().Inverse();
		
		if(!Proxy)
		{
			return;
		}

		if (!CachedData.Contains(Proxy))
		{
			return;
		}

		const FCachedEventData& ProxyCachedEventData = CachedData[Proxy];

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

		if (BreakingDataArray && ProxyCachedEventData.ProxyBreakingDataIndices)
		{
			for (int32 Index : *ProxyCachedEventData.ProxyBreakingDataIndices)
			{
				if (BreakingDataArray->IsValidIndex(Index))
				{
					const FBreakingData& BreakingData = (*BreakingDataArray)[Index];
					if (const TPBDRigidParticleHandle<FReal, 3>* Rigid = BreakingData.Particle->CastToRigidParticle())
					{
						int32 TransformIndex = Proxy->GetTransformGroupIndexFromHandle(Rigid);
						if (TransformIndex > INDEX_NONE)
						{
							OutFrame.PushEvent(FBreakingEvent::EventName, InTime, FBreakingEvent(TransformIndex, BreakingData, WorldToComponent));
						}
					}
				}
				
			}
		}
		
		if (CollisionDataArray && ProxyCachedEventData.ProxyCollisionDataIndices)
		{
			for (int32 Index : *ProxyCachedEventData.ProxyCollisionDataIndices)
			{
				if (CollisionDataArray->IsValidIndex(Index))
				{
					const FCollidingData& CollisionData = (*CollisionDataArray)[Index];
					if (const TPBDRigidParticleHandle<FReal, 3>* Rigid = CollisionData.Levelset->CastToRigidParticle())
					{
						int32 TransformIndex = Proxy->GetTransformGroupIndexFromHandle(Rigid);
						if (TransformIndex > INDEX_NONE)
						{
							OutFrame.PushEvent(FCollisionEvent::EventName, InTime, FCollisionEvent(TransformIndex, CollisionData, WorldToComponent));
						}
					}
				}

			}
		}
		
		if (TrailingDataArray && ProxyCachedEventData.ProxyTrailingDataIndices)
		{
			for (int32 Index : *ProxyCachedEventData.ProxyTrailingDataIndices)
			{
				if (TrailingDataArray->IsValidIndex(Index))
				{
					const FTrailingData& TrailingData = (*TrailingDataArray)[Index];
					if (const TPBDRigidParticleHandle<FReal, 3>*Rigid = TrailingData.Particle->CastToRigidParticle())
					{
						int32 TransformIndex = Proxy->GetTransformGroupIndexFromHandle(Rigid);
						if (TransformIndex > INDEX_NONE)
						{
							OutFrame.PushEvent(FTrailingEvent::EventName, InTime, FTrailingEvent(TransformIndex, TrailingData, WorldToComponent));
						}
					}
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
		const FTransform ComponentToWorld = Comp->GetComponentTransform();

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
		const TArray<FCacheEventHandle>* BreakingEvents = EvaluatedResult.Events.Find(FBreakingEvent::EventName);
		const TArray<FCacheEventHandle>* CollisionEvents = EvaluatedResult.Events.Find(FCollisionEvent::EventName);
		const TArray<FCacheEventHandle>* TrailingEvents = EvaluatedResult.Events.Find(FTrailingEvent::EventName);

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
						
						if (ChildParticle)
						{
							if (ChildParticle->ObjectState() != EObjectStateType::Kinematic)
							{
								// If a field or other external actor set the particle to static or dynamic we no longer apply the cache
								continue;
							}

							if (FRigidParticle* ClusterParent = ChildParticle->ClusterIds().Id)
							{
								if (FClusterParticle* Parent = ClusterParent->CastToClustered())
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

		if (BreakingEvents)
		{
			const FSolverBreakingEventFilter* SolverBreakingEventFilter = Solver->GetEventFilters()->GetBreakingFilter();

			for (const FCacheEventHandle& Handle : *BreakingEvents)
			{
				if (FBreakingEvent* Event = Handle.Get<FBreakingEvent>())
				{
					if (Particles.IsValidIndex(Event->Index))
					{
						Chaos::FPBDRigidClusteredParticleHandle* Particle = Particles[Event->Index];

						if (Particle)
						{
							if (Particle->ObjectState() != EObjectStateType::Kinematic)
							{
								// If a field or other external actor set the particle to static or dynamic we no longer apply the cache
								continue;
							}

							FBreakingData CachedBreak;
							CachedBreak.Particle = Particle;
							CachedBreak.Location = ComponentToWorld.TransformPosition(Event->Location);
							CachedBreak.Velocity = ComponentToWorld.TransformVector(Event->Velocity);
							CachedBreak.AngularVelocity = Event->AngularVelocity;
							CachedBreak.Mass = Event->Mass;
							CachedBreak.BoundingBox = TAABB<FReal, 3>(Event->BoundingBoxMin, Event->BoundingBoxMax);
							CachedBreak.BoundingBox = CachedBreak.BoundingBox.TransformedAABB(ComponentToWorld);

							if (!SolverBreakingEventFilter->Enabled() || SolverBreakingEventFilter->Pass(CachedBreak))
							{
								float TimeStamp = Solver->GetSolverTime();
								Solver->GetEventManager()->AddEvent<FBreakingEventData>(EEventType::Breaking, [&CachedBreak, TimeStamp](FBreakingEventData& BreakingEventData)
									{
										if (BreakingEventData.BreakingData.TimeCreated != TimeStamp)
										{
											BreakingEventData.BreakingData.AllBreakingsArray.Reset();
											BreakingEventData.BreakingData.TimeCreated = TimeStamp;
										}
										BreakingEventData.BreakingData.AllBreakingsArray.Add(CachedBreak);
									});
							}
						}
					}
				}
			}
		}

		if (TrailingEvents)
		{
			const FSolverTrailingEventFilter* SolverTrailingEventFilter = Solver->GetEventFilters()->GetTrailingFilter();

			for (const FCacheEventHandle& Handle : *TrailingEvents)
			{
				if (FTrailingEvent* Event = Handle.Get<FTrailingEvent>())
				{
					if (Particles.IsValidIndex(Event->Index))
					{
						Chaos::FPBDRigidClusteredParticleHandle* Particle = Particles[Event->Index];

						if (Particle)
						{
							if (Particle->ObjectState() != EObjectStateType::Kinematic)
							{
								// If a field or other external actor set the particle to static or dynamic we no longer apply the cache
								continue;
							}

							FTrailingData CachedTrail;
							CachedTrail.Particle = Particle;
							CachedTrail.Location = ComponentToWorld.TransformPosition(Event->Location);
							CachedTrail.Velocity = ComponentToWorld.TransformVector(Event->Velocity);
							CachedTrail.AngularVelocity = Event->AngularVelocity;
							CachedTrail.BoundingBox = TAABB<FReal, 3>(Event->BoundingBoxMin, Event->BoundingBoxMax);
							CachedTrail.BoundingBox = CachedTrail.BoundingBox.TransformedAABB(ComponentToWorld);

							if (!SolverTrailingEventFilter->Enabled() || SolverTrailingEventFilter->Pass(CachedTrail))
							{
								float TimeStamp = Solver->GetSolverTime();
								Solver->GetEventManager()->AddEvent<FTrailingEventData>(EEventType::Trailing, [&CachedTrail , TimeStamp](FTrailingEventData& TrailingEventData)
									{
										if (TrailingEventData.TrailingData.TimeCreated != TimeStamp)
										{
											TrailingEventData.TrailingData.AllTrailingsArray.Reset();
											TrailingEventData.TrailingData.TimeCreated = TimeStamp;
										}
										TrailingEventData.TrailingData.AllTrailingsArray.Add(CachedTrail);
									});
							}
						}
					}
				}
			}
		}

		if (CollisionEvents)
		{

			const FSolverCollisionEventFilter* SolverCollisionEventFilter = Solver->GetEventFilters()->GetCollisionFilter();
			for (const FCacheEventHandle& Handle : *CollisionEvents)
			{
				if (FCollisionEvent* Event = Handle.Get<FCollisionEvent>())
				{
					if (Particles.IsValidIndex(Event->Index))
					{
						Chaos::FPBDRigidClusteredParticleHandle* Particle = Particles[Event->Index];

						if (Particle)
						{
							if (Particle->ObjectState() != EObjectStateType::Kinematic)
							{
								// If a field or other external actor set the particle to static or dynamic we no longer apply the cache
								continue;
							}

							FCollidingData CachedCollision;
							CachedCollision.Location = ComponentToWorld.TransformPosition(Event->Location);
							CachedCollision.AccumulatedImpulse = ComponentToWorld.TransformVector(Event->AccumulatedImpulse);
							CachedCollision.Normal = ComponentToWorld.TransformVector(Event->Normal);
							CachedCollision.Velocity1 = ComponentToWorld.TransformVector(Event->Velocity1);
							CachedCollision.Velocity2 = ComponentToWorld.TransformVector(Event->Velocity2);
							CachedCollision.DeltaVelocity1 = ComponentToWorld.TransformVector(Event->DeltaVelocity1);
							CachedCollision.DeltaVelocity2 = ComponentToWorld.TransformVector(Event->DeltaVelocity2);
							CachedCollision.AngularVelocity1 = Event->AngularVelocity1;
							CachedCollision.AngularVelocity2 = Event->AngularVelocity2;
							CachedCollision.Mass1 = Event->Mass1;
							CachedCollision.Mass2 = Event->Mass2;
							CachedCollision.PenetrationDepth = Event->PenetrationDepth;
							CachedCollision.Particle = Particle;

							// #todo: Are these even available from a cache?
							CachedCollision.Levelset = nullptr;

							if (!SolverCollisionEventFilter->Enabled() || SolverCollisionEventFilter->Pass(CachedCollision))
							{
								float TimeStamp = Solver->GetSolverTime();
								Solver->GetEventManager()->AddEvent<FCollisionEventData>(EEventType::Collision, [&CachedCollision, TimeStamp, Particle](FCollisionEventData& CollisionEventData)
									{
										if (CollisionEventData.CollisionData.TimeCreated != TimeStamp)
										{
											CollisionEventData.CollisionData.AllCollisionsArray.Reset();
											CollisionEventData.PhysicsProxyToCollisionIndices.Reset();
											CollisionEventData.CollisionData.TimeCreated = TimeStamp;
										}
										int32 NewIdx = CollisionEventData.CollisionData.AllCollisionsArray.Add(CachedCollision);
										CollisionEventData.PhysicsProxyToCollisionIndices.PhysicsProxyToIndicesMap.FindOrAdd(Particle->PhysicsProxy()).Add(NewIdx);
									});
							}
						}
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

	void FGeometryCollectionCacheAdapter::Initialize()
	{
		CachedData.Empty();
	}
	
	bool FGeometryCollectionCacheAdapter::InitializeForRecord(UPrimitiveComponent* InComponent, UChaosCache* InCache)
	{
		UGeometryCollectionComponent*    Comp     = CastChecked<UGeometryCollectionComponent>(InComponent);
		FGeometryCollectionPhysicsProxy* Proxy    = Comp->GetPhysicsProxy();

		if (!Proxy)
		{
			return false;
		}

		Chaos::FPhysicsSolver* Solver = Proxy->GetSolver<Chaos::FPhysicsSolver>();

		if (!Solver)
		{
			return false;
		}

		// We need secondary event data to record event information into the cache
		Solver->SetGenerateBreakingData(true);
		Solver->SetGenerateCollisionData(true);
		Solver->SetGenerateTrailingData(true);
		
		// We only need to register event handlers once, the first time we initialize.
		if (CachedData.Num() == 0)
		{
			Chaos::FEventManager* EventManager = Solver->GetEventManager();
			if (EventManager)
			{
				EventManager->RegisterHandler<Chaos::FBreakingEventData>(Chaos::EEventType::Breaking, const_cast<FGeometryCollectionCacheAdapter*>(this), &FGeometryCollectionCacheAdapter::HandleBreakingEvents);
				EventManager->RegisterHandler<Chaos::FCollisionEventData>(Chaos::EEventType::Collision, const_cast<FGeometryCollectionCacheAdapter*>(this), &FGeometryCollectionCacheAdapter::HandleCollisionEvents);
				EventManager->RegisterHandler<Chaos::FTrailingEventData>(Chaos::EEventType::Trailing, const_cast<FGeometryCollectionCacheAdapter*>(this), &FGeometryCollectionCacheAdapter::HandleTrailingEvents);
			}

			BreakingDataArray = nullptr;
			CollisionDataArray = nullptr;
			TrailingDataArray = nullptr;
		}

		CachedData.Add(Proxy, FCachedEventData());

		return true;
	}

	bool FGeometryCollectionCacheAdapter::InitializeForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const
	{
		UGeometryCollectionComponent*    Comp  = CastChecked<UGeometryCollectionComponent>(InComponent);
		FGeometryCollectionPhysicsProxy* Proxy = Comp->GetPhysicsProxy();

		FGeometryDynamicCollection& Collection = Proxy->GetPhysicsCollection();

		for (int32& State : Collection.DynamicState)
		{
			State = (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		}

		return true;
	}

	void FGeometryCollectionCacheAdapter::HandleBreakingEvents(const Chaos::FBreakingEventData& Event)
	{
		BreakingDataArray = &Event.BreakingData.AllBreakingsArray;

		for (TPair<IPhysicsProxyBase*, FCachedEventData>& Data : CachedData)
		{
			if (Event.PhysicsProxyToBreakingIndices.PhysicsProxyToIndicesMap.Contains(Data.Key))
			{
				Data.Value.ProxyBreakingDataIndices = &Event.PhysicsProxyToBreakingIndices.PhysicsProxyToIndicesMap[Data.Key];
			}
			else
			{
				Data.Value.ProxyBreakingDataIndices = nullptr;
			}
		}
	}

	void FGeometryCollectionCacheAdapter::HandleCollisionEvents(const Chaos::FCollisionEventData& Event)
	{
		CollisionDataArray = &Event.CollisionData.AllCollisionsArray;

		for (TPair<IPhysicsProxyBase*, FCachedEventData>& Data : CachedData)
		{
			if (Event.PhysicsProxyToCollisionIndices.PhysicsProxyToIndicesMap.Contains(Data.Key))
			{
				Data.Value.ProxyCollisionDataIndices = &Event.PhysicsProxyToCollisionIndices.PhysicsProxyToIndicesMap[Data.Key];
			}
			else
			{
				Data.Value.ProxyCollisionDataIndices = nullptr;
			}
		}
	}

	void FGeometryCollectionCacheAdapter::HandleTrailingEvents(const Chaos::FTrailingEventData& Event)
	{
		TrailingDataArray = &Event.TrailingData.AllTrailingsArray;

		for (TPair<IPhysicsProxyBase*, FCachedEventData>& Data : CachedData)
		{
			if (Event.PhysicsProxyToTrailingIndices.PhysicsProxyToIndicesMap.Contains(Data.Key))
			{
				Data.Value.ProxyTrailingDataIndices = &Event.PhysicsProxyToTrailingIndices.PhysicsProxyToIndicesMap[Data.Key];
			}
			else
			{
				Data.Value.ProxyTrailingDataIndices = nullptr;
			}
		}
	}

}    // namespace Chaos
