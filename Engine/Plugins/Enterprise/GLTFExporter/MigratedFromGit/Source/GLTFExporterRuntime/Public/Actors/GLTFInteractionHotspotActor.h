// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFInteractionHotspotActor.generated.h"

class ASkeletalMeshActor;
class UMaterialBillboardComponent;
class USphereComponent;

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
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	//~ Begin AActor Interface
	virtual void PostRegisterAllComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor Interface

private:
	UFUNCTION()
	void BeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void EndCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);

	void SetActiveImage(UTexture2D* NewImage);

	UTexture2D* CalculateActiveImage(bool bCursorOver) const;

public:

	/* The skeletal mesh actor that will be animated when the hotspot is clicked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	ASkeletalMeshActor* SkeletalMeshActor;

	/* The animation that will be played on the skeletal mesh actor. Must be compatible with its skeletal mesh asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UAnimSequence* AnimationSequence;

	/* The billboard image that will be shown when the hotspot is in an inactive state or one without a specified image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* Image;

	/** The optional billboard image that will be shown when a cursor enters the hotspot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* HoveredImage;

	/** The optional billboard image that will be shown when the hotspot is toggled by a click. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* ToggledImage;

	/** The optional billboard image that will be shown when the hotspot is toggled by a click and a cursor enters it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* ToggledHoveredImage;

private:

	UPROPERTY(transient, DuplicateTransient)
	UMaterialBillboardComponent* BillboardComponent;

	UPROPERTY(transient, duplicatetransient)
	USphereComponent* SphereComponent;

	UPROPERTY(transient, duplicatetransient)
	UMaterialInterface* DefaultMaterial;

#if WITH_EDITORONLY_DATA
	UPROPERTY(transient, duplicatetransient)
	UMaterialInterface* DefaultIconMaterial;
#endif // WITH_EDITORONLY_DATA

	UPROPERTY(transient, duplicatetransient)
	UTexture* ActiveImage;

	FVector2D ActiveImageSize;

	bool bToggled;

	bool bIsInteractable;

	float RealtimeSecondsWhenLastInSight;

	float RealtimeSecondsWhenLastHidden;

	void SetupSpriteElement() const;

	UMaterialInstanceDynamic* GetSpriteMaterial() const;

	void UpdateSpriteSize();

	void SetSpriteOpacity(const float Opacity) const;

	FIntPoint GetCurrentViewportSize();

	void ViewportResized(FViewport*, uint32);
};
