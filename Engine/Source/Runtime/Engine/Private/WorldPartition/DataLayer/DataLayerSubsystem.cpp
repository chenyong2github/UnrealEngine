// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
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
#include "ProfilingDebugging/ScopedTimers.h"
#include "WorldPartition/DataLayer/DataLayerUtils.h"
#include "WorldPartition/DataLayer/IDataLayerEditorModule.h"
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
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

#if WITH_EDITOR
FDataLayersEditorBroadcast& FDataLayersEditorBroadcast::Get()
{
	static FDataLayersEditorBroadcast DataLayersEditorBroadcast;
	return DataLayersEditorBroadcast;
}

void FDataLayersEditorBroadcast::StaticOnActorDataLayersEditorLoadingStateChanged(bool bIsFromUserChange)
{
	Get().DataLayerEditorLoadingStateChanged.Broadcast(bIsFromUserChange);
}
#endif

UDataLayerSubsystem::UDataLayerSubsystem()
{}

void UDataLayerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	if (GEditor)
	{
		FModuleManager::LoadModuleChecked<IDataLayerEditorModule>("DataLayerEditor");
	}

	UActorDescContainer::OnActorDescContainerInitialized.AddUObject(this, &UDataLayerSubsystem::OnActorDescContainerInitialized);
#endif
}

void UDataLayerSubsystem::Deinitialize()
{
	Super::Deinitialize();

#if WITH_EDITOR
	UActorDescContainer::OnActorDescContainerInitialized.RemoveAll(this);
#endif
}

bool UDataLayerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return Super::DoesSupportWorldType(WorldType) || WorldType == EWorldType::Inactive;
}


#if WITH_EDITOR

bool UDataLayerSubsystem::CanResolveDataLayers() const
{
	return GetWorld()->GetWorldDataLayers() != nullptr;
}

bool UDataLayerSubsystem::RemoveDataLayer(const UDataLayerInstance* InDataLayer)
{
	if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		return WorldDataLayers->RemoveDataLayer(InDataLayer);;
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("Removing a Data Layer without a World Partition"));
	}

	return false;
}

bool UDataLayerSubsystem::RemoveDataLayers(const TArray<UDataLayerInstance*>& InDataLayerInstances)
{
	if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		return WorldDataLayers->RemoveDataLayers(InDataLayerInstances);
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("Removing Data Layers without a World Partition"));
	}

	return false;
}

void UDataLayerSubsystem::UpdateDataLayerEditorPerProjectUserSettings() const
{
	TArray<FName> DataLayersNotLoadedInEditor;
	TArray<FName> DataLayersLoadedInEditor;

	GetUserLoadedInEditorStates(DataLayersLoadedInEditor, DataLayersNotLoadedInEditor);

	GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->SetWorldDataLayersNonDefaultEditorLoadStates(GetWorld(), DataLayersLoadedInEditor, DataLayersNotLoadedInEditor);
}

void UDataLayerSubsystem::GetUserLoadedInEditorStates(TArray<FName>& OutDataLayersLoadedInEditor, TArray<FName>& OutDataLayersNotLoadedInEditor) const
{
	OutDataLayersLoadedInEditor.Empty();
	OutDataLayersNotLoadedInEditor.Empty();

	const TArray<FName>& SettingsDataLayersNotLoadedInEditor = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetWorldDataLayersNotLoadedInEditor(GetWorld());
	const TArray<FName>& SettingsDataLayersLoadedInEditor = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetWorldDataLayersLoadedInEditor(GetWorld());

	ForEachDataLayer([&OutDataLayersLoadedInEditor, &OutDataLayersNotLoadedInEditor, &SettingsDataLayersNotLoadedInEditor, &SettingsDataLayersLoadedInEditor](UDataLayerInstance* DataLayerInstance)
	{
		if (DataLayerInstance->IsLoadedInEditorChangedByUserOperation())
		{
			if (!DataLayerInstance->IsLoadedInEditor() && DataLayerInstance->IsInitiallyLoadedInEditor())
			{
				OutDataLayersNotLoadedInEditor.Add(DataLayerInstance->GetDataLayerFName());
			}
			else if (DataLayerInstance->IsLoadedInEditor() && !DataLayerInstance->IsInitiallyLoadedInEditor())
			{
				OutDataLayersLoadedInEditor.Add(DataLayerInstance->GetDataLayerFName());
			}

			DataLayerInstance->ClearLoadedInEditorChangedByUserOperation();
		}
		else
		{
			if (SettingsDataLayersNotLoadedInEditor.Contains(DataLayerInstance->GetDataLayerFName()))
			{
				OutDataLayersNotLoadedInEditor.Add(DataLayerInstance->GetDataLayerFName());
			}
			else if (SettingsDataLayersLoadedInEditor.Contains(DataLayerInstance->GetDataLayerFName()))
			{
				OutDataLayersLoadedInEditor.Add(DataLayerInstance->GetDataLayerFName());
			}
		}

		return true;
	});
}
#endif

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

