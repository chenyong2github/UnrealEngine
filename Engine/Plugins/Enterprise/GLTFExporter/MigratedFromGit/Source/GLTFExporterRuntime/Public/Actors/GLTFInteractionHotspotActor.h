// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GLTFInteractionHotspotActor.generated.h"

class ASkeletalMeshActor;
class UAnimSequence;
class UTexture2D;
class UTexture;
class UMaterialInterface;
class UMaterialBillboardComponent;
class USphereComponent;
class UMaterialInstanceDynamic;
class FViewport;

enum class EGLTFHotspotState : uint8
{
	Default,
	Hovered,
	Toggled,
	ToggledHovered
};

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

	void UpdateActiveImageFromState(EGLTFHotspotState State);

public:

	/* The skeletal mesh actor that will be animated when the hotspot is clicked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	ASkeletalMeshActor* SkeletalMeshActor;

	/* The animation that will be played on the skeletal mesh actor. Must be compatible with its skeletal mesh asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot", meta=(EditCondition="SkeletalMeshActor != nullptr"))
	UAnimSequence* AnimationSequence;

	/* The billboard image that will be shown when the hotspot is in an inactive state or one without a specified image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* Image;

	/** The optional billboard image that will be shown when a cursor enters the hotspot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot", meta=(EditCondition="Image != nullptr"))
	UTexture2D* HoveredImage;

	/** The optional billboard image that will be shown when the hotspot is toggled by a click. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot", meta=(EditCondition="Image != nullptr"))
	UTexture2D* ToggledImage;

	/** The optional billboard image that will be shown when the hotspot is toggled by a click and a cursor enters it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot", meta=(EditCondition="Image != nullptr"))
	UTexture2D* ToggledHoveredImage;

private:

	UPROPERTY(Transient, DuplicateTransient)
	UMaterialBillboardComponent* BillboardComponent;

	UPROPERTY(Transient, DuplicateTransient)
	USphereComponent* SphereComponent;

	UPROPERTY(Transient, DuplicateTransient)
	UMaterialInterface* DefaultMaterial;

	UPROPERTY(Transient, DuplicateTransient)
	UTexture2D* DefaultImage;

	UPROPERTY(Transient, DuplicateTransient)
	UTexture2D* DefaultHoveredImage;

	UPROPERTY(Transient, DuplicateTransient)
	UTexture2D* DefaultToggledImage;

	UPROPERTY(Transient, DuplicateTransient)
	UTexture2D* DefaultToggledHoveredImage;

	UPROPERTY(Transient, DuplicateTransient)
	UMaterialInterface* DefaultIconMaterial;

	UPROPERTY(Transient, DuplicateTransient)
	const UTexture* ActiveImage;

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

public:

	const UTexture2D* GetImageForState(EGLTFHotspotState State) const;
};
