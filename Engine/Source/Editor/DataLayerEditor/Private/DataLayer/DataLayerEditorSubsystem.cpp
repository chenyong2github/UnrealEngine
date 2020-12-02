// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayerEditorPerProjectUserSettings.h"
#include "Modules/ModuleManager.h"
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
	void OnLevelViewportClientListChanged();
	void OnLevelActorsAdded(AActor* InActor) { DataLayerEditorSubsystem->InitializeNewActorDataLayers(InActor); }

	TSet<FLevelEditorViewportClient*> RegisteredViewportClients;
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
		if (GEditor)
		{
			GEditor->OnLevelViewportClientListChanged().RemoveAll(this);
			RegisteredViewportClients.Reset();
		}
		GEngine->OnLevelActorAdded().RemoveAll(this);
		GEngine->OnLevelActorDeleted().RemoveAll(this);
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
		if (GEditor)
		{
			RegisteredViewportClients.Reset();
			RegisteredViewportClients.Append(GEditor->GetLevelViewportClients());
			for (FLevelEditorViewportClient* ViewportClient : RegisteredViewportClients)
			{
				DataLayerEditorSubsystem->UpdatePerViewVisibility(ViewportClient);
			}
			GEditor->OnLevelViewportClientListChanged().AddRaw(this, &FDataLayersBroadcast::OnLevelViewportClientListChanged);
			GEngine->OnLevelActorAdded().AddRaw(this, &FDataLayersBroadcast::OnLevelActorsAdded);
		}
	}
}

void FDataLayersBroadcast::OnObjectPostEditChange(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (Object)
	{
		bool bRefresh = false;
		if (UDataLayer* DataLayer = Cast<UDataLayer>(Object))
		{
			bRefresh = true;
		}
		else if (AActor* Actor = Cast<AActor>(Object))
		{
			bRefresh = Actor->IsPropertyChangedAffectingDataLayers(PropertyChangedEvent);
		}
		if (bRefresh)
		{
			// Force and update
			DataLayerEditorSubsystem->EditorRefreshDataLayerBrowser();
		}
	}
}

void FDataLayersBroadcast::OnLevelViewportClientListChanged()
{
	if (GEditor)
	{
		TSet<FLevelEditorViewportClient*> NewViewportClients(GEditor->GetLevelViewportClients());
		TSet<FLevelEditorViewportClient*> AddedViewportClients = NewViewportClients.Difference(RegisteredViewportClients);
		TSet<FLevelEditorViewportClient*> RemovedViewportClients = RegisteredViewportClients.Difference(NewViewportClients);
		for (FLevelEditorViewportClient* ViewportClient : AddedViewportClients)
		{
			DataLayerEditorSubsystem->UpdatePerViewVisibility(ViewportClient);
		}
		for (FLevelEditorViewportClient* ViewportClient : RemovedViewportClients)
		{
			DataLayerEditorSubsystem->RemoveViewFromActorViewVisibility(ViewportClient);
		}
		RegisteredViewportClients = NewViewportClients;
	}
}

//////////////////////////////////////////////////////////////////////////
// UDataLayerEditorSubsystem
//
// Note: 
//		- DataLayer visibility currently re-uses Actor's bHiddenEdLayer and HiddenEditorViews. It's viable since Layer & DataLayer ares mutually exclusive systems.
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
	Super::Initialize(Collection);

	// Set up the broadcast functions for DataLayerEditorSubsystem
	DataLayersBroadcast = MakeShareable(new FDataLayersBroadcast(this));

	if (UWorld* World = GetWorld())
	{
		World->PersistentLevel->OnLoadedActorAddedToLevelEvent.AddLambda([this](AActor& InActor) { InitializeNewActorDataLayers(&InActor); });
	}
}

void UDataLayerEditorSubsystem::Deinitialize()
{
	Super::Deinitialize();

	DataLayersBroadcast->Deinitialize();
}

bool UDataLayerEditorSubsystem::RefreshWorldPartitionEditorCells()
{
	if (UWorldPartition* WorldPartition = GetWorld() ? GetWorld()->GetWorldPartition() : nullptr)
	{
		if (!WorldPartition->RefreshLoadedEditorCells())
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
		WorldDataLayers->ForEachDataLayer([&DataLayersNotLoadedInEditor](UDataLayer* DataLayer)
		{
			if (!DataLayer->IsDynamicallyLoadedInEditor())
			{
				DataLayersNotLoadedInEditor.Add(DataLayer->GetFName());
			}
			return true;
		});
		GetMutableDefault<UDataLayerEditorPerProjectUserSettings>()->SetWorldDataLayersNotLoadedInEditor(GetWorld(), DataLayersNotLoadedInEditor);
	}
}

