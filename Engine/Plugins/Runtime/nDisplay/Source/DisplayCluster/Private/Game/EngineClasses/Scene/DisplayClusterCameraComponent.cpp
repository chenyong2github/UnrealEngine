// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterCameraComponent.h"
#include "Camera/CameraComponent.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterStrings.h"


UDisplayClusterCameraComponent::UDisplayClusterCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, InterpupillaryDistance(6.4f)
	, bSwapEyes(false)
	, StereoOffset(EDisplayClusterEyeStereoOffset::None)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;
}

void UDisplayClusterCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	const UDisplayClusterConfigurationSceneComponentCamera* CfgCamera = Cast<UDisplayClusterConfigurationSceneComponentCamera>(GetConfigParameters());
	if (CfgCamera)
	{
		InterpupillaryDistance = CfgCamera->InterpupillaryDistance;
		bSwapEyes = CfgCamera->bSwapEyes;

		switch (CfgCamera->StereoOffset)
		{
		case EDisplayClusterConfigurationEyeStereoOffset::Left:
			StereoOffset = EDisplayClusterEyeStereoOffset::Left;
			break;

		case EDisplayClusterConfigurationEyeStereoOffset::None:
			StereoOffset = EDisplayClusterEyeStereoOffset::None;
			break;

		case EDisplayClusterConfigurationEyeStereoOffset::Right:
			StereoOffset = EDisplayClusterEyeStereoOffset::Right;
			break;

		default:
			StereoOffset = EDisplayClusterEyeStereoOffset::None;
			break;
		}
	}
}
