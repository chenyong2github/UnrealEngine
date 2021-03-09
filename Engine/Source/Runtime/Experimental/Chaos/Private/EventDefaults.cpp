// Copyright Epic Games, Inc. All Rights Reserved.

#include "EventDefaults.h"
#include "EventsData.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidClustering.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "PBDRigidsSolver.h"
#include "PhysicsProxy/SkeletalMeshPhysicsProxy.h"
#include "PhysicsProxy/StaticMeshPhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/PerSolverFieldSystem.h"
#include "ChaosSolversModule.h"

namespace Chaos
{

	void FEventDefaults::RegisterSystemEvents(FEventManager& EventManager)
	{
		RegisterCollisionEvent(EventManager);
		RegisterBreakingEvent(EventManager);
		RegisterTrailingEvent(EventManager);
		RegisterSleepingEvent(EventManager);
	}

	void FEventDefaults::RegisterCollisionEvent(FEventManager& EventManager)
	{
		EventManager.template RegisterEvent<FCollisionEventData>(EEventType::Collision, []
		(const Chaos::FPBDRigidsSolver* Solver, FCollisionEventData& CollisionEventData)
		{
			check(Solver);
			SCOPE_CYCLE_COUNTER(STAT_GatherCollisionEvent);

			// #todo: This isn't working - SolverActor parameters are set on a solver but it is currently a different solver that is simulating!!
			//if (!Solver->GetEventFilters()->IsCollisionEventEnabled())
			//	return;

			FCollisionDataArray& AllCollisionsDataArray = CollisionEventData.CollisionData.AllCollisionsArray;
			TMap<IPhysicsProxyBase*, TArray<int32>>& AllCollisionsIndicesByPhysicsProxy = CollisionEventData.PhysicsProxyToCollisionIndices.PhysicsProxyToIndicesMap;

			AllCollisionsDataArray.Reset();
			AllCollisionsIndicesByPhysicsProxy.Reset();

			CollisionEventData.CollisionData.TimeCreated = Solver->MTime;
			CollisionEventData.PhysicsProxyToCollisionIndices.TimeCreated = Solver->MTime;

			const auto* Evolution = Solver->GetEvolution();

			const FPBDCollisionConstraints& CollisionRule = Evolution->GetCollisionConstraints();


			const FPBDRigidParticles& Particles = Evolution->GetParticles().GetDynamicParticles();
			const TArrayCollectionArray<ClusterId>& ClusterIdsArray = Evolution->GetRigidClustering().GetClusterIdsArray();
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap& ParentToChildrenMap = Evolution->GetRigidClustering().GetChildrenMap();
#endif
			const typename Chaos::TPBDRigidClustering<typename FPBDRigidsSolver::FPBDRigidsEvolution, FPBDCollisionConstraints>::FClusterMap& ParentToChildrenMap = Evolution->GetRigidClustering().GetChildrenMap();

			if(CollisionRule.NumConstraints() > 0)
			{
				// Get the number of valid constraints (AccumulatedImpulse != 0.f and Phi < 0.f) from AllConstraintsArray
				TArray<const Chaos::FPBDCollisionConstraintHandle*> ValidCollisionHandles;
				ValidCollisionHandles.SetNumUninitialized(CollisionRule.NumConstraints());
				int32 NumValidCollisions = 0;
				const FReal MinDeltaVelocityForHitEvents = FChaosSolversModule::GetModule()->GetSettingsProvider().GetMinDeltaVelocityForHitEvents();
				for (const Chaos::FPBDCollisionConstraintHandle * ContactHandle : CollisionRule.GetConstConstraintHandles())
				{
					if (ContactHandle->GetType() == FCollisionConstraintBase::FType::SinglePoint ||
						ContactHandle->GetType() == FCollisionConstraintBase::FType::SinglePointSwept)
					{
						const FRigidBodyPointContactConstraint& Constraint = (ContactHandle->GetType() == FCollisionConstraintBase::FType::SinglePoint) ?
							ContactHandle->GetPointContact() :
							*ContactHandle->GetSweptPointContact().As<FRigidBodyPointContactConstraint>();

						// Since Clustered GCs can be unioned the particleIndex representing the union 
						// is not associated with a PhysicsProxy
						if (const TSet<IPhysicsProxyBase*>* Proxies = Solver->GetProxies(Constraint.Particle[0]->Handle()))
						{
							for (IPhysicsProxyBase* Proxy : *Proxies)
							{
								if (NumValidCollisions >= CollisionRule.NumConstraints())
								{
									break;
								}

								if (Proxy != nullptr)
								{
									if (ensure(!Constraint.AccumulatedImpulse.ContainsNaN() && FMath::IsFinite(Constraint.GetPhi())))
									{
										FGeometryParticleHandle* Particle0 = Constraint.Particle[0];
										FGeometryParticleHandle* Particle1 = Constraint.Particle[1];
										FKinematicGeometryParticleHandle* Body0 = Particle0->CastToKinematicParticle();

										// presently when a rigidbody or kinematic hits static geometry then Body1 is null
										FKinematicGeometryParticleHandle* Body1 = Particle1->CastToKinematicParticle();

										if (!Constraint.AccumulatedImpulse.IsZero() && Body0)
										{
											if (ensure(!Constraint.GetLocation().ContainsNaN() &&
												!Constraint.GetNormal().ContainsNaN()) &&
												!Body0->V().ContainsNaN() &&
												!Body0->W().ContainsNaN() &&
												(Body1 == nullptr || ((!Body1->V().ContainsNaN()) && !Body1->W().ContainsNaN())))
											{
												ValidCollisionHandles[NumValidCollisions] = ContactHandle;
												NumValidCollisions++;
											}
										}
									}
								}
							}
						}
					}
				}

				ValidCollisionHandles.SetNum(NumValidCollisions);

				if(ValidCollisionHandles.Num() > 0)
				{
					for (int32 IdxCollision = 0; IdxCollision < ValidCollisionHandles.Num(); ++IdxCollision)
					{
						if (ValidCollisionHandles[IdxCollision]->GetType() == FCollisionConstraintBase::FType::SinglePoint ||
							ValidCollisionHandles[IdxCollision]->GetType() == FCollisionConstraintBase::FType::SinglePointSwept)
						{
							const FRigidBodyPointContactConstraint& Constraint = (ValidCollisionHandles[IdxCollision]->GetType() == FCollisionConstraintBase::FType::SinglePoint) ? 
								ValidCollisionHandles[IdxCollision]->GetPointContact() :
								*ValidCollisionHandles[IdxCollision]->GetSweptPointContact().As<FRigidBodyPointContactConstraint>();

							FGeometryParticleHandle* Particle0 = Constraint.Particle[0];
							FGeometryParticleHandle* Particle1 = Constraint.Particle[1];

							FCollidingData Data;
							Data.Location = Constraint.GetLocation();
							Data.AccumulatedImpulse = Constraint.AccumulatedImpulse;
							Data.Normal = Constraint.GetNormal();
							Data.PenetrationDepth = Constraint.GetPhi();
							Data.ParticleProxy = Solver->GetProxies(Particle0->Handle()) && Solver->GetProxies(Particle0->Handle())->Array().Num() ? 
								Solver->GetProxies(Particle0->Handle())->Array().operator[](0) : nullptr; // @todo(chaos) : Iterate all proxies
							Data.LevelsetProxy = Solver->GetProxies(Particle1->Handle()) && Solver->GetProxies(Particle0->Handle())->Array().Num() ? 
								Solver->GetProxies(Particle1->Handle())->Array().operator[](0) : nullptr; // @todo(chaos) : Iterate all proxies

							if (FPBDRigidParticleHandle * Rigid0 = Particle0->CastToRigidParticle())
							{
								Data.DeltaVelocity1 = Rigid0->V() - Rigid0->PreV();
							}
							if (FPBDRigidParticleHandle * Rigid1 = Particle1->CastToRigidParticle())
							{
								Data.DeltaVelocity2 = Rigid1->V() - Rigid1->PreV();
							}

							// todo: do we need these anymore now we are storing the particles you can access all of this stuff from there
							// do we still need these now we have pointers to particles returned?
							FPBDRigidParticleHandle* PBDRigid0 = Particle0->CastToRigidParticle();
							if (PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic)
							{
								Data.Velocity1 = PBDRigid0->V();
								Data.AngularVelocity1 = PBDRigid0->W();
								Data.Mass1 = PBDRigid0->M();
							}

							FPBDRigidParticleHandle* PBDRigid1 = Particle1->CastToRigidParticle();
							if (PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic)
							{
								Data.Velocity2 = PBDRigid1->V();
								Data.AngularVelocity2 = PBDRigid1->W();
								Data.Mass2 = PBDRigid1->M();
							}

							IPhysicsProxyBase* const PhysicsProxy = Data.ParticleProxy;
							IPhysicsProxyBase* const OtherPhysicsProxy = Data.LevelsetProxy;
							//Data.Material1 = nullptr; // #todo: provide UPhysicalMaterial for Particle
							//Data.Material2 = nullptr; // #todo: provide UPhysicalMaterial for Levelset

							const FSolverCollisionEventFilter* SolverCollisionEventFilter = Solver->GetEventFilters()->GetCollisionFilter();
							if (!SolverCollisionEventFilter->Enabled() || SolverCollisionEventFilter->Pass(Data))

							{
								const int32 NewIdx = AllCollisionsDataArray.Add(FCollidingData());
								FCollidingData& CollisionDataArrayItem = AllCollisionsDataArray[NewIdx];

								CollisionDataArrayItem = Data;

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
								// If Constraint.ParticleIndex is a cluster store an index for a mesh in this cluster
								if (ClusterIdsArray[Constraint.ParticleIndex].NumChildren > 0)
								{
									int32 ParticleIndexMesh = GetParticleIndexMesh(ParentToChildrenMap, Constraint.ParticleIndex);
									ensure(ParticleIndexMesh != INDEX_NONE);
									CollisionDataArrayItem.ParticleIndexMesh = ParticleIndexMesh;
								}
								// If Constraint.LevelsetIndex is a cluster store an index for a mesh in this cluster
								if (ClusterIdsArray[Constraint.LevelsetIndex].NumChildren > 0)
								{
									int32 LevelsetIndexMesh = GetParticleIndexMesh(ParentToChildrenMap, Constraint.LevelsetIndex);
									ensure(LevelsetIndexMesh != INDEX_NONE);
									CollisionDataArrayItem.LevelsetIndexMesh = LevelsetIndexMesh;
								}
#endif

								// Add to AllCollisionsIndicesByPhysicsProxy
								AllCollisionsIndicesByPhysicsProxy.FindOrAdd(PhysicsProxy).Add(FEventManager::EncodeCollisionIndex(NewIdx, false));

								if (OtherPhysicsProxy && OtherPhysicsProxy != PhysicsProxy)
								{
									AllCollisionsIndicesByPhysicsProxy.FindOrAdd(OtherPhysicsProxy).Add(FEventManager::EncodeCollisionIndex(NewIdx, true));
								}
							}
						}
					}
				}
			}
		});
	}

