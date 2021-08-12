// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "WorldPartition/WorldPartition.h"
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

const TSet<FName>& UDataLayerSubsystem::GetActiveDataLayerNames() const
{
	static TSet<FName> EmptySet;
	const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();
	return WorldDataLayers ? WorldDataLayers->GetActiveDataLayerNames() : EmptySet;
}

const TSet<FName>& UDataLayerSubsystem::GetLoadedDataLayerNames() const
{
	static TSet<FName> EmptySet;
	const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();
	return WorldDataLayers ? WorldDataLayers->GetLoadedDataLayerNames() : EmptySet;
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

void UDataLayerSubsystem::SetDataLayerState(const UDataLayer* InDataLayer, EDataLayerState InState)
{
	if (InDataLayer)
	{
		if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
		{
			WorldDataLayers->SetDataLayerState(FActorDataLayer(InDataLayer->GetFName()), InState);
		}
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerState called with null Data Layer"));
	}
}

void UDataLayerSubsystem::SetDataLayerStateByName(const FName& InDataLayerName, EDataLayerState InState)
{
	if (UDataLayer* DataLayer = GetDataLayerFromName(InDataLayerName))
	{
		SetDataLayerState(DataLayer, InState);
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerStateByName unknown Data Layer: '%s'"), *InDataLayerName.ToString());
	}
}

void UDataLayerSubsystem::SetDataLayerState(const FActorDataLayer& InDataLayer, EDataLayerState InState)
{
	if (UDataLayer* DataLayer = GetDataLayerFromName(InDataLayer.Name))
	{
		SetDataLayerState(DataLayer, InState);
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerState unknown Data Layer: '%s'"), *InDataLayer.Name.ToString());
	}
}

void UDataLayerSubsystem::SetDataLayerStateByLabel(const FName& InDataLayerLabel, EDataLayerState InState)
{
	if (UDataLayer* DataLayer = GetDataLayerFromLabel(InDataLayerLabel))
	{
		SetDataLayerState(DataLayer, InState);
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerStateByLabel unknown Data Layer: '%s'"), *InDataLayerLabel.ToString());
	}
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
	const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();
	if (!WorldDataLayers)
	{
		return EDataLayerState::Unloaded;
	}

	return WorldDataLayers->GetDataLayerStateByName(InDataLayerName);
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

	auto DrawLayerNames = [this, Canvas, &Pos, &MaxTextWidth](const FString& Title, FColor HeaderColor, FColor TextColor, const TSet<FName>& LayerNames)
	{
		if (LayerNames.Num() > 0)
		{
			FWorldPartitionDebugHelper::DrawText(Canvas, Title, GEngine->GetSmallFont(), HeaderColor, Pos, &MaxTextWidth);

			UFont* DataLayerFont = GEngine->GetSmallFont();
			for (const FName& DataLayerName : LayerNames)
			{
				if (UDataLayer* DataLayer = GetDataLayerFromName(DataLayerName))
				{
					FWorldPartitionDebugHelper::DrawLegendItem(Canvas, *DataLayer->GetDataLayerLabel().ToString(), DataLayerFont, DataLayer->GetDebugColor(), TextColor, Pos, &MaxTextWidth);
				}
			}
		}
	};

	const TSet<FName> LoadedDataLayers = GetLoadedDataLayerNames();
	const TSet<FName> ActiveDataLayers = GetActiveDataLayerNames();

	DrawLayerNames(TEXT("Loaded Data Layers"), FColor::Cyan, FColor::White, LoadedDataLayers);
	DrawLayerNames(TEXT("Active Data Layers"), FColor::Green, FColor::White, ActiveDataLayers);

	if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		TSet<FName> UnloadedDataLayers;
		WorldDataLayers->ForEachDataLayer([&LoadedDataLayers, &ActiveDataLayers, &UnloadedDataLayers](UDataLayer* DataLayer)
		{
			if (DataLayer->IsDynamicallyLoaded())
			{
				const FName DataLayerName = DataLayer->GetFName();
				if (!LoadedDataLayers.Contains(DataLayerName) && !ActiveDataLayers.Contains(DataLayerName))
				{
					UnloadedDataLayers.Add(DataLayerName);
				}
			}
			return true;
		});
		DrawLayerNames(TEXT("Unloaded Data Layers"), FColor::Silver, FColor(192,192,192), UnloadedDataLayers);
	}

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