// Copyright Epic Games, Inc. All Rights Reserved.
#include "LevelInstance/LevelInstanceComponent.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"

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
	if (Owner && Owner->GetLevel() && !GetOwner()->GetLevel()->IsInstancedLevel() && !GetWorld()->IsGameWorld())
	{
		// Re-enable before calling CreateSpriteComponent
		bVisualizeComponent = true;
		CreateSpriteComponent(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/LevelInstance")));
	}
#endif //WITH_EDITORONLY_DATA
}

void ULevelInstanceComponent::PostEditUndo()
{
	Super::PostEditUndo();
		
	UpdateComponentToWorld();
	UpdateEditorInstanceActor();
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
		check(GetOuterALevelInstance());
		CachedEditorInstanceActorPtr = GetOuterALevelInstance()->FindEditorInstanceActor();
	}

	if (AActor* EditorInstanceActor = CachedEditorInstanceActorPtr.Get())
	{
		EditorInstanceActor->GetRootComponent()->SetWorldTransform(GetComponentTransform());
	}
}

#endif