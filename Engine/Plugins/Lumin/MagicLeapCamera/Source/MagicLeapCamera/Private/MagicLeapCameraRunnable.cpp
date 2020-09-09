// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCameraRunnable.h"
#include "MagicLeapCameraPlugin.h"
#include "Engine/Texture2D.h"
#include "Lumin/LuminPlatformFile.h"
#include "Lumin/CAPIShims/LuminAPIMediaError.h"

#if WITH_MLSDK
FThreadSafeCounter64 FCameraRunnable::PreviewHandle = ML_INVALID_HANDLE;
#else
FThreadSafeCounter64 FCameraRunnable::PreviewHandle = 0;
#endif //WITH_MLSDK

FCameraRunnable::FCameraRunnable()
	: FMagicLeapRunnable({ EMagicLeapPrivilege::CameraCapture, EMagicLeapPrivilege::AudioCaptureMic, EMagicLeapPrivilege::VoiceInput }, TEXT("FCameraRunnable"))
	, ThreadSafeConnectionStatus(static_cast<int32>(EConnectionStatus::NotConnected))
	, bWasConnectedOnPause(false)
	, ImgExtension(".jpeg")
	, VidExtension(".mp4")
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
}

void FCameraRunnable::PushNewCaptureTask(FCameraTask::EType InTaskType)
{
#if WITH_MLSDK
	if (InTaskType == FCameraTask::EType::Disconnect && GetConnectionStatus() == EConnectionStatus::NotConnected)
	{
		return;
	}

	if (InTaskType == FCameraTask::EType::Connect && GetConnectionStatus() != EConnectionStatus::NotConnected)
	{
		return;
	}

	if (InTaskType != FCameraTask::EType::Connect &&
		InTaskType != FCameraTask::EType::Disconnect &&
		GetConnectionStatus() == EConnectionStatus::NotConnected)
	{
		FCameraTask ConnectTask;
		ConnectTask.CaptureType = FCameraTask::EType::Connect;
		PushNewTask(ConnectTask);
	}
	FCameraTask CaptureImgToFileTask;
	CaptureImgToFileTask.CaptureType = InTaskType;
	PushNewTask(CaptureImgToFileTask);
#endif // WITH_MLSDK
}

bool FCameraRunnable::IsConnected() const
{
	return GetConnectionStatus() == EConnectionStatus::Connected;
}

void FCameraRunnable::Pause()
{
#if WITH_MLSDK
	bWasConnectedOnPause = IsConnected();
	// Cancel the current video recording (if one is active).
	if (CurrentTask.CaptureType == FCameraTask::EType::StartVideoToFile)
	{
		StopRecordingVideo();
		CurrentTask.bSuccess = false;
		PushCompletedTask(CurrentTask);
	}
	// Cancel any incoming tasks.
	CancelIncomingTasks();
	// Disconnect camera if connected
	TryDisconnect();
#endif // WITH_MLSDK
}

void FCameraRunnable::Resume()
{
	if (bWasConnectedOnPause)
	{
		FCameraTask ConnectTask;
		ConnectTask.CaptureType = FCameraTask::EType::Connect;
		PushNewTask(ConnectTask);
	}
}

bool FCameraRunnable::ProcessCurrentTask()
{
	bool bSuccess = false;

#if WITH_MLSDK
	switch (CurrentTask.CaptureType)
	{
	case FCameraTask::EType::None: bSuccess = false; checkf(false, TEXT("Invalid camera task encountered!")); break;
	case FCameraTask::EType::Connect:
	{
		SetConnectionStatus(EConnectionStatus::Connecting);
		bSuccess = TryConnect();
		SetConnectionStatus(bSuccess ? EConnectionStatus::Connected : EConnectionStatus::NotConnected);
	}
	break;
	case FCameraTask::EType::Disconnect: bSuccess = TryDisconnect(); break;
	case FCameraTask::EType::ImageToFile: bSuccess = CaptureImageToFile(); break;
	case FCameraTask::EType::ImageToTexture: bSuccess = CaptureImageToTexture(); break;
	case FCameraTask::EType::StartVideoToFile: bSuccess = StartRecordingVideo(); break;
	case FCameraTask::EType::StopVideoToFile: bSuccess = StopRecordingVideo(); break;
	}

	if (bShuttingDown)
	{
		TryDisconnect();
	}
#endif // WITH_MLSDK
	return bSuccess;
}

