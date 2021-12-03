// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartition.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "Algo/Transform.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "WorldPartition/DataLayer/IDataLayerEditorModule.h"
#endif

extern int32 GDrawDataLayersLoadTime;

static FAutoConsoleCommandWithOutputDevice GDumpDataLayersCmd(
	TEXT("wp.DumpDataLayers"),
	TEXT("Dumps data layers to the log"),
	FConsoleCommandWithOutputDeviceDelegate::CreateStatic([](FOutputDevice& OutputDevice)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (const UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>())
				{
					DataLayerSubsystem->DumpDataLayers(OutputDevice);
				}
			}
		}
	})
);

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
		return WorldOuter->IsPartitionedWorld();
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

const TSet<FName>& UDataLayerSubsystem::GetEffectiveActiveDataLayerNames() const
{
	static TSet<FName> EmptySet;
	const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();
	return WorldDataLayers ? WorldDataLayers->GetEffectiveActiveDataLayerNames() : EmptySet;
}

const TSet<FName>& UDataLayerSubsystem::GetEffectiveLoadedDataLayerNames() const
{
	static TSet<FName> EmptySet;
	const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();
	return WorldDataLayers ? WorldDataLayers->GetEffectiveLoadedDataLayerNames() : EmptySet;
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

void UDataLayerSubsystem::SetDataLayerRuntimeState(const UDataLayer* InDataLayer, EDataLayerRuntimeState InState, bool bInIsRecursive)
{
	if (InDataLayer)
	{
		if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
		{
			WorldDataLayers->SetDataLayerRuntimeState(FActorDataLayer(InDataLayer->GetFName()), InState, bInIsRecursive);
		}
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerRuntimeState called with null Data Layer"));
	}
}

void UDataLayerSubsystem::SetDataLayerRuntimeStateByName(const FName& InDataLayerName, EDataLayerRuntimeState InState, bool bInIsRecursive)
{
	if (UDataLayer* DataLayer = GetDataLayerFromName(InDataLayerName))
	{
		SetDataLayerRuntimeState(DataLayer, InState, bInIsRecursive);
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerRuntimeStateByName unknown Data Layer: '%s'"), *InDataLayerName.ToString());
	}
}

void UDataLayerSubsystem::SetDataLayerRuntimeState(const FActorDataLayer& InDataLayer, EDataLayerRuntimeState InState, bool bInIsRecursive)
{
	if (UDataLayer* DataLayer = GetDataLayerFromName(InDataLayer.Name))
	{
		SetDataLayerRuntimeState(DataLayer, InState, bInIsRecursive);
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerRuntimeState unknown Data Layer: '%s'"), *InDataLayer.Name.ToString());
	}
}

void UDataLayerSubsystem::SetDataLayerRuntimeStateByLabel(const FName& InDataLayerLabel, EDataLayerRuntimeState InState, bool bInIsRecursive)
{
	if (UDataLayer* DataLayer = GetDataLayerFromLabel(InDataLayerLabel))
	{
		SetDataLayerRuntimeState(DataLayer, InState, bInIsRecursive);
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerRuntimeStateByLabel unknown Data Layer: '%s'"), *InDataLayerLabel.ToString());
	}
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerRuntimeState(const UDataLayer* InDataLayer) const
{
	if (!InDataLayer)
	{
		return EDataLayerRuntimeState::Unloaded;
	}

	return GetDataLayerRuntimeStateByName(InDataLayer->GetFName());
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerRuntimeStateByName(const FName& InDataLayerName) const
{
	const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();
	if (!WorldDataLayers)
	{
		return EDataLayerRuntimeState::Unloaded;
	}

	return WorldDataLayers->GetDataLayerRuntimeStateByName(InDataLayerName);
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerRuntimeState(const FActorDataLayer& InDataLayer) const
{
	return GetDataLayerRuntimeStateByName(InDataLayer.Name);
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerRuntimeStateByLabel(const FName& InDataLayerLabel) const
{
	return GetDataLayerRuntimeState(GetDataLayerFromLabel(InDataLayerLabel));
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerEffectiveRuntimeState(const UDataLayer* InDataLayer) const
{
	if (!InDataLayer)
	{
		return EDataLayerRuntimeState::Unloaded;
	}

	return GetDataLayerEffectiveRuntimeStateByName(InDataLayer->GetFName());
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerEffectiveRuntimeStateByName(const FName& InDataLayerName) const
{
	const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();
	if (!WorldDataLayers)
	{
		return EDataLayerRuntimeState::Unloaded;
	}

	return WorldDataLayers->GetDataLayerEffectiveRuntimeStateByName(InDataLayerName);
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerEffectiveRuntimeState(const FActorDataLayer& InDataLayer) const
{
	return GetDataLayerEffectiveRuntimeStateByName(InDataLayer.Name);
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerEffectiveRuntimeStateByLabel(const FName& InDataLayerLabel) const
{
	return GetDataLayerEffectiveRuntimeState(GetDataLayerFromLabel(InDataLayerLabel));
}

bool UDataLayerSubsystem::IsAnyDataLayerInEffectiveRuntimeState(const TArray<FName>& InDataLayerNames, EDataLayerRuntimeState InState) const
{
	for (FName DataLayerName : InDataLayerNames)
	{
		if (GetDataLayerEffectiveRuntimeStateByName(DataLayerName) == InState)
		{
			return true;
		}
	}
	return false;
}

void UDataLayerSubsystem::DrawDataLayersStatus(UCanvas* Canvas, FVector2D& Offset) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UDataLayerSubsystem::DrawDataLayersStatus);

	if (!Canvas || !Canvas->SceneView)
	{
		return;
	}
	
	FVector2D Pos = Offset;
	float MaxTextWidth = 0.f;

	UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();

	auto DrawLayerNames = [this, Canvas, &Pos, &MaxTextWidth](const FString& Title, FColor HeaderColor, FColor TextColor, const TSet<FName>& LayerNames)
	{
		if (LayerNames.Num() > 0)
		{
			FWorldPartitionDebugHelper::DrawText(Canvas, Title, GEngine->GetSmallFont(), HeaderColor, Pos, &MaxTextWidth);

			TArray<UDataLayer*> DataLayers;
			DataLayers.Reserve(LayerNames.Num());
			for (const FName& DataLayerName : LayerNames)
			{
				if (UDataLayer* DataLayer = GetDataLayerFromName(DataLayerName))
				{
					DataLayers.Add(DataLayer);
				}
			}

			DataLayers.Sort([](const UDataLayer& A, const UDataLayer& B) { return A.GetDataLayerLabel().LexicalLess(B.GetDataLayerLabel()); });

			UFont* DataLayerFont = GEngine->GetSmallFont();
			for (UDataLayer* DataLayer : DataLayers)
			{
				FString DataLayerString = DataLayer->GetDataLayerLabel().ToString();

				if (GDrawDataLayersLoadTime)
				{
					if (double* DataLayerLoadTime = ActiveDataLayersLoadTime.Find(DataLayer))
					{
						if (*DataLayerLoadTime < 0)
						{
							DataLayerString += FString::Printf(TEXT(" (streaming %s)"), *FPlatformTime::PrettyTime(FPlatformTime::Seconds() + *DataLayerLoadTime));
						}
						else
						{
							DataLayerString += FString::Printf(TEXT(" (took %s)"), *FPlatformTime::PrettyTime(*DataLayerLoadTime));
						}
					}
				}

				FWorldPartitionDebugHelper::DrawLegendItem(Canvas, *DataLayerString, DataLayerFont, DataLayer->GetDebugColor(), TextColor, Pos, &MaxTextWidth);
			}
		}
	};

	const TSet<FName> LoadedDataLayers = GetEffectiveLoadedDataLayerNames();
	const TSet<FName> ActiveDataLayers = GetEffectiveActiveDataLayerNames();

	DrawLayerNames(TEXT("Loaded Data Layers"), FColor::Cyan, FColor::White, LoadedDataLayers);
	DrawLayerNames(TEXT("Active Data Layers"), FColor::Green, FColor::White, ActiveDataLayers);

	TSet<FName> UnloadedDataLayers;
	if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{		
		WorldDataLayers->ForEachDataLayer([&LoadedDataLayers, &ActiveDataLayers, &UnloadedDataLayers](UDataLayer* DataLayer)
		{
			if (DataLayer->IsRuntime())
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

	// Update data layers load times
	if (GDrawDataLayersLoadTime)
	{
		for (FName DataLayerName : UnloadedDataLayers)
		{
			if (UDataLayer* DataLayer = GetDataLayerFromName(DataLayerName))
			{
				ActiveDataLayersLoadTime.Remove(DataLayer);
			}
		}

		TArray<UDataLayer*> LoadingDataLayers;
		LoadingDataLayers.Reserve(LoadedDataLayers.Num() + ActiveDataLayers.Num());
		auto CopyLambda = [this](FName DataLayerName) { return GetDataLayerFromName(DataLayerName); };
		Algo::Transform(LoadedDataLayers, LoadingDataLayers, CopyLambda);
		Algo::Transform(ActiveDataLayers, LoadingDataLayers, CopyLambda);

		for (UDataLayer* DataLayer : LoadingDataLayers)
		{
			double* DataLayerLoadTime = ActiveDataLayersLoadTime.Find(DataLayer);

			auto IsDataLayerReady = [WorldPartitionSubsystem](UDataLayer* DataLayer, EWorldPartitionRuntimeCellState TargetState)
			{
				FWorldPartitionStreamingQuerySource QuerySource;
				QuerySource.bDataLayersOnly = true;
				QuerySource.bSpatialQuery = false;
				QuerySource.DataLayers.Add(DataLayer->GetFName());
				return WorldPartitionSubsystem->IsStreamingCompleted(TargetState, { QuerySource }, true);
			};

			const EWorldPartitionRuntimeCellState TargetState = ActiveDataLayers.Contains(DataLayer->GetFName()) ? EWorldPartitionRuntimeCellState::Activated : EWorldPartitionRuntimeCellState::Loaded;

			if (!DataLayerLoadTime)
			{
				if (!IsDataLayerReady(DataLayer, TargetState))
				{
					DataLayerLoadTime = &ActiveDataLayersLoadTime.Add(DataLayer, -FPlatformTime::Seconds());
				}
			}

			if (DataLayerLoadTime && (*DataLayerLoadTime < 0))
			{
				if (IsDataLayerReady(DataLayer, TargetState))
				{
					*DataLayerLoadTime = FPlatformTime::Seconds() + *DataLayerLoadTime;
				}
			}
		}
	}
	else
	{
		ActiveDataLayersLoadTime.Empty();
	}
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

void UDataLayerSubsystem::DumpDataLayers(FOutputDevice& OutputDevice) const
{
	if (const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		WorldDataLayers->DumpDataLayers(OutputDevice);
	}
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
						DataLayerSubsystem->SetDataLayerRuntimeState(DataLayer, DataLayerSubsystem->GetDataLayerRuntimeState(DataLayer) == EDataLayerRuntimeState::Activated ? EDataLayerRuntimeState::Unloaded : EDataLayerRuntimeState::Activated);
					}
				}
			}
		}
	})
);

FAutoConsoleCommand UDataLayerSubsystem::SetDataLayerRuntimeStateCommand(
	TEXT("wp.Runtime.SetDataLayerRuntimeState"),
	TEXT("Sets Runtime DataLayers state. Args [State = Unloaded, Loaded, Activated] [DataLayerLabels]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
	{
		if (InArgs.Num() < 2)
		{
			UE_LOG(LogWorldPartition, Warning, TEXT("wp.Runtime.SetDataLayerRuntimeState : Requires at least 2 arguments. First argument should be the target state and the next ones should be the list of DataLayers."));
			return;
		}

		TArray<FString> Args = InArgs;
		FString StatetStr;
		Args.HeapPop(StatetStr);
		EDataLayerRuntimeState State;
		if (!GetDataLayerRuntimeStateFromName(StatetStr, State))
		{
			UE_LOG(LogWorldPartition, Warning, TEXT("wp.Runtime.SetDataLayerRuntimeState : Invalid first argument, expencted one of these values : Unloaded, Loaded, Activated."));
			return;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>())
				{
					TArray<UDataLayer*> DataLayers = UDataLayerSubsystem::ConvertArgsToDataLayers(World, Args);
					for (UDataLayer* DataLayer : DataLayers)
					{
						DataLayerSubsystem->SetDataLayerRuntimeState(DataLayer, State);
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