UDataLayerInstance* UDataLayerSubsystem::GetDataLayerInstanceFromAsset(const UDataLayerAsset* InDataLayerAsset) const
{
	return GetDataLayerInstance(InDataLayerAsset);
}

void UDataLayerSubsystem::SetDataLayerInstanceRuntimeState(const UDataLayerAsset* InDataLayerAsset, EDataLayerRuntimeState InState, bool bInIsRecursive)
{
	SetDataLayerRuntimeState(GetDataLayerInstanceFromAsset(InDataLayerAsset), InState, bInIsRecursive);
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerInstanceRuntimeState(const UDataLayerAsset* InDataLayerAsset) const
{
	return GetDataLayerRuntimeState(GetDataLayerInstanceFromAsset(InDataLayerAsset));
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerInstanceEffectiveRuntimeState(const UDataLayerAsset* InDataLayerAsset) const
{
	return GetDataLayerEffectiveRuntimeState(GetDataLayerInstanceFromAsset(InDataLayerAsset));
}

void UDataLayerSubsystem::SetDataLayerRuntimeState(const UDataLayerInstance* InDataLayerInstance, EDataLayerRuntimeState InState, bool bInIsRecursive)
{
	if (InDataLayerInstance)
	{
		if (AWorldDataLayers* WorldDataLayers = InDataLayerInstance->GetOuterAWorldDataLayers())
		{
			WorldDataLayers->SetDataLayerRuntimeState(InDataLayerInstance, InState, bInIsRecursive);
		}
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerRuntimeState called with null Data Layer"));
	}
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerRuntimeState(const UDataLayerInstance* InDataLayerInstance) const
{
	AWorldDataLayers* WorldDataLayers = InDataLayerInstance->GetOuterAWorldDataLayers();
	return WorldDataLayers->GetDataLayerRuntimeStateByName(InDataLayerInstance->GetDataLayerFName());
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerRuntimeStateByName(const FName& InDataLayerName) const
{
	if (UDataLayerInstance* DataLayerInstance = GetDataLayerInstance(InDataLayerName))
	{		
		return GetDataLayerRuntimeState(DataLayerInstance);
	}
	
	return EDataLayerRuntimeState::Unloaded;
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerEffectiveRuntimeState(const UDataLayerInstance* InDataLayerInstance) const
{
	AWorldDataLayers* WorldDataLayers = InDataLayerInstance->GetOuterAWorldDataLayers();
	return WorldDataLayers->GetDataLayerEffectiveRuntimeStateByName(InDataLayerInstance->GetDataLayerFName());
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerEffectiveRuntimeStateByName(const FName& InDataLayerName) const
{
	if (UDataLayerInstance* DataLayerInstance = GetDataLayerInstance(InDataLayerName))
	{
		return GetDataLayerEffectiveRuntimeState(DataLayerInstance);
	}

	return EDataLayerRuntimeState::Unloaded;
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

			TArray<const UDataLayerInstance*> DataLayers;
			DataLayers.Reserve(LayerNames.Num());
			for (const FName& DataLayerName : LayerNames)
			{
				if (const UDataLayerInstance* DataLayer = GetDataLayerInstance(DataLayerName))
				{
					DataLayers.Add(DataLayer);
				}
			}

			DataLayers.Sort([](const UDataLayerInstance& A, const UDataLayerInstance& B) { return A.GetDataLayerFullName() < B.GetDataLayerFullName(); });

			UFont* DataLayerFont = GEngine->GetSmallFont();
			for (const UDataLayerInstance* DataLayer : DataLayers)
			{
				FString DataLayerString = DataLayer->GetDataLayerShortName();

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
	ForEachDataLayer([&LoadedDataLayers, &ActiveDataLayers, &UnloadedDataLayers](UDataLayerInstance* DataLayer)
	{
		if (DataLayer->IsRuntime())
		{
			const FName DataLayerName = DataLayer->GetDataLayerFName();
			if (!LoadedDataLayers.Contains(DataLayerName) && !ActiveDataLayers.Contains(DataLayerName))
			{
				UnloadedDataLayers.Add(DataLayerName);
			}
		}
		return true;
	});
	DrawLayerNames(TEXT("Unloaded Data Layers"), FColor::Silver, FColor(192, 192, 192), UnloadedDataLayers);

	Offset.X += MaxTextWidth + 10;

	// Update data layers load times
	if (GDrawDataLayersLoadTime)
	{
		for (FName DataLayerName : UnloadedDataLayers)
		{
			if (const UDataLayerInstance* DataLayer = GetDataLayerInstance(DataLayerName))
			{
				ActiveDataLayersLoadTime.Remove(DataLayer);
			}
		}

		TArray<const UDataLayerInstance*> LoadingDataLayers;
		LoadingDataLayers.Reserve(LoadedDataLayers.Num() + ActiveDataLayers.Num());
		auto CopyLambda = [this](FName DataLayerName) { return GetDataLayerInstance(DataLayerName); };
		Algo::Transform(LoadedDataLayers, LoadingDataLayers, CopyLambda);
		Algo::Transform(ActiveDataLayers, LoadingDataLayers, CopyLambda);

		for (const UDataLayerInstance* DataLayer : LoadingDataLayers)
		{
			double* DataLayerLoadTime = ActiveDataLayersLoadTime.Find(DataLayer);

			auto IsDataLayerReady = [WorldPartitionSubsystem](const UDataLayerInstance* DataLayer, EWorldPartitionRuntimeCellState TargetState)
			{
				FWorldPartitionStreamingQuerySource QuerySource;
				QuerySource.bDataLayersOnly = true;
				QuerySource.bSpatialQuery = false;
				QuerySource.DataLayers.Add(DataLayer->GetDataLayerFName());
				return WorldPartitionSubsystem->IsStreamingCompleted(TargetState, { QuerySource }, true);
			};

			const EWorldPartitionRuntimeCellState TargetState = ActiveDataLayers.Contains(DataLayer->GetDataLayerFName()) ? EWorldPartitionRuntimeCellState::Activated : EWorldPartitionRuntimeCellState::Loaded;

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

TArray<UDataLayerInstance*> UDataLayerSubsystem::ConvertArgsToDataLayers(UWorld* World, const TArray<FString>& InArgs)
{
	TSet<UDataLayerInstance*> OutDataLayers;

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

	
	UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(World);
	for (const FString& Arg : Args)
	{
		FName DataLayerName = FName(Arg);
		bool bShortNameFound = false;
		FString SanitizedDataLayerName = DataLayerName.ToString().Replace(TEXT(" "), TEXT(""));
		DataLayerSubsystem->ForEachDataLayer([&OutDataLayers, &SanitizedDataLayerName, &bShortNameFound](UDataLayerInstance* DataLayerInstance)
		{
			if (DataLayerInstance->GetDataLayerShortName().Compare(SanitizedDataLayerName, ESearchCase::IgnoreCase) == 0)
			{
				if (bShortNameFound)
				{
					UE_LOG(LogWorldPartition, Error, TEXT("Found 2 data layers with the ShortName %s when converting arguments. Consider using the data layers FullName or renaming one of the two."), *SanitizedDataLayerName);
					return false;
				}
	
				OutDataLayers.Add(DataLayerInstance);
				bShortNameFound = true;
			}
			else if (DataLayerInstance->GetDataLayerFullName().Find(SanitizedDataLayerName, ESearchCase::IgnoreCase) == 0)
			{
				OutDataLayers.Add(DataLayerInstance);
				return false;
			}
	
			return true;
		});
	}

	return OutDataLayers.Array();
}

void UDataLayerSubsystem::DumpDataLayers(FOutputDevice& OutputDevice) const
{
	if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		WorldDataLayers->DumpDataLayers(OutputDevice);
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("Dumping Data Layers without a World Partition"));
	}
}

const UDataLayerInstance* UDataLayerSubsystem::GetDataLayerInstanceFromAssetName(const FName& InDataLayerAssetFullName) const
{
	if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		const UDataLayerInstance* DataLayerInstance = WorldDataLayers->GetDataLayerInstanceFromAssetName(InDataLayerAssetFullName);
		return const_cast<UDataLayerInstance*>(DataLayerInstance);
	}

	return nullptr;
}

void UDataLayerSubsystem::ForEachDataLayer(TFunctionRef<bool(UDataLayerInstance*)> Func, const ULevel* InLevelContext /* = nullptr */)
{
	if (!InLevelContext || InLevelContext == GetWorld()->PersistentLevel)
	{
		if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
		{
			WorldDataLayers->ForEachDataLayer(Func);
		}
		else
		{
			UE_LOG(LogWorldPartition, Warning, TEXT("Iterating over Data Layers without a World Partition"));
		}
	}
	else if (const AWorldDataLayers* CurrentLevelWorldDataLayers = (InLevelContext && !InLevelContext->IsPersistentLevel()) ? InLevelContext->GetWorldDataLayers() : nullptr)
	{
		CurrentLevelWorldDataLayers->ForEachDataLayer(Func);
	}
}

void UDataLayerSubsystem::ForEachDataLayer(TFunctionRef<bool(UDataLayerInstance*)> Func, const ULevel* InLevelContext /* = nullptr */) const
{
	const_cast<UDataLayerSubsystem*>(this)->ForEachDataLayer(Func, InLevelContext);
}

FAutoConsoleCommand UDataLayerSubsystem::ToggleDataLayerActivation(
	TEXT("wp.Runtime.ToggleDataLayerActivation"),
	TEXT("Toggles DataLayers active state. Args [DataLayerNames]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>())
				{
					TArray<UDataLayerInstance*> DataLayerInstances = UDataLayerSubsystem::ConvertArgsToDataLayers(World, InArgs);
					for (UDataLayerInstance* DataLayerInstance : DataLayerInstances)
					{
						DataLayerSubsystem->SetDataLayerRuntimeState(DataLayerInstance, DataLayerSubsystem->GetDataLayerRuntimeState(DataLayerInstance) == EDataLayerRuntimeState::Activated ? EDataLayerRuntimeState::Unloaded : EDataLayerRuntimeState::Activated);
					}
				}
			}
		}
	})
);

FAutoConsoleCommand UDataLayerSubsystem::SetDataLayerRuntimeStateCommand(
	TEXT("wp.Runtime.SetDataLayerRuntimeState"),
	TEXT("Sets Runtime DataLayers state. Args [State = Unloaded, Loaded, Activated] [DataLayerNames]"),
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
					TArray<UDataLayerInstance*> DataLayerInstances = UDataLayerSubsystem::ConvertArgsToDataLayers(World, Args);
					for (UDataLayerInstance* DataLayerInstance : DataLayerInstances)
					{
						DataLayerSubsystem->SetDataLayerRuntimeState(DataLayerInstance, State);
					}
				}
			}
		}
	})
);