	void FEventDefaults::RegisterBreakingEvent(FEventManager& EventManager)
	{
		EventManager.template RegisterEvent<FBreakingEventData>(EEventType::Breaking, []
		(const Chaos::FPBDRigidsSolver* Solver, FBreakingEventData& BreakingEventData)
		{
			check(Solver);
			SCOPE_CYCLE_COUNTER(STAT_GatherBreakingEvent);

			// #todo: This isn't working - SolverActor parameters are set on a solver but it is currently a different solver that is simulating!!
			if (!Solver->GetEventFilters()->IsBreakingEventEnabled())
				return;

			FBreakingDataArray& AllBreakingDataArray = BreakingEventData.BreakingData.AllBreakingsArray;

			AllBreakingDataArray.Reset();

			BreakingEventData.BreakingData.TimeCreated = Solver->MTime;

			const auto* Evolution = Solver->GetEvolution();
			const FPBDRigidParticles& Particles = Evolution->GetParticles().GetDynamicParticles();
			const TArray<FBreakingData>& AllBreakingsArray = Evolution->GetRigidClustering().GetAllClusterBreakings();
			const TArrayCollectionArray<ClusterId>& ClusterIdsArray = Evolution->GetRigidClustering().GetClusterIdsArray();

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap& ParentToChildrenMap = Evolution->GetRigidClustering().GetChildrenMap();
#endif

			if(AllBreakingsArray.Num() > 0)
			{
				for(int32 Idx = 0; Idx < AllBreakingsArray.Num(); ++Idx)
				{
					// Since Clustered GCs can be unioned the particleIndex representing the union 
					// is not associated with a PhysicsProxy
					FPBDRigidParticleHandle* PBDRigid = AllBreakingsArray[Idx].Particle->CastToRigidParticle();
					if(PBDRigid)
					{
						if(ensure(!AllBreakingsArray[Idx].Location.ContainsNaN() &&
							!PBDRigid->V().ContainsNaN() &&
							!PBDRigid->W().ContainsNaN()))
						{
							FBreakingData BreakingData;
							BreakingData.Location = AllBreakingsArray[Idx].Location;
							BreakingData.Velocity = PBDRigid->V();
							BreakingData.AngularVelocity = PBDRigid->W();
							BreakingData.Mass = PBDRigid->M();
							BreakingData.Particle = PBDRigid;
							BreakingData.ParticleProxy = Solver->GetProxies(PBDRigid->Handle()) && Solver->GetProxies(PBDRigid->Handle())->Array().Num() ?
								Solver->GetProxies(PBDRigid->Handle())->Array().operator[](0) : nullptr; // @todo(chaos) : Iterate all proxies
							
							if(PBDRigid->Geometry()->HasBoundingBox())
							{
								BreakingData.BoundingBox = PBDRigid->Geometry()->BoundingBox();
							}

							const FSolverBreakingEventFilter* SolverBreakingEventFilter = Solver->GetEventFilters()->GetBreakingFilter();
							if(!SolverBreakingEventFilter->Enabled() || SolverBreakingEventFilter->Pass(BreakingData))
							{
								int32 NewIdx = AllBreakingDataArray.Add(FBreakingData());
								FBreakingData& BreakingDataArrayItem = AllBreakingDataArray[NewIdx];
								BreakingDataArrayItem = BreakingData;

#if 0 // #todo
								// If AllBreakingsArray[Idx].ParticleIndex is a cluster store an index for a mesh in this cluster
								if(ClusterIdsArray[AllBreakingsArray[Idx].ParticleIndex].NumChildren > 0)
								{
									int32 ParticleIndexMesh = GetParticleIndexMesh(ParentToChildrenMap, AllBreakingsArray[Idx].ParticleIndex);
									ensure(ParticleIndexMesh != INDEX_NONE);
									BreakingDataArrayItem.ParticleIndexMesh = ParticleIndexMesh;
								}
#endif
								}
						}
					}
				}
			}
		});
	}

