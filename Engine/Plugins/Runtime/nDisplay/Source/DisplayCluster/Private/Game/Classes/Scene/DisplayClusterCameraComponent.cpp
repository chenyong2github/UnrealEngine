// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterCameraComponent.h"
#include "Config/DisplayClusterConfigTypes.h"

#include "Camera/CameraComponent.h"

#include "CoreGlobals.h"


UDisplayClusterCameraComponent::UDisplayClusterCameraComponent(const FObjectInitializer& ObjectInitializer)
	: UDisplayClusterSceneComponent(ObjectInitializer)
	, EyeDist(0.064f)
	, bEyeSwap(false)
	, ForceEyeOffset(0)
{
	PrimaryComponentTick.bCanEverTick = false;
}


void UDisplayClusterCameraComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UDisplayClusterCameraComponent::SetSettings(const FDisplayClusterConfigSceneNode* ConfigData)
{
	check(ConfigData);
	if (ConfigData)
	{
		const FDisplayClusterConfigCamera* const CameraCfg = static_cast<const FDisplayClusterConfigCamera*>(ConfigData);

		EyeDist        = CameraCfg->EyeDist;
		bEyeSwap       = CameraCfg->EyeSwap;
		ForceEyeOffset = CameraCfg->ForceOffset;
	}
	

	Super::SetSettings(ConfigData);
}

bool UDisplayClusterCameraComponent::ApplySettings()
{
	return Super::ApplySettings();
}
