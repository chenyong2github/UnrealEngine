// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "WorldPartition/DataLayer/DataLayerInstanceWithAsset.h"
#include "WorldPartition/DataLayer/DeprecatedDataLayerInstance.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"
#include "DataLayerEditorModule.h"
#include "ActorEditorUtils.h"
#include "LevelEditorViewport.h"
#include "Algo/Transform.h"
#include "Misc/IFilter.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Engine/Brush.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Subsystems/ActorEditorContextSubsystem.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"

#define LOCTEXT_NAMESPACE "DataLayer"

//////////////////////////////////////////////////////////////////////////
// FDataLayersBroadcast

class FDataLayersBroadcast
{
public:
	FDataLayersBroadcast(UDataLayerEditorSubsystem* InDataLayerEditorSubsystem);
	~FDataLayersBroadcast();
	void Deinitialize();

private:
	void Initialize();
	void OnEditorMapChange(uint32 MapChangeFlags = 0) { DataLayerEditorSubsystem->EditorMapChange(); }
	void OnPostUndoRedo() { DataLayerEditorSubsystem->PostUndoRedo(); }
	void OnObjectPostEditChange(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
	void OnLevelActorsAdded(AActor* InActor) { DataLayerEditorSubsystem->InitializeNewActorDataLayers(InActor); }
	void OnLevelSelectionChanged(UObject* InObject) { DataLayerEditorSubsystem->OnSelectionChanged(); }

	UDataLayerEditorSubsystem* DataLayerEditorSubsystem;
	bool bIsInitialized;
};

FDataLayersBroadcast::FDataLayersBroadcast(UDataLayerEditorSubsystem* InDataLayerEditorSubsystem)
	: DataLayerEditorSubsystem(InDataLayerEditorSubsystem)
	, bIsInitialized(false)
{
	Initialize();
}

FDataLayersBroadcast::~FDataLayersBroadcast()
{
	Deinitialize();
}

void FDataLayersBroadcast::Deinitialize()
{
	if (bIsInitialized)
	{
		bIsInitialized = false;
		FEditorDelegates::MapChange.RemoveAll(this);
		FEditorDelegates::PostUndoRedo.RemoveAll(this);
		FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
		GEngine->OnLevelActorAdded().RemoveAll(this);
		USelection::SelectionChangedEvent.RemoveAll(this);
		USelection::SelectObjectEvent.RemoveAll(this);
	}
}

void FDataLayersBroadcast::Initialize()
{
	if (!bIsInitialized)
	{
		bIsInitialized = true;
		FEditorDelegates::MapChange.AddRaw(this, &FDataLayersBroadcast::OnEditorMapChange);
		FEditorDelegates::PostUndoRedo.AddRaw(this, &FDataLayersBroadcast::OnPostUndoRedo);
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FDataLayersBroadcast::OnObjectPostEditChange);
		GEngine->OnLevelActorAdded().AddRaw(this, &FDataLayersBroadcast::OnLevelActorsAdded);
		USelection::SelectionChangedEvent.AddRaw(this, &FDataLayersBroadcast::OnLevelSelectionChanged);
		USelection::SelectObjectEvent.AddRaw(this, &FDataLayersBroadcast::OnLevelSelectionChanged);
	}
}

