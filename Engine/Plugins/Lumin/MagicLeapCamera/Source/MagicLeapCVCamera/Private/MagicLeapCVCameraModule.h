// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMagicLeapPlugin.h"
#include "IMagicLeapCVCameraModule.h"
#include "MagicLeapCVCameraRunnable.h"
#include "MagicLeapCameraTypes.h"
#include "MagicLeapCVCameraTypes.h"
#include "AppEventHandler.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapCVCamera, Verbose, All);

class FMagicLeapCVCameraModule : public IMagicLeapCVCameraModule
{
public:
	FMagicLeapCVCameraModule();
	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime) override;
	
	bool EnableAsync(const FMagicLeapCVCameraEnableStatic& ResultDelegate);
	bool DisableAsync(const FMagicLeapCVCameraDisableStatic& ResultDelegate);

	bool EnableAsync(const FMagicLeapCVCameraEnableMulti& ResultDelegate);
	bool DisableAsync(const FMagicLeapCVCameraDisableMulti& ResultDelegate);

	virtual bool GetIntrinsicCalibrationParameters(FMagicLeapCVCameraIntrinsicCalibrationParameters& OutParams) override;
	virtual bool GetFramePose(FTransform& OutFramePose) override;
	virtual bool GetCameraOutput(FMagicLeapCameraOutput& OutCameraOutput) override;
	bool SetLogDelegate(const FMagicLeapCameraLogMessageMulti& LogDelegate);

private:
	bool TryPushNewCaptureTask(FCVCameraTask::EType InTaskType);
	void OnAppPause();

	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	float MinCameraRunTimer;
	MagicLeap::IAppEventHandler AppEventHandler;
	FCVCameraRunnable* Runnable;
	FCVCameraTask::EType CurrentTaskType;
	FCVCameraTask::EType PrevTaskType;
	FMagicLeapCVCameraEnableStatic OnEnabledStatic;
	FMagicLeapCVCameraEnableMulti OnEnabled;
	FMagicLeapCVCameraDisableStatic OnDisabledStatic;
	FMagicLeapCVCameraDisableMulti OnDisabled;
	FMagicLeapCameraLogMessageMulti OnLogMessage;
};

inline FMagicLeapCVCameraModule& GetMagicLeapCVCameraModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapCVCameraModule>("MagicLeapCVCamera");
}
