// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCVCameraModule.h"
#include "Async/Async.h"
#include "Lumin/CAPIShims/LuminAPI.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY(LogMagicLeapCVCamera);

FMagicLeapCVCameraModule::FMagicLeapCVCameraModule()
: MinCameraRunTimer(0.0f)
, Runnable(nullptr)
, CurrentTaskType(FCVCameraTask::EType::None)
, PrevTaskType(FCVCameraTask::EType::None)
{
}

void FMagicLeapCVCameraModule::StartupModule()
{
	IMagicLeapCVCameraModule::StartupModule();
	Runnable = new FCVCameraRunnable();
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapCVCameraModule::Tick);

	AppEventHandler.SetOnAppPauseHandler([this]()
	{
		OnAppPause();
	});
}

void FMagicLeapCVCameraModule::ShutdownModule()
{
	FCVCameraRunnable* InRunnable = Runnable;
	Runnable = nullptr;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [InRunnable]()
	{
		delete InRunnable;
	});
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	IModuleInterface::ShutdownModule();
}

bool FMagicLeapCVCameraModule::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMagicLeapCVCameraModule_Tick);

	MinCameraRunTimer -= DeltaTime;
	FCVCameraTask CompletedTask;
	if (Runnable->TryGetCompletedTask(CompletedTask))
	{
		switch (CompletedTask.CaptureType)
		{
		case FCVCameraTask::EType::Enable:
		{
			OnEnabled.Broadcast(CompletedTask.bSuccess);
			PrevTaskType = CurrentTaskType;
			// do not reset CurrentTaskType if start recording is successful
			// as this constitutes an ongoing capture state
			if (!CompletedTask.bSuccess)
			{
				CurrentTaskType = FCVCameraTask::EType::None;
			}
		}
		break;

		case FCVCameraTask::EType::Disable:
		{
			OnDisabled.Broadcast(CompletedTask.bSuccess);
			PrevTaskType = CurrentTaskType;
			CurrentTaskType = FCVCameraTask::EType::None;
		}
		break;

		case FCVCameraTask::EType::Log:
		{
			UE_LOG(LogMagicLeapCVCamera, Log, TEXT("%s"), *CompletedTask.Log);
			OnLogMessage.Broadcast(FString::Printf(TEXT("<br>%s"), *CompletedTask.Log));
		}
		break;
		}
	}

	return true;
}

bool FMagicLeapCVCameraModule::EnableAsync(const FMagicLeapCVCameraEnableStatic& ResultDelegate)
{
	if (TryPushNewCaptureTask(FCVCameraTask::EType::Enable))
	{
		TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
		static constexpr float MinCameraRunDuration = 2.0f;
		MinCameraRunTimer = MinCameraRunDuration;
		OnEnabledStatic = ResultDelegate;
		return true;
	}

	return false;
}

bool FMagicLeapCVCameraModule::EnableAsync(const FMagicLeapCVCameraEnableMulti& ResultDelegate)
{
	if (TryPushNewCaptureTask(FCVCameraTask::EType::Enable))
	{
		TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
		static constexpr float MinCameraRunDuration = 2.0f;
		MinCameraRunTimer = MinCameraRunDuration;
		OnEnabled = ResultDelegate;
		return true;
	}

	return false;
}

bool FMagicLeapCVCameraModule::DisableAsync(const FMagicLeapCVCameraDisableStatic& ResultDelegate)
{
	if (MinCameraRunTimer < 0.0f && TryPushNewCaptureTask(FCVCameraTask::EType::Disable))
	{
		FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
		OnDisabledStatic = ResultDelegate;
		return true;
	}

	return false;
}

bool FMagicLeapCVCameraModule::DisableAsync(const FMagicLeapCVCameraDisableMulti& ResultDelegate)
{
	if (MinCameraRunTimer < 0.0f && TryPushNewCaptureTask(FCVCameraTask::EType::Disable))
	{
		FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
		OnDisabled = ResultDelegate;
		return true;
	}

	return false;
}

bool FMagicLeapCVCameraModule::GetIntrinsicCalibrationParameters(FMagicLeapCVCameraIntrinsicCalibrationParameters& OutParams)
{
	return Runnable->GetIntrinsicCalibrationParameters(OutParams);
}

bool FMagicLeapCVCameraModule::GetFramePose(FTransform& OutFramePose)
{
	return Runnable->GetFramePose(OutFramePose);
}

bool FMagicLeapCVCameraModule::GetCameraOutput(FMagicLeapCameraOutput& OutCameraOutput)
{
	return Runnable->GetCameraOutput(OutCameraOutput);
}

bool FMagicLeapCVCameraModule::SetLogDelegate(const FMagicLeapCameraLogMessageMulti& LogDelegate)
{
	OnLogMessage = LogDelegate;
	return true;
}

bool FMagicLeapCVCameraModule::TryPushNewCaptureTask(FCVCameraTask::EType InTaskType)
{
	bool bCanPushTask = false;

	switch (InTaskType)
	{
	case FCVCameraTask::EType::None:
	{
		bCanPushTask = false;
	}
	break;

	case FCVCameraTask::EType::Enable:
	{
		bCanPushTask = CurrentTaskType == FCVCameraTask::EType::None && PrevTaskType != FCVCameraTask::EType::Enable;
	}
	break;

	case FCVCameraTask::EType::Disable:
	{
		bCanPushTask = PrevTaskType != FCVCameraTask::EType::Disable &&
			(CurrentTaskType == FCVCameraTask::EType::None || CurrentTaskType == FCVCameraTask::EType::Enable);
	}
	break;

	case FCVCameraTask::EType::Log:
	{
		bCanPushTask = true;
	}
	break;
	}

	if (bCanPushTask)
	{
		if (InTaskType != FCVCameraTask::EType::Log)
		{
			CurrentTaskType = InTaskType;
		}

		Runnable->PushNewCaptureTask(InTaskType);
		return true;
	}

	return false;
}

void FMagicLeapCVCameraModule::OnAppPause()
{
	// Cancel the current video recording (if one is active).
	if (CurrentTaskType == FCVCameraTask::EType::Enable)
	{
		PrevTaskType = CurrentTaskType;
		CurrentTaskType = FCVCameraTask::EType::Disable;
		// The runnable will take care of terminating the video on it's own.
	}
}

IMPLEMENT_MODULE(FMagicLeapCVCameraModule, MagicLeapCVCamera);
