// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosBlueprint.h"
#include "PBDRigidsSolver.h"
#include "Async/Async.h"
#include "SolverObjects/GeometryCollectionPhysicsObject.h"

UChaosDestructionListener::UChaosDestructionListener(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer), LastCollisionDataTimeStamp(-1.f), LastBreakingDataTimeStamp(-1.f), LastTrailingDataTimeStamp(-1.f)
{
	bUseAttachParentBound = true;
	bAutoActivate = true;	
	bNeverNeedsRenderUpdate = true;

	PrimaryComponentTick.bCanEverTick = true;

#if INCLUDE_CHAOS
	SetCollisionFilter(MakeShareable(new FChaosCollisionEventFilter(&CollisionEventRequestSettings)));
	SetBreakingFilter(MakeShareable(new FChaosBreakingEventFilter(&BreakingEventRequestSettings)));
	SetTrailingFilter(MakeShareable(new FChaosTrailingEventFilter(&TrailingEventRequestSettings)));
#endif
}

void UChaosDestructionListener::UpdateSolvers()
{
#if INCLUDE_CHAOS
	// Reset the solvers
	Solvers.Reset();

	if (!ChaosSolverActors.Num())
	{
		if (TSharedPtr<FPhysScene_Chaos> WorldSolver = GetWorld()->PhysicsScene_Chaos)
		{
			if (Chaos::FPBDRigidsSolver* Solver = WorldSolver->GetSolver())
			{
				Solvers.Add(Solver);
			}
		}
	}
	else
	{
		for (AChaosSolverActor* ChaosSolverActorObject : ChaosSolverActors)
		{
			if (ChaosSolverActorObject)
			{
				if (Chaos::FPBDRigidsSolver* Solver = ChaosSolverActorObject->GetSolver())
				{
					Solvers.Add(Solver);
				}
			}
		}
	}
#endif
}

void UChaosDestructionListener::UpdateGeometryCollectionPhysicsObjects()
{
#if INCLUDE_CHAOS
	GeometryCollectionPhysicsObjects.Reset();

	if (GeometryCollectionActors.Num() > 0)
	{
		for (AGeometryCollectionActor* GeometryCollectionActorObject : GeometryCollectionActors)
		{
			if (GeometryCollectionActorObject)
			{
				// Get GeometryCollectionComponent
				if (const UGeometryCollectionComponent* GeometryCollectionComponent = GeometryCollectionActorObject->GetGeometryCollectionComponent())
				{
					// Get GeometryCollectionPhysicsObject
					if (const FGeometryCollectionPhysicsObject* GeometryCollectionPhysicsObject = GeometryCollectionComponent->GetPhysicsObject())
					{
						if (Chaos::FPBDRigidsSolver* Solver = GeometryCollectionPhysicsObject->GetSolver())
						{
							if (!Solvers.Contains(Solver))
							{
								GeometryCollectionPhysicsObjects.Add(GeometryCollectionPhysicsObject);
							}
						}
					}
				}
			}
		}
	}
#endif
}

