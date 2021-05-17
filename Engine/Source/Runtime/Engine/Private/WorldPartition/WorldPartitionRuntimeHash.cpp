// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "ActorReferencesUtils.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionHandle.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"
#endif

#define LOCTEXT_NAMESPACE "WorldPartition"

UWorldPartitionRuntimeHash::UWorldPartitionRuntimeHash(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

#if WITH_EDITOR
bool UWorldPartitionRuntimeHash::GenerateRuntimeStreaming(EWorldPartitionStreamingMode Mode, UWorldPartitionStreamingPolicy* Policy, TArray<FString>* OutPackagesToGenerate)
{
	FWorldPartitionReference LoadedWorldDataLayers;
	if (Mode == EWorldPartitionStreamingMode::EditorStandalone)
	{
		// Pre-load AWorldDataLayer as it is necessary for GenerateStreaming to world properly
		UWorldPartition* WorldPartition = GetOuterUWorldPartition();
		for (UActorDescContainer::TConstIterator<> ActorDescIterator(WorldPartition); ActorDescIterator; ++ActorDescIterator)
		{
			if (ActorDescIterator->GetActorClass()->IsChildOf<AWorldDataLayers>())
			{
				if (ensure(!LoadedWorldDataLayers.IsLoaded()))
				{
					LoadedWorldDataLayers = FWorldPartitionReference(WorldPartition, ActorDescIterator->GetGuid());
				}
			}
		}
	}

	return GenerateStreaming(Mode, Policy, OutPackagesToGenerate);
}

void UWorldPartitionRuntimeHash::OnBeginPlay(EWorldPartitionStreamingMode Mode)
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

void UWorldPartitionRuntimeHash::ForceExternalActorLevelReference(bool bForceExternalActorLevelReferenceForPIE)
{
	if (!IsRunningGame())
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

void UWorldPartitionRuntimeHash::CreateActorDescViewMap(const UActorDescContainer* Container, TMap<FGuid, FWorldPartitionActorDescView>& OutActorDescViewMap) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateActorDescViewMap);

	// Build the actor desc view map
	OutActorDescViewMap.Empty();

	const bool bIsPIE = GetOuterUWorldPartition()->bIsPIE;

	for (UActorDescContainer::TConstIterator<> ActorDescIt(Container); ActorDescIt; ++ActorDescIt)
	{
		if (bIsPIE)
		{
			if (AActor* Actor = ActorDescIt->GetActor())
			{
				// Don't include deleted, unsaved actors
				if (Actor->IsPendingKill())
				{
					continue;
				}

				// Handle dirty, unsaved actors
				if (Actor->GetPackage()->IsDirty())
				{
					FWorldPartitionActorDesc* ActorDesc = ModifiedActorDescListForPIE.AddActor(Actor);
					OutActorDescViewMap.Emplace(ActorDesc->GetGuid(), ActorDesc);
					continue;
				}
			}
		}

		// Normal, non-dirty actors
		OutActorDescViewMap.Emplace(ActorDescIt->GetGuid(), *ActorDescIt);
	}

	if (bIsPIE)
	{
		// Append new unsaved actors for the persistent level
		if (Container->GetContainerPackage() == GetWorld()->PersistentLevel->GetPackage()->GetLoadedPath().GetPackageFName())
		{
			for (AActor* Actor : GetWorld()->PersistentLevel->Actors)
			{
				if (Actor && 
					Actor->IsPackageExternal() && 
					!Actor->IsPendingKill() && 
					!Container->GetActorDesc(Actor->GetActorGuid()) && // Actor not on disk yet so not found in container
					Actor->IsMainPackageActor())
				{
					FWorldPartitionActorDesc* ActorDesc = ModifiedActorDescListForPIE.AddActor(Actor);
					OutActorDescViewMap.Emplace(ActorDesc->GetGuid(), ActorDesc);
				}
			}
		}
	}

	// Gather all references to external actors from the level script and make them always loaded
	if (ULevelScriptBlueprint* LevelScriptBlueprint = Container->GetWorld()->PersistentLevel->GetLevelScriptBlueprint(true))
	{
		TArray<AActor*> LevelScriptExternalActorReferences = ActorsReferencesUtils::GetExternalActorReferences(LevelScriptBlueprint);

		for (AActor* Actor : LevelScriptExternalActorReferences)
		{
			if (FWorldPartitionActorDescView* ActorDescView = OutActorDescViewMap.Find(Actor->GetActorGuid()))
			{
				ChangeActorDescViewGridPlacement(*ActorDescView, EActorGridPlacement::AlwaysLoaded);
			}
		}
	}
}

