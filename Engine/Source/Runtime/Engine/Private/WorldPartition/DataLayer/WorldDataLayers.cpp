// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldDataLayers.cpp: AWorldDataLayers class implementation
=============================================================================*/

#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "EngineUtils.h"
#include "Engine/CoreSettings.h"
#include "Net/UnrealNetwork.h"
#include "WorldPartition/WorldPartition.h"
#if WITH_EDITOR
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#endif

#define LOCTEXT_NAMESPACE "WorldDataLayers"

int32 AWorldDataLayers::DataLayersStateEpoch = 0;

FString JoinDataLayerLabelsFromNames(AWorldDataLayers* InWorldDataLayers, const TArray<FName>& InDataLayerNames)
{
	check(InWorldDataLayers);
	TArray<FString> DataLayerLabels;
	DataLayerLabels.Reserve(InDataLayerNames.Num());
	for (const FName& DataLayerName : InDataLayerNames)
	{
		if (const UDataLayer* DataLayer = InWorldDataLayers->GetDataLayerFromName(DataLayerName))
		{
			DataLayerLabels.Add(DataLayer->GetDataLayerLabel().ToString());
		}
	}
	return FString::Join(DataLayerLabels, TEXT(","));
}

AWorldDataLayers::AWorldDataLayers(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, bAllowRuntimeDataLayerEditing(true)
#endif
{
	bAlwaysRelevant = true;
	bReplicates = true;

	// Avoid actor from being Destroyed/Recreated when scrubbing a replay
	// instead AWorldDataLayers::RewindForReplay() gets called to reset this actors state
	bReplayRewindable = true;
}

void AWorldDataLayers::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWorldDataLayers, RepLoadedDataLayerNames);
	DOREPLIFETIME(AWorldDataLayers, RepActiveDataLayerNames);
	DOREPLIFETIME(AWorldDataLayers, RepEffectiveLoadedDataLayerNames);
	DOREPLIFETIME(AWorldDataLayers, RepEffectiveActiveDataLayerNames);
}

void AWorldDataLayers::BeginPlay()
{
	Super::BeginPlay();

	// When running a Replay we want to reset our state to CDO (empty) and rely on the Replay/Replication.
	// Unfortunately this can't be tested in the PostLoad as the World doesn't have a demo driver yet.
	if (GetWorld()->IsPlayingReplay())
	{
		ResetDataLayerRuntimeStates();
	}
}

void AWorldDataLayers::RewindForReplay()
{
	Super::RewindForReplay();

	// Same as BeginPlay when rewinding we want to reset our state to CDO (empty) and rely on Replay/Replication.
	ResetDataLayerRuntimeStates();
}

void AWorldDataLayers::InitializeDataLayerRuntimeStates()
{
	check(ActiveDataLayerNames.IsEmpty() && LoadedDataLayerNames.IsEmpty());

	if (GetWorld()->IsGameWorld())
	{
		ForEachDataLayer([this](class UDataLayer* DataLayer)
		{
			if (DataLayer && DataLayer->IsRuntime())
			{
				if (DataLayer->GetInitialRuntimeState() == EDataLayerRuntimeState::Activated)
				{
					ActiveDataLayerNames.Add(DataLayer->GetFName());
				}
				else if (DataLayer->GetInitialRuntimeState() == EDataLayerRuntimeState::Loaded)
				{
					LoadedDataLayerNames.Add(DataLayer->GetFName());
				}
			}
			return true;
		});

		RepActiveDataLayerNames = ActiveDataLayerNames.Array();
		RepLoadedDataLayerNames = LoadedDataLayerNames.Array();

		ForEachDataLayer([this](class UDataLayer* DataLayer)
		{
			if (DataLayer && DataLayer->IsRuntime())
			{
				ResolveEffectiveRuntimeState(DataLayer, /*bNotifyChange*/false);
			}
			return true;
		});

		RepEffectiveActiveDataLayerNames = EffectiveActiveDataLayerNames.Array();
		RepEffectiveLoadedDataLayerNames = EffectiveLoadedDataLayerNames.Array();

		UE_LOG(LogWorldPartition, Log, TEXT("Initial Data Layer Effective States Activated(%s) Loaded(%s)"), *JoinDataLayerLabelsFromNames(this, RepEffectiveActiveDataLayerNames), *JoinDataLayerLabelsFromNames(this, RepEffectiveLoadedDataLayerNames));
	}
}

