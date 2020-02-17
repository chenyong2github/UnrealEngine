// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCVCameraRunnable.h"
#include "MagicLeapCVCameraModule.h"
#include "MagicLeapMath.h"
#include "Engine/Texture2D.h"
#include "Lumin/CAPIShims/LuminAPIMediaError.h"
#include "Lumin/CAPIShims/LuminAPIHeadTracking.h"

FCVCameraRunnable::FCVCameraRunnable()
	: FMagicLeapRunnable(
		{
			EMagicLeapPrivilege::CameraCapture,
			EMagicLeapPrivilege::ComputerVision,
			EMagicLeapPrivilege::AudioCaptureMic,
			EMagicLeapPrivilege::VoiceInput
		}, TEXT("FCVCameraRunnable"))
#if WITH_MLSDK
	, HeadTracker(ML_INVALID_HANDLE)
	, CVCameraTracker(ML_INVALID_HANDLE)
#endif // WITH_MLSDK
	, ThreadSafeConnectionStatus(static_cast<int32>(EConnectionStatus::NotConnected))
	, bWasConnectedOnPause(false)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
}

void FCVCameraRunnable::Exit()
{
#if WITH_MLSDK
	TryDisconnect();
#endif // WITH_MLSDK
}

void FCVCameraRunnable::PushNewCaptureTask(FCVCameraTask::EType InTaskType)
{
#if WITH_MLSDK
	if (InTaskType == FCVCameraTask::EType::Disconnect && GetConnectionStatus() == EConnectionStatus::NotConnected)
	{
		return;
	}

	if (InTaskType == FCVCameraTask::EType::Connect && GetConnectionStatus() != EConnectionStatus::NotConnected)
	{
		return;
	}

	if (InTaskType != FCVCameraTask::EType::Connect &&
		InTaskType != FCVCameraTask::EType::Disconnect &&
		GetConnectionStatus() == EConnectionStatus::NotConnected)
	{
		FCVCameraTask ConnectTask;
		ConnectTask.CaptureType = FCVCameraTask::EType::Connect;
		PushNewTask(ConnectTask);
	}
	FCVCameraTask NewCaptureTask;
	NewCaptureTask.CaptureType = InTaskType;
	PushNewTask(NewCaptureTask);
#endif // WITH_MLSDK
}

bool FCVCameraRunnable::IsConnected() const
{
	return GetConnectionStatus() == EConnectionStatus::Connected;
}

bool FCVCameraRunnable::GetIntrinsicCalibrationParameters(FMagicLeapCVCameraIntrinsicCalibrationParameters& OutParams)
{
#if WITH_MLSDK
	FScopeLock Lock(&CriticalSection);
	MLCVCameraIntrinsicCalibrationParameters Params;
	MLCVCameraIntrinsicCalibrationParametersInit(&Params);
	MLResult Result = MLCVCameraGetIntrinsicCalibrationParameters(CVCameraTracker, MLCVCameraID::MLCVCameraID_ColorCamera, &Params);
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCVCameraGetIntrinsicCalibrationParameters failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}

	MLToUEIntrinsicCalibrationParameters(Params, OutParams);
#else
	(void)OutParams;
#endif // WITH_MLSDK
	return true;
}

bool FCVCameraRunnable::GetFramePose(FTransform& OutPose)
{
#if WITH_MLSDK
	FScopeLock Lock(&CriticalSection);
	MLTransform Transform;
	MLResult Result = MLCVCameraGetFramePose(CVCameraTracker, HeadTracker, MLCVCameraID_ColorCamera, CVCameraExtras.vcam_timestamp_us * 1000, &Transform);
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCVCameraGetFramePose failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}

	const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
	float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();
	check(WorldToMetersScale != 0);
	OutPose = MagicLeap::ToFTransform(Transform, WorldToMetersScale);
#else
	(void)OutPose;
#endif // WITH_MLSDK
	return true;
}

bool FCVCameraRunnable::GetCameraOutput(FMagicLeapCameraOutput& OutCameraOutput)
{
#if WITH_MLSDK
	FScopeLock Lock(&CriticalSection);
	if (CVCameraOutput.Planes.Num() == 0)
	{
		Log(TEXT("GetCameraOutput called before receiving data from the camera."));
		return false;
	}

	OutCameraOutput = CVCameraOutput;
#else
	(void)OutCameraOutput;
#endif // WITH_MLSDK
	return true;
}


void FCVCameraRunnable::Pause()
{
#if WITH_MLSDK
	bWasConnectedOnPause = IsConnected();
	// Cancel the current video recording (if one is active).
	if (CurrentTask.CaptureType == FCVCameraTask::EType::Enable)
	{
		StopCaptureRawVideo();
		CurrentTask.bSuccess = false;
		PushCompletedTask(CurrentTask);
	}
	// Cancel any incoming tasks.
	CancelIncomingTasks();
	// Disconnect camera if connected
	TryDisconnect();
#endif // WITH_MLSDK
}

