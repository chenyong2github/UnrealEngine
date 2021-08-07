// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneDataLayerSystem.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneSpawnablesSystem.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedEntityCaptureSource.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.inl"
#include "MovieSceneTracksComponentTypes.h"
#include "Sections/MovieSceneDataLayerSection.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "Engine/World.h"
#include "Misc/EnumClassFlags.h"

#include "Kismet/KismetSystemLibrary.h"

#if WITH_EDITOR
	#include "DataLayer/DataLayerEditorSubsystem.h"
#endif

namespace UE
{
namespace MovieScene
{

enum class EDataLayerUpdateFlags : uint8
{
	None						= 0,
	FlushStreamingVisibility	= 1,
	FlushStreamingFull			= 2,
};
ENUM_CLASS_FLAGS(EDataLayerUpdateFlags)

/** Traits class governing how pre-animated state is (re)stored for data layers */
struct FPreAnimatedDataLayerStorageTraits
{
	using KeyType = TObjectKey<UDataLayer>;
	using StorageType = EDataLayerState;

	/** Called when a previously animated data layer needs to be restored */
	static void RestorePreAnimatedValue(const TObjectKey<UDataLayer>& InKey, EDataLayerState PreviousState, const FRestoreStateParams& Params);
};

/** Container class for all pre-animated data layer state */
struct FPreAnimatedDataLayerStorage
	: TPreAnimatedStateStorage<FPreAnimatedDataLayerStorageTraits>
	, IPreAnimatedStateGroupManager
{
	static TAutoRegisterPreAnimatedStorageID<FPreAnimatedDataLayerStorage> StorageID;
	static TAutoRegisterPreAnimatedStorageID<FPreAnimatedDataLayerStorage> GroupManagerID;

	/*~ IPreAnimatedStateGroupManager */
	void InitializeGroupManager(FPreAnimatedStateExtension* Extension) override;
	void OnGroupDestroyed(FPreAnimatedStorageGroupHandle Group) override;

	/** Make an entry for the specified data layer */
	FPreAnimatedStateEntry MakeEntry(UDataLayer* InDataLayer);

	/** Save the value of a data layer. Should only be used for runtime / PIE worlds */
	void SavePreAnimatedState(UDataLayer* DataLayer, UDataLayerSubsystem* SubSystem);

#if WITH_EDITOR
	/** Save the value of a data layer. Should only be used for editor worlds */
	void SavePreAnimatedStateInEditor(UDataLayer* DataLayer);
#endif

private:

	FPreAnimatedStorageGroupHandle GroupHandle;
};


struct FDataLayerState
{
	void Reset();
	bool IsEmpty() const;
	void AddRequest(int16 InBias, EDataLayerState RequestedState, bool bRequiresStreamingFlush);
	TOptional<EDataLayerState> ComputeDesiredState() const;
	bool ShouldFlushStreaming(EDataLayerState ComputedState) const;

private:

	int16 HierarchicalBias = 0;
	int32 UnloadedCount    = 0;
	int32 LoadedCount      = 0;
	int32 ActivatedCount   = 0;
	bool  bFlushUnloaded   = false;
	bool  bFlushActivated  = false;
};


struct FDesiredLayerStates
{
	bool IsEmpty() const;
	void Reset();
	EDataLayerUpdateFlags Apply(FPreAnimatedDataLayerStorage* PreAnimatedStorage, UDataLayerSubsystem* DataLayerSubsystem, UWorldPartitionSubsystem* WorldPartitionSubsystem);
#if WITH_EDITOR
	void ApplyInEditor(FPreAnimatedDataLayerStorage* PreAnimatedStorage, UDataLayerEditorSubsystem* EditorSubSystem);
#endif
	void ApplyNewState(const FName& InDataLayerName, int16 HierarchicalBias, EDataLayerState DesiredState, bool bRequiresStreamingFlush);