void UChaosDestructionListener::GetDataFromSolvers()
{
#if INCLUDE_CHAOS
	// Loop through each solver and build up the array of data before passing off to the task to sort and analyze
	// Note: we currently need to do this on the GT since the Solver getter itself is not thread safe so can't be retrieved on the task itself.
	for (Chaos::FPBDRigidsSolver* Solver : Solvers)
	{
		if (Solver)
		{
			Chaos::FPBDRigidsSolver::FScopedGetEventsData ScopedAccess = Solver->ScopedGetEventsData();

			if (bIsCollisionEventListeningEnabled)
			{
				if (Solver->GetGenerateCollisionData() && Solver->GetSolverTime() > 0.f)
				{
					// ----------------------------------------------------------------------------------------------------------
					// GETTING DATA FROM PBDRIGIDSOLVER
					// ----------------------------------------------------------------------------------------------------------
					const Chaos::FPBDRigidsSolver::FAllCollisionDataMaps& AllCollisionData_Maps = ScopedAccess.GetAllCollisions_Maps();
					Chaos::FPBDRigidsSolver::FAllCollisionData AllCollisions;
					Chaos::FPBDRigidsSolver::FSolverObjectReverseMapping SolverObjectReverseMapping;
					Chaos::FPBDRigidsSolver::FParticleIndexReverseMapping ParticleIndexReverseMapping;
					Chaos::FPBDRigidsSolver::FAllCollisionsIndicesBySolverObject AllCollisionsIndicesBySolverObject;
					if (AllCollisionData_Maps.IsValid())
					{
						const float DataTimeStamp = AllCollisionData_Maps.AllCollisionData->TimeCreated;
						if (DataTimeStamp > LastCollisionDataTimeStamp)
						{
							LastCollisionDataTimeStamp = DataTimeStamp;

							if (AllCollisionData_Maps.AllCollisionData)
							{
								AllCollisions = *AllCollisionData_Maps.AllCollisionData;
							}
							else
							{
								return;
							}
							if (AllCollisionData_Maps.SolverObjectReverseMapping)
							{
								SolverObjectReverseMapping = *AllCollisionData_Maps.SolverObjectReverseMapping;
							}
							else
							{
								return;
							}
							if (AllCollisionData_Maps.ParticleIndexReverseMapping)
							{
								ParticleIndexReverseMapping = *AllCollisionData_Maps.ParticleIndexReverseMapping;
							}
							else
							{
								return;
							}
							if (AllCollisionData_Maps.AllCollisionsIndicesBySolverObject)
							{
								AllCollisionsIndicesBySolverObject = *AllCollisionData_Maps.AllCollisionsIndicesBySolverObject;
							}
							else
							{
								return;
							}

							// ----------------------------------------------------------------------------------------------------------
							// END OF GETTING DATA FROM PBDRIGIDSOLVER
							// ----------------------------------------------------------------------------------------------------------

							int32 NumCollisions = AllCollisions.AllCollisionsArray.Num();
							if (NumCollisions > 0)
							{
								RawCollisionDataArray.Append(AllCollisions.AllCollisionsArray.GetData(), NumCollisions);
							}
						}
					}
				}
			}

			if (bIsBreakingEventListeningEnabled)
			{
				if (Solver->GetGenerateBreakingData() && Solver->GetSolverTime() > 0.f)
				{
					// ----------------------------------------------------------------------------------------------------------
					// GETTING DATA FROM PBDRIGIDSOLVER
					// ----------------------------------------------------------------------------------------------------------
					const Chaos::FPBDRigidsSolver::FAllBreakingDataMaps& AllBreakingData_Maps = ScopedAccess.GetAllBreakings_Maps();
					Chaos::FPBDRigidsSolver::FAllBreakingData AllBreakings;
					Chaos::FPBDRigidsSolver::FSolverObjectReverseMapping SolverObjectReverseMapping;
					Chaos::FPBDRigidsSolver::FParticleIndexReverseMapping ParticleIndexReverseMapping;
					Chaos::FPBDRigidsSolver::FAllBreakingsIndicesBySolverObject AllBreakingsIndicesBySolverObject;
					if (AllBreakingData_Maps.IsValid())
					{
						const float DataTimeStamp = AllBreakingData_Maps.AllBreakingData->TimeCreated;
						if (DataTimeStamp > LastBreakingDataTimeStamp)
						{
							LastBreakingDataTimeStamp = DataTimeStamp;

							if (AllBreakingData_Maps.AllBreakingData)
							{
								AllBreakings = *AllBreakingData_Maps.AllBreakingData;
							}
							else
							{
								return;
							}
							if (AllBreakingData_Maps.SolverObjectReverseMapping)
							{
								SolverObjectReverseMapping = *AllBreakingData_Maps.SolverObjectReverseMapping;
							}
							else
							{
								return;
							}
							if (AllBreakingData_Maps.ParticleIndexReverseMapping)
							{
								ParticleIndexReverseMapping = *AllBreakingData_Maps.ParticleIndexReverseMapping;
							}
							else
							{
								return;
							}
							if (AllBreakingData_Maps.AllBreakingsIndicesBySolverObject)
							{
								AllBreakingsIndicesBySolverObject = *AllBreakingData_Maps.AllBreakingsIndicesBySolverObject;
							}
							else
							{
								return;
							}

							// ----------------------------------------------------------------------------------------------------------
							// END OF GETTING DATA FROM PBDRIGIDSOLVER
							// ----------------------------------------------------------------------------------------------------------

							int32 NumBreakings = AllBreakings.AllBreakingsArray.Num();
							if (NumBreakings > 0)
							{
								RawBreakingDataArray.Append(AllBreakings.AllBreakingsArray.GetData(), NumBreakings);
							}
						}
					}
				}
			}

			if (bIsTrailingEventListeningEnabled)
			{
				if (Solver->GetGenerateTrailingData() && Solver->GetSolverTime() > 0.f)
				{
					// ----------------------------------------------------------------------------------------------------------
					// GETTING DATA FROM PBDRIGIDSOLVER
					// ----------------------------------------------------------------------------------------------------------
					const Chaos::FPBDRigidsSolver::FAllTrailingDataMaps& AllTrailingData_Maps = ScopedAccess.GetAllTrailings_Maps();
					Chaos::FPBDRigidsSolver::FAllTrailingData AllTrailings;
					Chaos::FPBDRigidsSolver::FSolverObjectReverseMapping SolverObjectReverseMapping;
					Chaos::FPBDRigidsSolver::FParticleIndexReverseMapping ParticleIndexReverseMapping;
					Chaos::FPBDRigidsSolver::FAllTrailingsIndicesBySolverObject AllTrailingsIndicesBySolverObject;
					if (AllTrailingData_Maps.IsValid())
					{
						const float DataTimeStamp = AllTrailingData_Maps.AllTrailingData->TimeCreated;
						if (DataTimeStamp > LastTrailingDataTimeStamp)
						{
							LastTrailingDataTimeStamp = DataTimeStamp;

							if (AllTrailingData_Maps.AllTrailingData)
							{
								AllTrailings = *AllTrailingData_Maps.AllTrailingData;
							}
							else
							{
								return;
							}
							if (AllTrailingData_Maps.SolverObjectReverseMapping)
							{
								SolverObjectReverseMapping = *AllTrailingData_Maps.SolverObjectReverseMapping;
							}
							else
							{
								return;
							}
							if (AllTrailingData_Maps.ParticleIndexReverseMapping)
							{
								ParticleIndexReverseMapping = *AllTrailingData_Maps.ParticleIndexReverseMapping;
							}
							else
							{
								return;
							}
							if (AllTrailingData_Maps.AllTrailingsIndicesBySolverObject)
							{
								AllTrailingsIndicesBySolverObject = *AllTrailingData_Maps.AllTrailingsIndicesBySolverObject;
							}
							else
							{
								return;
							}

							// ----------------------------------------------------------------------------------------------------------
							// END OF GETTING DATA FROM PBDRIGIDSOLVER
							// ----------------------------------------------------------------------------------------------------------

							int32 NumTrailings = AllTrailings.AllTrailingsArray.Num();
							if (NumTrailings > 0)
							{
								RawTrailingDataArray.Append(AllTrailings.AllTrailingsArray.GetData(), NumTrailings);
							}
						}
					}
				}
			}
		}
	}
#endif
}

