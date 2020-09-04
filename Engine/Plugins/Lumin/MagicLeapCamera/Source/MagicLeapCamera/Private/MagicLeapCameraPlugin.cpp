// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCameraPlugin.h"
#include "Async/Async.h"
#include "Lumin/CAPIShims/LuminAPI.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY(LogMagicLeapCamera);

FMagicLeapCameraPlugin::FMagicLeapCameraPlugin()
: UserCount(0)
, CaptureState(ECaptureState::Idle)
, MinVidCaptureTimer(0.0f)
, AppEventHandler({ EMagicLeapPrivilege::CameraCapture, EMagicLeapPrivilege::AudioCaptureMic, EMagicLeapPrivilege::VoiceInput })
, Runnable(nullptr)
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
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMagicLeapCameraPlugin_Tick);

	MinVidCaptureTimer -= DeltaTime;
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
				// Only clear CaptureState if connect was manually called
				// (otherwise it was auto-called from within the runnable and the actual
				// current task is still in progress).
				CaptureState = ECaptureState::Idle;
			}
		}
		break;

		case FCameraTask::EType::Disconnect:
		{
			OnCameraDisconnect.ExecuteIfBound(CompletedTask.bSuccess);
			CaptureState = ECaptureState::Idle;
		}
		break;

		case FCameraTask::EType::ImageToFile:
		{
			OnCaptureImgToFile.Broadcast(CompletedTask.bSuccess, CompletedTask.FilePath);
			CaptureState = ECaptureState::Idle;
		}
		break;

		case FCameraTask::EType::ImageToTexture:
		{
			OnCaptureImgToTexture.Broadcast(CompletedTask.bSuccess, CompletedTask.Texture);
			CaptureState = ECaptureState::Idle;
		}
		break;

		case FCameraTask::EType::StartVideoToFile:
		{
			OnStartRecording.Broadcast(CompletedTask.bSuccess);
			// do not reset CaptureState if start recording is successful
			// as this constitutes an ongoing capture state
			if (!CompletedTask.bSuccess)
			{
				CaptureState = ECaptureState::Idle;
			}
			else
			{
				CaptureState = ECaptureState::Capturing;
			}
		}
		break;

		case FCameraTask::EType::StopVideoToFile:
		{
			OnStopRecording.Broadcast(CompletedTask.bSuccess, CompletedTask.FilePath);
			CaptureState = ECaptureState::Idle;
		}
		break;

		case FCameraTask::EType::Log:
		{
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
		static constexpr float MinVidCaptureDuration = 2.0f;
		MinVidCaptureTimer = MinVidCaptureDuration;
		CaptureState = ECaptureState::BeginningCapture;
		OnStartRecording = ResultDelegate;
		return true;
	}

	return false;
}

bool FMagicLeapCameraPlugin::StopRecordingAsync(const FMagicLeapCameraStopRecordingMulti& ResultDelegate)
{
	if (MinVidCaptureTimer < 0.0f && TryPushNewCaptureTask(FCameraTask::EType::StopVideoToFile))
	{
		CaptureState = ECaptureState::EndingCapture;
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
	return CaptureState != ECaptureState::Idle;
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
		bCanPushTask = CaptureState == ECaptureState::Idle || CaptureState == ECaptureState::Disconnecting;
	}
	break;

	case FCameraTask::EType::Disconnect:
	{
		bCanPushTask = CaptureState != ECaptureState::Disconnecting;
	}
	break;

	case FCameraTask::EType::ImageToFile:
	case FCameraTask::EType::ImageToTexture:
	case FCameraTask::EType::StartVideoToFile:
	{
		bCanPushTask = CaptureState == ECaptureState::Idle;
	}
	break;

	case FCameraTask::EType::StopVideoToFile:
	{
		bCanPushTask = CaptureState == ECaptureState::Capturing;
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
		Runnable->PushNewCaptureTask(InTaskType);
		return true;
	}

	return false;
}

void FMagicLeapCameraPlugin::OnAppPause()
{
	// Cancel the current video recording (if one is active).
	if (CaptureState == ECaptureState::BeginningCapture || CaptureState == ECaptureState::Capturing)
	{
		CaptureState = ECaptureState::Idle;
		// The runnable will take care of terminating the video on it's own.
	}
}

const TCHAR* FMagicLeapCameraPlugin::CaptureStateToString(ECaptureState InCaptureState)
{
	const TCHAR* CaptureStateString = TEXT("Invalid");
	switch (InCaptureState)
	{
		case ECaptureState::Idle: CaptureStateString = TEXT("Idle"); break;
		case ECaptureState::Connecting: CaptureStateString = TEXT("Connecting"); break;
		case ECaptureState::Disconnecting: CaptureStateString = TEXT("Disconnecting"); break;
		case ECaptureState::BeginningCapture: CaptureStateString = TEXT("BeginningCapture"); break;
		case ECaptureState::Capturing: CaptureStateString = TEXT("Capturing"); break;
		case ECaptureState::EndingCapture: CaptureStateString = TEXT("EndingCapture"); break;
	}

	return CaptureStateString;
}