void FDataLayersBroadcast::OnObjectPostEditChange(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (Object && (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive))
	{
		bool bRefresh = false;
		if (Object->IsA<UDataLayerInstance>() || Object->IsA<UDataLayerAsset>())
		{
			bRefresh = true;
		}
		else if (AActor* Actor = Cast<AActor>(Object))
		{
			bRefresh = Actor->IsPropertyChangedAffectingDataLayers(PropertyChangedEvent) || Actor->HasDataLayers();
		}
		if (bRefresh)
		{
			// Force and update
			DataLayerEditorSubsystem->EditorRefreshDataLayerBrowser();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// UDataLayerEditorSubsystem
//
// Note: 
//		- DataLayer visibility currently re-uses Actor's bHiddenEdLayer. It's viable since Layer & DataLayer are mutually exclusive systems.
//		- UDataLayerEditorSubsystem is intended to replace ULayersSubsystem for worlds using the World Partition system.
//		  Extra work is necessary to replace all references to GetEditorSubsystem<ULayersSubsystem> in the Editor.
//		  Either a proxy that redirects calls to the proper EditorSubsystem will be used or user code will change to trigger delegate broadcast instead of directly accessing the subsystem (see calls to InitializeNewActorDataLayers everywhere as an example).
//

UDataLayerEditorSubsystem* UDataLayerEditorSubsystem::Get()
{
	return GEditor ? GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>() : nullptr;
}

void UDataLayerEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UActorEditorContextSubsystem>();

	Super::Initialize(Collection);

	// Set up the broadcast functions for DataLayerEditorSubsystem
	DataLayersBroadcast = MakeShareable(new FDataLayersBroadcast(this));

	if (UWorld* World = GetWorld())
	{
		World->PersistentLevel->OnLoadedActorAddedToLevelEvent.AddLambda([this](AActor& InActor) { InitializeNewActorDataLayers(&InActor); });
	}

	UActorEditorContextSubsystem::Get()->RegisterClient(this);
}

void UDataLayerEditorSubsystem::Deinitialize()
{
	UActorEditorContextSubsystem::Get()->UnregisterClient(this);

	Super::Deinitialize();

	DataLayersBroadcast->Deinitialize();
}

void UDataLayerEditorSubsystem::OnExecuteActorEditorContextAction(UWorld* InWorld, const EActorEditorContextAction& InType, AActor* InActor)
{
	AWorldDataLayers* WorldDataLayer = InWorld->GetWorldDataLayers();
	if (!WorldDataLayer)
	{
		return;
	}

	switch (InType)
	{
	case EActorEditorContextAction::ApplyContext:
		check(InActor && InActor->GetWorld() == InWorld);
		{
			AddActorToDataLayers(InActor, WorldDataLayer->GetActorEditorContextDataLayers());
		}
		break;
	case EActorEditorContextAction::ResetContext:
		for (UDataLayerInstance* DataLayer : WorldDataLayer->GetActorEditorContextDataLayers())
		{
			RemoveFromActorEditorContext(DataLayer);
		}
		break;
	case EActorEditorContextAction::PushContext:
		WorldDataLayer->PushActorEditorContext();
		BroadcastDataLayerChanged(EDataLayerAction::Reset, NULL, NAME_None);
		break;
	case EActorEditorContextAction::PopContext:
		WorldDataLayer->PopActorEditorContext();
		BroadcastDataLayerChanged(EDataLayerAction::Reset, NULL, NAME_None);
		break;
	}
}

bool UDataLayerEditorSubsystem::GetActorEditorContextDisplayInfo(UWorld* InWorld, FActorEditorContextClientDisplayInfo& OutDiplayInfo) const
{
	AWorldDataLayers* WorldDataLayer = InWorld->GetWorldDataLayers();
	if (WorldDataLayer && !WorldDataLayer->GetActorEditorContextDataLayers().IsEmpty())
	{
		OutDiplayInfo.Title = TEXT("Current Data Layers");
		OutDiplayInfo.Brush = FEditorStyle::GetBrush(TEXT("DataLayer.Editor"));
		return true;
	}
	return false;
}

TSharedRef<SWidget> UDataLayerEditorSubsystem::GetActorEditorContextWidget(UWorld* InWorld) const
{
	TSharedRef<SVerticalBox> OutWidget = SNew(SVerticalBox);
	if (AWorldDataLayers* WorldDataLayer = InWorld->GetWorldDataLayers())
	{
		TArray<UDataLayerInstance*> DataLayers = WorldDataLayer->GetActorEditorContextDataLayers();
		for (UDataLayerInstance* DataLayer : DataLayers)
		{
			check(IsValid(DataLayer));
			OutWidget->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 1.0f, 1.0f, 1.0f)
				[
					SNew(SImage)
					.ColorAndOpacity(DataLayer->GetDebugColor())
					.Image(FAppStyle::Get().GetBrush("Level.ColorIcon"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 1.0f, 1.0f, 1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(DataLayer->GetDataLayerShortName()))
				]
			];
		}
	}
	return OutWidget;
}

void UDataLayerEditorSubsystem::AddToActorEditorContext(UDataLayerInstance* InDataLayerInstance)
{
	if (InDataLayerInstance->AddToActorEditorContext())
	{
		BroadcastDataLayerChanged(EDataLayerAction::Modify, InDataLayerInstance, NAME_None);
	}
}

void UDataLayerEditorSubsystem::RemoveFromActorEditorContext(UDataLayerInstance* InDataLayerInstance)
{
	if (InDataLayerInstance->RemoveFromActorEditorContext())
	{
		BroadcastDataLayerChanged(EDataLayerAction::Modify, InDataLayerInstance, NAME_None);
	}
}

bool UDataLayerEditorSubsystem::RefreshWorldPartitionEditorCells(bool bIsFromUserChange)
{
	if (UWorldPartition* WorldPartition = GetWorld() ? GetWorld()->GetWorldPartition() : nullptr)
	{
		if (!WorldPartition->RefreshLoadedEditorCells(bIsFromUserChange))
		{
			return false;
		}
		UpdateDataLayerEditorPerProjectUserSettings();
	}
	return true;
}

void UDataLayerEditorSubsystem::UpdateDataLayerEditorPerProjectUserSettings()
{
	if (AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		TArray<FName> DataLayersNotLoadedInEditor;
		TArray<FName> DataLayersLoadedInEditor;
		WorldDataLayers->GetUserLoadedInEditorStates(DataLayersLoadedInEditor, DataLayersNotLoadedInEditor);
		GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->SetWorldDataLayersNonDefaultEditorLoadStates(GetWorld(), DataLayersLoadedInEditor, DataLayersNotLoadedInEditor);
	}
}

void UDataLayerEditorSubsystem::EditorMapChange()
{
	if (UWorld * World = GetWorld())
	{
		World->PersistentLevel->OnLoadedActorAddedToLevelEvent.AddLambda([this](AActor& InActor) { InitializeNewActorDataLayers(&InActor); });
	}
	BroadcastDataLayerChanged(EDataLayerAction::Reset, NULL, NAME_None);
	UpdateAllActorsVisibility(true, true);
}

void UDataLayerEditorSubsystem::EditorRefreshDataLayerBrowser()
{
	BroadcastDataLayerChanged(EDataLayerAction::Reset, NULL, NAME_None);
	UpdateAllActorsVisibility(false, false);
}

void UDataLayerEditorSubsystem::PostUndoRedo()
{
	BroadcastDataLayerChanged(EDataLayerAction::Reset, NULL, NAME_None);
	UpdateAllActorsVisibility(true, true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on an individual actor.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool UDataLayerEditorSubsystem::IsActorValidForDataLayer(AActor* Actor)
{
	return Actor && Actor->SupportsDataLayer() && Actor->IsValidForDataLayer() && (Actor->GetLevel() == Actor->GetWorld()->PersistentLevel);
}

void UDataLayerEditorSubsystem::InitializeNewActorDataLayers(AActor* Actor)
{
	if (!IsActorValidForDataLayer(Actor))
	{
		return;
	}

	Actor->FixupDataLayers();

	// update general actor visibility
	bool bActorModified = false;
	bool bActorSelectionChanged = false;
	UpdateActorVisibility(Actor, bActorSelectionChanged, bActorModified, /*bActorNotifySelectionChange*/true, /*bActorRedrawViewports*/false);
}

UWorld* UDataLayerEditorSubsystem::GetWorld() const
{
	return GWorld;
}

bool UDataLayerEditorSubsystem::SetParentDataLayer(UDataLayerInstance* DataLayer, UDataLayerInstance* ParentDataLayer)
{
	if (DataLayer->CanParent(ParentDataLayer))
	{
		const bool bIsLoaded = DataLayer->IsEffectiveLoadedInEditor();
		DataLayer->SetParent(ParentDataLayer);
		BroadcastDataLayerChanged(EDataLayerAction::Reset, NULL, NAME_None);
		UpdateAllActorsVisibility(true, true);
		if (bIsLoaded != DataLayer->IsEffectiveLoadedInEditor())
		{
			RefreshWorldPartitionEditorCells(true);
		}
		return true;
	}
	return false;
}

bool UDataLayerEditorSubsystem::AddActorToDataLayer(AActor* Actor, UDataLayerInstance* DataLayer)
{
	TArray<AActor*> Actors;
	Actors.Add(Actor);

	return AddActorsToDataLayers(Actors, {DataLayer});
}

bool UDataLayerEditorSubsystem::AddActorToDataLayers(AActor* Actor, const TArray<UDataLayerInstance*>& DataLayers)
{
	TArray<AActor*> Actors;
	Actors.Add(Actor);

	return AddActorsToDataLayers(Actors, DataLayers);
}

bool UDataLayerEditorSubsystem::AddActorsToDataLayer(const TArray<AActor*>& Actors, UDataLayerInstance* DataLayer)
{
	return AddActorsToDataLayers(Actors, {DataLayer});
}

bool UDataLayerEditorSubsystem::AddActorsToDataLayers(const TArray<AActor*>& Actors, const TArray<UDataLayerInstance*>& DataLayers)
{
	bool bChangesOccurred = false;

	if (DataLayers.Num() > 0)
	{
		GEditor->GetSelectedActors()->BeginBatchSelectOperation();

		for (AActor* Actor : Actors)
		{
			if (!IsActorValidForDataLayer(Actor))
			{
				continue;
			}

			bool bActorWasModified = false;
			for (const UDataLayerInstance* DataLayer : DataLayers)
			{
				if (Actor->AddDataLayer(DataLayer))
				{
					bActorWasModified = true;
					BroadcastActorDataLayersChanged(Actor);
				}
			}

			if (bActorWasModified)
			{
				// Update general actor visibility
				bool bActorModified = false;
				bool bActorSelectionChanged = false;
				UpdateActorVisibility(Actor, bActorSelectionChanged, bActorModified, /*bActorNotifySelectionChange*/true, /*bActorRedrawViewports*/false);

				bChangesOccurred = true;
			}
		}

		GEditor->GetSelectedActors()->EndBatchSelectOperation();
	}

	return bChangesOccurred;
}

bool UDataLayerEditorSubsystem::RemoveActorFromAllDataLayers(AActor* Actor)
{
	return RemoveActorsFromAllDataLayers({ Actor });
}

bool UDataLayerEditorSubsystem::RemoveActorsFromAllDataLayers(const TArray<AActor*>& Actors)
{
	GEditor->GetSelectedActors()->BeginBatchSelectOperation();

	bool bChangesOccurred = false;
	for (AActor* Actor : Actors)
	{
		TArray<const UDataLayerInstance*> ModifiedDataLayerInstances = Actor->GetDataLayerInstances();
		if (Actor->RemoveAllDataLayers())
		{
			for (const UDataLayerInstance* DataLayerInstance : ModifiedDataLayerInstances)
			{
				BroadcastDataLayerChanged(EDataLayerAction::Modify, DataLayerInstance, NAME_None);
			}
			BroadcastActorDataLayersChanged(Actor);

			// Update general actor visibility
			bool bActorModified = false;
			bool bActorSelectionChanged = false;
			UpdateActorVisibility(Actor, bActorSelectionChanged, bActorModified, /*bActorNotifySelectionChange*/true, /*bActorRedrawViewports*/false);

			bChangesOccurred = true;
		}
	}

	GEditor->GetSelectedActors()->EndBatchSelectOperation();

	return bChangesOccurred;
}

bool UDataLayerEditorSubsystem::RemoveActorFromDataLayer(AActor* Actor, UDataLayerInstance* DataLayer)
{
	TArray<AActor*> Actors;
	Actors.Add(Actor);

	return RemoveActorsFromDataLayers(Actors, {DataLayer});
}

bool UDataLayerEditorSubsystem::RemoveActorFromDataLayers(AActor* Actor, const TArray<UDataLayerInstance*>& DataLayers)
{
	TArray<AActor*> Actors;
	Actors.Add(Actor);

	return RemoveActorsFromDataLayers(Actors, DataLayers);
}

bool UDataLayerEditorSubsystem::RemoveActorsFromDataLayer(const TArray<AActor*>& Actors, UDataLayerInstance* DataLayer)
{
	return RemoveActorsFromDataLayers(Actors, {DataLayer});
}

bool UDataLayerEditorSubsystem::RemoveActorsFromDataLayers(const TArray<AActor*>& Actors, const TArray<UDataLayerInstance*>& DataLayers)
{
	GEditor->GetSelectedActors()->BeginBatchSelectOperation();

	bool bChangesOccurred = false;
	for (AActor* Actor : Actors)
	{
		if (!IsActorValidForDataLayer(Actor))
		{
			continue;
		}

		bool bActorWasModified = false;
		for (const UDataLayerInstance* DataLayer : DataLayers)
		{
			if (Actor->RemoveDataLayer(DataLayer))
			{
				bActorWasModified = true;
				BroadcastDataLayerChanged(EDataLayerAction::Modify, DataLayer, NAME_None);
				BroadcastActorDataLayersChanged(Actor);
			}
		}

		if (bActorWasModified)
		{
			// Update general actor visibility
			bool bActorModified = false;
			bool bActorSelectionChanged = false;
			UpdateActorVisibility(Actor, bActorSelectionChanged, bActorModified, /*bActorNotifySelectionChange*/true, /*bActorRedrawViewports*/false);

			bChangesOccurred = true;
		}
	}

	GEditor->GetSelectedActors()->EndBatchSelectOperation();

	return bChangesOccurred;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on selected actors.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TArray<AActor*> UDataLayerEditorSubsystem::GetSelectedActors() const
{
	TArray<AActor*> CurrentlySelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(CurrentlySelectedActors);
	return CurrentlySelectedActors;
}

bool UDataLayerEditorSubsystem::AddSelectedActorsToDataLayer(UDataLayerInstance* DataLayer)
{
	return AddActorsToDataLayer(GetSelectedActors(), DataLayer);
}

bool UDataLayerEditorSubsystem::RemoveSelectedActorsFromDataLayer(UDataLayerInstance* DataLayer)
{
	return RemoveActorsFromDataLayer(GetSelectedActors(), DataLayer);
}

bool UDataLayerEditorSubsystem::AddSelectedActorsToDataLayers(const TArray<UDataLayerInstance*>& DataLayers)
{
	return AddActorsToDataLayers(GetSelectedActors(), DataLayers);
}

bool UDataLayerEditorSubsystem::RemoveSelectedActorsFromDataLayers(const TArray<UDataLayerInstance*>& DataLayers)
{
	return RemoveActorsFromDataLayers(GetSelectedActors(), DataLayers);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on actors in DataLayers
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool UDataLayerEditorSubsystem::SelectActorsInDataLayer(UDataLayerInstance* DataLayer, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden)
{
	return SelectActorsInDataLayer(DataLayer, bSelect, bNotify, bSelectEvenIfHidden, nullptr);
}

bool UDataLayerEditorSubsystem::SelectActorsInDataLayer(UDataLayerInstance* DataLayer, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden, const TSharedPtr<FActorFilter>& Filter)
{
	bool bChangesOccurred = false;

	GEditor->GetSelectedActors()->BeginBatchSelectOperation();
	{
		// Iterate over all actors, looking for actors in the specified DataLayers.
		for (AActor* Actor : FActorRange(GetWorld()))
		{
			if (!IsActorValidForDataLayer(Actor))
			{
				continue;
			}

			if (Filter.IsValid() && !Filter->PassesFilter(Actor))
			{
				continue;
			}

			if (Actor->ContainsDataLayer(DataLayer))
			{
				// The actor was found to be in a specified DataLayer. Set selection state and move on to the next actor.
				bool bNotifyForActor = false;
				GEditor->GetSelectedActors()->Modify();
				GEditor->SelectActor(Actor, bSelect, bNotifyForActor, bSelectEvenIfHidden);
				bChangesOccurred = true;
			}
		}
	}
	GEditor->GetSelectedActors()->EndBatchSelectOperation();

	if (bNotify)
	{
		GEditor->NoteSelectionChange();
	}

	return bChangesOccurred;
}

bool UDataLayerEditorSubsystem::SelectActorsInDataLayers(const TArray<UDataLayerInstance*>& DataLayers, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden)
{
	return SelectActorsInDataLayers(DataLayers, bSelect, bNotify, bSelectEvenIfHidden, nullptr);
}

bool UDataLayerEditorSubsystem::SelectActorsInDataLayers(const TArray<UDataLayerInstance*>& DataLayers, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden, const TSharedPtr<FActorFilter>& Filter)
{
	if (DataLayers.Num() == 0)
	{
		return true;
	}

	GEditor->GetSelectedActors()->BeginBatchSelectOperation();
	bool bChangesOccurred = false;

	// Iterate over all actors, looking for actors in the specified DataLayers.
	for (AActor* Actor : FActorRange(GetWorld()))
	{
		if (!IsActorValidForDataLayer(Actor))
		{
			continue;
		}

		if (Filter.IsValid() && !Filter->PassesFilter(TWeakObjectPtr<AActor>(Actor)))
		{
			continue;
		}

		for (const UDataLayerInstance* DataLayer : DataLayers)
		{
			if (Actor->ContainsDataLayer(DataLayer))
			{
				// The actor was found to be in a specified DataLayer. Set selection state and move on to the next actor.
				bool bNotifyForActor = false;
				GEditor->GetSelectedActors()->Modify();
				GEditor->SelectActor(Actor, bSelect, bNotifyForActor, bSelectEvenIfHidden);
				bChangesOccurred = true;
				break;
			}
		}
	}

	GEditor->GetSelectedActors()->EndBatchSelectOperation();

	if (bNotify)
	{
		GEditor->NoteSelectionChange();
	}

	return bChangesOccurred;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on actor viewport visibility regarding DataLayers
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool UDataLayerEditorSubsystem::UpdateActorVisibility(AActor* Actor, bool& bOutSelectionChanged, bool& bOutActorModified, const bool bNotifySelectionChange, const bool bRedrawViewports)
{
	bOutActorModified = false;
	bOutSelectionChanged = false;

	if (!IsActorValidForDataLayer(Actor))
	{
		return false;
	}

	// If the actor doesn't belong to any DataLayers
	if (!Actor->HasValidDataLayers())
	{
		// Actors that don't belong to any DataLayer shouldn't be hidden
		bOutActorModified = Actor->SetIsHiddenEdLayer(false);
		return bOutActorModified;
	}

	bool bActorBelongsToVisibleDataLayer = false;
	if (AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		WorldDataLayers->ForEachDataLayer([&Actor, &bOutActorModified, &bActorBelongsToVisibleDataLayer](UDataLayerInstance* DataLayer)
		{
			if (DataLayer->IsEffectiveVisible() && Actor->ContainsDataLayer(DataLayer))
			{
				if (Actor->SetIsHiddenEdLayer(false))
				{
					bOutActorModified = true;
				}
				// Stop, because we found at least one visible DataLayer the actor belongs to
				bActorBelongsToVisibleDataLayer = true;
				return false;
			}
			return true;
		});
	}

	// If the actor isn't part of a visible DataLayer, hide and de-select it.
	if (!bActorBelongsToVisibleDataLayer)
	{
		if (Actor->SetIsHiddenEdLayer(true))
		{
			bOutActorModified = true;
		}
		
		// If the actor was selected, mark it as unselected
		if (Actor->IsSelected())
		{
			bool bSelect = false;
			bool bNotify = false;
			bool bIncludeHidden = true;
			GEditor->SelectActor(Actor, bSelect, bNotify, bIncludeHidden);

			bOutSelectionChanged = true;
			bOutActorModified = true;
		}
	}

	if (bNotifySelectionChange && bOutSelectionChanged)
	{
		GEditor->NoteSelectionChange();
	}

	if (bRedrawViewports)
	{
		GEditor->RedrawLevelEditingViewports();
	}

	return bOutActorModified || bOutSelectionChanged;
}

bool UDataLayerEditorSubsystem::UpdateAllActorsVisibility(const bool bNotifySelectionChange, const bool bRedrawViewports)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UDataLayerEditorSubsystem::UpdateAllActorsVisibility);

	bool bSelectionChanged = false;
	bool bChangesOccurred = false;
	for (AActor* Actor : FActorRange(GetWorld()))
	{
		bool bActorModified = false;
		bool bActorSelectionChanged = false;
		bChangesOccurred |= UpdateActorVisibility(Actor, bActorSelectionChanged, bActorModified, /*bActorNotifySelectionChange*/false, /*bActorRedrawViewports*/false);
		bSelectionChanged |= bActorSelectionChanged;
	}

	if (bNotifySelectionChange && bSelectionChanged)
	{
		GEditor->NoteSelectionChange();
	}

	if (bRedrawViewports)
	{
		GEditor->RedrawLevelEditingViewports();
	}

	return bChangesOccurred;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on DataLayers
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void UDataLayerEditorSubsystem::AppendActorsFromDataLayer(UDataLayerInstance* DataLayer, TArray<AActor*>& InOutActors) const
{
	AppendActorsFromDataLayer(DataLayer, InOutActors, nullptr);
}

void UDataLayerEditorSubsystem::AppendActorsFromDataLayer(UDataLayerInstance* DataLayer, TArray<AActor*>& InOutActors, const TSharedPtr<FActorFilter>& Filter) const
{
	for (AActor* Actor : FActorRange(GetWorld()))
	{
		if (Filter.IsValid() && !Filter->PassesFilter(Actor))
		{
			continue;
		}
		if (Actor->ContainsDataLayer(DataLayer))
		{
			InOutActors.Add(Actor);
		}
	}
}

void UDataLayerEditorSubsystem::AppendActorsFromDataLayers(const TArray<UDataLayerInstance*>& DataLayers, TArray<AActor*>& InOutActors) const
{
	AppendActorsFromDataLayers(DataLayers, InOutActors, nullptr);
}

void UDataLayerEditorSubsystem::AppendActorsFromDataLayers(const TArray<UDataLayerInstance*>& DataLayers, TArray<AActor*>& InOutActors, const TSharedPtr<FActorFilter>& Filter) const
{
	for (AActor* Actor : FActorRange(GetWorld()))
	{
		if (Filter.IsValid() && !Filter->PassesFilter(Actor))
		{
			continue;
		}
		for (const UDataLayerInstance* DataLayer : DataLayers)
		{
			if (Actor->ContainsDataLayer(DataLayer))
			{
				InOutActors.Add(Actor);
				break;
			}
		}
	}
}

TArray<AActor*> UDataLayerEditorSubsystem::GetActorsFromDataLayer(UDataLayerInstance* DataLayer) const
{
	TArray<AActor*> OutActors;
	AppendActorsFromDataLayer(DataLayer, OutActors);
	return OutActors;
}

TArray<AActor*> UDataLayerEditorSubsystem::GetActorsFromDataLayer(UDataLayerInstance* DataLayer, const TSharedPtr<FActorFilter>& Filter) const
{
	TArray<AActor*> OutActors;
	AppendActorsFromDataLayer(DataLayer, OutActors, Filter);
	return OutActors;
}

TArray<AActor*> UDataLayerEditorSubsystem::GetActorsFromDataLayers(const TArray<UDataLayerInstance*>& DataLayers) const
{
	TArray<AActor*> OutActors;
	AppendActorsFromDataLayers(DataLayers, OutActors);
	return OutActors;
}

TArray<AActor*> UDataLayerEditorSubsystem::GetActorsFromDataLayers(const TArray<UDataLayerInstance*>& DataLayers, const TSharedPtr<FActorFilter>& Filter) const
{
	TArray<AActor*> OutActors;
	AppendActorsFromDataLayers(DataLayers, OutActors, Filter);
	return OutActors;
}

void UDataLayerEditorSubsystem::SetDataLayerVisibility(UDataLayerInstance* DataLayer, const bool bIsVisible)
{
	SetDataLayersVisibility({ DataLayer }, bIsVisible);
}

void UDataLayerEditorSubsystem::SetDataLayersVisibility(const TArray<UDataLayerInstance*>& DataLayers, const bool bIsVisible)
{
	bool bChangeOccurred = false;
	for (UDataLayerInstance* DataLayer : DataLayers)
	{
		check(DataLayer);

		if (DataLayer->IsVisible() != bIsVisible)
		{
			DataLayer->Modify(/*bAlswaysMarkDirty*/false);
			DataLayer->SetVisible(bIsVisible);
			BroadcastDataLayerChanged(EDataLayerAction::Modify, DataLayer, "bIsVisible");
			bChangeOccurred = true;
		}
	}

	if (bChangeOccurred)
	{
		UpdateAllActorsVisibility(true, true);
	}
}

void UDataLayerEditorSubsystem::ToggleDataLayerVisibility(UDataLayerInstance* DataLayer)
{
	check(DataLayer);
	SetDataLayerVisibility(DataLayer, !DataLayer->IsVisible());
}

void UDataLayerEditorSubsystem::ToggleDataLayersVisibility(const TArray<UDataLayerInstance*>& DataLayers)
{
	if (DataLayers.Num() == 0)
	{
		return;
	}

	for (UDataLayerInstance* DataLayer : DataLayers)
	{
		DataLayer->Modify();
		DataLayer->SetVisible(!DataLayer->IsVisible());
		BroadcastDataLayerChanged(EDataLayerAction::Modify, DataLayer, "bIsVisible");
	}

	UpdateAllActorsVisibility(true, true);
}

void UDataLayerEditorSubsystem::MakeAllDataLayersVisible()
{
	if (AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		WorldDataLayers->ForEachDataLayer([this](UDataLayerInstance* DataLayer)
		{
			if (!DataLayer->IsVisible())
			{
				DataLayer->Modify();
				DataLayer->SetVisible(true);
				BroadcastDataLayerChanged(EDataLayerAction::Modify, DataLayer, "bIsVisible");
			}
			return true;
		});
	}
	
	UpdateAllActorsVisibility(true, true);
}

bool UDataLayerEditorSubsystem::SetDataLayerIsLoadedInEditorInternal(UDataLayerInstance* DataLayer, const bool bIsLoadedInEditor, const bool bIsFromUserChange)
{
	check(DataLayer);
	if (DataLayer->IsLoadedInEditor() != bIsLoadedInEditor)
	{
		const bool bWasVisible = DataLayer->IsEffectiveVisible();

		DataLayer->Modify(false);
		DataLayer->SetIsLoadedInEditor(bIsLoadedInEditor, /*bFromUserChange*/bIsFromUserChange);
		BroadcastDataLayerChanged(EDataLayerAction::Modify, DataLayer, "bIsLoadedInEditor");

		if (DataLayer->IsEffectiveVisible() != bWasVisible)
		{
			UpdateAllActorsVisibility(true, true);
		}
		return true;
	}
	return false;
}

bool UDataLayerEditorSubsystem::SetDataLayerIsLoadedInEditor(UDataLayerInstance* DataLayer, const bool bIsLoadedInEditor, const bool bIsFromUserChange)
{
	bool bRefreshNeeded = SetDataLayerIsLoadedInEditorInternal(DataLayer, bIsLoadedInEditor, bIsFromUserChange);
	return bRefreshNeeded ? RefreshWorldPartitionEditorCells(bIsFromUserChange) : true;
}

bool UDataLayerEditorSubsystem::SetDataLayersIsLoadedInEditor(const TArray<UDataLayerInstance*>& DataLayers, const bool bIsLoadedInEditor, const bool bIsFromUserChange)
{
	bool bRefreshNeeded = false;
	for (UDataLayerInstance* DataLayer : DataLayers)
	{
		bRefreshNeeded |= SetDataLayerIsLoadedInEditorInternal(DataLayer, bIsLoadedInEditor, bIsFromUserChange);
	}
	return bRefreshNeeded ? RefreshWorldPartitionEditorCells(bIsFromUserChange) : true;
}

bool UDataLayerEditorSubsystem::ToggleDataLayerIsLoadedInEditor(UDataLayerInstance* DataLayer, const bool bIsFromUserChange)
{
	check(DataLayer);
	return SetDataLayerIsLoadedInEditor(DataLayer, !DataLayer->IsLoadedInEditor(), bIsFromUserChange);
}

bool UDataLayerEditorSubsystem::ToggleDataLayersIsLoadedInEditor(const TArray<UDataLayerInstance*>& DataLayers, const bool bIsFromUserChange)
{
	bool bRefreshNeeded = false;
	for (UDataLayerInstance* DataLayer : DataLayers)
	{
		bRefreshNeeded |= SetDataLayerIsLoadedInEditorInternal(DataLayer, !DataLayer->IsLoadedInEditor(), bIsFromUserChange);
	}
	return bRefreshNeeded ? RefreshWorldPartitionEditorCells(bIsFromUserChange) : true;
}

bool UDataLayerEditorSubsystem::ResetUserSettings()
{
	bool bRefreshNeeded = false;
	if (const AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		WorldDataLayers->ForEachDataLayer([this, &bRefreshNeeded](UDataLayerInstance* DataLayer)
		{
			bRefreshNeeded |= SetDataLayerIsLoadedInEditorInternal(DataLayer, DataLayer->IsInitiallyLoadedInEditor(), true);
			return true;
		});
	}
	return bRefreshNeeded ? RefreshWorldPartitionEditorCells(true) : true;
}

bool UDataLayerEditorSubsystem::HasDeprecatedDataLayers() const
{
	if (const AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		return WorldDataLayers->HasDeprecatedDataLayers();
	}

	return false;
}

UDataLayerInstance* UDataLayerEditorSubsystem::GetDataLayerInstance(const FName& DataLayerInstanceName) const
{
	const AWorldDataLayers* WorldDataLayers = GetWorldDataLayers();
	return WorldDataLayers ? const_cast<UDataLayerInstance*>(WorldDataLayers->GetDataLayerInstance(DataLayerInstanceName)) : nullptr;
}

UDataLayerInstance* UDataLayerEditorSubsystem::GetDataLayerInstance(const UDataLayerAsset* DataLayerAsset) const
{
	const AWorldDataLayers* WorldDataLayers = GetWorldDataLayers();
	return WorldDataLayers ? const_cast<UDataLayerInstance*>(WorldDataLayers->GetDataLayerInstance(DataLayerAsset)) : nullptr;
}

const AWorldDataLayers* UDataLayerEditorSubsystem::GetWorldDataLayers() const
{
	return GetWorld()->GetWorldDataLayers();
}

AWorldDataLayers* UDataLayerEditorSubsystem::GetWorldDataLayers(bool bCreateIfNotFound)
{
	AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers();

	if (!WorldDataLayers && bCreateIfNotFound)
	{
		WorldDataLayers = AWorldDataLayers::Create(GetWorld());
	}

	return WorldDataLayers;
}

void UDataLayerEditorSubsystem::AddAllDataLayersTo(TArray<TWeakObjectPtr<UDataLayerInstance>>& OutDataLayers) const
{
	if (const AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		WorldDataLayers->ForEachDataLayer([&OutDataLayers](UDataLayerInstance* DataLayer)
			{
				OutDataLayers.Add(DataLayer);
				return true;
			});
	}
}

UDataLayerInstance* UDataLayerEditorSubsystem::CreateDataLayer(UDataLayerInstance* ParentDataLayer)
{
	UDataLayerInstance* NewDataLayer = nullptr;


	AWorldDataLayers* WorldDataLayers = GetWorldDataLayers(/*bCreateIfNotFound*/true);
	if (!WorldDataLayers->HasDeprecatedDataLayers())
	{
		bool bContinuePrompt = true;
		while (bContinuePrompt)
		{
			FAssetData AssetData;
			bContinuePrompt = PromptDataLayerAssetSelection(AssetData);
			if (bContinuePrompt)
			{
				UDataLayerAsset* DataLayerAsset = CastChecked<UDataLayerAsset>(AssetData.GetAsset());
				if (UDataLayerInstance* DataLayerInstance = GetDataLayerInstance(DataLayerAsset))
				{
					FText WarningMessage(LOCTEXT("CreateDataLayerAlreadyExist", "A Data Layer of that asset already exists. Pick another Data Layer?"));
					bContinuePrompt = FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage) == EAppReturnType::Yes;
				}
				else if (ParentDataLayer != nullptr && ParentDataLayer->GetType() != DataLayerAsset->GetType())
				{
					FText WarningMessage(LOCTEXT("CreateDataLayerDiffType", "The parent data layer is of a different type. Pick another Data Layer?"));
					bContinuePrompt = FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage) == EAppReturnType::Yes;
				}
				else
				{
					NewDataLayer = WorldDataLayers->CreateDataLayer<UDataLayerInstanceWithAsset>(DataLayerAsset);
					bContinuePrompt = false;
				}
			}
		}
	}
	else
	{
		NewDataLayer = WorldDataLayers->CreateDataLayer<UDeprecatedDataLayerInstance>();
	}

	if (NewDataLayer != nullptr)
	{
		BroadcastDataLayerChanged(EDataLayerAction::Add, NewDataLayer, NAME_None);
		SetParentDataLayer(NewDataLayer, ParentDataLayer);
	}
	
	return NewDataLayer;
}

void UDataLayerEditorSubsystem::DeleteDataLayers(const TArray<UDataLayerInstance*>& DataLayersToDelete)
{
	if (AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		if (WorldDataLayers->RemoveDataLayers(DataLayersToDelete))
		{
			BroadcastDataLayerChanged(EDataLayerAction::Delete, NULL, NAME_None);
		}
	}
}

void UDataLayerEditorSubsystem::DeleteDataLayer(UDataLayerInstance* DataLayerToDelete)
{
	if (AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		if (WorldDataLayers->RemoveDataLayer(DataLayerToDelete))
		{
			BroadcastDataLayerChanged(EDataLayerAction::Delete, NULL, NAME_None);
		}
	}
}

void UDataLayerEditorSubsystem::BroadcastActorDataLayersChanged(const TWeakObjectPtr<AActor>& ChangedActor)
{
	RebuildSelectedDataLayersFromEditorSelection();
	ActorDataLayersChanged.Broadcast(ChangedActor);
}

void UDataLayerEditorSubsystem::BroadcastDataLayerChanged(const EDataLayerAction Action, const TWeakObjectPtr<const UDataLayerInstance>& ChangedDataLayer, const FName& ChangedProperty)
{
	RebuildSelectedDataLayersFromEditorSelection();
	DataLayerChanged.Broadcast(Action, ChangedDataLayer, ChangedProperty);
	ActorEditorContextClientChanged.Broadcast(this);
}

void UDataLayerEditorSubsystem::OnSelectionChanged()
{
	RebuildSelectedDataLayersFromEditorSelection();
}

void UDataLayerEditorSubsystem::RebuildSelectedDataLayersFromEditorSelection()
{
	SelectedDataLayersFromEditorSelection.Reset();

	TArray<AActor*> Actors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(Actors);
	for (const AActor* Actor : Actors)
	{
		for (const UDataLayerInstance* DataLayerInstance : Actor->GetDataLayerInstances())
		{
			SelectedDataLayersFromEditorSelection.Add(DataLayerInstance);
		}
	}
}


bool UDataLayerEditorSubsystem::PromptDataLayerAssetSelection(FAssetData& OutAsset) const
{
	IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	FOpenAssetDialogConfig Config;
	Config.bAllowMultipleSelection = false;
	Config.AssetClassNames.Add(UDataLayerAsset::StaticClass()->GetFName());
	Config.DefaultPath = PickDataLayerDialogPath;
	Config.DialogTitleOverride = LOCTEXT("PickDataLayerAssetDialogTitle", "Pick A Data Layer Asset");

	TArray<FAssetData> Assets = ContentBrowserSingleton.CreateModalOpenAssetDialog(Config);
	if (Assets.Num() == 1)
	{
		OutAsset = Assets[0];
		OutAsset.PackagePath.ToString(PickDataLayerDialogPath);
		return true;
	}

	return false;
}

//~ Begin Deprecated

PRAGMA_DISABLE_DEPRECATION_WARNINGS

bool UDataLayerEditorSubsystem::RenameDataLayer(UDataLayerInstance* DataLayerInstance, const FName& InDataLayerLabel)
{
	if (DataLayerInstance->SupportRelabeling())
	{
		if (DataLayerInstance->RelabelDataLayer(InDataLayerLabel))
		{
			BroadcastDataLayerChanged(EDataLayerAction::Rename, DataLayerInstance, "DataLayerLabel");
			return true;
		}	
	}

	return false;
}

bool UDataLayerEditorSubsystem::TryGetDataLayerFromLabel(const FName& DataLayerLabel, UDataLayerInstance*& OutDataLayer) const
{
	OutDataLayer = GetDataLayerFromLabel(DataLayerLabel);
	return (OutDataLayer != nullptr);
}

UDataLayerInstance* UDataLayerEditorSubsystem::GetDataLayerFromLabel(const FName& DataLayerLabel) const
{
	const AWorldDataLayers* WorldDataLayers = GetWorldDataLayers();
	return WorldDataLayers ? const_cast<UDataLayerInstance*>(WorldDataLayers->GetDataLayerFromLabel(DataLayerLabel)) : nullptr;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

UDataLayerInstance* UDataLayerEditorSubsystem::GetDataLayer(const FActorDataLayer& ActorDataLayer) const
{
	return GetDataLayerInstance(ActorDataLayer.Name);
}

//~ End Deprecated

#undef LOCTEXT_NAMESPACE