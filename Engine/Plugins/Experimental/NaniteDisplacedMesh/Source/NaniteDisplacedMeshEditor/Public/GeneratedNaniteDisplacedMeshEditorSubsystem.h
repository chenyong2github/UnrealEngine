// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"

#include "Delegates/IDelegateInstance.h"
#include "GameFramework/Actor.h"
#include "UObject/Object.h"
#include "UObject/ObjectKey.h"

#include "GeneratedNaniteDisplacedMeshEditorSubsystem.generated.h"

struct FNaniteDisplacedMeshParams;
struct FPropertyChangedEvent;

template<typename Type>
class TSubclassOf;

UCLASS()
class NANITEDISPLACEDMESHEDITOR_API UGeneratedNaniteDisplacedMeshEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

	/**
	 * Utility functions to setup an automatic update of the level actors that have a generated NaniteDisplacedMesh constructed some assets data.
	 */
public:
	using FOnActorDependencyChanged = TUniqueFunction<void (AActor* /*ActorToUpdate*/, UObject* /*AssetChanged*/, FPropertyChangedEvent& /*PropertyChangedEvent*/)>;
	
	struct FActorClassHandler
	{
		FOnActorDependencyChanged Callback;

		// If empty it will accept any change
		TMap<UClass*, TSet<FProperty*>> PropertiesToWatchPerAssetType;
	};

	/**
	 * Tell the system what to callback when a dependency was changed for an actor the specified type.
	 */
	void RegisterClassHandler(const TSubclassOf<AActor>& ActorClass, FActorClassHandler&& ActorClassHandler);
	void UnregisterClassHandler(const TSubclassOf<AActor>& ActorClass);

	/**
	 * Tell the system to track for change to the dependencies of the actor.
	 * The system will invoke a callback after a change to any asset that this actor has a dependency.
	 */
	void UpdateActorDependencies(AActor* Actor, TArray<TObjectKey<UObject>>&& Dependencies);

	/**
	 * Tell the system to stop tracking stuff for this actor.
	 */
	void RemoveActor(AActor* ActorToRemove);

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void OnObjectPostEditChange(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);
	void OnLevelActorDeleted(AActor* Actor);

	bool CanObjectBeTracked(UObject* InObject);
	bool RemoveActor(const TObjectKey<AActor>& InActorToRemove, uint32 InWeakActorHash);

	FActorClassHandler* FindClassHandler(UClass* Class);
	bool ShouldCallback(UClass* AssetClass, const FActorClassHandler& ClassHandler, const FPropertyChangedEvent& PropertyChangedEvent);
private:
	TMap<UClass*, FActorClassHandler> ActorClassHandlers;
	TMap<TObjectKey<AActor>, TArray<TObjectKey<UObject>>> ActorsToDependencies;
	TMap<TObjectKey<UObject>, TSet<TObjectKey<AActor>>> DependenciesToActors;

	FDelegateHandle OnPostEditChangeHandle;
	FDelegateHandle OnObjectsReplacedHandle;
	FDelegateHandle OnLevelActorDeletedHandle;
};