void AWorldDataLayers::ResetDataLayerRuntimeStates()
{
	ActiveDataLayerNames.Reset();
	LoadedDataLayerNames.Reset();
	RepActiveDataLayerNames.Reset();
	RepLoadedDataLayerNames.Reset();

	EffectiveActiveDataLayerNames.Reset();
	EffectiveLoadedDataLayerNames.Reset();
	RepEffectiveActiveDataLayerNames.Reset();
	RepEffectiveLoadedDataLayerNames.Reset();
}

void AWorldDataLayers::SetDataLayerRuntimeState(FActorDataLayer InDataLayer, EDataLayerRuntimeState InState, bool bInIsRecursive)
{
	if (ensure(GetLocalRole() == ROLE_Authority))
	{
		const UDataLayer* DataLayer = GetDataLayerFromName(InDataLayer.Name);
		if (!DataLayer || !DataLayer->IsRuntime())
		{
			return;
		}

		EDataLayerRuntimeState CurrentState = GetDataLayerRuntimeStateByName(InDataLayer.Name);
		if (CurrentState != InState)
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			if (GetWorld()->IsGameWorld())
			{
				FName DataLayerLabelName = DataLayer->GetDataLayerLabel();
				if (DataLayersFilterDelegate.IsBound())
				{
					if (!DataLayersFilterDelegate.Execute(DataLayerLabelName, CurrentState, InState))
					{
						UE_LOG(LogWorldPartition, Log, TEXT("Data Layer '%s' was filtered out: %s -> %s"),
							*DataLayerLabelName.ToString(),
							*StaticEnum<EDataLayerRuntimeState>()->GetDisplayNameTextByValue((int64)CurrentState).ToString(),
							*StaticEnum<EDataLayerRuntimeState>()->GetDisplayNameTextByValue((int64)InState).ToString());
						return;
					}
				}
			}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

			LoadedDataLayerNames.Remove(InDataLayer.Name);
			ActiveDataLayerNames.Remove(InDataLayer.Name);

			if (InState == EDataLayerRuntimeState::Loaded)
			{
				LoadedDataLayerNames.Add(InDataLayer.Name);
			}
			else if (InState == EDataLayerRuntimeState::Activated)
			{
				ActiveDataLayerNames.Add(InDataLayer.Name);
			}
			else if (InState == EDataLayerRuntimeState::Unloaded)
			{
				GLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurgeOverride = 1;
			}

			// Update Replicated Properties
			RepActiveDataLayerNames = ActiveDataLayerNames.Array();
			RepLoadedDataLayerNames = LoadedDataLayerNames.Array();

			++DataLayersStateEpoch;

#if !NO_LOGGING || CSV_PROFILER
			const FString DataLayerLabel = DataLayer->GetDataLayerLabel().ToString();
			UE_LOG(LogWorldPartition, Log, TEXT("Data Layer '%s' state changed: %s -> %s"),
				*DataLayerLabel, 
				*StaticEnum<EDataLayerRuntimeState>()->GetDisplayNameTextByValue((int64)CurrentState).ToString(),
				*StaticEnum<EDataLayerRuntimeState>()->GetDisplayNameTextByValue((int64)InState).ToString());

			CSV_EVENT_GLOBAL(TEXT("DataLayer-%s-%s"), *DataLayerLabel, *StaticEnum<EDataLayerRuntimeState>()->GetDisplayNameTextByValue((int64)InState).ToString());
#endif

			ResolveEffectiveRuntimeState(DataLayer);
		}

		if (bInIsRecursive)
		{
			DataLayer->ForEachChild([this, InState, bInIsRecursive](const UDataLayer* Child)
			{
				SetDataLayerRuntimeState(Child->GetFName(), InState, bInIsRecursive);
				return true;
			});
		}
	}
}