	TMap<FName, FDataLayerState> StatesByLayer;
};

// ---------------------------------------------------------------------
// FPreAnimatedDataLayerStorageTraits definitions
void FPreAnimatedDataLayerStorageTraits::RestorePreAnimatedValue(const TObjectKey<UDataLayer>& InKey, EDataLayerState PreviousState, const FRestoreStateParams& Params)
{
	UDataLayer* DataLayer = InKey.ResolveObjectPtr();
	if (!DataLayer)
	{
		return;
	}

	UWorld* World = DataLayer->GetWorld();
	if (World)
	{
#if WITH_EDITOR
		if (World->WorldType == EWorldType::Editor)
		{
			UDataLayerEditorSubsystem* SubSystem = UDataLayerEditorSubsystem::Get();
			SubSystem->SetDataLayerVisibility(DataLayer, PreviousState == EDataLayerState::Activated);
		}
		else
#endif
		if (UDataLayerSubsystem* SubSystem = World->GetSubsystem<UDataLayerSubsystem>())
		{
			SubSystem->SetDataLayerState(DataLayer, PreviousState);
		}
	}
}

// ---------------------------------------------------------------------
// FPreAnimatedDataLayerStorage definitions
TAutoRegisterPreAnimatedStorageID<FPreAnimatedDataLayerStorage> FPreAnimatedDataLayerStorage::StorageID;
TAutoRegisterPreAnimatedStorageID<FPreAnimatedDataLayerStorage> FPreAnimatedDataLayerStorage::GroupManagerID;

void FPreAnimatedDataLayerStorage::InitializeGroupManager(FPreAnimatedStateExtension* Extension)
{}

void FPreAnimatedDataLayerStorage::OnGroupDestroyed(FPreAnimatedStorageGroupHandle Group)
{
	ensure(Group == GroupHandle);
	GroupHandle = FPreAnimatedStorageGroupHandle();
}

FPreAnimatedStateEntry FPreAnimatedDataLayerStorage::MakeEntry(UDataLayer* InDataLayer)
{
	if (!GroupHandle)
	{
		GroupHandle = ParentExtension->AllocateGroup(SharedThis(this));
	}
	FPreAnimatedStorageIndex StorageIndex = GetOrCreateStorageIndex(InDataLayer);
	return FPreAnimatedStateEntry{ GroupHandle, FPreAnimatedStateCachedValueHandle{ StorageID, StorageIndex } };
}

void FPreAnimatedDataLayerStorage::SavePreAnimatedState(UDataLayer* DataLayer, UDataLayerSubsystem* SubSystem)
{
	FPreAnimatedStateEntry         Entry              = MakeEntry(DataLayer);
	EPreAnimatedStorageRequirement StorageRequirement = this->ParentExtension->GetStorageRequirement(Entry);

	if (!IsStorageRequirementSatisfied(Entry.ValueHandle.StorageIndex, StorageRequirement))
	{
		// @todo: If a data layer is loading when Sequencer attempts to activate it,
		// should it return to ::Loading when sequencer is done?
		EDataLayerState ExistingState = SubSystem->GetDataLayerState(DataLayer);

		AssignPreAnimatedValue(Entry.ValueHandle.StorageIndex, StorageRequirement, CopyTemp(ExistingState));
	}
}

#if WITH_EDITOR
void FPreAnimatedDataLayerStorage::SavePreAnimatedStateInEditor(UDataLayer* DataLayer)
{
	FPreAnimatedStateEntry         Entry              = MakeEntry(DataLayer);
	EPreAnimatedStorageRequirement StorageRequirement = this->ParentExtension->GetStorageRequirement(Entry);

	if (!IsStorageRequirementSatisfied(Entry.ValueHandle.StorageIndex, StorageRequirement))
	{
		// We never unload data-layers in editor, so feign currently unloaded layers as loaded
		EDataLayerState ExistingState = (DataLayer->IsVisible() && DataLayer->IsDynamicallyLoadedInEditor()) ? EDataLayerState::Activated : EDataLayerState::Loaded;

		AssignPreAnimatedValue(Entry.ValueHandle.StorageIndex, StorageRequirement, CopyTemp(ExistingState));
	}
}
#endif

// ---------------------------------------------------------------------
// FDataLayerState definitions
void FDataLayerState::Reset()
{
	HierarchicalBias = 0;
	UnloadedCount    = 0;
	LoadedCount      = 0;
	ActivatedCount   = 0;
	bFlushUnloaded   = false;
	bFlushActivated  = false;
}

void FDataLayerState::AddRequest(int16 InBias, EDataLayerState RequestedState, bool bRequiresStreamingFlush)
{
	if (InBias > HierarchicalBias)
	{
		Reset();
		HierarchicalBias = InBias;
	}

	if (InBias == HierarchicalBias)
	{
		switch (RequestedState)
		{
		case EDataLayerState::Unloaded:  ++UnloadedCount;  bFlushUnloaded |= bRequiresStreamingFlush; break;
		case EDataLayerState::Loaded:    ++LoadedCount;    break;
		case EDataLayerState::Activated: ++ActivatedCount; bFlushActivated |= bRequiresStreamingFlush; break;
		}
	}
}

bool FDataLayerState::IsEmpty() const
{
	return (UnloadedCount + LoadedCount + ActivatedCount) == 0;
}

TOptional<EDataLayerState> FDataLayerState::ComputeDesiredState() const
{
	// If we have any requests to keep a layer loaded, always keep it loaded (even if things ask for it to be hidden)
	EDataLayerState FallbackState = LoadedCount != 0 ? EDataLayerState::Loaded : EDataLayerState::Unloaded;

	if (ActivatedCount == UnloadedCount)
	{
		// Equal number of requests for active and unloaded - just leave the data layer alone
		if (LoadedCount != 0)
		{
			return EDataLayerState::Loaded;
		}
		return TOptional<EDataLayerState>();
	}
	
	if (ActivatedCount > UnloadedCount)
	{
		return EDataLayerState::Activated;
	}

	return FallbackState;
}

bool FDataLayerState::ShouldFlushStreaming(EDataLayerState ComputedState) const
{
	switch (ComputedState)
	{
	case EDataLayerState::Unloaded:  return bFlushUnloaded;
	case EDataLayerState::Activated: return bFlushActivated;
	}

	return false;
}

// ---------------------------------------------------------------------
// FDataLayerStates definitions
bool FDesiredLayerStates::IsEmpty() const
{
	return StatesByLayer.Num() == 0;
}

void FDesiredLayerStates::Reset()
{
	for (TTuple<FName, FDataLayerState>& Pair : StatesByLayer)
	{
		Pair.Value.Reset();
	}
}

EDataLayerUpdateFlags FDesiredLayerStates::Apply(FPreAnimatedDataLayerStorage* PreAnimatedStorage, UDataLayerSubsystem* DataLayerSubsystem, UWorldPartitionSubsystem* WorldPartitionSubsystem)
{
	EDataLayerUpdateFlags Flags = EDataLayerUpdateFlags::None;

	auto IsDataLayerReady = [WorldPartitionSubsystem](UDataLayer* DataLayer, EDataLayerState DesireState, bool bExactState)
	{
		EWorldPartitionRuntimeCellState QueryState;
		switch (DesireState)
		{
		case EDataLayerState::Activated:
			QueryState = EWorldPartitionRuntimeCellState::Activated;
			break;
		case EDataLayerState::Loaded:
			QueryState = EWorldPartitionRuntimeCellState::Loaded;
			break;
		case EDataLayerState::Unloaded:
			QueryState = EWorldPartitionRuntimeCellState::Unloaded;
			break;
		default:
			UE_LOG(LogMovieScene, Error, TEXT("Unkown data layer state"));
			return true;
		}

		FWorldPartitionStreamingQuerySource QuerySource;
		QuerySource.bDataLayersOnly = true;
		QuerySource.bSpatialQuery = false; // @todo_ow: how would we support spatial query from sequencer?
		QuerySource.DataLayers.Add(DataLayer->GetFName());
		return WorldPartitionSubsystem->IsStreamingCompleted(QueryState, { QuerySource }, bExactState);
	};

	for (auto It = StatesByLayer.CreateIterator(); It; ++It)
	{
		const FDataLayerState& StateValue = It.Value();
		if (StateValue.IsEmpty())
		{
			It.RemoveCurrent();
			continue;
		}

		if (TOptional<EDataLayerState> DesiredState = StateValue.ComputeDesiredState())
		{
			UDataLayer* DataLayer = DataLayerSubsystem->GetDataLayerFromName(It.Key());
			if (DataLayer)
			{
				if (PreAnimatedStorage)
				{
					PreAnimatedStorage->SavePreAnimatedState(DataLayer, DataLayerSubsystem);
				}

				const EDataLayerState DesiredStateValue = DesiredState.GetValue();
				DataLayerSubsystem->SetDataLayerState(DataLayer, DesiredStateValue);

				if (StateValue.ShouldFlushStreaming(DesiredStateValue) && !IsDataLayerReady(DataLayer, DesiredStateValue, true))
				{
					// Exception for Full flush is if Desired State is Activated but we are not at least in Loaded state
					if (DesiredStateValue == EDataLayerState::Activated && !IsDataLayerReady(DataLayer, EDataLayerState::Loaded, false))
					{
						Flags |= EDataLayerUpdateFlags::FlushStreamingFull;
						UE_LOG(LogMovieScene, Warning, TEXT("Data layer with name '%s' is causing a full streaming flush"), *DataLayer->GetDataLayerLabel().ToString());
					}
					else
					{
						Flags |= EDataLayerUpdateFlags::FlushStreamingVisibility;
						UE_LOG(LogMovieScene, Log, TEXT("Data layer with name '%s' is causing a visibility streaming flush"), *DataLayer->GetDataLayerLabel().ToString());
					}
				}
			}
			else
			{
				UE_LOG(LogMovieScene, Warning, TEXT("Unable to find data layer with name '%s'"), *It.Key().ToString());
			}
		}
	}

	return Flags;
}

#if WITH_EDITOR
void FDesiredLayerStates::ApplyInEditor(FPreAnimatedDataLayerStorage* PreAnimatedStorage, UDataLayerEditorSubsystem* SubSystem)
{
	TArray<UDataLayer*> DatalayersNeedingLoad;
	TArray<UDataLayer*> DatalayersNeedingShow;
	TArray<UDataLayer*> DatalayersNeedingHide;

	for (auto It = StatesByLayer.CreateIterator(); It; ++It)
	{
		const FDataLayerState& StateValue = It.Value();
		if (StateValue.IsEmpty())
		{
			It.RemoveCurrent();
			continue;
		}

		if (TOptional<EDataLayerState> DesiredState = StateValue.ComputeDesiredState())
		{
			UDataLayer* DataLayer = SubSystem->GetDataLayerFromName(It.Key());
			if (DataLayer)
			{
				if (PreAnimatedStorage)
				{
					PreAnimatedStorage->SavePreAnimatedStateInEditor(DataLayer);
				}

				// In-editor we only ever hide data layers, we never unload them
				switch (DesiredState.GetValue())
				{
				case EDataLayerState::Unloaded:
					DatalayersNeedingHide.Add(DataLayer);
					break;
				case EDataLayerState::Loaded:
					DatalayersNeedingLoad.Add(DataLayer);
					DatalayersNeedingHide.Add(DataLayer);
					break;
				case EDataLayerState::Activated:
					DatalayersNeedingLoad.Add(DataLayer);
					DatalayersNeedingShow.Add(DataLayer);
					break;
				default:
					break;
				}
			}
			else
			{
				UE_LOG(LogMovieScene, Warning, TEXT("Unable to find data layer with name '%s'"), *It.Key().ToString());
			}
		}
	}

	if (DatalayersNeedingLoad.Num() > 0)
	{
		// This blocks while we load data layers
		SubSystem->SetDataLayersIsDynamicallyLoadedInEditor(DatalayersNeedingLoad, true);
	}
	if (DatalayersNeedingShow.Num() > 0)
	{
		SubSystem->SetDataLayersVisibility(DatalayersNeedingShow, true);
	}
	if (DatalayersNeedingHide.Num() > 0)
	{
		SubSystem->SetDataLayersVisibility(DatalayersNeedingHide, false);
	}
}
#endif

void FDesiredLayerStates::ApplyNewState(const FName& InDataLayerName, int16 HierarchicalBias, EDataLayerState DesiredState, bool bRequiresStreamingFlush)
{
	using namespace UE::MovieScene;

	FDataLayerState* LayerState = StatesByLayer.Find(InDataLayerName);
	if (!LayerState)
	{
		LayerState = &StatesByLayer.Add(InDataLayerName, FDataLayerState());
	}

	LayerState->AddRequest(HierarchicalBias, DesiredState, bRequiresStreamingFlush);
}

} // namespace MovieScene
} // namespace UE

