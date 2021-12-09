// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterICVFXCameraComponent.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_CameraMotionBlur.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "DisplayClusterRootActor.h"


void UDisplayClusterICVFXCameraComponent::GetDesiredView(FMinimalViewInfo& DesiredView)
{
	if (ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(GetOwner()))
	{
		UCineCameraComponent* const CineCameraComponent = CameraSettings.ExternalCameraActor.IsValid() ? CameraSettings.ExternalCameraActor->GetCineCameraComponent() : this;

		const float DeltaTime = RootActor->GetWorldDeltaSeconds();
		CineCameraComponent->GetCameraView(DeltaTime, DesiredView);
	}
}

UCameraComponent* UDisplayClusterICVFXCameraComponent::GetCameraComponent()
{
	return CameraSettings.ExternalCameraActor.IsValid() ? CameraSettings.ExternalCameraActor->GetCineCameraComponent() : this;
}

FString UDisplayClusterICVFXCameraComponent::GetCameraUniqueId() const
{
	return GetFName().ToString();
}

#if WITH_EDITOR
bool UDisplayClusterICVFXCameraComponent::GetEditorPreviewInfo(float DeltaTime, FMinimalViewInfo& ViewOut)
{
	return CameraSettings.ExternalCameraActor.IsValid() ?
		CameraSettings.ExternalCameraActor->GetCineCameraComponent()->GetEditorPreviewInfo(DeltaTime, ViewOut) :
		UCameraComponent::GetEditorPreviewInfo(DeltaTime, ViewOut);
}

TSharedPtr<SWidget> UDisplayClusterICVFXCameraComponent::GetCustomEditorPreviewWidget()
{
	return CameraSettings.ExternalCameraActor.IsValid() ?
		CameraSettings.ExternalCameraActor->GetCineCameraComponent()->GetCustomEditorPreviewWidget() :
		UCameraComponent::GetCustomEditorPreviewWidget();
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
