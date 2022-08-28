// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/BillboardComponent.h"
#include "GLTFInteractionHotspotComponent.generated.h"

/**
 * 
 */
UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "GLTF Interaction Hotspot Component")
class GLTFEXPORTER_API UGLTFInteractionHotspotComponent : public UBillboardComponent
{
	GENERATED_UCLASS_BODY()
	
	virtual void BeginPlay() override;

	virtual bool ShouldCreatePhysicsState() const { return true; }

	UFUNCTION()
	void BeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void EndCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);
};