UMovieSceneDataLayerSystem::UMovieSceneDataLayerSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();

	Phase = UE::MovieScene::ESystemPhase::Spawn;
	RelevantComponent = TracksComponents->DataLayer;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(GetClass(), UMovieSceneSpawnablesSystem::StaticClass());
	}
	else
	{
		DesiredLayerStates = MakeShared<FDesiredLayerStates>();

		// We only need to run if there are data layer components that need (un)linking
		ApplicableFilter.Filter.All({ TracksComponents->DataLayer });
		ApplicableFilter.Filter.Any({ BuiltInComponents->Tags.NeedsLink, BuiltInComponents->Tags.NeedsUnlink });
	}
}

void UMovieSceneDataLayerSystem::OnLink()
{
}

bool UMovieSceneDataLayerSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	return InLinker->EntityManager.ContainsComponent(RelevantComponent) || (DesiredLayerStates && !DesiredLayerStates->IsEmpty());
}

void UMovieSceneDataLayerSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	// Only run if we must
	UWorld* World = GetWorld();

	if (!World || !ApplicableFilter.Matches(Linker->EntityManager))
	{
		return;
	}

	// Update the desired states of all data layers from the entity manager
	UpdateDesiredStates();

	// In-editor we apply desired states through the editor sub-system
#if WITH_EDITOR
	if (World->WorldType == EWorldType::Editor)
	{
		UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get();
		if (ensureMsgf(DataLayerEditorSubsystem, TEXT("Unable to retrieve data layer editor subsystem - data layer tracks will not function correctly")))
		{
			DesiredLayerStates->ApplyInEditor(WeakPreAnimatedStorage.Pin().Get(), DataLayerEditorSubsystem);
		}
	}
	else
