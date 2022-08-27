// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/BillboardComponent.h"
#include "GLTFInteractionHotspotComponent.generated.h"

class ASkeletalMeshActor;
class UAnimSequence;

/**
 * 
 */
UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "GLTF Interaction Hotspot Component")
class GLTFEXPORTER_API UGLTFInteractionHotspotComponent : public UBillboardComponent
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ASkeletalMeshActor* SkeletalMeshActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UAnimSequence* AnimationSequence;

	//~ Begin UActorComponent Interface.
	virtual void BeginPlay() override;
	//~ End UActorComponent Interface.

private:
	UFUNCTION()
	void BeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void EndCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);
};
