// Copyright Epic Games, Inc. All Rights Reserved.
#include "Foundation/FoundationComponent.h"
#include "Foundation/FoundationActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"

UFoundationComponent::UFoundationComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	bWantsOnUpdateTransform = true;
#endif
}

#if WITH_EDITOR
void UFoundationComponent::OnRegister()
{
#if WITH_EDITORONLY_DATA
	// Prevents USceneComponent from creating the SpriteComponent in OnRegister because we want to provide a different texture and condition
	bVisualizeComponent = false;
#endif

	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	AActor* Owner = GetOwner();
	// Only show Sprite for non-instanced foundations
	if (Owner && Owner->GetLevel() && !GetOwner()->GetLevel()->IsInstancedLevel() && !GetWorld()->IsGameWorld())
	{
		// Re-enable before calling CreateSpriteComponent
		bVisualizeComponent = true;
		CreateSpriteComponent(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/FoundationActor")));
	}
#endif //WITH_EDITORONLY_DATA
}

void UFoundationComponent::PostEditUndo()
{
	Super::PostEditUndo();
		
	UpdateComponentToWorld();
	UpdateEditorInstanceActor();
}

void UFoundationComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UpdateEditorInstanceActor();
}

void UFoundationComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);

	UpdateEditorInstanceActor();
}

void UFoundationComponent::UpdateEditorInstanceActor()
{
	if (!CachedEditorInstanceActorPtr.IsValid())
	{
		check(GetOuterAFoundationActor());
		CachedEditorInstanceActorPtr = GetOuterAFoundationActor()->FindEditorInstanceActor();
	}

	if (AActor* EditorInstanceActor = CachedEditorInstanceActorPtr.Get())
	{
		EditorInstanceActor->GetRootComponent()->SetWorldTransform(GetComponentTransform());
	}
}

#endif