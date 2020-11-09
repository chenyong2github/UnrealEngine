// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CineCameraActor.h"
#include "VCamComponent.h"
#include "VCamSample_CameraActor.generated.h"

/**
 * A CineCameraActor with a preattached VCamComponent for sample usage
 */
UCLASS()
class VCAMCORE_API AVCamSample_CameraActor : public ACineCameraActor
{
	GENERATED_BODY()

public:
	AVCamSample_CameraActor(const FObjectInitializer& ObjectInitializer);

private:
	UPROPERTY(EditAnywhere, Category = "VirtualCamera")
	UVCamComponent* VCamComponent = nullptr;
};