void FCVCameraRunnable::Resume()
{
	if (bWasConnectedOnPause)
	{
		FCVCameraTask ConnectTask;
		ConnectTask.CaptureType = FCVCameraTask::EType::Connect;
		PushNewTask(ConnectTask);
	}
}

bool FCVCameraRunnable::ProcessCurrentTask()
{
	bool bSuccess = false;

#if WITH_MLSDK
	switch (CurrentTask.CaptureType)
	{
	case FCVCameraTask::EType::None: bSuccess = false; checkf(false, TEXT("Invalid camera task encountered!")); break;
	case FCVCameraTask::EType::Connect:
	{
		SetConnectionStatus(EConnectionStatus::Connecting);
		bSuccess = TryConnect(false);
		SetConnectionStatus(bSuccess ? EConnectionStatus::Connected : EConnectionStatus::NotConnected);
	}
	break;
	case FCVCameraTask::EType::Disconnect: bSuccess = TryDisconnect(); break;
	case FCVCameraTask::EType::Enable: bSuccess = StartCaptureRawVideo(); break;
	case FCVCameraTask::EType::Disable: bSuccess = StopCaptureRawVideo(); break;
	}
#endif // WITH_MLSDK
	return bSuccess;
}

#if WITH_MLSDK
void FCVCameraRunnable::OnRawBufferAvailable(const MLCameraOutput* Output, const MLCameraResultExtras* Extra, const MLCameraFrameMetadata* Metadata, void* Context)
{
	FCVCameraRunnable* This = static_cast<FCVCameraRunnable*>(Context);
	FScopeLock Lock(&This->CriticalSection);
	This->MLToUECameraOutput(*Output, This->CVCameraOutput);
	This->CVCameraExtras = *Extra;
	This->CVCameraMetadata = *Metadata;
}

