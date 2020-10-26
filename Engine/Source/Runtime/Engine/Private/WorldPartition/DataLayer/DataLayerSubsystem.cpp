// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

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

void UDataLayerSubsystem::ActivateDataLayer(const FName& InDataLayer, bool bActivate)
{
	const bool bIsLayerActive = IsDataLayerActive(InDataLayer);
	if (bIsLayerActive != bActivate)
	{
		if (bActivate)
		{
			//@todo_ow: Once DataLayer is an asset, validate it exists
			ActiveDataLayers.Add(InDataLayer);
		}
		else
		{
			ActiveDataLayers.Remove(InDataLayer);
		}
	}
}

bool UDataLayerSubsystem::IsDataLayerActive(const FName& InDataLayer) const
{
	return ActiveDataLayers.Contains(InDataLayer);
}

bool UDataLayerSubsystem::IsAnyDataLayerActive(const TArray<FName>& InDataLayers) const
{
	for (const FName& DataLayer : InDataLayers)
	{
		if (IsDataLayerActive(DataLayer))
		{
			return true;
		}
	}
	return false;
}

FAutoConsoleCommand UDataLayerSubsystem::ToggleDataLayerActivation(
	TEXT("wp.Runtime.ToggleDataLayerActivation"),
	TEXT("Toggles a DataLayer. Args [DataLayerName]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() == 1)
		{
			FName DataLayer = FName(Args[0]);
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->IsGameWorld())
				{
					if (UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>())
					{
						DataLayerSubsystem->ActivateDataLayer(DataLayer, !DataLayerSubsystem->IsDataLayerActive(DataLayer));
					}
				}
			}
		}
	})
);
