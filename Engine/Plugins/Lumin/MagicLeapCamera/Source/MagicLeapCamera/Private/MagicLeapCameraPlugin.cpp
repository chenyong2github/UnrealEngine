// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCameraPlugin.h"
#include "Async/Async.h"
#include "Lumin/CAPIShims/LuminAPI.h"

DEFINE_LOG_CATEGORY(LogMagicLeapCamera);

FMagicLeapCameraPlugin::FMagicLeapCameraPlugin()
: UserCount(0)
, Runnable(nullptr)
, CurrentTaskType(FCameraTask::EType::None)
, PrevTaskType(FCameraTask::EType::None)
{
}

void FMagicLeapCameraPlugin::StartupModule()
{
	IMagicLeapCameraPlugin::StartupModule();
	Runnable = new FCameraRunnable();
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapCameraPlugin::Tick);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);

	AppEventHandler.SetOnAppPauseHandler([this]()
	{
		OnAppPause();
	});
}

void FMagicLeapCameraPlugin::ShutdownModule()
{
	FCameraRunnable* InRunnable = Runnable;
	Runnable = nullptr;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [InRunnable]()
	{
		delete InRunnable;
	});
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	IModuleInterface::ShutdownModule();
}

bool FMagicLeapCameraPlugin::Tick(float DeltaTime)
{
	FCameraTask CompletedTask;
	if (Runnable->TryGetCompletedTask(CompletedTask))
	{
		switch (CompletedTask.CaptureType)
		{
		case FCameraTask::EType::Connect:
		{
			if (OnCameraConnect.IsBound())
			{
				OnCameraConnect.ExecuteIfBound(CompletedTask.bSuccess);
				// Only clear CurrentTaskType if connect was manually called
				// (otherwise it was auto-called from within the runnable and the actual
				// current task is still in progress).
				PrevTaskType = CurrentTaskType;
				CurrentTaskType = FCameraTask::EType::None;
			}
		}
		break;

		case FCameraTask::EType::Disconnect:
		{
			OnCameraDisconnect.ExecuteIfBound(CompletedTask.bSuccess);
			PrevTaskType = CurrentTaskType;
			CurrentTaskType = FCameraTask::EType::None;
		}
		break;

		case FCameraTask::EType::ImageToFile:
		{
			OnCaptureImgToFile.Broadcast(CompletedTask.bSuccess, CompletedTask.FilePath);
			PrevTaskType = CurrentTaskType;
			CurrentTaskType = FCameraTask::EType::None;
		}
		break;

		case FCameraTask::EType::ImageToTexture:
		{
			OnCaptureImgToTexture.Broadcast(CompletedTask.bSuccess, CompletedTask.Texture);
			PrevTaskType = CurrentTaskType;
			CurrentTaskType = FCameraTask::EType::None;
		}
		break;

		case FCameraTask::EType::StartVideoToFile:
		{
			OnStartRecording.Broadcast(CompletedTask.bSuccess);
			PrevTaskType = CurrentTaskType;
			// do not reset CurrentTaskType if start recording is successful
			// as this constitutes an ongoing capture state
			if (!CompletedTask.bSuccess)
			{
				CurrentTaskType = FCameraTask::EType::None;
			}
		}
		break;

		case FCameraTask::EType::StopVideoToFile:
		{
			OnStopRecording.Broadcast(CompletedTask.bSuccess, CompletedTask.FilePath);
			PrevTaskType = CurrentTaskType;
			CurrentTaskType = FCameraTask::EType::None;
		}
		break;

		case FCameraTask::EType::Log:
		{
			UE_LOG(LogMagicLeapCamera, Log, TEXT("%s"), *CompletedTask.Log);
			OnLogMessage.Broadcast(FString::Printf(TEXT("<br>%s"), *CompletedTask.Log));
		}
		break;
		}
	}

	return true;
}

bool FMagicLeapCameraPlugin::CameraConnect(const FMagicLeapCameraConnect& ResultDelegate)
{
	if (TryPushNewCaptureTask(FCameraTask::EType::Connect))
	{
		OnCameraConnect = ResultDelegate;
		return true;
	}

	return false;
}

bool FMagicLeapCameraPlugin::CameraDisconnect(const FMagicLeapCameraDisconnect& ResultDelegate)
{
	if (UserCount <= 0 && TryPushNewCaptureTask(FCameraTask::EType::Disconnect))
	{
		OnCameraDisconnect = ResultDelegate;
		return true;
	}

	return false;
}

