// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterCameraComponent.h"
#include "Config/DisplayClusterConfigTypes.h"

#include "CoreGlobals.h"


UDisplayClusterCameraComponent::UDisplayClusterCameraComponent(const FObjectInitializer& ObjectInitializer)
	: UDisplayClusterSceneComponent(ObjectInitializer)
	, EyeDist(0.064f)
	, bEyeSwap(false)
	, ForceEyeOffset(0)
	, NearClipPlane(GNearClippingPlane)
	, FarClipPlane(2000000.f)
{
	PrimaryComponentTick.bCanEverTick = true;
}


void UDisplayClusterCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	
	// ...
	
}

void UDisplayClusterCameraComponent::TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent( DeltaTime, TickType, ThisTickFunction );

	// ...
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
