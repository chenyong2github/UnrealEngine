// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/BillboardComponent.h"
#include "GLTFInteractionHotspotComponent.generated.h"

class ASkeletalMeshActor;
class UAnimSequence;
class UBodySetup;
class UTexture2D;

USTRUCT(BlueprintType)
struct FGLTFAnimation
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ASkeletalMeshActor* SkeletalMeshActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UAnimSequence* AnimationSequence;
};

/**
 * A component to set up hotspots which appear as billboards and allow playback of skeletal animations when cursor input is enabled.
 */
UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent), hidecategories = (Sprite), DisplayName = "GLTF Interaction Hotspot Component")
class GLTFEXPORTER_API UGLTFInteractionHotspotComponent : public UBillboardComponent
{
	GENERATED_UCLASS_BODY()
public:
	//~ Begin UObject Interface.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface.

	//~ Begin UActorComponent Interface.
	virtual void BeginPlay() override;

protected:
	virtual void OnRegister() override;
	//~ End UActorComponent Interface.

	//~ Begin UBillboardComponent Interface.
public:
	virtual void SetSprite(class UTexture2D* NewSprite) override;
	//~ End UBillboardComponent Interface.

	void SetRadius(float NewRadius);

private:
	UFUNCTION()
	void BeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void EndCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);

	void UpdateCollisionVolume();
	float GetBillboardBoundingRadius() const;
	FTransform GetWorldTransform() const;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	TArray<FGLTFAnimation> Animations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* DefaultSprite;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* HighlightSprite;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	UTexture2D* ClickSprite;

private:
	UPROPERTY(transient, duplicatetransient)
	UBodySetup* ShapeBodySetup;
};
