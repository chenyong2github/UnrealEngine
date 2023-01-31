// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionISMPoolSubSystem.h"
#include "GeometryCollection/GeometryCollectionISMPoolActor.h"
#include "Engine/World.h"

//----------------------------------------------------------------------//
// UGeometryCollectionISMPoolSubSystem
//----------------------------------------------------------------------//
UGeometryCollectionISMPoolSubSystem::UGeometryCollectionISMPoolSubSystem()
	: ISMPoolActor(nullptr)
{
}

void UGeometryCollectionISMPoolSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGeometryCollectionISMPoolSubSystem>();
}

void UGeometryCollectionISMPoolSubSystem::Deinitialize()
{
	if (ISMPoolActor)
	{
		if (UWorld* World = GetWorld())
		{
			GetWorld()->DestroyActor(ISMPoolActor);
		}
	}
	Super::Deinitialize();
}

void UGeometryCollectionISMPoolSubSystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
}

AGeometryCollectionISMPoolActor* UGeometryCollectionISMPoolSubSystem::FindISMPoolActor(const USceneComponent& Requester)
{
	// on demand creation of the actor 
	// very simple logic for now, we can extend that in the future to return a specific actor based on the criteria
	if (!ISMPoolActor)
	{
		// we keep it as transient to avoid accumulation of those actors in saved levels
		FActorSpawnParameters Params;
		Params.ObjectFlags = EObjectFlags::RF_DuplicateTransient | EObjectFlags::RF_Transient;
		ISMPoolActor = GetWorld()->SpawnActor<AGeometryCollectionISMPoolActor>(Params);
	}
	return ISMPoolActor;
}


