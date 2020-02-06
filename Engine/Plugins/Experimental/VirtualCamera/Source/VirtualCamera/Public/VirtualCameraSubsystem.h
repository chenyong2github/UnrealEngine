// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IVirtualCameraController.h"
#include "Subsystems/EngineSubsystem.h"

#include "VirtualCameraSubsystem.generated.h"

UCLASS()
class VIRTUALCAMERA_API UVirtualCameraSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
	
public:

	UVirtualCameraSubsystem();

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

private:

	UPROPERTY(Transient)
	TScriptInterface<IVirtualCameraController> ActiveCameraController;

	bool bIsStreaming;
};
