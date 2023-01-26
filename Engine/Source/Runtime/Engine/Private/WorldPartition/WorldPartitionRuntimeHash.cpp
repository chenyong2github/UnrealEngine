// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "Engine/Level.h"
#include "Misc/HierarchicalLogArchive.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#if WITH_EDITOR
#include "WorldPartition/Cook/WorldPartitionCookPackage.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldPartitionRuntimeHash)

#define LOCTEXT_NAMESPACE "WorldPartition"

void URuntimeHashExternalStreamingObjectBase::ForEachStreamingCells(TFunctionRef<void(UWorldPartitionRuntimeCell&)> Func)
{
	TArray<UObject*> Objects;
	GetObjectsWithOuter(this, Objects);

	for (UObject* Object : Objects)
	{
		if (UWorldPartitionRuntimeCell* Cell = Cast<UWorldPartitionRuntimeCell>(Object))
		{
			Func(*Cell);
		}
	}
}

UWorldPartitionRuntimeHash::UWorldPartitionRuntimeHash(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

#if WITH_EDITOR
void UWorldPartitionRuntimeHash::OnBeginPlay()
{
	// Mark always loaded actors so that the Level will force reference to these actors for PIE.
	// These actor will then be duplicated for PIE during the PIE world duplication process
	ForceExternalActorLevelReference(/*bForceExternalActorLevelReferenceForPIE*/true);
}

void UWorldPartitionRuntimeHash::OnEndPlay()
{
	// Unmark always loaded actors
	ForceExternalActorLevelReference(/*bForceExternalActorLevelReferenceForPIE*/false);

	// Release references (will unload actors that were not already loaded in the Editor)
	AlwaysLoadedActorsForPIE.Empty();

	ModifiedActorDescListForPIE.Empty();
}

bool UWorldPartitionRuntimeHash::GenerateStreaming(class UWorldPartitionStreamingPolicy* StreamingPolicy, const IStreamingGenerationContext* StreamingGenerationContext, TArray<FString>* OutPackagesToGenerate)
{
	return PackagesToGenerateForCook.IsEmpty();
}

void UWorldPartitionRuntimeHash::FlushStreaming()
{
	PackagesToGenerateForCook.Empty();
}

// In PIE, Always loaded cell is not generated. Instead, always loaded actors will be added to AlwaysLoadedActorsForPIE.
// This will trigger loading/registration of these actors in the PersistentLevel (if not already loaded).
// Then, duplication of world for PIE will duplicate only these actors. 
// When stopping PIE, WorldPartition will release these FWorldPartitionReferences which 
// will unload actors that were not already loaded in the non PIE world.
bool UWorldPartitionRuntimeHash::ConditionalRegisterAlwaysLoadedActorsForPIE(const IStreamingGenerationContext::FActorSetInstance* ActorSetInstance, bool bIsMainWorldPartition, bool bIsMainContainer, bool bIsCellAlwaysLoaded)
{
	if (bIsMainWorldPartition && bIsMainContainer && bIsCellAlwaysLoaded && !IsRunningCookCommandlet())
	{
		for (const FGuid& ActorGuid : ActorSetInstance->ActorSet->Actors)
		{
			IStreamingGenerationContext::FActorInstance ActorInstance(ActorGuid, ActorSetInstance);
			const FWorldPartitionActorDescView& ActorDescView = ActorInstance.GetActorDescView();

			// This will load the actor if it isn't already loaded
			FWorldPartitionReference Reference(GetOuterUWorldPartition(), ActorDescView.GetGuid());

			if (AActor* AlwaysLoadedActor = FindObject<AActor>(nullptr, *ActorDescView.GetActorSoftPath().ToString()))
			{
				AlwaysLoadedActorsForPIE.Emplace(Reference, AlwaysLoadedActor);

				// Handle child actors
				AlwaysLoadedActor->ForEachComponent<UChildActorComponent>(true, [this, &Reference](UChildActorComponent* ChildActorComponent)
				{
					if (AActor* ChildActor = ChildActorComponent->GetChildActor())
					{
						AlwaysLoadedActorsForPIE.Emplace(Reference, ChildActor);
					}
				});
			}
		}

		return true;
	}

	return false;
}

void UWorldPartitionRuntimeHash::PopulateRuntimeCell(UWorldPartitionRuntimeCell* RuntimeCell, const TArray<IStreamingGenerationContext::FActorInstance>& ActorInstances, TArray<FString>* OutPackagesToGenerate)
{
	for (const IStreamingGenerationContext::FActorInstance& ActorInstance : ActorInstances)
	{
		if (ActorInstance.GetContainerID().IsMainContainer())
		{
			const FWorldPartitionActorDescView& ActorDescView = ActorInstance.GetActorDescView();
			if (AActor* Actor = FindObject<AActor>(nullptr, *ActorDescView.GetActorSoftPath().ToString()))
			{
				if (ModifiedActorDescListForPIE.GetActorDesc(ActorDescView.GetGuid()))
				{
					// Create an actor container to make sure duplicated actors will share an outer to properly remap inter-actors references
					RuntimeCell->UnsavedActorsContainer = NewObject<UActorContainer>(RuntimeCell);
					break;
				}
			}
		}
	}

	FBox CellContentBounds(ForceInit);
	for (const IStreamingGenerationContext::FActorInstance& ActorInstance : ActorInstances)
	{
		const FWorldPartitionActorDescView& ActorDescView = ActorInstance.GetActorDescView();
		RuntimeCell->AddActorToCell(ActorDescView, ActorInstance.GetContainerID(), ActorInstance.GetTransform(), ActorInstance.GetActorDescContainer());
		CellContentBounds += ActorDescView.GetRuntimeBounds().TransformBy(ActorInstance.GetTransform());
					
		if (ActorInstance.GetContainerID().IsMainContainer() && RuntimeCell->UnsavedActorsContainer)
		{
			if (AActor* Actor = FindObject<AActor>(nullptr, *ActorDescView.GetActorSoftPath().ToString()))
			{
				RuntimeCell->UnsavedActorsContainer->Actors.Add(Actor->GetFName(), Actor);

				// Handle child actors
				Actor->ForEachComponent<UChildActorComponent>(true, [RuntimeCell](UChildActorComponent* ChildActorComponent)
				{
					if (AActor* ChildActor = ChildActorComponent->GetChildActor())
					{
						RuntimeCell->UnsavedActorsContainer->Actors.Add(ChildActor->GetFName(), ChildActor);
					}
				});
			}
		}
	}

	RuntimeCell->RuntimeCellData->ContentBounds = CellContentBounds;

	// Always loaded cell actors are transfered to World's Persistent Level (see UWorldPartitionRuntimeSpatialHash::PopulateGeneratorPackageForCook)
	if (OutPackagesToGenerate && RuntimeCell->GetActorCount() && !RuntimeCell->IsAlwaysLoaded())
	{
		const FString PackageRelativePath = RuntimeCell->GetPackageNameToCreate();
		check(!PackageRelativePath.IsEmpty());

		OutPackagesToGenerate->Add(PackageRelativePath);

		// Map relative package to StreamingCell for PopulateGeneratedPackageForCook/PopulateGeneratorPackageForCook/GetCellForPackage
		PackagesToGenerateForCook.Add(PackageRelativePath, RuntimeCell);

		UE_CLOG(IsRunningCookCommandlet(), LogWorldPartition, Log, TEXT("Creating runtime streaming cells %s."), *RuntimeCell->GetName());
	}
}

bool UWorldPartitionRuntimeHash::PopulateGeneratedPackageForCook(const FWorldPartitionCookPackage& InPackagesToCook, TArray<UPackage*>& OutModifiedPackages)
{
	OutModifiedPackages.Reset();
	if (UWorldPartitionRuntimeCell** MatchingCell = PackagesToGenerateForCook.Find(InPackagesToCook.RelativePath))
	{
		UWorldPartitionRuntimeCell* Cell = *MatchingCell;
		if (ensure(Cell))
		{
			return Cell->PopulateGeneratedPackageForCook(InPackagesToCook.GetPackage(), OutModifiedPackages);
		}
	}
	return false;
}

UWorldPartitionRuntimeCell* UWorldPartitionRuntimeHash::GetCellForPackage(const FWorldPartitionCookPackage& PackageToCook) const
{
	UWorldPartitionRuntimeCell** MatchingCell = const_cast<UWorldPartitionRuntimeCell**>(PackagesToGenerateForCook.Find(PackageToCook.RelativePath));
	return MatchingCell ? *MatchingCell : nullptr;
}

TArray<UWorldPartitionRuntimeCell*> UWorldPartitionRuntimeHash::GetAlwaysLoadedCells() const
{
	TArray<UWorldPartitionRuntimeCell*> Result;
	ForEachStreamingCells([&Result](const UWorldPartitionRuntimeCell* Cell)
	{
		if (Cell->IsAlwaysLoaded())
		{
			Result.Add(const_cast<UWorldPartitionRuntimeCell*>(Cell));
		}
		return true;
	});
	return Result;
}

bool UWorldPartitionRuntimeHash::PrepareGeneratorPackageForCook(TArray<UPackage*>& OutModifiedPackages)
{
	check(IsRunningCookCommandlet());

	for (UWorldPartitionRuntimeCell* Cell : GetAlwaysLoadedCells())
	{
		check(Cell->IsAlwaysLoaded());
		if (!Cell->PopulateGeneratorPackageForCook(OutModifiedPackages))
		{
			return false;
		}
	}

	//@todo_ow: here we can safely remove always loaded cells as they are not part of the OutPackagesToGenerate
	return true;
}

bool UWorldPartitionRuntimeHash::PopulateGeneratorPackageForCook(const TArray<FWorldPartitionCookPackage*>& InPackagesToCook, TArray<UPackage*>& OutModifiedPackages)
{
	check(IsRunningCookCommandlet());

	for (const FWorldPartitionCookPackage* CookPackage : InPackagesToCook)
	{
		UWorldPartitionRuntimeCell** MatchingCell = PackagesToGenerateForCook.Find(CookPackage->RelativePath);
		UWorldPartitionRuntimeCell* Cell = MatchingCell ? *MatchingCell : nullptr;
		if (!Cell || !Cell->PrepareCellForCook(CookPackage->GetPackage()))
		{
			return false;
		}
	}
	return true;
}

void UWorldPartitionRuntimeHash::DumpStateLog(FHierarchicalLogArchive& Ar)
{
	Ar.Printf(TEXT("----------------------------------------------------------------------------------------------------------------"));
	Ar.Printf(TEXT("%s - Persistent Level"), *GetWorld()->GetName());
	Ar.Printf(TEXT("----------------------------------------------------------------------------------------------------------------"));
	Ar.Printf(TEXT("Always loaded Actor Count: %d "), GetWorld()->PersistentLevel->Actors.Num());
	Ar.Printf(TEXT(""));
}

void UWorldPartitionRuntimeHash::ForceExternalActorLevelReference(bool bForceExternalActorLevelReferenceForPIE)
{
	// Do this only on non game worlds prior to PIE so that always loaded actors get duplicated with the world
	if (!GetWorld()->IsGameWorld())
	{
		for (const FAlwaysLoadedActorForPIE& AlwaysLoadedActor : AlwaysLoadedActorsForPIE)
		{
			if (AActor* Actor = AlwaysLoadedActor.Actor)
			{
				Actor->SetForceExternalActorLevelReferenceForPIE(bForceExternalActorLevelReferenceForPIE);
			}
		}
	}
}
#endif

int32 UWorldPartitionRuntimeHash::GetAllStreamingCells(TSet<const UWorldPartitionRuntimeCell*>& Cells, bool bAllDataLayers, bool bDataLayersOnly, const TSet<FName>& InDataLayers) const
{
	ForEachStreamingCells([&Cells, bAllDataLayers, bDataLayersOnly, InDataLayers](const UWorldPartitionRuntimeCell* Cell)
	{
		if (!bDataLayersOnly && !Cell->HasDataLayers())
		{
			Cells.Add(Cell);
		}
		else if (Cell->HasDataLayers() && (bAllDataLayers || Cell->HasAnyDataLayer(InDataLayers)))
		{
			Cells.Add(Cell);
		}
		return true;
	});

	return Cells.Num();
}

bool UWorldPartitionRuntimeHash::GetStreamingCells(const FWorldPartitionStreamingQuerySource& QuerySource, TSet<const UWorldPartitionRuntimeCell*>& OutCells) const
{
	ForEachStreamingCellsQuery(QuerySource, [QuerySource, &OutCells](const UWorldPartitionRuntimeCell* Cell)
	{
		OutCells.Add(Cell);
		return true;
	});

	return !!OutCells.Num();
}

bool UWorldPartitionRuntimeHash::GetStreamingCells(const TArray<FWorldPartitionStreamingSource>& Sources, FStreamingSourceCells& OutActivateCells, FStreamingSourceCells& OutLoadCells) const
{
	ForEachStreamingCellsSources(Sources, [&OutActivateCells, &OutLoadCells](const UWorldPartitionRuntimeCell* Cell, EStreamingSourceTargetState TargetState)
	{
		switch (TargetState)
		{
		case EStreamingSourceTargetState::Loaded:
			OutLoadCells.GetCells().Add(Cell);
			break;
		case EStreamingSourceTargetState::Activated:
			OutActivateCells.GetCells().Add(Cell);
			break;
		}
		return true;
	});

	return !!(OutActivateCells.Num() + OutLoadCells.Num());
}

bool UWorldPartitionRuntimeHash::IsCellRelevantFor(bool bClientOnlyVisible) const
{
	if (bClientOnlyVisible)
	{
		const UWorld* World = GetWorld();
		if (World->IsGameWorld())
		{
			// Dedicated server & listen server without server streaming won't consider client-only visible cells
			const ENetMode NetMode = World->GetNetMode();
			if ((NetMode == NM_DedicatedServer) || ((NetMode == NM_ListenServer) && !GetOuterUWorldPartition()->IsServerStreamingEnabled()))
			{
				return false;
			}
		}
	}
	return true;
}

EWorldPartitionStreamingPerformance UWorldPartitionRuntimeHash::GetStreamingPerformance(const TSet<const UWorldPartitionRuntimeCell*>& CellsToActivate) const
{
	EWorldPartitionStreamingPerformance StreamingPerformance = EWorldPartitionStreamingPerformance::Good;

	if (!CellsToActivate.IsEmpty() && GetWorld()->bMatchStarted)
	{
		UWorld* World = GetWorld();

		for (const UWorldPartitionRuntimeCell* Cell : CellsToActivate)
		{
			if (Cell->GetBlockOnSlowLoading() && !Cell->IsAlwaysLoaded() && Cell->GetStreamingStatus() != LEVEL_Visible)
			{
				EWorldPartitionStreamingPerformance CellPerformance = GetStreamingPerformanceForCell(Cell);
				// Cell Performance is worst than previous cell performance
				if (CellPerformance > StreamingPerformance)
				{
					StreamingPerformance = CellPerformance;
					// Early out performance is critical
					if (StreamingPerformance == EWorldPartitionStreamingPerformance::Critical)
					{
						return StreamingPerformance;
					}
				}
			}
		}
	}

	return StreamingPerformance;
}

void UWorldPartitionRuntimeHash::FStreamingSourceCells::AddCell(const UWorldPartitionRuntimeCell* Cell, const FWorldPartitionStreamingSource& Source, const FSphericalSector& SourceShape)
{
	if (Cell->ShouldResetStreamingSourceInfo())
	{
		Cell->ResetStreamingSourceInfo();
	}

	Cell->AppendStreamingSourceInfo(Source, SourceShape);
	Cells.Add(Cell);
}

void FWorldPartitionQueryCache::AddCellInfo(const UWorldPartitionRuntimeCell* Cell, const FSphericalSector& SourceShape)
{
	const double SquareDistance = FVector::DistSquared2D(SourceShape.GetCenter(), Cell->GetContentBounds().GetCenter());
	if (double* ExistingSquareDistance = CellToSourceMinSqrDistances.Find(Cell))
	{
		*ExistingSquareDistance = FMath::Min(*ExistingSquareDistance, SquareDistance);
	}
	else
	{
		CellToSourceMinSqrDistances.Add(Cell, SquareDistance);
	}
}

double FWorldPartitionQueryCache::GetCellMinSquareDist(const UWorldPartitionRuntimeCell* Cell) const
{
	const double* Dist = CellToSourceMinSqrDistances.Find(Cell);
	return Dist ? *Dist : MAX_dbl;
}

#undef LOCTEXT_NAMESPACE