void UWorldPartitionRuntimeHash::ChangeActorDescViewGridPlacement(FWorldPartitionActorDescView& ActorDescView, EActorGridPlacement GridPlacement) const
{
	if (ActorDescView.EffectiveGridPlacement != GridPlacement)
	{
		ActorDescView.EffectiveGridPlacement = GridPlacement;
		UE_LOG(LogWorldPartition, Verbose, TEXT("Actor '%s' grid placement changed to %s"), *ActorDescView.GetActorLabel().ToString(), *StaticEnum<EActorGridPlacement>()->GetNameStringByValue((int64)GridPlacement));
	}
}

void UWorldPartitionRuntimeHash::CheckForErrors() const
{
	TMap<FGuid, FWorldPartitionActorViewProxy> ActorDescList;
	for (UActorDescContainer::TConstIterator<> ActorDescIt(GetOuterUWorldPartition()); ActorDescIt; ++ActorDescIt)
	{
		ActorDescList.Emplace(ActorDescIt->GetGuid(), *ActorDescIt);
	}

	CheckForErrorsInternal(ActorDescList);
}

void UWorldPartitionRuntimeHash::CheckForErrorsInternal(const TMap<FGuid, FWorldPartitionActorViewProxy>& ActorDescList) const
{
	auto GetActorLabel = [](const FWorldPartitionActorDescView& ActorDescView) -> FString
	{
		const FName ActorLabel = ActorDescView.GetActorLabel();
		if (!ActorLabel.IsNone())
		{
			return ActorLabel.ToString();
		}

		const FString ActorPath = ActorDescView.GetActorPath().ToString();

		FString SubObjectName;
		FString SubObjectContext;
		if (FString(ActorPath).Split(TEXT("."), &SubObjectContext, &SubObjectName))
		{
			return SubObjectName;
		}

		return ActorPath;
	};

	for (auto& ActorDescListPair: ActorDescList)
	{
		const FWorldPartitionActorViewProxy& ActorDescView = ActorDescListPair.Value;

		if (!ActorDescView.GetActorIsEditorOnly())
		{
			for (const FGuid ActorDescRefGuid : ActorDescView.GetReferences())
			{
				if (const FWorldPartitionActorViewProxy* ActorDescRefView = ActorDescList.Find(ActorDescRefGuid))
				{
					const bool bIsActorDescAlwaysLoaded = ActorDescView.GetGridPlacement() == EActorGridPlacement::AlwaysLoaded;
					const bool bIsActorDescRefAlwaysLoaded = ActorDescRefView->GetGridPlacement() == EActorGridPlacement::AlwaysLoaded;

					if (bIsActorDescAlwaysLoaded != bIsActorDescRefAlwaysLoaded)
					{
						const FText StreamedActor(LOCTEXT("MapCheck_WorldPartition_StreamedActor", "Streamed actor"));
						const FText AlwaysLoadedActor(LOCTEXT("MapCheck_WorldPartition_AlwaysLoadedActor", "Always loaded actor"));

						TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Warning()
							->AddToken(FTextToken::Create(bIsActorDescAlwaysLoaded ? AlwaysLoadedActor : StreamedActor))
							->AddToken(FAssetNameToken::Create(GetActorLabel(ActorDescView)))
							->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_References", "references")))
							->AddToken(FTextToken::Create(bIsActorDescRefAlwaysLoaded ? AlwaysLoadedActor : StreamedActor)								)
							->AddToken(FAssetNameToken::Create(GetActorLabel(*ActorDescRefView)))
							->AddToken(FMapErrorToken::Create(FName(TEXT("WorldPartition_StreamedActorReferenceAlwaysLoadedActor_CheckForErrors"))));
					}

					TArray<FName> ActorDescLayers = ActorDescView.GetDataLayers();
					ActorDescLayers.Sort([](const FName& A, const FName& B) { return A.FastLess(B); });

					TArray<FName> ActorDescRefLayers = ActorDescRefView->GetDataLayers();
					ActorDescRefLayers.Sort([](const FName& A, const FName& B) { return A.FastLess(B); });

					if (ActorDescLayers != ActorDescRefLayers)
					{
						TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Error()
							->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_Actor", "Actor")))
							->AddToken(FAssetNameToken::Create(GetActorLabel(ActorDescView)))
							->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_ReferenceActorInOtherDataLayers", "references an actor in a different set of data layers")))
							->AddToken(FAssetNameToken::Create(GetActorLabel(*ActorDescRefView)))
							->AddToken(FMapErrorToken::Create(FName(TEXT("WorldPartition_ActorReferenceActorInAnotherDataLayer_CheckForErrors"))));
					}
				}
				else
				{
					TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Warning()
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_Actor", "Actor")))
						->AddToken(FAssetNameToken::Create(GetActorLabel(ActorDescView)))
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_HaveMissingRefsTo", "have missing references to")))
						->AddToken(FTextToken::Create(FText::FromString(ActorDescRefGuid.ToString())))
						->AddToken(FMapErrorToken::Create(FName(TEXT("WorldPartition_MissingActorReference_CheckForErrors"))));
				}
			}
		}
	}

	// Check Level Script Blueprint
	if (ULevelScriptBlueprint* LevelScriptBlueprint = GetOuterUWorldPartition()->GetWorld()->PersistentLevel->GetLevelScriptBlueprint(true))
	{
		TArray<AActor*> LevelScriptExternalActorReferences = ActorsReferencesUtils::GetExternalActorReferences(LevelScriptBlueprint);

		for (AActor* Actor : LevelScriptExternalActorReferences)
		{
			if (const FWorldPartitionActorDescView* ActorDescView = ActorDescList.Find(Actor->GetActorGuid()))
			{
				TArray<FName> ActorDescLayers = ActorDescView->GetDataLayers();
				if (ActorDescLayers.Num())
				{
					TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Error()
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_LevelScriptBlueprintActorReference", "Level Script Blueprint references actor")))
						->AddToken(FAssetNameToken::Create(GetActorLabel(*ActorDescView)))
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_LevelScriptBlueprintDataLayerReference", "with a non empty set of data layers")))
						->AddToken(FMapErrorToken::Create(FName(TEXT("WorldPartition_LevelScriptBlueprintRefefenceDataLayer_CheckForErrors"))));
				}
			}
		}
	}
}
#endif

void UWorldPartitionRuntimeHash::SortStreamingCellsByImportance(const TSet<const UWorldPartitionRuntimeCell*>& InCells, const TArray<FWorldPartitionStreamingSource>& InSources, TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>>& OutSortedCells) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeHash::SortStreamingCellsByImportance);

	OutSortedCells = InCells.Array();
	Algo::Sort(OutSortedCells, [](const UWorldPartitionRuntimeCell* CellA, const UWorldPartitionRuntimeCell* CellB)
	{
		return CellA->SortCompare(CellB) < 0;
	});
}

void UWorldPartitionRuntimeHash::FStreamingSourceCells::AddCell(const UWorldPartitionRuntimeCell* InCell, const FWorldPartitionStreamingSource& InSource)
{
	InCell->CacheStreamingSourceInfo(InSource);
	Cells.Add(InCell);
}

#undef LOCTEXT_NAMESPACE