void AWorldDataLayers::OnDataLayerRuntimeStateChanged_Implementation(const UDataLayer* InDataLayer, EDataLayerRuntimeState InState)
{
	UDataLayerSubsystem* DataLayerSubsystem = GetWorld()->GetSubsystem<UDataLayerSubsystem>();
	DataLayerSubsystem->OnDataLayerRuntimeStateChanged.Broadcast(InDataLayer, InState);
}

void AWorldDataLayers::OnRep_ActiveDataLayerNames()
{
	ActiveDataLayerNames.Reset();
	ActiveDataLayerNames.Append(RepActiveDataLayerNames);
}

void AWorldDataLayers::OnRep_LoadedDataLayerNames()
{
	LoadedDataLayerNames.Reset();
	LoadedDataLayerNames.Append(RepLoadedDataLayerNames);
}

EDataLayerRuntimeState AWorldDataLayers::GetDataLayerRuntimeStateByName(FName InDataLayerName) const
{
	if (ActiveDataLayerNames.Contains(InDataLayerName))
	{
		check(!LoadedDataLayerNames.Contains(InDataLayerName));
		return EDataLayerRuntimeState::Activated;
	}
	else if (LoadedDataLayerNames.Contains(InDataLayerName))
	{
		check(!ActiveDataLayerNames.Contains(InDataLayerName));
		return EDataLayerRuntimeState::Loaded;
	}

	return EDataLayerRuntimeState::Unloaded;
}

void AWorldDataLayers::OnRep_EffectiveActiveDataLayerNames()
{
	EffectiveActiveDataLayerNames.Reset();
	EffectiveActiveDataLayerNames.Append(RepEffectiveActiveDataLayerNames);
}

void AWorldDataLayers::OnRep_EffectiveLoadedDataLayerNames()
{
	EffectiveLoadedDataLayerNames.Reset();
	EffectiveLoadedDataLayerNames.Append(RepEffectiveLoadedDataLayerNames);
}

EDataLayerRuntimeState AWorldDataLayers::GetDataLayerEffectiveRuntimeStateByName(FName InDataLayerName) const
{
	if (EffectiveActiveDataLayerNames.Contains(InDataLayerName))
	{
		check(!EffectiveLoadedDataLayerNames.Contains(InDataLayerName));
		return EDataLayerRuntimeState::Activated;
	}
	else if (EffectiveLoadedDataLayerNames.Contains(InDataLayerName))
	{
		check(!EffectiveActiveDataLayerNames.Contains(InDataLayerName));
		return EDataLayerRuntimeState::Loaded;
	}

	return EDataLayerRuntimeState::Unloaded;
}