#if WITH_MLSDK
void FCameraRunnable::OnPreviewBufferAvailable(MLHandle Output, void *Data)
{
	(void)Data;
	PreviewHandle.Set(static_cast<int64>(Output));
}

bool FCameraRunnable::TryConnect()
{
	if (AppEventHandler.GetPrivilegeStatus(EMagicLeapPrivilege::CameraCapture) != MagicLeap::EPrivilegeState::Granted)
	{
		Log(TEXT("Cannot connect to camera due to lack of privilege!"));
		return false;
	}
	else
	{
		if (bPaused) return false;

		MLResult Result = MLCameraConnect();

		if (bPaused) return false;

		if (Result != MLResult_Ok)
		{
			Log(FString::Printf(TEXT("MLCameraConnect failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
			CancelIncomingTasks();
			return false;
		}
		else
		{
			FMemory::Memset(&DeviceStatusCallbacks, 0, sizeof(MLCameraDeviceStatusCallbacks));
			DeviceStatusCallbacks.on_preview_buffer_available = OnPreviewBufferAvailable;
			Result = MLCameraSetDeviceStatusCallbacks(&DeviceStatusCallbacks, nullptr);
			if (Result != MLResult_Ok)
			{
				Log(FString::Printf(TEXT("MLCameraSetDeviceStatusCallbacks failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
			}
		}
	}

	return true;
}

bool FCameraRunnable::TryDisconnect()
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
	}

	return !IsConnected();
}

bool FCameraRunnable::TryPrepareCapture(MLCameraCaptureType InCaptureType, MLHandle& OutHandle)
{
	MLResult Result = MLCameraPrepareCapture(InCaptureType, &OutHandle);
	// A failure here is likely the result of the third eye capture system being engaged over the top
	// of this application.  In such a case we currently have no way of knowing that our connection has
	// been invalidated by another process.  All we can do is try to disconnect, reconnect and make one
	// final attempt to prepare the required capture.
	if (Result != MLResult_Ok)
	{
		if (!TryDisconnect())
		{
			Log(TEXT("Failed to disconnect after preparation failure"));
			return false;
		}

		if (bPaused) return false;

		if (!TryConnect())
		{
			Log(TEXT("Failed to connect after preparation failure"));
			return false;
		}
	}

	Result = MLCameraPrepareCapture(InCaptureType, &OutHandle);
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraPrepareCapture failed with error %s!  Camera capture aborted!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}

	return true;
}

bool FCameraRunnable::CaptureImageToFile()
{
	if (bPaused || bShuttingDown) return false;

	Log(TEXT("Beginning capture image to file."));
	MLHandle Handle = ML_INVALID_HANDLE;
	if (!TryPrepareCapture(MLCameraCaptureType_Image, Handle))
	{
		return false;
	}

	if (bPaused || bShuttingDown) return false;

#if PLATFORM_LUMIN
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	// This module is only for Lumin so this is fine for now.
	FLuminPlatformFile* LuminPlatformFile = static_cast<FLuminPlatformFile*>(&PlatformFile);
	UniqueFileName = LuminPlatformFile->ConvertToLuminPath(FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("Img_"), *ImgExtension), true);
#endif

	if (bPaused || bShuttingDown) return false;

	MLResult Result = MLCameraCaptureImage(TCHAR_TO_UTF8(*UniqueFileName));
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraCaptureImage failed with error %s!  Camera capture aborted!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}

	Log(FString::Printf(TEXT("Captured image to %s"), *UniqueFileName));
	CurrentTask.FilePath = UniqueFileName;
	return true;
}

bool FCameraRunnable::CaptureImageToTexture()
{
	if (bPaused || bShuttingDown) return false;

	Log(TEXT("Beginning capture image to texture."));
	MLCameraOutput* CameraOutput = nullptr;
	MLHandle Handle = ML_INVALID_HANDLE;
	if (!TryPrepareCapture(MLCameraCaptureType_ImageRaw, Handle))
	{
		return false;
	}

	if (bPaused || bShuttingDown) return false;

	MLResult Result = MLCameraCaptureImageRaw();
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraCaptureImageRaw failed with error %s!  Camera capture aborted!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}

	if (bPaused || bShuttingDown) return false;

	Result = MLCameraGetImageStream(&CameraOutput);
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraGetImageStream failed with error %s!  Camera capture aborted!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}

	if (bPaused || bShuttingDown) return false;

	if (CameraOutput->plane_count == 0)
	{
		Log(TEXT("Invalid plane_count!  Camera capture aborted!"));
		return false;
	}

	MLCameraPlaneInfo& ImageInfo = CameraOutput->planes[0];
	if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(ImageInfo.data, ImageInfo.size))
	{
		TArray<uint8> RawData;
		if (ImageWrapper->GetRaw(ImageWrapper->GetFormat(), 8, RawData))
		{
			Log(FString::Printf(TEXT("ImageWrapper width=%d height=%d size=%" INT64_FMT), ImageWrapper->GetWidth(), ImageWrapper->GetHeight(), RawData.Num()));
			UTexture2D* CaptureTexture = UTexture2D::CreateTransient(ImageWrapper->GetWidth(), ImageWrapper->GetHeight(), EPixelFormat::PF_R8G8B8A8);
			CaptureTexture->AddToRoot();
			FTexture2DMipMap& Mip = CaptureTexture->PlatformData->Mips[0];
			void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Data, RawData.GetData(), Mip.BulkData.GetBulkDataSize());
			Mip.BulkData.Unlock();
			CaptureTexture->UpdateResource();
			CurrentTask.Texture = CaptureTexture;
		}
	}

	return true;
}

