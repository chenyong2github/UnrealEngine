// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Components/GLTFInteractionHotspotComponent.h"
#include "GLTFInteractionHotspotActor.generated.h"

/**
 * Actor wrapper for the GLTF hotspot component. Appears as a billboard and allows playback of skeletal animations when cursor input is enabled.
 */
UCLASS(BlueprintType, Blueprintable, HideCategories = (Sprite, Physics, Collision, Navigation), DisplayName = "GLTF Interaction Hotspot Actor")
class GLTFEXPORTERRUNTIME_API AGLTFInteractionHotspotActor : public AActor
{
	GENERATED_BODY()
	//~ Begin UObject Interface
public:
	AGLTFInteractionHotspotActor(const FObjectInitializer& ObjectInitializer);
	//~ End UObject Interface

private:
	UPROPERTY(Category = "GLTF Interaction Hotspot", VisibleAnywhere, BlueprintReadOnly, meta = (ExposeFunctionCategories = "GLTF Interaction Hotspot", AllowPrivateAccess = "true"))
	UGLTFInteractionHotspotComponent* InteractionHotspotComponent;
};
