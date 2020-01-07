// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMagicLeapPlugin.h"
#include "IMagicLeapCameraPlugin.h"
#include "MagicLeapCameraRunnable.h"
#include "MagicLeapCameraTypes.h"
#include "MagicLeapPluginUtil.h"
#include "AppEventHandler.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapCamera, Verbose, All);

class FMagicLeapCameraPlugin : public IMagicLeapCameraPlugin
{
public:
	FMagicLeapCameraPlugin();
	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime) override;
	bool CameraConnect(const FMagicLeapCameraConnect& ResultDelegate) override;
	bool CameraDisconnect(const FMagicLeapCameraDisconnect& ResultDelegate) override;
	int64 GetPreviewHandle() const override;

	void IncUserCount();
	void DecUserCount();
	bool CaptureImageToFileAsync(const FMagicLeapCameraCaptureImgToFileMulti& ResultDelegate);
	bool CaptureImageToTextureAsync(const FMagicLeapCameraCaptureImgToTextureMulti& ResultDelegate);
	bool StartRecordingAsync(const FMagicLeapCameraStartRecordingMulti& ResultDelegate);
	bool StopRecordingAsync(const FMagicLeapCameraStopRecordingMulti& ResultDelegate);
	bool SetLogDelegate(const FMagicLeapCameraLogMessageMulti& LogDelegate);
	bool IsCapturing() const;

private:
	bool TryPushNewCaptureTask(FCameraTask::EType InTaskType);
	void OnAppPause();

	FMagicLeapAPISetup APISetup;
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	uint32 UserCount;
	MagicLeap::IAppEventHandler AppEventHandler;
	FCameraRunnable* Runnable;
	FCameraTask::EType CurrentTaskType;
	FCameraTask::EType PrevTaskType;
	FMagicLeapCameraConnect OnCameraConnect;
	FMagicLeapCameraDisconnect OnCameraDisconnect;
	FMagicLeapCameraCaptureImgToFileMulti OnCaptureImgToFile;
	FMagicLeapCameraCaptureImgToTextureMulti OnCaptureImgToTexture;
	FMagicLeapCameraStartRecordingMulti OnStartRecording;
	FMagicLeapCameraStopRecordingMulti OnStopRecording;
	FMagicLeapCameraLogMessageMulti OnLogMessage;
};

inline FMagicLeapCameraPlugin& GetMagicLeapCameraPlugin()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapCameraPlugin>("MagicLeapCamera");
}
