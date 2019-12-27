// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCameraComponent.h"
#include "MagicLeapCameraPlugin.h"

IMPLEMENT_MODULE(FMagicLeapCameraPlugin, MagicLeapCamera);

void UMagicLeapCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	GetMagicLeapCameraPlugin().IncUserCount();
	GetMagicLeapCameraPlugin().SetLogDelegate(OnLogMessage);
}

void UMagicLeapCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetMagicLeapCameraPlugin().DecUserCount();
	Super::EndPlay(EndPlayReason);
}

bool UMagicLeapCameraComponent::CaptureImageToFileAsync()
{
	return GetMagicLeapCameraPlugin().CaptureImageToFileAsync(OnCaptureImgToFile);
}

bool UMagicLeapCameraComponent::CaptureImageToTextureAsync()
{
	return GetMagicLeapCameraPlugin().CaptureImageToTextureAsync(OnCaptureImgToTexture);
}

bool UMagicLeapCameraComponent::StartRecordingAsync()
{
	return GetMagicLeapCameraPlugin().StartRecordingAsync(OnStartRecording);
}

bool UMagicLeapCameraComponent::StopRecordingAsync()
{
	return GetMagicLeapCameraPlugin().StopRecordingAsync(OnStopRecording);
}

bool UMagicLeapCameraComponent::IsCapturing() const
{
	return GetMagicLeapCameraPlugin().IsCapturing();
}
