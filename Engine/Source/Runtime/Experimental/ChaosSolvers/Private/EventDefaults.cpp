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
#include "PhysicsProxy/FieldSystemPhysicsProxy.h"

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
		EventManager.RegisterEvent<FCollisionEventData>(EEventType::Collision, []
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

			const FPBDRigidsSolver::FPBDRigidsEvolution* Evolution = Solver->GetEvolution();

			const FPhysicsSolver::FPBDCollisionConstraints& CollisionRule = Evolution->GetCollisionConstraints();


			const TPBDRigidParticles<float, 3>& Particles = Evolution->GetParticles().GetDynamicParticles();
			const TArrayCollectionArray<ClusterId>& ClusterIdsArray = Evolution->GetRigidClustering().GetClusterIdsArray();
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap& ParentToChildrenMap = Evolution->GetRigidClustering().GetChildrenMap();
#endif
			const Chaos::TPBDRigidClustering<FPhysicsSolver::FPBDRigidsEvolution, FPhysicsSolver::FPBDCollisionConstraints, float, 3>::FClusterMap& ParentToChildrenMap = Evolution->GetRigidClustering().GetChildrenMap();

			if(CollisionRule.NumConstraints() > 0)
			{
				// Get the number of valid constraints (AccumulatedImpulse != 0.f and Phi < 0.f) from AllConstraintsArray
				TArray<const Chaos::TPBDCollisionConstraintHandle<float, 3> *> ValidCollisionHandles;
				ValidCollisionHandles.SetNumUninitialized(CollisionRule.NumConstraints());
				int32 NumValidCollisions = 0;

				for (const Chaos::TPBDCollisionConstraintHandle<float, 3> * ContactHandle : CollisionRule.GetConstConstraintHandles())
				{
					if (ContactHandle->GetType() == TPBDCollisionConstraintHandle<float, 3>::FConstraintBase::FType::SinglePoint)
					{
						const TRigidBodyPointContactConstraint<float, 3>& Constraint = ContactHandle->GetPointContact();

						// Since Clustered GCs can be unioned the particleIndex representing the union 
						// is not associated with a PhysicsProxy
						if (Constraint.Particle[0]->Handle()->GTGeometryParticle()->Proxy != nullptr)

						{
							if (ensure(!Constraint.AccumulatedImpulse.ContainsNaN() && FMath::IsFinite(Constraint.GetPhi())))
							{
								TGeometryParticleHandle<float, 3>* Particle0 = Constraint.Particle[0];
								TGeometryParticleHandle<float, 3>* Particle1 = Constraint.Particle[1];
								TKinematicGeometryParticleHandle<float, 3>* Body0 = Particle0->CastToKinematicParticle();

								// presently when a rigidbody or kinematic hits static geometry then Body1 is null
								TKinematicGeometryParticleHandle<float, 3>* Body1 = Particle1->CastToKinematicParticle();

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

				ValidCollisionHandles.SetNum(NumValidCollisions);

				if(ValidCollisionHandles.Num() > 0)
				{
					for(int32 IdxCollision = 0; IdxCollision < ValidCollisionHandles.Num(); ++IdxCollision)
					{
						Chaos::TPBDCollisionConstraints<float, 3>::FPointContactConstraint const& Constraint = ValidCollisionHandles[IdxCollision]->GetPointContact();

						TGeometryParticleHandle<float, 3>* Particle0 = Constraint.Particle[0];
						TGeometryParticleHandle<float, 3>* Particle1 = Constraint.Particle[1];

						TCollisionData<float, 3> Data;
						Data.Location = Constraint.GetLocation();
						Data.AccumulatedImpulse = Constraint.AccumulatedImpulse;
						Data.Normal = Constraint.GetNormal();
						Data.PenetrationDepth = Constraint.GetPhi();
						Data.Particle = Particle0->Handle()->GTGeometryParticle();
						Data.Levelset = Particle1->Handle()->GTGeometryParticle();

						// todo: do we need these anymore now we are storing the particles you can access all of this stuff from there
						// do we still need these now we have pointers to particles returned?
						TPBDRigidParticleHandle<float, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
						if(PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic)
						{
							Data.Velocity1 = PBDRigid0->V();
							Data.AngularVelocity1 = PBDRigid0->W();
							Data.Mass1 = PBDRigid0->M();
						}

						TPBDRigidParticleHandle<float, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
						if(PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic)
						{
							Data.Velocity2 = PBDRigid1->V();
							Data.AngularVelocity2 = PBDRigid1->W();
							Data.Mass2 = PBDRigid1->M();
						}

						IPhysicsProxyBase* const PhysicsProxy = Data.Particle->Proxy;
						IPhysicsProxyBase* const OtherPhysicsProxy = Data.Levelset->Proxy;
						//Data.Material1 = nullptr; // #todo: provide UPhysicalMaterial for Particle
						//Data.Material2 = nullptr; // #todo: provide UPhysicalMaterial for Levelset

						const FSolverCollisionEventFilter* SolverCollisionEventFilter = Solver->GetEventFilters()->GetCollisionFilter();
						if(!SolverCollisionEventFilter->Enabled() || SolverCollisionEventFilter->Pass(Data))

						{
							const int32 NewIdx = AllCollisionsDataArray.Add(TCollisionData<float, 3>());
							TCollisionData<float, 3>& CollisionDataArrayItem = AllCollisionsDataArray[NewIdx];

							CollisionDataArrayItem = Data;

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
							// If Constraint.ParticleIndex is a cluster store an index for a mesh in this cluster
							if(ClusterIdsArray[Constraint.ParticleIndex].NumChildren > 0)
							{
								int32 ParticleIndexMesh = GetParticleIndexMesh(ParentToChildrenMap, Constraint.ParticleIndex);
								ensure(ParticleIndexMesh != INDEX_NONE);
								CollisionDataArrayItem.ParticleIndexMesh = ParticleIndexMesh;
							}
							// If Constraint.LevelsetIndex is a cluster store an index for a mesh in this cluster
							if(ClusterIdsArray[Constraint.LevelsetIndex].NumChildren > 0)
							{
								int32 LevelsetIndexMesh = GetParticleIndexMesh(ParentToChildrenMap, Constraint.LevelsetIndex);
								ensure(LevelsetIndexMesh != INDEX_NONE);
								CollisionDataArrayItem.LevelsetIndexMesh = LevelsetIndexMesh;
							}
#endif

							// Add to AllCollisionsIndicesByPhysicsProxy
							AllCollisionsIndicesByPhysicsProxy.FindOrAdd(PhysicsProxy).Add(FEventManager::EncodeCollisionIndex(NewIdx, false));

							if(OtherPhysicsProxy && OtherPhysicsProxy != PhysicsProxy)
							{
								AllCollisionsIndicesByPhysicsProxy.FindOrAdd(OtherPhysicsProxy).Add(FEventManager::EncodeCollisionIndex(NewIdx, true));
							}
						}
					}
				}
			}
		});
	}

	void FEventDefaults::RegisterBreakingEvent(FEventManager& EventManager)
	{
		EventManager.RegisterEvent<FBreakingEventData>(EEventType::Breaking, []
		(const Chaos::FPBDRigidsSolver* Solver, FBreakingEventData& BreakingEventData)
		{
			check(Solver);
			SCOPE_CYCLE_COUNTER(STAT_GatherBreakingEvent);

			// #todo: This isn't working - SolverActor parameters are set on a solver but it is currently a different solver that is simulating!!
			if (!Solver->GetEventFilters()->IsBreakingEventEnabled())
				return;

			FBreakingDataArray& AllBreakingDataArray = BreakingEventData.BreakingData.AllBreakingsArray;
			TMap<IPhysicsProxyBase*, TArray<int32>>& AllBreakingIndicesByPhysicsProxy = BreakingEventData.PhysicsProxyToBreakingIndices.PhysicsProxyToIndicesMap;

			AllBreakingDataArray.Reset();
			AllBreakingIndicesByPhysicsProxy.Reset();

			BreakingEventData.BreakingData.TimeCreated = Solver->MTime;
			BreakingEventData.PhysicsProxyToBreakingIndices.TimeCreated = Solver->MTime;

			const FPBDRigidsSolver::FPBDRigidsEvolution* Evolution = Solver->GetEvolution();
			const TPBDRigidParticles<float, 3>& Particles = Evolution->GetParticles().GetDynamicParticles();
			const TArray<TBreakingData<float, 3>>& AllBreakingsArray = Evolution->GetRigidClustering().GetAllClusterBreakings();
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
					if(AllBreakingsArray[Idx].Particle->Proxy != nullptr)
					{
						if(ensure(!AllBreakingsArray[Idx].Location.ContainsNaN() &&
							!Particles.V(AllBreakingsArray[Idx].ParticleIndex).ContainsNaN() &&
							!Particles.W(AllBreakingsArray[Idx].ParticleIndex).ContainsNaN()))
						{
							TBreakingData<float, 3> BreakingData;
							BreakingData.Location = AllBreakingsArray[Idx].Location;
							BreakingData.Velocity = Particles.V(AllBreakingsArray[Idx].ParticleIndex);
							BreakingData.AngularVelocity = Particles.W(AllBreakingsArray[Idx].ParticleIndex);
							BreakingData.Mass = Particles.M(AllBreakingsArray[Idx].ParticleIndex);
							BreakingData.ParticleIndex = AllBreakingsArray[Idx].ParticleIndex;
							if(Particles.Geometry(Idx)->HasBoundingBox())
							{
								BreakingData.BoundingBox = Particles.Geometry(Idx)->BoundingBox();;
							}

							const FSolverBreakingEventFilter* SolverBreakingEventFilter = Solver->GetEventFilters()->GetBreakingFilter();
							if(!SolverBreakingEventFilter->Enabled() || SolverBreakingEventFilter->Pass(BreakingData))
							{
								int32 NewIdx = AllBreakingDataArray.Add(TBreakingData<float, 3>());
								TBreakingData<float, 3>& BreakingDataArrayItem = AllBreakingDataArray[NewIdx];
								BreakingDataArrayItem = BreakingData;

								// If AllBreakingsArray[Idx].ParticleIndex is a cluster store an index for a mesh in this cluster
								if(ClusterIdsArray[AllBreakingsArray[Idx].ParticleIndex].NumChildren > 0)
								{
#if 0 // #todo
									int32 ParticleIndexMesh = GetParticleIndexMesh(ParentToChildrenMap, AllBreakingsArray[Idx].ParticleIndex);
									ensure(ParticleIndexMesh != INDEX_NONE);
									BreakingDataArrayItem.ParticleIndexMesh = ParticleIndexMesh;
#endif
								}

								// Add to AllBreakingsIndicesByPhysicsProxy
								IPhysicsProxyBase* PhysicsProxy = AllBreakingsArray[Idx].Particle->Proxy;
								AllBreakingIndicesByPhysicsProxy.FindOrAdd(PhysicsProxy).Add(NewIdx);
							}
						}
					}
				}
			}
		});
	}

	void FEventDefaults::RegisterTrailingEvent(FEventManager& EventManager)
	{
		EventManager.RegisterEvent<FTrailingEventData>(EEventType::Trailing, []
		(const Chaos::FPBDRigidsSolver* Solver, FTrailingEventData& TrailingEventData)
		{
			check(Solver);

			// #todo: This isn't working - SolverActor parameters are set on a solver but it is currently a different solver that is simulating!!
			if (!Solver->GetEventFilters()->IsTrailingEventEnabled())
				return;

			const FPBDRigidsSolver::FPBDRigidsEvolution* Evolution = Solver->GetEvolution();

			const TArrayCollectionArray<ClusterId>& ClusterIdsArray = Evolution->GetRigidClustering().GetClusterIdsArray();
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			const TMap<uint32, TUniquePtr<TArray<uint32>>>& ParentToChildrenMap = Evolution->GetRigidClustering().GetChildrenMap();
#endif
			auto& AllTrailingsDataArray = TrailingEventData.TrailingData.AllTrailingsArray;
			auto& AllTrailingsIndicesByPhysicsProxy = TrailingEventData.PhysicsProxyToTrailingIndices.PhysicsProxyToIndicesMap;

			AllTrailingsDataArray.Reset();
			AllTrailingsIndicesByPhysicsProxy.Reset();

			TrailingEventData.TrailingData.TimeCreated = Solver->MTime;
			TrailingEventData.PhysicsProxyToTrailingIndices.TimeCreated = Solver->MTime;

			for (auto& ActiveParticle : Evolution->GetParticles().GetActiveParticlesView())
			{
				TPBDRigidParticle<float, 3>* GTParticle = ActiveParticle.Handle()->GTGeometryParticle()->CastToRigidParticle();
				// Since Clustered GCs can be unioned the particleIndex representing the union 
				// is not associated with a PhysicsProxy
				if (GTParticle && GTParticle->Proxy != nullptr)
				{
					if (ensure(FMath::IsFinite(GTParticle->InvM())))
					{
						if (GTParticle->InvM() != 0.f &&
							ActiveParticle.Geometry() &&
							ActiveParticle.Geometry()->HasBoundingBox())
						{
							if (ensure(!GTParticle->X().ContainsNaN() &&
								!GTParticle->V().ContainsNaN() &&
								!GTParticle->W().ContainsNaN() &&
								FMath::IsFinite(GTParticle->M())))
							{
								TTrailingData<float, 3> TrailingData;
								TrailingData.Location = GTParticle->X();
								TrailingData.Velocity = GTParticle->V();
								TrailingData.AngularVelocity = GTParticle->W();
								TrailingData.Mass = GTParticle->M();
								TrailingData.Particle = GTParticle;
								if (ActiveParticle.Geometry()->HasBoundingBox())
								{
									TrailingData.BoundingBox = ActiveParticle.Geometry()->BoundingBox();
								}

								const FSolverTrailingEventFilter* SolverTrailingEventFilter = Solver->GetEventFilters()->GetTrailingFilter();
								if (!SolverTrailingEventFilter->Enabled() || SolverTrailingEventFilter->Pass(TrailingData))
								{
									int32 NewIdx = AllTrailingsDataArray.Add(TTrailingData<float, 3>());
									TTrailingData<float, 3>& TrailingDataArrayItem = AllTrailingsDataArray[NewIdx];
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

									// Add to AllTrailingsIndicesByPhysicsProxy
									IPhysicsProxyBase* PhysicsProxy = GTParticle->Proxy;
									AllTrailingsIndicesByPhysicsProxy.FindOrAdd(PhysicsProxy).Add(NewIdx);
								}
							}
						}
					}
				}
			}
		});
	}

	void FEventDefaults::RegisterSleepingEvent(FEventManager& EventManager)
	{
		EventManager.RegisterEvent<FSleepingEventData>(EEventType::Sleeping, []
		(const Chaos::FPBDRigidsSolver* Solver, FSleepingEventData& SleepingEventData)
		{
			check(Solver);
			SCOPE_CYCLE_COUNTER(STAT_GatherSleepingEvent);

			const FPBDRigidsSolver::FPBDRigidsEvolution* Evolution = Solver->GetEvolution();

			FSleepingDataArray& EventSleepDataArray = SleepingEventData.SleepingData;
			TMap<IPhysicsProxyBase*, TArray<int32>>& AllSleepIndicesByPhysicsProxy = SleepingEventData.PhysicsProxyToSleepingIndices.PhysicsProxyToIndicesMap;
			EventSleepDataArray.Reset();
			AllSleepIndicesByPhysicsProxy.Reset();

			Chaos::FPBDRigidsSolver* NonConstSolver = (Chaos::FPBDRigidsSolver*)(Solver);
			auto& SolverSleepingData = NonConstSolver->Particles.GetDynamicParticles().GetSleepData();
			for(const TSleepData<float, 3>& SleepData : SolverSleepingData)
			{
				if(SleepData.Particle)
				{
					TGeometryParticle<float, 3>* Particle = SleepData.Particle->GTGeometryParticle();
					if(Particle->Proxy != nullptr)
					{
						int32 NewIdx = EventSleepDataArray.Add(TSleepingData<float, 3>());
						TSleepingData<float, 3>& SleepingDataArrayItem = EventSleepDataArray[NewIdx];
						SleepingDataArrayItem.Particle = Particle;
						SleepingDataArrayItem.Sleeping = SleepData.Sleeping;

						IPhysicsProxyBase* PhysicsProxy = Particle->Proxy;
						AllSleepIndicesByPhysicsProxy.FindOrAdd(PhysicsProxy).Add(NewIdx);
					}
				}
			}

			SolverSleepingData.Empty();

		});
	}

}