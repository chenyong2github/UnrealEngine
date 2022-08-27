// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GLTFInteractionHotspotActor.generated.h"

class USceneComponent;
class UGLTFInteractionHotspotComponent;
class ASkeletalMeshActor;
class AnimationSequence;

/**
 * Actor wrapper for the GLTF hotspot component. Appears as a billboard and allows playback of skeletal animations when cursor input is enabled.
 */
UCLASS(DisplayName = "GLTF Interaction Hotspot Actor")
class GLTFEXPORTER_API AGLTFInteractionHotspotActor : public AActor
{
	GENERATED_BODY()
private:
	UPROPERTY()
	USceneComponent* SceneComponent;

	UPROPERTY()
	UGLTFInteractionHotspotComponent* InteractionHotspotComponent;

	//~ Begin UObject Interface.
public:
	AGLTFInteractionHotspotActor(const FObjectInitializer& ObjectInitializer);

private:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface.

private:
	void ForwardPropertiesToComponent();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actors|GLTF Interaction Hotspot")
	ASkeletalMeshActor* SkeletalMeshActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actors|GLTF Interaction Hotspot")
	UAnimSequence* AnimationSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actors|GLTF Interaction Hotspot")
	UTexture2D* DefaultSprite;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actors|GLTF Interaction Hotspot")
	UTexture2D* HighlightSprite;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actors|GLTF Interaction Hotspot")
	UTexture2D* ClickSprite;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actors|GLTF Interaction Hotspot")
	float Radius;
};
