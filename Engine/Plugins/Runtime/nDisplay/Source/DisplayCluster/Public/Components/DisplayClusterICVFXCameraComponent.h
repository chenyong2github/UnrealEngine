// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"

#include "DisplayClusterConfigurationTypes_ICVFX.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_CameraMotionBlur.h"

#include "DisplayClusterICVFXCameraComponent.generated.h"


class UCameraComponent;


/**
 * ICVFX camera with configuration
 */
UCLASS(ClassGroup = (DisplayCluster), meta = (DisplayName="ICVFX Camera"))
class DISPLAYCLUSTER_API UDisplayClusterICVFXCameraComponent
	: public UCineCameraComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterICVFXCameraComponent(const FObjectInitializer& ObjectInitializer)
	{ }

public:
	// Use external cine camera actor
	UPROPERTY(EditAnywhere, Category = NDisplay)
	TSoftObjectPtr<ACineCameraActor> ExternalCameraActor;

	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ShowInnerProperties))
	FDisplayClusterConfigurationICVFX_CameraSettings CameraSettings;

public:
	FDisplayClusterViewport_CameraMotionBlur GetMotionBlurParameters();

	bool IsICVFXEnabled() const
	{
		return CameraSettings.bEnable;
	}

	// Return unique camera name
	FString GetCameraUniqueId() const;

	const FDisplayClusterConfigurationICVFX_CameraSettings& GetCameraSettingsICVFX() const
	{
		return CameraSettings;
	}

	UCameraComponent* GetCameraComponent();
};