void UChaosDestructionListener::GetDataFromGeometryCollectionPhysicsObjects()
{
#if INCLUDE_CHAOS
	for (const FGeometryCollectionPhysicsObject* GeometryCollectionPhysicsObject : GeometryCollectionPhysicsObjects)
	{
		if (GeometryCollectionPhysicsObject)
		{
			if (bIsCollisionEventListeningEnabled)
			{
				if (Chaos::FPBDRigidsSolver* Solver = GeometryCollectionPhysicsObject->GetSolver())
				{
					if (Solver->GetGenerateCollisionData() && Solver->GetSolverTime() > 0.f)
					{
						Chaos::FPBDRigidsSolver::FScopedGetEventsData SopedAccess = Solver->ScopedGetEventsData();

						// ----------------------------------------------------------------------------------------------------------
						// GETTING DATA FROM PBDRIGIDSOLVER
						// ----------------------------------------------------------------------------------------------------------
						const Chaos::FPBDRigidsSolver::FAllCollisionDataMaps& AllCollisionData_Maps = SopedAccess.GetAllCollisions_Maps();
						Chaos::FPBDRigidsSolver::FAllCollisionData AllCollisions;
						Chaos::FPBDRigidsSolver::FSolverObjectReverseMapping SolverObjectReverseMapping;
						Chaos::FPBDRigidsSolver::FParticleIndexReverseMapping ParticleIndexReverseMapping;
						Chaos::FPBDRigidsSolver::FAllCollisionsIndicesBySolverObject AllCollisionsIndicesBySolverObject;
						if (AllCollisionData_Maps.IsValid())
						{
							if (AllCollisionData_Maps.AllCollisionData)
							{
								AllCollisions = *AllCollisionData_Maps.AllCollisionData;
							}
							else
							{
								return;
							}
							if (AllCollisionData_Maps.SolverObjectReverseMapping)
							{
								SolverObjectReverseMapping = *AllCollisionData_Maps.SolverObjectReverseMapping;
							}
							else
							{
								return;
							}
							if (AllCollisionData_Maps.ParticleIndexReverseMapping)
							{
								ParticleIndexReverseMapping = *AllCollisionData_Maps.ParticleIndexReverseMapping;
							}
							else
							{
								return;
							}
							if (AllCollisionData_Maps.AllCollisionsIndicesBySolverObject)
							{
								AllCollisionsIndicesBySolverObject = *AllCollisionData_Maps.AllCollisionsIndicesBySolverObject;
							}
							else
							{
								return;
							}

							// ----------------------------------------------------------------------------------------------------------
							// END OF GETTING DATA FROM PBDRIGIDSOLVER
							// ----------------------------------------------------------------------------------------------------------

							int32 NumCollisions = AllCollisions.AllCollisionsArray.Num();
							if (NumCollisions > 0)
							{
								// Get collisions for this GeometryCollectionPhysicsObject from AllCollisions.AllCollisions[]
								if (AllCollisionsIndicesBySolverObject.AllCollisionsIndicesBySolverObjectMap.Contains(GeometryCollectionPhysicsObject))
								{
									TArray<int32> CollisionIndices = AllCollisionsIndicesBySolverObject.AllCollisionsIndicesBySolverObjectMap[GeometryCollectionPhysicsObject];
									for (int32 Idx = 0; Idx < CollisionIndices.Num(); ++Idx)
									{
										RawCollisionDataArray.Add(AllCollisions.AllCollisionsArray[CollisionIndices[Idx]]);
									}
								}
							}
						}
					}
				}
			}

			if (bIsBreakingEventListeningEnabled)
			{
				if (Chaos::FPBDRigidsSolver* Solver = GeometryCollectionPhysicsObject->GetSolver())
				{
					Chaos::FPBDRigidsSolver::FScopedGetEventsData SopedAccess = Solver->ScopedGetEventsData();

					if (Solver->GetGenerateBreakingData() && Solver->GetSolverTime() > 0.f)
					{
						// ----------------------------------------------------------------------------------------------------------
						// GETTING DATA FROM PBDRIGIDSOLVER
						// ----------------------------------------------------------------------------------------------------------
						const Chaos::FPBDRigidsSolver::FAllBreakingDataMaps& AllBreakingData_Maps = SopedAccess.GetAllBreakings_Maps();
						Chaos::FPBDRigidsSolver::FAllBreakingData AllBreakings;
						Chaos::FPBDRigidsSolver::FSolverObjectReverseMapping SolverObjectReverseMapping;
						Chaos::FPBDRigidsSolver::FParticleIndexReverseMapping ParticleIndexReverseMapping;
						Chaos::FPBDRigidsSolver::FAllBreakingsIndicesBySolverObject AllBreakingsIndicesBySolverObject;
						if (AllBreakingData_Maps.IsValid())
						{
							if (AllBreakingData_Maps.AllBreakingData)
							{
								AllBreakings = *AllBreakingData_Maps.AllBreakingData;
							}
							else
							{
								return;
							}
							if (AllBreakingData_Maps.SolverObjectReverseMapping)
							{
								SolverObjectReverseMapping = *AllBreakingData_Maps.SolverObjectReverseMapping;
							}
							else
							{
								return;
							}
							if (AllBreakingData_Maps.ParticleIndexReverseMapping)
							{
								ParticleIndexReverseMapping = *AllBreakingData_Maps.ParticleIndexReverseMapping;
							}
							else
							{
								return;
							}
							if (AllBreakingData_Maps.AllBreakingsIndicesBySolverObject)
							{
								AllBreakingsIndicesBySolverObject = *AllBreakingData_Maps.AllBreakingsIndicesBySolverObject;
							}
							else
							{
								return;
							}

							// ----------------------------------------------------------------------------------------------------------
							// END OF GETTING DATA FROM PBDRIGIDSOLVER
							// ----------------------------------------------------------------------------------------------------------

							int32 NumBreakings = AllBreakings.AllBreakingsArray.Num();
							if (NumBreakings > 0)
							{
								// Get breakings for this GeometryCollectionPhysicsObject from AllBreakings.AllBreakings[]
								if (AllBreakingsIndicesBySolverObject.AllBreakingsIndicesBySolverObjectMap.Contains(GeometryCollectionPhysicsObject))
								{
									TArray<int32> BreakingIndices = AllBreakingsIndicesBySolverObject.AllBreakingsIndicesBySolverObjectMap[GeometryCollectionPhysicsObject];
									for (int32 Idx = 0; Idx < BreakingIndices.Num(); ++Idx)
									{
										RawBreakingDataArray.Add(AllBreakings.AllBreakingsArray[BreakingIndices[Idx]]);
									}
								}
							}
						}
					}
				}
			}

			if (bIsTrailingEventListeningEnabled)
			{
				if (Chaos::FPBDRigidsSolver* Solver = GeometryCollectionPhysicsObject->GetSolver())
				{
					Chaos::FPBDRigidsSolver::FScopedGetEventsData ScopedAccess = Solver->ScopedGetEventsData();

					if (Solver->GetGenerateTrailingData() && Solver->GetSolverTime() > 0.f)
					{
						// ----------------------------------------------------------------------------------------------------------
						// GETTING DATA FROM PBDRIGIDSOLVER
						// ----------------------------------------------------------------------------------------------------------
						const Chaos::FPBDRigidsSolver::FAllTrailingDataMaps& AllTrailingData_Maps = ScopedAccess.GetAllTrailings_Maps();
						Chaos::FPBDRigidsSolver::FAllTrailingData AllTrailings;
						Chaos::FPBDRigidsSolver::FSolverObjectReverseMapping SolverObjectReverseMapping;
						Chaos::FPBDRigidsSolver::FParticleIndexReverseMapping ParticleIndexReverseMapping;
						Chaos::FPBDRigidsSolver::FAllTrailingsIndicesBySolverObject AllTrailingsIndicesBySolverObject;
						if (AllTrailingData_Maps.IsValid())
						{
							if (AllTrailingData_Maps.AllTrailingData)
							{
								AllTrailings = *AllTrailingData_Maps.AllTrailingData;
							}
							else
							{
								return;
							}
							if (AllTrailingData_Maps.SolverObjectReverseMapping)
							{
								SolverObjectReverseMapping = *AllTrailingData_Maps.SolverObjectReverseMapping;
							}
							else
							{
								return;
							}
							if (AllTrailingData_Maps.ParticleIndexReverseMapping)
							{
								ParticleIndexReverseMapping = *AllTrailingData_Maps.ParticleIndexReverseMapping;
							}
							else
							{
								return;
							}
							if (AllTrailingData_Maps.AllTrailingsIndicesBySolverObject)
							{
								AllTrailingsIndicesBySolverObject = *AllTrailingData_Maps.AllTrailingsIndicesBySolverObject;
							}
							else
							{
								return;
							}

							// ----------------------------------------------------------------------------------------------------------
							// END OF GETTING DATA FROM PBDRIGIDSOLVER
							// ----------------------------------------------------------------------------------------------------------

							int32 NumTrailings = AllTrailings.AllTrailingsArray.Num();
							if (NumTrailings > 0)
							{
								// Get trailings for this GeometryCollectionPhysicsObject from AllTrailings.AllTrailings[]
								if (AllTrailingsIndicesBySolverObject.AllTrailingsIndicesBySolverObjectMap.Contains(GeometryCollectionPhysicsObject))
								{
									TArray<int32> TrailingIndices = AllTrailingsIndicesBySolverObject.AllTrailingsIndicesBySolverObjectMap[GeometryCollectionPhysicsObject];
									for (int32 Idx = 0; Idx < TrailingIndices.Num(); ++Idx)
									{
										RawTrailingDataArray.Add(AllTrailings.AllTrailingsArray[TrailingIndices[Idx]]);
									}
								}
							}
						}
					}
				}
			}
		}
	}
#endif
}

