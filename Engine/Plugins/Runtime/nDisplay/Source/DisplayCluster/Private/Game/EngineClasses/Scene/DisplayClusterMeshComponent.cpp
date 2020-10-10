// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterMeshComponent.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterGlobals.h"


UDisplayClusterMeshComponent::UDisplayClusterMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;

	const UDisplayClusterConfigurationSceneComponentMesh* CfgMesh = Cast<UDisplayClusterConfigurationSceneComponentMesh>(GetConfigParameters());
	if (CfgMesh)
	{
		AssetPath = CfgMesh->AssetPath;
	}
}

void UDisplayClusterMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	// todo mesh initialization
}

void UDisplayClusterMeshComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UDisplayClusterMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
