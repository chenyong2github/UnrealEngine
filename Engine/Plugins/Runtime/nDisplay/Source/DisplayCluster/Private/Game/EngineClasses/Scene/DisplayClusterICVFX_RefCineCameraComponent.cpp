// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterICVFX_RefCineCameraComponent.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_CameraMotionBlur.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "DisplayClusterRootActor.h"

UDisplayClusterICVFX_RefCineCameraComponent::UDisplayClusterICVFX_RefCineCameraComponent(const FObjectInitializer& ObjectInitializer)
{
	IncameraSettings = CreateDefaultSubobject<UDisplayClusterConfigurationICVFX_CameraSettings>(TEXT("IncameraSettings"));
}

UCameraComponent* UDisplayClusterICVFX_RefCineCameraComponent::GetCameraComponent() const
{
	if (CineCameraActor != nullptr)
	{
		return CineCameraActor->GetCameraComponent();
	}

	return nullptr;
}

FString UDisplayClusterICVFX_RefCineCameraComponent::GetCameraUniqueId() const
{
	if (CineCameraActor != nullptr)
	{
		return CineCameraActor->GetFName().ToString();
	}

	return FString();
}

FDisplayClusterViewport_CameraMotionBlur UDisplayClusterICVFX_RefCineCameraComponent::GetMotionBlurParameters()
{
	FDisplayClusterViewport_CameraMotionBlur OutParameters;
	OutParameters.Mode = EDisplayClusterViewport_CameraMotionBlur::Undefined;

	switch (IncameraSettings->CameraMotionBlur.MotionBlurMode)
	{
	case EDisplayClusterConfigurationCameraMotionBlurMode::Off:
		OutParameters.Mode = EDisplayClusterViewport_CameraMotionBlur::Off;
		break;

	case EDisplayClusterConfigurationCameraMotionBlurMode::On:
		OutParameters.Mode = EDisplayClusterViewport_CameraMotionBlur::On;
		break;

	case EDisplayClusterConfigurationCameraMotionBlurMode::Override:
	{
		ADisplayClusterRootActor* RootActor = static_cast<ADisplayClusterRootActor*>(GetOwner());
		if (RootActor)
		{
			UDisplayClusterCameraComponent* OuterCamera = RootActor->GetDefaultCamera();
			if (OuterCamera)
			{
				OutParameters.CameraLocation = OuterCamera->K2_GetComponentLocation();
				OutParameters.CameraRotation = OuterCamera->K2_GetComponentRotation();

				OutParameters.TranslationScale = IncameraSettings->CameraMotionBlur.TranslationScale;

				OutParameters.Mode = EDisplayClusterViewport_CameraMotionBlur::Override;
			}
		}
	}
	break;

	default:
		break;
	}

	return OutParameters;
}
