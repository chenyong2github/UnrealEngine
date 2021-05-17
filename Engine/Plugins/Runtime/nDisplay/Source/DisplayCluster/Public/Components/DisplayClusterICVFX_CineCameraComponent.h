// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CineCameraActor.h"
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
	// Use external cine camrea actor ref
	UPROPERTY(EditAnywhere, Category = NDisplay)
	TSoftObjectPtr<ACineCameraActor> CineCameraActorRef;

	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ShowInnerProperties))
	FDisplayClusterConfigurationICVFX_CameraSettings CameraSettings;

public:
	FDisplayClusterViewport_CameraMotionBlur GetMotionBlurParameters();

	bool IsShouldUseICVFX() const
	{
		return CameraSettings.bEnable;
	}

	// Return unique camera name
	FString GetCameraUniqueId() const;

	const FDisplayClusterConfigurationICVFX_CameraSettings& GetCameraSettingsICVFX() const
	{
		return CameraSettings;
	}

	class UCameraComponent* GetCameraComponent();
};
