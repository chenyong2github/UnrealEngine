// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GLTFInteractionHotspotActor.generated.h"

UCLASS()
class GLTFEXPORTER_API AGLTFInteractionHotspotActor : public AActor
{
	GENERATED_BODY()

public:
	AGLTFInteractionHotspotActor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	UFUNCTION()
	void BeginCursorOver(AActor* TouchedActor);

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class USceneComponent* SceneComponent;
		
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UGLTFInteractionHotspotComponent* InteractionHotspotComponent;
};
