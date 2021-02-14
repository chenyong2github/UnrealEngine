// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldDataLayers.cpp: AWorldDataLayers class implementation
=============================================================================*/

#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#endif

#define LOCTEXT_NAMESPACE "WorldDataLayers"

AWorldDataLayers::AWorldDataLayers(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

const AWorldDataLayers* AWorldDataLayers::Get(UWorld* World)
{
	if (World)
	{
		// Prepare flags for actor iterator. Don't use default Flags because it uses EActorIteratorFlags::OnlyActiveLevels 
		// which will make this code return no actor when cooking (because world is not initialized)
		EActorIteratorFlags Flags = EActorIteratorFlags::SkipPendingKill;
		if (!IsRunningCookCommandlet())
		{
			Flags |= EActorIteratorFlags::OnlyActiveLevels;
		}
		for (AWorldDataLayers* Actor : TActorRange<AWorldDataLayers>(World, AWorldDataLayers::StaticClass(), Flags))
		{
			if (Actor)
			{
				check(!Actor->IsPendingKill());
				return Actor;
			}
		}
	}
	return nullptr;
}

#if WITH_EDITOR
AWorldDataLayers* AWorldDataLayers::Get(UWorld* World, bool bCreateIfNotFound)
{
	if (bCreateIfNotFound)
	{
		AWorldDataLayers* WorldDataLayers = nullptr;
		static FName WorldDataLayersName = AWorldDataLayers::StaticClass()->GetFName();
		if (UObject* ExistingObject = StaticFindObject(nullptr, World->PersistentLevel, *WorldDataLayersName.ToString()))
		{
			WorldDataLayers = CastChecked<AWorldDataLayers>(ExistingObject);
			if (WorldDataLayers->IsPendingKill())
			{
				// Handle the case where the actor already exists, but it's pending kill
				WorldDataLayers->Rename(nullptr, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional | REN_ForceNoResetLoaders);
				WorldDataLayers = nullptr;
			}
		}

		if (!WorldDataLayers)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = World->PersistentLevel;
			SpawnParams.bHideFromSceneOutliner = true;
			SpawnParams.Name = WorldDataLayersName;
			SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_Fatal;
			WorldDataLayers = World->SpawnActor<AWorldDataLayers>(AWorldDataLayers::StaticClass(), SpawnParams);
		}
		return WorldDataLayers;
	}
	else
	{
		return const_cast<AWorldDataLayers*>(Get(World));
	}
}

FName AWorldDataLayers::GenerateUniqueDataLayerLabel(const FName& InDataLayerLabel) const
{
	int32 DataLayerIndex = 0;
	FName UniqueNewDataLayerLabel = InDataLayerLabel;
	while (GetDataLayerFromLabel(UniqueNewDataLayerLabel))
	{
		UniqueNewDataLayerLabel = FName(*FString::Printf(TEXT("%s%d"), *InDataLayerLabel.ToString(), ++DataLayerIndex));
	};
	return UniqueNewDataLayerLabel;
}

TArray<FName> AWorldDataLayers::GetDataLayerNames(const TArray<FActorDataLayer>& InDataLayers) const
{
	TArray<FName> OutDataLayerNames;
	OutDataLayerNames.Reserve(DataLayers.Num());

	for (const UDataLayer* DataLayer : GetDataLayerObjects(InDataLayers))
	{
		OutDataLayerNames.Add(DataLayer->GetFName());
	}

	return OutDataLayerNames;
}

TArray<const UDataLayer*> AWorldDataLayers::GetDataLayerObjects(const TArray<FActorDataLayer>& InDataLayers) const
{
	TArray<const UDataLayer*> OutDataLayers;
	OutDataLayers.Reserve(DataLayers.Num());

	for (const FActorDataLayer& DataLayer : InDataLayers)
	{
		if (const UDataLayer* DataLayerObject = GetDataLayerFromName(DataLayer.Name))
		{
			OutDataLayers.AddUnique(DataLayerObject);
		}
	}

	return OutDataLayers;
}