#if WITH_EDITOR
void UChaosDestructionListener::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif 

bool UChaosDestructionListener::IsEventListening() const
{
	return  bIsCollisionEventListeningEnabled ||
			bIsBreakingEventListeningEnabled  ||
			bIsTrailingEventListeningEnabled;
}

void UChaosDestructionListener::UpdateTransformSettings()
{
	// Only need to update the transform if anybody is listening at all and if any of the settings are sorting by nearest, otherwise, no need to get updates
	if (IsEventListening())
	{
		bWantsOnUpdateTransform = CollisionEventRequestSettings.SortMethod == EChaosCollisionSortMethod::SortByNearestFirst ||
								  BreakingEventRequestSettings.SortMethod == EChaosBreakingSortMethod::SortByNearestFirst ||
							      TrailingEventRequestSettings.SortMethod == EChaosTrailingSortMethod::SortByNearestFirst;
	}
	else
	{
		bWantsOnUpdateTransform = false;
	}

	bChanged = true;
}

void UChaosDestructionListener::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	bool bIsListening = IsEventListening();

	// if owning actor is disabled, don't listen
	AActor* Owner = GetOwner();
	if (Owner && !Owner->IsActorTickEnabled())
	{
		bIsListening = false;
	}

	// If we have a task and it isn't finished, let it do it's thing
	int32 TaskStateValue = TaskState.GetValue();
	if (TaskStateValue == (int32)ETaskState::Processing)
	{
		return;
	}
	// Note this could be "NoTask" if this is the first tick or if the event listener has been stopped
	else if (TaskStateValue == (int32)ETaskState::Finished)
	{
		// Notify the callbacks with the filtered destruction data results if they're being listened to
		// If the data was changed during the task, then bChanged will be true and we will avoid broadcasting this frame since it won't be valid.
		if (bIsListening && !bChanged)
		{
#if INCLUDE_CHAOS
			if (ChaosCollisionFilter.IsValid())
			{
				if (bIsCollisionEventListeningEnabled && ChaosCollisionFilter->GetNumEvents() > 0 && OnCollisionEvents.IsBound())
				{
					OnCollisionEvents.Broadcast(ChaosCollisionFilter->GetFilteredResults());
				}
			}

			if (ChaosBreakingFilter.IsValid())
			{
				if (bIsBreakingEventListeningEnabled && ChaosBreakingFilter->GetNumEvents() > 0 && OnBreakingEvents.IsBound())
				{
					OnBreakingEvents.Broadcast(ChaosBreakingFilter->GetFilteredResults());
				}
			}

			if (ChaosTrailingFilter.IsValid())
			{
				if (bIsTrailingEventListeningEnabled && ChaosTrailingFilter->GetNumEvents() > 0 && OnTrailingEvents.IsBound())
				{
					OnTrailingEvents.Broadcast(ChaosTrailingFilter->GetFilteredResults());
				}
			}
#endif
		}
		else
		{
			TaskState.Set((int32)ETaskState::NoTask);
		}

		// Reset the changed bool so we can broadcast next tick if the settings haven't changed
		bChanged = false;
	}

	// Early exit if we're not listening anymore
	if (!bIsListening)
	{
		return;
	}

