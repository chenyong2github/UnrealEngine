// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MagicLeapRunnable.h"
#include "HAL/ThreadSafeCounter.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Lumin/CAPIShims/LuminAPICamera.h"

struct FCameraTask : public FMagicLeapTask
{
	enum class EType : uint32
	{
		None,
		Connect,
		Disconnect,
		ImageToFile,
		ImageToTexture,
		StartVideoToFile,
		StopVideoToFile,
		Log,
	};

	EType CaptureType;
	FString FilePath;
	FString Log;
	class UTexture2D* Texture;

	FCameraTask()
		: CaptureType(EType::None)
		, Texture(nullptr)
	{
	}

	static const TCHAR* TaskTypeToString(EType InTaskType)
	{
		const TCHAR* TaskTypeString = TEXT("Invalid");
		switch (InTaskType)
		{
			case EType::None: TaskTypeString = TEXT("None"); break;
			case EType::Connect: TaskTypeString = TEXT("Connect"); break;
			case EType::Disconnect: TaskTypeString = TEXT("Disconnect"); break;
			case EType::ImageToFile: TaskTypeString = TEXT("ImageToFile"); break;
			case EType::ImageToTexture: TaskTypeString = TEXT("ImageToTexture"); break;
			case EType::StartVideoToFile: TaskTypeString = TEXT("StartVideoToFile"); break;
			case EType::StopVideoToFile: TaskTypeString = TEXT("StopVideoToFile"); break;
			case EType::Log: TaskTypeString = TEXT("Log"); break;
		}

		return TaskTypeString;
	}
};

class FCameraRunnable : public FMagicLeapRunnable<FCameraTask>
{
public:
	FCameraRunnable();
	void PushNewCaptureTask(FCameraTask::EType InTaskType);
	bool IsConnected() const;

	static FThreadSafeCounter64 PreviewHandle;

protected:
	void Pause() override;
	void Resume() override;

private:
	bool ProcessCurrentTask() override;
#if WITH_MLSDK
	static void OnPreviewBufferAvailable(MLHandle Output, void *Data);
	bool TryConnect();
	bool TryDisconnect();
	bool TryPrepareCapture(MLCameraCaptureType InCaptureType, MLHandle& OutHandle);
	bool CaptureImageToFile();
	bool CaptureImageToTexture();
	bool StartRecordingVideo();
	bool StopRecordingVideo();
	void Log(const FString& InLogMsg);

	MLCameraDeviceStatusCallbacks DeviceStatusCallbacks;
#endif //WITH_MLSDK
	FCameraTask::EType CurrentTaskType;
	enum class EConnectionStatus : int32
	{
		NotConnected,
		Connecting,
		Connected
	};
	FThreadSafeCounter ThreadSafeConnectionStatus;
	bool bWasConnectedOnPause;
	const FString ImgExtension;
	const FString VidExtension;
	FString UniqueFileName;
	TSharedPtr<IImageWrapper> ImageWrapper;

	void SetConnectionStatus(EConnectionStatus ConnectionStatus);
	EConnectionStatus GetConnectionStatus() const;
	const TCHAR* ConnectionStatusToString(EConnectionStatus InConnectionStatus);
};
