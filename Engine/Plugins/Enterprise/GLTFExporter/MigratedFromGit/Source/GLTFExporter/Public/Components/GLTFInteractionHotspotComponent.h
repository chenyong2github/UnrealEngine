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

	bool operator == (const FGLTFAnimation& OtherAnimation) const
	{
		return SkeletalMeshActor == OtherAnimation.SkeletalMeshActor &&
			AnimationSequence == OtherAnimation.AnimationSequence;
	}

	bool operator != (const FGLTFAnimation& OtherAnimation) const
	{
		return !(*this == OtherAnimation);
	}
};

/**
 * A component to set up hotspots which appear as billboards and allow playback of skeletal animations when cursor input is enabled.
 */
UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent), hidecategories = (Sprite), DisplayName = "GLTF Interaction Hotspot Component")
class GLTFEXPORTER_API UGLTFInteractionHotspotComponent : public UBillboardComponent
{
	GENERATED_UCLASS_BODY()
public:
	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;

protected:
	virtual void OnRegister() override;
	virtual void OnCreatePhysicsState() override;
	//~ End UActorComponent Interface

	//~ Begin UPrimitiveComponent Interface
public:
	virtual UBodySetup* GetBodySetup() override;
	//~ End UPrimitiveComponent Interface

	//~ Begin UBillboardComponent Interface
	virtual void SetSprite(class UTexture2D* NewSprite) override;
	//~ End UBillboardComponent Interface

private:
	UFUNCTION()
	void BeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void EndCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);

	void UpdateCollisionVolume();
	float GetBillboardBoundingRadius() const;
	UTexture2D* GetActiveImage(bool bCursorOver) const;

public:
	/** List of skeletal meshes and animations to be played when the hotspot is interacted with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Interaction Hotspot")
	TArray<FGLTFAnimation> Animations;

	/** The billboard image that will be shown when the hotspot is in an idle state or one without a specified image. */
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
	UPROPERTY(transient, duplicatetransient)
	UBodySetup* ShapeBodySetup;

	bool bToggled;
};
