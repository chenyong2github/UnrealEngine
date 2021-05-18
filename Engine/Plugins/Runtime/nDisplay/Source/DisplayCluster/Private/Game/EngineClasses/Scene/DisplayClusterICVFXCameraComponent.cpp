// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterICVFXCameraComponent.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_CameraMotionBlur.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "DisplayClusterRootActor.h"


UCameraComponent* UDisplayClusterICVFXCameraComponent::GetCameraComponent()
{
	return ExternalCameraActor.IsValid() ? ExternalCameraActor->GetCameraComponent() : this;
}

FString UDisplayClusterICVFXCameraComponent::GetCameraUniqueId() const
{
	return GetFName().ToString();
}

FDisplayClusterViewport_CameraMotionBlur UDisplayClusterICVFXCameraComponent::GetMotionBlurParameters()
{
	FDisplayClusterViewport_CameraMotionBlur OutParameters;
	OutParameters.Mode = EDisplayClusterViewport_CameraMotionBlur::Undefined;

	switch (CameraSettings.CameraMotionBlur.MotionBlurMode)
	{
	case EDisplayClusterConfigurationCameraMotionBlurMode::Off:
		OutParameters.Mode = EDisplayClusterViewport_CameraMotionBlur::Off;
		break;

	case EDisplayClusterConfigurationCameraMotionBlurMode::On:
		OutParameters.Mode = EDisplayClusterViewport_CameraMotionBlur::On;
		break;

	case EDisplayClusterConfigurationCameraMotionBlurMode::Override:
		ADisplayClusterRootActor* RootActor = static_cast<ADisplayClusterRootActor*>(GetOwner());
		if (RootActor)
		{
			UDisplayClusterCameraComponent* OuterCamera = RootActor->GetDefaultCamera();
			if (OuterCamera)
			{
				OutParameters.CameraLocation   = OuterCamera->GetComponentLocation();
				OutParameters.CameraRotation   = OuterCamera->GetComponentRotation();
				OutParameters.TranslationScale = CameraSettings.CameraMotionBlur.TranslationScale;
				OutParameters.Mode             = EDisplayClusterViewport_CameraMotionBlur::Override;
			}
		}
		break;
	}

	return OutParameters;
}
