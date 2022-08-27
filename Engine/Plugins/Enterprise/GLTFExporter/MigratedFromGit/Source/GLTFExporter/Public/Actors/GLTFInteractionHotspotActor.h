// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Components/GLTFInteractionHotspotComponent.h"
#include "GLTFInteractionHotspotActor.generated.h"

/**
 * Actor wrapper for the GLTF hotspot component. Appears as a billboard and allows playback of skeletal animations when cursor input is enabled.
 */
UCLASS(DisplayName = "GLTF Interaction Hotspot Actor")
class GLTFEXPORTER_API AGLTFInteractionHotspotActor : public AActor
{
	GENERATED_BODY()
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
	/** List of skeletal meshes and animations to be played when the hotspot is interacted with */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	TArray<FGLTFAnimation> Animations;

	/** The billboard image that will be shown when the hotspot is in an inactive state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* DefaultSprite;

	/** The optional billboard image that will be shown when a cursor enters the hotspot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* HighlightSprite;

	/** The optional billboard image that will be shown when the hotspot is toggled by a click */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* ToggledSprite;

private:
	UPROPERTY()
	USceneComponent* SceneComponent;

public:
	UPROPERTY(BlueprintReadOnly, Category = "GLTF Interaction Hotspot")
	UGLTFInteractionHotspotComponent* InteractionHotspotComponent;
};