void UDataLayerEditorSubsystem::EditorMapChange()
{
	if (UWorld * World = GetWorld())
	{
		World->PersistentLevel->OnLoadedActorAddedToLevelEvent.AddLambda([this](AActor& InActor) { InitializeNewActorDataLayers(&InActor); });
	}
	DataLayerChanged.Broadcast(EDataLayerAction::Reset, NULL, NAME_None);
}

void UDataLayerEditorSubsystem::EditorRefreshDataLayerBrowser()
{
	DataLayerChanged.Broadcast(EDataLayerAction::Reset, NULL, NAME_None);
	UpdateAllActorsVisibility(false, false);
}

void UDataLayerEditorSubsystem::PostUndoRedo()
{
	DataLayerChanged.Broadcast(EDataLayerAction::Reset, NULL, NAME_None);
	UpdateAllActorsVisibility(true, true);
	RefreshWorldPartitionEditorCells();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on an individual actor.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool UDataLayerEditorSubsystem::IsActorValidForDataLayer(AActor* Actor)
{
	return Actor && Actor->IsValidForDataLayer();
}

void UDataLayerEditorSubsystem::InitializeNewActorDataLayers(AActor* Actor)
{
	if (!IsActorValidForDataLayer(Actor))
	{
		return;
	}

	Actor->FixupDataLayers();

	// update per-view visibility info
	UpdateActorAllViewsVisibility(Actor);

	// update general actor visibility
	bool bActorModified = false;
	bool bActorSelectionChanged = false;
	UpdateActorVisibility(Actor, bActorSelectionChanged, bActorModified, /*bActorNotifySelectionChange*/true, /*bActorRedrawViewports*/false);
}

UWorld* UDataLayerEditorSubsystem::GetWorld() const
{
	return GWorld;
}

bool UDataLayerEditorSubsystem::AddActorToDataLayer(AActor* Actor, const UDataLayer* DataLayer)
{
	TArray<AActor*> Actors;
	Actors.Add(Actor);

	return AddActorsToDataLayers(Actors, {DataLayer});
}

bool UDataLayerEditorSubsystem::AddActorToDataLayers(AActor* Actor, const TArray<const UDataLayer*>& DataLayers)
{
	TArray<AActor*> Actors;
	Actors.Add(Actor);

	return AddActorsToDataLayers(Actors, DataLayers);
}

bool UDataLayerEditorSubsystem::AddActorsToDataLayer(const TArray<AActor*>& Actors, const UDataLayer* DataLayer)
{
	return AddActorsToDataLayers(Actors, {DataLayer});
}

bool UDataLayerEditorSubsystem::AddActorsToDataLayer(const TArray<TWeakObjectPtr<AActor>>& Actors, const UDataLayer* DataLayer)
{
	return AddActorsToDataLayers(Actors, {DataLayer});
}

bool UDataLayerEditorSubsystem::AddActorsToDataLayers(const TArray<AActor*>& Actors, const TArray<const UDataLayer*>& DataLayers)
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
			for (const UDataLayer* DataLayer : DataLayers)
			{
				if (Actor->AddDataLayer(DataLayer))
				{
					bActorWasModified = true;
					ActorDataLayersChanged.Broadcast(Actor);
				}
			}

			if (bActorWasModified)
			{
				// Update per-view visibility info
				UpdateActorAllViewsVisibility(Actor);

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

bool UDataLayerEditorSubsystem::AddActorsToDataLayers(const TArray<TWeakObjectPtr<AActor>>& Actors, const TArray<const UDataLayer*>& DataLayers)
{
	TArray<AActor*> ActorsRawPtr;
	Algo::TransformIf(Actors, ActorsRawPtr, [](const TWeakObjectPtr<AActor>& Actor) { return Actor.IsValid(); }, [](const TWeakObjectPtr<AActor>& Actor) { return Actor.Get(); });

	return AddActorsToDataLayers(ActorsRawPtr, DataLayers);
}

bool UDataLayerEditorSubsystem::RemoveActorFromDataLayer(AActor* Actor, const UDataLayer* DataLayer)
{
	TArray<AActor*> Actors;
	Actors.Add(Actor);

	return RemoveActorsFromDataLayers(Actors, {DataLayer});
}

bool UDataLayerEditorSubsystem::RemoveActorFromDataLayers(AActor* Actor, const TArray<const UDataLayer*>& DataLayers)
{
	TArray<AActor*> Actors;
	Actors.Add(Actor);

	return RemoveActorsFromDataLayers(Actors, DataLayers);
}

bool UDataLayerEditorSubsystem::RemoveActorsFromDataLayer(const TArray<AActor*>& Actors, const UDataLayer* DataLayer)
{
	return RemoveActorsFromDataLayers(Actors, {DataLayer});
}

bool UDataLayerEditorSubsystem::RemoveActorsFromDataLayer(const TArray<TWeakObjectPtr<AActor>>& Actors, const UDataLayer* DataLayer)
{
	return RemoveActorsFromDataLayers(Actors, {DataLayer});
}

bool UDataLayerEditorSubsystem::RemoveActorsFromDataLayers(const TArray<AActor*>& Actors, const TArray<const UDataLayer*>& DataLayers)
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
		for (const UDataLayer* DataLayer : DataLayers)
		{
			if (Actor->RemoveDataLayer(DataLayer))
			{
				bActorWasModified = true;
				DataLayerChanged.Broadcast(EDataLayerAction::Modify, DataLayer, NAME_None);
				ActorDataLayersChanged.Broadcast(Actor);
			}
		}

		if (bActorWasModified)
		{
			// Update per-view visibility info
			UpdateActorAllViewsVisibility(Actor);

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

bool UDataLayerEditorSubsystem::RemoveActorsFromDataLayers(const TArray<TWeakObjectPtr<AActor>>& Actors, const TArray<const UDataLayer*>& DataLayers)
{
	TArray<AActor*> ActorsRawPtr;
	Algo::TransformIf(Actors, ActorsRawPtr, [](const TWeakObjectPtr<AActor>& Actor) { return Actor.IsValid(); }, [](const TWeakObjectPtr<AActor>& Actor) { return Actor.Get(); });

	return RemoveActorsFromDataLayers(ActorsRawPtr, DataLayers);
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

bool UDataLayerEditorSubsystem::AddSelectedActorsToDataLayer(const UDataLayer* DataLayer)
{
	return AddActorsToDataLayer(GetSelectedActors(), DataLayer);
}

bool UDataLayerEditorSubsystem::RemoveSelectedActorsFromDataLayer(const UDataLayer* DataLayer)
{
	return RemoveActorsFromDataLayer(GetSelectedActors(), DataLayer);
}

bool UDataLayerEditorSubsystem::AddSelectedActorsToDataLayers(const TArray<const UDataLayer*>& DataLayers)
{
	return AddActorsToDataLayers(GetSelectedActors(), DataLayers);
}

bool UDataLayerEditorSubsystem::RemoveSelectedActorsFromDataLayers(const TArray<const UDataLayer*>& DataLayers)
{
	return RemoveActorsFromDataLayers(GetSelectedActors(), DataLayers);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on actors in DataLayers
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool UDataLayerEditorSubsystem::SelectActorsInDataLayer(const UDataLayer* DataLayer, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden)
{
	return SelectActorsInDataLayer(DataLayer, bSelect, bNotify, bSelectEvenIfHidden, nullptr);
}

bool UDataLayerEditorSubsystem::SelectActorsInDataLayer(const UDataLayer* DataLayer, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden, const TSharedPtr<FActorFilter>& Filter)
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

bool UDataLayerEditorSubsystem::SelectActorsInDataLayers(const TArray<const UDataLayer*>& DataLayers, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden)
{
	return SelectActorsInDataLayers(DataLayers, bSelect, bNotify, bSelectEvenIfHidden, nullptr);
}

bool UDataLayerEditorSubsystem::SelectActorsInDataLayers(const TArray<const UDataLayer*>& DataLayers, const bool bSelect, const bool bNotify, const bool bSelectEvenIfHidden, const TSharedPtr<FActorFilter>& Filter)
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

		for (const UDataLayer* DataLayer : DataLayers)
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
void UDataLayerEditorSubsystem::UpdatePerViewVisibility(FLevelEditorViewportClient* ViewportClient, const UDataLayer* DataLayerThatChanged)
{
	if (!ViewportClient->GetWorld())
	{
		return;
	}

	// Iterate over all actors, looking for actors in the specified DataLayers.
	const int32 ViewIndex = ViewportClient->ViewIndex;
	for (AActor* Actor : FActorRange(ViewportClient->GetWorld()))
	{
		if (!IsActorValidForDataLayer(Actor))
		{
			continue;
		}

		// If the view has nothing hidden, just quickly mark the actor as visible in this view 
		if (ViewportClient->ViewHiddenDataLayers.Num() == 0)
		{
			// If the actor had this view hidden, then unhide it
			if (Actor->HiddenEditorViews & ((uint64)1 << ViewIndex))
			{
				// Make sure this actor doesn't have the view set
				Actor->HiddenEditorViews &= ~((uint64)1 << ViewIndex);
				Actor->MarkComponentsRenderStateDirty();
			}
		}
		// Else if we were given a name that was changed, only update actors with that name in their DataLayers, otherwise update all actors
		else if (DataLayerThatChanged == nullptr || Actor->ContainsDataLayer(DataLayerThatChanged))
		{
			UpdateActorViewVisibility(ViewportClient, Actor);
		}
	}

	// Make sure we redraw the viewport
	ViewportClient->Invalidate();
}

void UDataLayerEditorSubsystem::UpdateAllViewVisibility(const UDataLayer* DataLayerThatChanged)
{
	// Update all view's hidden DataLayers if they had this one
	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		UpdatePerViewVisibility(ViewportClient, DataLayerThatChanged);
	}
}

void UDataLayerEditorSubsystem::UpdateActorViewVisibility(FLevelEditorViewportClient* ViewportClient, AActor* Actor, bool bReregisterIfDirty)
{
	const int32 ViewIndex = ViewportClient->ViewIndex;
	uint64 OriginalHiddenViews = Actor->HiddenEditorViews;

	// Update Actor's HiddenEditorViews to reflect ViewHiddenDataLayers
	if (bool bIsHiddenByViewHiddenDataLayers = Actor->HasAnyOfDataLayers(ViewportClient->ViewHiddenDataLayers))
	{
		Actor->HiddenEditorViews |= ((uint64)1 << ViewIndex);
	}
	else
	{
		Actor->HiddenEditorViews &= ~((uint64)1 << ViewIndex);
	}

	// Re-register if we changed the visibility bits, as the rendering thread needs them
	if (bReregisterIfDirty && OriginalHiddenViews != Actor->HiddenEditorViews)
	{
		Actor->MarkComponentsRenderStateDirty();

		// Make sure we redraw the viewport
		ViewportClient->Invalidate();
	}
}

void UDataLayerEditorSubsystem::UpdateActorAllViewsVisibility(AActor* Actor)
{
	uint64 OriginalHiddenViews = Actor->HiddenEditorViews;

	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		// Don't have this reattach, as we can do it once for all views
		UpdateActorViewVisibility(ViewportClient, Actor, false);
	}

	// Re-register if we changed the visibility bits, as the rendering thread needs them
	if (OriginalHiddenViews != Actor->HiddenEditorViews)
	{
		return;
	}

	Actor->MarkComponentsRenderStateDirty();

	// Redraw all viewports
	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		ViewportClient->Invalidate();
	}
}

void UDataLayerEditorSubsystem::RemoveViewFromActorViewVisibility(FLevelEditorViewportClient* ViewportClient)
{
	if (!ViewportClient->GetWorld())
	{
		return;
	}

	// Get the bit for the view index
	const int32 ViewIndex = ViewportClient->ViewIndex;
	uint64 ViewBit = ((uint64)1 << ViewIndex);
	// Get all bits under that that we want to keep
	uint64 KeepBits = ViewBit - 1;

	// Iterate over all actors, looking for actors in the specified DataLayers.
	for (AActor* Actor : FActorRange(ViewportClient->GetWorld()))
	{
		if (!IsActorValidForDataLayer(Actor))
		{
			continue;
		}

		// Remember original bits
		uint64 OriginalHiddenViews = Actor->HiddenEditorViews;
		uint64 Was = Actor->HiddenEditorViews;

		// Slide all bits higher than ViewIndex down one since the view is being removed from Editor
		uint64 LowBits = Actor->HiddenEditorViews & KeepBits;

		// Now slide the top bits down by ViewIndex + 1 (chopping off ViewBit)
		uint64 HighBits = Actor->HiddenEditorViews >> (ViewIndex + 1);
		// Then slide back up by ViewIndex, which will now have erased ViewBit, as well as leaving 0 in the low bits
		HighBits = HighBits << ViewIndex;

		// Put it all back together
		Actor->HiddenEditorViews = LowBits | HighBits;

		// Re-register if we changed the visibility bits, as the rendering thread needs them
		if (OriginalHiddenViews == Actor->HiddenEditorViews)
		{
			continue;
		}

		// Find all registered primitive components and update the scene proxy with the actors updated visibility map
		TInlineComponentArray<UPrimitiveComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Actor->GetComponents())
		{
			UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
			if (PrimitiveComponent && PrimitiveComponent->IsRegistered())
			{
				// Push visibility to the render thread
				PrimitiveComponent->PushEditorVisibilityToProxy(Actor->HiddenEditorViews);
			}
		}
	}
}

static void UpdateBrushDataLayerVisibility(ABrush* Brush, bool bIsHidden)
{
	ULevel* Level = Brush->GetLevel();
	if (!Level)
	{
		return;
	}

	UModel* Model = Level->Model;
	if (!Model)
	{
		return;
	}

	bool bAnySurfaceWasFound = false;
	for (FBspSurf& Surf : Model->Surfs)
	{
		if (Surf.Actor == Brush)
		{
			Surf.bHiddenEdLayer = bIsHidden;
			bAnySurfaceWasFound = true;
		}
	}

	if (bAnySurfaceWasFound)
	{
		Level->UpdateModelComponents();
		Model->InvalidSurfaces = true;
	}
}

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
		// If the actor is also hidden
		if (Actor->bHiddenEdLayer)
		{
			// Actors that don't belong to any DataLayer shouldn't be hidden
			Actor->bHiddenEdLayer = false;
			Actor->MarkComponentsRenderStateDirty();
			bOutActorModified = true;
		}

		return bOutActorModified;
	}

	bool bActorBelongsToVisibleDataLayer = false;
	if (AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		WorldDataLayers->ForEachDataLayer([&Actor, &bOutActorModified, &bActorBelongsToVisibleDataLayer](UDataLayer* DataLayer)
		{
			if (DataLayer->IsVisible() && Actor->ContainsDataLayer(DataLayer))
			{
				if (Actor->bHiddenEdLayer)
				{
					Actor->bHiddenEdLayer = false;
					Actor->MarkComponentsRenderStateDirty();
					bOutActorModified = true;

					if (ABrush* Brush = Cast<ABrush>(Actor))
					{
						UpdateBrushDataLayerVisibility(Brush, /*bIsHidden*/false);
					}
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
		if (!Actor->bHiddenEdLayer)
		{
			Actor->bHiddenEdLayer = true;
			Actor->MarkComponentsRenderStateDirty();
			bOutActorModified = true;

			if (ABrush* Brush = Cast<ABrush>(Actor))
			{
				UpdateBrushDataLayerVisibility(Brush, /*bIsHidden*/false);
			}
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

void UDataLayerEditorSubsystem::AppendActorsFromDataLayer(const UDataLayer* DataLayer, TArray<AActor*>& InOutActors) const
{
	AppendActorsFromDataLayer(DataLayer, InOutActors, nullptr);
}

void UDataLayerEditorSubsystem::AppendActorsFromDataLayer(const UDataLayer* DataLayer, TArray<AActor*>& InOutActors, const TSharedPtr<FActorFilter>& Filter) const
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

void UDataLayerEditorSubsystem::AppendActorsFromDataLayer(const UDataLayer* DataLayer, TArray<TWeakObjectPtr<AActor>>& InOutActors, const TSharedPtr<FActorFilter>& Filter) const
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

void UDataLayerEditorSubsystem::AppendActorsFromDataLayers(const TArray<const UDataLayer*>& DataLayers, TArray<AActor*>& InOutActors) const
{
	AppendActorsFromDataLayers(DataLayers, InOutActors, nullptr);
}

void UDataLayerEditorSubsystem::AppendActorsFromDataLayers(const TArray<const UDataLayer*>& DataLayers, TArray<AActor*>& InOutActors, const TSharedPtr<FActorFilter>& Filter) const
{
	for (AActor* Actor : FActorRange(GetWorld()))
	{
		if (Filter.IsValid() && !Filter->PassesFilter(Actor))
		{
			continue;
		}
		for (const UDataLayer* DataLayer : DataLayers)
		{
			if (Actor->ContainsDataLayer(DataLayer))
			{
				InOutActors.Add(Actor);
				break;
			}
		}
	}
}

void UDataLayerEditorSubsystem::AppendActorsFromDataLayers(const TArray<const UDataLayer*>& DataLayers, TArray<TWeakObjectPtr<AActor>>& InOutActors, const TSharedPtr<FActorFilter>& Filter) const
{
	for (AActor* Actor : FActorRange(GetWorld()))
	{
		if (Filter.IsValid() && !Filter->PassesFilter(Actor))
		{
			continue;
		}
		for (const UDataLayer* DataLayer : DataLayers)
		{
			if (Actor->ContainsDataLayer(DataLayer))
			{
				InOutActors.Add(Actor);
				break;
			}
		}
	}
}

TArray<AActor*> UDataLayerEditorSubsystem::GetActorsFromDataLayer(const UDataLayer* DataLayer) const
{
	TArray<AActor*> OutActors;
	AppendActorsFromDataLayer(DataLayer, OutActors);
	return OutActors;
}

TArray<AActor*> UDataLayerEditorSubsystem::GetActorsFromDataLayer(const UDataLayer* DataLayer, const TSharedPtr<FActorFilter>& Filter) const
{
	TArray<AActor*> OutActors;
	AppendActorsFromDataLayer(DataLayer, OutActors, Filter);
	return OutActors;
}

TArray<AActor*> UDataLayerEditorSubsystem::GetActorsFromDataLayers(const TArray<const UDataLayer*>& DataLayers) const
{
	TArray<AActor*> OutActors;
	AppendActorsFromDataLayers(DataLayers, OutActors);
	return OutActors;
}

TArray<AActor*> UDataLayerEditorSubsystem::GetActorsFromDataLayers(const TArray<const UDataLayer*>& DataLayers, const TSharedPtr<FActorFilter>& Filter) const
{
	TArray<AActor*> OutActors;
	AppendActorsFromDataLayers(DataLayers, OutActors, Filter);
	return OutActors;
}

void UDataLayerEditorSubsystem::SetDataLayerVisibility(UDataLayer* DataLayer, const bool bIsVisible)
{
	SetDataLayersVisibility({ DataLayer }, bIsVisible);
}

void UDataLayerEditorSubsystem::SetDataLayersVisibility(const TArray<UDataLayer*>& DataLayers, const bool bIsVisible)
{
	bool bChangeOccurred = false;
	for (UDataLayer* DataLayer : DataLayers)
	{
		check(DataLayer);

		if (DataLayer->IsVisible() != bIsVisible)
		{
			DataLayer->Modify();
			DataLayer->SetVisible(bIsVisible);
			DataLayerChanged.Broadcast(EDataLayerAction::Modify, DataLayer, "bIsVisible");
			bChangeOccurred = true;
		}
	}

	if (bChangeOccurred)
	{
		UpdateAllActorsVisibility(true, true);
	}
}

void UDataLayerEditorSubsystem::ToggleDataLayerVisibility(UDataLayer* DataLayer)
{
	check(DataLayer);
	SetDataLayerVisibility(DataLayer, !DataLayer->IsVisible());
}

void UDataLayerEditorSubsystem::ToggleDataLayersVisibility(const TArray<UDataLayer*>& DataLayers)
{
	if (DataLayers.Num() == 0)
	{
		return;
	}

	for (UDataLayer* DataLayer : DataLayers)
	{
		DataLayer->Modify();
		DataLayer->SetVisible(!DataLayer->IsVisible());
		DataLayerChanged.Broadcast(EDataLayerAction::Modify, DataLayer, "bIsVisible");
	}

	UpdateAllActorsVisibility(true, true);
}

void UDataLayerEditorSubsystem::MakeAllDataLayersVisible()
{
	if (AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		WorldDataLayers->ForEachDataLayer([this](UDataLayer* DataLayer)
		{
			if (!DataLayer->IsVisible())
			{
				DataLayer->Modify();
				DataLayer->SetVisible(true);
				DataLayerChanged.Broadcast(EDataLayerAction::Modify, DataLayer, "bIsVisible");
			}
			return true;
		});
	}
	
	UpdateAllActorsVisibility(true, true);
}

bool UDataLayerEditorSubsystem::SetDataLayerIsDynamicallyLoadedInternal(UDataLayer* DataLayer, const bool bIsDynamicallyLoaded)
{
	check(DataLayer);
	if (DataLayer->IsDynamicallyLoaded() != bIsDynamicallyLoaded)
	{
		DataLayer->Modify();
		DataLayer->SetIsDynamicallyLoaded(bIsDynamicallyLoaded);
		DataLayerChanged.Broadcast(EDataLayerAction::Modify, DataLayer, "bIsDynamicallyLoaded");
		return true;
	}
	return false;
}

bool UDataLayerEditorSubsystem::SetDataLayerIsDynamicallyLoaded(UDataLayer* DataLayer, const bool bIsDynamicallyLoaded)
{
	bool bRefreshNeeded = SetDataLayerIsDynamicallyLoadedInternal(DataLayer, bIsDynamicallyLoaded);
	return bRefreshNeeded ? RefreshWorldPartitionEditorCells() : true;
}

bool UDataLayerEditorSubsystem::SetDataLayersIsDynamicallyLoaded(const TArray<UDataLayer*>& DataLayers, const bool bIsDynamicallyLoaded)
{
	bool bRefreshNeeded = false;
	for (UDataLayer* DataLayer : DataLayers)
	{
		bRefreshNeeded = bRefreshNeeded || SetDataLayerIsDynamicallyLoadedInternal(DataLayer, bIsDynamicallyLoaded);
	}
	return bRefreshNeeded ? RefreshWorldPartitionEditorCells() : true;
}

bool UDataLayerEditorSubsystem::ToggleDataLayerIsDynamicallyLoaded(UDataLayer* DataLayer)
{
	check(DataLayer);
	return SetDataLayerIsDynamicallyLoaded(DataLayer, !DataLayer->IsDynamicallyLoaded());
}

bool UDataLayerEditorSubsystem::ToggleDataLayersIsDynamicallyLoaded(const TArray<UDataLayer*>& DataLayers)
{
	bool bRefreshNeeded = false;
	for (UDataLayer* DataLayer : DataLayers)
	{
		bRefreshNeeded = bRefreshNeeded || SetDataLayerIsDynamicallyLoadedInternal(DataLayer, !DataLayer->IsDynamicallyLoaded());
	}
	return bRefreshNeeded ? RefreshWorldPartitionEditorCells() : true;
}

bool UDataLayerEditorSubsystem::SetDataLayerIsDynamicallyLoadedInEditorInternal(UDataLayer* DataLayer, const bool bIsDynamicallyLoadedInEditor)
{
	check(DataLayer);
	if (DataLayer->IsDynamicallyLoadedInEditor() != bIsDynamicallyLoadedInEditor)
	{
		DataLayer->Modify(false);
		DataLayer->SetIsDynamicallyLoadedInEditor(bIsDynamicallyLoadedInEditor);
		DataLayerChanged.Broadcast(EDataLayerAction::Modify, DataLayer, "bIsDynamicallyLoadedInEditor");
		return true;
	}
	return false;
}

bool UDataLayerEditorSubsystem::SetDataLayerIsDynamicallyLoadedInEditor(UDataLayer* DataLayer, const bool bIsDynamicallyLoadedInEditor)
{
	bool bRefreshNeeded = SetDataLayerIsDynamicallyLoadedInEditorInternal(DataLayer, bIsDynamicallyLoadedInEditor);
	return bRefreshNeeded ? RefreshWorldPartitionEditorCells() : true;
}

bool UDataLayerEditorSubsystem::SetDataLayersIsDynamicallyLoadedInEditor(const TArray<UDataLayer*>& DataLayers, const bool bIsDynamicallyLoadedInEditor)
{
	bool bRefreshNeeded = false;
	for (UDataLayer* DataLayer : DataLayers)
	{
		bRefreshNeeded = bRefreshNeeded || SetDataLayerIsDynamicallyLoadedInEditorInternal(DataLayer, bIsDynamicallyLoadedInEditor);
	}
	return bRefreshNeeded ? RefreshWorldPartitionEditorCells() : true;
}

bool UDataLayerEditorSubsystem::ToggleDataLayerIsDynamicallyLoadedInEditor(UDataLayer* DataLayer)
{
	check(DataLayer);
	return SetDataLayerIsDynamicallyLoadedInEditor(DataLayer, !DataLayer->IsDynamicallyLoadedInEditor());
}

bool UDataLayerEditorSubsystem::ToggleDataLayersIsDynamicallyLoadedInEditor(const TArray<UDataLayer*>& DataLayers)
{
	bool bRefreshNeeded = false;
	for (UDataLayer* DataLayer : DataLayers)
	{
		bRefreshNeeded = bRefreshNeeded || SetDataLayerIsDynamicallyLoadedInEditorInternal(DataLayer, !DataLayer->IsDynamicallyLoadedInEditor());
	}
	return bRefreshNeeded ? RefreshWorldPartitionEditorCells() : true;
}

const UDataLayer* UDataLayerEditorSubsystem::GetDataLayerFromName(const FName& DataLayerName) const
{
	const AWorldDataLayers* WorldDataLayers = GetWorldDataLayers();
	return WorldDataLayers ? WorldDataLayers->GetDataLayerFromName(DataLayerName) : nullptr;
}

const UDataLayer* UDataLayerEditorSubsystem::GetDataLayerFromLabel(const FName& DataLayerLabel) const
{
	const AWorldDataLayers* WorldDataLayers = GetWorldDataLayers();
	return WorldDataLayers ? WorldDataLayers->GetDataLayerFromLabel(DataLayerLabel) : nullptr;
}

bool UDataLayerEditorSubsystem::TryGetDataLayerFromLabel(const FName& DataLayerLabel, const UDataLayer*& OutDataLayer)
{
	OutDataLayer = GetDataLayerFromLabel(DataLayerLabel);
	return (OutDataLayer != nullptr);
}

const AWorldDataLayers* UDataLayerEditorSubsystem::GetWorldDataLayers() const
{
	return AWorldDataLayers::Get(GetWorld());
}

AWorldDataLayers* UDataLayerEditorSubsystem::GetWorldDataLayers(bool bCreateIfNotFound)
{
	return AWorldDataLayers::Get(GetWorld(), bCreateIfNotFound);
}

void UDataLayerEditorSubsystem::AddAllDataLayersTo(TArray<TWeakObjectPtr<UDataLayer>>& OutDataLayers) const
{
	if (const AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		WorldDataLayers->ForEachDataLayer([&OutDataLayers](UDataLayer* DataLayer)
		{
			OutDataLayers.Add(DataLayer);
			return true;
		});
	}
}

UDataLayer* UDataLayerEditorSubsystem::CreateDataLayer()
{
	AWorldDataLayers* WorldDataLayers = GetWorldDataLayers(/*bCreateIfNotFound*/true);
	UDataLayer* NewDataLayer = WorldDataLayers->CreateDataLayer();
	DataLayerChanged.Broadcast(EDataLayerAction::Add, NewDataLayer, NAME_None);
	return NewDataLayer;
}

void UDataLayerEditorSubsystem::DeleteDataLayers(const TArray<const UDataLayer*>& DataLayersToDelete)
{
	if (AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		if (WorldDataLayers->RemoveDataLayers(DataLayersToDelete))
		{
			DataLayerChanged.Broadcast(EDataLayerAction::Delete, NULL, NAME_None);
		}
	}
}

void UDataLayerEditorSubsystem::DeleteDataLayer(const UDataLayer* DataLayerToDelete)
{
	if (AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
	{
		if (WorldDataLayers->RemoveDataLayer(DataLayerToDelete))
		{
			DataLayerChanged.Broadcast(EDataLayerAction::Delete, NULL, NAME_None);
		}
	}
}

bool UDataLayerEditorSubsystem::RenameDataLayer(UDataLayer* DataLayer, const FName& DataLayerLabel)
{
	if (DataLayer->GetDataLayerLabel() != DataLayerLabel)
	{
		if (AWorldDataLayers* WorldDataLayers = GetWorldDataLayers())
		{
			FName UniqueNewDataLayerLabel = WorldDataLayers->GenerateUniqueDataLayerLabel(DataLayerLabel);
			DataLayer->Modify();
			DataLayer->SetDataLayerLabel(UniqueNewDataLayerLabel);
			DataLayerChanged.Broadcast(EDataLayerAction::Rename, DataLayer, "DataLayerLabel");
			return true;
		}
	}
	return false;
}