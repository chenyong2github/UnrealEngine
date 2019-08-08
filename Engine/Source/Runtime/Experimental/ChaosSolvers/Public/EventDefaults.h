// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EventManager.h"
#include "EventsData.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidClustering.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "PBDRigidsSolver.h"

namespace Chaos
{
	class FEventDefaults
	{
	public:

		/**
		 * Register default event types
		 */
		static void RegisterSystemEvents(FEventManager& EventManager)
		{
			RegisterCollisionEvent(EventManager);
			RegisterBreakingEvent(EventManager);
			RegisterTrailingEvent(EventManager);
			RegisterSleepingEvent(EventManager);
		}

	private:

		/**
		 * Register collision event gathering function & data type
		 */
		static void RegisterCollisionEvent(FEventManager& EventManager)
		{
			EventManager.RegisterEvent<FCollisionEventData>(EEventType::Collision, []
			(const Chaos::FPBDRigidsSolver* Solver, FCollisionEventData& CollisionEventData)
			{
				check(Solver);

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
				const TArray<FPhysicsSolver::FPBDCollisionConstraints::FRigidBodyContactConstraint>& AllConstraintsArray = CollisionRule.GetAllConstraints();

				const TPBDRigidParticles<float, 3>& Particles = Evolution->GetParticles().GetDynamicParticles();
				const TArrayCollectionArray<ClusterId>& ClusterIdsArray = Evolution->GetRigidClustering().GetClusterIdsArray();
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
				const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap& ParentToChildrenMap = Evolution->GetRigidClustering().GetChildrenMap();
#endif
				const Chaos::TPBDRigidClustering<FPhysicsSolver::FPBDRigidsEvolution, FPhysicsSolver::FPBDCollisionConstraints, float, 3>::FClusterMap& ParentToChildrenMap = Evolution->GetRigidClustering().GetChildrenMap();

				if (AllConstraintsArray.Num() > 0)
				{
					// There's a couple of nasty const correctness issues here
					Chaos::FPBDRigidsSolver* NonConstSolver = (Chaos::FPBDRigidsSolver*)(Solver);
					TMap<IPhysicsProxyBase*, TArray<int32>>* ProxyMap = (TMap<IPhysicsProxyBase*, TArray<int32>>*)(&AllCollisionsIndicesByPhysicsProxy);
					// Add the keys to AllCollisionsIndicesByPhysicsProxy map
					NonConstSolver->ForEachPhysicsProxy([ProxyMap](auto* Obj)
					{
						if (Obj && Obj->IsSimulating())
						{
							if (!ProxyMap->Contains(Obj))
							{
								ProxyMap->Reset();
								ProxyMap->Add(Obj, TArray<int32>());
							}
						}
					});

					// Get the number of valid constraints (AccumulatedImpulse != 0.f and Phi < 0.f) from AllConstraintsArray
					TArray<int32> ValidCollisionIndices;
					ValidCollisionIndices.SetNumUninitialized(AllConstraintsArray.Num());
					int32 NumValidCollisions = 0;

					for (int32 Idx = 0; Idx < AllConstraintsArray.Num(); ++Idx)
					{
						const TRigidBodyContactConstraint<float, 3>& Constraint = AllConstraintsArray[Idx];

						// Since Clustered GCs can be unioned the particleIndex representing the union 
						// is not associated with a PhysicsProxy
						if (Constraint.Particle->Handle()->GTGeometryParticle()->Proxy != nullptr)

						{
							if (ensure(!Constraint.AccumulatedImpulse.ContainsNaN() && FMath::IsFinite(Constraint.Phi)))
							{
								TGeometryParticleHandle<float, 3>* Particle0 = Constraint.Particle;
								TGeometryParticleHandle<float, 3>* Particle1 = Constraint.Levelset;
								TKinematicGeometryParticleHandle<float, 3>* Body0 = Particle0->AsKinematic();

								// presently when a rigidbody or kinematic hits static geometry then Body1 is null
								TKinematicGeometryParticleHandle<float, 3>* Body1 = Particle1->AsKinematic();

								if (!Constraint.AccumulatedImpulse.IsZero() && Body0)
								{
									if (ensure(!Constraint.Location.ContainsNaN() &&
										!Constraint.Normal.ContainsNaN()) &&
										!Body0->V().ContainsNaN() &&
										!Body0->W().ContainsNaN() &&
										(Body1==nullptr || (!Body1->V().ContainsNaN()) && !Body1->W().ContainsNaN()) )
									{
										ValidCollisionIndices[NumValidCollisions] = Idx;
										NumValidCollisions++;
									}
								}
							}
						}
					}

					ValidCollisionIndices.SetNum(NumValidCollisions);

					if (ValidCollisionIndices.Num() > 0)
					{
						for (int32 IdxCollision = 0; IdxCollision < ValidCollisionIndices.Num(); ++IdxCollision)
						{
							Chaos::TPBDCollisionConstraint<float, 3>::FRigidBodyContactConstraint const& Constraint = AllConstraintsArray[ValidCollisionIndices[IdxCollision]];

							TGeometryParticleHandle<float, 3>* Particle0 = Constraint.Particle;
							TGeometryParticleHandle<float, 3>* Particle1 = Constraint.Levelset;

							TCollisionData<float, 3> Data;
							Data.Location = Constraint.Location;
							Data.AccumulatedImpulse = Constraint.AccumulatedImpulse;
							Data.Normal = Constraint.Normal;
							Data.PenetrationDepth = Constraint.Phi;
							Data.Particle = Particle0->Handle()->GTGeometryParticle();
							Data.Levelset = Particle1->Handle()->GTGeometryParticle();

							// todo: do we need these anymore now we are storing the particles you can access all of this stuff from there
							// do we still need these now we have pointers to particles returned?
							if (TPBDRigidParticleHandle<float, 3>* PBDRigid0 = Particle0->AsDynamic())
							{
								Data.Velocity1 = PBDRigid0->V();
								Data.AngularVelocity1 = PBDRigid0->W();
								Data.Mass1 = PBDRigid0->M();
							}

							if (TPBDRigidParticleHandle<float, 3>* PBDRigid1 = Particle1->AsDynamic())
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
							if (!SolverCollisionEventFilter->Enabled() || SolverCollisionEventFilter->Pass(Data))

							{
								const int32 NewIdx = AllCollisionsDataArray.Add(TCollisionData<float, 3>());
								TCollisionData<float, 3>& CollisionDataArrayItem = AllCollisionsDataArray[NewIdx];

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

			});
		}

		/**
		 * Register breaking event gathering function & data type
		 */
		static void RegisterBreakingEvent(FEventManager& EventManager)
		{
			EventManager.RegisterEvent<FBreakingEventData>(EEventType::Breaking, []
			(const Chaos::FPBDRigidsSolver* Solver, FBreakingEventData& BreakingEventData)
			{
				check(Solver);

				// #todo: This isn't working - SolverActor parameters are set on a solver but it is currently a different solver that is simulating!!
				//if (!Solver->GetEventFilters()->IsBreakingEventEnabled())
				//	return;

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

				if (AllBreakingsArray.Num() > 0)
				{
					// There's a couple of nasty const correctness issues here
					Chaos::FPBDRigidsSolver* NonConstSolver = (Chaos::FPBDRigidsSolver*)(Solver);
					TMap<IPhysicsProxyBase*, TArray<int32>>* ProxyMap = (TMap<IPhysicsProxyBase*, TArray<int32>>*)(&AllBreakingIndicesByPhysicsProxy);
					NonConstSolver->ForEachPhysicsProxy([ProxyMap](auto* Obj)
					{
						if (Obj && Obj->IsSimulating())
						{
							if (!ProxyMap->Contains(Obj))
							{
								ProxyMap->Add(Obj, TArray<int32>());
							}
						}
					});

					for (int32 Idx = 0; Idx < AllBreakingsArray.Num(); ++Idx)
					{
						// Since Clustered GCs can be unioned the particleIndex representing the union 
						// is not associated with a PhysicsProxy
						if (AllBreakingsArray[Idx].Particle->Proxy != nullptr)
						{
							if (ensure(!AllBreakingsArray[Idx].Location.ContainsNaN() &&
								!Particles.V(AllBreakingsArray[Idx].ParticleIndex).ContainsNaN() &&
								!Particles.W(AllBreakingsArray[Idx].ParticleIndex).ContainsNaN()))
							{
								TBreakingData<float, 3> BreakingData;
								BreakingData.Location = AllBreakingsArray[Idx].Location;
								BreakingData.Velocity = Particles.V(AllBreakingsArray[Idx].ParticleIndex);
								BreakingData.AngularVelocity = Particles.W(AllBreakingsArray[Idx].ParticleIndex);
								BreakingData.Mass = Particles.M(AllBreakingsArray[Idx].ParticleIndex);
								BreakingData.ParticleIndex = AllBreakingsArray[Idx].ParticleIndex;
								if (Particles.Geometry(Idx)->HasBoundingBox())
								{
									BreakingData.BoundingBox = Particles.Geometry(Idx)->BoundingBox();;
								}

								const FSolverBreakingEventFilter* SolverBreakingEventFilter = Solver->GetEventFilters()->GetBreakingFilter();
								if (!SolverBreakingEventFilter->Enabled() || SolverBreakingEventFilter->Pass(BreakingData))
								{
									int32 NewIdx = AllBreakingDataArray.Add(TBreakingData<float, 3>());
									TBreakingData<float, 3>& BreakingDataArrayItem = AllBreakingDataArray[NewIdx];
									BreakingDataArrayItem = BreakingData;

									// If AllBreakingsArray[Idx].ParticleIndex is a cluster store an index for a mesh in this cluster
									if (ClusterIdsArray[AllBreakingsArray[Idx].ParticleIndex].NumChildren > 0)
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

		/**
		 * Register trailing event gathering function & data type
		 */
		static void RegisterTrailingEvent(FEventManager& EventManager)
		{
			EventManager.RegisterEvent<FTrailingEventData>(EEventType::Trailing, []
			(const Chaos::FPBDRigidsSolver* Solver, FTrailingEventData& TrailingEventData)
			{
				check(Solver);

				// #todo: This isn't working - SolverActor parameters are set on a solver but it is currently a different solver that is simulating!!
				//if (!Solver->GetEventFilters()->IsTrailingEventEnabled())
				//	return;

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

				// There's a couple of nasty const correctness issues here
				Chaos::FPBDRigidsSolver* NonConstSolver = (Chaos::FPBDRigidsSolver*)(Solver);
				TMap<IPhysicsProxyBase*, TArray<int32>>* ProxyMap = (TMap<IPhysicsProxyBase*, TArray<int32>>*)(&AllTrailingsIndicesByPhysicsProxy);
				// Add the keys to AllTrailingsIndicesByPhysicsProxy map
				NonConstSolver->ForEachPhysicsProxy([ProxyMap](auto* Obj)
				{
					if (Obj && Obj->IsSimulating())
					{
						if (!ProxyMap->Contains(Obj))
						{
							ProxyMap->Add(Obj, TArray<int32>());
						}
					}
				});

				for (auto& ActiveParticle :  Evolution->GetParticles().GetActiveParticlesView())
				{
					TPBDRigidParticle<float, 3>* GTParticle = ActiveParticle.Handle()->GTGeometryParticle()->AsDynamic();
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

		static void RegisterSleepingEvent(FEventManager& EventManager)
		{
			//#todo: implement sleeping events
			//EventManager.RegisterEvent<FSleepingEventData>(EEventType::Sleeping, []
			//(const Chaos::FPBDRigidsSolver* Solver, FSleepingEventData& SleepingEventData)
			//{
			//	check(Solver);

			//});
		}



	};
}