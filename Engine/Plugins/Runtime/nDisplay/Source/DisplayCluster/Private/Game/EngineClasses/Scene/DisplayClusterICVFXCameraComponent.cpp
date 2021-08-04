// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterICVFXCameraComponent.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_CameraMotionBlur.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "DisplayClusterRootActor.h"

void UDisplayClusterICVFXCameraComponent::GetDesiredView(FMinimalViewInfo& DesiredView)
{
	if (ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(GetOwner()))
	{
		UCineCameraComponent* CineCameraComponent = CameraSettings.ExternalCameraActor.IsValid() ? CameraSettings.ExternalCameraActor->GetCineCameraComponent() : static_cast<UCineCameraComponent*>(this);

		float DeltaTime = RootActor->GetWorldDeltaSeconds();
		CineCameraComponent->GetCameraView(DeltaTime, DesiredView);
	}
}

UCameraComponent* UDisplayClusterICVFXCameraComponent::GetCameraComponent()
{
	return CameraSettings.ExternalCameraActor.IsValid() ? CameraSettings.ExternalCameraActor->GetCameraComponent() : this;
}

FString UDisplayClusterICVFXCameraComponent::GetCameraUniqueId() const
{
	return GetFName().ToString();
}

#if WITH_EDITOR
bool UDisplayClusterICVFXCameraComponent::GetEditorPreviewInfo(float DeltaTime, FMinimalViewInfo& ViewOut)
{
	const bool bEnabled = Super::GetEditorPreviewInfo(DeltaTime, ViewOut);
	GetDesiredView(ViewOut);
	return bEnabled;
}
#endif

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
