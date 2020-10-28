// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldDataLayers.cpp: AWorldDataLayers class implementation
=============================================================================*/

#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "WorldDataLayers"

AWorldDataLayers::AWorldDataLayers(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

const AWorldDataLayers* AWorldDataLayers::Get(UWorld* World)
{
	if (World)
	{
		for (AWorldDataLayers* Actor : TActorRange<AWorldDataLayers>(World))
		{
			if (Actor && !Actor->IsPendingKill())
			{
				return Actor;
			}
		}
	}
	return nullptr;
}

#if WITH_EDITOR
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

UDataLayer* AWorldDataLayers::CreateDataLayer()
{
	Modify();

	// Make sure new DataLayer name (not label) is unique and never re-used so that actors still referencing on deleted DataLayer's don't get valid again.
	const FName DataLayerUniqueName = *FString::Format(TEXT("DataLayer_{0}"), { FGuid::NewGuid().ToString()});
	UDataLayer* NewDataLayer = NewObject<UDataLayer>(this, DataLayerUniqueName, RF_Transactional);
	check(NewDataLayer != NULL);
	const FName DefaultLabel = TEXT("DataLayer");
	FName DataLayerLabel = GenerateUniqueDataLayerLabel(DefaultLabel);
	NewDataLayer->SetDataLayerLabel(DataLayerLabel);
	NewDataLayer->SetVisible(true);
	WorldDataLayers.Add(NewDataLayer);
	check(GetDataLayerFromName(NewDataLayer->GetFName()));
	return NewDataLayer;
}

bool AWorldDataLayers::RemoveDataLayers(const TArray<const UDataLayer*>& InDataLayers)
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

bool AWorldDataLayers::RemoveDataLayer(const UDataLayer* InDataLayer)
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

#if !WITH_EDITOR
	// Build acceleration tables
	for (const UDataLayer* DataLayer : WorldDataLayers)
	{
		LabelToDataLayer.Add(DataLayer->GetDataLayerLabel(), DataLayer);
		NameToDataLayer.Add(DataLayer->GetFName(), DataLayer);
	}
#endif
}

#undef LOCTEXT_NAMESPACE