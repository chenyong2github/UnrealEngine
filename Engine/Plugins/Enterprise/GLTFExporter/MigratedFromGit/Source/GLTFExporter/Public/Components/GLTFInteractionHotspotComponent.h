// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/BillboardComponent.h"
#include "GLTFInteractionHotspotComponent.generated.h"

class ASkeletalMeshActor;
class UAnimSequence;
class UBodySetup;
class UTexture2D;

/**
 * 
 */
UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent), hidecategories = (Sprite), DisplayName = "GLTF Interaction Hotspot Component")
class GLTFEXPORTER_API UGLTFInteractionHotspotComponent : public UBillboardComponent
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ASkeletalMeshActor* SkeletalMeshActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UAnimSequence* AnimationSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UTexture2D* DefaultSprite;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UTexture2D* HighlightSprite;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UTexture2D* ClickSprite;

	/** Description of collision */
	UPROPERTY(transient, duplicatetransient)
	UBodySetup* ShapeBodySetup;

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

private:
	UFUNCTION()
	void BeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void EndCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);
};
