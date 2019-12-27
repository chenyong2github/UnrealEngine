// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformProcess.h"

#include "Lumin/CAPIShims/LuminAPICoordinateFrameUID.h"
#include "CoreMinimal.h"
#include "AppEventHandler.h"
#include "AsyncDestroyer.h"
#include "IMagicLeapPlugin.h"

class FMagicLeapHMD;
struct FTrackingFrame;

class MAGICLEAP_API FAppFramework
{
public:
	FAppFramework();
	~FAppFramework();

	void Startup();
	void Shutdown();

	void BeginUpdate();

	void ApplicationPauseDelegate();
	void ApplicationResumeDelegate();
	void OnApplicationShutdown();

	void OnDeviceActive();
	void OnDeviceRealityMode();
	void OnDeviceStandby();

	FTransform GetDisplayCenterTransform() const { return FTransform::Identity; }; // HACK
	uint32 GetViewportCount() const;

	float GetWorldToMetersScale() const;
	FTransform GetCurrentFrameUpdatePose() const;
#if WITH_MLSDK
	bool GetTransform(const MLCoordinateFrameUID& Id, FTransform& OutTransform, EMagicLeapTransformFailReason& OutReason) const;
#endif //WITH_MLSDK

	static void AddEventHandler(MagicLeap::IAppEventHandler* InEventHandler);
	static void RemoveEventHandler(MagicLeap::IAppEventHandler* InEventHandler);
	static bool AsyncDestroy(MagicLeap::IAppEventHandler* InEventHandler);

private:
	const FTrackingFrame* GetCurrentFrame() const;
	const FTrackingFrame* GetOldFrame() const;
	void PauseRendering(bool bPause);

	bool bInitialized = false;

	float saved_max_fps_;

	static TArray<MagicLeap::IAppEventHandler*> EventHandlers;
	static FCriticalSection EventHandlersCriticalSection;
	static MagicLeap::FAsyncDestroyer* AsyncDestroyer;
};

// TODO: pull this out of here
DEFINE_LOG_CATEGORY_STATIC(LogMagicLeap, Log, All);