void AWorldDataLayers::ResolveEffectiveRuntimeState(const UDataLayer* InDataLayer, bool bInNotifyChange)
{
	check(InDataLayer);
	const FName DataLayerName = InDataLayer->GetFName();
	EDataLayerRuntimeState CurrentEffectiveRuntimeState = GetDataLayerEffectiveRuntimeStateByName(DataLayerName);
	EDataLayerRuntimeState NewEffectiveRuntimeState = GetDataLayerRuntimeStateByName(DataLayerName);
	const UDataLayer* Parent = InDataLayer->GetParent();
	while (Parent && (NewEffectiveRuntimeState != EDataLayerRuntimeState::Unloaded))
	{
		if (Parent->IsRuntime())
		{
			// Apply min logic with parent DataLayers
			NewEffectiveRuntimeState = (EDataLayerRuntimeState)FMath::Min((int32)NewEffectiveRuntimeState, (int32)GetDataLayerRuntimeStateByName(Parent->GetFName()));
		}
		Parent = Parent->GetParent();
	};

	if (CurrentEffectiveRuntimeState != NewEffectiveRuntimeState)
	{
		EffectiveLoadedDataLayerNames.Remove(DataLayerName);
		EffectiveActiveDataLayerNames.Remove(DataLayerName);

		if (NewEffectiveRuntimeState == EDataLayerRuntimeState::Loaded)
		{
			EffectiveLoadedDataLayerNames.Add(DataLayerName);
		}
		else if (NewEffectiveRuntimeState == EDataLayerRuntimeState::Activated)
		{
			EffectiveActiveDataLayerNames.Add(DataLayerName);
		}

		// Update Replicated Properties
		RepEffectiveActiveDataLayerNames = EffectiveActiveDataLayerNames.Array();
		RepEffectiveLoadedDataLayerNames = EffectiveLoadedDataLayerNames.Array();

		++DataLayersStateEpoch;

		if (bInNotifyChange)
		{
			UE_LOG(LogWorldPartition, Log, TEXT("Data Layer '%s' effective state changed: %s -> %s"),
				*InDataLayer->GetDataLayerLabel().ToString(),
				*StaticEnum<EDataLayerRuntimeState>()->GetDisplayNameTextByValue((int64)CurrentEffectiveRuntimeState).ToString(),
				*StaticEnum<EDataLayerRuntimeState>()->GetDisplayNameTextByValue((int64)NewEffectiveRuntimeState).ToString());

			OnDataLayerRuntimeStateChanged(InDataLayer, NewEffectiveRuntimeState);
		}

		for (const UDataLayer* Child : InDataLayer->GetChildren())
		{
			ResolveEffectiveRuntimeState(Child);
		}
	}
}

void AWorldDataLayers::DumpDataLayerRecursively(const UDataLayer* DataLayer, FString Prefix, FOutputDevice& OutputDevice) const
{
	auto GetDataLayerRuntimeStateString = [this](const UDataLayer* DataLayer)
	{
		if (DataLayer->IsRuntime())
		{
			if (!DataLayer->GetWorld()->IsGameWorld())
			{
				return FString::Printf(TEXT("(Initial State = %s)"), GetDataLayerRuntimeStateName(DataLayer->GetInitialRuntimeState()));
			}
			else
			{
				return FString::Printf(TEXT("(Effective State = %s | Target State = %s)"),
					GetDataLayerRuntimeStateName(GetDataLayerEffectiveRuntimeStateByName(DataLayer->GetFName())),
					GetDataLayerRuntimeStateName(GetDataLayerRuntimeStateByName(DataLayer->GetFName()))
				);
			}
		}
		return FString("");
	};

	OutputDevice.Logf(TEXT(" %s%s%s %s"),
		*Prefix,
		(DataLayer->GetChildren().IsEmpty() && DataLayer->GetParent()) ? TEXT("") : TEXT("[+]"),
		*DataLayer->GetDataLayerLabel().ToString(),
		*GetDataLayerRuntimeStateString(DataLayer));

	for (const UDataLayer* Child : DataLayer->GetChildren())
	{
		DumpDataLayerRecursively(Child, Prefix + TEXT(" | "), OutputDevice);
	}
};