	void FEventDefaults::RegisterTrailingEvent(FEventManager& EventManager)
	{
		EventManager.template RegisterEvent<FTrailingEventData>(EEventType::Trailing, []
		(const Chaos::FPBDRigidsSolver* Solver, FTrailingEventData& TrailingEventData)
		{
			check(Solver);

			// #todo: This isn't working - SolverActor parameters are set on a solver but it is currently a different solver that is simulating!!
			if (!Solver->GetEventFilters()->IsTrailingEventEnabled())
				return;

			const auto* Evolution = Solver->GetEvolution();

			const TArrayCollectionArray<ClusterId>& ClusterIdsArray = Evolution->GetRigidClustering().GetClusterIdsArray();
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			const TMap<uint32, TUniquePtr<TArray<uint32>>>& ParentToChildrenMap = Evolution->GetRigidClustering().GetChildrenMap();
#endif
			auto& AllTrailingsDataArray = TrailingEventData.TrailingData.AllTrailingsArray;

			AllTrailingsDataArray.Reset();

			TrailingEventData.TrailingData.TimeCreated = Solver->MTime;

			for (auto& ActiveParticle : Evolution->GetParticles().GetActiveParticlesView())
			{

				if (ensure(FMath::IsFinite(ActiveParticle.InvM())))
				{
					if (ActiveParticle.InvM() != 0.f &&
						ActiveParticle.Geometry() &&
						ActiveParticle.Geometry()->HasBoundingBox())
					{
						if (ensure(!ActiveParticle.X().ContainsNaN() &&
							!ActiveParticle.V().ContainsNaN() &&
							!ActiveParticle.W().ContainsNaN() &&
							FMath::IsFinite(ActiveParticle.M())))
						{
							FTrailingData TrailingData;
							TrailingData.Location = ActiveParticle.X();
							TrailingData.Velocity = ActiveParticle.V();
							TrailingData.AngularVelocity = ActiveParticle.W();
							TrailingData.Mass = ActiveParticle.M();
							TrailingData.Particle = nullptr; // #todo: provide a particle
							if (ActiveParticle.Geometry()->HasBoundingBox())
							{
								TrailingData.BoundingBox = ActiveParticle.Geometry()->BoundingBox();
							}

							const FSolverTrailingEventFilter* SolverTrailingEventFilter = Solver->GetEventFilters()->GetTrailingFilter();
							if (!SolverTrailingEventFilter->Enabled() || SolverTrailingEventFilter->Pass(TrailingData))
							{
								int32 NewIdx = AllTrailingsDataArray.Add(FTrailingData());
								FTrailingData& TrailingDataArrayItem = AllTrailingsDataArray[NewIdx];
								TrailingDataArrayItem = TrailingData;

								// If IdxParticle is a cluster store an index for a mesh in this cluster
#if 0
								if (ClusterIdsArray[IdxParticle].NumChildren > 0)
								{
									int32 ParticleIndexMesh = GetParticleIndexMesh(ParentToChildrenMap, IdxParticle);
									ensure(ParticleIndexMesh != INDEX_NONE);
									TrailingDataArrayItem.ParticleIndexMesh = ParticleIndexMesh;
								}
#endif
							}
						}
					}
				}
			}
		});
	}

