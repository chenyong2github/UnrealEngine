// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MagicLeapRunnable.h"
#include "MagicLeapCameraTypes.h"
#include "MagicLeapCVCameraTypes.h"
#include "HAL/ThreadSafeCounter.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Lumin/CAPIShims/LuminAPICamera.h"
#include "Lumin/CAPIShims/LuminAPICVCamera.h"

struct FCVCameraTask : public FMagicLeapTask
{
	enum class EType : uint32
	{
		None,
		Connect,
		Disconnect,
		Enable,
		Disable,
		Log,
	};

	EType CaptureType;
	FString FilePath;
	FString Log;
	class UTexture2D* Texture;

	FCVCameraTask()
		: CaptureType(EType::None)
		, Texture(nullptr)
	{
	}
};

class FCVCameraRunnable : public FMagicLeapRunnable<FCVCameraTask>
{
public:
	FCVCameraRunnable();
	void Exit() override;
	void PushNewCaptureTask(FCVCameraTask::EType InTaskType);
	bool IsConnected() const;
	bool GetIntrinsicCalibrationParameters(FMagicLeapCVCameraIntrinsicCalibrationParameters& OutParams);
	bool GetFramePose(FTransform& OutFramePose);
	bool GetCameraOutput(FMagicLeapCameraOutput& OutCameraOutput);

protected:
	void Pause() override;
	void Resume() override;

private:
	bool ProcessCurrentTask() override;
#if WITH_MLSDK
	static void OnRawBufferAvailable(const MLCameraOutput* Output, const MLCameraResultExtras* Extra, const MLCameraFrameMetadata* Metadata, void* Context);
	bool TryConnect(bool bConnectCV);
	bool TryDisconnect();
	bool StartCaptureRawVideo();
	bool StopCaptureRawVideo();
	void Log(const FString& InLogMsg);

	void MLToUEIntrinsicCalibrationParameters(const MLCVCameraIntrinsicCalibrationParameters& InMLParams,
		FMagicLeapCVCameraIntrinsicCalibrationParameters& OutUEParams);
	void MLToUECameraOutput(const MLCameraOutput& InMLCameraOutput, FMagicLeapCameraOutput& OutUECameraOutput);
	EPixelFormat MLToUEPixelFormat(MLCameraOutputFormat MLPixelFormat);

	MLHandle HeadTracker;
	MLHandle CVCameraTracker;
	FMagicLeapCameraOutput CVCameraOutput;
	MLCameraResultExtras CVCameraExtras;
	MLCameraFrameMetadata CVCameraMetadata;
	MLCameraCaptureCallbacksEx CaptureCallbacks;
#endif //WITH_MLSDK
	FCVCameraTask::EType CurrentTaskType;
	enum class EConnectionStatus : int32
	{
		NotConnected,
		Connecting,
		Connected
	};
	FThreadSafeCounter ThreadSafeConnectionStatus;
	bool bWasConnectedOnPause;
	TSharedPtr<IImageWrapper> ImageWrapper;
	FCriticalSection CriticalSection;

	void SetConnectionStatus(EConnectionStatus ConnectionStatus);
	EConnectionStatus GetConnectionStatus() const;
};