void UDataLayerSubsystem::GetDataLayerDebugColors(TMap<FName, FColor>& OutMapping) const
{
	OutMapping.Reset();
	
	ForEachDataLayer([&OutMapping](UDataLayerInstance* DataLayer)
	{
		OutMapping.Add(DataLayer->GetDataLayerFName(), DataLayer->GetDebugColor());
		return true;
	});
}

#if WITH_EDITOR

void UDataLayerSubsystem::PushActorEditorContext() const
{
	if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		WorldDataLayers->PushActorEditorContext();
	}
}

void UDataLayerSubsystem::PopActorEditorContext() const
{
	if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		WorldDataLayers->PopActorEditorContext();
	}
}

TArray<UDataLayerInstance*> UDataLayerSubsystem::GetActorEditorContextDataLayers() const
{
	if(AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		return WorldDataLayers->GetActorEditorContextDataLayers();
	}

	return TArray<UDataLayerInstance*>();
}

uint32 UDataLayerSubsystem::GetDataLayerEditorContextHash() const
{
	TArray<FName> DataLayerInstanceNames;
	for (UDataLayerInstance* DataLayerInstance : GetActorEditorContextDataLayers())
	{
		DataLayerInstanceNames.Add(DataLayerInstance->GetDataLayerFName());
	}
	return FDataLayerEditorContext(GetWorld(), DataLayerInstanceNames).GetHash();
}