	void FEventDefaults::RegisterSleepingEvent(FEventManager& EventManager)
	{
		EventManager.template RegisterEvent<FSleepingEventData>(EEventType::Sleeping, []
		(const Chaos::FPBDRigidsSolver* Solver, FSleepingEventData& SleepingEventData)
		{
			check(Solver);
			SCOPE_CYCLE_COUNTER(STAT_GatherSleepingEvent);

			const auto* Evolution = Solver->GetEvolution();

			FSleepingDataArray& EventSleepDataArray = SleepingEventData.SleepingData;
			EventSleepDataArray.Reset();

			Chaos::FPBDRigidsSolver* NonConstSolver = const_cast<Chaos::FPBDRigidsSolver*>(Solver);

			NonConstSolver->Particles.GetDynamicParticles().GetSleepDataLock().ReadLock();
			auto& SolverSleepingData = NonConstSolver->Particles.GetDynamicParticles().GetSleepData();
			for(const TSleepData<FReal, 3>& SleepData : SolverSleepingData)
			{
				if(SleepData.Particle)
				{
					if (const TSet<IPhysicsProxyBase*>* Proxies = Solver->GetProxies(SleepData.Particle))
					{
						for (IPhysicsProxyBase* Proxy : *Proxies)
						{
							FGeometryParticle* Particle = SleepData.Particle->GTGeometryParticle();
							if (Particle != nullptr && Proxy != nullptr)
							{
								int32 NewIdx = EventSleepDataArray.Add(FSleepingData());
								FSleepingData& SleepingDataArrayItem = EventSleepDataArray[NewIdx];
								SleepingDataArrayItem.Particle = Particle;
								SleepingDataArrayItem.Sleeping = SleepData.Sleeping;
							}
						}
					}
				}
			}
			NonConstSolver->Particles.GetDynamicParticles().GetSleepDataLock().ReadUnlock();

			NonConstSolver->Particles.GetDynamicParticles().ClearSleepData();


		});
	}
}