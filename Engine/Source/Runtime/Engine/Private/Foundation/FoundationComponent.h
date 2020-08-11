// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "FoundationComponent.generated.h"

/**
 * UFoundationComponent subclasses USceneComponent for Editing purposes so that we can have a proxy to the FoundationActor's RootComponent transform without attaching to it.
 *
 * It is responsible for updating the transform of the AFoundationEditorInstanceActor which is created when loading a Foundation Instance Level
 *
 * We use this method to avoid attaching the Instance Level Actors to the AFoundationActor. (Cross level attachment and undo/redo pain)
 * 
 * The Foundation Level Actors are attached to this AFoundationEditorInstanceActor keeping the attachment local to the Instance Level and shielded from the transaction buffer.
 *
 * Avoiding those Level Actors from being part of the Transaction system allows us to unload that level without clearing the transaction buffer. It also allows BP Reinstancing without having to update attachements.
 */
UCLASS(Within=FoundationActor)
class ENGINE_API UFoundationComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()
public:
#if WITH_EDITOR
	// Those are the methods that need overriding to be able to properly update the AttachComponent
	virtual void OnRegister() override;
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport) override;

	void UpdateEditorInstanceActor();
private:
	TWeakObjectPtr<AActor> CachedEditorInstanceActorPtr;
#endif
};