bool FCVCameraRunnable::TryConnect(bool bConnectCV)
{
	if ((AppEventHandler.GetPrivilegeStatus(EMagicLeapPrivilege::ComputerVision) != MagicLeap::EPrivilegeState::Granted) ||
		(AppEventHandler.GetPrivilegeStatus(EMagicLeapPrivilege::CameraCapture) != MagicLeap::EPrivilegeState::Granted))
	{
		Log(TEXT("Cannot connect to camera due to lack of privilege!"));
		return false;
	}

	if (bPaused) return false;

	MLResult Result = MLResult_Ok;
	{
		FScopeLock Lock(&CriticalSection);
		Result = MLHeadTrackingCreate(&HeadTracker);
		if (Result != MLResult_Ok)
		{
			Log(FString::Printf(TEXT("MLCVCameraTrackingCreate failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
			CancelIncomingTasks();
			return false;
		}

		Result = MLCVCameraTrackingCreate(&CVCameraTracker);
		if (Result != MLResult_Ok)
		{
			Log(FString::Printf(TEXT("MLCVCameraTrackingCreate failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
			CancelIncomingTasks();
			return false;
		}
	}

	if (bPaused) return false;

	MLCameraCaptureCallbacksExInit(&CaptureCallbacks);
	CaptureCallbacks.on_video_buffer_available = OnRawBufferAvailable;
	Result = MLCameraSetCaptureCallbacksEx(&CaptureCallbacks, this);
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraSetCaptureCallbacksEx failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		CancelIncomingTasks();
		return false;
	}

	if (bPaused) return false;

	Result = MLCameraSetOutputFormat(MLCameraOutputFormat_YUV_420_888);
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraSetOutputFormat failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		CancelIncomingTasks();
		return false;
	}

	if (bPaused) return false;

	Result = MLCameraConnect();

	if (bPaused) return false;

	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraConnect failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		CancelIncomingTasks();
		return false;
	}
	
	return true;
}

bool FCVCameraRunnable::TryDisconnect()
{
	if (IsConnected())
	{
		MLResult Result = MLCameraDisconnect();
		if (Result != MLResult_Ok)
		{
			Log(FString::Printf(TEXT("MLCameraDisconnect failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		}
		else
		{
			SetConnectionStatus(EConnectionStatus::NotConnected);
		}

		FScopeLock Lock(&CriticalSection);
		Result = MLCVCameraTrackingDestroy(CVCameraTracker);
		if (Result != MLResult_Ok)
		{
			Log(FString::Printf(TEXT("MLCVCameraTrackingDestroy failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		}
		else
		{
			CVCameraTracker = ML_INVALID_HANDLE;
		}

		Result = MLHeadTrackingDestroy(HeadTracker);
		if (Result != MLResult_Ok)
		{
			Log(FString::Printf(TEXT("MLHeadTrackingDestroy failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		}
		else
		{
			HeadTracker = ML_INVALID_HANDLE;
		}
	}

	return !IsConnected();
}

bool FCVCameraRunnable::StartCaptureRawVideo()
{
	if (bPaused) return false;

	Log(TEXT("Beginning raw video capture."));
	MLHandle Handle = ML_INVALID_HANDLE;
	MLResult Result = MLCameraPrepareCapture(MLCameraCaptureType_VideoRaw, &Handle);
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraPrepareCapture failed with error %s!  Camera capture aborted!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}

	if (bPaused) return false;

	Result = MLCameraCaptureRawVideoStart();
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraCaptureRawVideoStart failed with error %s!  Video capture aborted!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}

	return true;
}

bool FCVCameraRunnable::StopCaptureRawVideo()
{
	MLResult Result = MLCameraCaptureVideoStop();
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraCaptureVideoStop failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}

	return true;
}

void FCVCameraRunnable::Log(const FString& Info)
{
	FCVCameraTask LogTask;
	LogTask.CaptureType = FCVCameraTask::EType::Log;
	LogTask.Log = Info;
	PushCompletedTask(LogTask);
	UE_LOG(LogMagicLeapCVCamera, Log, TEXT("%s"), *Info);
}

void FCVCameraRunnable::MLToUEIntrinsicCalibrationParameters(const MLCVCameraIntrinsicCalibrationParameters& InMLParams,
	FMagicLeapCVCameraIntrinsicCalibrationParameters& OutUEParams)
{
	const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
	float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();
	check(WorldToMetersScale != 0);

	OutUEParams.Version = InMLParams.version;
	OutUEParams.Width = InMLParams.width;
	OutUEParams.Height = InMLParams.height;
	OutUEParams.FocalLength = FVector2D(InMLParams.focal_length.x * WorldToMetersScale, InMLParams.focal_length.y * WorldToMetersScale);
	OutUEParams.PrincipalPoint = FVector2D(InMLParams.principal_point.x * WorldToMetersScale, InMLParams.principal_point.y * WorldToMetersScale);
	OutUEParams.FOV = InMLParams.fov;
	for (uint32 DistortionIndex = 0; DistortionIndex < MLCVCameraIntrinsics_MaxDistortionCoefficients; ++DistortionIndex)
	{
		OutUEParams.Distortion.Add(InMLParams.distortion[DistortionIndex]);
	}
}

void FCVCameraRunnable::MLToUECameraOutput(const MLCameraOutput& InMLCameraOutput, FMagicLeapCameraOutput& OutUECameraOutput)
{
	OutUECameraOutput.Planes.Empty();
	for (uint8 PlaneIndex = 0; PlaneIndex < InMLCameraOutput.plane_count; ++PlaneIndex)
	{
		MLCameraPlaneInfo MLPlaneInfo = InMLCameraOutput.planes[PlaneIndex];
		FMagicLeapCameraPlaneInfo& UEPlaneInfo = OutUECameraOutput.Planes.AddDefaulted_GetRef();
		UEPlaneInfo.Width = MLPlaneInfo.width;
		UEPlaneInfo.Height = MLPlaneInfo.height;
		UEPlaneInfo.Stride = MLPlaneInfo.stride;
		UEPlaneInfo.BytesPerPixel = MLPlaneInfo.bytes_per_pixel;
		UEPlaneInfo.Data.AddZeroed(MLPlaneInfo.size);
		FMemory::Memcpy(UEPlaneInfo.Data.GetData(), MLPlaneInfo.data, MLPlaneInfo.size);
	}

	OutUECameraOutput.Format = MLToUEPixelFormat(InMLCameraOutput.format);
}

EPixelFormat FCVCameraRunnable::MLToUEPixelFormat(MLCameraOutputFormat MLPixelFormat)
{
	EPixelFormat PixelFormat = EPixelFormat::PF_Unknown;
	switch (MLPixelFormat)
	{
	case MLCameraOutputFormat_Unknown: break;
	case MLCameraOutputFormat_YUV_420_888: break;
	case MLCameraOutputFormat_JPEG: PixelFormat = PF_R8G8B8A8;
	}

	return PixelFormat;
}
#endif //WITH_MLSDK

void FCVCameraRunnable::SetConnectionStatus(EConnectionStatus ConnectionStatus)
{
	ThreadSafeConnectionStatus.Set(static_cast<int32>(ConnectionStatus));
}

FCVCameraRunnable::EConnectionStatus FCVCameraRunnable::GetConnectionStatus() const
{
	return static_cast<EConnectionStatus>(ThreadSafeConnectionStatus.GetValue());
}
