// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"

#include "DisplayClusterConfigurationTypes_ICVFX.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_CameraMotionBlur.h"

#include "DisplayClusterICVFXCameraComponent.generated.h"

struct FMinimalViewInfo;
class UCameraComponent;

/**
 * nDisplay in-camera VFX camera representation
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
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = NDisplay, meta = (ShowInnerProperties))
	FDisplayClusterConfigurationICVFX_CameraSettings CameraSettings;

#if WITH_EDITOR
	virtual bool GetEditorPreviewInfo(float DeltaTime, FMinimalViewInfo& ViewOut) override;
#endif
	
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
	void GetDesiredView(FMinimalViewInfo& DesiredView);
};