void AWorldDataLayers::DumpDataLayers(FOutputDevice& OutputDevice) const
{
	OutputDevice.Logf(TEXT("===================================================="));
	OutputDevice.Logf(TEXT(" Data Layers for World %s"), *GetWorld()->GetName());
	OutputDevice.Logf(TEXT("===================================================="));
	OutputDevice.Logf(TEXT(""));

	if (GetWorld()->IsGameWorld())
	{
		auto DumpDataLayersRuntimeState = [this, &OutputDevice](const TCHAR* InStateName, const TSet<FName>& InDataLayers)
		{
			if (InDataLayers.Num())
			{
				OutputDevice.Logf(TEXT(" - %s Data Layers:"), InStateName);
				for (const FName& DataLayerName : InDataLayers)
				{
					if (const UDataLayer* DataLayer = GetDataLayerFromName(DataLayerName))
					{
						OutputDevice.Logf(TEXT("    - %s"), *DataLayer->GetDataLayerLabel().ToString());
					}
				}
			}
		};

		if (EffectiveLoadedDataLayerNames.Num() || EffectiveActiveDataLayerNames.Num())
		{
			OutputDevice.Logf(TEXT("----------------------------------------------------"));
			OutputDevice.Logf(TEXT(" Data Layers Runtime States"));
			DumpDataLayersRuntimeState(TEXT("Loaded"), EffectiveLoadedDataLayerNames);
			DumpDataLayersRuntimeState(TEXT("Active"), EffectiveActiveDataLayerNames);
			OutputDevice.Logf(TEXT("----------------------------------------------------"));
			OutputDevice.Logf(TEXT(""));
		}
	}
	
	OutputDevice.Logf(TEXT("----------------------------------------------------"));
	OutputDevice.Logf(TEXT(" Data Layers Hierarchy"));
	ForEachDataLayer([this, &OutputDevice](UDataLayer* DataLayer)
	{
		if (DataLayer && !DataLayer->GetParent())
		{
			DumpDataLayerRecursively(DataLayer, TEXT(""), OutputDevice);
		}
		return true;
	});
	OutputDevice.Logf(TEXT("----------------------------------------------------"));
}

#if WITH_EDITOR
void AWorldDataLayers::OverwriteDataLayerRuntimeStates(TArray<FActorDataLayer>* InActiveDataLayers, TArray<FActorDataLayer>* InLoadedDataLayers)
{
	if (GetLocalRole() == ROLE_Authority)
	{
		// This should get called before game starts. It doesn't send out events
		check(!GetWorld()->bMatchStarted);
		if (InActiveDataLayers)
		{
			ActiveDataLayerNames.Empty(InActiveDataLayers->Num());
			for (const FActorDataLayer& ActorDataLayer : *InActiveDataLayers)
			{
				const UDataLayer* DataLayer = GetDataLayerFromName(ActorDataLayer);
				if (DataLayer && DataLayer->IsRuntime())
				{
					ActiveDataLayerNames.Add(ActorDataLayer);
				}
			}
			RepActiveDataLayerNames = ActiveDataLayerNames.Array();
		}

		if (InLoadedDataLayers)
		{
			LoadedDataLayerNames.Empty(InLoadedDataLayers->Num());
			for (const FActorDataLayer& ActorDataLayer : *InLoadedDataLayers)
			{
				const UDataLayer* DataLayer = GetDataLayerFromName(ActorDataLayer);
				if (DataLayer && DataLayer->IsRuntime())
				{
					LoadedDataLayerNames.Add(ActorDataLayer);
				}
			}
			RepLoadedDataLayerNames = LoadedDataLayerNames.Array();
		}

		UE_LOG(LogWorldPartition, Log, TEXT("Overwrite Data Layer States Activated(%s) Loaded(%s)"), *JoinDataLayerLabelsFromNames(this, RepActiveDataLayerNames), *JoinDataLayerLabelsFromNames(this, RepLoadedDataLayerNames));

		ForEachDataLayer([this](class UDataLayer* DataLayer)
		{
			if (DataLayer && DataLayer->IsRuntime())
			{
				ResolveEffectiveRuntimeState(DataLayer, /*bNotifyChange*/false);
			}
			return true;
		});

		RepEffectiveActiveDataLayerNames = EffectiveActiveDataLayerNames.Array();
		RepEffectiveLoadedDataLayerNames = EffectiveLoadedDataLayerNames.Array();

		UE_LOG(LogWorldPartition, Log, TEXT("Overwrite Data Layer Effective States Activated(%s) Loaded(%s)"), *JoinDataLayerLabelsFromNames(this, RepEffectiveActiveDataLayerNames), *JoinDataLayerLabelsFromNames(this, RepEffectiveLoadedDataLayerNames));
	}
}