#endif

	// Outside of editor, or in PIE, we use the runtime data layer sub-system
	{
		UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
		if (ensureMsgf(WorldPartitionSubsystem, TEXT("Unable to retrieve world partition subsystem - data layer tracks will not function correctly")))
		{
			UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>();
			if (ensureMsgf(DataLayerSubsystem, TEXT("Unable to retrieve data layer subsystem - data layer tracks will not function correctly")))
			{
				EDataLayerUpdateFlags UpdateFlags = DesiredLayerStates->Apply(WeakPreAnimatedStorage.Pin().Get(), DataLayerSubsystem, WorldPartitionSubsystem);

				if (EnumHasAnyFlags(UpdateFlags, EDataLayerUpdateFlags::FlushStreamingFull))
				{
					World->BlockTillLevelStreamingCompleted();
				}
				else if (EnumHasAnyFlags(UpdateFlags, EDataLayerUpdateFlags::FlushStreamingVisibility))
				{
					// Make sure any DataLayer state change is processed before flushing visibility
					WorldPartitionSubsystem->UpdateStreamingState();
					World->FlushLevelStreaming(EFlushLevelStreamingType::Visibility);
				}
			}
		}
	}
}

void UMovieSceneDataLayerSystem::UpdateDesiredStates()
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*          BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents  = FMovieSceneTracksComponentTypes::Get();

	BeginTrackingEntities();

	// Reset the container and re-harvest all active states from the entity manager
	DesiredLayerStates->Reset();

	auto GatherDataLayers = [this, BuiltInComponents](FEntityAllocationIteratorItem Item, const FMovieSceneDataLayerComponentData* ComponentData, const int16* OptHBiases)
	{
		const bool bPreroll = Item.GetAllocationType().Contains(BuiltInComponents->Tags.PreRoll);
		for (int32 Index = 0; Index < Item.GetAllocation()->Num(); ++Index)
		{
			const UMovieSceneDataLayerSection* Section = ComponentData[Index].Section.Get();
			if (!ensure(Section))
			{
				continue;
			}

			const bool bRequiresStreamingFlush = bPreroll == false;
			EDataLayerState DesiredState = bPreroll ? Section->GetPrerollState() : Section->GetDesiredState();

			for (const FActorDataLayer& ActorDataLayer : Section->GetDataLayers())
			{
				this->DesiredLayerStates->ApplyNewState(ActorDataLayer.Name, OptHBiases ? OptHBiases[Index] : 0, DesiredState, bRequiresStreamingFlush);
			}
		}
	};

	FEntityTaskBuilder()
	.Read(TracksComponents->DataLayer)
	.ReadOptional(BuiltInComponents->HierarchicalBias)
	.FilterNone({ BuiltInComponents->Tags.NeedsUnlink })  // Do not iterate things that are being destroyed
	.Iterate_PerAllocation(&Linker->EntityManager, GatherDataLayers);
}

