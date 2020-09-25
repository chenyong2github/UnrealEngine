// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterXformComponent.h"


UDisplayClusterXformComponent::UDisplayClusterXformComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;
}

void UDisplayClusterXformComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UDisplayClusterXformComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UDisplayClusterXformComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
