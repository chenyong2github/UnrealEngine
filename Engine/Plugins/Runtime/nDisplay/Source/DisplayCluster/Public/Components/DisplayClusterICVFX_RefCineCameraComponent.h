// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CineCameraActor.h"
#include "DisplayClusterConfigurationTypes_ICVFX.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_CameraMotionBlur.h"

#include "DisplayClusterICVFX_RefCineCameraComponent.generated.h"

/**
 * ICVFX camera with configuration
 */
UCLASS(ClassGroup = (DisplayCluster))
class DISPLAYCLUSTER_API UDisplayClusterICVFX_RefCineCameraComponent 
	: public USceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterICVFX_RefCineCameraComponent(const FObjectInitializer& ObjectInitializer);

public:
	UPROPERTY(EditAnywhere, Category = "Display Cluster ICVFX")
	UDisplayClusterConfigurationICVFX_CameraSettings* IncameraSettings;

	UPROPERTY(EditAnywhere, Category = "Display Cluster ICVFX")
	ACineCameraActor* CineCameraActor = nullptr;

public:
	FDisplayClusterViewport_CameraMotionBlur GetMotionBlurParameters();

	bool IsShouldUseICVFX() const
	{
		return (CineCameraActor != nullptr) && IncameraSettings->bEnable;
	}

	// Return unique camera name
	FString GetCameraUniqueId() const;

	const UDisplayClusterConfigurationICVFX_CameraSettings* GetCameraSettingsICVFX() const
	{
		return IncameraSettings;
	}

	class UCameraComponent* GetCameraComponent() const;
};
