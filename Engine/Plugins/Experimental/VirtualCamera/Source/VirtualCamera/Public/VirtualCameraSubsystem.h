// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "IVirtualCameraController.h"
#include "Subsystems/EngineSubsystem.h"

#include "VirtualCameraSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStreamStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStreamStopped);

UCLASS(BlueprintType, Category = "VirtualCamera", DisplayName = "VirtualCameraSubsystem")
class VIRTUALCAMERA_API UVirtualCameraSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
	
public:

	UVirtualCameraSubsystem();

public:
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Streaming")
	bool StartStreaming();

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Streaming")
	bool StopStreaming();

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Streaming")
	bool IsStreaming() const;

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	TScriptInterface<IVirtualCameraController> GetVirtualCameraController() const;
	
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void SetVirtualCameraController(TScriptInterface<IVirtualCameraController> VirtualCamera);

	UPROPERTY(BlueprintReadOnly, Category = "VirtualCamera")
	ULevelSequencePlaybackController* SequencePlaybackController;

	UPROPERTY(BlueprintAssignable, Category = "VirtualCamera | Streaming")
	FOnStreamStarted OnStreamStartedDelegate;

	UPROPERTY(BlueprintAssignable, Category = "VirtualCamera | Streaming")
	FOnStreamStopped OnStreamStoppedDelegate;
 
private:

	UPROPERTY(Transient) 
	TScriptInterface<IVirtualCameraController> ActiveCameraController;

	bool bIsStreaming;
};
