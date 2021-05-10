// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "WorldPartition/DataLayer/IDataLayerEditorModule.h"
#endif

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

void UDataLayerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	if (GEditor)
	{
		FModuleManager::LoadModuleChecked<IDataLayerEditorModule>("DataLayerEditor");
	}
#endif
}

void UDataLayerSubsystem::PostInitialize()
{
	Super::PostInitialize();

	if (GetWorld()->IsGameWorld())
	{
		// Initialize Dynamically loaded Data Layers state
		if (const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
		{
			WorldDataLayers->ForEachDataLayer([this](class UDataLayer* DataLayer)
			{
				if (DataLayer && DataLayer->IsDynamicallyLoaded())
				{
					SetDataLayerState(DataLayer, DataLayer->GetInitialState());
				}
				return true;
			});
		}
	}
}

UDataLayer* UDataLayerSubsystem::GetDataLayer(const FActorDataLayer& InDataLayer) const
{
	return GetDataLayerFromName(InDataLayer.Name);
}

UDataLayer* UDataLayerSubsystem::GetDataLayerFromLabel(FName InDataLayerLabel) const
{
	const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();
	return WorldDataLayers ? const_cast<UDataLayer*>(WorldDataLayers->GetDataLayerFromLabel(InDataLayerLabel)) : nullptr;
}

UDataLayer* UDataLayerSubsystem::GetDataLayerFromName(FName InDataLayerName) const
{
	const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();
	return WorldDataLayers ? const_cast<UDataLayer*>(WorldDataLayers->GetDataLayerFromName(InDataLayerName)) : nullptr;
}

// Deprecated start
void UDataLayerSubsystem::ActivateDataLayer(const FActorDataLayer& InDataLayer, bool bInActivate)
{
	// Keep deprecated behavior (!Activated == Unloaded)
	SetDataLayerState(GetDataLayerFromName(InDataLayer.Name), bInActivate ? EDataLayerState::Activated : EDataLayerState::Unloaded);
}

void UDataLayerSubsystem::ActivateDataLayerByLabel(const FName& InDataLayerLabel, bool bInActivate)
{
	// Keep deprecated behavior (!Activated == Unloaded)
	SetDataLayerState(GetDataLayerFromLabel(InDataLayerLabel), bInActivate ? EDataLayerState::Activated : EDataLayerState::Unloaded);
}

bool UDataLayerSubsystem::IsDataLayerActive(const FActorDataLayer& InDataLayer) const
{
	return GetDataLayerStateByName(InDataLayer.Name) == EDataLayerState::Activated;
}

bool UDataLayerSubsystem::IsDataLayerActiveByLabel(const FName& InDataLayerLabel) const
{
	return GetDataLayerStateByLabel(InDataLayerLabel) == EDataLayerState::Activated;
}
// Deprecation end

void UDataLayerSubsystem::SetDataLayerState(const UDataLayer* InDataLayer, EDataLayerState InState)
{
	if (!InDataLayer || !InDataLayer->IsDynamicallyLoaded())
	{
		return;
	}

	FName DataLayerName = InDataLayer->GetFName();
	EDataLayerState CurrentState = GetDataLayerStateByName(DataLayerName);
	if (CurrentState != InState)
	{
		LoadedDataLayerNames.Remove(DataLayerName);
		ActiveDataLayerNames.Remove(DataLayerName);
		
		if (InState == EDataLayerState::Loaded)
		{
			LoadedDataLayerNames.Add(DataLayerName);
		}
		else if(InState == EDataLayerState::Activated)
		{
			ActiveDataLayerNames.Add(DataLayerName);
		}
		
		// todo_ow: remove
		if (OnDataLayerActivationStateChanged.IsBound())
		{
			OnDataLayerActivationStateChanged.Broadcast(InDataLayer, InState == EDataLayerState::Activated);
		}

		if (OnDataLayerStateChanged.IsBound())
		{
			OnDataLayerStateChanged.Broadcast(InDataLayer, InState);
		}
	}
}

void UDataLayerSubsystem::SetDataLayerStateByName(const FName& InDataLayerName, EDataLayerState InState)
{
	SetDataLayerState(GetDataLayerFromName(InDataLayerName), InState);
}

void UDataLayerSubsystem::SetDataLayerState(const FActorDataLayer& InDataLayer, EDataLayerState InState)
{
	SetDataLayerState(GetDataLayerFromName(InDataLayer.Name), InState);
}

void UDataLayerSubsystem::SetDataLayerStateByLabel(const FName& InDataLayerLabel, EDataLayerState InState)
{
	SetDataLayerState(GetDataLayerFromLabel(InDataLayerLabel), InState);
}

EDataLayerState UDataLayerSubsystem::GetDataLayerState(const UDataLayer* InDataLayer) const
{
	if (!InDataLayer)
	{
		return EDataLayerState::Unloaded;
	}

	return GetDataLayerStateByName(InDataLayer->GetFName());
}

EDataLayerState UDataLayerSubsystem::GetDataLayerStateByName(const FName& InDataLayerName) const
{
	if (ActiveDataLayerNames.Contains(InDataLayerName))
	{
		check(!LoadedDataLayerNames.Contains(InDataLayerName));
		return EDataLayerState::Activated;
	}
	else if (LoadedDataLayerNames.Contains(InDataLayerName))
	{
		check(!ActiveDataLayerNames.Contains(InDataLayerName));
		return EDataLayerState::Loaded;
	}

	return EDataLayerState::Unloaded;
}

EDataLayerState UDataLayerSubsystem::GetDataLayerState(const FActorDataLayer& InDataLayer) const
{
	return GetDataLayerStateByName(InDataLayer.Name);
}

EDataLayerState UDataLayerSubsystem::GetDataLayerStateByLabel(const FName& InDataLayerLabel) const
{
	return GetDataLayerState(GetDataLayerFromLabel(InDataLayerLabel));
}

bool UDataLayerSubsystem::IsAnyDataLayerInState(const TArray<FName>& InDataLayerNames, EDataLayerState InState) const
{
	for (FName DataLayerName : InDataLayerNames)
	{
		if (GetDataLayerStateByName(DataLayerName) == InState)
		{
			return true;
		}
	}
	return false;
}

void UDataLayerSubsystem::DrawDataLayersStatus(UCanvas* Canvas, FVector2D& Offset) const
{
	if (!Canvas || !Canvas->SceneView)
	{
		return;
	}
	
	FVector2D Pos = Offset;
	float MaxTextWidth = 0.f;

	TMap<FName, FColor> ColorMapping;
	GetDataLayerDebugColors(ColorMapping);

	auto DrawLayerNames = [this, Canvas, &Pos, &MaxTextWidth, &ColorMapping](const FString& Title, FColor Color, const TSet<FName>& LayerNames)
	{
		if (LayerNames.Num() > 0)
		{
			FWorldPartitionDebugHelper::DrawText(Canvas, Title, GEngine->GetSmallFont(), Color, Pos, &MaxTextWidth);

			UFont* DataLayerFont = GEngine->GetTinyFont();
			for (const FName& DataLayerName : LayerNames)
			{
				if (UDataLayer* DataLayer = GetDataLayerFromName(DataLayerName))
				{
					FWorldPartitionDebugHelper::DrawLegendItem(Canvas, *DataLayer->GetDataLayerLabel().ToString(), DataLayerFont, ColorMapping[DataLayerName], Pos, &MaxTextWidth);
				}
			}
		}
	};

	DrawLayerNames(TEXT("Loaded Data Layers"), FColor::Cyan, LoadedDataLayerNames);
	DrawLayerNames(TEXT("Active Data Layers"), FColor::Green, ActiveDataLayerNames);

	Offset.X += MaxTextWidth + 10;
}

TArray<UDataLayer*> UDataLayerSubsystem::ConvertArgsToDataLayers(UWorld* World, const TArray<FString>& InArgs)
{
	TSet<UDataLayer*> OutDataLayers;

	const TCHAR* QuoteChar = TEXT("\"");
	bool bQuoteStarted = false;
	TStringBuilder<512> Builder;
	TArray<FString> Args;
	for (const FString& Arg : InArgs)
	{
		if (!bQuoteStarted && Arg.StartsWith(QuoteChar))
		{
			Builder.Append(Arg.Replace(QuoteChar, TEXT("")));
			if (Arg.EndsWith(QuoteChar) && Arg.Len() > 1)
			{
				Args.Add(Builder.ToString());
				Builder.Reset();
			}
			else
			{
				bQuoteStarted = true;
			}
		}
		else if (bQuoteStarted)
		{
			Builder.Append(TEXT(" "));
			Builder.Append(Arg.Replace(QuoteChar, TEXT("")));
			if (Arg.EndsWith(QuoteChar))
			{
				bQuoteStarted = false;
				Args.Add(Builder.ToString());
				Builder.Reset();
			}
		}
		else
		{
			Args.Add(Arg);
		}
	}
	if (bQuoteStarted)
	{
		Args.Add(Builder.ToString());
	}

	for (const FString& Arg : Args)
	{
		FName DataLayerLabel = FName(Arg);
		if (const AWorldDataLayers* WorldDataLayers = World->GetWorldDataLayers())
		{
			UDataLayer* DataLayer = const_cast<UDataLayer*>(WorldDataLayers->GetDataLayerFromLabel(DataLayerLabel));
			if (!DataLayer)
			{
				// Try to find Data Layer by ignoring whitespaces and case
				FString DataLayerLabelToFind = DataLayerLabel.ToString().Replace(TEXT(" "), TEXT(""));
				WorldDataLayers->ForEachDataLayer([&DataLayerLabelToFind, &DataLayer](UDataLayer* It)
				{
					if (It->GetDataLayerLabel().ToString().Replace(TEXT(" "), TEXT("")).Compare(DataLayerLabelToFind, ESearchCase::IgnoreCase) == 0)
					{
						DataLayer = It;
						return false;
					}
					return true;
				});
			}
			if (DataLayer)
			{
				OutDataLayers.Add(DataLayer);
			}
		}
	}

	return OutDataLayers.Array();
}

FAutoConsoleCommand UDataLayerSubsystem::ToggleDataLayerActivation(
	TEXT("wp.Runtime.ToggleDataLayerActivation"),
	TEXT("Toggles DataLayers active state. Args [DataLayerLabels]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>())
				{
					TArray<UDataLayer*> DataLayers = UDataLayerSubsystem::ConvertArgsToDataLayers(World, InArgs);
					for (UDataLayer* DataLayer : DataLayers)
					{
						DataLayerSubsystem->SetDataLayerState(DataLayer, DataLayerSubsystem->GetDataLayerState(DataLayer) == EDataLayerState::Activated ? EDataLayerState::Unloaded : EDataLayerState::Activated);
					}
				}
			}
		}
	})
);

void UDataLayerSubsystem::GetDataLayerDebugColors(TMap<FName, FColor>& OutMapping) const
{
	OutMapping.Reset();

	const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();
	if (!WorldDataLayers)
	{
		return;
	}
	
	WorldDataLayers->ForEachDataLayer([&OutMapping](UDataLayer* DataLayer)
	{
		OutMapping.Add(DataLayer->GetFName(), DataLayer->GetDebugColor());
		return true;
	});
}