void UMovieSceneDataLayerSystem::BeginTrackingEntities()
{
	using namespace UE::MovieScene;

	UWorld*                     World              = GetWorld();
	UDataLayerSubsystem*        DataLayerSubsystem = World ? World->GetSubsystem<UDataLayerSubsystem>() : nullptr;
	FPreAnimatedStateExtension* PreAnimatedState   = Linker->FindExtension<FPreAnimatedStateExtension>();

	if (!DataLayerSubsystem || !PreAnimatedState)
	{
		return;
	}

	FBuiltInComponentTypes*          BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents  = FMovieSceneTracksComponentTypes::Get();
	FPreAnimatedEntityCaptureSource* EntityMetaData    = PreAnimatedState->GetOrCreateEntityMetaData();

	// Cache the preanimated storage
	TSharedPtr<FPreAnimatedDataLayerStorage> PreAnimatedStorage = PreAnimatedState->GetOrCreateStorage<FPreAnimatedDataLayerStorage>();
	WeakPreAnimatedStorage = PreAnimatedStorage;

	// ---------------------------------------------------------------------------------
	// Only gather entity meta-data during SavePreAnimatedState - the actual values will be cached
	// inside FDataLayerState::Apply
	auto GatherDataLayers = [this, EntityMetaData, DataLayerSubsystem, PreAnimatedStorage](
		FEntityAllocationIteratorItem Item,
		TRead<FMovieSceneEntityID> EntityIDs,
		TRead<FInstanceHandle> RootInstanceHandles,
		TRead<FMovieSceneDataLayerComponentData> ComponentData)
	{
		const bool bRestoreState = Item.GetAllocationType().Contains(FBuiltInComponentTypes::Get()->Tags.RestoreState);
		for (int32 Index = 0; Index < Item.GetAllocation()->Num(); ++Index)
		{
			const UMovieSceneDataLayerSection* Section = ComponentData[Index].Section.Get();
			if (Section)
			{
				FMovieSceneEntityID EntityID     = EntityIDs[Index];
				FInstanceHandle     RootInstance = RootInstanceHandles[Index];

				for (const FActorDataLayer& ActorDataLayer : Section->GetDataLayers())
				{
					UDataLayer* DataLayer = DataLayerSubsystem->GetDataLayer(ActorDataLayer);
					if (DataLayer)
					{
						FPreAnimatedStateEntry Entry = PreAnimatedStorage->MakeEntry(DataLayer);
						EntityMetaData->BeginTrackingEntity(Entry, EntityID, RootInstance, bRestoreState);
					}
				}
			}
		}
	};

	// Iterate any data layer components that need link
	FEntityTaskBuilder()
	.ReadEntityIDs()
	.Read(BuiltInComponents->RootInstanceHandle)
	.Read(TracksComponents->DataLayer)
	.FilterAll({ BuiltInComponents->Tags.NeedsLink })
	.Iterate_PerAllocation(&Linker->EntityManager, GatherDataLayers);
}

