// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

UDataLayerSubsystem::UDataLayerSubsystem()
{}

const UDataLayer* UDataLayerSubsystem::GetDataLayerFromLabel(const FName& InDataLayerLabel) const
{
	const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(GetWorld());
	return WorldDataLayers ? WorldDataLayers->GetDataLayerFromLabel(InDataLayerLabel) : nullptr;
}

const UDataLayer* UDataLayerSubsystem::GetDataLayerFromName(const FName& InDataLayerName) const
{
	const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(GetWorld());
	return WorldDataLayers ? WorldDataLayers->GetDataLayerFromName(InDataLayerName) : nullptr;
}

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

void UDataLayerSubsystem::ActivateDataLayer(const FName& InDataLayerName, bool bActivate)
{
	// First validate that DataLayer is known by the world
	if (!GetDataLayerFromName(InDataLayerName))
{
		return;
	}

	const bool bIsLayerActive = IsDataLayerActive(InDataLayerName);
	if (bIsLayerActive != bActivate)
	{
		if (bActivate)
		{
			ActiveDataLayerNames.Add(InDataLayerName);
		}
		else
		{
			ActiveDataLayerNames.Remove(InDataLayerName);
		}
	}
}

bool UDataLayerSubsystem::IsDataLayerActive(const FName& InDataLayerName) const
{
	return ActiveDataLayerNames.Contains(InDataLayerName);
}

bool UDataLayerSubsystem::IsAnyDataLayerActive(const TArray<FName>& InDataLayerNames) const
{
	for (FName DataLayerName : InDataLayerNames)
	{
		if (IsDataLayerActive(DataLayerName))
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
						FName DataLayerName = DataLayer->GetFName();
						DataLayerSubsystem->ActivateDataLayer(DataLayerName, !DataLayerSubsystem->IsDataLayerActive(DataLayerName));
					}
				}
			}
		}
	})
);