UDataLayer* AWorldDataLayers::CreateDataLayer(FName InName, EObjectFlags InObjectFlags)
{
	Modify();

	// Make sure new DataLayer name (not label) is unique and never re-used so that actors still referencing on deleted DataLayer's don't get valid again.
	const FName DataLayerUniqueName = *FString::Format(TEXT("DataLayer_{0}"), { FGuid::NewGuid().ToString()});
	UDataLayer* NewDataLayer = NewObject<UDataLayer>(this, DataLayerUniqueName, RF_Transactional | InObjectFlags);
	check(NewDataLayer != NULL);
	FName DataLayerLabel = GenerateUniqueDataLayerLabel(InName);
	NewDataLayer->SetDataLayerLabel(DataLayerLabel);
	NewDataLayer->SetVisible(true);
	WorldDataLayers.Add(NewDataLayer);
	check(GetDataLayerFromName(NewDataLayer->GetFName()));
	return NewDataLayer;
}

bool AWorldDataLayers::RemoveDataLayers(const TArray<UDataLayer*>& InDataLayers)
{
	bool bIsModified = false;
	for (const UDataLayer* DataLayer : InDataLayers)
	{
		if (ContainsDataLayer(DataLayer))
		{
			Modify();
			WorldDataLayers.Remove(const_cast<UDataLayer*>(DataLayer));
			bIsModified = true;
		}
	}
	return bIsModified;
}

bool AWorldDataLayers::RemoveDataLayer(UDataLayer* InDataLayer)
{
	if (ContainsDataLayer(InDataLayer))
	{
		Modify();
		WorldDataLayers.Remove(const_cast<UDataLayer*>(InDataLayer));
		return true;
	}
	return false;
}

#endif

bool AWorldDataLayers::ContainsDataLayer(const UDataLayer* InDataLayer) const
{
	return WorldDataLayers.Contains(InDataLayer);
}

const UDataLayer* AWorldDataLayers::GetDataLayerFromName(const FName& InDataLayerName) const
{
#if WITH_EDITOR	
	for (UDataLayer* DataLayer : WorldDataLayers)
	{
		if (DataLayer->GetFName() == InDataLayerName)
		{
			return DataLayer;
		}
	}
#else
	if (const UDataLayer* const* FoundDataLayer = NameToDataLayer.Find(InDataLayerName))
	{
		return *FoundDataLayer;
	}
#endif
	return nullptr;
}

const UDataLayer* AWorldDataLayers::GetDataLayerFromLabel(const FName& InDataLayerLabel) const
{
#if WITH_EDITOR	
	for (const UDataLayer* DataLayer : WorldDataLayers)
	{
		if (DataLayer->GetDataLayerLabel() == InDataLayerLabel)
		{
			return DataLayer;
		}
	}
#else
	if (const UDataLayer* const* FoundDataLayer = LabelToDataLayer.Find(InDataLayerLabel))
	{
		return *FoundDataLayer;
	}
#endif
	return nullptr;
}

void AWorldDataLayers::ForEachDataLayer(TFunctionRef<bool(UDataLayer*)> Func)
{
	for (UDataLayer* DataLayer : WorldDataLayers)
	{
		if (!Func(DataLayer))
		{
			break;
		}
	}
}

void AWorldDataLayers::ForEachDataLayer(TFunctionRef<bool(UDataLayer*)> Func) const
{
	for (UDataLayer* DataLayer : WorldDataLayers)
	{
		if (!Func(DataLayer))
		{
			break;
		}
	}
}

void AWorldDataLayers::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	// Initialize DataLayer's IsDynamicallyLoadedInEditor based on DataLayerEditorPerProjectUserSettings
	const TArray<FName>& SettingsDataLayersNotLoadedInEditor = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetWorldDataLayersNotLoadedInEditor(GetWorld());
	for (const FName& DataLayerName : SettingsDataLayersNotLoadedInEditor)
	{
		if (UDataLayer* DataLayer = const_cast<UDataLayer*>(GetDataLayerFromName(DataLayerName)))
		{
			DataLayer->SetIsDynamicallyLoadedInEditor(false);
		}
	}
#else
	// Build acceleration tables
	for (const UDataLayer* DataLayer : WorldDataLayers)
	{
		LabelToDataLayer.Add(DataLayer->GetDataLayerLabel(), DataLayer);
		NameToDataLayer.Add(DataLayer->GetFName(), DataLayer);
	}
#endif
}

void AWorldDataLayers::BeginPlay()
{
	Super::BeginPlay();

	// Activate initially loaded data layers
	if (UDataLayerSubsystem* DataLayerSubsystem = GetWorld()->GetSubsystem<UDataLayerSubsystem>())
	{
		for (const UDataLayer* DataLayer : WorldDataLayers)
		{
			if (DataLayer && DataLayer->IsInitiallyActive())
			{
				DataLayerSubsystem->ActivateDataLayer(DataLayer, true);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE