// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GLTFInteractionHotspotActor.generated.h"

class USceneComponent;
class UGLTFInteractionHotspotComponent;

/**
 *
 */
UCLASS(DisplayName = "GLTF Interaction Hotspot Actor")
class GLTFEXPORTER_API AGLTFInteractionHotspotActor : public AActor
{
	GENERATED_BODY()
private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	USceneComponent* SceneComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UGLTFInteractionHotspotComponent* InteractionHotspotComponent;

public:
	//~ Begin AActor Interface.
	AGLTFInteractionHotspotActor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;
	//~ End AActor Interface.

private:
	UFUNCTION()
	void BeginCursorOver(AActor* TouchedActor);
};
