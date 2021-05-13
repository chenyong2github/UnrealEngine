// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CineCameraComponent.h"
#include "DisplayClusterConfigurationTypes_ICVFX.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_CameraMotionBlur.h"

#include "DisplayClusterICVFX_CineCameraComponent.generated.h"

/**
 * ICVFX camera with configuration
 */
UCLASS(ClassGroup = (DisplayCluster), meta = (DisplayName="ICVFX_CineCamera"))
class DISPLAYCLUSTER_API UDisplayClusterICVFX_CineCameraComponent 
	: public UCineCameraComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterICVFX_CineCameraComponent(const FObjectInitializer& ObjectInitializer);

public:
	UPROPERTY(EditAnywhere, Category = "NDisplay ICVFX")
	UDisplayClusterConfigurationICVFX_CameraSettings* IncameraSettings;

public:
	FDisplayClusterViewport_CameraMotionBlur GetMotionBlurParameters();

	bool IsShouldUseICVFX() const
	{
		return IncameraSettings->bEnable;
	}

	// Return unique camera name
	FString GetCameraUniqueId() const;

	const UDisplayClusterConfigurationICVFX_CameraSettings* GetCameraSettingsICVFX() const
	{
		return IncameraSettings;
	}

	class UCameraComponent* GetCameraComponent();
};
