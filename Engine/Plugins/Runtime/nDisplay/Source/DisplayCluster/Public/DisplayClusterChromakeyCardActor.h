// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterLightCardActor.h"

#include "DisplayClusterChromakeyCardActor.generated.h"

class UDisplayClusterICVFXCameraComponent;

UCLASS(Blueprintable, NotPlaceable, DisplayName = "Chromakey Card", HideCategories = (Tick, Physics, Collision, Replication, Cooking, Input, Actor))
class DISPLAYCLUSTER_API ADisplayClusterChromakeyCardActor : public ADisplayClusterLightCardActor
{
	GENERATED_BODY()
public:
	ADisplayClusterChromakeyCardActor(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;

	/** Setup chroma key for the owning root actor */
	void AddToChromakeyLayer(ADisplayClusterRootActor* InRootActor);
	
	/** Checks if the given ICVFX camera has chroma key settings supporting this actor */
	bool IsReferencedByICVFXCamera(UDisplayClusterICVFXCameraComponent* InCamera) const;
	
protected:
	void UpdateChromakeySettings();
};
