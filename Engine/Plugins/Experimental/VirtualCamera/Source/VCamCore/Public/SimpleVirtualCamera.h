// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CineCameraActor.h"
#include "VCamComponent.h"
#include "SimpleVirtualCamera.generated.h"

/**
 * A CineCameraActor with a preattached VCamComponent for sample usage
 */
UCLASS()
class VCAMCORE_API ASimpleVirtualCamera : public ACineCameraActor
{
	GENERATED_BODY()

public:
	virtual void PostActorCreated() override;

private:
	UPROPERTY(EditAnywhere, Category = "VirtualCamera")
	UVCamComponent* VCamComponent = nullptr;
};
