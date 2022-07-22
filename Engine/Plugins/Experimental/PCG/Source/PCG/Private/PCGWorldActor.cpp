// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGWorldActor.h"

#include "PCGComponent.h"
#include "PCGSubsystem.h"
#include "Grid/PCGPartitionActor.h"
#include "Helpers/PCGActorHelpers.h"

#include "Engine/World.h"

APCGWorldActor::APCGWorldActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
	bDefaultOutlinerExpansionState = false;
#endif

	PartitionGridSize = DefaultPartitionGridSize;
	LandscapeCache.SetOwner(this);
}

void APCGWorldActor::Serialize(FArchive& InArchive)
{
	if (InArchive.IsSaving() && !InArchive.IsCooking() && !(GetWorld() && GetWorld()->GetWorldPartition()))
	{
		LandscapeCache.ClearCache();
	}

	Super::Serialize(InArchive);
}

#if WITH_EDITOR
void APCGWorldActor::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	Super::BeginCacheForCookedPlatformData(TargetPlatform);
	LandscapeCache.PrimeCache();
}
#endif

void APCGWorldActor::PostLoad()
{
	Super::PostLoad();
	LandscapeCache.SetOwner(this, /*bUpdateCachedNames=*/false);
	RegisterToSubsystem();
}

void APCGWorldActor::BeginDestroy()
{
	UnregisterFromSubsystem();
	Super::BeginDestroy();
}

#if WITH_EDITOR
APCGWorldActor* APCGWorldActor::CreatePCGWorldActor(UWorld* InWorld)
{
	check(InWorld);
	APCGWorldActor* PCGActor = InWorld->SpawnActor<APCGWorldActor>();
	PCGActor->RegisterToSubsystem();

	return PCGActor;
}
#endif

void APCGWorldActor::RegisterToSubsystem()
{
	UPCGSubsystem* PCGSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UPCGSubsystem>() : nullptr;
	if (PCGSubsystem)
	{
		PCGSubsystem->RegisterPCGWorldActor(this);
	}
}

void APCGWorldActor::UnregisterFromSubsystem()
{
	UPCGSubsystem* PCGSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UPCGSubsystem>() : nullptr;
	if (PCGSubsystem)
	{
		PCGSubsystem->UnregisterPCGWorldActor(this);
	}
}

#if WITH_EDITOR
void APCGWorldActor::OnPartitionGridSizeChanged()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(APCGWorldActor::OnPartitionGridSizeChanged);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UPCGSubsystem* PCGSubsystem = World->GetSubsystem<UPCGSubsystem>();
	ULevel* Level = World->GetCurrentLevel();
	if (!PCGSubsystem || !Level)
	{
		return;
	}

	// First gather all the PCG components that linked to PCGPartitionActor
	TSet<TObjectPtr<UPCGComponent>> AllPartitionedComponents;
	
	bool bAllSafeToDelete = true;

	auto AddPartitionComponentAndCheckIfSafeToDelete = [&AllPartitionedComponents, &bAllSafeToDelete](AActor* Actor) -> bool
	{
		TObjectPtr<APCGPartitionActor> PartitionActor = CastChecked<APCGPartitionActor>(Actor);

		if (!PartitionActor->IsSafeForDeletion())
		{
			bAllSafeToDelete = false;
			return true;
		}

		for (UPCGComponent* PCGComponent : PartitionActor->GetAllOriginalPCGComponents())
		{
			if (PCGComponent)
			{
				AllPartitionedComponents.Add(PCGComponent);
			}
		}

		return true;
	};

	UPCGActorHelpers::ForEachActorInLevel<APCGPartitionActor>(Level, AddPartitionComponentAndCheckIfSafeToDelete);

	// TODO: When we have the capability to stop the generation, we should just do that
	// For now, just throw an error
	if (!bAllSafeToDelete)
	{
		UE_LOG(LogPCG, Error, TEXT("Trying to change the partition grid size while there are partitionned PCGComponents that are refreshing. We cannot stop the refresh for now, so we abort there. You should delete your partition actors manually and regenerate when the refresh is done"));
		return;
	}

	// Then delete all PCGPartitionActors
	PCGSubsystem->DeletePartitionActors();

	// And finally, refresh all components
	for (TObjectPtr<UPCGComponent> PCGComponent : AllPartitionedComponents)
	{
		PCGComponent->DirtyGenerated();
		PCGComponent->Refresh();
	}
}

void APCGWorldActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(APCGWorldActor, PartitionGridSize) 
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(APCGWorldActor, bUse2DGrid))
	{
		OnPartitionGridSizeChanged();
	}
}
#endif // WITH_EDITOR