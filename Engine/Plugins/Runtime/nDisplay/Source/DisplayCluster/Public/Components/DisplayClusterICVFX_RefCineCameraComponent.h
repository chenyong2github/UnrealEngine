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
UCLASS(ClassGroup = (DisplayCluster), meta = (DisplayName="NDisplay ICVFX_RefCineCamera"))
class DISPLAYCLUSTER_API UDisplayClusterICVFX_RefCineCameraComponent 
	: public USceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterICVFX_RefCineCameraComponent(const FObjectInitializer& ObjectInitializer);

public:
	UPROPERTY(EditAnywhere, Category = "NDisplay ICVFX")
	UDisplayClusterConfigurationICVFX_CameraSettings* IncameraSettings;

	UPROPERTY(EditAnywhere, Category = "NDisplay ICVFX")
	TSoftObjectPtr<ACineCameraActor> CineCameraActor;

public:
	FDisplayClusterViewport_CameraMotionBlur GetMotionBlurParameters();

	bool IsShouldUseICVFX() const
	{
		return IncameraSettings->bEnable && IsCineCameraActorValid();
	}

	// Return unique camera name
	FString GetCameraUniqueId() const
	{
		if (IsCineCameraActorValid())
		{
			return CineCameraActor->GetFName().ToString();
		}

		return FString();
	}

	const UDisplayClusterConfigurationICVFX_CameraSettings* GetCameraSettingsICVFX() const
	{
		return IncameraSettings;
	}

	class UCameraComponent* GetCameraComponent() const
	{
		if (IsCineCameraActorValid())
		{
			return CineCameraActor->GetCameraComponent();
		}

		return nullptr;
	}

private:
	bool IsCineCameraActorValid() const
	{
		return CineCameraActor.IsValid() && !CineCameraActor.IsNull();
	}
};