bool FCameraRunnable::StartRecordingVideo()
{
	if (bPaused) return false;

	Log(TEXT("Beginning capture video to file."));
	MLHandle Handle = ML_INVALID_HANDLE;
	if (!TryPrepareCapture(MLCameraCaptureType_Video, Handle))
	{
		return false;
	}

	if (bPaused) return false;

	if (AppEventHandler.GetPrivilegeStatus(EMagicLeapPrivilege::AudioCaptureMic) != MagicLeap::EPrivilegeState::Granted)
	{
		Log(TEXT("Cannot capture video due to lack of privilege!"));
		return false;
	}

	if (bPaused) return false;

	if (AppEventHandler.GetPrivilegeStatus(EMagicLeapPrivilege::VoiceInput) != MagicLeap::EPrivilegeState::Granted)
	{
		Log(TEXT("Cannot capture video due to lack of privilege!"));
		return false;
	}

	if (bPaused) return false;

#if PLATFORM_LUMIN
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	// This module is only for Lumin so this is fine for now.
	FLuminPlatformFile* LuminPlatformFile = static_cast<FLuminPlatformFile*>(&PlatformFile);
	UniqueFileName = LuminPlatformFile->ConvertToLuminPath(FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("Vid_"), *VidExtension), true);
#endif
	MLResult Result = MLCameraCaptureVideoStart(TCHAR_TO_UTF8(*UniqueFileName));
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraCaptureVideoStart failed with error %s!  Video capture aborted!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}

	return true;
}

bool FCameraRunnable::StopRecordingVideo()
{
	MLResult Result = MLCameraCaptureVideoStop();
	if (Result != MLResult_Ok)
	{
		Log(FString::Printf(TEXT("MLCameraCaptureVideoStop failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result))));
		return false;
	}
	else
	{
		Log(FString::Printf(TEXT("Captured video to %s"), *UniqueFileName));
	}

	CurrentTask.FilePath = UniqueFileName;
	return true;
}

void FCameraRunnable::Log(const FString& Info)
{
	FCameraTask LogTask;
	LogTask.CaptureType = FCameraTask::EType::Log;
	LogTask.Log = Info;
	PushCompletedTask(LogTask);
	UE_LOG(LogMagicLeapCamera, Log, TEXT("%s"), *Info);
}
#endif //WITH_MLSDK

void FCameraRunnable::SetConnectionStatus(EConnectionStatus InConnectionStatus)
{
	ThreadSafeConnectionStatus.Set(static_cast<int32>(InConnectionStatus));
}

FCameraRunnable::EConnectionStatus FCameraRunnable::GetConnectionStatus() const
{
	return static_cast<EConnectionStatus>(ThreadSafeConnectionStatus.GetValue());
}

const TCHAR* FCameraRunnable::ConnectionStatusToString(EConnectionStatus InConnectionStatus)
{
	const TCHAR* ConnectionStatusString = TEXT("Invalid");
	switch (InConnectionStatus)
	{
	case EConnectionStatus::NotConnected: ConnectionStatusString = TEXT("NotConnected"); break;
	case EConnectionStatus::Connecting: ConnectionStatusString = TEXT("Connecting"); break;
	case EConnectionStatus::Connected: ConnectionStatusString = TEXT("Connected"); break;
	}

	return ConnectionStatusString;
}
