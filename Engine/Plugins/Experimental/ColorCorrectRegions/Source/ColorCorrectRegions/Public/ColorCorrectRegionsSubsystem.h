// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Subsystems/EngineSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "ColorCorrectRegion.h"
#include "ColorCorrectRegionsSceneViewExtension.h"

#if WITH_EDITOR
#include "EditorUndoClient.h"
#endif

#include "ColorCorrectRegionsSubsystem.generated.h"

/**
 * Conditional inheritance to allow UColorCorrectRegionsSubsystem to inherit/avoid Editor's Undo/Redo in Editor/Game modes.
 */ 
#if WITH_EDITOR
class FColorCorrectRegionsEditorUndoClient : public FEditorUndoClient
{
};
#else
class FColorCorrectRegionsEditorUndoClient
{
};
#endif

/**
 * World Subsystem responsible for managing AColorCorrectRegion classes in level.
 * This subsystem handles:
 *		Level Loaded, Undo/Redo, Added to level, Removed from level events.
 * Unfortunately AActor class itself is not aware of when it is added/removed, Undo/Redo etc in the level.
 * 
 * This is the only way (that we found) that was handling all region aggregation cases in more or less efficient way.
 *		Covered cases: Region added to a level, deleted from level, level loaded, undo, redo, level closed, editor closed:
 *		World subsystem keeps track of all Regions in a level via three events OnLevelActorAdded, OnLevelActorDeleted, OnLevelActorListChanged.
 *		Actor classes are unaware of when they are added/deleted/undo/redo etc in the level, therefore this is the best place to manage this.
 * Alternative strategies (All tested):
 *		World's AddOnActorSpawnedHandler. Flawed. Invoked in some cases we don't need, but does not get called during UNDO/REDO
 *		AActor's PostSpawnInitialize, PostActorCreated  and OnConstruction are also flawed.
 *		AActor does not have an internal event for when its deleted (EndPlay is the closest we have).
 */
UCLASS()
class UColorCorrectRegionsSubsystem : public UWorldSubsystem, public FColorCorrectRegionsEditorUndoClient
{
	GENERATED_BODY()
public:

	// Subsystem Init/Deinit
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Undo/redo is only supported by editor.
#if WITH_EDITOR
	// FEditorUndoClient pure virtual methods.
	virtual void PostUndo(bool bSuccess) override { RefreshRegions(); };
	virtual void PostRedo(bool bSuccess) override { RefreshRegions(); }
#endif

public:

	/** A callback for CC Region creation. */
	void OnActorSpawned(AActor* InActor);

	/** A callback for CC Region deletion. */
	void OnActorDeleted(AActor* InActor);

	/** Called when level is added or removed. */
	void OnLevelsChanged() { RefreshRegions(); };
	
#if WITH_EDITOR
	/** A callback for when the level is loaded. */
	void OnLevelActorListChanged() { RefreshRegions(); };
#endif
	
	/** Sorts regions based on priority. */
	void SortRegionsByPriority();

private:

	/** Repopulates array of region actors. */
	void RefreshRegions();

public:

	/** Stores pointers to all ColorCorrectRegion Actors. */
	TArray<AColorCorrectRegion*> Regions;

private:

	/** Region class. Used for getting all region actors in level. */
	TSubclassOf<AColorCorrectRegion> RegionClass;

	TSharedPtr< class FColorCorrectRegionsSceneViewExtension, ESPMode::ThreadSafe > PostProcessSceneViewExtension;

	FCriticalSection RegionAccessCriticalSection;

public:
	friend class FColorCorrectRegionsSceneViewExtension;
};