#if INCLUDE_CHAOS
	// If we don't have solvers, call update to make sure we have built our solver array
	if (!Solvers.Num())
	{
		UpdateSolvers();
	}

	if (!GeometryCollectionPhysicsObjects.Num())
	{
		UpdateGeometryCollectionPhysicsObjects();
	}

	// Reset our cached data arrays for various destruction types
	RawCollisionDataArray.Reset();
	RawBreakingDataArray.Reset();
	RawTrailingDataArray.Reset();
#endif

	// Retrieve the raw data arrays from the solvers
	GetDataFromSolvers();

	// Retrieve the raw data arrays from the GeometryCollectionPhysicsObjects
	GetDataFromGeometryCollectionPhysicsObjects();

	TaskState.Set((int32)ETaskState::Processing);

	// Retreive a copy of the transform before kicking off the task
	ChaosComponentTransform = GetComponentTransform();

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,

		[this]()
		{
#if INCLUDE_CHAOS
			if (bIsCollisionEventListeningEnabled)
			{
				if (ChaosCollisionFilter.IsValid())
				{
					ChaosCollisionFilter->FilterEvents(ChaosComponentTransform, RawCollisionDataArray);
				}
			}

			if (ChaosBreakingFilter.IsValid())
			{
				if (bIsBreakingEventListeningEnabled)
				{
					ChaosBreakingFilter->FilterEvents(ChaosComponentTransform, RawBreakingDataArray);
				}
			}

			if (ChaosTrailingFilter.IsValid())
			{
				if (bIsTrailingEventListeningEnabled)
				{
					ChaosTrailingFilter->FilterEvents(ChaosComponentTransform, RawTrailingDataArray);
				}
			}
#endif
			TaskState.Set((int32)ETaskState::Finished);
		});
}