void AWorldDataLayers::GetUserLoadedInEditorStates(TArray<FName>& OutDataLayersLoadedInEditor, TArray<FName>& OutDataLayersNotLoadedInEditor) const
{
	const TArray<FName>& SettingsDataLayersNotLoadedInEditor = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetWorldDataLayersNotLoadedInEditor(GetWorld());
	const TArray<FName>& SettingsDataLayersLoadedInEditor = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetWorldDataLayersLoadedInEditor(GetWorld());

	OutDataLayersNotLoadedInEditor.Empty();
	OutDataLayersLoadedInEditor.Empty();
	ForEachDataLayer([&OutDataLayersNotLoadedInEditor, &OutDataLayersLoadedInEditor, &SettingsDataLayersNotLoadedInEditor, &SettingsDataLayersLoadedInEditor](UDataLayer* DataLayer)
	{
		if (DataLayer->IsLoadedInEditorChangedByUserOperation())
		{
			if (!DataLayer->IsLoadedInEditor() && DataLayer->IsInitiallyLoadedInEditor())
			{
				OutDataLayersNotLoadedInEditor.Add(DataLayer->GetFName());
			}
			else if (DataLayer->IsLoadedInEditor() && !DataLayer->IsInitiallyLoadedInEditor())
			{
				OutDataLayersLoadedInEditor.Add(DataLayer->GetFName());
			}
			
			DataLayer->ClearLoadedInEditorChangedByUserOperation();
		}
		else
		{
			if (SettingsDataLayersNotLoadedInEditor.Contains(DataLayer->GetFName()))
			{
				OutDataLayersNotLoadedInEditor.Add(DataLayer->GetFName());
			}
			else if (SettingsDataLayersLoadedInEditor.Contains(DataLayer->GetFName()))
			{
				OutDataLayersLoadedInEditor.Add(DataLayer->GetFName());
			}
		}
		return true;
	});
}

AWorldDataLayers* AWorldDataLayers::Create(UWorld* World)
{
	check(World);
	check(!World->GetWorldDataLayers());

	AWorldDataLayers* WorldDataLayers = nullptr;

	static FName WorldDataLayersName = AWorldDataLayers::StaticClass()->GetFName();
	if (UObject* ExistingObject = StaticFindObject(nullptr, World->PersistentLevel, *WorldDataLayersName.ToString()))
	{
		WorldDataLayers = CastChecked<AWorldDataLayers>(ExistingObject);
		if (!IsValidChecked(WorldDataLayers))
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
		SpawnParams.Name = WorldDataLayersName;
		SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_Fatal;
		WorldDataLayers = World->SpawnActor<AWorldDataLayers>(AWorldDataLayers::StaticClass(), SpawnParams);
	}

	check(WorldDataLayers);

	World->Modify();
	World->SetWorldDataLayers(WorldDataLayers);

	return WorldDataLayers;
}