int64 FMagicLeapCameraPlugin::GetPreviewHandle() const
{
	return FCameraRunnable::PreviewHandle.GetValue();
}

void FMagicLeapCameraPlugin::IncUserCount()
{
	++UserCount;
}

void FMagicLeapCameraPlugin::DecUserCount()
{
	--UserCount;
	if (UserCount <= 0)
	{
		UserCount = 0;
		TryPushNewCaptureTask(FCameraTask::EType::Disconnect);
	}
}

bool FMagicLeapCameraPlugin::CaptureImageToFileAsync(const FMagicLeapCameraCaptureImgToFileMulti& ResultDelegate)
{
	if (TryPushNewCaptureTask(FCameraTask::EType::ImageToFile))
	{
		OnCaptureImgToFile = ResultDelegate;
		return true;
	}

	return false;
}

bool FMagicLeapCameraPlugin::CaptureImageToTextureAsync(const FMagicLeapCameraCaptureImgToTextureMulti& ResultDelegate)
{
	if (TryPushNewCaptureTask(FCameraTask::EType::ImageToTexture))
	{
		OnCaptureImgToTexture = ResultDelegate;
		return true;
	}

	return false;
}

bool FMagicLeapCameraPlugin::StartRecordingAsync(const FMagicLeapCameraStartRecordingMulti& ResultDelegate)
{
	if (TryPushNewCaptureTask(FCameraTask::EType::StartVideoToFile))
	{
		OnStartRecording = ResultDelegate;
		return true;
	}

	return false;
}

bool FMagicLeapCameraPlugin::StopRecordingAsync(const FMagicLeapCameraStopRecordingMulti& ResultDelegate)
{
	if (TryPushNewCaptureTask(FCameraTask::EType::StopVideoToFile))
	{
		OnStopRecording = ResultDelegate;
		return true;
	}

	return false;
}

bool FMagicLeapCameraPlugin::SetLogDelegate(const FMagicLeapCameraLogMessageMulti& LogDelegate)
{
	OnLogMessage = LogDelegate;
	return true;
}

bool FMagicLeapCameraPlugin::IsCapturing() const
{
	return CurrentTaskType != FCameraTask::EType::None;
}

bool FMagicLeapCameraPlugin::TryPushNewCaptureTask(FCameraTask::EType InTaskType)
{
	bool bCanPushTask = false;

	switch (InTaskType)
	{
	case FCameraTask::EType::None:
	{
		bCanPushTask = false;
	}
	break;

	case FCameraTask::EType::Connect:
	{
		bCanPushTask = CurrentTaskType == FCameraTask::EType::None || CurrentTaskType == FCameraTask::EType::Disconnect;
	}
	break;

	case FCameraTask::EType::Disconnect:
	{
		bCanPushTask = CurrentTaskType != FCameraTask::EType::Disconnect;
	}
	break;

	case FCameraTask::EType::ImageToFile:
	{
		bCanPushTask = CurrentTaskType == FCameraTask::EType::None;
	}
	break;

	case FCameraTask::EType::ImageToTexture:
	{
		bCanPushTask = CurrentTaskType == FCameraTask::EType::None;
	}
	break;

	case FCameraTask::EType::StartVideoToFile:
	{
		bCanPushTask = CurrentTaskType == FCameraTask::EType::None && PrevTaskType != FCameraTask::EType::StartVideoToFile;
	}
	break;

	case FCameraTask::EType::StopVideoToFile:
	{
		bCanPushTask = PrevTaskType != FCameraTask::EType::StopVideoToFile &&
			(CurrentTaskType == FCameraTask::EType::None || CurrentTaskType == FCameraTask::EType::StartVideoToFile);
	}
	break;

	case FCameraTask::EType::Log:
	{
		bCanPushTask = true;
	}
	break;
	}

	if (bCanPushTask)
	{
		if (InTaskType != FCameraTask::EType::Log)
		{
			CurrentTaskType = InTaskType;
		}

		Runnable->PushNewCaptureTask(InTaskType);
		return true;
	}

	return false;
}

void FMagicLeapCameraPlugin::OnAppPause()
{
	// Cancel the current video recording (if one is active).
	if (CurrentTaskType == FCameraTask::EType::StartVideoToFile)
	{
		PrevTaskType = CurrentTaskType;
		CurrentTaskType = FCameraTask::EType::StopVideoToFile;
		// The runnable will take care of terminating the video on it's own.
	}
}
