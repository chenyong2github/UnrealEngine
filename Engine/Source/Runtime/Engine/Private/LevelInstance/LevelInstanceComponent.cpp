// Copyright Epic Games, Inc. All Rights Reserved.
#include "LevelInstance/LevelInstanceComponent.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "LevelInstance/LevelInstanceEditorInstanceActor.h"
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/Level.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LevelInstanceComponent)

ULevelInstanceComponent::ULevelInstanceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	bWantsOnUpdateTransform = true;
#endif
}

#if WITH_EDITOR
void ULevelInstanceComponent::OnRegister()
{
#if WITH_EDITORONLY_DATA
	// Prevents USceneComponent from creating the SpriteComponent in OnRegister because we want to provide a different texture and condition
	bVisualizeComponent = false;
#endif

	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	AActor* Owner = GetOwner();
	// Only show Sprite for non-instanced LevelInstances
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		// Re-enable before calling CreateSpriteComponent
		bVisualizeComponent = true;
		CreateSpriteComponent(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/LevelInstance")), false);
		if (SpriteComponent)
		{
			SpriteComponent->bShowLockedLocation = false;
			SpriteComponent->SetVisibility(ShouldShowSpriteComponent());
			SpriteComponent->RegisterComponent();
		}
	}
#endif //WITH_EDITORONLY_DATA
}

void ULevelInstanceComponent::SetFilter(const FWorldPartitionActorFilter& InFilter)
{
	if (Filter != InFilter)
	{
		Modify();
		Filter = InFilter;
		FWorldPartitionActorFilter::GetOnWorldPartitionActorFilterChanged().Broadcast();
	}
}

bool ULevelInstanceComponent::ShouldShowSpriteComponent() const
{
	return GetOwner() && GetOwner()->GetLevel() && (GetOwner()->GetLevel()->IsPersistentLevel() || !GetOwner()->GetLevel()->IsInstancedLevel());
}

void ULevelInstanceComponent::PreEditUndo()
{
	UndoRedoCachedFilter = Filter;
}

void ULevelInstanceComponent::PostEditUndo()
{
	Super::PostEditUndo();
		
	UpdateComponentToWorld();
	UpdateEditorInstanceActor();

	if (Filter != UndoRedoCachedFilter)
	{
		FWorldPartitionActorFilter::RequestFilterRefresh(false);
		FWorldPartitionActorFilter::GetOnWorldPartitionActorFilterChanged().Broadcast();
	}
	UndoRedoCachedFilter = FWorldPartitionActorFilter();
}

void ULevelInstanceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UpdateEditorInstanceActor();
}

void ULevelInstanceComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);

	UpdateEditorInstanceActor();
}

void ULevelInstanceComponent::UpdateEditorInstanceActor()
{
	if (!CachedEditorInstanceActorPtr.IsValid())
	{
		if (ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(GetOwner()))
		{
			if (ULevelInstanceSubsystem* LevelInstanceSubsystem = LevelInstance->GetLevelInstanceSubsystem())
			{
				if (LevelInstanceSubsystem->IsLoaded(LevelInstance))
				{
					LevelInstanceSubsystem->ForEachActorInLevelInstance(LevelInstance, [this, LevelInstance](AActor* LevelActor)
					{
						if (ALevelInstanceEditorInstanceActor* LevelInstanceEditorInstanceActor = Cast<ALevelInstanceEditorInstanceActor>(LevelActor))
						{
							check(LevelInstanceEditorInstanceActor->GetLevelInstanceID() == LevelInstance->GetLevelInstanceID());
							CachedEditorInstanceActorPtr = LevelInstanceEditorInstanceActor;
							return false;
						}
						return true;
					});
				}
			}
		}
	}

	if (ALevelInstanceEditorInstanceActor* EditorInstanceActor = Cast<ALevelInstanceEditorInstanceActor>(CachedEditorInstanceActorPtr.Get()))
	{
		EditorInstanceActor->UpdateWorldTransform(GetComponentTransform());
	}
}

void ULevelInstanceComponent::OnEdit()
{
	if (SpriteComponent)
	{
		SpriteComponent->SetVisibility(false);
	}
}

void ULevelInstanceComponent::OnCommit()
{
	if (SpriteComponent)
	{
		SpriteComponent->SetVisibility(ShouldShowSpriteComponent());
	}
}

const TMap<FActorContainerID, TSet<FGuid>>& ULevelInstanceComponent::GetFilteredActorsPerContainer() const
{
	if (CachedFilter != Filter)
	{
		CachedFilteredActorsPerContainer.Reset();
	}

	if (CachedFilteredActorsPerContainer.IsSet())
	{
		return CachedFilteredActorsPerContainer.GetValue();
	}

	TMap<FActorContainerID, TSet<FGuid>> FilteredActors;
	if (ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(GetOwner()))
	{
		// Fill Container Instance Filter
		UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
		check(WorldPartitionSubsystem);
				
		FilteredActors = WorldPartitionSubsystem->GetFilteredActorsPerContainer(LevelInstance->GetLevelInstanceID().GetContainerID(), LevelInstance->GetWorldAssetPackage(), Filter);
	}
	
	CachedFilteredActorsPerContainer = MoveTemp(FilteredActors);
	CachedFilter = Filter;
	return CachedFilteredActorsPerContainer.GetValue();
}

#endif