FName AWorldDataLayers::GenerateUniqueDataLayerLabel(const FName& InDataLayerLabel) const
{
	int32 DataLayerIndex = 0;
	const FName DataLayerLabelSanitized = UDataLayer::GetSanitizedDataLayerLabel(InDataLayerLabel);
	FName UniqueNewDataLayerLabel = DataLayerLabelSanitized;
	while (GetDataLayerFromLabel(UniqueNewDataLayerLabel))
	{
		UniqueNewDataLayerLabel = FName(*FString::Printf(TEXT("%s%d"), *DataLayerLabelSanitized.ToString(), ++DataLayerIndex));
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

TArray<const UDataLayer*> AWorldDataLayers::GetDataLayerObjects(const TArray<FName>& InDataLayerNames) const
{
	TArray<const UDataLayer*> OutDataLayers;
	OutDataLayers.Reserve(DataLayers.Num());

	for (const FName& DataLayerName : InDataLayerNames)
	{
		if (const UDataLayer* DataLayerObject = GetDataLayerFromName(DataLayerName))
		{
			OutDataLayers.AddUnique(DataLayerObject);
		}
	}

	return OutDataLayers;
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
	for (UDataLayer* DataLayer : InDataLayers)
	{
		if (ContainsDataLayer(DataLayer))
		{
			Modify();
			DataLayer->SetChildParent(DataLayer->GetParent());
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

void AWorldDataLayers::SetAllowRuntimeDataLayerEditing(bool bInAllowRuntimeDataLayerEditing)
{
	if (bAllowRuntimeDataLayerEditing != bInAllowRuntimeDataLayerEditing)
	{
		Modify();
		bAllowRuntimeDataLayerEditing = bInAllowRuntimeDataLayerEditing;
	}
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
	const FName DataLayerLabelSanitized = UDataLayer::GetSanitizedDataLayerLabel(InDataLayerLabel);
#if WITH_EDITOR	
	for (const UDataLayer* DataLayer : WorldDataLayers)
	{
		if (DataLayer->GetDataLayerLabel() == DataLayerLabelSanitized)
		{
			return DataLayer;
		}
	}
#else
	if (const UDataLayer* const* FoundDataLayer = LabelToDataLayer.Find(DataLayerLabelSanitized))
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

	GetLevel()->ConditionalPostLoad();

	GetWorld()->SetWorldDataLayers(this);

#if WITH_EDITOR
	// Remove all Editor Data Layers when cooking or when in a game world
	if (IsRunningCookCommandlet() || GetWorld()->IsGameWorld())
	{
		ForEachDataLayer([](UDataLayer* DataLayer)
		{
			DataLayer->ConditionalPostLoad();
			return true;
		});

		TArray<UDataLayer*> EditorDataLayers;
		ForEachDataLayer([&EditorDataLayers](UDataLayer* DataLayer)
		{
			if (DataLayer && !DataLayer->IsRuntime())
			{
				EditorDataLayers.Add(DataLayer);
			}
			return true;
		});
		RemoveDataLayers(EditorDataLayers);
	}

	// Setup defaults before overriding with user settings
	for (UDataLayer* DataLayer : WorldDataLayers)
	{
		DataLayer->SetIsLoadedInEditor(DataLayer->IsInitiallyLoadedInEditor(), /*bFromUserChange*/false);
	}

	// Initialize DataLayer's IsLoadedInEditor based on DataLayerEditorPerProjectUserSettings
	TArray<FName> SettingsDataLayersNotLoadedInEditor = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetWorldDataLayersNotLoadedInEditor(GetWorld());
	for (const FName& DataLayerName : SettingsDataLayersNotLoadedInEditor)
	{
		if (UDataLayer* DataLayer = const_cast<UDataLayer*>(GetDataLayerFromName(DataLayerName)))
		{
			DataLayer->SetIsLoadedInEditor(false, /*bFromUserChange*/false);
		}
	}

	TArray<FName> SettingsDataLayersLoadedInEditor = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetWorldDataLayersLoadedInEditor(GetWorld());
	for (const FName& DataLayerName : SettingsDataLayersLoadedInEditor)
	{
		if (UDataLayer* DataLayer = const_cast<UDataLayer*>(GetDataLayerFromName(DataLayerName)))
		{
			DataLayer->SetIsLoadedInEditor(true, /*bFromUserChange*/false);
		}
	}

	bListedInSceneOutliner = true;
#else
	// Build acceleration tables
	for (const UDataLayer* DataLayer : WorldDataLayers)
	{
		LabelToDataLayer.Add(DataLayer->GetDataLayerLabel(), DataLayer);
		NameToDataLayer.Add(DataLayer->GetFName(), DataLayer);
	}
#endif

	InitializeDataLayerRuntimeStates();
}

#undef LOCTEXT_NAMESPACE