void UChaosDestructionListener::AddChaosSolverActor(AChaosSolverActor* ChaosSolverActor)
{
	if (ChaosSolverActor)
	{
		ChaosSolverActors.Add(ChaosSolverActor);
		UpdateSolvers();
	}
}

void UChaosDestructionListener::RemoveChaosSolverActor(AChaosSolverActor* ChaosSolverActor)
{
	if (ChaosSolverActor)
	{
		ChaosSolverActors.Remove(ChaosSolverActor);
		UpdateSolvers();
	}
}

void UChaosDestructionListener::AddGeometryCollectionActor(AGeometryCollectionActor* GeometryCollectionActor)
{
	if (GeometryCollectionActor)
	{
		GeometryCollectionActors.Add(GeometryCollectionActor);
		UpdateGeometryCollectionPhysicsObjects();
	}
}

void UChaosDestructionListener::RemoveGeometryCollectionActor(AGeometryCollectionActor* GeometryCollectionActor)
{
	if (GeometryCollectionActor)
	{
		GeometryCollectionActors.Remove(GeometryCollectionActor);
		UpdateGeometryCollectionPhysicsObjects();
	}
}

void UChaosDestructionListener::SetCollisionEventRequestSettings(const FChaosCollisionEventRequestSettings& InSettings)
{
	CollisionEventRequestSettings = InSettings;
	UpdateTransformSettings();
}