void UDataLayerSubsystem::OnActorDescContainerInitialized(UActorDescContainer* InActorDescContainer)
{
	check(InActorDescContainer);

	UE_SCOPED_TIMER(*FString::Printf(TEXT("Resolving Data Layer Instance Names for %s"), *InActorDescContainer->GetContainerPackage().ToString()), LogWorldPartition, Display);

	const FWorldDataLayersActorDesc* WorldDataLayersActorDesc = FDataLayerUtils::GetWorldDataLayersActorDesc(InActorDescContainer);
	for (FActorDescList::TIterator<> Iterator(InActorDescContainer); Iterator; ++Iterator)
	{
		FWorldPartitionActorDesc* ActorDesc = *Iterator;
		check(ActorDesc->GetContainer() == InActorDescContainer);
		ActorDesc->SetDataLayerInstanceNames(FDataLayerUtils::ResolvedDataLayerInstanceNames(ActorDesc, WorldDataLayersActorDesc));
	}
}

#endif

//~ Begin Deprecated

PRAGMA_DISABLE_DEPRECATION_WARNINGS

UDataLayerInstance* UDataLayerSubsystem::GetDataLayer(const FActorDataLayer& InDataLayer) const
{
	return GetDataLayerFromName(InDataLayer.Name);
}

UDataLayerInstance* UDataLayerSubsystem::GetDataLayerFromLabel(FName InDataLayerLabel) const
{
	if (AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		const UDataLayerInstance* DataLayerInstance = GetWorld()->GetWorldDataLayers()->GetDataLayerFromLabel(InDataLayerLabel);
		return const_cast<UDataLayerInstance*>(DataLayerInstance);
	}

	return nullptr;
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerRuntimeStateByLabel(const FName& InDataLayerLabel) const
{
	return GetDataLayerRuntimeState(GetDataLayerFromLabel(InDataLayerLabel));
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerEffectiveRuntimeStateByLabel(const FName& InDataLayerLabel) const
{
	return GetDataLayerEffectiveRuntimeState(GetDataLayerFromLabel(InDataLayerLabel));
}

void UDataLayerSubsystem::SetDataLayerRuntimeState(const FActorDataLayer& InDataLayer, EDataLayerRuntimeState InState, bool bInIsRecursive)
{
	if (UDataLayerInstance* DataLayerInstance = GetDataLayerFromName(InDataLayer.Name))
	{
		SetDataLayerRuntimeState(DataLayerInstance, InState, bInIsRecursive);
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerRuntimeState unknown Data Layer: '%s'"), *InDataLayer.Name.ToString());
	}
}

void UDataLayerSubsystem::SetDataLayerRuntimeStateByLabel(const FName& InDataLayerLabel, EDataLayerRuntimeState InState, bool bInIsRecursive)
{
	if (UDataLayerInstance* DataLayerInstance = GetDataLayerFromLabel(InDataLayerLabel))
	{
		SetDataLayerRuntimeState(DataLayerInstance, InState, bInIsRecursive);
	}
	else
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("UDataLayerSubsystem::SetDataLayerRuntimeStateByLabel unknown Data Layer: '%s'"), *InDataLayerLabel.ToString());
	}
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

UDataLayerInstance* UDataLayerSubsystem::GetDataLayerFromName(FName InDataLayerName) const
{
	return GetDataLayerInstance(InDataLayerName);
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerRuntimeState(const FActorDataLayer& InDataLayer) const
{
	return GetDataLayerRuntimeStateByName(InDataLayer.Name);
}

EDataLayerRuntimeState UDataLayerSubsystem::GetDataLayerEffectiveRuntimeState(const FActorDataLayer& InDataLayer) const
{
	return GetDataLayerEffectiveRuntimeStateByName(InDataLayer.Name);
}

//~ End Deprecated