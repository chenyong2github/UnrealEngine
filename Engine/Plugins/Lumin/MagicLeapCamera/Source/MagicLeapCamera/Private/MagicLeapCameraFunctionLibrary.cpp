// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCameraFunctionLibrary.h"
#include "MagicLeapCameraPlugin.h"

bool UMagicLeapCameraFunctionLibrary::CameraConnect(const FMagicLeapCameraConnect& ResultDelegate)
{
	return GetMagicLeapCameraPlugin().CameraConnect(ResultDelegate);
}

bool UMagicLeapCameraFunctionLibrary::CameraDisconnect(const FMagicLeapCameraDisconnect& ResultDelegate)
{
	return GetMagicLeapCameraPlugin().CameraDisconnect(ResultDelegate);
}

bool UMagicLeapCameraFunctionLibrary::CaptureImageToFileAsync(const FMagicLeapCameraCaptureImgToFile& InResultDelegate)
{
	FMagicLeapCameraCaptureImgToFileMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapCameraPlugin().CaptureImageToFileAsync(ResultDelegate);
}

bool UMagicLeapCameraFunctionLibrary::CaptureImageToTextureAsync(const FMagicLeapCameraCaptureImgToTexture& InResultDelegate)
{
	FMagicLeapCameraCaptureImgToTextureMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapCameraPlugin().CaptureImageToTextureAsync(ResultDelegate);
}

bool UMagicLeapCameraFunctionLibrary::StartRecordingAsync(const FMagicLeapCameraStartRecording& InResultDelegate)
{
	FMagicLeapCameraStartRecordingMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapCameraPlugin().StartRecordingAsync(ResultDelegate);
}

bool UMagicLeapCameraFunctionLibrary::StopRecordingAsync(const FMagicLeapCameraStopRecording& InResultDelegate)
{
	FMagicLeapCameraStopRecordingMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapCameraPlugin().StopRecordingAsync(ResultDelegate);
}

bool UMagicLeapCameraFunctionLibrary::SetLogDelegate(const FMagicLeapCameraLogMessage& InLogDelegate)
{
	FMagicLeapCameraLogMessageMulti LogDelegate;
	LogDelegate.Add(InLogDelegate);
	return GetMagicLeapCameraPlugin().SetLogDelegate(LogDelegate);
}

bool UMagicLeapCameraFunctionLibrary::IsCapturing()
{
	return GetMagicLeapCameraPlugin().IsCapturing();
}
