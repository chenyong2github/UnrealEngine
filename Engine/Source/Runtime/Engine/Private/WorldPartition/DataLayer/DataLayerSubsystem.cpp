// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

UDataLayerSubsystem::UDataLayerSubsystem()
{}

bool UDataLayerSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	if (UWorld* WorldOuter = Cast<UWorld>(Outer))
	{
		return WorldOuter->GetWorldPartition() != nullptr;
	}

	return false;
}

UDataLayer* UDataLayerSubsystem::GetDataLayerFromLabel(const FName& InDataLayerLabel) const
{
	const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(GetWorld());
	return WorldDataLayers ? const_cast<UDataLayer*>(WorldDataLayers->GetDataLayerFromLabel(InDataLayerLabel)) : nullptr;
}

UDataLayer* UDataLayerSubsystem::GetDataLayerFromName(const FName& InDataLayerName) const
{
	const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(GetWorld());
	return WorldDataLayers ? const_cast<UDataLayer*>(WorldDataLayers->GetDataLayerFromName(InDataLayerName)) : nullptr;
}

void UDataLayerSubsystem::ActivateDataLayer(const FActorDataLayer& InDataLayer, bool bInActivate)
{
	ActivateDataLayerByName(InDataLayer.Name, bInActivate);
}

void UDataLayerSubsystem::ActivateDataLayer(const UDataLayer* InDataLayer, bool bInActivate)
{
	if (!InDataLayer)
	{
		return;
	}

	FName DataLayerName = InDataLayer->GetFName();
	const bool bIsLayerActive = IsDataLayerActiveByName(DataLayerName);
	if (bIsLayerActive != bInActivate)
	{
		if (bInActivate)
		{
			ActiveDataLayerNames.Add(DataLayerName);
		}
		else
		{
			ActiveDataLayerNames.Remove(DataLayerName);
		}
	}
}

void UDataLayerSubsystem::ActivateDataLayerByName(const FName& InDataLayerName, bool bInActivate)
{
	ActivateDataLayer(GetDataLayerFromName(InDataLayerName), bInActivate);
}

void UDataLayerSubsystem::ActivateDataLayerByLabel(const FName& InDataLayerLabel, bool bInActivate)
{
	ActivateDataLayer(GetDataLayerFromLabel(InDataLayerLabel), bInActivate);
}

bool UDataLayerSubsystem::IsDataLayerActive(const FActorDataLayer& InDataLayer) const
{
	return IsDataLayerActiveByName(InDataLayer.Name);
}

bool UDataLayerSubsystem::IsDataLayerActive(const UDataLayer* InDataLayer) const
{
	return InDataLayer && IsDataLayerActiveByName(InDataLayer->GetFName());
}

bool UDataLayerSubsystem::IsDataLayerActiveByName(const FName& InDataLayerName) const
{
	return ActiveDataLayerNames.Contains(InDataLayerName);
}

bool UDataLayerSubsystem::IsDataLayerActiveByLabel(const FName& InDataLayerLabel) const
{
	return IsDataLayerActive(GetDataLayerFromLabel(InDataLayerLabel));
}

bool UDataLayerSubsystem::IsAnyDataLayerActive(const TArray<FName>& InDataLayerNames) const
{
	for (FName DataLayerName : InDataLayerNames)
	{
		if (IsDataLayerActiveByName(DataLayerName))
		{
			return true;
		}
	}
	return false;
}

FAutoConsoleCommand UDataLayerSubsystem::ToggleDataLayerActivation(
	TEXT("wp.Runtime.ToggleDataLayerActivation"),
	TEXT("Toggles a DataLayer. Args [DataLayerLabel]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() == 1)
		{
			FName DataLayerLabel = FName(Args[0]);
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->IsGameWorld())
				{
					UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>();
					if (const UDataLayer* DataLayer = DataLayerSubsystem ? DataLayerSubsystem->GetDataLayerFromLabel(DataLayerLabel) : nullptr)
					{
						DataLayerSubsystem->ActivateDataLayer(DataLayer, !DataLayerSubsystem->IsDataLayerActive(DataLayer));
					}
				}
			}
		}
	})
);