void UChaosDestructionListener::SetBreakingEventRequestSettings(const FChaosBreakingEventRequestSettings& InSettings)
{
	BreakingEventRequestSettings = InSettings;
	UpdateTransformSettings();
}

void UChaosDestructionListener::SetTrailingEventRequestSettings(const FChaosTrailingEventRequestSettings& InSettings)
{
	TrailingEventRequestSettings = InSettings;
	UpdateTransformSettings();
}

void UChaosDestructionListener::SetCollisionEventEnabled(bool bIsEnabled)
{
	bIsCollisionEventListeningEnabled = bIsEnabled;
	UpdateTransformSettings();
}

void UChaosDestructionListener::SetBreakingEventEnabled(bool bIsEnabled)
{
	bIsBreakingEventListeningEnabled = bIsEnabled;
	UpdateTransformSettings();
}

void UChaosDestructionListener::SetTrailingEventEnabled(bool bIsEnabled)
{
	bIsTrailingEventListeningEnabled = bIsEnabled;
	UpdateTransformSettings();
}

void UChaosDestructionListener::SortCollisionEvents(TArray<FChaosCollisionEventData>& CollisionEvents, EChaosCollisionSortMethod SortMethod)
{
#if INCLUDE_CHAOS
	if (ChaosCollisionFilter.IsValid())
	{
		ChaosCollisionFilter->SortEvents(CollisionEvents, SortMethod, GetComponentTransform());
	}
#endif
}

void UChaosDestructionListener::SortBreakingEvents(TArray<FChaosBreakingEventData>& BreakingEvents, EChaosBreakingSortMethod SortMethod)
{
#if INCLUDE_CHAOS
	if (ChaosBreakingFilter.IsValid())
	{
		ChaosBreakingFilter->SortEvents(BreakingEvents, SortMethod, GetComponentTransform());
	}
#endif
}

void UChaosDestructionListener::SortTrailingEvents(TArray<FChaosTrailingEventData>& TrailingEvents, EChaosTrailingSortMethod SortMethod)
{
#if INCLUDE_CHAOS
	if (ChaosTrailingFilter.IsValid())
	{
		ChaosTrailingFilter->SortEvents(TrailingEvents, SortMethod, GetComponentTransform());
	}
#endif
}
