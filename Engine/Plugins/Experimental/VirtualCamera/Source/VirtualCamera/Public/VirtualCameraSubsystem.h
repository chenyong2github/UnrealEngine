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

	UFUNCTION(BlueprintCallable, Category = "Virtual Camera")
	TScriptInterface<IVirtualCameraController> GetVirtualCameraController() const { return ActiveCameraController; }
	
	UFUNCTION(BlueprintCallable, Category = "Virtual Camera")
	void SetVirtualCameraController(TScriptInterface<IVirtualCameraController> VirtualCamera);
	
private:

	UPROPERTY(Transient)
	TScriptInterface<IVirtualCameraController> ActiveCameraController;
};
