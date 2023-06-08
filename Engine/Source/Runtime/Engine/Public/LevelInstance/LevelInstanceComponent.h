// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WorldPartition/Filter/WorldPartitionActorFilter.h"
#include "WorldPartition/WorldPartitionActorContainerID.h"

#include "LevelInstanceComponent.generated.h"

/**
 * ULevelInstanceComponent subclasses USceneComponent for Editing purposes so that we can have a proxy to the LevelInstanceActor's RootComponent transform without attaching to it.
 *
 * It is responsible for updating the transform of the ALevelInstanceEditorInstanceActor which is created when loading a LevelInstance Instance Level
 *
 * We use this method to avoid attaching the Instance Level Actors to the ILevelInstanceInterface. (Cross level attachment and undo/redo pain)
 * 
 * The LevelInstance Level Actors are attached to this ALevelInstanceEditorInstanceActor keeping the attachment local to the Instance Level and shielded from the transaction buffer.
 *
 * Avoiding those Level Actors from being part of the Transaction system allows us to unload that level without clearing the transaction buffer. It also allows BP Reinstancing without having to update attachements.
 */
UCLASS()
class ENGINE_API ULevelInstanceComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()
public:
#if WITH_EDITORONLY_DATA
	virtual void Serialize(FArchive& Ar) override;
#endif
#if WITH_EDITOR
	// Those are the methods that need overriding to be able to properly update the AttachComponent
	virtual void OnRegister() override;
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport) override;

	void UpdateEditorInstanceActor();
	void OnEdit();
	void OnCommit();

	const FWorldPartitionActorFilter& GetFilter() const { return IsEditFilter() ? EditFilter : Filter; }
	void SetFilter(const FWorldPartitionActorFilter& InFilter);
	const TMap<FActorContainerID, TSet<FGuid>>& GetFilteredActorsPerContainer() const;
	void UpdateEditFilter();
private:
	bool ShouldShowSpriteComponent() const;
	void OnFilterChanged();
	void SetActiveFilter(const FWorldPartitionActorFilter& InFilter);
	bool IsEditFilter() const;

	TWeakObjectPtr<AActor> CachedEditorInstanceActorPtr;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Filter, meta=(LevelInstanceFilter))
	FWorldPartitionActorFilter Filter;

	UPROPERTY(EditAnywhere, Transient, Category = Filter, meta=(LevelInstanceEditFilter))
	FWorldPartitionActorFilter EditFilter;

	FWorldPartitionActorFilter UndoRedoCachedFilter;

	mutable FWorldPartitionActorFilter CachedFilter;
	mutable TOptional<TMap<FActorContainerID, TSet<FGuid>>> CachedFilteredActorsPerContainer;

	// Used to cancel the package getting dirty when editing the EditFilter which is transient
	bool bWasDirtyBeforeEditFilterChange;
#endif
};